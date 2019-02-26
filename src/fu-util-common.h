/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>
#include <fwupd.h>

G_BEGIN_DECLS

/* this is only valid for tools */
#define FWUPD_ERROR_INVALID_ARGS        (FWUPD_ERROR_LAST+1)

void		 fu_util_print_data		(const gchar	*title,
						 const gchar	*msg);
guint		 fu_util_prompt_for_number	(guint		 maxnum);
gboolean	 fu_util_prompt_for_boolean	(gboolean	 def);

gboolean	 fu_util_print_device_tree	(GNode *n, gpointer data);
gboolean	 fu_util_is_interesting_device	(FwupdDevice	*dev);
gchar		*fu_util_get_user_cache_path	(const gchar	*fn);

gchar		*fu_util_get_versions		(void);

gboolean	fu_util_prompt_complete		(FwupdDeviceFlags flags,
						 gboolean prompt,
						 GError **error);

G_END_DECLS
