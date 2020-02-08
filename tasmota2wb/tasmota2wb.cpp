// tasmotatest.cpp : Defines the entry point for the console application.
//

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



int main(int argc, char* argv[])
{

	bool bDaemon = false, bDebug = false;
	string serverName;
	string mqttHost = "wirenboard";
	string configName = "tasmota2wb.json";
	

#ifndef WIN32
	int c;
	//~ int digit_optind = 0;
	//~ int aopt = 0, bopt = 0;
	//~ char *copt = 0, *dopt = 0;
	while ((c = getopt(argc, argv, "dc:s:")) != -1) {
		//~ int this_option_optind = optind ? optind : 1;
		switch (c) {
		case 'c':
			configName = optarg;
			break;
		case 's':
			serverName = optarg;
			break;
		case 'd':
			bDebug = true;
			break;
}
	}
#endif

	if (!configName.length())
	{
		printf("Config not defined\n");
		return -1;
	}

	try
	{
		CConfig config;
		config.Load(configName);

		CConfigItem debug = config.getNode("debug");
		if (debug.isNode())
		{
			CLog::Init(&debug);
		}

		CLog* log = CLog::Default();
		if (bDebug) log->SetConsoleLogLevel(50);

		CMqttConnection mqttConn(config.getRoot(), log);

		while (!mqttConn.isStopped())
			Sleep(1000);
	}
	catch (CHaException ex)
	{
		CLog* log = CLog::Default();
		log->Printf(0, "Failed with exception %d(%s)", ex.GetCode(), ex.GetMsg().c_str());
	}
	return 0;
}
