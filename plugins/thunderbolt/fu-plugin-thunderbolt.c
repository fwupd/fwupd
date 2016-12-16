/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Intel Corporation <thunderbolt-software@lists.01.org>
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

#include <appstream-glib.h>
#include <gudev/gudev.h>
#include <tbt/tbt_fwu.h>

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

struct FuPluginData {
	/* A handle on some state for dealing with our registration
	 * for udev events.
	 */
	GUdevClient				*gudev_client;
};

void
fu_plugin_init (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	const gchar* subsystems[] = { "pci", NULL };
	data->gudev_client = g_udev_client_new (subsystems);
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	tbt_fwu_shutdown ();
	g_object_unref (data->gudev_client);
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	gint rc = tbt_fwu_init ();
	if (rc != TBT_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "TBT initialization failed: %s",
			     tbt_strerror (rc));
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	//FuPluginData *data = fu_plugin_get_data (plugin);
	return TRUE;
}
