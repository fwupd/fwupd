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

#include "config.h"

#include <glib-object.h>
#include <gio/gio.h>

#include "fu-config.h"

static void fu_config_finalize	 (GObject *obj);

struct _FuConfig
{
	GObject			 parent_instance;
	GKeyFile		*keyfile;
	GPtrArray		*blacklist_devices;
	GPtrArray		*blacklist_plugins;
	gboolean		 enable_option_rom;
};

G_DEFINE_TYPE (FuConfig, fu_config, G_TYPE_OBJECT)

static const gchar *
fu_config_get_sysconfig_dir (void)
{
	if (g_file_test (SYSCONFDIR, G_FILE_TEST_EXISTS))
		return SYSCONFDIR;
	return "/etc";
}

gboolean
fu_config_load (FuConfig *self, GError **error)
{
	g_autofree gchar *config_file = NULL;
	g_auto(GStrv) devices = NULL;
	g_auto(GStrv) plugins = NULL;

	g_return_val_if_fail (FU_IS_CONFIG (self), FALSE);

	/* load the main daemon config file */
	config_file = g_build_filename (fu_config_get_sysconfig_dir (),
					"fwupd.conf", NULL);
	g_debug ("loading config values from %s", config_file);
	if (!g_key_file_load_from_file (self->keyfile, config_file,
					G_KEY_FILE_NONE, error))
		return FALSE;

	/* optional, at the moment */
	self->enable_option_rom =
		g_key_file_get_boolean (self->keyfile,
					"fwupd",
					"EnableOptionROM",
					NULL);

	/* get blacklisted devices */
	devices = g_key_file_get_string_list (self->keyfile,
					      "fwupd",
					      "BlacklistDevices",
					      NULL, /* length */
					      NULL);
	if (devices != NULL) {
		for (guint i = 0; devices[i] != NULL; i++) {
			g_ptr_array_add (self->blacklist_devices,
					 g_strdup (devices[i]));
		}
	}

	/* get blacklisted plugins */
	plugins = g_key_file_get_string_list (self->keyfile,
					      "fwupd",
					      "BlacklistPlugins",
					      NULL, /* length */
					      NULL);
	if (plugins != NULL) {
		for (guint i = 0; plugins[i] != NULL; i++) {
			g_ptr_array_add (self->blacklist_plugins,
					 g_strdup (plugins[i]));
		}
	}

	return TRUE;
}

GPtrArray *
fu_config_get_blacklist_devices (FuConfig *self)
{
	g_return_val_if_fail (FU_IS_CONFIG (self), NULL);
	return self->blacklist_devices;
}

GPtrArray *
fu_config_get_blacklist_plugins (FuConfig *self)
{
	g_return_val_if_fail (FU_IS_CONFIG (self), NULL);
	return self->blacklist_plugins;
}

gboolean
fu_config_get_enable_option_rom (FuConfig *self)
{
	g_return_val_if_fail (FU_IS_CONFIG (self), FALSE);
	return self->enable_option_rom;
}

static void
fu_config_class_init (FuConfigClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_config_finalize;
}

static void
fu_config_init (FuConfig *self)
{
	self->keyfile = g_key_file_new ();
	self->blacklist_devices = g_ptr_array_new_with_free_func (g_free);
	self->blacklist_plugins = g_ptr_array_new_with_free_func (g_free);
}

static void
fu_config_finalize (GObject *obj)
{
	FuConfig *self = FU_CONFIG (obj);

	g_key_file_unref (self->keyfile);
	g_ptr_array_unref (self->blacklist_devices);
	g_ptr_array_unref (self->blacklist_plugins);

	G_OBJECT_CLASS (fu_config_parent_class)->finalize (obj);
}

FuConfig *
fu_config_new (void)
{
	FuConfig *self;
	self = g_object_new (FU_TYPE_CONFIG, NULL);
	return FU_CONFIG (self);
}
