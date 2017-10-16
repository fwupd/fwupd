/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
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

GPtrArray	*fu_config_get_blacklist_devices	(FuConfig	*self);
GPtrArray	*fu_config_get_blacklist_plugins	(FuConfig	*self);
GPtrArray	*fu_config_get_remotes			(FuConfig	*self);
FwupdRemote	*fu_config_get_remote_by_id		(FuConfig	*self,
							 const gchar	*remote_id);

G_END_DECLS

#endif /* __FU_CONFIG_H */

