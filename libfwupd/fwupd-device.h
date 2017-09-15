/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
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

#ifndef __FWUPD_DEVICE_H
#define __FWUPD_DEVICE_H

#include <glib-object.h>

#include "fwupd-enums.h"
#include "fwupd-release.h"

G_BEGIN_DECLS

#define FWUPD_TYPE_DEVICE (fwupd_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FwupdDevice, fwupd_device, FWUPD, DEVICE, GObject)

struct _FwupdDeviceClass
{
	GObjectClass			 parent_class;
	/*< private >*/
	void (*_fwupd_reserved1)	(void);
	void (*_fwupd_reserved2)	(void);
	void (*_fwupd_reserved3)	(void);
	void (*_fwupd_reserved4)	(void);
	void (*_fwupd_reserved5)	(void);
	void (*_fwupd_reserved6)	(void);
	void (*_fwupd_reserved7)	(void);
};

FwupdDevice	*fwupd_device_new			(void);
gchar		*fwupd_device_to_string			(FwupdDevice	*device);

const gchar	*fwupd_device_get_id			(FwupdDevice	*device);
void		 fwupd_device_set_id			(FwupdDevice	*device,
							 const gchar	*id);
const gchar	*fwupd_device_get_name			(FwupdDevice	*device);
void		 fwupd_device_set_name			(FwupdDevice	*device,
							 const gchar	*name);
const gchar	*fwupd_device_get_summary		(FwupdDevice	*device);
void		 fwupd_device_set_summary		(FwupdDevice	*device,
							 const gchar	*summary);
const gchar	*fwupd_device_get_description		(FwupdDevice	*device);
void		 fwupd_device_set_description		(FwupdDevice	*device,
							 const gchar	*description);
const gchar	*fwupd_device_get_version		(FwupdDevice	*device);
void		 fwupd_device_set_version		(FwupdDevice	*device,
							 const gchar	*version);
const gchar	*fwupd_device_get_version_lowest	(FwupdDevice	*device);
void		 fwupd_device_set_version_lowest	(FwupdDevice	*device,
							 const gchar	*version_lowest);
const gchar	*fwupd_device_get_version_bootloader	(FwupdDevice	*device);
void		 fwupd_device_set_version_bootloader	(FwupdDevice	*device,
							 const gchar	*version_bootloader);
guint32		 fwupd_device_get_flashes_left		(FwupdDevice	*device);
void		 fwupd_device_set_flashes_left		(FwupdDevice	*device,
							 guint32	flashes_left);
guint64		 fwupd_device_get_flags			(FwupdDevice	*device);
void		 fwupd_device_set_flags			(FwupdDevice	*device,
							 guint64	 flags);
void		 fwupd_device_add_flag			(FwupdDevice	*device,
							 FwupdDeviceFlags flag);
void		 fwupd_device_remove_flag		(FwupdDevice	*device,
							 FwupdDeviceFlags flag);
gboolean	 fwupd_device_has_flag			(FwupdDevice	*device,
							 FwupdDeviceFlags flag);
guint64		 fwupd_device_get_created		(FwupdDevice	*device);
void		 fwupd_device_set_created		(FwupdDevice	*device,
							 guint64	 created);
guint64		 fwupd_device_get_modified		(FwupdDevice	*device);
void		 fwupd_device_set_modified		(FwupdDevice	*device,
							 guint64	 modified);
GPtrArray	*fwupd_device_get_checksums		(FwupdDevice	*device);
void		 fwupd_device_add_checksum		(FwupdDevice	*device,
							 const gchar	*checksum);
const gchar	*fwupd_device_get_plugin		(FwupdDevice	*device);
void		 fwupd_device_set_plugin		(FwupdDevice	*device,
							 const gchar	*plugin);
const gchar	*fwupd_device_get_vendor		(FwupdDevice	*device);
void		 fwupd_device_set_vendor		(FwupdDevice	*device,
							 const gchar	*vendor);
const gchar	*fwupd_device_get_vendor_id		(FwupdDevice	*device);
void		 fwupd_device_set_vendor_id		(FwupdDevice	*device,
							 const gchar	*vendor_id);
void		 fwupd_device_add_guid			(FwupdDevice	*device,
							 const gchar	*guid);
gboolean	 fwupd_device_has_guid			(FwupdDevice	*device,
							 const gchar	*guid);
GPtrArray	*fwupd_device_get_guids			(FwupdDevice	*device);
const gchar	*fwupd_device_get_guid_default		(FwupdDevice	*device);
void		 fwupd_device_add_icon			(FwupdDevice	*device,
							 const gchar	*icon);
GPtrArray	*fwupd_device_get_icons			(FwupdDevice	*device);

FwupdUpdateState fwupd_device_get_update_state		(FwupdDevice	*device);
void		 fwupd_device_set_update_state		(FwupdDevice	*device,
							 FwupdUpdateState update_state);
const gchar	*fwupd_device_get_update_error		(FwupdDevice	*device);
void		 fwupd_device_set_update_error		(FwupdDevice	*device,
							 const gchar	*update_error);
void		 fwupd_device_add_release		(FwupdDevice	*device,
							 FwupdRelease	*release);
GPtrArray	*fwupd_device_get_releases		(FwupdDevice	*device);
FwupdRelease	*fwupd_device_get_release_default	(FwupdDevice	*device);

G_END_DECLS

#endif /* __FWUPD_DEVICE_H */

