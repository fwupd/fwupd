/*
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

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
const gchar	*fwupd_device_get_parent_id		(FwupdDevice	*device);
void		 fwupd_device_set_parent_id		(FwupdDevice	*device,
							 const gchar	*parent_id);
FwupdDevice	*fwupd_device_get_parent		(FwupdDevice	*device);
void		 fwupd_device_set_parent		(FwupdDevice	*device,
							 FwupdDevice	*parent);
GPtrArray	*fwupd_device_get_children		(FwupdDevice	*device);
const gchar	*fwupd_device_get_name			(FwupdDevice	*device);
void		 fwupd_device_set_name			(FwupdDevice	*device,
							 const gchar	*name);
const gchar	*fwupd_device_get_serial		(FwupdDevice	*device);
void		 fwupd_device_set_serial		(FwupdDevice	*device,
							 const gchar	*serial);
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
guint64		 fwupd_device_get_version_raw		(FwupdDevice	*device);
void		 fwupd_device_set_version_raw		(FwupdDevice	*device,
							 guint64	version_raw);
FwupdVersionFormat fwupd_device_get_version_format	(FwupdDevice	*device);
void		 fwupd_device_set_version_format	(FwupdDevice	*device,
							 FwupdVersionFormat version_format);
guint32		 fwupd_device_get_flashes_left		(FwupdDevice	*device);
void		 fwupd_device_set_flashes_left		(FwupdDevice	*device,
							 guint32	flashes_left);
guint32		 fwupd_device_get_install_duration	(FwupdDevice	*device);
void		 fwupd_device_set_install_duration	(FwupdDevice	*device,
							 guint32	 duration);
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
const gchar	*fwupd_device_get_protocol		(FwupdDevice	*device);
void		 fwupd_device_set_protocol		(FwupdDevice	*device,
							 const gchar	*protocol);
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
void		 fwupd_device_add_instance_id		(FwupdDevice	*device,
							 const gchar	*instance_id);
gboolean	 fwupd_device_has_instance_id		(FwupdDevice	*device,
							 const gchar	*instance_id);
GPtrArray	*fwupd_device_get_instance_ids		(FwupdDevice	*device);
void		 fwupd_device_add_icon			(FwupdDevice	*device,
							 const gchar	*icon);
GPtrArray	*fwupd_device_get_icons			(FwupdDevice	*device);

FwupdUpdateState fwupd_device_get_update_state		(FwupdDevice	*device);
void		 fwupd_device_set_update_state		(FwupdDevice	*device,
							 FwupdUpdateState update_state);
const gchar	*fwupd_device_get_update_error		(FwupdDevice	*device);
void		 fwupd_device_set_update_error		(FwupdDevice	*device,
							 const gchar	*update_error);
const gchar	*fwupd_device_get_update_message	(FwupdDevice	*device);
void		 fwupd_device_set_update_message	(FwupdDevice	*device,
							 const gchar	*update_message);
void		 fwupd_device_add_release		(FwupdDevice	*device,
							 FwupdRelease	*release);
GPtrArray	*fwupd_device_get_releases		(FwupdDevice	*device);
FwupdRelease	*fwupd_device_get_release_default	(FwupdDevice	*device);
gint		 fwupd_device_compare			(FwupdDevice	*device1,
							 FwupdDevice	*device2);

FwupdDevice	*fwupd_device_from_variant		(GVariant	*value);
GPtrArray	*fwupd_device_array_from_variant	(GVariant	*value);
void		 fwupd_device_array_ensure_parents	(GPtrArray	*devices);

G_END_DECLS
