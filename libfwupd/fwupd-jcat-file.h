/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-jcat-item.h"

G_BEGIN_DECLS

#define FWUPD_TYPE_JCAT_FILE (fwupd_jcat_file_get_type())

G_DECLARE_FINAL_TYPE(FwupdJcatFile, fwupd_jcat_file, FWUPD, JCAT_FILE, GObject)

FwupdJcatFile *
fwupd_jcat_file_new(void) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fwupd_jcat_file_import_stream(FwupdJcatFile *self,
			      GInputStream *istream,
			      GError **error) G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_jcat_file_import_bytes(FwupdJcatFile *self,
			     GBytes *blob,
			     GError **error) G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_jcat_file_import_json(FwupdJcatFile *self,
			    const gchar *json,
			    GError **error) G_GNUC_NON_NULL(1, 2);
GBytes *
fwupd_jcat_file_export_bytes(FwupdJcatFile *self, GError **error) G_GNUC_NON_NULL(1);
gchar *
fwupd_jcat_file_export_json(FwupdJcatFile *self, FwupdCodecFlags flags, GError **error)
    G_GNUC_NON_NULL(1);
GPtrArray *
fwupd_jcat_file_get_items(FwupdJcatFile *self) G_GNUC_NON_NULL(1);
FwupdJcatItem *
fwupd_jcat_file_get_item_by_id(FwupdJcatFile *self, const gchar *id, GError **error)
    G_GNUC_NON_NULL(1, 2);
FwupdJcatItem *
fwupd_jcat_file_get_item_default(FwupdJcatFile *self, GError **error) G_GNUC_NON_NULL(1);
void
fwupd_jcat_file_add_item(FwupdJcatFile *self, FwupdJcatItem *item) G_GNUC_NON_NULL(1, 2);
guint32
fwupd_jcat_file_get_version_major(FwupdJcatFile *self) G_GNUC_NON_NULL(1);
guint32
fwupd_jcat_file_get_version_minor(FwupdJcatFile *self) G_GNUC_NON_NULL(1);

G_END_DECLS
