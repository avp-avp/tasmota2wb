#pragma once
#include "../libs/libutils/Config.h"
#include "../libs/libwb/WBDevice.h"
#include "mosquittopp.h"

struct CTasmotaWBDevice {
	CTasmotaWBDevice(string Name, string Description);
	string_map params;
	int relayCount, channelCount;
	bool isShutter, isOpentherm, b_NeedCreate;
	CWBDevice wbDevice;
	time_t lastMessage, lastStatusRequest;
	string ip;
};
typedef map<string, CTasmotaWBDevice*> CTasmotaWBDeviceMap;

struct CSensorType {
	string path, name;
	CWBControl::ControlType type;
	CSensorType(const CConfigItem* cfg);
};
typedef vector<CSensorType> CSensorTypeList;


class CMqttConnection
	:public mosqpp::mosquittopp
{
	string m_Server;
	CLog *m_Log;
	bool m_isConnected, m_bStop;
	CTasmotaWBDeviceMap m_Devices;
	CSensorTypeList m_SensorTypeList;
	string_vector m_Groups;
	CWBDevice m_tasmotaDevice;
	time_t m_lastIdle;

public:
	CMqttConnection(CConfigItem config, CLog* log, string mqttServer);
	~CMqttConnection();
	void NewMessage(string message);
	bool isStopped() {return m_bStop;};
	void onIdle();

private:
	virtual void on_connect(int rc);
	virtual void on_disconnect(int rc);
	//virtual void on_publish(int mid);
	virtual void on_message(const struct mosquitto_message *message);
	//virtual void on_subscribe(int mid, int qos_count, const int *granted_qos);
	//virtual void on_unsubscribe(int mid);
	virtual void on_log(int level, const char *str);
	virtual void on_error();

	void sendCommand(string device, string cmd, string data="");
	void SendUpdate();
	void CreateDevice(CTasmotaWBDevice* dev);
	void CreateDevice(CWBDevice* dev);
	void subscribe(const string &topic);
	void publish(const string &topic, const string &payload, bool retain=false);

	int countEntity(string preffix, Json::Value values);
	void queryDevice(string deviceName);

};
