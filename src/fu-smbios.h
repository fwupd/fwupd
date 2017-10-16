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

#ifndef __FU_SMBIOS_H
#define __FU_SMBIOS_H

#include <glib-object.h>

G_BEGIN_DECLS

#define FU_TYPE_SMBIOS (fu_smbios_get_type ())

G_DECLARE_FINAL_TYPE (FuSmbios, fu_smbios, FU, SMBIOS, GObject)

FuSmbios	*fu_smbios_new			(void);

#define FU_SMBIOS_STRUCTURE_TYPE_BIOS		0x00
#define FU_SMBIOS_STRUCTURE_TYPE_SYSTEM		0x01
#define FU_SMBIOS_STRUCTURE_TYPE_BASEBOARD	0x02
#define FU_SMBIOS_STRUCTURE_TYPE_CHASSIS	0x03

gboolean	 fu_smbios_setup		(FuSmbios	*self,
						 GError		**error);
gboolean	 fu_smbios_setup_from_path	(FuSmbios	*self,
						 const gchar	*path,
						 GError		**error);
gboolean	 fu_smbios_setup_from_file	(FuSmbios	*self,
						 const gchar	*filename,
						 GError		**error);
gchar		*fu_smbios_to_string		(FuSmbios	*self);

const gchar	*fu_smbios_get_string		(FuSmbios	*self,
						 guint8		 type,
						 guint8		 offset,
						 GError		**error);
GBytes		*fu_smbios_get_data		(FuSmbios	*self,
						 guint8		 type,
						 GError		**error);

G_END_DECLS

#endif /* __FU_SMBIOS_H */
