/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <json-glib/json-glib.h>

#include "fwupd-build.h"
#include "fwupd-remote.h"

G_BEGIN_DECLS

void
fwupd_remote_set_kind(FwupdRemote *self, FwupdRemoteKind kind) G_GNUC_NON_NULL(1);
void
fwupd_remote_set_id(FwupdRemote *self, const gchar *id) G_GNUC_NON_NULL(1);
void
fwupd_remote_set_title(FwupdRemote *self, const gchar *title) G_GNUC_NON_NULL(1);
void
fwupd_remote_set_privacy_uri(FwupdRemote *self, const gchar *privacy_uri) G_GNUC_NON_NULL(1);
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
fwupd_remote_set_order_after(FwupdRemote *self, const gchar *ids) G_GNUC_NON_NULL(1);
void
fwupd_remote_set_order_before(FwupdRemote *self, const gchar *ids) G_GNUC_NON_NULL(1);
void
fwupd_remote_set_refresh_interval(FwupdRemote *self, guint64 refresh_interval) G_GNUC_NON_NULL(1);
void
fwupd_remote_set_username(FwupdRemote *self, const gchar *username) G_GNUC_NON_NULL(1);
void
fwupd_remote_set_password(FwupdRemote *self, const gchar *password) G_GNUC_NON_NULL(1);
void
fwupd_remote_set_report_uri(FwupdRemote *self, const gchar *report_uri) G_GNUC_NON_NULL(1);
void
fwupd_remote_set_firmware_base_uri(FwupdRemote *self, const gchar *firmware_base_uri)
    G_GNUC_NON_NULL(1);

void
fwupd_remote_set_remotes_dir(FwupdRemote *self, const gchar *directory) G_GNUC_NON_NULL(1);
void
fwupd_remote_set_filename_source(FwupdRemote *self, const gchar *filename_source)
    G_GNUC_NON_NULL(1);
gboolean
fwupd_remote_setup(FwupdRemote *self, GError **error) G_GNUC_NON_NULL(1);
gchar *
fwupd_remote_build_metadata_sig_uri(FwupdRemote *self, GError **error) G_GNUC_NON_NULL(1);
gchar *
fwupd_remote_build_metadata_uri(FwupdRemote *self, GError **error) G_GNUC_NON_NULL(1);

G_END_DECLS
