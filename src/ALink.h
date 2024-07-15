//
// Created by 高崧淇 on 24-7-12.
//

#ifndef BME_DEV_KIT_ODOR_ALINK_H
#define BME_DEV_KIT_ODOR_ALINK_H

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
//#include <format>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <utility>

#include "ArduinoJson.h"

#include "csdk/aiot_compress_api.h"
#include "csdk/aiot_dm_api.h"
#include "csdk/aiot_mqtt_api.h"
#include "csdk/aiot_ntp_api.h"
#include "csdk/aiot_ota_api.h"
#include "csdk/aiot_state_api.h"
#include "csdk/aiot_sysdep_api.h"

#define DEBUG_PRINT_LINE(fmt, ...)                    \
	printf("File: %s, Line: %d\t", __FILE__, __LINE__); \
	printf(fmt "\n", ##__VA_ARGS__);

class ALink {
   public:
	static int32_t                    res;
	static void                      *dm_handle;
	static void                      *mqtt_handle;
	static aiot_sysdep_network_cred_t cred; /* 安全凭据结构体, 如果要用TLS, 这个结构体中配置CA证书等参数 */
	static uint8_t                    post_reply;
	static int8_t                     time_zone;
	static void                      *ntp_handle;
	static std::string                cur_version;
	static void                      *ota_handle;
	//	static uint32_t                   timeout_ms = 0;


   public:
	static int32_t begin(const char *_productKey, const char *_deviceName, const char *_deviceSecret, const char *_instanceID);

	static int32_t beginNTP(std::function<void(const aiot_ntp_recv_t *)> _ntp_recv_cb, int8_t _time_zone = 8);

	static int32_t requestNTP();

	static int32_t beginOTA(const std::string &_cur_version);

	static void registerDownloadBufferWriteCb(std::function<void(const void *, size_t)> _cb);

	static void registerDownloadDownCb(std::function<void()> _cb);

	static int32_t end();

	template<typename T>
	static int32_t sendPropertyPost(std::string property, T value);


	template<typename T>
	static int32_t sendPropertyBatchPost(const std::list<std::tuple<std::string, std::list<std::tuple<T, time_t>>>> properties);

	template<typename T>
	static int32_t sendEventPost(std::string event, const std::list<std::tuple<std::string, T>> params);

	static void registerAsyncService(
			const std::string &service_id, std::function<std::pair<bool, std::optional<JsonVariant>>(JsonVariant)> callback);

	static void unregisterAsyncService(const std::string &service_id);

	static void registerSyncService(const std::string &service_id, std::function<bool(JsonVariant)> callback);

	static void unregisterSyncService(const std::string &service_id);

	static void registerPropertySet(const std::string &prop, std::function<bool(JsonVariant)> callback);
	static void registerGenericReply(std::function<void(int, int, std::string, std::string)> callback);
};


//namespace std {
//
//template<typename T>
//class optional {
//   public:
//	optional() = default;
//
//	optional(const optional &)            = default;
//	optional &operator=(const optional &) = default;
//
//	optional(optional &&)            = default;
//	optional &operator=(optional &&) = default;
//
//	~optional() = default;
//
//	template<typename... Args>
//	explicit optional(Args &&...args) : _value(true, T(std::forward<Args>(args)...))
//	{}
//
//	explicit operator bool() const
//	{
//		return _value.first;
//	}
//
//	T &value()
//	{
//		return _value.second;
//	}
//
//	const T &value() const
//	{
//		return _value.second;
//	}
//
//	T *operator->()
//	{
//		return &(_value.second);
//	}
//
//	const T *operator->() const
//	{
//		return &(_value.second);
//	}
//
//	T &operator*()
//	{
//		return _value.second;
//	}
//
//	const T &operator*() const
//	{
//		return _value.second;
//	}
//
//   private:
//	std::pair<bool, T> _value;
//};
//
//}// namespace std

#endif//BME_DEV_KIT_ODOR_ALINK_H
