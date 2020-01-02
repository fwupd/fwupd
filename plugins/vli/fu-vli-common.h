/*
 * Copyright (C) 2017-2019 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

typedef enum {
	FU_VLI_DEVICE_KIND_UNKNOWN		= 0x0000,
	FU_VLI_DEVICE_KIND_VL100		= 0x0100,
	FU_VLI_DEVICE_KIND_VL101		= 0x0101,
	FU_VLI_DEVICE_KIND_VL102		= 0x0102,
	FU_VLI_DEVICE_KIND_VL103		= 0x0103,
	FU_VLI_DEVICE_KIND_VL104		= 0x0104,
	FU_VLI_DEVICE_KIND_VL105		= 0x0105,
	FU_VLI_DEVICE_KIND_VL120		= 0x0120,
	FU_VLI_DEVICE_KIND_VL210		= 0x0210,
	FU_VLI_DEVICE_KIND_VL211		= 0x0211,
	FU_VLI_DEVICE_KIND_VL212		= 0x0212,
	FU_VLI_DEVICE_KIND_VL810		= 0x0810,
	FU_VLI_DEVICE_KIND_VL811		= 0x0811,
	FU_VLI_DEVICE_KIND_VL811PB0		= 0x8110,
	FU_VLI_DEVICE_KIND_VL811PB3		= 0x8113,
	FU_VLI_DEVICE_KIND_VL812B0		= 0xa812,
	FU_VLI_DEVICE_KIND_VL812B3		= 0xb812,
	FU_VLI_DEVICE_KIND_VL812Q4S		= 0xc812,
	FU_VLI_DEVICE_KIND_VL813		= 0x0813,
	FU_VLI_DEVICE_KIND_VL815		= 0x0815,
	FU_VLI_DEVICE_KIND_VL817		= 0x0817,
	FU_VLI_DEVICE_KIND_VL819		= 0x0819,
	FU_VLI_DEVICE_KIND_VL820Q7		= 0xa820,
	FU_VLI_DEVICE_KIND_VL820Q8		= 0xb820,
	FU_VLI_DEVICE_KIND_MSP430		= 0xf430, /* guessed */
	FU_VLI_DEVICE_KIND_PS186		= 0xf186, /* guessed */
} FuVliDeviceKind;

const gchar	*fu_vli_common_device_kind_to_string	(FuVliDeviceKind	 device_kind);
FuVliDeviceKind	 fu_vli_common_device_kind_from_string	(const gchar		*device_kind);
guint32		 fu_vli_common_device_kind_get_size	(FuVliDeviceKind	 device_kind);
guint32		 fu_vli_common_device_kind_get_offset	(FuVliDeviceKind	 device_kind);

guint8		 fu_vli_common_crc8			(const guint8		*buf,
							 gsize			 bufsz);
guint16		 fu_vli_common_crc16			(const guint8		*buf,
							 gsize			 bufsz);
