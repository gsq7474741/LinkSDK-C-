////
//// Created by 高崧淇 on 24-7-11.
////
//
//
//
//#include "AIoTNTP.h"
//#include "core/aiot_state_api.h"
//
//static const char *deviceName   = NULL;
//static const char *productKey   = NULL;
//static const char *deviceSecret = NULL;
//static const char *region       = NULL;
//
//
//
//
//#define NTP_MODULE_NAME               "ntp" /* 用于内存统计的模块名字符串 */
//
//#define NTP_DEFAULT_DEINIT_TIMEOUT_MS (2 * 1000)
//#define NTP_DEFAULT_TIME_ZONE         (0)
//
//#define NTP_REQUEST_TOPIC_FMT         "/ext/ntp/%s/%s/request"
//#define NTP_REQUEST_PAYLOAD_FMT       "{\"deviceSendTime\":\"%s\"}"
//#define NTP_RESPONSE_TOPIC_FMT        "/ext/ntp/%s/%s/response"
//
//#define NTP_DEINIT_INTERVAL_MS        (100)
//
//void *core_sysdep_mutex_init(void)
//{
//	return xSemaphoreCreateMutex();
//}
//
//void core_sysdep_mutex_lock(void *mutex)
//{
//	xSemaphoreTake((SemaphoreHandle_t) mutex, portMAX_DELAY);
//}
//
//void core_sysdep_mutex_unlock(void *mutex)
//{
//	xSemaphoreGive((SemaphoreHandle_t) mutex);
//}
//
//void core_sysdep_mutex_deinit(void **mutex)
//{
//	if (mutex != NULL || *mutex != NULL) {
//		vSemaphoreDelete((SemaphoreHandle_t) *mutex);
//		*mutex = NULL;
//	}
//}
//
//int32_t core_uint642str(uint64_t input, char *output, uint8_t *output_len)
//{
//	uint8_t i = 0, j = 0;
//	char    temp[20] = {0};
//
//	do {
//		temp[i++] = input % 10 + '0';
//	} while ((input /= 10) > 0);
//
//	do {
//		output[--i] = temp[j++];
//	} while (i > 0);
//
//	if (output_len) {
//		*output_len = j;
//	}
//
//	return STATE_SUCCESS;
//}
//
//uint64_t core_sysdep_time(void)
//{
//	return (uint64_t) (xTaskGetTickCount() * portTICK_RATE_MS);
//}
//
//void core_sysdep_free(void *ptr)
//{
//	vPortFree(ptr);
//}
//
//
//static void _ntp_recv_handler(void *handle, const aiot_mqtt_recv_t *packet, void *userdata)
//{
//	ntp_handle_t *ntp_handle = (ntp_handle_t *)userdata;
//
//	switch (packet->type) {
//		case AIOT_MQTTRECV_PUB: {
//			char *dst_key = "deviceSendTime", *srt_key = "serverRecvTime", *sst_key = "serverSendTime";
//			char *dst_value = NULL, *srt_value = NULL, *sst_value = NULL;
//			uint32_t dst_value_len = 0, srt_value_len = 0, sst_value_len = 0;
//			uint64_t dst = 0, srt = 0, sst = 0, utc = 0;
//
//			if (core_json_value((char *)packet->data.pub.payload, packet->data.pub.payload_len, dst_key, (uint32_t)strlen(dst_key),
//								&dst_value, &dst_value_len) == STATE_SUCCESS &&
//				core_json_value((char *)packet->data.pub.payload, packet->data.pub.payload_len, srt_key, (uint32_t)strlen(srt_key),
//								&srt_value, &srt_value_len) == STATE_SUCCESS &&
//				core_json_value((char *)packet->data.pub.payload, packet->data.pub.payload_len, sst_key, (uint32_t)strlen(sst_key),
//								&sst_value, &sst_value_len) == STATE_SUCCESS) {
//				if (core_str2uint64(dst_value, (uint8_t)dst_value_len, &dst) == STATE_SUCCESS &&
//					core_str2uint64(srt_value, (uint8_t)srt_value_len, &srt) == STATE_SUCCESS &&
//					core_str2uint64(sst_value, (uint8_t)sst_value_len, &sst) == STATE_SUCCESS) {
//					core_date_t date;
//					utc = (srt + sst + ntp_handle->sysdep->core_sysdep_time() - dst) / 2;
//
//					core_log_set_timestamp(ntp_handle->sysdep, utc);
//
//					memset(&date, 0, sizeof(core_date_t));
//					core_utc2date(utc, ntp_handle->time_zone, &date);
//					if (ntp_handle->recv_handler != NULL) {
//						aiot_ntp_recv_t recv;
//
//						memset(&recv, 0, sizeof(aiot_ntp_recv_t));
//						recv.type = AIOT_NTPRECV_LOCAL_TIME;
//						recv.data.local_time.timestamp = utc;
//						recv.data.local_time.year = date.year;
//						recv.data.local_time.mon = date.mon;
//						recv.data.local_time.day = date.day;
//						recv.data.local_time.hour = date.hour;
//						recv.data.local_time.min = date.min;
//						recv.data.local_time.sec = date.sec;
//						recv.data.local_time.msec = date.msec;
//
//						ntp_handle->recv_handler(ntp_handle, &recv, ntp_handle->userdata);
//					}
//				} else {
//					if (ntp_handle->event_handler != NULL) {
//						aiot_ntp_event_t event;
//
//						memset(&event, 0, sizeof(aiot_ntp_event_t));
//						event.type = AIOT_NTPEVT_INVALID_TIME_FORMAT;
//						ntp_handle->event_handler(ntp_handle, &event, ntp_handle->userdata);
//					}
//				}
//			} else {
//				if (ntp_handle->event_handler != NULL) {
//					aiot_ntp_event_t event;
//
//					memset(&event, 0, sizeof(aiot_ntp_event_t));
//					event.type = AIOT_NTPEVT_INVALID_RESPONSE;
//					ntp_handle->event_handler(ntp_handle, &event, ntp_handle->userdata);
//				}
//			}
//		}
//		default: {
//
//		}
//		break;
//	}
//}
//
//static int32_t _ntp_operate_topic_map(ntp_handle_t *ntp_handle, aiot_mqtt_option_t option)
//{
//	int32_t res = STATE_SUCCESS;
//	aiot_mqtt_topic_map_t map;
//	char *topic = NULL;
//	char *topic_src[] = { core_mqtt_get_product_key(ntp_handle->mqtt_handle), core_mqtt_get_device_name(ntp_handle->mqtt_handle) };
//	char *topic_fmt = NTP_RESPONSE_TOPIC_FMT;
//
//	memset(&map, 0, sizeof(aiot_mqtt_topic_map_t));
//
//	res = core_sprintf(ntp_handle->sysdep, &topic, topic_fmt, topic_src, sizeof(topic_src) / sizeof(char *),
//					   NTP_MODULE_NAME);
////	if (res < STATE_SUCCESS) {
////		return res;
////	}
//
//	map.topic = topic;
//	map.handler = _ntp_recv_handler;
//	map.userdata = (void *)ntp_handle;
//
//	res = aiot_mqtt_setopt(ntp_handle->mqtt_handle, option, &map);
//	ntp_handle->sysdep->core_sysdep_free(topic);
//
//	return res;
//}
//
//static void _ntp_core_mqtt_process_handler(void *context, aiot_mqtt_event_t *event, core_mqtt_event_t *core_event)
//{
//	ntp_handle_t *ntp_handle = (ntp_handle_t *)context;
//
//	if (core_event != NULL) {
//		switch (core_event->type) {
//			case CORE_MQTTEVT_DEINIT: {
//				ntp_handle->mqtt_handle = NULL;
//				return;
//			}
//			default: {
//
//			}
//			break;
//		}
//	}
//}
//
//static int32_t _ntp_core_mqtt_operate_process_handler(ntp_handle_t *ntp_handle, core_mqtt_option_t option)
//{
//	core_mqtt_process_data_t process_data;
//
//	memset(&process_data, 0, sizeof(core_mqtt_process_data_t));
//	process_data.handler = _ntp_core_mqtt_process_handler;
//	process_data.context = ntp_handle;
//
//	return core_mqtt_setopt(ntp_handle->mqtt_handle, option, &process_data);
//}
//
//
//void AIoTNTP::_core_ntp_exec_inc()
//{
//	core_sysdep_mutex_lock(this->data_mutex);
//	this->exec_count++;
//	core_sysdep_mutex_unlock(this->data_mutex);
//}
//
//void AIoTNTP::_core_ntp_exec_dec()
//{
//	core_sysdep_mutex_lock(this->data_mutex);
//	this->exec_count--;
//	core_sysdep_mutex_unlock(this->data_mutex);
//}
//
//void AIoTNTP::begin(
//		Client &espClient, const char *_productKey, const char *_deviceName, const char *_deviceSecret, const char *_region)
//{
//
//	client           = new PubSubClient(espClient);
//	productKey       = _productKey;
//	deviceName       = _deviceName;
//	deviceSecret     = _deviceSecret;
//	region           = _region;
//	long   times     = millis();
//	String timestamp = String(times);
//
//	sprintf(clientId, "%s|securemode=3,signmethod=hmacsha256,timestamp=%s|", deviceName, timestamp.c_str());
//
//	String signcontent = "clientId";
//	signcontent += deviceName;
//	signcontent += "deviceName";
//	signcontent += deviceName;
//	signcontent += "productKey";
//	signcontent += productKey;
//	signcontent += "timestamp";
//	signcontent += timestamp;
//
//	String pwd = hmac256(signcontent, deviceSecret);
//
//	strcpy(mqttPwd, pwd.c_str());
//
//	sprintf(mqttUsername, "%s&%s", deviceName, productKey);
//	sprintf(ALINK_TOPIC_PROP_POST, "/sys/%s/%s/thing/event/property/post", productKey, deviceName);
//	sprintf(ALINK_TOPIC_PROP_SET, "/sys/%s/%s/thing/service/property/set", productKey, deviceName);
//	sprintf(ALINK_TOPIC_PROP_SET_REPLY, "/sys/%s/%s/thing/service/property/set_reply", productKey, deviceName);
//	sprintf(ALINK_TOPIC_EVENT, "/sys/%s/%s/thing/event", productKey, deviceName);
//
//	//	/sys/${productKey}/${deviceName}/thing/service/${tsl.service.identifier}
//	sprintf(ALINK_TOPIC_SERVICE, "/sys/%s/%s/thing/service", productKey, deviceName);
//
//	sprintf(domain, "%s.iot-as-mqtt.%s.aliyuncs.com", productKey, region);
//	client->setServer(domain, MQTT_PORT); /* 连接WiFi之后，连接MQTT服务器 */
//	client->setCallback(callback);
//
//	mqttCheckConnect();
//}
//
//
//AIoTNTP::AIoTNTP(const char *product_key, const char *device_name, PubSubClient *client)
//	: product_key(product_key),
//	  device_name(device_name)
//{
//	if (client == nullptr) {
//		return;
//	}
//	this->client            = client;
//	this->deinit_timeout_ms = NTP_DEFAULT_DEINIT_TIMEOUT_MS;
//	this->data_mutex        = core_sysdep_mutex_init();
//	this->exec_enabled      = 1;
//
//	core_sysdep_mutex_unlock(this->data_mutex);
//	res = _ntp_operate_topic_map(ntp_handle, AIOT_MQTTOPT_APPEND_TOPIC_MAP);
//	if (res >= STATE_SUCCESS) {
//		res = _ntp_core_mqtt_operate_process_handler(ntp_handle, CORE_MQTTOPT_APPEND_PROCESS_HANDLER);
//	}
//	core_sysdep_mutex_lock(this->data_mutex);
//}
//
//int32_t AIoTNTP::aiot_ntp_setopt(aiot_ntp_option_t option, void *data)
//{
//	int32_t res = STATE_SUCCESS;
//	//	ntp_handle_t *ntp_handle = (ntp_handle_t *) handle;
//
//	if (data == NULL) {
//		return STATE_USER_INPUT_NULL_POINTER;
//	}
//
//	if (option >= AIOT_NTPOPT_MAX) {
//		return STATE_USER_INPUT_OUT_RANGE;
//	}
//
//	if (this->exec_enabled == 0) {
//		return STATE_USER_INPUT_EXEC_DISABLED;
//	}
//
//	this->_core_ntp_exec_inc();
//
//	core_sysdep_mutex_lock(this->data_mutex);
//	switch (option) {
//		case AIOT_NTPOPT_MQTT_HANDLE: {
////						this->mqtt_handle = data;
////						core_sysdep_mutex_unlock(this->data_mutex);
////						res = _ntp_operate_topic_map(ntp_handle, AIOT_MQTTOPT_APPEND_TOPIC_MAP);
////						if (res >= STATE_SUCCESS) {
////							res = _ntp_core_mqtt_operate_process_handler(ntp_handle, CORE_MQTTOPT_APPEND_PROCESS_HANDLER);
////						}
////						core_sysdep_mutex_lock(this->data_mutex);
//		} break;
//		case AIOT_NTPOPT_TIME_ZONE: {
//			this->time_zone = *(int8_t *) data;
//		} break;
//		case AIOT_NTPOPT_RECV_HANDLER: {
//			this->recv_handler = (aiot_ntp_recv_handler_t) data;
//		} break;
//		case AIOT_NTPOPT_EVENT_HANDLER: {
//			this->event_handler = (aiot_ntp_event_handler_t) data;
//		} break;
//		case AIOT_NTPOPT_USERDATA: {
//			this->userdata = data;
//		} break;
//		case AIOT_NTPOPT_DEINIT_TIMEOUT_MS: {
//			this->deinit_timeout_ms = *(uint32_t *) data;
//		} break;
//		default: {
//			res = STATE_USER_INPUT_UNKNOWN_OPTION;
//		} break;
//	}
//	core_sysdep_mutex_unlock(this->data_mutex);
//
//	this->_core_ntp_exec_dec();
//
//	return res;
//}
//int32_t AIoTNTP::aiot_ntp_send_request()
//{
//	int32_t res   = STATE_SUCCESS;
//	char   *topic = NULL, *payload = NULL;
//
//
//	if (this->exec_enabled == 0) {
//		return STATE_USER_INPUT_EXEC_DISABLED;
//	}
//
//	this->_core_ntp_exec_inc();
//
//	{
//		char *topic_src[]   = {const_cast<char *>(this->product_key), const_cast<char *>(this->device_name)};
//		char *topic_fmt     = NTP_REQUEST_TOPIC_FMT;
//		char  time_str[21]  = {0};
//		char *payload_src[] = {time_str};
//		char *payload_fmt   = NTP_REQUEST_PAYLOAD_FMT;
//
//		//		res = core_sprintf(this->sysdep, &topic, topic_fmt, topic_src, sizeof(topic_src) / sizeof(char *), NTP_MODULE_NAME);
//		res = sprintf(topic, topic_fmt, topic_src);
//		if (res < STATE_SUCCESS) {
//			this->_core_ntp_exec_dec();
//			return res;
//		}
//
//		core_uint642str(core_sysdep_time(), time_str, NULL);
//
//		//		res = core_sprintf(this->sysdep, &payload, payload_fmt, payload_src, sizeof(payload_src) / sizeof(char *), NTP_MODULE_NAME);
//		res = sprintf(payload, payload_fmt, payload_src);
//		if (res < STATE_SUCCESS) {
//			core_sysdep_free(topic);
//			_core_ntp_exec_dec();
//			return res;
//		}
//	}
//
//	//	res = aiot_mqtt_pub(this->mqtt_handle, topic, (uint8_t *) payload, (uint32_t) strlen(payload), 0);
//	res = client->publish(topic, (uint8_t *) payload, (uint32_t) strlen(payload), 0);
//	core_sysdep_free(topic);
//	core_sysdep_free(payload);
//	if (!res) {
//		_core_ntp_exec_dec();
//		return res;
//	}
//
//	_core_ntp_exec_dec();
//
//	return STATE_SUCCESS;
//}
