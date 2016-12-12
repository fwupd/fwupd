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

#include "fu-plugin.h"

struct FuPluginPrivate {
	GMutex			 mutex;
};

const gchar *
fu_plugin_get_name (void)
{
	return "test";
}

void
fu_plugin_init (FuPlugin *plugin)
{
	plugin->priv = FU_PLUGIN_GET_PRIVATE (FuPluginPrivate);

	/* only enable when testing */
	if (g_getenv ("FWUPD_ENABLE_TEST_PLUGIN") == NULL) {
		plugin->enabled = FALSE;
		return;
	}
	g_debug ("init");
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	g_debug ("destroy");
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	g_debug ("startup");
	return TRUE;
}
