/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include <libmm-glib.h>

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

#include "fu-mm-device.h"
#include "fu-mm-utils.h"

/* amount of time to wait for ports of the same device	being exposed by kernel */
#define FU_MM_UDEV_DEVICE_PORTS_TIMEOUT		3	/* s */

typedef struct FuPluginMmInhibitedDeviceInfo FuPluginMmInhibitedDeviceInfo;

struct FuPluginData {
	MMManager	*manager;
	gboolean	 manager_ready;
	GUdevClient	*udev_client;
	guint		 udev_timeout_id;

	/* when a device is inhibited from MM, we store all relevant details
	 * ourselves to recreate a functional device object even without MM
	 */
	FuPluginMmInhibitedDeviceInfo *inhibited;
};

static void
fu_plugin_mm_udev_device_removed (FuPlugin *plugin)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	FuMmDevice *dev;

	if (priv->inhibited == NULL)
		return;

	dev = fu_plugin_cache_lookup (plugin, priv->inhibited->physical_id);
	if (dev == NULL)
		return;

	/* once the first port is gone, consider device is gone */
	fu_plugin_cache_remove (plugin, priv->inhibited->physical_id);
	fu_plugin_device_remove (plugin, FU_DEVICE (dev));

	/* no need to wait for more ports, cancel that right away */
	if (priv->udev_timeout_id != 0) {
		g_source_remove (priv->udev_timeout_id);
		priv->udev_timeout_id = 0;
	}
}

static void
fu_plugin_mm_uninhibit_device (FuPlugin *plugin)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FuPluginMmInhibitedDeviceInfo) info = NULL;

	/* get the device removed from the plugin cache before uninhibiting */
	fu_plugin_mm_udev_device_removed (plugin);

	info = g_steal_pointer (&priv->inhibited);
	if ((priv->manager != NULL) && (info != NULL)) {
		g_debug ("uninhibit modemmanager device with uid %s", info->inhibited_uid);
		mm_manager_uninhibit_device_sync (priv->manager, info->inhibited_uid, NULL, NULL);
	}
}

static gboolean
fu_plugin_mm_udev_device_ports_timeout (gpointer user_data)
{
	FuPlugin *plugin = user_data;
	FuPluginData *priv = fu_plugin_get_data (plugin);
	FuMmDevice *dev;
	g_autoptr(GError) error = NULL;

	g_return_val_if_fail (priv->inhibited != NULL, G_SOURCE_REMOVE);
	priv->udev_timeout_id = 0;

	dev = fu_plugin_cache_lookup (plugin, priv->inhibited->physical_id);
	if (dev != NULL) {
		if (!fu_device_probe (FU_DEVICE (dev), &error)) {
			g_warning ("failed to probe MM device: %s", error->message);
		} else {
			fu_plugin_device_add (plugin, FU_DEVICE (dev));
		}
	}

	return G_SOURCE_REMOVE;
}

static void
fu_plugin_mm_udev_device_ports_timeout_reset (FuPlugin *plugin)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);

	g_return_if_fail (priv->inhibited != NULL);
	if (priv->udev_timeout_id != 0)
		g_source_remove (priv->udev_timeout_id);
	priv->udev_timeout_id = g_timeout_add_seconds (FU_MM_UDEV_DEVICE_PORTS_TIMEOUT,
						       fu_plugin_mm_udev_device_ports_timeout,
						       plugin);
}

static void
fu_plugin_mm_udev_device_port_added (FuPlugin		*plugin,
				     const gchar	*subsystem,
				     const gchar	*path,
				     gint		 ifnum)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	FuMmDevice *existing;
	g_autoptr(FuMmDevice) dev = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (priv->inhibited != NULL);
	existing = fu_plugin_cache_lookup (plugin, priv->inhibited->physical_id);
	if (existing != NULL) {
		/* add port to existing device */
		fu_mm_device_udev_add_port (existing, subsystem, path, ifnum);
		fu_plugin_mm_udev_device_ports_timeout_reset (plugin);
		return;
	}
	/* create device and add to cache */
	dev = fu_mm_device_udev_new (priv->manager, priv->inhibited);
	fu_mm_device_udev_add_port (dev, subsystem, path, ifnum);
	fu_plugin_cache_add (plugin, priv->inhibited->physical_id, dev);

	/* wait a bit before probing, in case more ports get added */
	fu_plugin_mm_udev_device_ports_timeout_reset (plugin);
}

static gboolean
fu_plugin_mm_udev_uevent_cb (GUdevClient	*udev,
			     const gchar	*action,
			     GUdevDevice	*device,
			     gpointer		 user_data)
{
	FuPlugin *plugin = FU_PLUGIN (user_data);
	FuPluginData *priv = fu_plugin_get_data (plugin);
	const gchar *subsystem = g_udev_device_get_subsystem (device);
	const gchar *name = g_udev_device_get_name (device);
	g_autofree gchar *path = NULL;
	g_autofree gchar *device_sysfs_path = NULL;
	gint ifnum = -1;

	if (action == NULL || subsystem == NULL || priv->inhibited == NULL || name == NULL)
		return TRUE;

	/* ignore if loading port info fails */
	if (!fu_mm_utils_get_udev_port_info (device, &device_sysfs_path, &ifnum, NULL))
		return TRUE;

	/* ignore all events for ports not owned by our device */
	if (g_strcmp0 (device_sysfs_path, priv->inhibited->physical_id) != 0)
		return TRUE;

	/* ignore non-cdc-wdm usbmisc ports */
	if (g_str_equal (subsystem, "usbmisc") && !g_str_has_prefix (name, "cdc-wdm"))
		return TRUE;

	path = g_strdup_printf ("/dev/%s", name);

	if ((g_str_equal (action, "add")) || (g_str_equal (action, "change"))) {
		g_debug ("added port to inhibited modem: %s (ifnum %d)", path, ifnum);
		fu_plugin_mm_udev_device_port_added (plugin, subsystem, path, ifnum);
	} else if (g_str_equal (action, "remove")) {
		g_debug ("removed port from inhibited modem: %s", path);
		fu_plugin_mm_udev_device_removed (plugin);
	}

	return TRUE;
}

static gboolean
fu_plugin_mm_inhibit_device (FuPlugin *plugin, FuDevice *device, GError **error)
{
	static const gchar *subsystems[] = { "tty", "usbmisc", NULL };
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FuPluginMmInhibitedDeviceInfo) info = NULL;

	fu_plugin_mm_uninhibit_device (plugin);

	info = fu_plugin_mm_inhibited_device_info_new (FU_MM_DEVICE (device));

	g_debug ("inhibit modemmanager device with uid %s", info->inhibited_uid);
	if (!mm_manager_inhibit_device_sync (priv->manager, info->inhibited_uid, NULL, error))
		return FALSE;

	/* setup inhibited device info */
	priv->inhibited = g_steal_pointer (&info);

	/* as soon as inhibition is place, we need to do modem device monitoring based
	 * on the udev client, as MM no longer reports devices */
	priv->udev_client = g_udev_client_new (subsystems);
	g_signal_connect (priv->udev_client, "uevent",
			  G_CALLBACK (fu_plugin_mm_udev_uevent_cb), plugin);

	return TRUE;
}

static void
fu_plugin_mm_device_add (FuPlugin *plugin, MMObject *modem)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	const gchar *object_path = mm_object_get_path (modem);
	g_autoptr(FuMmDevice) dev = NULL;
	g_autoptr(GError) error = NULL;
	if (fu_plugin_cache_lookup (plugin, object_path) != NULL) {
		g_warning ("MM device already added, ignoring");
		return;
	}
	dev = fu_mm_device_new (priv->manager, modem);
	if (!fu_device_probe (FU_DEVICE (dev), &error)) {
		g_warning ("failed to probe MM device: %s", error->message);
		return;
	}
	fu_plugin_device_add (plugin, FU_DEVICE (dev));
	fu_plugin_cache_add (plugin, object_path, dev);
}

static void
fu_plugin_mm_device_added_cb (MMManager *manager, MMObject *modem, FuPlugin *plugin)
{
	fu_plugin_mm_device_add (plugin, modem);
}

static void
fu_plugin_mm_device_removed_cb (MMManager *manager, MMObject *modem, FuPlugin *plugin)
{
	const gchar *object_path = mm_object_get_path (modem);
	FuMmDevice *dev = fu_plugin_cache_lookup (plugin, object_path);
	if (dev == NULL)
		return;
	g_debug ("removed modem: %s", mm_object_get_path (modem));
	fu_plugin_cache_remove (plugin, object_path);
	fu_plugin_device_remove (plugin, FU_DEVICE (dev));
}

static void
fu_plugin_mm_teardown_manager (FuPlugin *plugin)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);

	if (priv->manager_ready) {
		g_debug ("ModemManager no longer available");
		g_signal_handlers_disconnect_by_func (priv->manager,
						      G_CALLBACK (fu_plugin_mm_device_added_cb),
						      plugin);
		g_signal_handlers_disconnect_by_func (priv->manager,
						      G_CALLBACK (fu_plugin_mm_device_removed_cb),
						      plugin);
		priv->manager_ready = FALSE;
	}
}

static void
fu_plugin_mm_setup_manager (FuPlugin *plugin)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	const gchar *version = mm_manager_get_version (priv->manager);
	GList *list;

	if (fu_common_vercmp_full (version,
				   MM_REQUIRED_VERSION,
				   FWUPD_VERSION_FORMAT_TRIPLET) < 0) {
		g_warning ("ModemManager %s is available, but need at least %s",
			   version, MM_REQUIRED_VERSION);
		return;
	}

	g_debug ("ModemManager %s is available", version);

	g_signal_connect (priv->manager, "object-added",
			  G_CALLBACK (fu_plugin_mm_device_added_cb), plugin);
	g_signal_connect (priv->manager, "object-removed",
			  G_CALLBACK (fu_plugin_mm_device_removed_cb), plugin);

	list = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (priv->manager));
	for (GList *l = list; l != NULL; l = g_list_next (l)) {
		MMObject *modem = MM_OBJECT (l->data);
		fu_plugin_mm_device_add (plugin, modem);
		g_object_unref (modem);
	}
	g_list_free (list);

	priv->manager_ready = TRUE;
}

static void
fu_plugin_mm_name_owner_updated (FuPlugin *plugin)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autofree gchar *name_owner = NULL;
	name_owner = g_dbus_object_manager_client_get_name_owner (G_DBUS_OBJECT_MANAGER_CLIENT (priv->manager));
	if (name_owner != NULL)
		fu_plugin_mm_setup_manager (plugin);
	else
		fu_plugin_mm_teardown_manager (plugin);
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_signal_connect_swapped (priv->manager, "notify::name-owner",
				  G_CALLBACK (fu_plugin_mm_name_owner_updated),
				  plugin);
	fu_plugin_mm_name_owner_updated (plugin);
	return TRUE;
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(GDBusConnection) connection = NULL;

	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
	if (connection == NULL)
		return FALSE;
	priv->manager = mm_manager_new_sync (connection,
					     G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START,
					     NULL, error);
	if (priv->manager == NULL)
		return FALSE;

	return TRUE;
}

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);

	fu_plugin_mm_uninhibit_device (plugin);

	if (priv->udev_timeout_id)
		g_source_remove (priv->udev_timeout_id);
	if (priv->udev_client)
		g_object_unref (priv->udev_client);
	if (priv->manager != NULL)
		g_object_unref (priv->manager);
}

gboolean
fu_plugin_update_detach (FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open device */
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;

	/* inhibit device and track it inside the plugin, not bound to the
	 * lifetime of the FuMmDevice, because that object will only exist for
	 * as long as the ModemManager device exists, and inhibiting will
	 * implicitly remove the device from ModemManager. */
	if (priv->inhibited == NULL) {
		if (!fu_plugin_mm_inhibit_device (plugin, device, error))
			return FALSE;
	}

	/* reset */
	if (!fu_device_detach (device, error)) {
		fu_plugin_mm_uninhibit_device (plugin);
		return FALSE;
	}

	/* note: wait for replug set by device if it really needs it */
	return TRUE;
}

static void
fu_plugin_mm_device_attach_finished (gpointer user_data)
{
	FuPlugin *plugin = FU_PLUGIN (user_data);
	fu_plugin_mm_uninhibit_device (plugin);
}

gboolean
fu_plugin_update_attach (FuPlugin *plugin, FuDevice *device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open device */
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;

	/* schedule device attach asynchronously, which is extremely important
         * so that engine can setup the device "waiting" logic before the actual
         * attach procedure happens (which will reset the module if it worked
         * properly) */
	if (!fu_device_attach (device, error))
		return FALSE;

	/* this signal will always be emitted asynchronously */
	g_signal_connect_swapped (device, "attach-finished",
				  G_CALLBACK (fu_plugin_mm_device_attach_finished), plugin);

	return TRUE;
}
