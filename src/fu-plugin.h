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

#ifndef __FU_PLUGIN_H
#define __FU_PLUGIN_H

#include <glib-object.h>
#include <gmodule.h>
#include <gusb.h>

#include "fu-device.h"

G_BEGIN_DECLS

typedef struct	FuPluginPrivate	FuPluginPrivate;
typedef struct	FuPlugin	FuPlugin;

struct FuPlugin {
	GModule			*module;
	GUsbContext		*usb_ctx;
	gboolean		 enabled;
	gchar			*name;
	FuPluginPrivate		*priv;
};

#define	FU_PLUGIN_GET_PRIVATE(x)			g_new0 (x,1)
#define	FU_PLUGIN(x)					((FuPlugin *) x);

typedef const gchar	*(*FuPluginGetNameFunc)		(void);
typedef void		 (*FuPluginInitFunc)		(FuPlugin	*plugin);
typedef gboolean	 (*FuPluginStartupFunc)		(FuPlugin	*plugin,
							 GError		**error);
typedef gboolean	 (*FuPluginDeviceProbeFunc)	(FuPlugin	*plugin,
							 FuDevice	*device,
							 GError		**error);
typedef gboolean	 (*FuPluginDeviceUpdateFunc)	(FuPlugin	*plugin,
							 FuDevice	*device,
							 GBytes		*data,
							 GError		**error);

/* these are implemented by the plugin */
const gchar	*fu_plugin_get_name			(void);
void		 fu_plugin_init				(FuPlugin	*plugin);
void		 fu_plugin_destroy			(FuPlugin	*plugin);
gboolean	 fu_plugin_startup			(FuPlugin	*plugin,
							 GError		**error);
gboolean	 fu_plugin_device_probe			(FuPlugin	*plugin,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_plugin_device_update		(FuPlugin	*plugin,
							 FuDevice	*device,
							 GBytes		*data,
							 GError		**error);

/* these are called from the daemon */
FuPlugin	*fu_plugin_new				(GModule	*module);
void		 fu_plugin_free				(FuPlugin	*plugin);
gboolean	 fu_plugin_run_startup			(FuPlugin	*plugin,
							 GError		**error);
gboolean	 fu_plugin_run_device_probe		(FuPlugin	*plugin,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_plugin_run_device_update		(FuPlugin	*plugin,
							 FuDevice	*device,
							 GBytes		*data,
							 GError		**error);
GUsbContext	*fu_plugin_get_usb_context		(FuPlugin	*plugin);
void		 fu_plugin_set_usb_context		(FuPlugin	*plugin,
							 GUsbContext	*usb_ctx);

G_END_DECLS

#endif /* __FU_PLUGIN_H */
