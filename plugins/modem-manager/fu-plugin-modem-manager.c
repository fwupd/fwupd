/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include <libmm-glib.h>

#include "fu-plugin-vfuncs.h"

#include "fu-mm-device.h"

struct FuPluginData {
	MMManager	*manager;
	gboolean	manager_ready;
};

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
		g_debug ("failed to probe MM device: %s", error->message);
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

	if (fu_common_vercmp (version, MM_REQUIRED_VERSION) < 0) {
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
	const gchar *name_owner;
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
	if (priv->manager != NULL)
		g_object_unref (priv->manager);
}

gboolean
fu_plugin_update_detach (FuPlugin *plugin, FuDevice *device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open device */
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;

	/* reset */
	if (!fu_device_detach (FU_DEVICE (device), error))
		return FALSE;

	/* wait for replug */
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}
