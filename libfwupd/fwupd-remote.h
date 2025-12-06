/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib-object.h>

#include "fwupd-remote-struct.h"

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

FwupdRemote *
fwupd_remote_new(void);
const gchar *
fwupd_remote_get_id(FwupdRemote *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_remote_get_title(FwupdRemote *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_remote_get_privacy_uri(FwupdRemote *self) G_GNUC_NON_NULL(1);
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

G_END_DECLS
