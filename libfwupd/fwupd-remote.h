/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

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

/**
 * FwupdRemoteKind:
 * @FWUPD_REMOTE_KIND_UNKNOWN:			Unknown kind
 * @FWUPD_REMOTE_KIND_DOWNLOAD:			Requires files to be downloaded
 * @FWUPD_REMOTE_KIND_LOCAL:			Reads files from the local machine
 * @FWUPD_REMOTE_KIND_DIRECTORY:		Reads directory from the local machine
 *
 * The kind of remote.
 **/
typedef enum {
	FWUPD_REMOTE_KIND_UNKNOWN,
	FWUPD_REMOTE_KIND_DOWNLOAD,
	FWUPD_REMOTE_KIND_LOCAL,
	FWUPD_REMOTE_KIND_DIRECTORY,			/* Since: 1.2.4 */
	/*< private >*/
	FWUPD_REMOTE_KIND_LAST
} FwupdRemoteKind;

FwupdRemoteKind	 fwupd_remote_kind_from_string		(const gchar	*kind);
const gchar	*fwupd_remote_kind_to_string		(FwupdRemoteKind kind);

FwupdRemote	*fwupd_remote_new			(void);
const gchar	*fwupd_remote_get_id			(FwupdRemote	*self);
const gchar	*fwupd_remote_get_title			(FwupdRemote	*self);
const gchar	*fwupd_remote_get_agreement		(FwupdRemote	*self);
const gchar	*fwupd_remote_get_remotes_dir		(FwupdRemote	*self);
const gchar	*fwupd_remote_get_checksum		(FwupdRemote	*self);
const gchar	*fwupd_remote_get_username		(FwupdRemote	*self);
const gchar	*fwupd_remote_get_password		(FwupdRemote	*self);
const gchar	*fwupd_remote_get_filename_cache	(FwupdRemote	*self);
const gchar	*fwupd_remote_get_filename_cache_sig	(FwupdRemote	*self);
const gchar	*fwupd_remote_get_filename_source	(FwupdRemote	*self);
const gchar	*fwupd_remote_get_firmware_base_uri	(FwupdRemote	*self);
const gchar	*fwupd_remote_get_report_uri		(FwupdRemote	*self);
const gchar	*fwupd_remote_get_metadata_uri		(FwupdRemote	*self);
const gchar	*fwupd_remote_get_metadata_uri_sig	(FwupdRemote	*self);
gboolean	 fwupd_remote_get_enabled		(FwupdRemote	*self);
gboolean	 fwupd_remote_get_approval_required	(FwupdRemote	*self);
gboolean	 fwupd_remote_get_automatic_reports	(FwupdRemote	*self);
gint		 fwupd_remote_get_priority		(FwupdRemote	*self);
guint64		 fwupd_remote_get_age			(FwupdRemote	*self);
FwupdRemoteKind	 fwupd_remote_get_kind			(FwupdRemote	*self);
FwupdKeyringKind fwupd_remote_get_keyring_kind		(FwupdRemote	*self);
gchar		*fwupd_remote_build_firmware_uri	(FwupdRemote	*self,
							 const gchar	*url,
							 GError		**error);

FwupdRemote	*fwupd_remote_from_variant		(GVariant	*value);
GPtrArray	*fwupd_remote_array_from_variant	(GVariant	*value);

G_END_DECLS
