/*****************************************************************************
Copyright (c) 2018 - 2020 Qualcomm Technologies International, Ltd.
******************************************************************************/

/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: central.proto */

/* Do not generate deprecated warnings for self */
#ifndef PROTOBUF_C__NO_DEPRECATED
#define PROTOBUF_C__NO_DEPRECATED
#endif

#include "central.pb-c.h"
void   central_information__init
                     (CentralInformation         *message)
{
  static const CentralInformation init_value = CENTRAL_INFORMATION__INIT;
  *message = init_value;
}
size_t central_information__get_packed_size
                     (const CentralInformation *message)
{
  assert(message->base.descriptor == &central_information__descriptor);
  return protobuf_c_message_get_packed_size ((const ProtobufCMessage*)(message));
}
size_t central_information__pack
                     (const CentralInformation *message,
                      uint8_t       *out)
{
  assert(message->base.descriptor == &central_information__descriptor);
  return protobuf_c_message_pack ((const ProtobufCMessage*)message, out);
}
size_t central_information__pack_to_buffer
                     (const CentralInformation *message,
                      ProtobufCBuffer *buffer)
{
  assert(message->base.descriptor == &central_information__descriptor);
  return protobuf_c_message_pack_to_buffer ((const ProtobufCMessage*)message, buffer);
}
CentralInformation *
       central_information__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data)
{
  return (CentralInformation *)
     protobuf_c_message_unpack (&central_information__descriptor,
                                allocator, len, data);
}
void   central_information__free_unpacked
                     (CentralInformation *message,
                      ProtobufCAllocator *allocator)
{
  if(!message)
    return;
  assert(message->base.descriptor == &central_information__descriptor);
  protobuf_c_message_free_unpacked ((ProtobufCMessage*)message, allocator);
}
void   get_central_information__init
                     (GetCentralInformation         *message)
{
  static const GetCentralInformation init_value = GET_CENTRAL_INFORMATION__INIT;
  *message = init_value;
}
size_t get_central_information__get_packed_size
                     (const GetCentralInformation *message)
{
  assert(message->base.descriptor == &get_central_information__descriptor);
  return protobuf_c_message_get_packed_size ((const ProtobufCMessage*)(message));
}
size_t get_central_information__pack
                     (const GetCentralInformation *message,
                      uint8_t       *out)
{
  assert(message->base.descriptor == &get_central_information__descriptor);
  return protobuf_c_message_pack ((const ProtobufCMessage*)message, out);
}
size_t get_central_information__pack_to_buffer
                     (const GetCentralInformation *message,
                      ProtobufCBuffer *buffer)
{
  assert(message->base.descriptor == &get_central_information__descriptor);
  return protobuf_c_message_pack_to_buffer ((const ProtobufCMessage*)message, buffer);
}
GetCentralInformation *
       get_central_information__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data)
{
  return (GetCentralInformation *)
     protobuf_c_message_unpack (&get_central_information__descriptor,
                                allocator, len, data);
}
void   get_central_information__free_unpacked
                     (GetCentralInformation *message,
                      ProtobufCAllocator *allocator)
{
  if(!message)
    return;
  assert(message->base.descriptor == &get_central_information__descriptor);
  protobuf_c_message_free_unpacked ((ProtobufCMessage*)message, allocator);
}
static const ProtobufCFieldDescriptor central_information__field_descriptors[2] =
{
  {
    "name",
    1,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(CentralInformation, name),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "platform",
    2,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_ENUM,
    0,   /* quantifier_offset */
    offsetof(CentralInformation, platform),
    &platform__descriptor,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
};
static const unsigned central_information__field_indices_by_name[] = {
  0,   /* field[0] = name */
  1,   /* field[1] = platform */
};
static const ProtobufCIntRange central_information__number_ranges[1 + 1] =
{
  { 1, 0 },
  { 0, 2 }
};
const ProtobufCMessageDescriptor central_information__descriptor =
{
  PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
  "CentralInformation",
  "CentralInformation",
  "CentralInformation",
  "",
  sizeof(CentralInformation),
  2,
  central_information__field_descriptors,
  central_information__field_indices_by_name,
  1,  central_information__number_ranges,
  (ProtobufCMessageInit) central_information__init,
  NULL,NULL,NULL    /* reserved[123] */
};
#define get_central_information__field_descriptors NULL
#define get_central_information__field_indices_by_name NULL
#define get_central_information__number_ranges NULL
const ProtobufCMessageDescriptor get_central_information__descriptor =
{
  PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
  "GetCentralInformation",
  "GetCentralInformation",
  "GetCentralInformation",
  "",
  sizeof(GetCentralInformation),
  0,
  get_central_information__field_descriptors,
  get_central_information__field_indices_by_name,
  0,  get_central_information__number_ranges,
  (ProtobufCMessageInit) get_central_information__init,
  NULL,NULL,NULL    /* reserved[123] */
};
static const ProtobufCEnumValue platform__enum_values_by_number[3] =
{
  { "UNDEFINED", "PLATFORM__UNDEFINED", 0 },
  { "IOS", "PLATFORM__IOS", 1 },
  { "ANDROID", "PLATFORM__ANDROID", 2 },
};
static const ProtobufCIntRange platform__value_ranges[] = {
{0, 0},{0, 3}
};
static const ProtobufCEnumValueIndex platform__enum_values_by_name[3] =
{
  { "ANDROID", 2 },
  { "IOS", 1 },
  { "UNDEFINED", 0 },
};
const ProtobufCEnumDescriptor platform__descriptor =
{
  PROTOBUF_C__ENUM_DESCRIPTOR_MAGIC,
  "Platform",
  "Platform",
  "Platform",
  "",
  3,
  platform__enum_values_by_number,
  3,
  platform__enum_values_by_name,
  1,
  platform__value_ranges,
  NULL,NULL,NULL,NULL   /* reserved[1234] */
};