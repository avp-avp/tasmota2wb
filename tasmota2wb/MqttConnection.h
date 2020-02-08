#pragma once
#include "../libs/libutils/Config.h"
#include "../libs/libwb/WBDevice.h"
#include "mosquittopp.h"

struct CTasmotaWBDevice {
	CTasmotaWBDevice(string Name, string Description);
	string_map params;
	int relayCount;
	bool isShutter;
	CWBDevice wbDevice;
};

typedef map<string, CTasmotaWBDevice*> CTasmotaWBDeviceMap;

class CMqttConnection
	:public mosqpp::mosquittopp
{
	string m_Server;
	CLog *m_Log;
	bool m_isConnected, m_bStop;
	CTasmotaWBDeviceMap m_Devices;
	string_vector m_Groups;

	enum ActiveCommand {None=0, Module, State, SetOption80} m_ActiveCommand;
	string m_ActiveCommandDevice;

public:
	CMqttConnection(CConfigItem config, CLog* log);
	~CMqttConnection();
	void NewMessage(string message);
	bool isStopped() {return m_bStop;};

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
	void subscribe(const string &topic);
	void publish(const string &topic, const string &payload, bool retain=false);
};
