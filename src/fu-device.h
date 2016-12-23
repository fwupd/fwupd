/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
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
G_DECLARE_DERIVABLE_TYPE (FuDevice, fu_device, FU, DEVICE, FwupdResult)

struct _FuDeviceClass
{
	FwupdResultClass	 parent_class;
};

FuDevice	*fu_device_new				(void);

/* compat setters */
#define fu_device_add_flag(d,v)			fwupd_result_add_device_flag(FWUPD_RESULT(d),v)
#define fu_device_remove_flag(d,v)		fwupd_result_remove_device_flag(FWUPD_RESULT(d),v)
#define fu_device_has_flag(d,v)			fwupd_result_has_device_flag(FWUPD_RESULT(d),v)
#define fu_device_set_checksum(d,v)		fwupd_result_set_device_checksum(FWUPD_RESULT(d),v)
#define fu_device_set_checksum_kind(d,v)	fwupd_result_set_device_checksum_kind(FWUPD_RESULT(d),v)
#define fu_device_set_created(d,v)		fwupd_result_set_device_created(FWUPD_RESULT(d),v)
#define fu_device_set_description(d,v)		fwupd_result_set_device_description(FWUPD_RESULT(d),v)
#define fu_device_set_flags(d,v)		fwupd_result_set_device_flags(FWUPD_RESULT(d),v)
#define fu_device_has_guid(d,v)			fwupd_result_has_guid(FWUPD_RESULT(d),v)
#define fu_device_set_id(d,v)			fwupd_result_set_device_id(FWUPD_RESULT(d),v)
#define fu_device_set_modified(d,v)		fwupd_result_set_device_modified(FWUPD_RESULT(d),v)
#define fu_device_set_plugin(d,v)		fwupd_result_set_device_provider(FWUPD_RESULT(d),v)
#define fu_device_set_unique_id(d,v)		fwupd_result_set_unique_id(FWUPD_RESULT(d),v)
#define fu_device_set_update_checksum(d,v)	fwupd_result_set_update_checksum(FWUPD_RESULT(d),v)
#define fu_device_set_update_description(d,v)	fwupd_result_set_update_description(FWUPD_RESULT(d),v)
#define fu_device_set_update_error(d,v)		fwupd_result_set_update_error(FWUPD_RESULT(d),v)
#define fu_device_set_update_filename(d,v)	fwupd_result_set_update_filename(FWUPD_RESULT(d),v)
#define fu_device_set_update_homepage(d,v)	fwupd_result_set_update_homepage(FWUPD_RESULT(d),v)
#define fu_device_set_update_id(d,v)		fwupd_result_set_update_id(FWUPD_RESULT(d),v)
#define fu_device_set_update_license(d,v)	fwupd_result_set_update_license(FWUPD_RESULT(d),v)
#define fu_device_set_update_name(d,v)		fwupd_result_set_update_name(FWUPD_RESULT(d),v)
#define fu_device_set_update_state(d,v)		fwupd_result_set_update_state(FWUPD_RESULT(d),v)
#define fu_device_set_update_summary(d,v)	fwupd_result_set_update_summary(FWUPD_RESULT(d),v)
#define fu_device_set_update_uri(d,v)		fwupd_result_set_update_uri(FWUPD_RESULT(d),v)
#define fu_device_set_update_vendor(d,v)	fwupd_result_set_update_vendor(FWUPD_RESULT(d),v)
#define fu_device_set_update_version(d,v)	fwupd_result_set_update_version(FWUPD_RESULT(d),v)
#define fu_device_set_vendor(d,v)		fwupd_result_set_device_vendor(FWUPD_RESULT(d),v)
#define fu_device_set_version(d,v)		fwupd_result_set_device_version(FWUPD_RESULT(d),v)
#define fu_device_set_version_lowest(d,v)	fwupd_result_set_device_version_lowest(FWUPD_RESULT(d),v)
#define fu_device_set_version_bootloader(d,v)	fwupd_result_set_device_version_bootloader(FWUPD_RESULT(d),v)
#define fu_device_set_flashes_left(d,v)		fwupd_result_set_device_flashes_left(FWUPD_RESULT(d),v)

/* compat getters */
#define fu_device_get_checksum(d)		fwupd_result_get_device_checksum(FWUPD_RESULT(d))
#define fu_device_get_flags(d)			fwupd_result_get_device_flags(FWUPD_RESULT(d))
#define fu_device_get_guids(d)			fwupd_result_get_guids(FWUPD_RESULT(d))
#define fu_device_get_guid_default(d)		fwupd_result_get_guid_default(FWUPD_RESULT(d))
#define fu_device_get_name(d)			fwupd_result_get_device_name(FWUPD_RESULT(d))
#define fu_device_get_id(d)			fwupd_result_get_device_id(FWUPD_RESULT(d))
#define fu_device_get_plugin(d)			fwupd_result_get_device_provider(FWUPD_RESULT(d))
#define fu_device_get_update_checksum(d)	fwupd_result_get_update_checksum(FWUPD_RESULT(d))
#define fu_device_get_update_error(d)		fwupd_result_get_update_error(FWUPD_RESULT(d))
#define fu_device_get_update_filename(d)	fwupd_result_get_update_filename(FWUPD_RESULT(d))
#define fu_device_get_update_state(d)		fwupd_result_get_update_state(FWUPD_RESULT(d))
#define fu_device_get_update_version(d)		fwupd_result_get_update_version(FWUPD_RESULT(d))
#define fu_device_get_version(d)		fwupd_result_get_device_version(FWUPD_RESULT(d))
#define fu_device_get_version_lowest(d)		fwupd_result_get_device_version_lowest(FWUPD_RESULT(d))
#define fu_device_get_version_bootloader(d)	fwupd_result_get_device_version_bootloader(FWUPD_RESULT(d))
#define fu_device_get_flashes_left(d)		fwupd_result_get_device_flashes_left(FWUPD_RESULT(d))

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
void		 fu_device_set_metadata			(FuDevice	*device,
							 const gchar	*key,
							 const gchar	*value);
void		 fu_device_set_name			(FuDevice	*device,
							 const gchar	*value);

G_END_DECLS

#endif /* __FU_DEVICE_H */

