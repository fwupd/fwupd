/*
 * Copyright (C) 2017-2019 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-vli-common.h"

guint8
fu_vli_common_crc8 (const guint8 *buf, gsize bufsz)
{
	guint32 crc = 0;
	for (gsize j = bufsz; j > 0; j--) {
		crc ^= (*(buf++) << 8);
		for (guint32 i = 8; i; i--) {
			if (crc & 0x8000)
				crc ^= (0x1070 << 3);
			crc <<= 1;
		}
	}
	return (guint8) (crc >> 8);
}

guint16
fu_vli_common_crc16 (const guint8 *buf, gsize bufsz)
{
	guint16 crc = 0xffff;
	for (gsize len = bufsz; len > 0; len--) {
		crc = (guint16) (crc ^ (*buf++));
		for (guint8 i = 0; i < 8; i++) {
			if (crc & 0x1) {
				crc = (crc >> 1) ^ 0xa001;
			} else {
				crc >>= 1;
			}
		}
	}
	return ~crc;
}

const gchar *
fu_vli_common_device_kind_to_string (FuVliDeviceKind device_kind)
{
	if (device_kind == FU_VLI_DEVICE_KIND_VL100)
		return "VL100";
	if (device_kind == FU_VLI_DEVICE_KIND_VL101)
		return "VL101";
	if (device_kind == FU_VLI_DEVICE_KIND_VL102)
		return "VL102";
	if (device_kind == FU_VLI_DEVICE_KIND_VL103)
		return "VL103";
	if (device_kind == FU_VLI_DEVICE_KIND_VL104)
		return "VL104";
	if (device_kind == FU_VLI_DEVICE_KIND_VL105)
		return "VL105";
	if (device_kind == FU_VLI_DEVICE_KIND_VL810)
		return "VL810";
	if (device_kind == FU_VLI_DEVICE_KIND_VL811)
		return "VL811";
	if (device_kind == FU_VLI_DEVICE_KIND_VL811PB0)
		return "VL811PB0";
	if (device_kind == FU_VLI_DEVICE_KIND_VL811PB3)
		return "VL811PB3";
	if (device_kind == FU_VLI_DEVICE_KIND_VL812B0)
		return "VL812B0";
	if (device_kind == FU_VLI_DEVICE_KIND_VL812B3)
		return "VL812B3";
	if (device_kind == FU_VLI_DEVICE_KIND_VL812Q4S)
		return "VL812Q4S";
	if (device_kind == FU_VLI_DEVICE_KIND_VL813)
		return "VL813";
	if (device_kind == FU_VLI_DEVICE_KIND_VL815)
		return "VL815";
	if (device_kind == FU_VLI_DEVICE_KIND_VL817)
		return "VL817";
	if (device_kind == FU_VLI_DEVICE_KIND_VL819)
		return "VL819";
	if (device_kind == FU_VLI_DEVICE_KIND_VL820Q7)
		return "VL820Q7";
	if (device_kind == FU_VLI_DEVICE_KIND_VL820Q8)
		return "VL820Q8";
	if (device_kind == FU_VLI_DEVICE_KIND_VL120)
		return "VL120";
	if (device_kind == FU_VLI_DEVICE_KIND_VL210)
		return "VL210";
	if (device_kind == FU_VLI_DEVICE_KIND_VL211)
		return "VL211";
	if (device_kind == FU_VLI_DEVICE_KIND_VL212)
		return "VL212";
	if (device_kind == FU_VLI_DEVICE_KIND_MSP430)
		return "MSP430";
	if (device_kind == FU_VLI_DEVICE_KIND_PS186)
		return "PS186";
	return NULL;
}

FuVliDeviceKind
fu_vli_common_device_kind_from_string (const gchar *device_kind)
{
	if (g_strcmp0 (device_kind, "VL100") == 0)
		return FU_VLI_DEVICE_KIND_VL100;
	if (g_strcmp0 (device_kind, "VL101") == 0)
		return FU_VLI_DEVICE_KIND_VL101;
	if (g_strcmp0 (device_kind, "VL102") == 0)
		return FU_VLI_DEVICE_KIND_VL102;
	if (g_strcmp0 (device_kind, "VL103") == 0)
		return FU_VLI_DEVICE_KIND_VL103;
	if (g_strcmp0 (device_kind, "VL104") == 0)
		return FU_VLI_DEVICE_KIND_VL104;
	if (g_strcmp0 (device_kind, "VL105") == 0)
		return FU_VLI_DEVICE_KIND_VL105;
	if (g_strcmp0 (device_kind, "VL810") == 0)
		return FU_VLI_DEVICE_KIND_VL810;
	if (g_strcmp0 (device_kind, "VL811") == 0)
		return FU_VLI_DEVICE_KIND_VL811;
	if (g_strcmp0 (device_kind, "VL811PB0") == 0)
		return FU_VLI_DEVICE_KIND_VL811PB0;
	if (g_strcmp0 (device_kind, "VL811PB3") == 0)
		return FU_VLI_DEVICE_KIND_VL811PB3;
	if (g_strcmp0 (device_kind, "VL812B0") == 0)
		return FU_VLI_DEVICE_KIND_VL812B0;
	if (g_strcmp0 (device_kind, "VL812B3") == 0)
		return FU_VLI_DEVICE_KIND_VL812B3;
	if (g_strcmp0 (device_kind, "VL812Q4S") == 0)
		return FU_VLI_DEVICE_KIND_VL812Q4S;
	if (g_strcmp0 (device_kind, "VL813") == 0)
		return FU_VLI_DEVICE_KIND_VL813;
	if (g_strcmp0 (device_kind, "VL815") == 0)
		return FU_VLI_DEVICE_KIND_VL815;
	if (g_strcmp0 (device_kind, "VL817") == 0)
		return FU_VLI_DEVICE_KIND_VL817;
	if (g_strcmp0 (device_kind, "VL819") == 0)
		return FU_VLI_DEVICE_KIND_VL819;
	if (g_strcmp0 (device_kind, "VL820Q7") == 0)
		return FU_VLI_DEVICE_KIND_VL820Q7;
	if (g_strcmp0 (device_kind, "VL820Q8") == 0)
		return FU_VLI_DEVICE_KIND_VL820Q8;
	if (g_strcmp0 (device_kind, "VL120") == 0)
		return FU_VLI_DEVICE_KIND_VL120;
	if (g_strcmp0 (device_kind, "VL210") == 0)
		return FU_VLI_DEVICE_KIND_VL210;
	if (g_strcmp0 (device_kind, "VL211") == 0)
		return FU_VLI_DEVICE_KIND_VL211;
	if (g_strcmp0 (device_kind, "VL212") == 0)
		return FU_VLI_DEVICE_KIND_VL212;
	if (g_strcmp0 (device_kind, "MSP430") == 0)
		return FU_VLI_DEVICE_KIND_MSP430;
	if (g_strcmp0 (device_kind, "PS186") == 0)
		return FU_VLI_DEVICE_KIND_PS186;
	return FU_VLI_DEVICE_KIND_UNKNOWN;
}

guint32
fu_vli_common_device_kind_get_size (FuVliDeviceKind device_kind)
{
	if (device_kind == FU_VLI_DEVICE_KIND_VL100)
		return 0x8000;	/* 32KB */
	if (device_kind == FU_VLI_DEVICE_KIND_VL101)
		return 0xc000;	/* 48KB */
	if (device_kind == FU_VLI_DEVICE_KIND_VL102)
		return 0x8000;	/* 32KB */
	if (device_kind == FU_VLI_DEVICE_KIND_VL103)
		return 0x8000;	/* 32KB */
	if (device_kind == FU_VLI_DEVICE_KIND_VL104)
		return 0xc000;	/* 48KB */
	if (device_kind == FU_VLI_DEVICE_KIND_VL105)
		return 0xc000;	/* 48KB */
	if (device_kind == FU_VLI_DEVICE_KIND_VL210)
		return 0x20000 * 2;
	if (device_kind == FU_VLI_DEVICE_KIND_VL211)
		return 0x20000 * 2;
	if (device_kind == FU_VLI_DEVICE_KIND_VL212)
		return 0x20000 * 2;
	if (device_kind == FU_VLI_DEVICE_KIND_VL810)
		return 0x8000;
	if (device_kind == FU_VLI_DEVICE_KIND_VL811)
		return 0x8000;
	if (device_kind == FU_VLI_DEVICE_KIND_VL811PB0)
		return 0x8000;
	if (device_kind == FU_VLI_DEVICE_KIND_VL811PB3)
		return 0x8000;
	if (device_kind == FU_VLI_DEVICE_KIND_VL812B0)
		return 0x8000;
	if (device_kind == FU_VLI_DEVICE_KIND_VL812B3)
		return 0x8000;
	if (device_kind == FU_VLI_DEVICE_KIND_VL812Q4S)
		return 0x8000;
	if (device_kind == FU_VLI_DEVICE_KIND_VL813)
		return 0x8000;
	if (device_kind == FU_VLI_DEVICE_KIND_VL815)
		return 0x20000 * 2;
	if (device_kind == FU_VLI_DEVICE_KIND_VL817)
		return 0x20000 * 2;
	if (device_kind == FU_VLI_DEVICE_KIND_VL819)
		return 0x20000 * 2;
	if (device_kind == FU_VLI_DEVICE_KIND_VL820Q7)
		return 0x20000 * 2;
	if (device_kind == FU_VLI_DEVICE_KIND_VL820Q8)
		return 0x20000 * 2;
	if (device_kind == FU_VLI_DEVICE_KIND_PS186)
		return 0x40000;
	return 0x0;
}

guint32
fu_vli_common_device_kind_get_offset (FuVliDeviceKind device_kind)
{
	if (device_kind == FU_VLI_DEVICE_KIND_VL100)
		return 0x10000;
	if (device_kind == FU_VLI_DEVICE_KIND_VL101)
		return 0x10000;
	if (device_kind == FU_VLI_DEVICE_KIND_VL102)
		return 0x20000;
	if (device_kind == FU_VLI_DEVICE_KIND_VL103)
		return 0x20000;
	if (device_kind == FU_VLI_DEVICE_KIND_VL104)
		return 0x20000;
	if (device_kind == FU_VLI_DEVICE_KIND_VL105)
		return 0x20000;
	return 0x0;
}
