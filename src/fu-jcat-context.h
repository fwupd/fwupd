/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fwupd-jcat-blob.h"
#include "fwupd-jcat-item.h"

#include "fu-jcat-engine.h"

#define FU_TYPE_JCAT_CONTEXT fu_jcat_context_get_type()

G_DECLARE_FINAL_TYPE(FuJcatContext, fu_jcat_context, FU, JCAT_CONTEXT, GObject)

FuJcatContext *
fu_jcat_context_new(void);
void
fu_jcat_context_add_public_keys(FuJcatContext *self, const gchar *path) G_GNUC_NON_NULL(1, 2);
FuJcatEngine *
fu_jcat_context_get_engine(FuJcatContext *self, FwupdJcatBlobKind kind, GError **error)
    G_GNUC_NON_NULL(1);
void
fu_jcat_context_set_keyring_path(FuJcatContext *self, const gchar *path) G_GNUC_NON_NULL(1);
const gchar *
fu_jcat_context_get_keyring_path(FuJcatContext *self) G_GNUC_NON_NULL(1);
FuJcatResult *
fu_jcat_context_verify_blob(FuJcatContext *self,
			    GBytes *data,
			    FwupdJcatBlob *blob,
			    FuJcatVerifyFlags flags,
			    GError **error) G_GNUC_NON_NULL(1, 2, 3);
GPtrArray *
fu_jcat_context_verify_item(FuJcatContext *self,
			    GBytes *data,
			    FwupdJcatItem *item,
			    FuJcatVerifyFlags flags,
			    GError **error) G_GNUC_NON_NULL(1, 2, 3);
GPtrArray *
fu_jcat_context_verify_target(FuJcatContext *self,
			      FwupdJcatItem *item_target,
			      FwupdJcatItem *item,
			      FuJcatVerifyFlags flags,
			      GError **error) G_GNUC_NON_NULL(1, 2, 3);
void
fu_jcat_context_allow_blob_kind(FuJcatContext *self, FwupdJcatBlobKind kind) G_GNUC_NON_NULL(1);
GPtrArray *
fu_jcat_context_get_public_keys(FuJcatContext *self) G_GNUC_NON_NULL(1);
