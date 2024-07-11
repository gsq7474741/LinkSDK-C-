//
// Created by 高崧淇 on 24-7-11.
//

#ifndef BME_DEV_KIT_ODOR_ALINKIOT_H
#define BME_DEV_KIT_ODOR_ALINKIOT_H

#include "AIoTNTP.h"

class ALinkIoT {

   public:
	ALinkIoT(const char *product_key,const char *device_name,PubSubClient *client);
	 ~ALinkIoT()=default;

	 AIoTNTP *ntp;

   private:
	const char *product_key      ;
	const char *device_name     ;
	PubSubClient *client;
	// mqtt 链接信息，动态生成的
	static char mqttPwd[256];
	static char clientId[256];
	static char mqttUsername[100];
	static char domain[150];
};


#endif//BME_DEV_KIT_ODOR_ALINKIOT_H
