/*
 * Copyright (C) 2017 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-vli-pd-common.h"
#include "fu-vli-struct.h"

FuVliDeviceKind
fu_vli_pd_common_guess_device_kind(guint32 fwver)
{
	guint32 tmp = (fwver & 0xFF000000) >> 24;
	if (tmp == 0x01 || tmp == 0x02 || tmp == 0x03)
		return FU_VLI_DEVICE_KIND_VL100;
	if (tmp == 0x04 || tmp == 0x05 || tmp == 0x06)
		return FU_VLI_DEVICE_KIND_VL101;
	if (tmp == 0x07 || tmp == 0x08)
		return FU_VLI_DEVICE_KIND_VL102;
	if (tmp == 0x09 || tmp == 0x0A)
		return FU_VLI_DEVICE_KIND_VL103;
	if (tmp == 0x0B)
		return FU_VLI_DEVICE_KIND_VL104;
	if (tmp == 0x0C)
		return FU_VLI_DEVICE_KIND_VL105;
	if (tmp == 0x0D)
		return FU_VLI_DEVICE_KIND_VL106;
	if (tmp == 0x0E)
		return FU_VLI_DEVICE_KIND_VL107;
	if (tmp == 0xA1 || tmp == 0xB1)
		return FU_VLI_DEVICE_KIND_VL108;
	if (tmp == 0xA2 || tmp == 0xB2)
		return FU_VLI_DEVICE_KIND_VL109;
	return FU_VLI_DEVICE_KIND_UNKNOWN;
}
