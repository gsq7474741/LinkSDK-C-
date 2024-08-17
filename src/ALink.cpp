//
// Created by 高崧淇 on 24-7-12.
//

#include "ALink.h"
#include "csdk/dm_private.h"

//#include <utility>

/*
 * 这个例程适用于`Linux`这类支持pthread的POSIX设备, 它演示了用SDK配置MQTT参数并建立连接, 之后创建2个线程
 *
 * + 一个线程用于保活长连接
 * + 一个线程用于接收消息, 并在有消息到达时进入默认的数据回调, 在连接状态变化时进入事件回调
 *
 * 接着演示了在MQTT连接上进行属性上报, 事件上报, 以及处理收到的属性设置, 服务调用, 取消这些代码段落的注释即可观察运行效果
 *
 * 需要用户关注或修改的部分, 已经用 TODO 在注释中标明
 *
 */


/* TODO: 替换为自己设备的三元组 */
static const char *product_key   = "${YourProductKey}";
static const char *device_name   = "${YourDeviceName}";
static const char *device_secret = "${YourDeviceSecret}";
static const char *instance_id   = "${YourInstanceId}";
//static char product_key[]

static std::unordered_map<std::string, std::function<std::pair<bool, std::optional<JsonVariant>>(JsonVariant)>> async_service_cb_map;
static std::unordered_map<std::string, std::function<bool(JsonVariant)>>                                        sync_service_cb_map;
static std::unordered_map<std::string, std::function<bool(JsonVariant)>>                                        property_set_cb_map;

//static std::function<void(std::string, size_t)>                        property_set_cb;
static std::function<void(int32_t, int32_t, std::string, std::string)> generic_reply_cb;
static std::function<void(const aiot_ntp_recv_t *)>                    ntp_recv_cb;
static std::function<void(uint8_t *, const uint32_t)>                  ota_download_buffer_write_cb;
static std::function<void(void)>                                       ota_download_down_cb;

//	res         = STATE_SUCCESS;
//	dm_handle   = NULL;
//	mqtt_handle = NULL;
//	post_reply  = 1;
int32_t                    ALink::res         = STATE_SUCCESS;
void                      *ALink::dm_handle   = NULL;
void                      *ALink::mqtt_handle = NULL;
aiot_sysdep_network_cred_t ALink::cred{}; /* 安全凭据结构体, 如果要用TLS, 这个结构体中配置CA证书等参数 */
uint8_t                    ALink::post_reply  = 1;
int8_t                     ALink::time_zone   = 8;
void                      *ALink::ntp_handle  = NULL;
std::string                ALink::cur_version = "0.0.1";
void                      *ALink::ota_handle  = NULL;


//static

/*
    TODO: 替换为自己实例的接入点

    对于企业实例, 或者2021年07月30日之后（含当日）开通的物联网平台服务下公共实例
    mqtt_host的格式为"${YourInstanceId}.mqtt.iothub.aliyuncs.com"
    其中${YourInstanceId}: 请替换为您企业/公共实例的Id

    对于2021年07月30日之前（不含当日）开通的物联网平台服务下公共实例，请使用旧版接入点。
    详情请见: https://help.aliyun.com/document_detail/147356.html
*/
static char mqtt_host[256] = "";
/*
    原端口：1883/443，对应的证书(GlobalSign R1),于2028年1月过期，届时可能会导致设备不能建连。
    (推荐)新端口：8883，将搭载新证书，由阿里云物联网平台自签证书，于2053年7月过期。
*/
const uint16_t port = 8883;

/* 位于portfiles/aiot_port文件夹下的系统适配函数集合 */
extern aiot_sysdep_portfile_t g_aiot_sysdep_portfile;

/* 位于external/ali_ca_cert.c中的服务器证书 */
extern const char *ali_ca_cert;

static pthread_t g_mqtt_process_thread;
static pthread_t g_mqtt_recv_thread;
static uint8_t   g_mqtt_process_thread_running = 0;
static uint8_t   g_mqtt_recv_thread_running    = 0;

/* 位于external/ali_ca_cert.c中的服务器证书 */
static void    *g_ota_handle    = nullptr;
static void    *g_dl_handle     = nullptr;
static uint32_t g_firmware_size = 0;

/* TODO: 如果要关闭日志, 就把这个函数实现为空, 如果要减少日志, 可根据code选择不打印
 *
 * 上面这条日志的code就是0317(十六进制), code值的定义见core/aiot_state_api.h
 *
 */

/* 日志回调函数, SDK的日志会从这里输出 */
int32_t demo_state_logcb(int32_t code, char *message)
{
	/* 下载固件的时候会有大量的HTTP收包日志, 通过code筛选出来关闭 */
	if (code != STATE_HTTP_LOG_RECV_CONTENT
//		&& code!= STATE_MQTT_LOG_HEXDUMP

		) {
		//		printf("%s", message);
		Serial.printf("%s", message);
	}
	return 0;
}

/* MQTT事件回调函数, 当网络连接/重连/断开时被触发, 事件定义见core/aiot_mqtt_api.h */
void demo_mqtt_event_handler(void *handle, const aiot_mqtt_event_t *event, void *userdata)
{
	switch (event->type) {
		/* SDK因为用户调用了aiot_mqtt_connect()接口, 与mqtt服务器建立连接已成功 */
		case AIOT_MQTTEVT_CONNECT: {
			printf("AIOT_MQTTEVT_CONNECT\n");
		} break;

		/* SDK因为网络状况被动断连后, 自动发起重连已成功 */
		case AIOT_MQTTEVT_RECONNECT: {
			printf("AIOT_MQTTEVT_RECONNECT\n");
		} break;

		/* SDK因为网络的状况而被动断开了连接, network是底层读写失败, heartbeat是没有按预期得到服务端心跳应答 */
		case AIOT_MQTTEVT_DISCONNECT: {
			const char *cause =
					(event->data.disconnect == AIOT_MQTTDISCONNEVT_NETWORK_DISCONNECT) ? ("network disconnect") : ("heartbeat disconnect");
			printf("AIOT_MQTTEVT_DISCONNECT: %s\n", cause);
		} break;

		default: {
		}
	}
}

/* 执行aiot_mqtt_process的线程, 包含心跳发送和QoS1消息重发 */
void *demo_mqtt_process_thread(void *args)
{
	int32_t res = STATE_SUCCESS;

	while (g_mqtt_process_thread_running) {
		res = aiot_mqtt_process(args);
		if (res == STATE_USER_INPUT_EXEC_DISABLED) {
			break;
		}
		sleep(1);
	}
	return NULL;
}

/* 执行aiot_mqtt_recv的线程, 包含网络自动重连和从服务器收取MQTT消息 */
void *demo_mqtt_recv_thread(void *args)
{
	int32_t res = STATE_SUCCESS;

	while (g_mqtt_recv_thread_running) {
		res = aiot_mqtt_recv(args);
		if (res < STATE_SUCCESS) {
			if (res == STATE_USER_INPUT_EXEC_DISABLED) {
				break;
			}
			sleep(1);
		}
	}
	return NULL;
}

static void demo_dm_recv_generic_reply(void *dm_handle, const aiot_dm_recv_t *recv, void *userdata)
{
	printf("demo_dm_recv_generic_reply msg_id = %d, code = %d, data = %.*s, message = %.*s\r\n",
		   recv->data.generic_reply.msg_id,
		   recv->data.generic_reply.code,
		   recv->data.generic_reply.data_len,
		   recv->data.generic_reply.data,
		   recv->data.generic_reply.message_len,
		   recv->data.generic_reply.message);
}

static void demo_dm_recv_property_set(void *dm_handle, const aiot_dm_recv_t *recv, void *userdata)
{
	printf("demo_dm_recv_property_set msg_id = %ld, params = %.*s\r\n",
		   (unsigned long) recv->data.property_set.msg_id,
		   recv->data.property_set.params_len,
		   recv->data.property_set.params);

	/* TODO: 以下代码演示如何对来自云平台的属性设置指令进行应答, 用户可取消注释查看演示效果 */
	auto payload                                = recv->data.property_set.params;
	payload[recv->data.property_set.params_len] = '\0';
	StaticJsonDocument<200> doc;
	DeserializationError    error = deserializeJson(doc, payload);//反序列化JSON数据

	bool all_success = true;
	if (!error)//检查反序列化是否成功
	{
		for (const auto &p: doc.as<JsonObject>()) {
			all_success = all_success && property_set_cb_map[p.key().c_str()](p.value());
		}
		//		async_service_cb_map[recv->data.property_set.service_id](doc.as<JsonVariant>());//将参数传递后打印输出
	}


	aiot_dm_msg_t msg;

	memset(&msg, 0, sizeof(aiot_dm_msg_t));
	msg.type                           = AIOT_DMMSG_PROPERTY_SET_REPLY;
	msg.data.property_set_reply.msg_id = recv->data.property_set.msg_id;
	msg.data.property_set_reply.code   = all_success ? 200 : 100000;
	msg.data.property_set_reply.data   = "{}";
	int32_t res                        = aiot_dm_send(dm_handle, &msg);
	if (res < 0) {
		printf("aiot_dm_send failed\r\n");
	}
}

static void demo_dm_recv_async_service_invoke(void *dm_handle, const aiot_dm_recv_t *recv, void *userdata)
{
	printf("demo_dm_recv_async_service_invoke msg_id = %ld, service_id = %s, params = %.*s\r\n",
		   (unsigned long) recv->data.async_service_invoke.msg_id,
		   recv->data.async_service_invoke.service_id,
		   recv->data.async_service_invoke.params_len,
		   recv->data.async_service_invoke.params);

	/* TODO: 以下代码演示如何对来自云平台的异步服务调用进行应答, 用户可取消注释查看演示效果
        *
        * 注意: 如果用户在回调函数外进行应答, 需要自行保存msg_id, 因为回调函数入参在退出回调函数后将被SDK销毁, 不可以再访问到
	*/

	auto payload                                        = recv->data.async_service_invoke.params;
	payload[recv->data.async_service_invoke.params_len] = '\0';
	StaticJsonDocument<200> doc;
	DeserializationError    error = deserializeJson(doc, payload);//反序列化JSON数据

	if (error) {
		return;
	}

	auto cb_res = async_service_cb_map[recv->data.async_service_invoke.service_id](doc.as<JsonVariant>());//将参数传递后打印输出

	aiot_dm_msg_t msg;

	memset(&msg, 0, sizeof(aiot_dm_msg_t));
	msg.type                            = AIOT_DMMSG_ASYNC_SERVICE_REPLY;
	msg.data.async_service_reply.msg_id = recv->data.async_service_invoke.msg_id;
	msg.data.async_service_reply.code   = cb_res.first ? 200 : 100000;
	//        msg.data.async_service_reply.service_id = "ToggleLightSwitch";
	msg.data.async_service_reply.service_id = recv->data.async_service_invoke.service_id;
	if (cb_res.second) {
		auto ret_Jvar = cb_res.second.value().as<JsonVariantConst>();
		//		char *jsonBuf=new char[measureJson(ret_Jvar)];
		std::string jsonBuf;
		serializeJson(ret_Jvar, jsonBuf);
		msg.data.async_service_reply.data = jsonBuf.data();
	} else {
		msg.data.async_service_reply.data = "{}";
	}
	//        msg.data.async_service_reply.data = "{\"dataA\": 20}";
	int32_t res = aiot_dm_send(dm_handle, &msg);
	if (res < 0) {
		printf("aiot_dm_send failed\r\n");
	}
}

static void demo_dm_recv_sync_service_invoke(void *dm_handle, const aiot_dm_recv_t *recv, void *userdata)
{
	printf("demo_dm_recv_sync_service_invoke msg_id = %ld, rrpc_id = %s, service_id = %s, params = %.*s\r\n",
		   (unsigned long) recv->data.sync_service_invoke.msg_id,
		   recv->data.sync_service_invoke.rrpc_id,
		   recv->data.sync_service_invoke.service_id,
		   recv->data.sync_service_invoke.params_len,
		   recv->data.sync_service_invoke.params);

	/* TODO: 以下代码演示如何对来自云平台的同步服务调用进行应答, 用户可取消注释查看演示效果
        *
        * 注意: 如果用户在回调函数外进行应答, 需要自行保存msg_id和rrpc_id字符串, 因为回调函数入参在退出回调函数后将被SDK销毁, 不可以再访问到
        */

	auto payload                                       = recv->data.sync_service_invoke.params;
	payload[recv->data.sync_service_invoke.params_len] = '\0';
	StaticJsonDocument<200> doc;
	DeserializationError    error = deserializeJson(doc, payload);//反序列化JSON数据

	if (!error)//检查反序列化是否成功
	{
		async_service_cb_map[recv->data.sync_service_invoke.service_id](doc.as<JsonVariant>());//将参数传递后打印输出
	}

	//	sync_service_cb_map[recv->data.sync_service_invoke.service_id](
	//			recv->data.sync_service_invoke.params,
	//			recv->data.sync_service_invoke.params_len);

	/*
    {
        aiot_dm_msg_t msg;

        memset(&msg, 0, sizeof(aiot_dm_msg_t));
        msg.type = AIOT_DMMSG_SYNC_SERVICE_REPLY;
        msg.data.sync_service_reply.rrpc_id = recv->data.sync_service_invoke.rrpc_id;
        msg.data.sync_service_reply.msg_id = recv->data.sync_service_invoke.msg_id;
        msg.data.sync_service_reply.code = 200;
        msg.data.sync_service_reply.service_id = "SetLightSwitchTimer";
        msg.data.sync_service_reply.data = "{}";
        int32_t res = aiot_dm_send(dm_handle, &msg);
        if (res < 0) {
            printf("aiot_dm_send failed\r\n");
        }
    }
    */
}

static void demo_dm_recv_raw_data(void *dm_handle, const aiot_dm_recv_t *recv, void *userdata)
{
	printf("demo_dm_recv_raw_data raw data len = %d\r\n", recv->data.raw_data.data_len);
	/* TODO: 以下代码演示如何发送二进制格式数据, 若使用需要有相应的数据透传脚本部署在云端 */
	/*
    {
        aiot_dm_msg_t msg;
        uint8_t raw_data[] = {0x01, 0x02};

        memset(&msg, 0, sizeof(aiot_dm_msg_t));
        msg.type = AIOT_DMMSG_RAW_DATA;
        msg.data.raw_data.data = raw_data;
        msg.data.raw_data.data_len = sizeof(raw_data);
        aiot_dm_send(dm_handle, &msg);
    }
    */
}

static void demo_dm_recv_raw_sync_service_invoke(void *dm_handle, const aiot_dm_recv_t *recv, void *userdata)
{
	printf("demo_dm_recv_raw_sync_service_invoke raw sync service rrpc_id = %s, data_len = %d\r\n",
		   recv->data.raw_service_invoke.rrpc_id,
		   recv->data.raw_service_invoke.data_len);
}

static void demo_dm_recv_raw_data_reply(void *dm_handle, const aiot_dm_recv_t *recv, void *userdata)
{
	printf("demo_dm_recv_raw_data_reply receive reply for up_raw msg, data len = %d\r\n", recv->data.raw_data.data_len);
	/* TODO: 用户处理下行的二进制数据, 位于recv->data.raw_data.data中 */
}

/* 用户数据接收处理回调函数 */
static void demo_dm_recv_handler(void *dm_handle, const aiot_dm_recv_t *recv, void *userdata)
{
	printf("demo_dm_recv_handler, type = %d\r\n", recv->type);

	switch (recv->type) {

		/* 属性上报, 事件上报, 获取期望属性值或者删除期望属性值的应答 */
		case AIOT_DMRECV_GENERIC_REPLY: {
			demo_dm_recv_generic_reply(dm_handle, recv, userdata);
		} break;

		/* 属性设置 */
		case AIOT_DMRECV_PROPERTY_SET: {
			demo_dm_recv_property_set(dm_handle, recv, userdata);
		} break;

		/* 异步服务调用 */
		case AIOT_DMRECV_ASYNC_SERVICE_INVOKE: {
			demo_dm_recv_async_service_invoke(dm_handle, recv, userdata);
		} break;

		/* 同步服务调用 */
		case AIOT_DMRECV_SYNC_SERVICE_INVOKE: {
			demo_dm_recv_sync_service_invoke(dm_handle, recv, userdata);
		} break;

		/* 下行二进制数据 */
		case AIOT_DMRECV_RAW_DATA: {
			demo_dm_recv_raw_data(dm_handle, recv, userdata);
		} break;

		/* 二进制格式的同步服务调用, 比单纯的二进制数据消息多了个rrpc_id */
		case AIOT_DMRECV_RAW_SYNC_SERVICE_INVOKE: {
			demo_dm_recv_raw_sync_service_invoke(dm_handle, recv, userdata);
		} break;

		/* 上行二进制数据后, 云端的回复报文 */
		case AIOT_DMRECV_RAW_DATA_REPLY: {
			demo_dm_recv_raw_data_reply(dm_handle, recv, userdata);
		} break;

		default:
			break;
	}
}

/* 属性上报函数演示 */
int32_t demo_send_property_post(void *dm_handle, char *params)
{
	aiot_dm_msg_t msg;

	memset(&msg, 0, sizeof(aiot_dm_msg_t));
	msg.type                      = AIOT_DMMSG_PROPERTY_POST;
	msg.data.property_post.params = params;

	return aiot_dm_send(dm_handle, &msg);
}

int32_t demo_send_property_batch_post(void *dm_handle, char *params)
{
	aiot_dm_msg_t msg;

	memset(&msg, 0, sizeof(aiot_dm_msg_t));
	msg.type                      = AIOT_DMMSG_PROPERTY_BATCH_POST;
	msg.data.property_post.params = params;

	return aiot_dm_send(dm_handle, &msg);
}

/* 事件上报函数演示 */
int32_t demo_send_event_post(void *dm_handle, char *event_id, char *params)
{
	aiot_dm_msg_t msg;

	memset(&msg, 0, sizeof(aiot_dm_msg_t));
	msg.type                     = AIOT_DMMSG_EVENT_POST;
	msg.data.event_post.event_id = event_id;
	msg.data.event_post.params   = params;

	return aiot_dm_send(dm_handle, &msg);
}

/* 演示了获取属性LightSwitch的期望值, 用户可将此函数加入到main函数中运行演示 */
int32_t demo_send_get_desred_requset(void *dm_handle)
{
	aiot_dm_msg_t msg;

	memset(&msg, 0, sizeof(aiot_dm_msg_t));
	msg.type                    = AIOT_DMMSG_GET_DESIRED;
	msg.data.get_desired.params = "[\"LightSwitch\"]";

	return aiot_dm_send(dm_handle, &msg);
}

/* 演示了删除属性LightSwitch的期望值, 用户可将此函数加入到main函数中运行演示 */
int32_t demo_send_delete_desred_requset(void *dm_handle)
{
	aiot_dm_msg_t msg;

	memset(&msg, 0, sizeof(aiot_dm_msg_t));
	msg.type                    = AIOT_DMMSG_DELETE_DESIRED;
	msg.data.get_desired.params = "{\"LightSwitch\":{}}";

	return aiot_dm_send(dm_handle, &msg);
}


/* 事件处理回调,  */
void demo_ntp_event_handler(void *handle, const aiot_ntp_event_t *event, void *userdata)
{
	switch (event->type) {
		case AIOT_NTPEVT_INVALID_RESPONSE: {
			printf("AIOT_NTPEVT_INVALID_RESPONSE\n");
		} break;
		case AIOT_NTPEVT_INVALID_TIME_FORMAT: {
			printf("AIOT_NTPEVT_INVALID_TIME_FORMAT\n");
		} break;
		default: {
		}
	}
}

/* TODO: 数据处理回调, 当SDK从网络上收到ntp消息时被调用 */
void demo_ntp_recv_handler(void *handle, const aiot_ntp_recv_t *packet, void *userdata)
{
	switch (packet->type) {
		/* TODO: 结构体 aiot_ntp_recv_t{} 中包含当前时区下, 年月日时分秒的数值, 可在这里把它们解析储存起来 */
		case AIOT_NTPRECV_LOCAL_TIME: {
			//			printf("local time: %llu, %02lu/%02lu/%02lu-%02lu:%02lu:%02lu:%lu\n",
			//				   (long long unsigned int) packet->data.local_time.timestamp,
			//				   packet->data.local_time.year,
			//				   packet->data.local_time.mon,
			//				   packet->data.local_time.day,
			//				   packet->data.local_time.hour,
			//				   packet->data.local_time.min,
			//				   packet->data.local_time.sec,
			//				   packet->data.local_time.msec);

			ntp_recv_cb(packet);
		} break;

		default: {
		}
	}
}

/* 下载收包回调, 用户调用 aiot_download_recv() 后, SDK收到数据会进入这个函数, 把下载到的数据交给用户 */
/* TODO: 一般来说, 设备升级时, 会在这个回调中, 把下载到的数据写到Flash上 */
void user_download_recv_handler(void *handle, const aiot_download_recv_t *packet, void *userdata)
{
	int32_t  percent         = 0;
	int32_t  last_percent    = 0;
	uint32_t data_buffer_len = 0;

	/* 目前只支持 packet->type 为 AIOT_DLRECV_HTTPBODY 的情况 */
	if (!packet || AIOT_DLRECV_HTTPBODY != packet->type) {
		return;
	}
	percent = packet->data.percent;

	/* 如果 percent 为负数, 说明发生了收包异常或者digest校验错误 */
	if (percent < 0) {
		/* digest校验错误 */
		printf("exception happend, percent is %d\r\n", percent);
		if (userdata) {
			free(userdata);
		}
		return;
	}

	/* userdata可以存放 demo_download_recv_handler() 的不同次进入之间, 需要共享的数据 */
	/* 这里用来存放上一次进入本回调函数时, 下载的固件进度百分比 */
	if (userdata) {
		last_percent = *((uint32_t *) (userdata));
	}
	data_buffer_len = packet->data.len;

	/*
     * TODO: 下载一段固件成功, 这个时候, 用户应该将
     *       起始地址为 packet->data.buffer, 长度为 packet->data.len 的内存, 保存到flash上
     *
     *       如果烧写flash失败, 还应该调用 aiot_download_report_progress(handle, -4) 将失败上报给云平台
     *       备注:协议中, 与云平台商定的错误码在 aiot_ota_protocol_errcode_t 类型中, 例如
     *            -1: 表示升级失败
     *            -2: 表示下载失败
     *            -3: 表示校验失败
     *            -4: 表示烧写失败
     *
     *       详情可见 https://help.aliyun.com/document_detail/85700.html
     */

	ota_download_buffer_write_cb(packet->data.buffer, packet->data.len);

	/* percent 入参的值为 100 时, 说明SDK已经下载固件内容全部完成 */
	if (percent == 100) {
		/* 上报版本号 */
		/*
         * TODO: 这个时候, 一般用户就应该完成所有的固件烧录, 保存当前工作, 重启设备, 切换到新的固件上启动了
         *       并且, 新的固件必须要以
         *
         *       aiot_ota_report_version(g_ota_handle, new_version);
         *
         *       这样的操作, 将升级后的新版本号(比如1.0.0升到1.1.0, 则new_version的值是"1.1.0")上报给云平台
         *       云平台收到了新的版本号上报后, 才会判定升级成功, 否则会认为本次升级失败了
         *
         */
		ota_download_down_cb();
	}

	/* 简化输出, 只有距离上次的下载进度增加5%以上时, 才会打印进度, 并向服务器上报进度 */
	if (percent - last_percent >= 5 || percent == 100) {
		printf("download %03d%% done, +%d bytes\r\n", percent, data_buffer_len);
		aiot_download_report_progress(handle, percent);

		if (userdata) {
			*((uint32_t *) (userdata)) = percent;
		}
		if (percent == 100 && userdata) {
			free(userdata);
		}
	}
}


/* 用户通过 aiot_ota_setopt() 注册的OTA消息处理回调, 如果SDK收到了OTA相关的MQTT消息, 会自动识别, 调用这个回调函数 */
void user_ota_recv_handler(void *ota_handle, aiot_ota_recv_t *ota_msg, void *userdata)
{
	switch (ota_msg->type) {
		case AIOT_OTARECV_FOTA: {
			uint16_t                   port           = 443;
			uint32_t                   max_buffer_len = 2048;
			aiot_sysdep_network_cred_t cred;
			void                      *dl_handle    = NULL;
			void                      *last_percent = NULL;

			if (NULL == ota_msg->task_desc || ota_msg->task_desc->protocol_type != AIOT_OTA_PROTOCOL_HTTPS) {
				break;
			}

			dl_handle = aiot_download_init();
			if (NULL == dl_handle) {
				break;
			}

			last_percent = malloc(sizeof(uint32_t));
			if (NULL == last_percent) {
				aiot_download_deinit(&dl_handle);
				break;
			}
			memset(last_percent, 0, sizeof(uint32_t));

			printf("OTA target firmware version: %s, size: %u Bytes\r\n", ota_msg->task_desc->version, ota_msg->task_desc->size_total);

			if (NULL != ota_msg->task_desc->extra_data) {
				printf("extra data: %s\r\n", ota_msg->task_desc->extra_data);
			}

			g_firmware_size = ota_msg->task_desc->size_total;

			memset(&cred, 0, sizeof(aiot_sysdep_network_cred_t));
			cred.option               = AIOT_SYSDEP_NETWORK_CRED_SVRCERT_CA;
			cred.max_tls_fragment     = 16384;
			cred.x509_server_cert     = ali_ca_cert;
			cred.x509_server_cert_len = strlen(ali_ca_cert);
			uint32_t end              = g_firmware_size / 2;
			/* 设置下载时为TLS下载 */
			if ((STATE_SUCCESS != aiot_download_setopt(dl_handle, AIOT_DLOPT_NETWORK_CRED, (void *) (&cred)))
				/* 设置下载时访问的服务器端口号 */
				|| (STATE_SUCCESS != aiot_download_setopt(dl_handle, AIOT_DLOPT_NETWORK_PORT, (void *) (&port)))
				/* 设置下载的任务信息, 通过输入参数 ota_msg 中的 task_desc 成员得到, 内含下载地址, 固件大小, 固件签名等 */
				|| (STATE_SUCCESS != aiot_download_setopt(dl_handle, AIOT_DLOPT_TASK_DESC, (void *) (ota_msg->task_desc)))
				/* 设置下载内容到达时, SDK将调用的回调函数 */
				|| (STATE_SUCCESS != aiot_download_setopt(dl_handle, AIOT_DLOPT_RECV_HANDLER, (void *) (user_download_recv_handler)))
				/* 设置单次下载最大的buffer长度, 每当这个长度的内存读满了后会通知用户 */
				|| (STATE_SUCCESS != aiot_download_setopt(dl_handle, AIOT_DLOPT_BODY_BUFFER_MAX_LEN, (void *) (&max_buffer_len)))
				/* 设置 AIOT_DLOPT_RECV_HANDLER 的不同次调用之间共享的数据, 比如例程把进度存在这里 */
				|| (STATE_SUCCESS != aiot_download_setopt(dl_handle, AIOT_DLOPT_USERDATA, (void *) last_percent))
				/* 指明下载方式是按照range下载, 并且当前只下载一半 */
				|| (STATE_SUCCESS != aiot_download_setopt(dl_handle, AIOT_DLOPT_RANGE_END, (void *) &end))
				/* 发送http的GET请求给http服务器 */
				|| (STATE_SUCCESS != aiot_download_send_request(dl_handle))) {
				aiot_download_deinit(&dl_handle);
				free(last_percent);
				break;
			}
			g_dl_handle = dl_handle;
			break;
		}
		default:
			break;
	}
}


//class Alink {
//   private:
//	static int32_t                    res;
//	static void                      *dm_handle;
//	static void                      *mqtt_handle;
//	static aiot_sysdep_network_cred_t cred; /* 安全凭据结构体, 如果要用TLS, 这个结构体中配置CA证书等参数 */
//	static uint8_t                    post_reply;
//	static int8_t                     time_zone;
//	static void                      *ntp_handle;
//	static std::string                cur_version;
//	static void                      *ota_handle;
////	static uint32_t                   timeout_ms = 0;
//
//
//   public:
int32_t ALink::begin(const char *_productKey, const char *_deviceName, const char *_deviceSecret, const char *_instanceID)
{
	product_key   = _productKey;
	device_name   = _deviceName;
	device_secret = _deviceSecret;
	instance_id   = _instanceID;
	//	mqtt_host     = std::format("{}.mqtt.iothub.aliyuncs.com", instance_id).data();
	sprintf(mqtt_host, "%s.mqtt.iothub.aliyuncs.com", instance_id);

	res         = STATE_SUCCESS;
	dm_handle   = NULL;
	mqtt_handle = NULL;
	post_reply  = 1;

	/* 配置SDK的底层依赖 */
	aiot_sysdep_set_portfile(&g_aiot_sysdep_portfile);
	/* 配置SDK的日志输出 */
	aiot_state_set_logcb(demo_state_logcb);

	/* 创建SDK的安全凭据, 用于建立TLS连接 */
	memset(&cred, 0, sizeof(aiot_sysdep_network_cred_t));
	cred.option               = AIOT_SYSDEP_NETWORK_CRED_SVRCERT_CA; /* 使用RSA证书校验MQTT服务端 */
	cred.max_tls_fragment     = 16384;               /* 最大的分片长度为16K, 其它可选值还有4K, 2K, 1K, 0.5K */
	cred.sni_enabled          = 1;                   /* TLS建连时, 支持Server Name Indicator */
	cred.x509_server_cert     = ali_ca_cert;         /* 用来验证MQTT服务端的RSA根证书 */
	cred.x509_server_cert_len = strlen(ali_ca_cert); /* 用来验证MQTT服务端的RSA根证书长度 */

	/* 创建1个MQTT客户端实例并内部初始化默认参数 */
	mqtt_handle = aiot_mqtt_init();
	if (mqtt_handle == NULL) {
		printf("aiot_mqtt_init failed\n");
		return -1;
	}

	/* 配置MQTT服务器地址 */
	aiot_mqtt_setopt(mqtt_handle, AIOT_MQTTOPT_HOST, (void *) mqtt_host);
	/* 配置MQTT服务器端口 */
	aiot_mqtt_setopt(mqtt_handle, AIOT_MQTTOPT_PORT, (void *) &port);
	/* 配置设备productKey */
	aiot_mqtt_setopt(mqtt_handle, AIOT_MQTTOPT_PRODUCT_KEY, (void *) product_key);
	/* 配置设备deviceName */
	aiot_mqtt_setopt(mqtt_handle, AIOT_MQTTOPT_DEVICE_NAME, (void *) device_name);
	/* 配置设备deviceSecret */
	aiot_mqtt_setopt(mqtt_handle, AIOT_MQTTOPT_DEVICE_SECRET, (void *) device_secret);
	/* 配置网络连接的安全凭据, 上面已经创建好了 */
	aiot_mqtt_setopt(mqtt_handle, AIOT_MQTTOPT_NETWORK_CRED, (void *) &cred);
	/* 配置MQTT事件回调函数 */
	aiot_mqtt_setopt(mqtt_handle, AIOT_MQTTOPT_EVENT_HANDLER, (void *) demo_mqtt_event_handler);

	/* 创建DATA-MODEL实例 */
	dm_handle = aiot_dm_init();
	if (dm_handle == NULL) {
		printf("aiot_dm_init failed");
		return -1;
	}
	/* 配置MQTT实例句柄 */
	aiot_dm_setopt(dm_handle, AIOT_DMOPT_MQTT_HANDLE, mqtt_handle);
	/* 配置消息接收处理回调函数 */
	aiot_dm_setopt(dm_handle, AIOT_DMOPT_RECV_HANDLER, (void *) demo_dm_recv_handler);

	/* 配置是云端否需要回复post_reply给设备. 如果为1, 表示需要云端回复, 否则表示不回复 */
	aiot_dm_setopt(dm_handle, AIOT_DMOPT_POST_REPLY, (void *) &post_reply);

	/* 与服务器建立MQTT连接 */
	res = aiot_mqtt_connect(mqtt_handle);
	if (res < STATE_SUCCESS) {
		/* 尝试建立连接失败, 销毁MQTT实例, 回收资源 */
		aiot_dm_deinit(&dm_handle);
		aiot_mqtt_deinit(&mqtt_handle);
		printf("aiot_mqtt_connect failed: -0x%04X\n\r\n", -res);
		printf("please check variables like mqtt_host, produt_key, device_name, device_secret in demo\r\n");
		return -1;
	}
	DEBUG_PRINT_LINE("mqtt connected");
	/* 向服务器订阅property/batch/post_reply这个topic */
	//	std::string topic = std::format("/sys/{}/{}/thing/event/property/batch/post_reply", product_key, device_name);
	char topic[256];
	sprintf(topic, "/sys/%s/%s/thing/event/property/batch/post_reply", product_key, device_name);
	aiot_mqtt_sub(mqtt_handle, topic, NULL, 1, NULL);
	DEBUG_PRINT_LINE("topic subscribed");

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	attr.stacksize = 1024 * 16;
	DEBUG_PRINT_LINE("default stacksize: %d", attr.stacksize);

	/* 创建一个单独的线程, 专用于执行aiot_mqtt_process, 它会自动发送心跳保活, 以及重发QoS1的未应答报文 */
	g_mqtt_process_thread_running = 1;
	res                           = pthread_create(&g_mqtt_process_thread, &attr, demo_mqtt_process_thread, mqtt_handle);
	if (res < 0) {
		printf("pthread_create demo_mqtt_process_thread failed: %d\n", res);
		aiot_dm_deinit(&dm_handle);
		aiot_mqtt_deinit(&mqtt_handle);
		return -1;
	}

	/* 创建一个单独的线程用于执行aiot_mqtt_recv, 它会循环收取服务器下发的MQTT消息, 并在断线时自动重连 */
	g_mqtt_recv_thread_running = 1;
	res                        = pthread_create(&g_mqtt_recv_thread, &attr, demo_mqtt_recv_thread, mqtt_handle);
	if (res < 0) {
		printf("pthread_create demo_mqtt_recv_thread failed: %d\n", res);
		aiot_dm_deinit(&dm_handle);
		aiot_mqtt_deinit(&mqtt_handle);
		return -1;
	}

	return 0;
}

int32_t ALink::beginNTP(std::function<void(const aiot_ntp_recv_t *)> _ntp_recv_cb, int8_t _time_zone)
{
	ntp_recv_cb = std::move(_ntp_recv_cb);
	time_zone   = _time_zone;
	/* 创建1个ntp客户端实例并内部初始化默认参数 */
	ntp_handle = aiot_ntp_init();
	if (ntp_handle == NULL) {
		//			demo_mqtt_stop(&mqtt_handle);
		printf("aiot_ntp_init failed\n");
		return -1;
	}

	res = aiot_ntp_setopt(ntp_handle, AIOT_NTPOPT_MQTT_HANDLE, mqtt_handle);
	if (res < STATE_SUCCESS) {
		printf("aiot_ntp_setopt AIOT_NTPOPT_MQTT_HANDLE failed, res: -0x%04X\n", -res);
		aiot_ntp_deinit(&ntp_handle);
		//			demo_mqtt_stop(&mqtt_handle);
		return -1;
	}

	res = aiot_ntp_setopt(ntp_handle, AIOT_NTPOPT_TIME_ZONE, (int8_t *) &time_zone);
	if (res < STATE_SUCCESS) {
		printf("aiot_ntp_setopt AIOT_NTPOPT_TIME_ZONE failed, res: -0x%04X\n", -res);
		aiot_ntp_deinit(&ntp_handle);
		//			demo_mqtt_stop(&mqtt_handle);
		return -1;
	}

	/* TODO: NTP消息回应从云端到达设备时, 会进入此处设置的回调函数 */
	res = aiot_ntp_setopt(ntp_handle, AIOT_NTPOPT_RECV_HANDLER, (void *) demo_ntp_recv_handler);
	if (res < STATE_SUCCESS) {
		printf("aiot_ntp_setopt AIOT_NTPOPT_RECV_HANDLER failed, res: -0x%04X\n", -res);
		aiot_ntp_deinit(&ntp_handle);
		//			demo_mqtt_stop(&mqtt_handle);
		return -1;
	}

	res = aiot_ntp_setopt(ntp_handle, AIOT_NTPOPT_EVENT_HANDLER, (void *) demo_ntp_event_handler);
	if (res < STATE_SUCCESS) {
		printf("aiot_ntp_setopt AIOT_NTPOPT_EVENT_HANDLER failed, res: -0x%04X\n", -res);
		aiot_ntp_deinit(&ntp_handle);
		//			demo_mqtt_stop(&mqtt_handle);
		return -1;
	}
	return 0;
}

int32_t ALink::requestNTP()
{
	/* 发送NTP查询请求给云平台 */
	res = aiot_ntp_send_request(ntp_handle);
	if (res < STATE_SUCCESS) {
		aiot_ntp_deinit(&ntp_handle);
		//			demo_mqtt_stop(&mqtt_handle);
		return -1;
	}
	return 0;
}

int32_t ALink::beginOTA(const std::string &_cur_version)
{
	/* 与MQTT例程不同的是, 这里需要增加创建OTA会话实例的语句 */
	ota_handle = aiot_ota_init();
	if (NULL == ota_handle) {
		return -1;
	}

	/* 用以下语句, 把OTA会话和MQTT会话关联起来 */
	aiot_ota_setopt(ota_handle, AIOT_OTAOPT_MQTT_HANDLE, mqtt_handle);
	/* 用以下语句, 设置OTA会话的数据接收回调, SDK收到OTA相关推送时, 会进入这个回调函数 */
	aiot_ota_setopt(ota_handle, AIOT_OTAOPT_RECV_HANDLER, (void *) (user_ota_recv_handler));
	g_ota_handle = ota_handle;


	/*   TODO: 非常重要!!!
     *
     *   cur_version 要根据用户实际情况, 改成从设备的配置区获取, 要反映真实的版本号, 而不能像示例这样写为固定值
     *
     *   1. 如果设备从未上报过版本号, 在控制台网页将无法部署升级任务
     *   2. 如果设备升级完成后, 上报的不是新的版本号, 在控制台网页将会显示升级失败
     *
     */

	cur_version = _cur_version;
	/* 演示MQTT连接建立起来之后, 就可以上报当前设备的版本号了 */
	res = aiot_ota_report_version(ota_handle, cur_version.data());
	if (res < STATE_SUCCESS) {
		printf("report version failed, code is -0x%04X\r\n", -res);
		return -1;
	}
	return 0;
}

void ALink::registerDownloadBufferWriteCb(std::function<void(const void *, size_t)> _cb)
{
	ota_download_buffer_write_cb = std::move(_cb);
}

void ALink::registerDownloadDownCb(std::function<void()> _cb)
{
	ota_download_down_cb = std::move(_cb);
}

int32_t ALink::end()
{

	/* 停止收发动作 */
	g_mqtt_process_thread_running = 0;
	g_mqtt_recv_thread_running    = 0;

	/* 断开MQTT连接, 一般不会运行到这里 */
	res = aiot_mqtt_disconnect(mqtt_handle);
	if (res < STATE_SUCCESS) {
		aiot_dm_deinit(&dm_handle);
		aiot_mqtt_deinit(&mqtt_handle);
		printf("aiot_mqtt_disconnect failed: -0x%04X\n", -res);
		return -1;
	}

	/* 销毁DATA-MODEL实例, 一般不会运行到这里 */
	res = aiot_dm_deinit(&dm_handle);
	if (res < STATE_SUCCESS) {
		printf("aiot_dm_deinit failed: -0x%04X\n", -res);
		return -1;
	}

	/* 销毁MQTT实例, 一般不会运行到这里 */
	res = aiot_mqtt_deinit(&mqtt_handle);
	if (res < STATE_SUCCESS) {
		printf("aiot_mqtt_deinit failed: -0x%04X\n", -res);
		return -1;
	}

	pthread_join(g_mqtt_process_thread, NULL);
	pthread_join(g_mqtt_recv_thread, NULL);

	return 0;
}

/* TODO: 以下代码演示了简单的属性上报和事件上报, 用户可取消注释观察演示效果 */
//	demo_send_property_post(dm_handle, "{\"LightSwitch\": 0}");
/*
        demo_send_event_post(dm_handle, "Error", "{\"ErrorCode\": 0}");
        */

/* TODO: 以下代码演示了基于模块的物模型的上报, 用户可取消注释观察演示效果
         * 本例需要用户在产品的功能定义的页面中, 点击"编辑草稿", 增加一个名为demo_extra_block的模块,
         * 再到该模块中, 通过添加标准功能, 选择一个名为NightLightSwitch的物模型属性, 再点击"发布上线".
         * 有关模块化的物模型的概念, 请见 https://help.aliyun.com/document_detail/73727.html
        */
/*
        demo_send_property_post(dm_handle, "{\"demo_extra_block:NightLightSwitch\": 1}");
        */

/* TODO: 以下代码显示批量上报用户数据, 用户可取消注释观察演示效果
         * 具体数据格式请见https://help.aliyun.com/document_detail/89301.html 的"设备批量上报属性、事件"一节
        */
/*
        demo_send_property_batch_post(dm_handle,
                                      "{\"properties\":{\"Power\": [ {\"value\":\"on\",\"time\":1612684518}],\"WF\": [{\"value\": 3,\"time\":1612684518}]}}");
        */

template<typename T>
int32_t ALink::sendPropertyPost(std::string property, T value)
{
	std::string params_tmp;
	params_tmp += "{\"";
	params_tmp += property;
	params_tmp += "\":";
	if constexpr (std::is_arithmetic_v<T>) {
		// 对于算术类型（整数、浮点数），使用std::to_string
		params_tmp += std::to_string(value);
	} else if constexpr (std::is_same_v<T, std::string>) {
		// 对于std::string类型，直接添加引号
		params_tmp += "\"" + value + "\"";
	} else {
		// 对于其他类型，这里可以添加特定的转换逻辑或抛出异常
		//		static_assert(false, "Unsupported type for ALink::sendPropertyPost");
		params_tmp += "\"" + std::string{"Unsupported type"} + typeid(T).name() + "\"";
	}
	params_tmp += "}";
	//	return demo_send_property_post(dm_handle, std::format("{{\"{}\": {}}}", property, value).data());
	return demo_send_property_post(dm_handle, params_tmp.data());
}


template<typename T>
int32_t ALink::sendPropertyBatchPost(const std::list<std::tuple<std::string, std::list<std::tuple<T, uint64_t>>>> properties)
{
	std::string property_batch_data = "{\"properties\":{";
	for (auto property: properties) {
		//	property_batch_data +=std::format("\"{}\": [{{\"value\":{},\"time\":{}}}]", std::get<0>(property), std::get<1>(property), std::get<2>(property));
		//		property_batch_data += std::format("\"{}\": [", std::get<0>(property));
		property_batch_data += "\"";
		property_batch_data += std::get<0>(property);
		property_batch_data += "\": [";
		for (const auto &datum: std::get<1>(property)) {
			//			property_batch_data += std::format("{\"value\": {}, \"time\": {}},", std::get<0>(datum), std::get<1>(datum));
			property_batch_data += "{\"value\": ";
			property_batch_data += std::get<0>(datum);
			property_batch_data += ", \"time\": ";
			char time_str[21] = {0};
			core_uint642str(std::get<1>(datum), time_str, NULL);
			property_batch_data += time_str;
			property_batch_data += "},";
		}
		if (!property_batch_data.empty()) {
			property_batch_data.pop_back();// 删除最后一个字符
		}
		property_batch_data += "],";
	}
	if (!property_batch_data.empty()) {
		property_batch_data.pop_back();// 删除最后一个字符
	}
	property_batch_data += "}}";
	return demo_send_property_batch_post(ALink::dm_handle, property_batch_data.data());
}

#define TEMPLATE_INSTANTIATE(T)                                                                       \
	template int32_t ALink::sendPropertyPost(std::string property, T value);                          \
	template int32_t ALink::sendPropertyBatchPost(                                                    \
			const std::list<std::tuple<std::string, std::list<std::tuple<T, uint64_t>>>> properties); \
	template int32_t ALink::sendEventPost(std::string event, const std::list<std::tuple<std::string, T>> params);

#define TEMPLATE_INSTANTIATE_ALL_TYPES() \
	TEMPLATE_INSTANTIATE(std::string)    \
	TEMPLATE_INSTANTIATE(float)          \
	TEMPLATE_INSTANTIATE(double)         \
	TEMPLATE_INSTANTIATE(int32_t)        \
	TEMPLATE_INSTANTIATE(int64_t)        \
	TEMPLATE_INSTANTIATE(bool)

TEMPLATE_INSTANTIATE_ALL_TYPES()

//template int32_t ALink::sendPropertyBatchPost(
//		std::list<std::tuple<std::string, std::list<std::tuple<std::string , time_t>>>> properties);
//{
//	return _sendPropertyBatchPost<std::string>(property, properties);
//}

template<typename T>
int32_t ALink::sendEventPost(std::string event, const std::list<std::tuple<std::string, T>> params)
{
	std::string event_params = "{";
	for (auto param: params) {
		//		event_params += std::format("\"{}\":{},", std::get<0>(param), std::get<1>(param));
		event_params += "\"";
		event_params += std::get<0>(param);
		event_params += "\":";
		event_params += std::get<1>(param);
		event_params += ",";
	}
	event_params += "}";
	return demo_send_event_post(dm_handle, event.data(), event_params.data());
}

void ALink::registerAsyncService(
		const std::string &service_id, std::function<std::pair<bool, std::optional<JsonVariant>>(JsonVariant)> callback)
{
	async_service_cb_map[service_id] = std::move(callback);
}

void ALink::unregisterAsyncService(const std::string &service_id)
{
	async_service_cb_map.erase(service_id);
}

void ALink::registerSyncService(const std::string &service_id, std::function<bool(JsonVariant)> callback)
{
	sync_service_cb_map[service_id] = std::move(callback);
}

void ALink::unregisterSyncService(const std::string &service_id)
{
	sync_service_cb_map.erase(service_id);
}

void ALink::registerPropertySet(const std::string &prop, std::function<bool(JsonVariant)> callback)
{
	property_set_cb_map[prop] = std::move(callback);
}

void ALink::registerGenericReply(std::function<void(int, int, std::string, std::string)> callback)
{
	generic_reply_cb = std::move(callback);
}
//template<typename T>
//int32_t ALink::makePropertyBatch(const std::string key, const std::list<std::tuple<T, time_t>> properties)
//{
//	return 0;
//}
//};
