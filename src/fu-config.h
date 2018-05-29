/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_CONFIG_H
#define __FU_CONFIG_H

G_BEGIN_DECLS

#include <glib-object.h>

#include "fwupd-remote.h"

#define FU_TYPE_CONFIG (fu_config_get_type ())
G_DECLARE_FINAL_TYPE (FuConfig, fu_config, FU, CONFIG, GObject)

FuConfig	*fu_config_new				(void);
gboolean	 fu_config_load				(FuConfig	*self,
							 GError		**error);

guint64		 fu_config_get_archive_size_max		(FuConfig	*self);
GPtrArray	*fu_config_get_blacklist_devices	(FuConfig	*self);
GPtrArray	*fu_config_get_blacklist_plugins	(FuConfig	*self);
GPtrArray	*fu_config_get_remotes			(FuConfig	*self);
FwupdRemote	*fu_config_get_remote_by_id		(FuConfig	*self,
							 const gchar	*remote_id);

G_END_DECLS

#endif /* __FU_CONFIG_H */

