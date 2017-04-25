/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef __FU_HWIDS_H
#define __FU_HWIDS_H

#include <glib-object.h>

G_BEGIN_DECLS

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
gboolean	 fu_hwids_setup			(FuHwids	*self,
						 const gchar	*sysfsdir,
						 GError		**error);

G_END_DECLS

#endif /* __FU_HWIDS_H */
