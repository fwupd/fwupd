/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_REMOTE_LIST (fu_remote_list_get_type())
G_DECLARE_FINAL_TYPE(FuRemoteList, fu_remote_list, FU, REMOTE_LIST, GObject)

FuRemoteList *
fu_remote_list_new(FuContext *ctx);
gboolean
fu_remote_list_set_testing_remote_enabled(FuRemoteList *self, gboolean enable, GError **error)
    G_GNUC_NON_NULL(1);
gboolean
fu_remote_list_load(FuRemoteList *self, FuContextLoadFlags flags, GError **error)
    G_GNUC_NON_NULL(1);
gboolean
fu_remote_list_set_key_value(FuRemoteList *self,
			     const gchar *remote_id,
			     const gchar *key,
			     const gchar *value,
			     GError **error) G_GNUC_NON_NULL(1, 2, 3, 4);
GPtrArray *
fu_remote_list_get_all(FuRemoteList *self) G_GNUC_NON_NULL(1);
FwupdRemote *
fu_remote_list_get_by_id(FuRemoteList *self, const gchar *remote_id, GError **error)
    G_GNUC_NON_NULL(1, 2);
void
fu_remote_list_set_lvfs_metadata_format(FuRemoteList *self, const gchar *lvfs_metadata_format);

/* for the self tests */
void
fu_remote_list_add_remote(FuRemoteList *self, FwupdRemote *remote) G_GNUC_NON_NULL(1, 2);
