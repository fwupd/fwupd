/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_SREC_FIRMWARE (fu_srec_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuSrecFirmware, fu_srec_firmware, FU, SREC_FIRMWARE, FuFirmware)

typedef enum {
	FU_FIRMWARE_SREC_RECORD_KIND_S0_HEADER,
	FU_FIRMWARE_SREC_RECORD_KIND_S1_DATA_16,
	FU_FIRMWARE_SREC_RECORD_KIND_S2_DATA_24,
	FU_FIRMWARE_SREC_RECORD_KIND_S3_DATA_32,
	FU_FIRMWARE_SREC_RECORD_KIND_S4_RESERVED,
	FU_FIRMWARE_SREC_RECORD_KIND_S5_COUNT_16,
	FU_FIRMWARE_SREC_RECORD_KIND_S6_COUNT_24,
	FU_FIRMWARE_SREC_RECORD_KIND_S7_COUNT_32,
	FU_FIRMWARE_SREC_RECORD_KIND_S8_TERMINATION_24,
	FU_FIRMWARE_SREC_RECORD_KIND_S9_TERMINATION_16,
	FU_FIRMWARE_SREC_RECORD_KIND_LAST
} FuFirmareSrecRecordKind;

typedef struct {
	guint			 ln;
	FuFirmareSrecRecordKind	 kind;
	guint32			 addr;
	GByteArray		*buf;
} FuSrecFirmwareRecord;

FuFirmware		*fu_srec_firmware_new		(void);
GPtrArray		*fu_srec_firmware_get_records	(FuSrecFirmware	*self);
FuSrecFirmwareRecord	*fu_srec_firmware_record_new	(guint		 ln,
							 FuFirmareSrecRecordKind kind,
							 guint32	 addr);
