/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#include "fu-smbios.h"

#define FU_TYPE_HWIDS (fu_hwids_get_type ())

G_DECLARE_FINAL_TYPE (FuHwids, fu_hwids, FU, HWIDS, GObject)

#define FU_HWIDS_KEY_BASEBOARD_MANUFACTURER	"BaseboardManufacturer"
#define FU_HWIDS_KEY_BASEBOARD_PRODUCT		"BaseboardProduct"
#define FU_HWIDS_KEY_BIOS_MAJOR_RELEASE		"BiosMajorRelease"
#define FU_HWIDS_KEY_BIOS_MINOR_RELEASE		"BiosMinorRelease"
#define FU_HWIDS_KEY_BIOS_VENDOR		"BiosVendor"
#define FU_HWIDS_KEY_BIOS_VERSION		"BiosVersion"
#define FU_HWIDS_KEY_ENCLOSURE_KIND		"EnclosureKind"
#define FU_HWIDS_KEY_FAMILY			"Family"
#define FU_HWIDS_KEY_MANUFACTURER		"Manufacturer"
#define FU_HWIDS_KEY_PRODUCT_NAME		"ProductName"
#define FU_HWIDS_KEY_PRODUCT_SKU		"ProductSku"

FuHwids		*fu_hwids_new			(void);

const gchar	*fu_hwids_get_value		(FuHwids	*self,
						 const gchar	*key);
const gchar	*fu_hwids_get_replace_keys	(FuHwids	*self,
						 const gchar	*key);
gchar		*fu_hwids_get_replace_values	(FuHwids	*self,
						 const gchar	*keys,
						 GError		**error);
gchar		*fu_hwids_get_guid		(FuHwids	*self,
						 const gchar	*keys,
						 GError		**error);
GPtrArray	*fu_hwids_get_guids		(FuHwids	*self);
gboolean	 fu_hwids_has_guid		(FuHwids	*self,
						 const gchar	*guid);
gboolean	 fu_hwids_setup			(FuHwids	*self,
						 FuSmbios	*smbios,
						 GError		**error);
