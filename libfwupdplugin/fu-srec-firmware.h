/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"
#include "fu-srec-struct.h"

/* FIXME: rename */
#define FuFirmwareSrecRecordKind FuFirmwareSrecRecordKind

#define FU_TYPE_SREC_FIRMWARE	     (fu_srec_firmware_get_type())
#define FU_TYPE_SREC_FIRMWARE_RECORD (fu_srec_firmware_record_get_type())
G_DECLARE_DERIVABLE_TYPE(FuSrecFirmware, fu_srec_firmware, FU, SREC_FIRMWARE, FuFirmware)

struct _FuSrecFirmwareClass {
	FuFirmwareClass parent_class;
};

/**
 * FuSrecFirmwareRecord:
 *
 * A single SREC record.
 **/
typedef struct {
	guint ln;
	FuFirmwareSrecRecordKind kind;
	guint32 addr;
	GByteArray *buf;
} FuSrecFirmwareRecord;

FuFirmware *
fu_srec_firmware_new(void);
void
fu_srec_firmware_set_addr_min(FuSrecFirmware *self, guint32 addr_min) G_GNUC_NON_NULL(1);
void
fu_srec_firmware_set_addr_max(FuSrecFirmware *self, guint32 addr_max) G_GNUC_NON_NULL(1);
GPtrArray *
fu_srec_firmware_get_records(FuSrecFirmware *self) G_GNUC_NON_NULL(1);
GType
fu_srec_firmware_record_get_type(void);
FuSrecFirmwareRecord *
fu_srec_firmware_record_new(guint ln, FuFirmwareSrecRecordKind kind, guint32 addr);
