/*
 * Copyright 2017 VIA Corporation
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-vli-pd-common.h"
#include "fu-vli-struct.h"

FuVliDeviceKind
fu_vli_pd_common_guess_device_kind(guint32 fwver)
{
	guint32 tmp = (fwver & 0xFF000000) >> 24;
	if (tmp < 0xA0)
		tmp = tmp & 0x0F;
	if (tmp == FU_VLI_DEVICE_FW_TAG_VL100A || tmp == FU_VLI_DEVICE_FW_TAG_VL100B ||
	    tmp == FU_VLI_DEVICE_FW_TAG_VL100C)
		return FU_VLI_DEVICE_KIND_VL100;
	if (tmp == FU_VLI_DEVICE_FW_TAG_VL101A || tmp == FU_VLI_DEVICE_FW_TAG_VL101B ||
	    tmp == FU_VLI_DEVICE_FW_TAG_VL101C)
		return FU_VLI_DEVICE_KIND_VL101;
	if (tmp == FU_VLI_DEVICE_FW_TAG_VL102A || tmp == FU_VLI_DEVICE_FW_TAG_VL102B)
		return FU_VLI_DEVICE_KIND_VL102;
	if (tmp == FU_VLI_DEVICE_FW_TAG_VL103A || tmp == FU_VLI_DEVICE_FW_TAG_VL103B)
		return FU_VLI_DEVICE_KIND_VL103;
	if (tmp == FU_VLI_DEVICE_FW_TAG_VL104)
		return FU_VLI_DEVICE_KIND_VL104;
	if (tmp == FU_VLI_DEVICE_FW_TAG_VL105)
		return FU_VLI_DEVICE_KIND_VL105;
	if (tmp == FU_VLI_DEVICE_FW_TAG_VL106)
		return FU_VLI_DEVICE_KIND_VL106;
	if (tmp == FU_VLI_DEVICE_FW_TAG_VL107)
		return FU_VLI_DEVICE_KIND_VL107;
	if (tmp == FU_VLI_DEVICE_FW_TAG_VL108A || tmp == FU_VLI_DEVICE_FW_TAG_VL108B)
		return FU_VLI_DEVICE_KIND_VL108;
	if (tmp == FU_VLI_DEVICE_FW_TAG_VL109A || tmp == FU_VLI_DEVICE_FW_TAG_VL109B)
		return FU_VLI_DEVICE_KIND_VL109;
	return FU_VLI_DEVICE_KIND_UNKNOWN;
}
