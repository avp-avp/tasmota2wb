#pragma once
#include "../libs/libutils/Config.h"
#include "../libs/libwb/WBDevice.h"
#include "mosquittopp.h"

struct Template{
	int relayCount;
	bool curtain;

	Template(int _relayCount, bool _curtain);
	Template();

};

typedef map<string, Template> TemplateMap;


class CMqttConnection
	:public mosqpp::mosquittopp
{
	string m_Server;
	CLog *m_Log;
	bool m_isConnected, m_bStop;
	TemplateMap m_Templates;
	CWBDeviceMap m_Devices;

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
	void CreateDevice(CWBDevice* dev);
};
