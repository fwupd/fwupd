/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: device_info.proto */

#ifndef PROTOBUF_C_device_5finfo_2eproto__INCLUDED
#define PROTOBUF_C_device_5finfo_2eproto__INCLUDED

#include <protobuf-c/protobuf-c.h>

PROTOBUF_C__BEGIN_DECLS

#if PROTOBUF_C_VERSION_NUMBER < 1003000
# error This file was generated by a newer version of protoc-c which is incompatible with your libprotobuf-c headers. Please update your headers.
#elif 1004000 < PROTOBUF_C_MIN_COMPILER_VERSION
# error This file was generated by an older version of protoc-c which is incompatible with your libprotobuf-c headers. Please regenerate this file with a newer version of protoc-c.
#endif

typedef struct Logi__Device__Proto__GetDeviceInfoRequest Logi__Device__Proto__GetDeviceInfoRequest;
typedef struct Logi__Device__Proto__GetDeviceInfoResponse
    Logi__Device__Proto__GetDeviceInfoResponse;

/* --- enums --- */

/* --- messages --- */

/*
 **
 * Request Device information
 * This is to be included in UsbMsg
 * EXPECTED RESPONSE
 * GetDeviceInfoResponse
 */
struct Logi__Device__Proto__GetDeviceInfoRequest {
	ProtobufCMessage base;
	/*
	 **
	 * Unused. Reserved for future use.
	 */
	protobuf_c_boolean reserved;
};
#define LOGI__DEVICE__PROTO__GET_DEVICE_INFO_REQUEST__INIT                                         \
	{                                                                                          \
		PROTOBUF_C_MESSAGE_INIT(&logi__device__proto__get_device_info_request__descriptor) \
		, 0                                                                                \
	}

/*
 **
 * Get device information response
 */
struct Logi__Device__Proto__GetDeviceInfoResponse {
	ProtobufCMessage base;
	/*
	 **
	 * payload contains actual mqtt message
	 */
	char *payload;
};
#define LOGI__DEVICE__PROTO__GET_DEVICE_INFO_RESPONSE__INIT                                        \
	{                                                                                          \
		PROTOBUF_C_MESSAGE_INIT(                                                           \
		    &logi__device__proto__get_device_info_response__descriptor)                    \
		, (char *)protobuf_c_empty_string                                                  \
	}

/* Logi__Device__Proto__GetDeviceInfoRequest methods */
void
logi__device__proto__get_device_info_request__init(
    Logi__Device__Proto__GetDeviceInfoRequest *message);
size_t
logi__device__proto__get_device_info_request__get_packed_size(
    const Logi__Device__Proto__GetDeviceInfoRequest *message);
size_t
logi__device__proto__get_device_info_request__pack(
    const Logi__Device__Proto__GetDeviceInfoRequest *message,
    uint8_t *out);
size_t
logi__device__proto__get_device_info_request__pack_to_buffer(
    const Logi__Device__Proto__GetDeviceInfoRequest *message,
    ProtobufCBuffer *buffer);
Logi__Device__Proto__GetDeviceInfoRequest *
logi__device__proto__get_device_info_request__unpack(ProtobufCAllocator *allocator,
						     size_t len,
						     const uint8_t *data);
void
logi__device__proto__get_device_info_request__free_unpacked(
    Logi__Device__Proto__GetDeviceInfoRequest *message,
    ProtobufCAllocator *allocator);
/* Logi__Device__Proto__GetDeviceInfoResponse methods */
void
logi__device__proto__get_device_info_response__init(
    Logi__Device__Proto__GetDeviceInfoResponse *message);
size_t
logi__device__proto__get_device_info_response__get_packed_size(
    const Logi__Device__Proto__GetDeviceInfoResponse *message);
size_t
logi__device__proto__get_device_info_response__pack(
    const Logi__Device__Proto__GetDeviceInfoResponse *message,
    uint8_t *out);
size_t
logi__device__proto__get_device_info_response__pack_to_buffer(
    const Logi__Device__Proto__GetDeviceInfoResponse *message,
    ProtobufCBuffer *buffer);
Logi__Device__Proto__GetDeviceInfoResponse *
logi__device__proto__get_device_info_response__unpack(ProtobufCAllocator *allocator,
						      size_t len,
						      const uint8_t *data);
void
logi__device__proto__get_device_info_response__free_unpacked(
    Logi__Device__Proto__GetDeviceInfoResponse *message,
    ProtobufCAllocator *allocator);
/* --- per-message closures --- */

typedef void (*Logi__Device__Proto__GetDeviceInfoRequest_Closure)(
    const Logi__Device__Proto__GetDeviceInfoRequest *message,
    void *closure_data);
typedef void (*Logi__Device__Proto__GetDeviceInfoResponse_Closure)(
    const Logi__Device__Proto__GetDeviceInfoResponse *message,
    void *closure_data);

/* --- services --- */

/* --- descriptors --- */

extern const ProtobufCMessageDescriptor logi__device__proto__get_device_info_request__descriptor;
extern const ProtobufCMessageDescriptor logi__device__proto__get_device_info_response__descriptor;

PROTOBUF_C__END_DECLS

#endif /* PROTOBUF_C_device_5finfo_2eproto__INCLUDED */
