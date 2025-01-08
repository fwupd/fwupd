/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"
#include "fu-ihex-struct.h"

#define FU_TYPE_IHEX_FIRMWARE (fu_ihex_firmware_get_type())
G_DECLARE_DERIVABLE_TYPE(FuIhexFirmware, fu_ihex_firmware, FU, IHEX_FIRMWARE, FuFirmware)

struct _FuIhexFirmwareClass {
	FuFirmwareClass parent_class;
};

/**
 * FuIhexFirmwareRecord:
 *
 * A single Intel HEX record.
 **/
typedef struct {
	guint ln;
	GString *buf;
	guint8 byte_cnt;
	guint32 addr;
	guint8 record_type;
	GByteArray *data;
} FuIhexFirmwareRecord;

FuFirmware *
fu_ihex_firmware_new(void);
GPtrArray *
fu_ihex_firmware_get_records(FuIhexFirmware *self) G_GNUC_NON_NULL(1);
void
fu_ihex_firmware_set_padding_value(FuIhexFirmware *self, guint8 padding_value) G_GNUC_NON_NULL(1);
