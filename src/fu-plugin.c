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

#include "config.h"

#include <gio/gio.h>

#include "fu-device.h"
#include "fu-plugin.h"

/**
 * fu_plugin_new:
 **/
FuPlugin *
fu_plugin_new (GModule *module)
{
	FuPlugin *plugin;
	FuPluginInitFunc func;
	FuPluginGetNameFunc plugin_name = NULL;
	gboolean ret;

	/* get description */
	ret = g_module_symbol (module,
			       "fu_plugin_get_name",
			       (gpointer *) &plugin_name);
	if (!ret)
		return NULL;

	/* add to registered plugins */
	plugin = g_new0 (FuPlugin, 1);
	plugin->enabled = TRUE;
	plugin->module = module;
	plugin->name = g_strdup (plugin_name ());

	/* optional */
	if (g_module_symbol (plugin->module,
			     "fu_plugin_init", (gpointer *) &func)) {
		g_debug ("performing init() on %s", plugin->name);
		func (plugin);
	}
	return plugin;
}

/**
 * fu_plugin_free:
 **/
void
fu_plugin_free (FuPlugin *plugin)
{
	FuPluginInitFunc func;

	/* optional */
	if (g_module_symbol (plugin->module,
			     "fu_plugin_destroy", (gpointer *) &func)) {
		g_debug ("performing destroy() on %s", plugin->name);
		func (plugin);
	}

	/* deallocate */
	g_module_close (plugin->module);
	g_free (plugin->name);
	g_free (plugin->priv);
	g_free (plugin);
}

/**
 * fu_plugin_run_startup:
 **/
gboolean
fu_plugin_run_startup (FuPlugin *plugin, GError **error)
{
	FuPluginStartupFunc func;

	/* not enabled */
	if (!plugin->enabled)
		return TRUE;

	/* optional */
	if (!g_module_symbol (plugin->module,
			      "fu_plugin_startup", (gpointer *) &func))
		return TRUE;
	g_debug ("performing startup() on %s", plugin->name);
	if (!func (plugin, error)) {
		g_prefix_error (error, "failed to startup %s: ", plugin->name);
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_plugin_run_device_probe:
 **/
gboolean
fu_plugin_run_device_probe (FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuPluginDeviceProbeFunc func;

	/* not enabled */
	if (!plugin->enabled)
		return TRUE;

	/* optional */
	if (!g_module_symbol (plugin->module,
			      "fu_plugin_device_probe", (gpointer *) &func))
		return TRUE;
	g_debug ("performing device_probe() on %s", plugin->name);
	if (!func (plugin, device, error)) {
		g_prefix_error (error, "failed to device_probe %s: ", plugin->name);
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_plugin_run_device_update:
 **/
gboolean
fu_plugin_run_device_update (FuPlugin *plugin, FuDevice *device,
			     GBytes *data, GError **error)
{
	FuPluginDeviceUpdateFunc func;

	/* not enabled */
	if (!plugin->enabled)
		return TRUE;

	/* optional */
	if (!g_module_symbol (plugin->module,
			      "fu_plugin_device_update", (gpointer *) &func))
		return TRUE;

	/* does a vendor plugin exist */
	g_debug ("performing device_update() on %s", plugin->name);
	if (!func (plugin, device, data, error)) {
		g_prefix_error (error, "failed to device_update %s: ", plugin->name);
		return FALSE;
	}
	return TRUE;
}
