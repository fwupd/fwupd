/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#include "fwupd-remote.h"

G_BEGIN_DECLS

#define FU_TYPE_CONFIG (fu_config_get_type ())
G_DECLARE_FINAL_TYPE (FuConfig, fu_config, FU, CONFIG, GObject)

typedef enum {
	FU_CONFIG_LOAD_FLAG_NONE		= 0,
	FU_CONFIG_LOAD_FLAG_READONLY_FS		= 1 << 0,
	FU_CONFIG_LOAD_FLAG_LAST
} FuConfigLoadFlags;

FuConfig	*fu_config_new				(void);
gboolean	 fu_config_load				(FuConfig	*self,
							 FuConfigLoadFlags flags,
							 GError		**error);
gboolean	 fu_config_modify_and_save		(FuConfig	*self,
							 const gchar	*key,
							 const gchar	*value,
							 GError		**error);

guint64		 fu_config_get_archive_size_max		(FuConfig	*self);
guint		 fu_config_get_idle_timeout		(FuConfig	*self);
GPtrArray	*fu_config_get_blacklist_devices	(FuConfig	*self);
GPtrArray	*fu_config_get_blacklist_plugins	(FuConfig	*self);
GPtrArray	*fu_config_get_approved_firmware	(FuConfig	*self);
GPtrArray	*fu_config_get_remotes			(FuConfig	*self);
FwupdRemote	*fu_config_get_remote_by_id		(FuConfig	*self,
							 const gchar	*remote_id);

G_END_DECLS
