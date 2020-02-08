#include "stdafx.h"

#include "mosquittopp.h"

#ifdef HAVE_JSON_JSON_H
#	include "json/json.h"
#elif defined(HAVE_JSONCPP_JSON_JSON_H)
#	include "jsoncpp/json/json.h"
#else 
#	error josn.h not found
#endif

#include "../libs/libwb/WBDevice.h"


#include "MqttConnection.h"


CTasmotaWBDevice::CTasmotaWBDevice(string Name, string Description)
	:wbDevice(Name, Description), relayCount(-1), isShutter(0) {		
};


CMqttConnection::CMqttConnection(CConfigItem config, CLog* log)
	:m_isConnected(false), mosquittopp("Tasmota2Wb"), m_bStop(false)
{
	
	m_Server = config.getStr("mqtt/host", false, "wirenboard");

	CConfigItemList groupList;
	config.getList("mqtt/groups", groupList);
	for_each_const(CConfigItemList, groupList, group)
	{
		string name = (*group)->getStr("");
		m_Groups.push_back(name);
	}

	if (m_Groups.size() == 0) m_Groups.push_back("tasmotas");

	m_Log = log;
	m_Log->Printf(0, "Starting tasmota2wb");

	connect(m_Server.c_str());
	loop_start();

}

CMqttConnection::~CMqttConnection()
{
	loop_stop(true);
}

void CMqttConnection::sendCommand(string device, string cmd, string data) {
	string topic = "cmnd/"+device+"/"+cmd;
	publish(topic, data);
}

void CMqttConnection::on_connect(int rc)
{
	m_Log->Printf(1, "mqtt::on_connect(%d)", rc);

	if (!rc)
	{
		m_isConnected = true;
	}

	for_each_const(CTasmotaWBDeviceMap, m_Devices, device) {
		string topic = "/devices/"+device->first+"/controls/+/on";
		m_Log->Printf(5, "subscribe to %s", topic.c_str());
		subscribe(topic);
	}

	subscribe("tele/#");
	subscribe("stat/#");
	for_each_const(string_vector, m_Groups, group) {
		publish("cmnd/"+(*group)+"/Status", "0");
		publish("cmnd/"+(*group)+"/SetOption80", "");
	}
	m_ActiveCommand = SetOption80;

}

void CMqttConnection::on_disconnect(int rc)
{
	m_isConnected = false;
	m_Log->Printf(1, "mqtt::on_disconnect(%d)", rc);
}

void Parse(string str, Json::Value &obj){
	strstream stream;
	stream<<str;
	stream>>obj;	
}

void CMqttConnection::on_message(const struct mosquitto_message *message)
{
	string payload;

	//m_Log->Printf(3, "%s::on_message(%s, %d bytes)", m_sName.c_str(), message->topic, message->payloadlen);

	if (message->payloadlen && message->payload)
	{
		char* payload_str = new char[message->payloadlen+1];
		memcpy(payload_str, message->payload, message->payloadlen);
		payload_str[message->payloadlen] = 0;
		//payload.assign((const char*)message->payload, message->payloadlen);
		//payload += "\0";
		payload = payload_str;
		//m_Log->Printf(3, "%s", payload_str);
		delete []payload_str;
	}
	
	m_Log->Printf(5, "CMqttConnection::on_message(%s=%s)", message->topic, payload.c_str());
	
	try
	{
		string_vector v;
		SplitString(message->topic, '/', v);
		if (v.size() < 3)
			return;

		string device = v[1];

		if ((v[0] == "tele" && v[2]=="STATE")) {
			if (m_Devices.find(device)!=m_Devices.end()) {
				CTasmotaWBDevice *dev = m_Devices[device];
				Json::Value obj; Parse(payload, obj);
				for(int i=0;i<5;i++) {
					string controlName = string("K")+itoa(i+1);
					string powerName = dev->relayCount==1 ? "POWER" : string("POWER")+itoa(i+1);
					string val = obj[powerName].asString();
					if (dev->wbDevice.controlExists(controlName) && val.length()) {
						string value = val=="ON"?"1":"0";
						dev->wbDevice.set(controlName, value);
						m_Log->Printf(5, "%s->%s", controlName.c_str(), value.c_str());
					}

				}
				SendUpdate();	
			} else if (m_ActiveCommand==None) {
				publish("cmnd/"+(v[1])+"/Status", "0");
				publish("cmnd/"+(v[1])+"/SetOption80", "0");
				m_ActiveCommand=SetOption80;
			}
		} else if (v[0] == "stat" && (v[2].find("STATUS")==0 || v[2]=="RESULT")) {
			string field = v[2];
			if (field=="RESULT") {
				if (m_ActiveCommand==SetOption80) field = "SetOption80";
				else if (m_ActiveCommand==None) {
				}
				else return;
			}

			CTasmotaWBDevice *dev;
			if (m_Devices.find(device)==m_Devices.end())
			{
				dev = new CTasmotaWBDevice(device, device); 
				m_Devices[device] = dev;			
			} else dev = m_Devices[device];

			dev->params[field] = payload;

			if (dev->wbDevice.getControls()->size()==0 &&
					dev->params.find("STATUS")!=dev->params.end() &&
					dev->params.find("SetOption80")!=dev->params.end() &&
					dev->params.find("STATUS11")!=dev->params.end()) {
				Json::Value status; Parse(dev->params["STATUS"], status);
				string desc;
				if (status.isMember("Status") && status["Status"].isMember("FriendlyName") && 
					status["Status"]["FriendlyName"].isArray()) {
					desc = status["Status"]["FriendlyName"][0].asString();
					dev->wbDevice.enrichDevice("name", desc);
				}

				Json::Value SetOption80; Parse(dev->params["SetOption80"], SetOption80);
				if (SetOption80.isMember("SetOption80") && SetOption80["SetOption80"]=="ON") {
					dev->isShutter = true;
				}

				Json::Value status11; Parse(dev->params["STATUS11"], status11);
				if (status11.isMember("StatusSTS")) {
					status11 = status11["StatusSTS"];
					int relayCount=0;
					for (; relayCount<16; relayCount++) {
						string tsmCtl = "POWER"+itoa(relayCount+1);
						if (relayCount==0 && status11.isMember("POWER")) tsmCtl = "POWER";
						else if (!status11.isMember(tsmCtl)) break;
						if (dev->isShutter && relayCount<2) {
							if (relayCount==0) {
								dev->wbDevice.addControl("Up", CWBControl::PushButton, false);
								dev->wbDevice.addControl("Down", CWBControl::PushButton, false);
								dev->wbDevice.addControl("Stop", CWBControl::PushButton, false);
							}
						} else {
							string wbCtl = "K"+itoa(relayCount+1);
							dev->wbDevice.addControl(wbCtl, CWBControl::Switch, false);
							bool power = status11[tsmCtl]=="ON";
							dev->wbDevice.set(wbCtl, power?"1":"0");
							m_Log->Printf(4, "Add %s with %s", (device+"/"+wbCtl).c_str(), power?"ON":"OFF");
						}
					}
					if (relayCount>0) {
						dev->relayCount = relayCount;
					}
				}
				
				m_Log->Printf(0, "Create %s(%s). %d relays%s", device.c_str(), desc.c_str(), dev->relayCount, dev->isShutter?" with shuter":"");
				CreateDevice(dev);
				SendUpdate();
				string topic = "/devices/"+device+"/controls/+/on"; 
				m_Log->Printf(5, "subscribe to %s", topic.c_str());
				subscribe(topic.c_str());
				dev->params.empty();
			} else {
				Json::Value obj; Parse(payload, obj);

				for(int i=0;i<dev->relayCount;i++) {
					string wbCtl = "K"+itoa(i+1);
					string tsmCtl = dev->relayCount==1 ? "POWER" : "POWER"+itoa(i+1);
					if (obj.isMember(tsmCtl) && dev->wbDevice.controlExists(wbCtl)) {
				 		bool power = obj[tsmCtl]=="ON";
						m_Log->Printf(4, "Copy %s.%s=%s to %s", device.c_str(), tsmCtl.c_str(), power?"on":"off", wbCtl.c_str());
						dev->wbDevice.set(wbCtl, power?"1":"0");
					}
				}
				SendUpdate();
			}
		} 

		if (v[0] == "tele") {
			if (v[2]=="LWT" && payload=="Online") { // Device online
				sendCommand(device, "status");
			}
		} else if (v[0] == "stat") {
			if (v[2]=="STATUS") {
				if (m_Devices.find(device)!=m_Devices.end()) {
				} 
			} else if (v[2]=="RESULT" || v[2]=="STATE") {
			}
		}  else if (v[0] == "" && v[1]=="devices") {
			string device = v[2];
			if (m_Devices.find(device)!=m_Devices.end()) {
				CTasmotaWBDevice *dev= m_Devices[device]; 
				if (v.size()==6 && v[3]=="controls" && v[5]=="on")
				{
					if (v[4][0]=='K') {
						int relay = atoi(v[4].c_str()+1);
						if (relay>(dev->isShutter?2:0) && relay<5)
						{
							string cmd = dev->relayCount==1 ? "POWER" : string("POWER")+itoa(relay);
							sendCommand(device, cmd, payload);
						}
					} else if(v[4]=="Down") {
						sendCommand(device, "POWER1", "0");
						sendCommand(device, "POWER2", "1");
					} else if(v[4]=="Up") {
						sendCommand(device, "POWER2", "0");
						sendCommand(device, "POWER1", "1");
					} else if(v[4]=="Stop") {
						sendCommand(device, "POWER2", "0");
						sendCommand(device, "POWER1", "0");
					}
				}
			}
		}
	}
	catch (CHaException ex)
	{
		m_Log->Printf(0, "Exception %s (%d)", ex.GetMsg().c_str(), ex.GetCode());
	}	
}

void CMqttConnection::on_log(int level, const char *str)
{
	m_Log->Printf(10, "mqtt::on_log(%d, %s)", level, str);
}

void CMqttConnection::on_error()
{
	m_Log->Printf(1, "mqtt::on_error()");
}

void CMqttConnection::CreateDevice(CTasmotaWBDevice* dev)
{
	m_Devices[dev->wbDevice.getName()] = dev;
	string_map v;
	dev->wbDevice.createDeviceValues(v);
	for_each(string_map, v, i)
	{
		publish(i->first, i->second, true);
		m_Log->Printf(5, "publish %s=%s", i->first.c_str(), i->second.c_str());
	}
}

void CMqttConnection::SendUpdate()
{
	string_map v;

	for_each(CTasmotaWBDeviceMap, m_Devices, dev)
	{
		if (dev->second)
			dev->second->wbDevice.updateValues(v);
	}

	for_each(string_map, v, i)
	{
		publish(i->first, i->second, true);
		m_Log->Printf(5, "publish %s=%s", i->first.c_str(), i->second.c_str());
	}
}

void CMqttConnection::subscribe(const string &topic) {
	mosqpp::mosquittopp::subscribe(NULL, topic.c_str());
}

void CMqttConnection::publish(const string &topic, const string &payload, bool retain) {
	mosqpp::mosquittopp::publish(NULL, topic.c_str(), payload.length(), payload.c_str(), 0, retain);
}
