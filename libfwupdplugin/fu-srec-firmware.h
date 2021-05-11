/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_SREC_FIRMWARE (fu_srec_firmware_get_type ())
#define FU_TYPE_SREC_FIRMWARE_RECORD (fu_srec_firmware_record_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuSrecFirmware, fu_srec_firmware, FU, SREC_FIRMWARE, FuFirmware)

struct _FuSrecFirmwareClass
{
	FuFirmwareClass		 parent_class;
};

/**
 * FuFirmareSrecRecordKind:
 * @FU_FIRMWARE_SREC_RECORD_KIND_S0_HEADER:		Header
 * @FU_FIRMWARE_SREC_RECORD_KIND_S1_DATA_16:		16 bit data
 * @FU_FIRMWARE_SREC_RECORD_KIND_S2_DATA_24:		24 bit data
 * @FU_FIRMWARE_SREC_RECORD_KIND_S3_DATA_32:		32 bit data
 * @FU_FIRMWARE_SREC_RECORD_KIND_S4_RESERVED:		Reserved value
 * @FU_FIRMWARE_SREC_RECORD_KIND_S5_COUNT_16:		16 bit count
 * @FU_FIRMWARE_SREC_RECORD_KIND_S6_COUNT_24:		24 bit count
 * @FU_FIRMWARE_SREC_RECORD_KIND_S7_COUNT_32:		32 bit count
 * @FU_FIRMWARE_SREC_RECORD_KIND_S8_TERMINATION_24:	24 bit termination
 * @FU_FIRMWARE_SREC_RECORD_KIND_S9_TERMINATION_16:	16 bit termination
 *
 * The kind of SREC record kind.
 **/
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
	/*< private >*/
	FU_FIRMWARE_SREC_RECORD_KIND_LAST
} FuFirmareSrecRecordKind;

/**
 * FuSrecFirmwareRecord:
 *
 * A single SREC record.
 **/
typedef struct {
	guint			 ln;
	FuFirmareSrecRecordKind	 kind;
	guint32			 addr;
	GByteArray		*buf;
} FuSrecFirmwareRecord;

FuFirmware		*fu_srec_firmware_new		(void);
GPtrArray		*fu_srec_firmware_get_records	(FuSrecFirmware	*self);
GType			 fu_srec_firmware_record_get_type (void);
FuSrecFirmwareRecord	*fu_srec_firmware_record_new	(guint		 ln,
							 FuFirmareSrecRecordKind kind,
							 guint32	 addr);
