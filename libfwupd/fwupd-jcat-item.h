/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-jcat-blob.h"

G_BEGIN_DECLS

#define FWUPD_TYPE_JCAT_ITEM (fwupd_jcat_item_get_type())

G_DECLARE_FINAL_TYPE(FwupdJcatItem, fwupd_jcat_item, FWUPD, JCAT_ITEM, GObject)

FwupdJcatItem *
fwupd_jcat_item_new(const gchar *id);
GPtrArray *
fwupd_jcat_item_get_blobs(FwupdJcatItem *self) G_GNUC_NON_NULL(1);
GPtrArray *
fwupd_jcat_item_get_blobs_by_kind(FwupdJcatItem *self, FwupdJcatBlobKind kind) G_GNUC_NON_NULL(1);
FwupdJcatBlob *
fwupd_jcat_item_get_blob_by_kind(FwupdJcatItem *self, FwupdJcatBlobKind kind, GError **error)
    G_GNUC_NON_NULL(1);
void
fwupd_jcat_item_add_blob(FwupdJcatItem *self, FwupdJcatBlob *blob) G_GNUC_NON_NULL(1, 2);
const gchar *
fwupd_jcat_item_get_id(FwupdJcatItem *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_jcat_item_get_id_safe(FwupdJcatItem *self, GError **error) G_GNUC_NON_NULL(1);
void
fwupd_jcat_item_add_alias_id(FwupdJcatItem *self, const gchar *id) G_GNUC_NON_NULL(1, 2);
void
fwupd_jcat_item_remove_alias_id(FwupdJcatItem *self, const gchar *id) G_GNUC_NON_NULL(1, 2);
GPtrArray *
fwupd_jcat_item_get_alias_ids(FwupdJcatItem *self) G_GNUC_NON_NULL(1);
gboolean
fwupd_jcat_item_has_target(FwupdJcatItem *self) G_GNUC_NON_NULL(1);

G_END_DECLS
