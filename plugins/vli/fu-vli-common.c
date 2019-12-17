/*
 * Copyright (C) 2017-2019 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-vli-common.h"

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
	return NULL;
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
	return 0x0;
}

