/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <json-glib/json-glib.h>

#include "fwupd-remote.h"

G_BEGIN_DECLS

GVariant *
fwupd_remote_to_variant(FwupdRemote *self);
gboolean
fwupd_remote_load_from_filename(FwupdRemote *self,
				const gchar *filename,
				GCancellable *cancellable,
				GError **error);
void
fwupd_remote_set_priority(FwupdRemote *self, gint priority);
void
fwupd_remote_set_agreement(FwupdRemote *self, const gchar *agreement);
void
fwupd_remote_set_checksum(FwupdRemote *self, const gchar *checksum);
void
fwupd_remote_set_filename_cache(FwupdRemote *self, const gchar *filename);
void
fwupd_remote_set_mtime(FwupdRemote *self, guint64 mtime);
gchar **
fwupd_remote_get_order_after(FwupdRemote *self);
gchar **
fwupd_remote_get_order_before(FwupdRemote *self);

void
fwupd_remote_set_remotes_dir(FwupdRemote *self, const gchar *directory);
void
fwupd_remote_set_filename_source(FwupdRemote *self, const gchar *filename_source);
void
fwupd_remote_set_keyring_kind(FwupdRemote *self, FwupdKeyringKind keyring_kind);
gboolean
fwupd_remote_setup(FwupdRemote *self, GError **error);
void
fwupd_remote_to_json(FwupdRemote *self, JsonBuilder *builder);

G_END_DECLS
