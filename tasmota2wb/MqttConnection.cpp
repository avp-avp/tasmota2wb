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

const char g_tasmotaMinimal[] = "http://thehackbox.org/tasmota/release/tasmota-minimal.bin";

CTasmotaWBDevice::CTasmotaWBDevice(string Name, string Description)
	:wbDevice(Name, Description), relayCount(-1), isShutter(0), upgradeState(0	) {	
};

CSensorType::CSensorType(const CConfigItem* cfg){
	path = cfg->getStr("path");
	string_vector v; SplitString(path, '/', v);
	name = cfg->getStr("name");
	type = CWBControl::getType(cfg->getStr("type"));
}


CMqttConnection::CMqttConnection(CConfigItem config, CLog* log)
	:m_isConnected(false), mosquittopp(PACKAGE_STRING __TIME__), m_bStop(false),
	m_tasmotaDevice("tasmota", "Tasmota")
{
	time(&m_lastIdle);
	m_tasmotaDevice.addControl("log");
	m_tasmotaDevice.addControl("ip");
	m_tasmotaDevice.addControl("upgrade", CWBControl::Text, true);
	m_Server = config.getStr("mqtt/host", false, "wirenboard");

	CConfigItemList groupList;
	config.getList("mqtt/groups", groupList);
	for_each_const(CConfigItemList, groupList, group)
	{
		string name = (*group)->getStr("");
		m_Groups.push_back(name);
	}

	CConfigItemList sensorList;
	config.getList("general/sensors", sensorList);
	for_each_const(CConfigItemList, sensorList, sensor)
	{
		CSensorType s(*sensor);
		m_SensorTypeList.push_back(s);
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

	subscribe("/devices/tasmota/controls/+/on");
	subscribe("tele/#");
	subscribe("stat/#");
	for_each_const(string_vector, m_Groups, group) {
		//publish("cmnd/"+(*group)+"/Status", "0");
		//publish("cmnd/"+(*group)+"/SetOption80", "");
	}
	CreateDevice(&m_tasmotaDevice);
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

	//m_Log->Printf(3, "%s::on_message(%s, %d  ytes)", m_sName.c_str(), message->topic, message->payloadlen);

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

		string deviceName = v[1];
		CTasmotaWBDevice *tasmotaDevice = NULL;
		if (v[0] == "tele" || v[0] == "stat") {
			if (m_Devices.find(deviceName)!=m_Devices.end()) {
				tasmotaDevice = m_Devices[deviceName];
				tasmotaDevice->lastMessage = time(NULL);
				if (tasmotaDevice->wbDevice.getS("ip")=="Offline"){
					tasmotaDevice->wbDevice.set("ip", tasmotaDevice->ip);
					SendUpdate();
				}
			}
		}

		if (v[0] == "tele") {
			if(v[2]=="LWT" && payload=="Online") 
				queryDevice(deviceName);
			else if (!tasmotaDevice)
				return;
			else if(v[2]=="STATE") {
				if (tasmotaDevice->upgradeState==3) {
					sendCommand(tasmotaDevice->wbDevice.getName(), "OtaUrl", tasmotaDevice->OtaUrl);
					sendCommand(tasmotaDevice->wbDevice.getName(), "Upgrade", "1");
					m_Log->Printf(0, "Upgrade to minimal finished");
					tasmotaDevice->upgradeState=4; 
				} else if (tasmotaDevice->upgradeState==5) {
					m_Log->Printf(0, "Upgrade finished");
					tasmotaDevice->upgradeState = 0;
				}

				Json::Value obj; Parse(payload, obj);
				for(int i=0;i<5;i++) {
					string controlName = string("K")+itoa(i+1);
					string powerName = tasmotaDevice->relayCount==1 ? "POWER" : string("POWER")+itoa(i+1);
					string val = obj[powerName].asString();
					if (tasmotaDevice->wbDevice.controlExists(controlName) && val.length()) {
						string value = val=="ON"?"1":"0";
						tasmotaDevice->wbDevice.set(controlName, value);
						m_Log->Printf(5, "%s->%s", controlName.c_str(), value.c_str());
					}
				}
				SendUpdate();	
			} else if(v[2]=="SENSOR") {
				Json::Value obj; Parse(payload, obj);
				bool needCreate = false;

				for_each_const(CSensorTypeList, m_SensorTypeList, sensor) {
					string_vector v;
					SplitString(sensor->path, '/', v);

					Json::Value obj_path = obj;
					for_each(string_vector, v, p) {
						if(obj_path.isMember(*p)) {
							obj_path = obj_path[*p];
						} else {
							obj_path = Json::Value::null;
							break;
						}
					}

					if (!obj_path.isNull()) {
						if(!tasmotaDevice->wbDevice.controlExists(sensor->name)) {
							tasmotaDevice->wbDevice.addControl(sensor->name, sensor->type, true);
							needCreate = true;
						}

						if(obj_path.isInt())
							tasmotaDevice->wbDevice.set(sensor->name, obj_path.asInt());
						if(obj_path.isDouble()) 
							tasmotaDevice->wbDevice.set(sensor->name, obj_path.asDouble());
						else
							tasmotaDevice->wbDevice.set(sensor->name, obj_path.asString());
						
					}
				}
				if (needCreate) CreateDevice(tasmotaDevice);
				SendUpdate();
				/*
				for_each_const(CSensorTypeList, m_SensorTypeList, sensor) {
					if(obj.isMember(sensor->pathSensor) && obj[sensor->pathSensor].isMember(sensor->pathValue) && tasmotaDevice) {
						if(!tasmotaDevice->wbDevice.controlExists(sensor->name)) {
							tasmotaDevice->wbDevice.addControl(sensor->name, sensor->type, true);
							CreateDevice(tasmotaDevice);
						}

						if(obj[sensor->pathSensor][sensor->pathValue].isInt())
							tasmotaDevice->wbDevice.set(sensor->name, obj[sensor->pathSensor][sensor->pathValue].asInt());
						if(obj[sensor->pathSensor][sensor->pathValue].isDouble())
							tasmotaDevice->wbDevice.set(sensor->name, obj[sensor->pathSensor][sensor->pathValue].asDouble());
						else
							tasmotaDevice->wbDevice.set(sensor->name, obj[sensor->pathSensor][sensor->pathValue].asString());
						
						SendUpdate();
					}
				}*/
			} else if(v[2]=="INFO1") {
				Json::Value jsonPayload; Parse(payload, jsonPayload);
				if (jsonPayload.isMember("Module") && jsonPayload.isMember("Version")) {
					string Module = jsonPayload["Module"].asString();
					string Version = jsonPayload["Version"].asString();
					m_Log->Printf(0, "M:%s V:%s", Module.c_str(), Version.c_str());

					if(Version.find("(minimal)")!=Version.npos) {
						if (tasmotaDevice->OtaUrl!="") 
							sendCommand(tasmotaDevice->wbDevice.getName(), "OtaUrl", tasmotaDevice->OtaUrl);
						else
							sendCommand(tasmotaDevice->wbDevice.getName(), "OtaUrl", "1");
						sendCommand(tasmotaDevice->wbDevice.getName(), "Upgrade", "1");
						tasmotaDevice->upgradeState = 4;
					}
				}
			}
		} else if (v[0] == "stat" && (v[2].find("STATUS")==0 || v[2]=="RESULT") && tasmotaDevice) {
			Json::Value jsonPayload; Parse(payload, jsonPayload);
			string_vector names = jsonPayload.getMemberNames();

			if (names.size()==1 && (names[0].find("Status")==0 || names[0]=="SetOption80")) {
				tasmotaDevice->params[names[0]] = payload;
			} 
			else if (names[0]=="OtaUrl") {
				string OtaUrl = jsonPayload[names[0]].asString();
				if (OtaUrl.find("minimal")==OtaUrl.npos && tasmotaDevice->upgradeState==1)
				{
					tasmotaDevice->OtaUrl = OtaUrl;
					sendCommand(tasmotaDevice->wbDevice.getName(), "OtaUrl", g_tasmotaMinimal);
				} 
				sendCommand(tasmotaDevice->wbDevice.getName(), "Upgrade", "1");
				tasmotaDevice->upgradeState = 2;
			} 
			
			if (names[0]=="StatusFWR" && jsonPayload[names[0]].isMember("Version")) {
				string Version = jsonPayload[names[0]]["Version"].asString();
				if(Version.find("(minimal)")!=Version.npos) {
					if (tasmotaDevice->upgradeState!=4) {
						if (tasmotaDevice->OtaUrl!="") 
							sendCommand(tasmotaDevice->wbDevice.getName(), "OtaUrl", tasmotaDevice->OtaUrl);
						else
							sendCommand(tasmotaDevice->wbDevice.getName(), "OtaUrl", "1");
						sendCommand(tasmotaDevice->wbDevice.getName(), "Upgrade", "1");
						tasmotaDevice->upgradeState = 4;
					}
				} else if (tasmotaDevice->upgradeState) {
					tasmotaDevice->upgradeState = 0; 
					m_Log->Printf(0, "Upgrade finished. Current version: %s", Version.c_str());
				}
			}
			

			if (tasmotaDevice->wbDevice.getControls()->size()<2 &&
					tasmotaDevice->params.find("Status")!=tasmotaDevice->params.end() &&
					tasmotaDevice->params.find("SetOption80")!=tasmotaDevice->params.end() &&
					tasmotaDevice->params.find("StatusSTS")!=tasmotaDevice->params.end() && 
					tasmotaDevice->params.find("StatusNET")!=tasmotaDevice->params.end()
				) {
				Json::Value status; Parse(tasmotaDevice->params["Status"], status);
				string desc;
				if (status.isMember("Status") && status["Status"].isMember("FriendlyName") && 
					status["Status"]["FriendlyName"].isArray()) {
					desc = status["Status"]["FriendlyName"][0].asString();
					tasmotaDevice->wbDevice.enrichDevice("name", desc);
				}

				Json::Value SetOption80; Parse(tasmotaDevice->params["SetOption80"], SetOption80);
				if (SetOption80.isMember("SetOption80") && SetOption80["SetOption80"]=="ON") {
					tasmotaDevice->isShutter = true;
				}

				Json::Value status11; Parse(tasmotaDevice->params["StatusSTS"], status11);
				if (status11.isMember("StatusSTS")) {
					status11 = status11["StatusSTS"];
					tasmotaDevice->relayCount = countEntity("POWER", status11);
					int ChannelCount = countEntity("Channel", status11);

					for (int i=0;i<tasmotaDevice->relayCount;i++) {
						string tsmCtl = tasmotaDevice->relayCount==1 ? "POWER" : "POWER"+itoa(i+1);
						if (tasmotaDevice->isShutter && i<2) {
							if (i==0) {
								tasmotaDevice->wbDevice.addControl("Up", CWBControl::PushButton, false);
								tasmotaDevice->wbDevice.addControl("Down", CWBControl::PushButton, false);
								tasmotaDevice->wbDevice.addControl("Stop", CWBControl::PushButton, false);
							}
						} else {
							string wbCtl = "K"+itoa(i+1);
							tasmotaDevice->wbDevice.addControl(wbCtl, CWBControl::Switch, false);
							bool power = status11[tsmCtl]=="ON";
							tasmotaDevice->wbDevice.set(wbCtl, power?"1":"0");
							m_Log->Printf(4, "Add %s with %s", (deviceName+"/"+wbCtl).c_str(), power?"ON":"OFF");
						}
					}								
				}
				
				Json::Value statusNet; Parse(tasmotaDevice->params["StatusNET"], statusNet);
				if (statusNet.isMember("StatusNET")) {
					statusNet = statusNet["StatusNET"];
					tasmotaDevice->ip = statusNet["IPAddress"].asString();	
					tasmotaDevice->wbDevice.set("ip", tasmotaDevice->ip);		
				}
				
				m_Log->Printf(0, "Create %s(%s). %d relays%s", deviceName.c_str(), desc.c_str(), tasmotaDevice->relayCount, tasmotaDevice->isShutter?" with shuter":"");
				CreateDevice(tasmotaDevice);
				SendUpdate();
				string topic = "/devices/"+deviceName+"/controls/+/on"; 
				m_Log->Printf(5, "subscribe to %s", topic.c_str());
				subscribe(topic.c_str());
				tasmotaDevice->params.empty();
			} 
		} else if (v[0] == "stat" && v[2].find("UPGRADE")==0 && tasmotaDevice && tasmotaDevice->upgradeState==2) {
			tasmotaDevice->upgradeState==3;
		}

		if (v[0] == "tele") {
			if (v[2]=="LWT") {
				if(payload=="Online") { // Device online
					if (tasmotaDevice) { 
						sendCommand(deviceName, "status");
						if (tasmotaDevice->wbDevice.getS("ip")=="Offline") {
							tasmotaDevice->wbDevice.set("ip", tasmotaDevice->ip);
							SendUpdate();
						}
					} else {
						queryDevice(deviceName);
					}
				} else if(payload=="Offline" && tasmotaDevice) {
					tasmotaDevice->wbDevice.set("ip", "Offline");
					SendUpdate();
				}
			} else if (v[2]=="STATE") {
				Json::Value jsonPayload; Parse(payload, jsonPayload);
				//value = jsonPayload["Time"].asString();
			}
 		} else if (v[0] == "stat") {
			if (v[2]=="STATUS") {
				if (m_Devices.find(deviceName)!=m_Devices.end()) {
				} 
			} else if (v[2]=="RESULT" || v[2]=="STATE") {
				Json::Value obj; Parse(payload, obj);

				for(int i=0;i<tasmotaDevice->relayCount;i++) {
					string wbCtl = "K"+itoa(i+1);
					string tsmCtl = tasmotaDevice->relayCount==1 ? "POWER" : "POWER"+itoa(i+1);
					if (obj.isMember(tsmCtl) && tasmotaDevice->wbDevice.controlExists(wbCtl)) {
				 		bool power = obj[tsmCtl]=="ON";
						m_Log->Printf(4, "Copy %s.%s=%s to %s", deviceName.c_str(), tsmCtl.c_str(), power?"on":"off", wbCtl.c_str());
						tasmotaDevice->wbDevice.set(wbCtl, power?"1":"0");
					}
				}
				SendUpdate();

			}
		}  else if (v[0] == "" && v[1]=="devices") {
			string device = v[2];
			if (device=="tasmota") {
				string control = v[4];

				if (control=="upgrade" && m_Devices.find(payload)!=m_Devices.end()) {
					CTasmotaWBDevice *dev= m_Devices[payload]; 
					sendCommand(dev->wbDevice.getName(), "OtaUrl", "");
					dev->upgradeState = 1;
				}
			}
 			else if (m_Devices.find(device)!=m_Devices.end()) {
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
	CreateDevice(&dev->wbDevice);
}

void CMqttConnection::CreateDevice(CWBDevice* wbDevice)
{
	string_map v;
	wbDevice->createDeviceValues(v);
	for_each(string_map, v, i)
	{
		publish(i->first, i->second, true);
		m_Log->Printf(5, "publish %s=%s", i->first.c_str(), i->second.c_str());
	}
}

void CMqttConnection::SendUpdate()
{
	string_map v;

	m_tasmotaDevice.updateValues(v);

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

int CMqttConnection::countEntity(string preffix, Json::Value values){
	if (values.isMember(preffix)) return 1;

	for (int count = 16; count>=0; count--) {
		if (values.isMember(preffix+itoa(count))) return count;
	}

	return 0;
}

void CMqttConnection::onIdle(){
	if (m_lastIdle+10>time(NULL)) return;
	time(&m_lastIdle);

	for_each(CTasmotaWBDeviceMap, m_Devices, dev)
	{
		if (dev->second && 
				dev->second->wbDevice.getS("ip")!="Offline" && 
				dev->second->lastMessage+300<time(NULL))
			dev->second->wbDevice.set("ip", "Offline");
	}

	SendUpdate();
}

void CMqttConnection::queryDevice(string deviceName){
	if (m_Devices.find(deviceName)==m_Devices.end())
	{
		CTasmotaWBDevice *tasmotaDevice = new CTasmotaWBDevice(deviceName, deviceName); 
		tasmotaDevice->wbDevice.addControl("ip"); 
		tasmotaDevice->wbDevice.set("ip", "Unknown");
		m_Devices[deviceName] = tasmotaDevice;			
	} 

	publish("cmnd/"+deviceName+"/Status", "0");
	publish("cmnd/"+deviceName+"/SetOption80", "0");
}
