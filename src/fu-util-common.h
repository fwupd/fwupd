/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>
#include <fwupd.h>
#include <libsoup/soup.h>

G_BEGIN_DECLS

/* this is only valid for tools */
#define FWUPD_ERROR_INVALID_ARGS        (FWUPD_ERROR_LAST+1)

typedef struct FuUtilPrivate FuUtilPrivate;
typedef gboolean (*FuUtilCmdFunc)		(FuUtilPrivate	*util,
						 gchar		**values,
						 GError		**error);
typedef struct {
	gchar		*name;
	gchar		*arguments;
	gchar		*description;
	FuUtilCmdFunc	 callback;
} FuUtilCmd;

void		 fu_util_print_data		(const gchar	*title,
						 const gchar	*msg);
guint		 fu_util_prompt_for_number	(guint		 maxnum);
gboolean	 fu_util_prompt_for_boolean	(gboolean	 def);

gboolean	 fu_util_print_device_tree	(GNode *n, gpointer data);
gboolean	 fu_util_is_interesting_device	(FwupdDevice	*dev);
gchar		*fu_util_get_user_cache_path	(const gchar	*fn);
SoupSession	*fu_util_setup_networking	(GError		**error);

gchar		*fu_util_get_versions		(void);

gboolean	fu_util_prompt_complete		(FwupdDeviceFlags flags,
						 gboolean prompt,
						 GError **error);
gboolean	fu_util_update_reboot		(GError **error);

GPtrArray	*fu_util_cmd_array_new		(void);
void		 fu_util_cmd_array_add		(GPtrArray	*array,
						 const gchar	*name,
						 const gchar	*arguments,
						 const gchar	*description,
						 FuUtilCmdFunc	 callback);
gchar		*fu_util_cmd_array_to_string	(GPtrArray	*array);
void		 fu_util_cmd_array_sort		(GPtrArray	*array);
gboolean	 fu_util_cmd_array_run		(GPtrArray	*array,
						 FuUtilPrivate	*priv,
						 const gchar	*command,
						 gchar		**values,
						 GError		**error);
gchar		*fu_util_release_get_name	(FwupdRelease	*release);

gboolean	 fu_util_stop_daemon		(GError **error);

G_END_DECLS
