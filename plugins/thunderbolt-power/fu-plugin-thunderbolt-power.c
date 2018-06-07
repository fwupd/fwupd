/* -*- mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Dell Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
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
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GUdevDevice, g_object_unref)
#pragma clang diagnostic pop
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
			fu_plugin_request_recoldplug (plugin);
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
	if (data->timeout_id != 0) {
		g_source_remove (data->timeout_id);
		data->timeout_id = 0;
	}
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

	/* reset any timers that might still be running from coldplug */
	if (data->timeout_id != 0) {
		g_source_remove (data->timeout_id);
		data->timeout_id = 0;
	}

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
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	for (guint i = 0; i < 5; i++) {
		g_autoptr(GUdevDevice) udevice_tmp = NULL;
		g_usleep (TBT_NEW_DEVICE_TIMEOUT * G_USEC_PER_SEC);
		udevice_tmp = g_udev_client_query_by_sysfs_path (data->udev, devpath);
		if (udevice_tmp != NULL)
			return TRUE;
	}

	/* device did not wake up */
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_SUPPORTED,
		     "device did not wake up when required");
	return FALSE;
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

static gboolean
fu_plugin_thunderbolt_power_coldplug (FuPlugin *plugin, GError **error)
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

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	return fu_plugin_thunderbolt_power_coldplug (plugin, error);
}
