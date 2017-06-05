/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
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

#ifndef __FWUPD_RESULT_H
#define __FWUPD_RESULT_H

#include <glib-object.h>

#include "fwupd-device.h"
#include "fwupd-enums.h"
#include "fwupd-release.h"

G_BEGIN_DECLS

#define FWUPD_TYPE_RESULT (fwupd_result_get_type ())
G_DECLARE_DERIVABLE_TYPE (FwupdResult, fwupd_result, FWUPD, RESULT, GObject)

struct _FwupdResultClass
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

FwupdResult	*fwupd_result_new			(void);
FwupdResult	*fwupd_result_new_from_data		(GVariant	*data);

/* matches */
G_DEPRECATED_FOR(fwupd_device_add_guid)
void		 fwupd_result_add_guid			(FwupdResult	*result,
							 const gchar	*guid);
G_DEPRECATED_FOR(fwupd_device_has_guid)
gboolean	 fwupd_result_has_guid			(FwupdResult	*result,
							 const gchar	*guid);
G_DEPRECATED_FOR(fwupd_device_get_guids)
GPtrArray	*fwupd_result_get_guids			(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_device_get_guid_default)
const gchar	*fwupd_result_get_guid_default		(FwupdResult	*result);

const gchar	*fwupd_result_get_unique_id		(FwupdResult	*result);
void		 fwupd_result_set_unique_id		(FwupdResult	*result,
							 const gchar	*unique_id);

/* helper objects */
FwupdRelease	*fwupd_result_get_release		(FwupdResult	*result);
FwupdDevice	*fwupd_result_get_device		(FwupdResult	*result);

/* device-specific */
G_DEPRECATED_FOR(fwupd_device_get_id)
const gchar	*fwupd_result_get_device_id		(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_device_set_id)
void		 fwupd_result_set_device_id		(FwupdResult	*result,
							 const gchar	*device_id);
G_DEPRECATED_FOR(fwupd_device_get_name)
const gchar	*fwupd_result_get_device_name		(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_device_set_name)
void		 fwupd_result_set_device_name		(FwupdResult	*result,
							 const gchar	*device_name);
G_DEPRECATED_FOR(fwupd_device_get_description)
const gchar	*fwupd_result_get_device_description	(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_device_set_description)
void		 fwupd_result_set_device_description	(FwupdResult	*result,
							 const gchar	*device_description);
G_DEPRECATED_FOR(fwupd_device_get_version)
const gchar	*fwupd_result_get_device_version	(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_device_set_version)
void		 fwupd_result_set_device_version	(FwupdResult	*result,
							 const gchar	*device_version);
G_DEPRECATED_FOR(fwupd_device_get_version_lowest)
const gchar	*fwupd_result_get_device_version_lowest	(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_device_set_version_lowest)
void		 fwupd_result_set_device_version_lowest	(FwupdResult	*result,
							 const gchar	*device_version_lowest);
G_DEPRECATED_FOR(fwupd_device_get_version_bootloader)
const gchar	*fwupd_result_get_device_version_bootloader	(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_device_set_version_bootloader)
void		 fwupd_result_set_device_version_bootloader	(FwupdResult	*result,
							 const gchar	*device_version_bootloader);
G_DEPRECATED_FOR(fwupd_device_get_flashes_left)
guint32		 fwupd_result_get_device_flashes_left	(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_device_set_flashes_left)
void		 fwupd_result_set_device_flashes_left	(FwupdResult	*result,
							 guint32	flashes_left);
G_DEPRECATED_FOR(fwupd_device_get_flags)
guint64		 fwupd_result_get_device_flags		(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_device_set_flags)
void		 fwupd_result_set_device_flags		(FwupdResult	*result,
							 guint64	 device_flags);
G_DEPRECATED_FOR(fwupd_device_add_flag)
void		 fwupd_result_add_device_flag		(FwupdResult	*result,
							 FwupdDeviceFlags flag);
G_DEPRECATED_FOR(fwupd_device_remove_flag)
void		 fwupd_result_remove_device_flag	(FwupdResult	*result,
							 FwupdDeviceFlags flag);
G_DEPRECATED_FOR(fwupd_device_has_flag)
gboolean	 fwupd_result_has_device_flag		(FwupdResult	*result,
							 FwupdDeviceFlags flag);
G_DEPRECATED_FOR(fwupd_device_get_created)
guint64		 fwupd_result_get_device_created	(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_device_set_created)
void		 fwupd_result_set_device_created	(FwupdResult	*result,
							 guint64	 device_created);
G_DEPRECATED_FOR(fwupd_device_get_modified)
guint64		 fwupd_result_get_device_modified	(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_device_set_modified)
void		 fwupd_result_set_device_modified	(FwupdResult	*result,
							 guint64	 device_modified);
G_DEPRECATED_FOR(fwupd_device_get_checksum)
const gchar	*fwupd_result_get_device_checksum	(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_device_set_checksum)
void		 fwupd_result_set_device_checksum	(FwupdResult	*result,
							 const gchar	*device_checksum);
G_DEPRECATED_FOR(fwupd_device_get_checksum_kind)
GChecksumType	 fwupd_result_get_device_checksum_kind	(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_device_set_checksum_kind)
void		 fwupd_result_set_device_checksum_kind	(FwupdResult	*result,
							 GChecksumType	 checkum_kind);
G_DEPRECATED_FOR(fwupd_device_get_provider)
const gchar	*fwupd_result_get_device_provider	(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_device_set_provider)
void		 fwupd_result_set_device_provider	(FwupdResult	*result,
							 const gchar	*device_provider);
G_DEPRECATED_FOR(fwupd_device_get_vendor)
const gchar	*fwupd_result_get_device_vendor		(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_device_set_vendor)
void		 fwupd_result_set_device_vendor		(FwupdResult	*result,
							 const gchar	*device_vendor);

/* update-specific */
G_DEPRECATED_FOR(fwupd_release_get_size)
guint64		 fwupd_result_get_update_size		(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_release_set_size)
void		 fwupd_result_set_update_size		(FwupdResult	*result,
							 guint64	 update_size);
G_DEPRECATED_FOR(fwupd_release_get_version)
const gchar	*fwupd_result_get_update_version	(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_release_set_version)
void		 fwupd_result_set_update_version	(FwupdResult	*result,
							 const gchar	*update_version);
G_DEPRECATED_FOR(fwupd_release_get_filename)
const gchar	*fwupd_result_get_update_filename	(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_release_set_filename)
void		 fwupd_result_set_update_filename	(FwupdResult	*result,
							 const gchar	*update_filename);
FwupdUpdateState fwupd_result_get_update_state		(FwupdResult	*result);
void		 fwupd_result_set_update_state		(FwupdResult	*result,
							 FwupdUpdateState update_state);
G_DEPRECATED_FOR(fwupd_release_get_checksum)
const gchar	*fwupd_result_get_update_checksum	(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_release_set_checksum)
void		 fwupd_result_set_update_checksum	(FwupdResult	*result,
							 const gchar	*update_checksum);
G_DEPRECATED_FOR(fwupd_release_get_checksum_kind)
GChecksumType	 fwupd_result_get_update_checksum_kind	(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_release_set_checksum_kind)
void		 fwupd_result_set_update_checksum_kind	(FwupdResult	*result,
							 GChecksumType	 checkum_kind);
G_DEPRECATED_FOR(fwupd_release_get_uri)
const gchar	*fwupd_result_get_update_uri		(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_release_set_uri)
void		 fwupd_result_set_update_uri		(FwupdResult	*result,
							 const gchar	*update_uri);
G_DEPRECATED_FOR(fwupd_release_get_homepage)
const gchar	*fwupd_result_get_update_homepage	(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_release_set_homepage)
void		 fwupd_result_set_update_homepage	(FwupdResult	*result,
							 const gchar	*update_homepage);
G_DEPRECATED_FOR(fwupd_release_get_appstream_id)
const gchar	*fwupd_result_get_update_id		(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_release_set_appstream_id)
void		 fwupd_result_set_update_id		(FwupdResult	*result,
							 const gchar	*update_id);
G_DEPRECATED_FOR(fwupd_release_get_description)
const gchar	*fwupd_result_get_update_description	(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_release_set_description)
void		 fwupd_result_set_update_description	(FwupdResult	*result,
							 const gchar	*update_description);
G_DEPRECATED_FOR(fwupd_release_get_vendor)
const gchar	*fwupd_result_get_update_vendor		(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_release_set_vendor)
void		 fwupd_result_set_update_vendor		(FwupdResult	*result,
							 const gchar	*update_vendor);
G_DEPRECATED_FOR(fwupd_release_get_summary)
const gchar	*fwupd_result_get_update_summary	(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_release_set_summary)
void		 fwupd_result_set_update_summary	(FwupdResult	*result,
							 const gchar	*update_summary);
const gchar	*fwupd_result_get_update_error		(FwupdResult	*result);
void		 fwupd_result_set_update_error		(FwupdResult	*result,
							 const gchar	*update_error);
FwupdTrustFlags	 fwupd_result_get_update_trust_flags	(FwupdResult	*result);
void		 fwupd_result_set_update_trust_flags	(FwupdResult	*result,
							 FwupdTrustFlags trust_flags);
G_DEPRECATED_FOR(fwupd_release_get_license)
const gchar	*fwupd_result_get_update_license	(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_release_set_license)
void		 fwupd_result_set_update_license	(FwupdResult	*result,
							 const gchar	*update_license);
G_DEPRECATED_FOR(fwupd_release_get_name)
const gchar	*fwupd_result_get_update_name		(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_release_set_name)
void		 fwupd_result_set_update_name		(FwupdResult	*result,
							 const gchar	*update_name);

/* helpers */
GVariant	*fwupd_result_to_data			(FwupdResult	*result,
							 const gchar	*type_string);
gchar		*fwupd_result_to_string			(FwupdResult	*result);

/* deprecated */
G_DEPRECATED_FOR(fwupd_result_get_guids)
const gchar	*fwupd_result_get_guid			(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_result_add_guid)
void		 fwupd_result_set_guid			(FwupdResult	*result,
							 const gchar	*guid);

G_END_DECLS

#endif /* __FWUPD_RESULT_H */

