/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ccgx-common.h"

gchar *
fu_ccgx_version_to_string(guint32 val)
{
	/* 16 bits: application type [LSB]
	 *  8 bits: build number
	 *  4 bits: minor version
	 *  4 bits: major version [MSB] */
	return g_strdup_printf("%u.%u.%u",
			       (val >> 28) & 0x0f,
			       (val >> 24) & 0x0f,
			       (val >> 16) & 0xff);
}

const gchar *
fu_ccgx_fw_mode_to_string(FWMode val)
{
	if (val == FW_MODE_BOOT)
		return "BOOT";
	if (val == FW_MODE_FW1)
		return "FW1";
	if (val == FW_MODE_FW2)
		return "FW2";
	return NULL;
}

const gchar *
fu_ccgx_fw_image_type_to_string(FWImageType val)
{
	if (val == FW_IMAGE_TYPE_SINGLE)
		return "single";
	if (val == FW_IMAGE_TYPE_DUAL_SYMMETRIC)
		return "dual-symmetric";
	if (val == FW_IMAGE_TYPE_DUAL_ASYMMETRIC)
		return "dual-asymmetric";
	if (val == FW_IMAGE_TYPE_DUAL_ASYMMETRIC_VARIABLE)
		return "dual-asymmetric-variable";
	if (val == FW_IMAGE_TYPE_DMC_COMPOSITE)
		return "dmc-composite";
	return NULL;
}

FWImageType
fu_ccgx_fw_image_type_from_string(const gchar *val)
{
	if (g_strcmp0(val, "single") == 0)
		return FW_IMAGE_TYPE_SINGLE;
	if (g_strcmp0(val, "dual-symmetric") == 0)
		return FW_IMAGE_TYPE_DUAL_SYMMETRIC;
	if (g_strcmp0(val, "dual-asymmetric") == 0)
		return FW_IMAGE_TYPE_DUAL_ASYMMETRIC;
	if (g_strcmp0(val, "dual-asymmetric-variable") == 0)
		return FW_IMAGE_TYPE_DUAL_ASYMMETRIC_VARIABLE;
	if (g_strcmp0(val, "dmc-composite") == 0)
		return FW_IMAGE_TYPE_DMC_COMPOSITE;
	return FW_IMAGE_TYPE_UNKNOWN;
}

FWMode
fu_ccgx_fw_mode_get_alternate(FWMode val)
{
	if (val == FW_MODE_FW1)
		return FW_MODE_FW2;
	if (val == FW_MODE_FW2)
		return FW_MODE_FW1;
	return FW_MODE_BOOT;
}
