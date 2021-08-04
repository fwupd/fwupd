/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: device_common.proto */

/* Do not generate deprecated warnings for self */
#ifndef PROTOBUF_C__NO_DEPRECATED
#define PROTOBUF_C__NO_DEPRECATED
#endif

#include "device_common.pb-c.h"
void
logi__device__proto__error__init(Logi__Device__Proto__Error *message)
{
	static const Logi__Device__Proto__Error init_value = LOGI__DEVICE__PROTO__ERROR__INIT;
	*message = init_value;
}
size_t
logi__device__proto__error__get_packed_size(const Logi__Device__Proto__Error *message)
{
	assert(message->base.descriptor == &logi__device__proto__error__descriptor);
	return protobuf_c_message_get_packed_size((const ProtobufCMessage *)(message));
}
size_t
logi__device__proto__error__pack(const Logi__Device__Proto__Error *message, uint8_t *out)
{
	assert(message->base.descriptor == &logi__device__proto__error__descriptor);
	return protobuf_c_message_pack((const ProtobufCMessage *)message, out);
}
size_t
logi__device__proto__error__pack_to_buffer(const Logi__Device__Proto__Error *message,
					   ProtobufCBuffer *buffer)
{
	assert(message->base.descriptor == &logi__device__proto__error__descriptor);
	return protobuf_c_message_pack_to_buffer((const ProtobufCMessage *)message, buffer);
}
Logi__Device__Proto__Error *
logi__device__proto__error__unpack(ProtobufCAllocator *allocator, size_t len, const uint8_t *data)
{
	return (Logi__Device__Proto__Error *)protobuf_c_message_unpack(
	    &logi__device__proto__error__descriptor,
	    allocator,
	    len,
	    data);
}
void
logi__device__proto__error__free_unpacked(Logi__Device__Proto__Error *message,
					  ProtobufCAllocator *allocator)
{
	if (!message)
		return;
	assert(message->base.descriptor == &logi__device__proto__error__descriptor);
	protobuf_c_message_free_unpacked((ProtobufCMessage *)message, allocator);
}
static const ProtobufCFieldDescriptor logi__device__proto__error__field_descriptors[4] = {
    {
	"error_code",
	1,
	PROTOBUF_C_LABEL_NONE,
	PROTOBUF_C_TYPE_UINT32,
	0, /* quantifier_offset */
	offsetof(Logi__Device__Proto__Error, error_code),
	NULL,
	NULL,
	0, /* flags */
	0,
	NULL,
	NULL /* reserved1,reserved2, etc */
    },
    {
	"error_message",
	2,
	PROTOBUF_C_LABEL_NONE,
	PROTOBUF_C_TYPE_STRING,
	0, /* quantifier_offset */
	offsetof(Logi__Device__Proto__Error, error_message),
	NULL,
	&protobuf_c_empty_string,
	0, /* flags */
	0,
	NULL,
	NULL /* reserved1,reserved2, etc */
    },
    {
	"error_log_uri",
	3,
	PROTOBUF_C_LABEL_NONE,
	PROTOBUF_C_TYPE_STRING,
	0, /* quantifier_offset */
	offsetof(Logi__Device__Proto__Error, error_log_uri),
	NULL,
	&protobuf_c_empty_string,
	0, /* flags */
	0,
	NULL,
	NULL /* reserved1,reserved2, etc */
    },
    {
	"json_metadata",
	4,
	PROTOBUF_C_LABEL_NONE,
	PROTOBUF_C_TYPE_STRING,
	0, /* quantifier_offset */
	offsetof(Logi__Device__Proto__Error, json_metadata),
	NULL,
	&protobuf_c_empty_string,
	0, /* flags */
	0,
	NULL,
	NULL /* reserved1,reserved2, etc */
    },
};
static const unsigned logi__device__proto__error__field_indices_by_name[] = {
    0, /* field[0] = error_code */
    2, /* field[2] = error_log_uri */
    1, /* field[1] = error_message */
    3, /* field[3] = json_metadata */
};
static const ProtobufCIntRange logi__device__proto__error__number_ranges[1 + 1] = {{1, 0}, {0, 4}};
const ProtobufCMessageDescriptor logi__device__proto__error__descriptor = {
    PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
    "logi.device.proto.Error",
    "Error",
    "Logi__Device__Proto__Error",
    "logi.device.proto",
    sizeof(Logi__Device__Proto__Error),
    4,
    logi__device__proto__error__field_descriptors,
    logi__device__proto__error__field_indices_by_name,
    1,
    logi__device__proto__error__number_ranges,
    (ProtobufCMessageInit)logi__device__proto__error__init,
    NULL,
    NULL,
    NULL /* reserved[123] */
};
