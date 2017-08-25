/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
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

#ifndef __FU_PLUGIN_VFUNCS_H
#define __FU_PLUGIN_VFUNCS_H

#include "fu-plugin.h"
#include "fu-device.h"

G_BEGIN_DECLS

void		 fu_plugin_init				(FuPlugin	*plugin);
void		 fu_plugin_destroy			(FuPlugin	*plugin);
gboolean	 fu_plugin_startup			(FuPlugin	*plugin,
							 GError		**error);
gboolean	 fu_plugin_coldplug			(FuPlugin	*plugin,
							 GError		**error);
gboolean	 fu_plugin_coldplug_prepare		(FuPlugin	*plugin,
							 GError		**error);
gboolean	 fu_plugin_coldplug_cleanup		(FuPlugin	*plugin,
							 GError		**error);
gboolean	 fu_plugin_update			(FuPlugin	*plugin,
							 FuDevice	*dev,
							 GBytes		*blob_fw,
							 FwupdInstallFlags flags,
							 GError		**error);
gboolean	 fu_plugin_verify			(FuPlugin	*plugin,
							 FuDevice	*dev,
							 FuPluginVerifyFlags flags,
							 GError		**error);
gboolean	 fu_plugin_unlock			(FuPlugin	*plugin,
							 FuDevice	*dev,
							 GError		**error);
gboolean	 fu_plugin_clear_results		(FuPlugin	*plugin,
							 FuDevice	*dev,
							 GError		**error);
gboolean	 fu_plugin_get_results			(FuPlugin	*plugin,
							 FuDevice	*dev,
							 GError		**error);
gboolean	 fu_plugin_update_prepare		(FuPlugin	*plugin,
							 FuDevice	*dev,
							 GError		**error);
gboolean	 fu_plugin_update_cleanup		(FuPlugin	*plugin,
							 FuDevice	*dev,
							 GError		**error);
void		 fu_plugin_device_registered		(FuPlugin	*plugin,
							 FuDevice	*dev);

G_END_DECLS

#endif /* __FU_PLUGIN_VFUNCS_H */
