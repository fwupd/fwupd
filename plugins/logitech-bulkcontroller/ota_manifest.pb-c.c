/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: ota_manifest.proto */

/* Do not generate deprecated warnings for self */
#ifndef PROTOBUF_C__NO_DEPRECATED
#define PROTOBUF_C__NO_DEPRECATED
#endif

#include "ota_manifest.pb-c.h"
void
logi__device__proto__get_manifest_body_request__init(
    Logi__Device__Proto__GetManifestBodyRequest *message)
{
	static const Logi__Device__Proto__GetManifestBodyRequest init_value =
	    LOGI__DEVICE__PROTO__GET_MANIFEST_BODY_REQUEST__INIT;
	*message = init_value;
}
size_t
logi__device__proto__get_manifest_body_request__get_packed_size(
    const Logi__Device__Proto__GetManifestBodyRequest *message)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__get_manifest_body_request__descriptor);
	return protobuf_c_message_get_packed_size((const ProtobufCMessage *)(message));
}
size_t
logi__device__proto__get_manifest_body_request__pack(
    const Logi__Device__Proto__GetManifestBodyRequest *message,
    uint8_t *out)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__get_manifest_body_request__descriptor);
	return protobuf_c_message_pack((const ProtobufCMessage *)message, out);
}
size_t
logi__device__proto__get_manifest_body_request__pack_to_buffer(
    const Logi__Device__Proto__GetManifestBodyRequest *message,
    ProtobufCBuffer *buffer)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__get_manifest_body_request__descriptor);
	return protobuf_c_message_pack_to_buffer((const ProtobufCMessage *)message, buffer);
}
Logi__Device__Proto__GetManifestBodyRequest *
logi__device__proto__get_manifest_body_request__unpack(ProtobufCAllocator *allocator,
						       size_t len,
						       const uint8_t *data)
{
	return (Logi__Device__Proto__GetManifestBodyRequest *)protobuf_c_message_unpack(
	    &logi__device__proto__get_manifest_body_request__descriptor,
	    allocator,
	    len,
	    data);
}
void
logi__device__proto__get_manifest_body_request__free_unpacked(
    Logi__Device__Proto__GetManifestBodyRequest *message,
    ProtobufCAllocator *allocator)
{
	if (!message)
		return;
	assert(message->base.descriptor ==
	       &logi__device__proto__get_manifest_body_request__descriptor);
	protobuf_c_message_free_unpacked((ProtobufCMessage *)message, allocator);
}
void
logi__device__proto__get_manifest_body_response__init(
    Logi__Device__Proto__GetManifestBodyResponse *message)
{
	static const Logi__Device__Proto__GetManifestBodyResponse init_value =
	    LOGI__DEVICE__PROTO__GET_MANIFEST_BODY_RESPONSE__INIT;
	*message = init_value;
}
size_t
logi__device__proto__get_manifest_body_response__get_packed_size(
    const Logi__Device__Proto__GetManifestBodyResponse *message)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__get_manifest_body_response__descriptor);
	return protobuf_c_message_get_packed_size((const ProtobufCMessage *)(message));
}
size_t
logi__device__proto__get_manifest_body_response__pack(
    const Logi__Device__Proto__GetManifestBodyResponse *message,
    uint8_t *out)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__get_manifest_body_response__descriptor);
	return protobuf_c_message_pack((const ProtobufCMessage *)message, out);
}
size_t
logi__device__proto__get_manifest_body_response__pack_to_buffer(
    const Logi__Device__Proto__GetManifestBodyResponse *message,
    ProtobufCBuffer *buffer)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__get_manifest_body_response__descriptor);
	return protobuf_c_message_pack_to_buffer((const ProtobufCMessage *)message, buffer);
}
Logi__Device__Proto__GetManifestBodyResponse *
logi__device__proto__get_manifest_body_response__unpack(ProtobufCAllocator *allocator,
							size_t len,
							const uint8_t *data)
{
	return (Logi__Device__Proto__GetManifestBodyResponse *)protobuf_c_message_unpack(
	    &logi__device__proto__get_manifest_body_response__descriptor,
	    allocator,
	    len,
	    data);
}
void
logi__device__proto__get_manifest_body_response__free_unpacked(
    Logi__Device__Proto__GetManifestBodyResponse *message,
    ProtobufCAllocator *allocator)
{
	if (!message)
		return;
	assert(message->base.descriptor ==
	       &logi__device__proto__get_manifest_body_response__descriptor);
	protobuf_c_message_free_unpacked((ProtobufCMessage *)message, allocator);
}
static const ProtobufCFieldDescriptor
    logi__device__proto__get_manifest_body_request__field_descriptors[5] = {
	{
	    "challenge",
	    1,
	    PROTOBUF_C_LABEL_NONE,
	    PROTOBUF_C_TYPE_STRING,
	    0, /* quantifier_offset */
	    offsetof(Logi__Device__Proto__GetManifestBodyRequest, challenge),
	    NULL,
	    &protobuf_c_empty_string,
	    0, /* flags */
	    0,
	    NULL,
	    NULL /* reserved1,reserved2, etc */
	},
	{
	    "version",
	    2,
	    PROTOBUF_C_LABEL_NONE,
	    PROTOBUF_C_TYPE_STRING,
	    0, /* quantifier_offset */
	    offsetof(Logi__Device__Proto__GetManifestBodyRequest, version),
	    NULL,
	    &protobuf_c_empty_string,
	    0, /* flags */
	    0,
	    NULL,
	    NULL /* reserved1,reserved2, etc */
	},
	{
	    "channel",
	    3,
	    PROTOBUF_C_LABEL_NONE,
	    PROTOBUF_C_TYPE_STRING,
	    0, /* quantifier_offset */
	    offsetof(Logi__Device__Proto__GetManifestBodyRequest, channel),
	    NULL,
	    &protobuf_c_empty_string,
	    0, /* flags */
	    0,
	    NULL,
	    NULL /* reserved1,reserved2, etc */
	},
	{
	    "meta_info",
	    4,
	    PROTOBUF_C_LABEL_NONE,
	    PROTOBUF_C_TYPE_STRING,
	    0, /* quantifier_offset */
	    offsetof(Logi__Device__Proto__GetManifestBodyRequest, meta_info),
	    NULL,
	    &protobuf_c_empty_string,
	    0, /* flags */
	    0,
	    NULL,
	    NULL /* reserved1,reserved2, etc */
	},
	{
	    "ttl",
	    5,
	    PROTOBUF_C_LABEL_NONE,
	    PROTOBUF_C_TYPE_INT32,
	    0, /* quantifier_offset */
	    offsetof(Logi__Device__Proto__GetManifestBodyRequest, ttl),
	    NULL,
	    NULL,
	    0, /* flags */
	    0,
	    NULL,
	    NULL /* reserved1,reserved2, etc */
	},
};
static const unsigned logi__device__proto__get_manifest_body_request__field_indices_by_name[] = {
    0, /* field[0] = challenge */
    2, /* field[2] = channel */
    3, /* field[3] = meta_info */
    4, /* field[4] = ttl */
    1, /* field[1] = version */
};
static const ProtobufCIntRange
    logi__device__proto__get_manifest_body_request__number_ranges[1 + 1] = {{1, 0}, {0, 5}};
const ProtobufCMessageDescriptor logi__device__proto__get_manifest_body_request__descriptor = {
    PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
    "logi.device.proto.GetManifestBodyRequest",
    "GetManifestBodyRequest",
    "Logi__Device__Proto__GetManifestBodyRequest",
    "logi.device.proto",
    sizeof(Logi__Device__Proto__GetManifestBodyRequest),
    5,
    logi__device__proto__get_manifest_body_request__field_descriptors,
    logi__device__proto__get_manifest_body_request__field_indices_by_name,
    1,
    logi__device__proto__get_manifest_body_request__number_ranges,
    (ProtobufCMessageInit)logi__device__proto__get_manifest_body_request__init,
    NULL,
    NULL,
    NULL /* reserved[123] */
};
static const ProtobufCFieldDescriptor
    logi__device__proto__get_manifest_body_response__field_descriptors[2] = {
	{
	    "body",
	    1,
	    PROTOBUF_C_LABEL_NONE,
	    PROTOBUF_C_TYPE_STRING,
	    0, /* quantifier_offset */
	    offsetof(Logi__Device__Proto__GetManifestBodyResponse, body),
	    NULL,
	    &protobuf_c_empty_string,
	    0, /* flags */
	    0,
	    NULL,
	    NULL /* reserved1,reserved2, etc */
	},
	{
	    "signature",
	    2,
	    PROTOBUF_C_LABEL_NONE,
	    PROTOBUF_C_TYPE_STRING,
	    0, /* quantifier_offset */
	    offsetof(Logi__Device__Proto__GetManifestBodyResponse, signature),
	    NULL,
	    &protobuf_c_empty_string,
	    0, /* flags */
	    0,
	    NULL,
	    NULL /* reserved1,reserved2, etc */
	},
};
static const unsigned logi__device__proto__get_manifest_body_response__field_indices_by_name[] = {
    0, /* field[0] = body */
    1, /* field[1] = signature */
};
static const ProtobufCIntRange
    logi__device__proto__get_manifest_body_response__number_ranges[1 + 1] = {{1, 0}, {0, 2}};
const ProtobufCMessageDescriptor logi__device__proto__get_manifest_body_response__descriptor = {
    PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
    "logi.device.proto.GetManifestBodyResponse",
    "GetManifestBodyResponse",
    "Logi__Device__Proto__GetManifestBodyResponse",
    "logi.device.proto",
    sizeof(Logi__Device__Proto__GetManifestBodyResponse),
    2,
    logi__device__proto__get_manifest_body_response__field_descriptors,
    logi__device__proto__get_manifest_body_response__field_indices_by_name,
    1,
    logi__device__proto__get_manifest_body_response__number_ranges,
    (ProtobufCMessageInit)logi__device__proto__get_manifest_body_response__init,
    NULL,
    NULL,
    NULL /* reserved[123] */
};
