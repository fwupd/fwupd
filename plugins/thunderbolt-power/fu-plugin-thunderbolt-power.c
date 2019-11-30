/*
 * Copyright (C) 2017 Dell, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <gio/gunixfdlist.h>
#include <glib/gstdio.h>

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"
#include "fu-device-metadata.h"

#define BOLT_DBUS_SERVICE	"org.freedesktop.bolt"
#define BOLT_DBUS_PATH		"/org/freedesktop/bolt"
#define BOLT_DBUS_INTERFACE	"org.freedesktop.bolt1.Power"

/* empirically measured amount of time for the TBT device to come and go */
#define TBT_NEW_DEVICE_TIMEOUT	2 /* s */

struct FuPluginData {
	GUdevClient   *udev;
	gchar         *force_path;
	gboolean       needs_forcepower;
	gboolean       updating;
	guint          timeout_id;
	gint           bolt_fd;
};

static gboolean
fu_plugin_thunderbolt_power_bolt_supported (FuPlugin *plugin)
{
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GVariant) val = NULL;
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GError) error_local = NULL;
	gboolean supported = FALSE;

	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error_local);
	if (connection == NULL) {
		g_warning ("Failed to initialize d-bus connection: %s",
			   error_local->message);
		return supported;
	}

	proxy = g_dbus_proxy_new_sync (connection,
					G_DBUS_PROXY_FLAGS_NONE,
					NULL,
					BOLT_DBUS_SERVICE,
					BOLT_DBUS_PATH,
					BOLT_DBUS_INTERFACE,
					NULL,
					&error_local);
	if (proxy == NULL) {
		g_warning ("Failed to initialize d-bus proxy: %s",
			   error_local->message);
		return supported;
	}
	val = g_dbus_proxy_get_cached_property (proxy, "Supported");
	if (val != NULL)
		g_variant_get (val, "b", &supported);

	g_debug ("Bolt force power support: %d", supported);

	return supported;
}

static gboolean
fu_plugin_thunderbolt_power_bolt_force_power (FuPlugin *plugin,
					      GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GUnixFDList) fds = NULL;
	g_autoptr(GVariant) val = NULL;
	GVariant *input;

	input = g_variant_new ("(ss)",
				"fwupd",   /* who */
				"");       /* flags */

	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
	if (connection == NULL)
		return FALSE;

	proxy = g_dbus_proxy_new_sync (connection,
					G_DBUS_PROXY_FLAGS_NONE,
					NULL,
					BOLT_DBUS_SERVICE,
					BOLT_DBUS_PATH,
					BOLT_DBUS_INTERFACE,
					NULL,
					error);
	if (proxy == NULL)
		return FALSE;

	val = g_dbus_proxy_call_with_unix_fd_list_sync (proxy,
							"ForcePower",
							input,
							G_DBUS_CALL_FLAGS_NONE,
							-1,
							NULL,
							&fds,
							NULL,
							error);

	if (val == NULL)
		return FALSE;

	if (g_unix_fd_list_get_length (fds) != 1) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				"invalid number of file descriptors returned: %d",
				g_unix_fd_list_get_length (fds));
		return FALSE;
	}
	data->bolt_fd = g_unix_fd_list_get (fds, 0, NULL);

	return TRUE;
}

static void
fu_plugin_thunderbolt_power_get_kernel_path (FuPlugin *plugin)
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
			g_debug ("Direct kernel force power support at %s",
				 data->force_path);
			break;
		}
	}
	g_list_foreach (devices, (GFunc) g_object_unref, NULL);
}

static gboolean
fu_plugin_thunderbolt_power_kernel_supported (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	return data->force_path != NULL;
}

static gboolean
fu_plugin_thunderbolt_power_kernel_force_power (FuPlugin *plugin, gboolean enable,
						GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	gint fd;
	gint ret;

	if (!fu_plugin_thunderbolt_power_kernel_supported (plugin)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "unable to set power to %d (missing kernel support)",
			     enable);
		return FALSE;
	}
	g_debug ("Setting force power to %d using kernel", enable);
	fd = g_open (data->force_path, O_WRONLY, 0);
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
fu_plugin_thunderbolt_power_set (FuPlugin *plugin, gboolean enable,
				 GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);

	/* prefer bolt API if available */
	if (fu_plugin_thunderbolt_power_bolt_supported (plugin)) {
		g_debug ("Setting force power to %d using bolt", enable);
		if (enable)
			return fu_plugin_thunderbolt_power_bolt_force_power (plugin, error);
		return data->bolt_fd >= 0 ? g_close (data->bolt_fd, error) : TRUE;
	}

	return fu_plugin_thunderbolt_power_kernel_force_power (plugin, enable, error);
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

static void
fu_plugin_thunderbolt_reset_timeout (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);

	if (!data->needs_forcepower || data->updating)
		return;

	g_debug ("Setting timeout to %d seconds",
		 TBT_NEW_DEVICE_TIMEOUT * 10);

	/* in case this was a re-coldplug */
	if (data->timeout_id != 0)
		g_source_remove (data->timeout_id);

	/* reset force power to off after enough time to enumerate */
	data->timeout_id =
		g_timeout_add (TBT_NEW_DEVICE_TIMEOUT * 10000,
				fu_plugin_thunderbolt_power_reset_cb,
				plugin);
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

	g_debug ("uevent for %s: (%s) %s",
		 g_udev_device_get_name (device),
		 g_udev_device_get_sysfs_path (device),
		 action);

	/* thunderbolt device was turned on */
	if (g_str_equal (g_udev_device_get_subsystem (device), "thunderbolt") &&
	    g_str_equal (action, "add")) {
		fu_plugin_thunderbolt_reset_timeout (plugin);
	/* intel-wmi-thunderbolt has been loaded/unloaded */
	} else if (g_str_equal (action, "change")) {
		fu_plugin_thunderbolt_power_get_kernel_path (plugin);
		if (fu_plugin_thunderbolt_power_kernel_supported (plugin)) {
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
	const gchar *subsystems[] = { "thunderbolt", "wmi", NULL };

	data->udev = g_udev_client_new (subsystems);
	g_signal_connect (data->udev, "uevent",
			  G_CALLBACK (udev_uevent_cb), plugin);
	/* initially set to true, will wait for a device_register to reset */
	data->needs_forcepower = TRUE;
	/* will reset when needed */
	data->bolt_fd = -1;

	/* determines whether to run device_registered */
	fu_plugin_thunderbolt_power_get_kernel_path (plugin);

	/* make sure it's tried to coldplug */
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_RUN_AFTER, "thunderbolt");
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
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
	/* in case destroying before force power turned off */
	if (data->bolt_fd >= 0)
		g_close (data->bolt_fd, NULL);
}

void
fu_plugin_device_registered (FuPlugin *plugin, FuDevice *device)
{
	FuPluginData *data = fu_plugin_get_data (plugin);

	/* We care only about the thunderbolt devices. NB: we don't care
	 * about avoiding to auto-starting boltd here, because if there
	 * is thunderbolt hardware present, boltd is already running */
	if (g_strcmp0 (fu_device_get_plugin (device), "thunderbolt") == 0 &&
	    (fu_plugin_thunderbolt_power_bolt_supported (plugin) ||
	    fu_plugin_thunderbolt_power_kernel_supported (plugin))) {
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
			  FwupdInstallFlags flags,
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
	data->updating = TRUE;
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
			  FwupdInstallFlags flags,
			  FuDevice *device,
			  GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);

	/* only run for thunderbolt plugin */
	if (g_strcmp0 (fu_device_get_plugin (device), "thunderbolt") != 0)
		return TRUE;

	data->updating = FALSE;
	if (data->needs_forcepower &&
	    !fu_plugin_thunderbolt_power_set (plugin, FALSE, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_plugin_thunderbolt_power_coldplug (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);

	/* NB: we don't check for force-power support via bolt here
	 * (although we later prefer that), because boltd uses the
	 * same kernel interface and if that does not exist, we can
	 * avoid pinging bolt, potentially auto-starting it. */
	if (!fu_plugin_thunderbolt_power_kernel_supported (plugin)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "No support for force power detected");
		return FALSE;
	}

	/* this means no devices were found at coldplug by thunderbolt plugin */
	if (data->needs_forcepower) {
		if (!fu_plugin_thunderbolt_power_set (plugin, TRUE, error))
			return FALSE;

		fu_plugin_thunderbolt_reset_timeout (plugin);
	}

	return TRUE;
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	return fu_plugin_thunderbolt_power_coldplug (plugin, error);
}

gboolean
fu_plugin_recoldplug (FuPlugin *plugin, GError **error)
{
	return fu_plugin_thunderbolt_power_coldplug (plugin, error);
}
