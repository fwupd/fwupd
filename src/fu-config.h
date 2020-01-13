/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#include "fwupd-remote.h"

#define FU_TYPE_CONFIG (fu_config_get_type ())
G_DECLARE_FINAL_TYPE (FuConfig, fu_config, FU, CONFIG, GObject)

FuConfig	*fu_config_new				(void);
gboolean	 fu_config_load				(FuConfig	*self,
							 GError		**error);
gboolean	 fu_config_set_key_value		(FuConfig	*self,
							 const gchar	*key,
							 const gchar	*value,
							 GError		**error);

guint64		 fu_config_get_archive_size_max		(FuConfig	*self);
guint		 fu_config_get_idle_timeout		(FuConfig	*self);
GPtrArray	*fu_config_get_blacklist_devices	(FuConfig	*self);
GPtrArray	*fu_config_get_blacklist_plugins	(FuConfig	*self);
GPtrArray	*fu_config_get_approved_firmware	(FuConfig	*self);
gboolean	 fu_config_get_update_motd		(FuConfig	*self);
