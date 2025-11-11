/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_REMOTE_LIST (fu_remote_list_get_type())
G_DECLARE_FINAL_TYPE(FuRemoteList, fu_remote_list, FU, REMOTE_LIST, GObject)

/**
 * FuRemoteListLoadFlags:
 * @FU_REMOTE_LIST_LOAD_FLAG_NONE:		No flags set
 * @FU_REMOTE_LIST_LOAD_FLAG_READONLY_FS:	Ignore readonly filesystem errors
 * @FU_REMOTE_LIST_LOAD_FLAG_NO_CACHE:		Do not save persistent xmlb silos
 * @FU_REMOTE_LIST_LOAD_FLAG_TEST_REMOTE:		Enable test mode remotes
 * @FU_REMOTE_LIST_LOAD_FLAG_FIX_METADATA_URI:	Auto-fix to use the newest supported metadata
 *
 * The flags to use when loading a remote_listuration file.
 **/
typedef enum {
	FU_REMOTE_LIST_LOAD_FLAG_NONE = 0,
	FU_REMOTE_LIST_LOAD_FLAG_READONLY_FS = 1 << 0,
	FU_REMOTE_LIST_LOAD_FLAG_NO_CACHE = 1 << 1,
	FU_REMOTE_LIST_LOAD_FLAG_TEST_REMOTE = 1 << 2,
	FU_REMOTE_LIST_LOAD_FLAG_FIX_METADATA_URI = 1 << 3,
	/*< private >*/
	FU_REMOTE_LIST_LOAD_FLAG_LAST
} FuRemoteListLoadFlags;

FuRemoteList *
fu_remote_list_new(void);
gboolean
fu_remote_list_set_testing_remote_enabled(FuRemoteList *self, gboolean enable, GError **error)
    G_GNUC_NON_NULL(1);
gboolean
fu_remote_list_load(FuRemoteList *self, FuRemoteListLoadFlags flags, GError **error)
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
