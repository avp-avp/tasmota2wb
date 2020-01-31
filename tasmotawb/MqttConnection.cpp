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


Template::Template()
	:relayCount(0), curtain(false)
{

}
Template::Template(int _relayCount, bool _curtain)
	:relayCount(_relayCount), curtain(_curtain)
{

}


CMqttConnection::CMqttConnection(CConfigItem config, CLog* log)
	:m_isConnected(false), mosquittopp("TasmotaWb"), m_bStop(false)
{
	m_Server = config.getStr("mqtt/host", false, "wirenboard");
	
	CConfigItemList templateList;
	config.getList("templates", templateList);
	for_each_const(CConfigItemList, templateList, tmpl)
	{
		string name = (*tmpl)->getStr("name");
		int relayCount = (*tmpl)->getInt("relayCount", false, 1);
		int curtain = (*tmpl)->getInt("curtain", false, false);
		m_Templates[name] = Template(relayCount, curtain);

	}

	m_Log = log;

	connect(m_Server.c_str());
	loop_start();

}

CMqttConnection::~CMqttConnection()
{
	loop_stop(true);
}

void CMqttConnection::sendCommand(string device, string cmd, string data) {
	string topic = "cmnd/"+device+"/"+cmd;
	publish(NULL, topic.c_str(), data.length(), data.c_str());
}

void CMqttConnection::on_connect(int rc)
{
	m_Log->Printf(1, "mqtt::on_connect(%d)", rc);

	if (!rc)
	{
		m_isConnected = true;
	}

	subscribe(NULL, "tele/#");
	subscribe(NULL, "stat/#");
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

		if ((v[0] == "tele" && v[2]=="STATE") || (v[0] == "stat" && v[2]=="RESULT")) {
			if (m_Devices.find(device)!=m_Devices.end()) {
				CWBDevice *dev = m_Devices[device];
				Json::Value obj; Parse(payload, obj);
				for(int i=0;i<5;i++) {
					string controlName = string("K")+itoa(i+1);
					string powerName = string("POWER")+itoa(i+1);
					string val = obj[powerName].asString();
					if (dev->controlExists(controlName) && val.length()) {
						string value = val=="ON"?"1":"0";
						dev->set(controlName, value);
						m_Log->Printf(5, "%s->%s", controlName.c_str(), value.c_str());
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
				} else {
					Json::Value obj; Parse(payload, obj);

					if (m_Templates.find(device)!=m_Templates.end()){
						Json::Value friendlyName = obj["Status"]["FriendlyName"];
						int relayCount = m_Templates[device].relayCount;
						if (friendlyName.isArray()) friendlyName = friendlyName[0];
						m_Log->Printf(5, "Module:%d, Name:%s, relayCount:%d", obj["Status"]["Module"].asInt(), friendlyName.asCString(), relayCount);
						
						CWBDevice *dev = new CWBDevice(device, friendlyName.asCString()); 
						int i=0;
						if(m_Templates[device].curtain) {
							i=2;
							dev->addControl("Up", CWBControl::PushButton, false);
							dev->addControl("Down", CWBControl::PushButton, false);
							dev->addControl("Stop", CWBControl::PushButton, false);
						}
		                CreateDevice(dev);
						for (;i<relayCount;i++) {
							string control = string("K")+itoa(i+1);
							m_Log->Printf(5, "Add %s", control.c_str());
							dev->addControl(control, CWBControl::Switch, false);
						}
		                CreateDevice(dev);
		            	string topic = "/devices/"+device+"/controls/+/on"; //dev->getTopic(control)+"/on";
						m_Log->Printf(5, "subscribe to %s", topic.c_str());
		            	subscribe(NULL, topic.c_str());
						sendCommand(device, "state");
					}
				}
			} else if (v[2]=="RESULT" || v[2]=="STATE") {
			}
		}  else if (v[0] == "" && v[1]=="devices") {
			string device = v[2];
			if (m_Devices.find(device)!=m_Devices.end()) {
				if (v.size()==6 && v[3]=="controls" && v[5]=="on")
				{
					if (v[4][0]=='K') {
						int relay = atoi(v[4].c_str()+1);
						if (relay>(m_Templates[device].curtain?2:0) && relay<5)
						{
							string cmd = string("POWER")+itoa(relay);
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

void CMqttConnection::CreateDevice(CWBDevice* dev)
{
	m_Devices[dev->getName()] = dev;
	string_map v;
	dev->createDeviceValues(v);
	for_each(string_map, v, i)
	{
		publish(NULL, i->first.c_str(), i->second.size(), i->second.c_str(), 0, true);
		m_Log->Printf(5, "publish %s=%s", i->first.c_str(), i->second.c_str());
	}
}

void CMqttConnection::SendUpdate()
{
	string_map v;

	for_each(CWBDeviceMap, m_Devices, dev)
	{
		if (dev->second)
			dev->second->updateValues(v);
	}

	for_each(string_map, v, i)
	{
		publish(NULL, i->first.c_str(), i->second.size(), i->second.c_str(), 0, true);
		m_Log->Printf(5, "publish %s=%s", i->first.c_str(), i->second.c_str());
	}
}
