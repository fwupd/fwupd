/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: antiflicker.proto */

/* Do not generate deprecated warnings for self */
#ifndef PROTOBUF_C__NO_DEPRECATED
#define PROTOBUF_C__NO_DEPRECATED
#endif

#include "antiflicker.pb-c.h"
void
logi__device__proto__anti_flicker_configuration__init(
    Logi__Device__Proto__AntiFlickerConfiguration *message)
{
	static const Logi__Device__Proto__AntiFlickerConfiguration init_value =
	    LOGI__DEVICE__PROTO__ANTI_FLICKER_CONFIGURATION__INIT;
	*message = init_value;
}
size_t
logi__device__proto__anti_flicker_configuration__get_packed_size(
    const Logi__Device__Proto__AntiFlickerConfiguration *message)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__anti_flicker_configuration__descriptor);
	return protobuf_c_message_get_packed_size((const ProtobufCMessage *)(message));
}
size_t
logi__device__proto__anti_flicker_configuration__pack(
    const Logi__Device__Proto__AntiFlickerConfiguration *message,
    uint8_t *out)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__anti_flicker_configuration__descriptor);
	return protobuf_c_message_pack((const ProtobufCMessage *)message, out);
}
size_t
logi__device__proto__anti_flicker_configuration__pack_to_buffer(
    const Logi__Device__Proto__AntiFlickerConfiguration *message,
    ProtobufCBuffer *buffer)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__anti_flicker_configuration__descriptor);
	return protobuf_c_message_pack_to_buffer((const ProtobufCMessage *)message, buffer);
}
Logi__Device__Proto__AntiFlickerConfiguration *
logi__device__proto__anti_flicker_configuration__unpack(ProtobufCAllocator *allocator,
							size_t len,
							const uint8_t *data)
{
	return (Logi__Device__Proto__AntiFlickerConfiguration *)protobuf_c_message_unpack(
	    &logi__device__proto__anti_flicker_configuration__descriptor,
	    allocator,
	    len,
	    data);
}
void
logi__device__proto__anti_flicker_configuration__free_unpacked(
    Logi__Device__Proto__AntiFlickerConfiguration *message,
    ProtobufCAllocator *allocator)
{
	if (!message)
		return;
	assert(message->base.descriptor ==
	       &logi__device__proto__anti_flicker_configuration__descriptor);
	protobuf_c_message_free_unpacked((ProtobufCMessage *)message, allocator);
}
void
logi__device__proto__set_anti_flicker_configuration_request__init(
    Logi__Device__Proto__SetAntiFlickerConfigurationRequest *message)
{
	static const Logi__Device__Proto__SetAntiFlickerConfigurationRequest init_value =
	    LOGI__DEVICE__PROTO__SET_ANTI_FLICKER_CONFIGURATION_REQUEST__INIT;
	*message = init_value;
}
size_t
logi__device__proto__set_anti_flicker_configuration_request__get_packed_size(
    const Logi__Device__Proto__SetAntiFlickerConfigurationRequest *message)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__set_anti_flicker_configuration_request__descriptor);
	return protobuf_c_message_get_packed_size((const ProtobufCMessage *)(message));
}
size_t
logi__device__proto__set_anti_flicker_configuration_request__pack(
    const Logi__Device__Proto__SetAntiFlickerConfigurationRequest *message,
    uint8_t *out)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__set_anti_flicker_configuration_request__descriptor);
	return protobuf_c_message_pack((const ProtobufCMessage *)message, out);
}
size_t
logi__device__proto__set_anti_flicker_configuration_request__pack_to_buffer(
    const Logi__Device__Proto__SetAntiFlickerConfigurationRequest *message,
    ProtobufCBuffer *buffer)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__set_anti_flicker_configuration_request__descriptor);
	return protobuf_c_message_pack_to_buffer((const ProtobufCMessage *)message, buffer);
}
Logi__Device__Proto__SetAntiFlickerConfigurationRequest *
logi__device__proto__set_anti_flicker_configuration_request__unpack(ProtobufCAllocator *allocator,
								    size_t len,
								    const uint8_t *data)
{
	return (Logi__Device__Proto__SetAntiFlickerConfigurationRequest *)protobuf_c_message_unpack(
	    &logi__device__proto__set_anti_flicker_configuration_request__descriptor,
	    allocator,
	    len,
	    data);
}
void
logi__device__proto__set_anti_flicker_configuration_request__free_unpacked(
    Logi__Device__Proto__SetAntiFlickerConfigurationRequest *message,
    ProtobufCAllocator *allocator)
{
	if (!message)
		return;
	assert(message->base.descriptor ==
	       &logi__device__proto__set_anti_flicker_configuration_request__descriptor);
	protobuf_c_message_free_unpacked((ProtobufCMessage *)message, allocator);
}
void
logi__device__proto__set_anti_flicker_configuration_response__init(
    Logi__Device__Proto__SetAntiFlickerConfigurationResponse *message)
{
	static const Logi__Device__Proto__SetAntiFlickerConfigurationResponse init_value =
	    LOGI__DEVICE__PROTO__SET_ANTI_FLICKER_CONFIGURATION_RESPONSE__INIT;
	*message = init_value;
}
size_t
logi__device__proto__set_anti_flicker_configuration_response__get_packed_size(
    const Logi__Device__Proto__SetAntiFlickerConfigurationResponse *message)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__set_anti_flicker_configuration_response__descriptor);
	return protobuf_c_message_get_packed_size((const ProtobufCMessage *)(message));
}
size_t
logi__device__proto__set_anti_flicker_configuration_response__pack(
    const Logi__Device__Proto__SetAntiFlickerConfigurationResponse *message,
    uint8_t *out)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__set_anti_flicker_configuration_response__descriptor);
	return protobuf_c_message_pack((const ProtobufCMessage *)message, out);
}
size_t
logi__device__proto__set_anti_flicker_configuration_response__pack_to_buffer(
    const Logi__Device__Proto__SetAntiFlickerConfigurationResponse *message,
    ProtobufCBuffer *buffer)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__set_anti_flicker_configuration_response__descriptor);
	return protobuf_c_message_pack_to_buffer((const ProtobufCMessage *)message, buffer);
}
Logi__Device__Proto__SetAntiFlickerConfigurationResponse *
logi__device__proto__set_anti_flicker_configuration_response__unpack(ProtobufCAllocator *allocator,
								     size_t len,
								     const uint8_t *data)
{
	return (Logi__Device__Proto__SetAntiFlickerConfigurationResponse *)
	    protobuf_c_message_unpack(
		&logi__device__proto__set_anti_flicker_configuration_response__descriptor,
		allocator,
		len,
		data);
}
void
logi__device__proto__set_anti_flicker_configuration_response__free_unpacked(
    Logi__Device__Proto__SetAntiFlickerConfigurationResponse *message,
    ProtobufCAllocator *allocator)
{
	if (!message)
		return;
	assert(message->base.descriptor ==
	       &logi__device__proto__set_anti_flicker_configuration_response__descriptor);
	protobuf_c_message_free_unpacked((ProtobufCMessage *)message, allocator);
}
static const ProtobufCEnumValue
    logi__device__proto__anti_flicker_configuration__mode__enum_values_by_number[2] = {
	{"NTSC_60HZ", "LOGI__DEVICE__PROTO__ANTI_FLICKER_CONFIGURATION__MODE__NTSC_60HZ", 0},
	{"PAL_50HZ", "LOGI__DEVICE__PROTO__ANTI_FLICKER_CONFIGURATION__MODE__PAL_50HZ", 1},
};
static const ProtobufCIntRange
    logi__device__proto__anti_flicker_configuration__mode__value_ranges[] = {{0, 0}, {0, 2}};
static const ProtobufCEnumValueIndex
    logi__device__proto__anti_flicker_configuration__mode__enum_values_by_name[2] = {
	{"NTSC_60HZ", 0},
	{"PAL_50HZ", 1},
};
const ProtobufCEnumDescriptor logi__device__proto__anti_flicker_configuration__mode__descriptor = {
    PROTOBUF_C__ENUM_DESCRIPTOR_MAGIC,
    "logi.device.proto.AntiFlickerConfiguration.Mode",
    "Mode",
    "Logi__Device__Proto__AntiFlickerConfiguration__Mode",
    "logi.device.proto",
    2,
    logi__device__proto__anti_flicker_configuration__mode__enum_values_by_number,
    2,
    logi__device__proto__anti_flicker_configuration__mode__enum_values_by_name,
    1,
    logi__device__proto__anti_flicker_configuration__mode__value_ranges,
    NULL,
    NULL,
    NULL,
    NULL /* reserved[1234] */
};
static const ProtobufCFieldDescriptor
    logi__device__proto__anti_flicker_configuration__field_descriptors[1] = {
	{
	    "mode",
	    1,
	    PROTOBUF_C_LABEL_NONE,
	    PROTOBUF_C_TYPE_ENUM,
	    0, /* quantifier_offset */
	    offsetof(Logi__Device__Proto__AntiFlickerConfiguration, mode),
	    &logi__device__proto__anti_flicker_configuration__mode__descriptor,
	    NULL,
	    0, /* flags */
	    0,
	    NULL,
	    NULL /* reserved1,reserved2, etc */
	},
};
static const unsigned logi__device__proto__anti_flicker_configuration__field_indices_by_name[] = {
    0, /* field[0] = mode */
};
static const ProtobufCIntRange
    logi__device__proto__anti_flicker_configuration__number_ranges[1 + 1] = {{1, 0}, {0, 1}};
const ProtobufCMessageDescriptor logi__device__proto__anti_flicker_configuration__descriptor = {
    PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
    "logi.device.proto.AntiFlickerConfiguration",
    "AntiFlickerConfiguration",
    "Logi__Device__Proto__AntiFlickerConfiguration",
    "logi.device.proto",
    sizeof(Logi__Device__Proto__AntiFlickerConfiguration),
    1,
    logi__device__proto__anti_flicker_configuration__field_descriptors,
    logi__device__proto__anti_flicker_configuration__field_indices_by_name,
    1,
    logi__device__proto__anti_flicker_configuration__number_ranges,
    (ProtobufCMessageInit)logi__device__proto__anti_flicker_configuration__init,
    NULL,
    NULL,
    NULL /* reserved[123] */
};
static const ProtobufCFieldDescriptor
    logi__device__proto__set_anti_flicker_configuration_request__field_descriptors[1] = {
	{
	    "mode",
	    1,
	    PROTOBUF_C_LABEL_NONE,
	    PROTOBUF_C_TYPE_ENUM,
	    0, /* quantifier_offset */
	    offsetof(Logi__Device__Proto__SetAntiFlickerConfigurationRequest, mode),
	    &logi__device__proto__anti_flicker_configuration__mode__descriptor,
	    NULL,
	    0, /* flags */
	    0,
	    NULL,
	    NULL /* reserved1,reserved2, etc */
	},
};
static const unsigned
    logi__device__proto__set_anti_flicker_configuration_request__field_indices_by_name[] = {
	0, /* field[0] = mode */
};
static const ProtobufCIntRange
    logi__device__proto__set_anti_flicker_configuration_request__number_ranges[1 + 1] = {{1, 0},
											 {0, 1}};
const ProtobufCMessageDescriptor
    logi__device__proto__set_anti_flicker_configuration_request__descriptor = {
	PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
	"logi.device.proto.SetAntiFlickerConfigurationRequest",
	"SetAntiFlickerConfigurationRequest",
	"Logi__Device__Proto__SetAntiFlickerConfigurationRequest",
	"logi.device.proto",
	sizeof(Logi__Device__Proto__SetAntiFlickerConfigurationRequest),
	1,
	logi__device__proto__set_anti_flicker_configuration_request__field_descriptors,
	logi__device__proto__set_anti_flicker_configuration_request__field_indices_by_name,
	1,
	logi__device__proto__set_anti_flicker_configuration_request__number_ranges,
	(ProtobufCMessageInit)logi__device__proto__set_anti_flicker_configuration_request__init,
	NULL,
	NULL,
	NULL /* reserved[123] */
};
static const ProtobufCFieldDescriptor
    logi__device__proto__set_anti_flicker_configuration_response__field_descriptors[2] = {
	{
	    "success",
	    1,
	    PROTOBUF_C_LABEL_NONE,
	    PROTOBUF_C_TYPE_BOOL,
	    0, /* quantifier_offset */
	    offsetof(Logi__Device__Proto__SetAntiFlickerConfigurationResponse, success),
	    NULL,
	    NULL,
	    0, /* flags */
	    0,
	    NULL,
	    NULL /* reserved1,reserved2, etc */
	},
	{
	    "errors",
	    2,
	    PROTOBUF_C_LABEL_REPEATED,
	    PROTOBUF_C_TYPE_MESSAGE,
	    offsetof(Logi__Device__Proto__SetAntiFlickerConfigurationResponse, n_errors),
	    offsetof(Logi__Device__Proto__SetAntiFlickerConfigurationResponse, errors),
	    &logi__device__proto__error__descriptor,
	    NULL,
	    0, /* flags */
	    0,
	    NULL,
	    NULL /* reserved1,reserved2, etc */
	},
};
static const unsigned
    logi__device__proto__set_anti_flicker_configuration_response__field_indices_by_name[] = {
	1, /* field[1] = errors */
	0, /* field[0] = success */
};
static const ProtobufCIntRange
    logi__device__proto__set_anti_flicker_configuration_response__number_ranges[1 + 1] = {{1, 0},
											  {0, 2}};
const ProtobufCMessageDescriptor
    logi__device__proto__set_anti_flicker_configuration_response__descriptor = {
	PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
	"logi.device.proto.SetAntiFlickerConfigurationResponse",
	"SetAntiFlickerConfigurationResponse",
	"Logi__Device__Proto__SetAntiFlickerConfigurationResponse",
	"logi.device.proto",
	sizeof(Logi__Device__Proto__SetAntiFlickerConfigurationResponse),
	2,
	logi__device__proto__set_anti_flicker_configuration_response__field_descriptors,
	logi__device__proto__set_anti_flicker_configuration_response__field_indices_by_name,
	1,
	logi__device__proto__set_anti_flicker_configuration_response__number_ranges,
	(ProtobufCMessageInit)logi__device__proto__set_anti_flicker_configuration_response__init,
	NULL,
	NULL,
	NULL /* reserved[123] */
};
