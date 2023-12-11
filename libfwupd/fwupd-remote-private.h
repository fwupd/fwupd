/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <json-glib/json-glib.h>

#include "fwupd-build.h"
#include "fwupd-remote.h"

G_BEGIN_DECLS

GVariant *
fwupd_remote_to_variant(FwupdRemote *self) G_GNUC_NON_NULL(1);
gboolean
fwupd_remote_load_from_filename(FwupdRemote *self,
				const gchar *filename,
				GCancellable *cancellable,
				GError **error) G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_remote_save_to_filename(FwupdRemote *self,
			      const gchar *filename,
			      GCancellable *cancellable,
			      GError **error) G_GNUC_NON_NULL(1, 2);
G_DEPRECATED_FOR(fwupd_remote_add_flag)
void
fwupd_remote_set_enabled(FwupdRemote *self, gboolean enabled) G_GNUC_NON_NULL(1);
void
fwupd_remote_set_id(FwupdRemote *self, const gchar *id) G_GNUC_NON_NULL(1);
void
fwupd_remote_set_title(FwupdRemote *self, const gchar *title) G_GNUC_NON_NULL(1);
void
fwupd_remote_set_priority(FwupdRemote *self, gint priority) G_GNUC_NON_NULL(1);
void
fwupd_remote_set_agreement(FwupdRemote *self, const gchar *agreement) G_GNUC_NON_NULL(1);
void
fwupd_remote_set_checksum_sig(FwupdRemote *self, const gchar *checksum_sig) G_GNUC_NON_NULL(1);
void
fwupd_remote_set_filename_cache(FwupdRemote *self, const gchar *filename) G_GNUC_NON_NULL(1);
void
fwupd_remote_set_metadata_uri(FwupdRemote *self, const gchar *metadata_uri) G_GNUC_NON_NULL(1);
void
fwupd_remote_set_mtime(FwupdRemote *self, guint64 mtime) G_GNUC_NON_NULL(1);
gchar **
fwupd_remote_get_order_after(FwupdRemote *self) G_GNUC_NON_NULL(1);
gchar **
fwupd_remote_get_order_before(FwupdRemote *self) G_GNUC_NON_NULL(1);

void
fwupd_remote_set_remotes_dir(FwupdRemote *self, const gchar *directory) G_GNUC_NON_NULL(1);
void
fwupd_remote_set_filename_source(FwupdRemote *self, const gchar *filename_source)
    G_GNUC_NON_NULL(1);
void
fwupd_remote_set_keyring_kind(FwupdRemote *self, FwupdKeyringKind keyring_kind) G_GNUC_NON_NULL(1);
gboolean
fwupd_remote_setup(FwupdRemote *self, GError **error) G_GNUC_NON_NULL(1);
gchar *
fwupd_remote_build_metadata_sig_uri(FwupdRemote *self, GError **error) G_GNUC_NON_NULL(1);
gchar *
fwupd_remote_build_metadata_uri(FwupdRemote *self, GError **error) G_GNUC_NON_NULL(1);
void
fwupd_remote_to_json(FwupdRemote *self, JsonBuilder *builder) G_GNUC_NON_NULL(1, 2);

G_END_DECLS
