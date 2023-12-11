/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#include "fwupd-enums.h"

G_BEGIN_DECLS

#define FWUPD_TYPE_REMOTE (fwupd_remote_get_type())
G_DECLARE_DERIVABLE_TYPE(FwupdRemote, fwupd_remote, FWUPD, REMOTE, GObject)

struct _FwupdRemoteClass {
	GObjectClass parent_class;
	/*< private >*/
	void (*_fwupd_reserved1)(void);
	void (*_fwupd_reserved2)(void);
	void (*_fwupd_reserved3)(void);
	void (*_fwupd_reserved4)(void);
	void (*_fwupd_reserved5)(void);
	void (*_fwupd_reserved6)(void);
	void (*_fwupd_reserved7)(void);
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
	FWUPD_REMOTE_KIND_DIRECTORY, /* Since: 1.2.4 */
	/*< private >*/
	FWUPD_REMOTE_KIND_LAST
} FwupdRemoteKind;

/**
 * FwupdRemoteFlags:
 * @FWUPD_REMOTE_FLAG_NONE:				No flags set
 * @FWUPD_REMOTE_FLAG_ENABLED:				Is enabled
 * @FWUPD_REMOTE_FLAG_APPROVAL_REQUIRED:		Requires approval for each firmware
 * @FWUPD_REMOTE_FLAG_AUTOMATIC_REPORTS:		Send firmware reports automatically
 * @FWUPD_REMOTE_FLAG_AUTOMATIC_SECURITY_REPORTS:	Send security reports automatically
 * @FWUPD_REMOTE_FLAG_ALLOW_P2P_METADATA:		Use peer-to-peer locations for metadata
 * @FWUPD_REMOTE_FLAG_ALLOW_P2P_FIRMWARE:		Use peer-to-peer locations for firmware
 *
 * The flags available for the remote.
 **/
typedef enum {
	FWUPD_REMOTE_FLAG_NONE = 0,			       /* Since: 1.9.4 */
	FWUPD_REMOTE_FLAG_ENABLED = 1 << 0,		       /* Since: 1.9.4 */
	FWUPD_REMOTE_FLAG_APPROVAL_REQUIRED = 1 << 1,	       /* Since: 1.9.4 */
	FWUPD_REMOTE_FLAG_AUTOMATIC_REPORTS = 1 << 2,	       /* Since: 1.9.4 */
	FWUPD_REMOTE_FLAG_AUTOMATIC_SECURITY_REPORTS = 1 << 3, /* Since: 1.9.4 */
	FWUPD_REMOTE_FLAG_ALLOW_P2P_METADATA = 1 << 4,	       /* Since: 1.9.5 */
	FWUPD_REMOTE_FLAG_ALLOW_P2P_FIRMWARE = 1 << 5,	       /* Since: 1.9.5 */
} FwupdRemoteFlags;

FwupdRemoteKind
fwupd_remote_kind_from_string(const gchar *kind);
const gchar *
fwupd_remote_kind_to_string(FwupdRemoteKind kind);
const gchar *
fwupd_remote_flag_to_string(FwupdRemoteFlags flag);
FwupdRemoteFlags
fwupd_remote_flag_from_string(const gchar *flag);

FwupdRemote *
fwupd_remote_new(void);
const gchar *
fwupd_remote_get_id(FwupdRemote *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_remote_get_title(FwupdRemote *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_remote_get_agreement(FwupdRemote *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_remote_get_remotes_dir(FwupdRemote *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_remote_get_checksum(FwupdRemote *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_remote_get_checksum_metadata(FwupdRemote *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_remote_get_username(FwupdRemote *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_remote_get_password(FwupdRemote *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_remote_get_filename_cache(FwupdRemote *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_remote_get_filename_cache_sig(FwupdRemote *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_remote_get_filename_source(FwupdRemote *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_remote_get_firmware_base_uri(FwupdRemote *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_remote_get_report_uri(FwupdRemote *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_remote_get_security_report_uri(FwupdRemote *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_remote_get_metadata_uri(FwupdRemote *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_remote_get_metadata_uri_sig(FwupdRemote *self) G_GNUC_NON_NULL(1);
guint64
fwupd_remote_get_refresh_interval(FwupdRemote *self) G_GNUC_NON_NULL(1);

FwupdRemoteFlags
fwupd_remote_get_flags(FwupdRemote *self) G_GNUC_NON_NULL(1);
void
fwupd_remote_set_flags(FwupdRemote *self, FwupdRemoteFlags flags) G_GNUC_NON_NULL(1);
void
fwupd_remote_add_flag(FwupdRemote *self, FwupdRemoteFlags flag) G_GNUC_NON_NULL(1);
void
fwupd_remote_remove_flag(FwupdRemote *self, FwupdRemoteFlags flag) G_GNUC_NON_NULL(1);
gboolean
fwupd_remote_has_flag(FwupdRemote *self, FwupdRemoteFlags flag) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);

gboolean
fwupd_remote_needs_refresh(FwupdRemote *self) G_GNUC_NON_NULL(1);
gint
fwupd_remote_get_priority(FwupdRemote *self) G_GNUC_NON_NULL(1);
guint64
fwupd_remote_get_age(FwupdRemote *self) G_GNUC_NON_NULL(1);
FwupdRemoteKind
fwupd_remote_get_kind(FwupdRemote *self) G_GNUC_NON_NULL(1);
FwupdKeyringKind
fwupd_remote_get_keyring_kind(FwupdRemote *self) G_GNUC_NON_NULL(1);
gchar *
fwupd_remote_build_firmware_uri(FwupdRemote *self,
				const gchar *url,
				GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gchar *
fwupd_remote_build_report_uri(FwupdRemote *self, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
gboolean
fwupd_remote_load_signature(FwupdRemote *self,
			    const gchar *filename,
			    GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fwupd_remote_load_signature_bytes(FwupdRemote *self,
				  GBytes *bytes,
				  GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);

FwupdRemote *
fwupd_remote_from_variant(GVariant *value) G_GNUC_NON_NULL(1);
GPtrArray *
fwupd_remote_array_from_variant(GVariant *value) G_GNUC_NON_NULL(1);

G_END_DECLS
