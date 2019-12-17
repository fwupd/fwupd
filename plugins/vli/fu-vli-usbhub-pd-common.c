/*
 * Copyright (C) 2017-2019 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-vli-usbhub-pd-common.h"

guint16
fu_vli_usbhub_pd_crc16 (const guint8 *buf, gsize bufsz)
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

FuVliDeviceKind
fu_vli_usbhub_pd_guess_device_kind (guint32 fwver)
{
	guint32 tmp = (fwver & 0x0f000000) >> 24;
	if (tmp == 0x01 || tmp == 0x02 || tmp == 0x03)
		return FU_VLI_DEVICE_KIND_VL100;
	if (tmp == 0x04 || tmp == 0x05 || tmp == 0x06)
		return FU_VLI_DEVICE_KIND_VL101;
	if (tmp == 0x07 || tmp == 0x08)
		return FU_VLI_DEVICE_KIND_VL102;
	if (tmp == 0x09 || tmp == 0x0a)
		return FU_VLI_DEVICE_KIND_VL103;
	if (tmp == 0x0b)
		return FU_VLI_DEVICE_KIND_VL104;
	if (tmp == 0x0c)
		return FU_VLI_DEVICE_KIND_VL105;
	return FU_VLI_DEVICE_KIND_UNKNOWN;
}

guint32
fu_vli_usbhub_pd_get_offset_for_device_kind (FuVliDeviceKind device_kind)
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
