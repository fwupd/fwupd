/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <gudev/gudev.h>

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

#include "fu-nvme-device.h"

struct FuPluginData {
	GUdevClient		*gudev_client;
};

static gboolean
fu_plugin_nvme_add_device (FuPlugin *plugin, GUdevDevice *udev_device, GError **error)
{
	const gchar *devfile = g_udev_device_get_device_file (udev_device);
	g_autoptr(FuNvmeDevice) dev = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* create device and unlock */
	dev = fu_nvme_device_new (udev_device);
	locker = fu_device_locker_new (dev, error);
	if (locker == NULL)
		return FALSE;

	/* insert to hash */
	fu_plugin_cache_add (plugin, devfile, dev);
	fu_plugin_device_add (plugin, FU_DEVICE (dev));
	return TRUE;
}

static void
fu_plugin_nvme_remove (FuPlugin *plugin, GUdevDevice *udev_device)
{
	const gchar *devfile = g_udev_device_get_device_file (udev_device);
	FuDevice *dev = fu_plugin_cache_lookup (plugin, devfile);
	if (dev != NULL)
		fu_plugin_device_remove (plugin, dev);
}

static void
fu_plugin_nvme_add (FuPlugin *plugin, GUdevDevice *udev_device)
{
	const gchar *devfile = g_udev_device_get_device_file (udev_device);
	g_autoptr(GError) error = NULL;
	if (fu_plugin_cache_lookup (plugin, devfile) != NULL)
		return;
	if (!fu_plugin_nvme_add_device (plugin, udev_device, &error))
		g_warning ("failed to add NVMe device: %s", error->message);
}

static void
fu_plugin_nvme_uevent_cb (GUdevClient *gudev_client,
			  const gchar *action,
			  GUdevDevice *udev_device,
			  FuPlugin *plugin)
{
	if (g_strcmp0 (action, "remove") == 0) {
		fu_plugin_nvme_remove (plugin, udev_device);
		return;
	}
	if (g_strcmp0 (action, "add") == 0) {
		fu_plugin_nvme_add (plugin, udev_device);
		return;
	}
}

void
fu_plugin_init (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	const gchar *subsystems[] = { "nvme", NULL };
	data->gudev_client = g_udev_client_new (subsystems);
	g_signal_connect (data->gudev_client, "uevent",
			  G_CALLBACK (fu_plugin_nvme_uevent_cb), plugin);
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_object_unref (data->gudev_client);
}

#if !GLIB_CHECK_VERSION(2,56,0)
typedef GList FuObjectList;
static void
fu_object_list_free (FuObjectList *list)
{
	g_list_free_full (list, (GDestroyNotify) g_object_unref);
}
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuObjectList, fu_object_list_free)
#pragma clang diagnostic pop
#endif

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
#if GLIB_CHECK_VERSION(2,56,0)
	g_autolist(GObject) devices = NULL;
#else
	g_autoptr(FuObjectList) devices = NULL;
#endif
	devices = g_udev_client_query_by_subsystem (data->gudev_client, "nvme");
	for (GList *l = devices; l != NULL; l = l->next) {
		GUdevDevice *udev_device = l->data;
		if (!fu_plugin_nvme_add_device (plugin, udev_device, error))
			return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_update (FuPlugin *plugin,
		  FuDevice *device,
		  GBytes *blob_fw,
		  FwupdInstallFlags flags,
		  GError **error)
{
	if ((flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "writing NVMe firmware is untested, "
				     "use --force to override");
		return FALSE;
	}
	return fu_device_write_firmware (device, blob_fw, error);
}
