/*
 * Copyright (C) 2017 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-vli-common.h"

guint32
fu_vli_common_device_kind_get_size(FuVliDeviceKind device_kind)
{
	if (device_kind == FU_VLI_DEVICE_KIND_VL100)
		return 0x8000; /* 32KB */
	if (device_kind == FU_VLI_DEVICE_KIND_VL101)
		return 0xc000; /* 48KB */
	if (device_kind == FU_VLI_DEVICE_KIND_VL102)
		return 0x8000; /* 32KB */
	if (device_kind == FU_VLI_DEVICE_KIND_VL103)
		return 0x8000; /* 32KB */
	if (device_kind == FU_VLI_DEVICE_KIND_VL104)
		return 0xc000; /* 48KB */
	if (device_kind == FU_VLI_DEVICE_KIND_VL105)
		return 0xc000; /* 48KB */
	if (device_kind == FU_VLI_DEVICE_KIND_VL107)
		return 0x80000;
	if (device_kind == FU_VLI_DEVICE_KIND_VL122)
		return 0x80000;
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
	if (device_kind == FU_VLI_DEVICE_KIND_VL817S)
		return 0x20000 * 2;
	if (device_kind == FU_VLI_DEVICE_KIND_VL819Q7)
		return 0x20000 * 2;
	if (device_kind == FU_VLI_DEVICE_KIND_VL819Q8)
		return 0x20000 * 2;
	if (device_kind == FU_VLI_DEVICE_KIND_VL820Q7)
		return 0x20000 * 2;
	if (device_kind == FU_VLI_DEVICE_KIND_VL820Q8)
		return 0x20000 * 2;
	if (device_kind == FU_VLI_DEVICE_KIND_VL821Q7)
		return 0x20000 * 2;
	if (device_kind == FU_VLI_DEVICE_KIND_VL821Q8)
		return 0x20000 * 2;
	if (device_kind == FU_VLI_DEVICE_KIND_VL822T)
		return 0x20000 * 2;
	if (device_kind == FU_VLI_DEVICE_KIND_VL822Q5)
		return 0x20000 * 2;
	if (device_kind == FU_VLI_DEVICE_KIND_VL822Q7)
		return 0x20000 * 2;
	if (device_kind == FU_VLI_DEVICE_KIND_VL822Q8)
		return 0x20000 * 2;
	if (device_kind == FU_VLI_DEVICE_KIND_PS186)
		return 0x40000;
	if (device_kind == FU_VLI_DEVICE_KIND_VL650)
		return 0x40000;
	if (device_kind == FU_VLI_DEVICE_KIND_VL830)
		return 0x100000;
	return 0x0;
}

guint32
fu_vli_common_device_kind_get_offset(FuVliDeviceKind device_kind)
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
	if (device_kind == FU_VLI_DEVICE_KIND_VL107)
		return 0x20000;
	return 0x0;
}
