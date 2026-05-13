/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib-object.h>

#include "fwupd-jcat-struct.h"

G_BEGIN_DECLS

#define FWUPD_TYPE_JCAT_BLOB fwupd_jcat_blob_get_type()

G_DECLARE_FINAL_TYPE(FwupdJcatBlob, fwupd_jcat_blob, FWUPD, JCAT_BLOB, GObject)

FwupdJcatBlob *
fwupd_jcat_blob_new(FwupdJcatBlobKind kind,
		    GBytes *data,
		    FwupdJcatBlobFlags flags) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(2);
FwupdJcatBlob *
fwupd_jcat_blob_new_utf8(FwupdJcatBlobKind kind, const gchar *data) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(2);
GBytes *
fwupd_jcat_blob_get_data(FwupdJcatBlob *self) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gchar *
fwupd_jcat_blob_get_data_as_string(FwupdJcatBlob *self) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
FwupdJcatBlobKind
fwupd_jcat_blob_get_kind(FwupdJcatBlob *self) G_GNUC_NON_NULL(1);
FwupdJcatBlobKind
fwupd_jcat_blob_get_target(FwupdJcatBlob *self) G_GNUC_NON_NULL(1);
void
fwupd_jcat_blob_set_target(FwupdJcatBlob *self, FwupdJcatBlobKind target) G_GNUC_NON_NULL(1);
guint64
fwupd_jcat_blob_get_timestamp(FwupdJcatBlob *self) G_GNUC_NON_NULL(1);
void
fwupd_jcat_blob_set_timestamp(FwupdJcatBlob *self, guint64 timestamp) G_GNUC_NON_NULL(1);

G_END_DECLS
