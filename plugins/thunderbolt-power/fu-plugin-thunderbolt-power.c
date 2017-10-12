/* -*- mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Dell Inc.
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

#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gudev/gudev.h>

#include "fu-plugin-vfuncs.h"
#include "fu-device-metadata.h"

#ifndef HAVE_GUDEV_232
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GUdevDevice, g_object_unref)
#endif

/* empirically measured amount of time for the TBT device to come and go */
#define TBT_NEW_DEVICE_TIMEOUT	2 /* s */

struct FuPluginData {
	GUdevClient   *udev;
	gchar         *force_path;
	gboolean       needs_forcepower;
	guint          timeout_id;
};

static void
fu_plugin_thunderbolt_power_get_path (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autoptr(GList) devices = NULL;
	const gchar *basepath;
	const gchar *driver;

	/* in case driver went away */
	if (data->force_path != NULL) {
		g_free (data->force_path);
		data->force_path = NULL;
	}

	devices = g_udev_client_query_by_subsystem (data->udev, "wmi");
	for (GList* l = devices; l != NULL; l = l->next) {
		g_autofree gchar *built_path = NULL;
		GUdevDevice *device = l->data;

		/* only supports intel-wmi-thunderbolt for now */
		driver = g_udev_device_get_driver (device);
		if (g_strcmp0 (driver, "intel-wmi-thunderbolt") != 0)
			continue;

		/* check for the attribute to be loaded */
		basepath = g_udev_device_get_sysfs_path (device);
		if (basepath == NULL)
			continue;
		built_path = g_build_path ("/", basepath,
					   "force_power", NULL);
		if (g_file_test (built_path, G_FILE_TEST_IS_REGULAR)) {
			data->force_path = g_steal_pointer (&built_path);
			g_debug ("Detected force power support at %s",
				 data->force_path);
			break;
		}
	}
	g_list_foreach (devices, (GFunc) g_object_unref, NULL);
}

static gboolean
fu_plugin_thunderbolt_power_supported (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	return data->force_path != NULL;
}

static gboolean
fu_plugin_thunderbolt_power_set (FuPlugin *plugin, gboolean enable,
				 GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	gint fd;
	gint ret;

	if (!fu_plugin_thunderbolt_power_supported (plugin)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "unable to set power to %d (missing kernel support)",
			     enable);
		return FALSE;
	}
	g_debug ("Setting force power to %d", enable);
	fd = g_open (data->force_path, O_WRONLY);
	if (fd == -1) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to open %s", data->force_path);
		return FALSE;
	}
	ret = write (fd, enable ? "1" : "0", 1);
	if (ret < 1) {
		g_set_error (error, G_IO_ERROR,
			     g_io_error_from_errno (errno),
			     "could not write to force_power': %s",
			     g_strerror (errno));
		g_close (fd, NULL);
		return FALSE;
	}
	return g_close (fd, error);
}

static gboolean
fu_plugin_thunderbolt_power_reset_cb (gpointer user_data)
{
	FuPlugin *plugin = FU_PLUGIN (user_data);
	FuPluginData *data = fu_plugin_get_data (plugin);

	if (!fu_plugin_thunderbolt_power_set (plugin, FALSE, NULL))
		g_warning ("failed to reset thunderbolt power");
	data->timeout_id = 0;
	return FALSE;
}

static gboolean
udev_uevent_cb (GUdevClient *udev,
		const gchar *action,
		GUdevDevice *device,
		gpointer     user_data)
{
	FuPlugin *plugin = FU_PLUGIN(user_data);

	if (action == NULL)
		return TRUE;

	g_debug ("uevent for %s: %s", g_udev_device_get_sysfs_path (device), action);

	/* intel-wmi-thunderbolt has been loaded/unloaded */
	if (g_str_equal (action, "change")) {
		fu_plugin_thunderbolt_power_get_path (plugin);
		if (fu_plugin_thunderbolt_power_supported (plugin)) {
			fu_plugin_set_enabled (plugin, TRUE);
			fu_plugin_recoldplug (plugin);
		} else {
			fu_plugin_set_enabled (plugin, FALSE);
		}
	}

	return TRUE;
}

/* virtual functions */

void
fu_plugin_init (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	const gchar *subsystems[] = { "wmi", NULL };

	data->udev = g_udev_client_new (subsystems);
	g_signal_connect (data->udev, "uevent",
			  G_CALLBACK (udev_uevent_cb), plugin);
	/* initially set to true, will wait for a device_register to reset */
	data->needs_forcepower = TRUE;

	/* determines whether to run device_registered */
	fu_plugin_thunderbolt_power_get_path (plugin);

	/* make sure it's tried to coldplug */
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_RUN_AFTER, "thunderbolt");
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	if (data->timeout_id != 0)
		g_source_remove (data->timeout_id);
	g_object_unref (data->udev);
	g_free (data->force_path);
}

void
fu_plugin_device_registered (FuPlugin *plugin, FuDevice *device)
{
	FuPluginData *data = fu_plugin_get_data (plugin);

	/* thunderbolt plugin */
	if (g_strcmp0 (fu_device_get_plugin (device), "thunderbolt") == 0 &&
	    fu_plugin_thunderbolt_power_supported (plugin)) {
		data->needs_forcepower = FALSE;
		if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_INTERNAL)) {
			fu_device_set_metadata_boolean (device,
							FU_DEVICE_METADATA_TBT_CAN_FORCE_POWER,
							TRUE);
		}
	}
}

gboolean
fu_plugin_update_prepare (FuPlugin *plugin,
			  FuDevice *device,
			  GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autoptr(GUdevDevice) udevice = NULL;
	const gchar *devpath;

	/* only run for thunderbolt plugin */
	if (g_strcmp0 (fu_device_get_plugin (device), "thunderbolt") != 0)
		return TRUE;

	devpath = fu_device_get_metadata (device, "sysfs-path");

	udevice = g_udev_client_query_by_sysfs_path (data->udev, devpath);
	if (udevice != NULL) {
		data->needs_forcepower = FALSE;
		return TRUE;
	}
	if (!fu_plugin_thunderbolt_power_set (plugin, TRUE, error))
		return FALSE;

	data->needs_forcepower = TRUE;
	/* wait for the device to come back onto the bus */
	fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_RESTART);
	g_usleep (TBT_NEW_DEVICE_TIMEOUT * G_USEC_PER_SEC);

	return TRUE;
}

gboolean
fu_plugin_update_cleanup (FuPlugin *plugin,
			  FuDevice *device,
			  GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);

	/* only run for thunderbolt plugin */
	if (g_strcmp0 (fu_device_get_plugin (device), "thunderbolt") != 0)
		return TRUE;

	if (data->needs_forcepower &&
	    !fu_plugin_thunderbolt_power_set (plugin, FALSE, error))
		return FALSE;
	return TRUE;
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);

	if (!fu_plugin_thunderbolt_power_supported (plugin)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "missing kernel support for intel-wmi-thunderbolt");
		return FALSE;
	}

	/* this means no devices were found at coldplug by thunderbolt plugin */
	if (data->needs_forcepower) {
		if (!fu_plugin_thunderbolt_power_set (plugin, TRUE, error))
			return FALSE;
		/* in case this was a re-coldplug */
		if (data->timeout_id != 0)
			g_source_remove (data->timeout_id);

		/* reset force power to off after enough time to enumerate */
		data->timeout_id =
			g_timeout_add (TBT_NEW_DEVICE_TIMEOUT * 10000,
				       fu_plugin_thunderbolt_power_reset_cb,
				       plugin);
	}

	return TRUE;
}
