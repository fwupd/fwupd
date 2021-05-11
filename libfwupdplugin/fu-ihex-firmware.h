/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_IHEX_FIRMWARE (fu_ihex_firmware_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuIhexFirmware, fu_ihex_firmware, FU, IHEX_FIRMWARE, FuFirmware)

struct _FuIhexFirmwareClass
{
	FuFirmwareClass		 parent_class;
};

/**
 * FuIhexFirmwareRecord:
 *
 * A single Intel HEX record.
 **/
typedef struct {
	guint		 ln;
	GString		*buf;
	guint8		 byte_cnt;
	guint32		 addr;
	guint8		 record_type;
	GByteArray	*data;
} FuIhexFirmwareRecord;

#define FU_IHEX_FIRMWARE_RECORD_TYPE_DATA		0x00
#define FU_IHEX_FIRMWARE_RECORD_TYPE_EOF		0x01
#define FU_IHEX_FIRMWARE_RECORD_TYPE_EXTENDED_SEGMENT	0x02
#define FU_IHEX_FIRMWARE_RECORD_TYPE_START_SEGMENT	0x03
#define FU_IHEX_FIRMWARE_RECORD_TYPE_EXTENDED_LINEAR	0x04
#define FU_IHEX_FIRMWARE_RECORD_TYPE_START_LINEAR	0x05
#define FU_IHEX_FIRMWARE_RECORD_TYPE_SIGNATURE		0xfd

FuFirmware	*fu_ihex_firmware_new			(void);
GPtrArray	*fu_ihex_firmware_get_records		(FuIhexFirmware	*self);
void		 fu_ihex_firmware_set_padding_value	(FuIhexFirmware	*self,
							 guint8		 padding_value);
