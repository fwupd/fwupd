/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: device_request.proto */

/* Do not generate deprecated warnings for self */
#ifndef PROTOBUF_C__NO_DEPRECATED
#define PROTOBUF_C__NO_DEPRECATED
#endif

#include "device_request.pb-c.h"
void
logi__device__proto__reboot_device_request__init(Logi__Device__Proto__RebootDeviceRequest *message)
{
	static const Logi__Device__Proto__RebootDeviceRequest init_value =
	    LOGI__DEVICE__PROTO__REBOOT_DEVICE_REQUEST__INIT;
	*message = init_value;
}
size_t
logi__device__proto__reboot_device_request__get_packed_size(
    const Logi__Device__Proto__RebootDeviceRequest *message)
{
	assert(message->base.descriptor == &logi__device__proto__reboot_device_request__descriptor);
	return protobuf_c_message_get_packed_size((const ProtobufCMessage *)(message));
}
size_t
logi__device__proto__reboot_device_request__pack(
    const Logi__Device__Proto__RebootDeviceRequest *message,
    uint8_t *out)
{
	assert(message->base.descriptor == &logi__device__proto__reboot_device_request__descriptor);
	return protobuf_c_message_pack((const ProtobufCMessage *)message, out);
}
size_t
logi__device__proto__reboot_device_request__pack_to_buffer(
    const Logi__Device__Proto__RebootDeviceRequest *message,
    ProtobufCBuffer *buffer)
{
	assert(message->base.descriptor == &logi__device__proto__reboot_device_request__descriptor);
	return protobuf_c_message_pack_to_buffer((const ProtobufCMessage *)message, buffer);
}
Logi__Device__Proto__RebootDeviceRequest *
logi__device__proto__reboot_device_request__unpack(ProtobufCAllocator *allocator,
						   size_t len,
						   const uint8_t *data)
{
	return (Logi__Device__Proto__RebootDeviceRequest *)protobuf_c_message_unpack(
	    &logi__device__proto__reboot_device_request__descriptor,
	    allocator,
	    len,
	    data);
}
void
logi__device__proto__reboot_device_request__free_unpacked(
    Logi__Device__Proto__RebootDeviceRequest *message,
    ProtobufCAllocator *allocator)
{
	if (!message)
		return;
	assert(message->base.descriptor == &logi__device__proto__reboot_device_request__descriptor);
	protobuf_c_message_free_unpacked((ProtobufCMessage *)message, allocator);
}
void
logi__device__proto__reboot_device_response__init(
    Logi__Device__Proto__RebootDeviceResponse *message)
{
	static const Logi__Device__Proto__RebootDeviceResponse init_value =
	    LOGI__DEVICE__PROTO__REBOOT_DEVICE_RESPONSE__INIT;
	*message = init_value;
}
size_t
logi__device__proto__reboot_device_response__get_packed_size(
    const Logi__Device__Proto__RebootDeviceResponse *message)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__reboot_device_response__descriptor);
	return protobuf_c_message_get_packed_size((const ProtobufCMessage *)(message));
}
size_t
logi__device__proto__reboot_device_response__pack(
    const Logi__Device__Proto__RebootDeviceResponse *message,
    uint8_t *out)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__reboot_device_response__descriptor);
	return protobuf_c_message_pack((const ProtobufCMessage *)message, out);
}
size_t
logi__device__proto__reboot_device_response__pack_to_buffer(
    const Logi__Device__Proto__RebootDeviceResponse *message,
    ProtobufCBuffer *buffer)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__reboot_device_response__descriptor);
	return protobuf_c_message_pack_to_buffer((const ProtobufCMessage *)message, buffer);
}
Logi__Device__Proto__RebootDeviceResponse *
logi__device__proto__reboot_device_response__unpack(ProtobufCAllocator *allocator,
						    size_t len,
						    const uint8_t *data)
{
	return (Logi__Device__Proto__RebootDeviceResponse *)protobuf_c_message_unpack(
	    &logi__device__proto__reboot_device_response__descriptor,
	    allocator,
	    len,
	    data);
}
void
logi__device__proto__reboot_device_response__free_unpacked(
    Logi__Device__Proto__RebootDeviceResponse *message,
    ProtobufCAllocator *allocator)
{
	if (!message)
		return;
	assert(message->base.descriptor ==
	       &logi__device__proto__reboot_device_response__descriptor);
	protobuf_c_message_free_unpacked((ProtobufCMessage *)message, allocator);
}
void
logi__device__proto__set_speaker_boost_request__init(
    Logi__Device__Proto__SetSpeakerBoostRequest *message)
{
	static const Logi__Device__Proto__SetSpeakerBoostRequest init_value =
	    LOGI__DEVICE__PROTO__SET_SPEAKER_BOOST_REQUEST__INIT;
	*message = init_value;
}
size_t
logi__device__proto__set_speaker_boost_request__get_packed_size(
    const Logi__Device__Proto__SetSpeakerBoostRequest *message)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__set_speaker_boost_request__descriptor);
	return protobuf_c_message_get_packed_size((const ProtobufCMessage *)(message));
}
size_t
logi__device__proto__set_speaker_boost_request__pack(
    const Logi__Device__Proto__SetSpeakerBoostRequest *message,
    uint8_t *out)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__set_speaker_boost_request__descriptor);
	return protobuf_c_message_pack((const ProtobufCMessage *)message, out);
}
size_t
logi__device__proto__set_speaker_boost_request__pack_to_buffer(
    const Logi__Device__Proto__SetSpeakerBoostRequest *message,
    ProtobufCBuffer *buffer)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__set_speaker_boost_request__descriptor);
	return protobuf_c_message_pack_to_buffer((const ProtobufCMessage *)message, buffer);
}
Logi__Device__Proto__SetSpeakerBoostRequest *
logi__device__proto__set_speaker_boost_request__unpack(ProtobufCAllocator *allocator,
						       size_t len,
						       const uint8_t *data)
{
	return (Logi__Device__Proto__SetSpeakerBoostRequest *)protobuf_c_message_unpack(
	    &logi__device__proto__set_speaker_boost_request__descriptor,
	    allocator,
	    len,
	    data);
}
void
logi__device__proto__set_speaker_boost_request__free_unpacked(
    Logi__Device__Proto__SetSpeakerBoostRequest *message,
    ProtobufCAllocator *allocator)
{
	if (!message)
		return;
	assert(message->base.descriptor ==
	       &logi__device__proto__set_speaker_boost_request__descriptor);
	protobuf_c_message_free_unpacked((ProtobufCMessage *)message, allocator);
}
void
logi__device__proto__set_speaker_boost_response__init(
    Logi__Device__Proto__SetSpeakerBoostResponse *message)
{
	static const Logi__Device__Proto__SetSpeakerBoostResponse init_value =
	    LOGI__DEVICE__PROTO__SET_SPEAKER_BOOST_RESPONSE__INIT;
	*message = init_value;
}
size_t
logi__device__proto__set_speaker_boost_response__get_packed_size(
    const Logi__Device__Proto__SetSpeakerBoostResponse *message)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__set_speaker_boost_response__descriptor);
	return protobuf_c_message_get_packed_size((const ProtobufCMessage *)(message));
}
size_t
logi__device__proto__set_speaker_boost_response__pack(
    const Logi__Device__Proto__SetSpeakerBoostResponse *message,
    uint8_t *out)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__set_speaker_boost_response__descriptor);
	return protobuf_c_message_pack((const ProtobufCMessage *)message, out);
}
size_t
logi__device__proto__set_speaker_boost_response__pack_to_buffer(
    const Logi__Device__Proto__SetSpeakerBoostResponse *message,
    ProtobufCBuffer *buffer)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__set_speaker_boost_response__descriptor);
	return protobuf_c_message_pack_to_buffer((const ProtobufCMessage *)message, buffer);
}
Logi__Device__Proto__SetSpeakerBoostResponse *
logi__device__proto__set_speaker_boost_response__unpack(ProtobufCAllocator *allocator,
							size_t len,
							const uint8_t *data)
{
	return (Logi__Device__Proto__SetSpeakerBoostResponse *)protobuf_c_message_unpack(
	    &logi__device__proto__set_speaker_boost_response__descriptor,
	    allocator,
	    len,
	    data);
}
void
logi__device__proto__set_speaker_boost_response__free_unpacked(
    Logi__Device__Proto__SetSpeakerBoostResponse *message,
    ProtobufCAllocator *allocator)
{
	if (!message)
		return;
	assert(message->base.descriptor ==
	       &logi__device__proto__set_speaker_boost_response__descriptor);
	protobuf_c_message_free_unpacked((ProtobufCMessage *)message, allocator);
}
void
logi__device__proto__set_noise_reduction_request__init(
    Logi__Device__Proto__SetNoiseReductionRequest *message)
{
	static const Logi__Device__Proto__SetNoiseReductionRequest init_value =
	    LOGI__DEVICE__PROTO__SET_NOISE_REDUCTION_REQUEST__INIT;
	*message = init_value;
}
size_t
logi__device__proto__set_noise_reduction_request__get_packed_size(
    const Logi__Device__Proto__SetNoiseReductionRequest *message)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__set_noise_reduction_request__descriptor);
	return protobuf_c_message_get_packed_size((const ProtobufCMessage *)(message));
}
size_t
logi__device__proto__set_noise_reduction_request__pack(
    const Logi__Device__Proto__SetNoiseReductionRequest *message,
    uint8_t *out)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__set_noise_reduction_request__descriptor);
	return protobuf_c_message_pack((const ProtobufCMessage *)message, out);
}
size_t
logi__device__proto__set_noise_reduction_request__pack_to_buffer(
    const Logi__Device__Proto__SetNoiseReductionRequest *message,
    ProtobufCBuffer *buffer)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__set_noise_reduction_request__descriptor);
	return protobuf_c_message_pack_to_buffer((const ProtobufCMessage *)message, buffer);
}
Logi__Device__Proto__SetNoiseReductionRequest *
logi__device__proto__set_noise_reduction_request__unpack(ProtobufCAllocator *allocator,
							 size_t len,
							 const uint8_t *data)
{
	return (Logi__Device__Proto__SetNoiseReductionRequest *)protobuf_c_message_unpack(
	    &logi__device__proto__set_noise_reduction_request__descriptor,
	    allocator,
	    len,
	    data);
}
void
logi__device__proto__set_noise_reduction_request__free_unpacked(
    Logi__Device__Proto__SetNoiseReductionRequest *message,
    ProtobufCAllocator *allocator)
{
	if (!message)
		return;
	assert(message->base.descriptor ==
	       &logi__device__proto__set_noise_reduction_request__descriptor);
	protobuf_c_message_free_unpacked((ProtobufCMessage *)message, allocator);
}
void
logi__device__proto__set_noise_reduction_response__init(
    Logi__Device__Proto__SetNoiseReductionResponse *message)
{
	static const Logi__Device__Proto__SetNoiseReductionResponse init_value =
	    LOGI__DEVICE__PROTO__SET_NOISE_REDUCTION_RESPONSE__INIT;
	*message = init_value;
}
size_t
logi__device__proto__set_noise_reduction_response__get_packed_size(
    const Logi__Device__Proto__SetNoiseReductionResponse *message)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__set_noise_reduction_response__descriptor);
	return protobuf_c_message_get_packed_size((const ProtobufCMessage *)(message));
}
size_t
logi__device__proto__set_noise_reduction_response__pack(
    const Logi__Device__Proto__SetNoiseReductionResponse *message,
    uint8_t *out)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__set_noise_reduction_response__descriptor);
	return protobuf_c_message_pack((const ProtobufCMessage *)message, out);
}
size_t
logi__device__proto__set_noise_reduction_response__pack_to_buffer(
    const Logi__Device__Proto__SetNoiseReductionResponse *message,
    ProtobufCBuffer *buffer)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__set_noise_reduction_response__descriptor);
	return protobuf_c_message_pack_to_buffer((const ProtobufCMessage *)message, buffer);
}
Logi__Device__Proto__SetNoiseReductionResponse *
logi__device__proto__set_noise_reduction_response__unpack(ProtobufCAllocator *allocator,
							  size_t len,
							  const uint8_t *data)
{
	return (Logi__Device__Proto__SetNoiseReductionResponse *)protobuf_c_message_unpack(
	    &logi__device__proto__set_noise_reduction_response__descriptor,
	    allocator,
	    len,
	    data);
}
void
logi__device__proto__set_noise_reduction_response__free_unpacked(
    Logi__Device__Proto__SetNoiseReductionResponse *message,
    ProtobufCAllocator *allocator)
{
	if (!message)
		return;
	assert(message->base.descriptor ==
	       &logi__device__proto__set_noise_reduction_response__descriptor);
	protobuf_c_message_free_unpacked((ProtobufCMessage *)message, allocator);
}
void
logi__device__proto__set_reverb_mode_request__init(
    Logi__Device__Proto__SetReverbModeRequest *message)
{
	static const Logi__Device__Proto__SetReverbModeRequest init_value =
	    LOGI__DEVICE__PROTO__SET_REVERB_MODE_REQUEST__INIT;
	*message = init_value;
}
size_t
logi__device__proto__set_reverb_mode_request__get_packed_size(
    const Logi__Device__Proto__SetReverbModeRequest *message)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__set_reverb_mode_request__descriptor);
	return protobuf_c_message_get_packed_size((const ProtobufCMessage *)(message));
}
size_t
logi__device__proto__set_reverb_mode_request__pack(
    const Logi__Device__Proto__SetReverbModeRequest *message,
    uint8_t *out)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__set_reverb_mode_request__descriptor);
	return protobuf_c_message_pack((const ProtobufCMessage *)message, out);
}
size_t
logi__device__proto__set_reverb_mode_request__pack_to_buffer(
    const Logi__Device__Proto__SetReverbModeRequest *message,
    ProtobufCBuffer *buffer)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__set_reverb_mode_request__descriptor);
	return protobuf_c_message_pack_to_buffer((const ProtobufCMessage *)message, buffer);
}
Logi__Device__Proto__SetReverbModeRequest *
logi__device__proto__set_reverb_mode_request__unpack(ProtobufCAllocator *allocator,
						     size_t len,
						     const uint8_t *data)
{
	return (Logi__Device__Proto__SetReverbModeRequest *)protobuf_c_message_unpack(
	    &logi__device__proto__set_reverb_mode_request__descriptor,
	    allocator,
	    len,
	    data);
}
void
logi__device__proto__set_reverb_mode_request__free_unpacked(
    Logi__Device__Proto__SetReverbModeRequest *message,
    ProtobufCAllocator *allocator)
{
	if (!message)
		return;
	assert(message->base.descriptor ==
	       &logi__device__proto__set_reverb_mode_request__descriptor);
	protobuf_c_message_free_unpacked((ProtobufCMessage *)message, allocator);
}
void
logi__device__proto__set_reverb_mode_response__init(
    Logi__Device__Proto__SetReverbModeResponse *message)
{
	static const Logi__Device__Proto__SetReverbModeResponse init_value =
	    LOGI__DEVICE__PROTO__SET_REVERB_MODE_RESPONSE__INIT;
	*message = init_value;
}
size_t
logi__device__proto__set_reverb_mode_response__get_packed_size(
    const Logi__Device__Proto__SetReverbModeResponse *message)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__set_reverb_mode_response__descriptor);
	return protobuf_c_message_get_packed_size((const ProtobufCMessage *)(message));
}
size_t
logi__device__proto__set_reverb_mode_response__pack(
    const Logi__Device__Proto__SetReverbModeResponse *message,
    uint8_t *out)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__set_reverb_mode_response__descriptor);
	return protobuf_c_message_pack((const ProtobufCMessage *)message, out);
}
size_t
logi__device__proto__set_reverb_mode_response__pack_to_buffer(
    const Logi__Device__Proto__SetReverbModeResponse *message,
    ProtobufCBuffer *buffer)
{
	assert(message->base.descriptor ==
	       &logi__device__proto__set_reverb_mode_response__descriptor);
	return protobuf_c_message_pack_to_buffer((const ProtobufCMessage *)message, buffer);
}
Logi__Device__Proto__SetReverbModeResponse *
logi__device__proto__set_reverb_mode_response__unpack(ProtobufCAllocator *allocator,
						      size_t len,
						      const uint8_t *data)
{
	return (Logi__Device__Proto__SetReverbModeResponse *)protobuf_c_message_unpack(
	    &logi__device__proto__set_reverb_mode_response__descriptor,
	    allocator,
	    len,
	    data);
}
void
logi__device__proto__set_reverb_mode_response__free_unpacked(
    Logi__Device__Proto__SetReverbModeResponse *message,
    ProtobufCAllocator *allocator)
{
	if (!message)
		return;
	assert(message->base.descriptor ==
	       &logi__device__proto__set_reverb_mode_response__descriptor);
	protobuf_c_message_free_unpacked((ProtobufCMessage *)message, allocator);
}
static const ProtobufCFieldDescriptor
    logi__device__proto__reboot_device_request__field_descriptors[2] = {
	{
	    "reserved",
	    1,
	    PROTOBUF_C_LABEL_NONE,
	    PROTOBUF_C_TYPE_BOOL,
	    0, /* quantifier_offset */
	    offsetof(Logi__Device__Proto__RebootDeviceRequest, reserved),
	    NULL,
	    NULL,
	    0, /* flags */
	    0,
	    NULL,
	    NULL /* reserved1,reserved2, etc */
	},
	{
	    "iat",
	    2,
	    PROTOBUF_C_LABEL_NONE,
	    PROTOBUF_C_TYPE_UINT64,
	    0, /* quantifier_offset */
	    offsetof(Logi__Device__Proto__RebootDeviceRequest, iat),
	    NULL,
	    NULL,
	    0, /* flags */
	    0,
	    NULL,
	    NULL /* reserved1,reserved2, etc */
	},
};
static const unsigned logi__device__proto__reboot_device_request__field_indices_by_name[] = {
    1, /* field[1] = iat */
    0, /* field[0] = reserved */
};
static const ProtobufCIntRange logi__device__proto__reboot_device_request__number_ranges[1 + 1] = {
    {1, 0},
    {0, 2}};
const ProtobufCMessageDescriptor logi__device__proto__reboot_device_request__descriptor = {
    PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
    "logi.device.proto.RebootDeviceRequest",
    "RebootDeviceRequest",
    "Logi__Device__Proto__RebootDeviceRequest",
    "logi.device.proto",
    sizeof(Logi__Device__Proto__RebootDeviceRequest),
    2,
    logi__device__proto__reboot_device_request__field_descriptors,
    logi__device__proto__reboot_device_request__field_indices_by_name,
    1,
    logi__device__proto__reboot_device_request__number_ranges,
    (ProtobufCMessageInit)logi__device__proto__reboot_device_request__init,
    NULL,
    NULL,
    NULL /* reserved[123] */
};
static const ProtobufCFieldDescriptor
    logi__device__proto__reboot_device_response__field_descriptors[1] = {
	{
	    "success",
	    1,
	    PROTOBUF_C_LABEL_NONE,
	    PROTOBUF_C_TYPE_BOOL,
	    0, /* quantifier_offset */
	    offsetof(Logi__Device__Proto__RebootDeviceResponse, success),
	    NULL,
	    NULL,
	    0, /* flags */
	    0,
	    NULL,
	    NULL /* reserved1,reserved2, etc */
	},
};
static const unsigned logi__device__proto__reboot_device_response__field_indices_by_name[] = {
    0, /* field[0] = success */
};
static const ProtobufCIntRange logi__device__proto__reboot_device_response__number_ranges[1 + 1] = {
    {1, 0},
    {0, 1}};
const ProtobufCMessageDescriptor logi__device__proto__reboot_device_response__descriptor = {
    PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
    "logi.device.proto.RebootDeviceResponse",
    "RebootDeviceResponse",
    "Logi__Device__Proto__RebootDeviceResponse",
    "logi.device.proto",
    sizeof(Logi__Device__Proto__RebootDeviceResponse),
    1,
    logi__device__proto__reboot_device_response__field_descriptors,
    logi__device__proto__reboot_device_response__field_indices_by_name,
    1,
    logi__device__proto__reboot_device_response__number_ranges,
    (ProtobufCMessageInit)logi__device__proto__reboot_device_response__init,
    NULL,
    NULL,
    NULL /* reserved[123] */
};
static const ProtobufCFieldDescriptor
    logi__device__proto__set_speaker_boost_request__field_descriptors[1] = {
	{
	    "speaker_boost",
	    1,
	    PROTOBUF_C_LABEL_NONE,
	    PROTOBUF_C_TYPE_INT32,
	    0, /* quantifier_offset */
	    offsetof(Logi__Device__Proto__SetSpeakerBoostRequest, speaker_boost),
	    NULL,
	    NULL,
	    0, /* flags */
	    0,
	    NULL,
	    NULL /* reserved1,reserved2, etc */
	},
};
static const unsigned logi__device__proto__set_speaker_boost_request__field_indices_by_name[] = {
    0, /* field[0] = speaker_boost */
};
static const ProtobufCIntRange
    logi__device__proto__set_speaker_boost_request__number_ranges[1 + 1] = {{1, 0}, {0, 1}};
const ProtobufCMessageDescriptor logi__device__proto__set_speaker_boost_request__descriptor = {
    PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
    "logi.device.proto.SetSpeakerBoostRequest",
    "SetSpeakerBoostRequest",
    "Logi__Device__Proto__SetSpeakerBoostRequest",
    "logi.device.proto",
    sizeof(Logi__Device__Proto__SetSpeakerBoostRequest),
    1,
    logi__device__proto__set_speaker_boost_request__field_descriptors,
    logi__device__proto__set_speaker_boost_request__field_indices_by_name,
    1,
    logi__device__proto__set_speaker_boost_request__number_ranges,
    (ProtobufCMessageInit)logi__device__proto__set_speaker_boost_request__init,
    NULL,
    NULL,
    NULL /* reserved[123] */
};
static const ProtobufCFieldDescriptor
    logi__device__proto__set_speaker_boost_response__field_descriptors[1] = {
	{
	    "success",
	    1,
	    PROTOBUF_C_LABEL_NONE,
	    PROTOBUF_C_TYPE_BOOL,
	    0, /* quantifier_offset */
	    offsetof(Logi__Device__Proto__SetSpeakerBoostResponse, success),
	    NULL,
	    NULL,
	    0, /* flags */
	    0,
	    NULL,
	    NULL /* reserved1,reserved2, etc */
	},
};
static const unsigned logi__device__proto__set_speaker_boost_response__field_indices_by_name[] = {
    0, /* field[0] = success */
};
static const ProtobufCIntRange
    logi__device__proto__set_speaker_boost_response__number_ranges[1 + 1] = {{1, 0}, {0, 1}};
const ProtobufCMessageDescriptor logi__device__proto__set_speaker_boost_response__descriptor = {
    PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
    "logi.device.proto.SetSpeakerBoostResponse",
    "SetSpeakerBoostResponse",
    "Logi__Device__Proto__SetSpeakerBoostResponse",
    "logi.device.proto",
    sizeof(Logi__Device__Proto__SetSpeakerBoostResponse),
    1,
    logi__device__proto__set_speaker_boost_response__field_descriptors,
    logi__device__proto__set_speaker_boost_response__field_indices_by_name,
    1,
    logi__device__proto__set_speaker_boost_response__number_ranges,
    (ProtobufCMessageInit)logi__device__proto__set_speaker_boost_response__init,
    NULL,
    NULL,
    NULL /* reserved[123] */
};
static const ProtobufCFieldDescriptor
    logi__device__proto__set_noise_reduction_request__field_descriptors[1] = {
	{
	    "noise_reduction",
	    1,
	    PROTOBUF_C_LABEL_NONE,
	    PROTOBUF_C_TYPE_INT32,
	    0, /* quantifier_offset */
	    offsetof(Logi__Device__Proto__SetNoiseReductionRequest, noise_reduction),
	    NULL,
	    NULL,
	    0, /* flags */
	    0,
	    NULL,
	    NULL /* reserved1,reserved2, etc */
	},
};
static const unsigned logi__device__proto__set_noise_reduction_request__field_indices_by_name[] = {
    0, /* field[0] = noise_reduction */
};
static const ProtobufCIntRange
    logi__device__proto__set_noise_reduction_request__number_ranges[1 + 1] = {{1, 0}, {0, 1}};
const ProtobufCMessageDescriptor logi__device__proto__set_noise_reduction_request__descriptor = {
    PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
    "logi.device.proto.SetNoiseReductionRequest",
    "SetNoiseReductionRequest",
    "Logi__Device__Proto__SetNoiseReductionRequest",
    "logi.device.proto",
    sizeof(Logi__Device__Proto__SetNoiseReductionRequest),
    1,
    logi__device__proto__set_noise_reduction_request__field_descriptors,
    logi__device__proto__set_noise_reduction_request__field_indices_by_name,
    1,
    logi__device__proto__set_noise_reduction_request__number_ranges,
    (ProtobufCMessageInit)logi__device__proto__set_noise_reduction_request__init,
    NULL,
    NULL,
    NULL /* reserved[123] */
};
static const ProtobufCFieldDescriptor
    logi__device__proto__set_noise_reduction_response__field_descriptors[1] = {
	{
	    "success",
	    1,
	    PROTOBUF_C_LABEL_NONE,
	    PROTOBUF_C_TYPE_BOOL,
	    0, /* quantifier_offset */
	    offsetof(Logi__Device__Proto__SetNoiseReductionResponse, success),
	    NULL,
	    NULL,
	    0, /* flags */
	    0,
	    NULL,
	    NULL /* reserved1,reserved2, etc */
	},
};
static const unsigned logi__device__proto__set_noise_reduction_response__field_indices_by_name[] = {
    0, /* field[0] = success */
};
static const ProtobufCIntRange
    logi__device__proto__set_noise_reduction_response__number_ranges[1 + 1] = {{1, 0}, {0, 1}};
const ProtobufCMessageDescriptor logi__device__proto__set_noise_reduction_response__descriptor = {
    PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
    "logi.device.proto.SetNoiseReductionResponse",
    "SetNoiseReductionResponse",
    "Logi__Device__Proto__SetNoiseReductionResponse",
    "logi.device.proto",
    sizeof(Logi__Device__Proto__SetNoiseReductionResponse),
    1,
    logi__device__proto__set_noise_reduction_response__field_descriptors,
    logi__device__proto__set_noise_reduction_response__field_indices_by_name,
    1,
    logi__device__proto__set_noise_reduction_response__number_ranges,
    (ProtobufCMessageInit)logi__device__proto__set_noise_reduction_response__init,
    NULL,
    NULL,
    NULL /* reserved[123] */
};
static const ProtobufCEnumValue
    logi__device__proto__set_reverb_mode_request__reverb_mode__enum_values_by_number[4] = {
	{"DISABLED", "LOGI__DEVICE__PROTO__SET_REVERB_MODE_REQUEST__REVERB_MODE__DISABLED", 0},
	{"MILD", "LOGI__DEVICE__PROTO__SET_REVERB_MODE_REQUEST__REVERB_MODE__MILD", 1},
	{"NORMAL", "LOGI__DEVICE__PROTO__SET_REVERB_MODE_REQUEST__REVERB_MODE__NORMAL", 2},
	{"AGGRESSIVE", "LOGI__DEVICE__PROTO__SET_REVERB_MODE_REQUEST__REVERB_MODE__AGGRESSIVE", 3},
};
static const ProtobufCIntRange
    logi__device__proto__set_reverb_mode_request__reverb_mode__value_ranges[] = {{0, 0}, {0, 4}};
static const ProtobufCEnumValueIndex
    logi__device__proto__set_reverb_mode_request__reverb_mode__enum_values_by_name[4] = {
	{"AGGRESSIVE", 3},
	{"DISABLED", 0},
	{"MILD", 1},
	{"NORMAL", 2},
};
const ProtobufCEnumDescriptor
    logi__device__proto__set_reverb_mode_request__reverb_mode__descriptor = {
	PROTOBUF_C__ENUM_DESCRIPTOR_MAGIC,
	"logi.device.proto.SetReverbModeRequest.ReverbMode",
	"ReverbMode",
	"Logi__Device__Proto__SetReverbModeRequest__ReverbMode",
	"logi.device.proto",
	4,
	logi__device__proto__set_reverb_mode_request__reverb_mode__enum_values_by_number,
	4,
	logi__device__proto__set_reverb_mode_request__reverb_mode__enum_values_by_name,
	1,
	logi__device__proto__set_reverb_mode_request__reverb_mode__value_ranges,
	NULL,
	NULL,
	NULL,
	NULL /* reserved[1234] */
};
static const ProtobufCFieldDescriptor
    logi__device__proto__set_reverb_mode_request__field_descriptors[1] = {
	{
	    "reverb_mode",
	    1,
	    PROTOBUF_C_LABEL_NONE,
	    PROTOBUF_C_TYPE_ENUM,
	    0, /* quantifier_offset */
	    offsetof(Logi__Device__Proto__SetReverbModeRequest, reverb_mode),
	    &logi__device__proto__set_reverb_mode_request__reverb_mode__descriptor,
	    NULL,
	    0, /* flags */
	    0,
	    NULL,
	    NULL /* reserved1,reserved2, etc */
	},
};
static const unsigned logi__device__proto__set_reverb_mode_request__field_indices_by_name[] = {
    0, /* field[0] = reverb_mode */
};
static const ProtobufCIntRange logi__device__proto__set_reverb_mode_request__number_ranges[1 + 1] =
    {{1, 0}, {0, 1}};
const ProtobufCMessageDescriptor logi__device__proto__set_reverb_mode_request__descriptor = {
    PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
    "logi.device.proto.SetReverbModeRequest",
    "SetReverbModeRequest",
    "Logi__Device__Proto__SetReverbModeRequest",
    "logi.device.proto",
    sizeof(Logi__Device__Proto__SetReverbModeRequest),
    1,
    logi__device__proto__set_reverb_mode_request__field_descriptors,
    logi__device__proto__set_reverb_mode_request__field_indices_by_name,
    1,
    logi__device__proto__set_reverb_mode_request__number_ranges,
    (ProtobufCMessageInit)logi__device__proto__set_reverb_mode_request__init,
    NULL,
    NULL,
    NULL /* reserved[123] */
};
static const ProtobufCFieldDescriptor
    logi__device__proto__set_reverb_mode_response__field_descriptors[1] = {
	{
	    "success",
	    1,
	    PROTOBUF_C_LABEL_NONE,
	    PROTOBUF_C_TYPE_BOOL,
	    0, /* quantifier_offset */
	    offsetof(Logi__Device__Proto__SetReverbModeResponse, success),
	    NULL,
	    NULL,
	    0, /* flags */
	    0,
	    NULL,
	    NULL /* reserved1,reserved2, etc */
	},
};
static const unsigned logi__device__proto__set_reverb_mode_response__field_indices_by_name[] = {
    0, /* field[0] = success */
};
static const ProtobufCIntRange logi__device__proto__set_reverb_mode_response__number_ranges[1 + 1] =
    {{1, 0}, {0, 1}};
const ProtobufCMessageDescriptor logi__device__proto__set_reverb_mode_response__descriptor = {
    PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
    "logi.device.proto.SetReverbModeResponse",
    "SetReverbModeResponse",
    "Logi__Device__Proto__SetReverbModeResponse",
    "logi.device.proto",
    sizeof(Logi__Device__Proto__SetReverbModeResponse),
    1,
    logi__device__proto__set_reverb_mode_response__field_descriptors,
    logi__device__proto__set_reverb_mode_response__field_indices_by_name,
    1,
    logi__device__proto__set_reverb_mode_response__number_ranges,
    (ProtobufCMessageInit)logi__device__proto__set_reverb_mode_response__init,
    NULL,
    NULL,
    NULL /* reserved[123] */
};
