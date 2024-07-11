//
// Created by 高崧淇 on 24-7-11.
//

#include "ALinkIoT.h"
ALinkIoT::ALinkIoT(const char *product_key, const char *device_name,PubSubClient *client) : product_key(product_key), device_name(device_name),client(client)
{
	this->ntp=new AIoTNTP(product_key,device_name,client);
}
