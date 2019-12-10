/*
 * Copyright (C) 2017-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#include "fwupd-remote.h"

#define FU_TYPE_REMOTE_LIST (fu_remote_list_get_type ())
G_DECLARE_FINAL_TYPE (FuRemoteList, fu_remote_list, FU, REMOTE_LIST, GObject)

/**
 * FuRemoteListLoadFlags:
 * @FU_REMOTE_LIST_LOAD_FLAG_NONE:		No flags set
 * @FU_REMOTE_LIST_LOAD_FLAG_READONLY_FS:	Ignore readonly filesystem errors
 *
 * The flags to use when loading a remote_listuration file.
 **/
typedef enum {
	FU_REMOTE_LIST_LOAD_FLAG_NONE		= 0,
	FU_REMOTE_LIST_LOAD_FLAG_READONLY_FS	= 1 << 0,
	/*< private >*/
	FU_REMOTE_LIST_LOAD_FLAG_LAST
} FuRemoteListLoadFlags;

FuRemoteList	*fu_remote_list_new			(void);
gboolean	 fu_remote_list_load			(FuRemoteList	*self,
							 FuRemoteListLoadFlags flags,
							 GError		**error);
gboolean	 fu_remote_list_reload			(FuRemoteList	*self,
							 GError		**error);
gboolean	 fu_remote_list_set_key_value		(FuRemoteList	*self,
							 const gchar	*remote_id,
							 const gchar	*key,
							 const gchar	*value,
							 GError		**error);
GPtrArray	*fu_remote_list_get_all			(FuRemoteList	*self);
FwupdRemote	*fu_remote_list_get_by_id		(FuRemoteList	*self,
							 const gchar	*remote_id);
