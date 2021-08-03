/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: device_time.proto */

/* Do not generate deprecated warnings for self */
#ifndef PROTOBUF_C__NO_DEPRECATED
#define PROTOBUF_C__NO_DEPRECATED
#endif

#include "device_time.pb-c.h"
void   logi__device__proto__set_device_time_request__init
                     (Logi__Device__Proto__SetDeviceTimeRequest         *message)
{
  static const Logi__Device__Proto__SetDeviceTimeRequest init_value = LOGI__DEVICE__PROTO__SET_DEVICE_TIME_REQUEST__INIT;
  *message = init_value;
}
size_t logi__device__proto__set_device_time_request__get_packed_size
                     (const Logi__Device__Proto__SetDeviceTimeRequest *message)
{
  assert(message->base.descriptor == &logi__device__proto__set_device_time_request__descriptor);
  return protobuf_c_message_get_packed_size ((const ProtobufCMessage*)(message));
}
size_t logi__device__proto__set_device_time_request__pack
                     (const Logi__Device__Proto__SetDeviceTimeRequest *message,
                      uint8_t       *out)
{
  assert(message->base.descriptor == &logi__device__proto__set_device_time_request__descriptor);
  return protobuf_c_message_pack ((const ProtobufCMessage*)message, out);
}
size_t logi__device__proto__set_device_time_request__pack_to_buffer
                     (const Logi__Device__Proto__SetDeviceTimeRequest *message,
                      ProtobufCBuffer *buffer)
{
  assert(message->base.descriptor == &logi__device__proto__set_device_time_request__descriptor);
  return protobuf_c_message_pack_to_buffer ((const ProtobufCMessage*)message, buffer);
}
Logi__Device__Proto__SetDeviceTimeRequest *
       logi__device__proto__set_device_time_request__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data)
{
  return (Logi__Device__Proto__SetDeviceTimeRequest *)
     protobuf_c_message_unpack (&logi__device__proto__set_device_time_request__descriptor,
                                allocator, len, data);
}
void   logi__device__proto__set_device_time_request__free_unpacked
                     (Logi__Device__Proto__SetDeviceTimeRequest *message,
                      ProtobufCAllocator *allocator)
{
  if(!message)
    return;
  assert(message->base.descriptor == &logi__device__proto__set_device_time_request__descriptor);
  protobuf_c_message_free_unpacked ((ProtobufCMessage*)message, allocator);
}
static const ProtobufCFieldDescriptor logi__device__proto__set_device_time_request__field_descriptors[2] =
{
  {
    "ts",
    1,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_UINT64,
    0,   /* quantifier_offset */
    offsetof(Logi__Device__Proto__SetDeviceTimeRequest, ts),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "time_zone",
    2,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Logi__Device__Proto__SetDeviceTimeRequest, time_zone),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
};
static const unsigned logi__device__proto__set_device_time_request__field_indices_by_name[] = {
  1,   /* field[1] = time_zone */
  0,   /* field[0] = ts */
};
static const ProtobufCIntRange logi__device__proto__set_device_time_request__number_ranges[1 + 1] =
{
  { 1, 0 },
  { 0, 2 }
};
const ProtobufCMessageDescriptor logi__device__proto__set_device_time_request__descriptor =
{
  PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
  "logi.device.proto.SetDeviceTimeRequest",
  "SetDeviceTimeRequest",
  "Logi__Device__Proto__SetDeviceTimeRequest",
  "logi.device.proto",
  sizeof(Logi__Device__Proto__SetDeviceTimeRequest),
  2,
  logi__device__proto__set_device_time_request__field_descriptors,
  logi__device__proto__set_device_time_request__field_indices_by_name,
  1,  logi__device__proto__set_device_time_request__number_ranges,
  (ProtobufCMessageInit) logi__device__proto__set_device_time_request__init,
  NULL,NULL,NULL    /* reserved[123] */
};
