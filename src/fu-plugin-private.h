/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __FU_PLUGIN_PRIVATE_H
#define __FU_PLUGIN_PRIVATE_H

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_OFFLINE_TRIGGER_FILENAME	FU_OFFLINE_DESTDIR "/system-update"

FuPlugin	*fu_plugin_new				(void);
void		 fu_plugin_set_usb_context		(FuPlugin	*plugin,
							 GUsbContext	*usb_ctx);
void		 fu_plugin_set_hwids			(FuPlugin	*plugin,
							 GHashTable	*hwids);
gboolean	 fu_plugin_open				(FuPlugin	*plugin,
							 const gchar	*filename,
							 GError		**error);
gboolean	 fu_plugin_runner_startup		(FuPlugin	*plugin,
							 GError		**error);
gboolean	 fu_plugin_runner_coldplug		(FuPlugin	*plugin,
							 GError		**error);
gboolean	 fu_plugin_runner_coldplug_prepare	(FuPlugin	*plugin,
							 GError		**error);
gboolean	 fu_plugin_runner_coldplug_cleanup	(FuPlugin	*plugin,
							 GError		**error);
gboolean	 fu_plugin_runner_update_prepare	(FuPlugin	*plugin,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_plugin_runner_update_cleanup	(FuPlugin	*plugin,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_plugin_runner_update		(FuPlugin	*plugin,
							 FuDevice	*device,
							 GBytes		*blob_cab,
							 GBytes		*blob_fw,
							 FwupdInstallFlags flags,
							 GError		**error);
gboolean	 fu_plugin_runner_verify		(FuPlugin	*plugin,
							 FuDevice	*device,
							 FuPluginVerifyFlags flags,
							 GError		**error);
gboolean	 fu_plugin_runner_unlock		(FuPlugin	*plugin,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_plugin_runner_clear_results		(FuPlugin	*plugin,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_plugin_runner_get_results		(FuPlugin	*plugin,
							 FuDevice	*device,
							 GError		**error);

G_END_DECLS

#endif /* __FU_PLUGIN_PRIVATE_H */
