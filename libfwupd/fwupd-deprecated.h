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

#ifndef __FWUPD_DEPRECATED_H
#define __FWUPD_DEPRECATED_H

#include "fwupd-client.h"
#include "fwupd-enums.h"
#include "fwupd-remote.h"

#define FU_DEVICE_FLAG_NONE		FWUPD_DEVICE_FLAG_NONE
#define FU_DEVICE_FLAG_INTERNAL		FWUPD_DEVICE_FLAG_INTERNAL
#define FU_DEVICE_FLAG_ALLOW_ONLINE	FWUPD_DEVICE_FLAG_UPDATABLE
#define FU_DEVICE_FLAG_ALLOW_OFFLINE	FWUPD_DEVICE_FLAG_ONLY_OFFLINE
#define FU_DEVICE_FLAG_REQUIRE_AC	FWUPD_DEVICE_FLAG_REQUIRE_AC
#define FU_DEVICE_FLAG_LOCKED		FWUPD_DEVICE_FLAG_LOCKED
#define FU_DEVICE_FLAG_SUPPORTED	FWUPD_DEVICE_FLAG_SUPPORTED
#define FU_DEVICE_FLAG_NEEDS_BOOTLOADER	FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER
#define FU_DEVICE_FLAG_UNKNOWN		FWUPD_DEVICE_FLAG_UNKNOWN

#define FWUPD_DEVICE_FLAG_ALLOW_ONLINE	FWUPD_DEVICE_FLAG_UPDATABLE
#define FWUPD_DEVICE_FLAG_ALLOW_OFFLINE	FWUPD_DEVICE_FLAG_ONLY_OFFLINE

G_DEPRECATED_FOR(fwupd_client_get_devices_simple)
GPtrArray	*fwupd_client_get_devices		(FwupdClient	*client,
							 GCancellable	*cancellable,
							 GError		**error);
G_DEPRECATED_FOR(fwupd_client_get_details_local)
FwupdResult	*fwupd_client_get_details		(FwupdClient	*client,
							 const gchar	*filename,
							 GCancellable	*cancellable,
							 GError		**error);
G_DEPRECATED_FOR(fwupd_client_update_metadata_with_id)
gboolean	 fwupd_client_update_metadata		(FwupdClient	*client,
							 const gchar	*metadata_fn,
							 const gchar	*signature_fn,
							 GCancellable	*cancellable,
							 GError		**error);

SoupURI		*fwupd_remote_get_uri			(FwupdRemote	*self)
G_DEPRECATED_FOR(fwupd_remote_get_metadata_uri);
SoupURI		*fwupd_remote_get_uri_asc		(FwupdRemote	*self)
G_DEPRECATED_FOR(fwupd_remote_get_metadata_uri_sig);


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
G_DEPRECATED_FOR(fwupd_device_get_checksums)
const gchar	*fwupd_result_get_device_checksum	(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_device_add_checksum)
void		 fwupd_result_set_device_checksum	(FwupdResult	*result,
							 const gchar	*device_checksum);
G_DEPRECATED_FOR(fwupd_device_get_checksums)
GChecksumType	 fwupd_result_get_device_checksum_kind	(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_device_add_checksum)
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
G_DEPRECATED_FOR(fwupd_device_get_update_state)
FwupdUpdateState fwupd_result_get_update_state		(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_device_set_update_state)
void		 fwupd_result_set_update_state		(FwupdResult	*result,
							 FwupdUpdateState update_state);
G_DEPRECATED_FOR(fwupd_device_get_update_error)
const gchar	*fwupd_result_get_update_error		(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_device_set_update_error)
void		 fwupd_result_set_update_error		(FwupdResult	*result,
							 const gchar	*update_error);

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
G_DEPRECATED_FOR(fwupd_release_get_checksums)
const gchar	*fwupd_result_get_update_checksum	(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_release_add_checksum)
void		 fwupd_result_set_update_checksum	(FwupdResult	*result,
							 const gchar	*update_checksum);
G_DEPRECATED_FOR(fwupd_release_get_checksums)
GChecksumType	 fwupd_result_get_update_checksum_kind	(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_release_add_checksum)
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
G_DEPRECATED_FOR(fwupd_result_get_guids)
const gchar	*fwupd_result_get_guid			(FwupdResult	*result);
G_DEPRECATED_FOR(fwupd_result_add_guid)
void		 fwupd_result_set_guid			(FwupdResult	*result,
							 const gchar	*guid);

#endif /* __FWUPD_DEPRECATED_H */
