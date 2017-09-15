/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __FU_DEVICE_H
#define __FU_DEVICE_H

#include <glib-object.h>
#include <fwupd.h>

G_BEGIN_DECLS

#define FU_TYPE_DEVICE (fu_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuDevice, fu_device, FU, DEVICE, FwupdDevice)

struct _FuDeviceClass
{
	FwupdDeviceClass	 parent_class;
};

FuDevice	*fu_device_new				(void);

/* helpful casting macros */
#define fu_device_add_flag(d,v)			fwupd_device_add_flag(FWUPD_DEVICE(d),v)
#define fu_device_remove_flag(d,v)		fwupd_device_remove_flag(FWUPD_DEVICE(d),v)
#define fu_device_has_flag(d,v)			fwupd_device_has_flag(FWUPD_DEVICE(d),v)
#define fu_device_add_checksum(d,v)		fwupd_device_add_checksum(FWUPD_DEVICE(d),v)
#define fu_device_add_icon(d,v)			fwupd_device_add_icon(FWUPD_DEVICE(d),v)
#define fu_device_set_created(d,v)		fwupd_device_set_created(FWUPD_DEVICE(d),v)
#define fu_device_set_description(d,v)		fwupd_device_set_description(FWUPD_DEVICE(d),v)
#define fu_device_set_flags(d,v)		fwupd_device_set_flags(FWUPD_DEVICE(d),v)
#define fu_device_has_guid(d,v)			fwupd_device_has_guid(FWUPD_DEVICE(d),v)
#define fu_device_set_id(d,v)			fwupd_device_set_id(FWUPD_DEVICE(d),v)
#define fu_device_set_modified(d,v)		fwupd_device_set_modified(FWUPD_DEVICE(d),v)
#define fu_device_set_plugin(d,v)		fwupd_device_set_plugin(FWUPD_DEVICE(d),v)
#define fu_device_set_summary(d,v)		fwupd_device_set_summary(FWUPD_DEVICE(d),v)
#define fu_device_set_update_error(d,v)		fwupd_device_set_update_error(FWUPD_DEVICE(d),v)
#define fu_device_set_update_state(d,v)		fwupd_device_set_update_state(FWUPD_DEVICE(d),v)
#define fu_device_set_vendor(d,v)		fwupd_device_set_vendor(FWUPD_DEVICE(d),v)
#define fu_device_set_vendor_id(d,v)		fwupd_device_set_vendor_id(FWUPD_DEVICE(d),v)
#define fu_device_set_version(d,v)		fwupd_device_set_version(FWUPD_DEVICE(d),v)
#define fu_device_set_version_lowest(d,v)	fwupd_device_set_version_lowest(FWUPD_DEVICE(d),v)
#define fu_device_set_version_bootloader(d,v)	fwupd_device_set_version_bootloader(FWUPD_DEVICE(d),v)
#define fu_device_set_flashes_left(d,v)		fwupd_device_set_flashes_left(FWUPD_DEVICE(d),v)
#define fu_device_get_checksums(d)		fwupd_device_get_checksums(FWUPD_DEVICE(d))
#define fu_device_get_flags(d)			fwupd_device_get_flags(FWUPD_DEVICE(d))
#define fu_device_get_guids(d)			fwupd_device_get_guids(FWUPD_DEVICE(d))
#define fu_device_get_guid_default(d)		fwupd_device_get_guid_default(FWUPD_DEVICE(d))
#define fu_device_get_icons(d)			fwupd_device_get_icons(FWUPD_DEVICE(d))
#define fu_device_get_name(d)			fwupd_device_get_name(FWUPD_DEVICE(d))
#define fu_device_get_id(d)			fwupd_device_get_id(FWUPD_DEVICE(d))
#define fu_device_get_plugin(d)			fwupd_device_get_plugin(FWUPD_DEVICE(d))
#define fu_device_get_update_error(d)		fwupd_device_get_update_error(FWUPD_DEVICE(d))
#define fu_device_get_update_state(d)		fwupd_device_get_update_state(FWUPD_DEVICE(d))
#define fu_device_get_version(d)		fwupd_device_get_version(FWUPD_DEVICE(d))
#define fu_device_get_version_lowest(d)		fwupd_device_get_version_lowest(FWUPD_DEVICE(d))
#define fu_device_get_version_bootloader(d)	fwupd_device_get_version_bootloader(FWUPD_DEVICE(d))
#define fu_device_get_vendor_id(d)		fwupd_device_get_vendor_id(FWUPD_DEVICE(d))
#define fu_device_get_flashes_left(d)		fwupd_device_get_flashes_left(FWUPD_DEVICE(d))

/* accessors */
const gchar	*fu_device_get_equivalent_id		(FuDevice	*device);
void		 fu_device_set_equivalent_id		(FuDevice	*device,
							 const gchar	*equivalent_id);
void		 fu_device_add_guid			(FuDevice	*device,
							 const gchar	*guid);
FuDevice	*fu_device_get_alternate		(FuDevice	*device);
void		 fu_device_set_alternate		(FuDevice	*device,
							 FuDevice	*alternate);
const gchar	*fu_device_get_metadata			(FuDevice	*device,
							 const gchar	*key);
gboolean	 fu_device_get_metadata_boolean		(FuDevice	*device,
							 const gchar	*key);
guint		 fu_device_get_metadata_integer		(FuDevice	*device,
							 const gchar	*key);
void		 fu_device_set_metadata			(FuDevice	*device,
							 const gchar	*key,
							 const gchar	*value);
void		 fu_device_set_metadata_boolean		(FuDevice	*device,
							 const gchar	*key,
							 gboolean	 value);
void		 fu_device_set_metadata_integer		(FuDevice	*device,
							 const gchar	*key,
							 guint		 value);
void		 fu_device_set_name			(FuDevice	*device,
							 const gchar	*value);

G_END_DECLS

#endif /* __FU_DEVICE_H */

