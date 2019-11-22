/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#define FU_TYPE_SMBIOS (fu_smbios_get_type ())

G_DECLARE_FINAL_TYPE (FuSmbios, fu_smbios, FU, SMBIOS, GObject)

FuSmbios	*fu_smbios_new			(void);

#define FU_SMBIOS_STRUCTURE_TYPE_BIOS		0x00
#define FU_SMBIOS_STRUCTURE_TYPE_SYSTEM		0x01
#define FU_SMBIOS_STRUCTURE_TYPE_BASEBOARD	0x02
#define FU_SMBIOS_STRUCTURE_TYPE_CHASSIS	0x03

gchar		*fu_smbios_to_string		(FuSmbios	*self);

const gchar	*fu_smbios_get_string		(FuSmbios	*self,
						 guint8		 type,
						 guint8		 offset,
						 GError		**error);
GBytes		*fu_smbios_get_data		(FuSmbios	*self,
						 guint8		 type,
						 GError		**error);
