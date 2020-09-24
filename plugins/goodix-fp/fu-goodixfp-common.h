/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 boger wang <boger@goodix.com>
 * 
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
/*
 *  protocol
 */
#define GX_CMD_ACK          0xAA
#define GX_CMD_VERSION      0xD0
#define GX_CMD_RESET        0xB4
#define GX_CMD_UPGRADE      0x80
#define GX_CMD_UPGRADE_INIT 0x00
#define GX_CMD_UPGRADE_DATA 0x01
#define GX_CMD1_DEFAULT     0x00

#define PACKAGE_HEADER_SIZE (8)
#define PACKAGE_CRC_SIZE (4)

typedef struct _gxfp_version_info
{
  guint8 format[2];
  guint8 fwtype[8];
  guint8 fwversion[8];
  guint8 customer[8];
  guint8 mcu[8];
  guint8 sensor[8];
  guint8 algversion[8];
  guint8 interface[8];
  guint8 protocol[8];
  guint8 flashVersion[8];
  guint8 reserved[62];
} gxfp_version_info_t, *pgxfp_version_info_t;


typedef struct _gxfp_parse_msg
{
  guint8   ack_cmd;
  gboolean has_config;
} gxfp_parse_msg_t, *pgxfp_parse_msg_t;


typedef struct _fp_cmd_response
{
  guint8 result;
  union
  {
    gxfp_parse_msg_t    parse_msg;
    gxfp_version_info_t version_info;
  };
} gxfp_cmd_response_t, *pgxfp_cmd_response_t;

typedef struct _pack_header
{
  guint8  cmd0;
  guint8  cmd1;
  guint8  pkg_flag;
  guint8  reserved;
  guint16 len;
  guint8  crc8;
  guint8  rev_crc8;
} pack_header, *ppack_header;

/* Type covert */
#define MAKE_CMD_EX(cmd0, cmd1)    ((guint16) (((cmd0) << 8) | (cmd1)))

void init_pack_header (ppack_header pheader,
                       guint16      len,
                       guint8       cmd0,
                       guint8       cmd1,
                       guint8       packagenum);

gboolean gx_proto_parse_header (guint8      *buffer,
                                guint32      buffer_len,
                                pack_header *pheader);

guint8 gx_proto_crc8_calc (guint8 *lubp_date,
                           guint32 lui_len);

guint8 gx_proto_crc32_calc (guint8  *pchMsg,
                            guint32  wDataLen,
                            guint32 *pchMsgDst);

gboolean gx_proto_parse_body (guint8               cmd,
                              guint8              *buffer,
                              guint32              buffer_len,
                              pgxfp_cmd_response_t presp);