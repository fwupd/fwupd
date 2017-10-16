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

#ifndef __FWUPD_REMOTE_H
#define __FWUPD_REMOTE_H

#include "fwupd-enums.h"

G_BEGIN_DECLS

#define FWUPD_TYPE_REMOTE (fwupd_remote_get_type ())
G_DECLARE_DERIVABLE_TYPE (FwupdRemote, fwupd_remote, FWUPD, REMOTE, GObject)

struct _FwupdRemoteClass
{
	GObjectClass		 parent_class;
	/*< private >*/
	void (*_fwupd_reserved1)	(void);
	void (*_fwupd_reserved2)	(void);
	void (*_fwupd_reserved3)	(void);
	void (*_fwupd_reserved4)	(void);
	void (*_fwupd_reserved5)	(void);
	void (*_fwupd_reserved6)	(void);
	void (*_fwupd_reserved7)	(void);
};

typedef enum {
	FWUPD_REMOTE_KIND_UNKNOWN,
	FWUPD_REMOTE_KIND_DOWNLOAD,
	FWUPD_REMOTE_KIND_LOCAL,
	/*< private >*/
	FWUPD_REMOTE_KIND_LAST
} FwupdRemoteKind;

FwupdRemoteKind	 fwupd_remote_kind_from_string		(const gchar	*kind);
const gchar	*fwupd_remote_kind_to_string		(FwupdRemoteKind kind);

FwupdRemote	*fwupd_remote_new			(void);
const gchar	*fwupd_remote_get_id			(FwupdRemote	*self);
const gchar	*fwupd_remote_get_title			(FwupdRemote	*self);
const gchar	*fwupd_remote_get_checksum		(FwupdRemote	*self);
const gchar	*fwupd_remote_get_username		(FwupdRemote	*self);
const gchar	*fwupd_remote_get_password		(FwupdRemote	*self);
const gchar	*fwupd_remote_get_filename_cache	(FwupdRemote	*self);
const gchar	*fwupd_remote_get_filename_cache_sig	(FwupdRemote	*self);
const gchar	*fwupd_remote_get_filename_source	(FwupdRemote	*self);
const gchar	*fwupd_remote_get_firmware_base_uri	(FwupdRemote	*self);
const gchar	*fwupd_remote_get_metadata_uri		(FwupdRemote	*self);
const gchar	*fwupd_remote_get_metadata_uri_sig	(FwupdRemote	*self);
gboolean	 fwupd_remote_get_enabled		(FwupdRemote	*self);
gint		 fwupd_remote_get_priority		(FwupdRemote	*self);
guint64		 fwupd_remote_get_age			(FwupdRemote	*self);
FwupdRemoteKind	 fwupd_remote_get_kind			(FwupdRemote	*self);
FwupdKeyringKind fwupd_remote_get_keyring_kind		(FwupdRemote	*self);
gchar		*fwupd_remote_build_firmware_uri	(FwupdRemote	*self,
							 const gchar	*url,
							 GError		**error);

G_END_DECLS

#endif /* __FWUPD_REMOTE_H */

