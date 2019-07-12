/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>

#include "fu-plugin-vfuncs.h"

#include "fu-unifying-bootloader-nordic.h"
#include "fu-unifying-bootloader-texas.h"
#include "fu-unifying-common.h"
#include "fu-unifying-peripheral.h"
#include "fu-unifying-runtime.h"

gboolean
fu_plugin_update_detach (FuPlugin *plugin, FuDevice *device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_detach (device, error);
}

gboolean
fu_plugin_update_attach (FuPlugin *plugin, FuDevice *device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_attach (device, error);
}

gboolean
fu_plugin_update_reload (FuPlugin *plugin, FuDevice *device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	return TRUE;
}

gboolean
fu_plugin_update (FuPlugin *plugin,
		  FuDevice *device,
		  GBytes *blob_fw,
		  FwupdInstallFlags flags,
		  GError **error)
{
	g_autoptr(FuDeviceLocker) locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_write_firmware (device, blob_fw, flags, error);
}

static gboolean
fu_plugin_unifying_check_supported_device (FuPlugin *plugin, FuDevice *device)
{
	GPtrArray *guids = fu_device_get_guids (device);
	for (guint i = 0; i < guids->len; i++) {
		const gchar *guid = g_ptr_array_index (guids, i);
		if (fu_plugin_check_supported (plugin, guid))
			return TRUE;
	}
	return FALSE;
}

gboolean
fu_plugin_udev_device_added (FuPlugin *plugin, FuUdevDevice *device, GError **error)
{
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* interesting device? */
	if (g_strcmp0 (fu_udev_device_get_subsystem (device), "hidraw") != 0)
		return TRUE;

	/* logitech */
	if (fu_udev_device_get_vendor (device) != FU_UNIFYING_DEVICE_VID)
		return TRUE;

	/* runtime */
	if (fu_device_has_custom_flag (FU_DEVICE (device), "is-receiver")) {
		dev = g_object_new (FU_TYPE_UNIFYING_RUNTIME,
				    "version-format", FWUPD_VERSION_FORMAT_PLAIN,
				    NULL);
		fu_device_incorporate (dev, FU_DEVICE (device));
	} else {

		/* create device so we can run ->probe() and add UFY GUIDs */
		dev = g_object_new (FU_TYPE_UNIFYING_PERIPHERAL,
				    "version-format", FWUPD_VERSION_FORMAT_PLAIN,
				    NULL);
		fu_device_incorporate (dev, FU_DEVICE (device));
		if (!fu_device_probe (dev, error))
			return FALSE;

		/* there are a lot of unifying peripherals, but not all respond
		 * well to opening -- so limit to ones with issued updates */
		if (!fu_plugin_unifying_check_supported_device (plugin, dev)) {
			g_autofree gchar *guids = fu_device_get_guids_as_str (FU_DEVICE (device));
			g_debug ("%s has no updates, so ignoring device", guids);
			return TRUE;
		}
	}

	/* open to get the version */
	locker = fu_device_locker_new (dev, error);
	if (locker == NULL)
		return FALSE;
	fu_plugin_device_add (plugin, dev);
	return TRUE;
}

gboolean
fu_plugin_usb_device_added (FuPlugin *plugin, FuUsbDevice *device, GError **error)
{
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* logitech */
	if (fu_usb_device_get_vid (device) != FU_UNIFYING_DEVICE_VID)
		return TRUE;

	/* check is bootloader */
	if (!fu_device_has_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug ("not in bootloader mode, ignoring");
		return TRUE;
	}
	if (fu_device_has_custom_flag (FU_DEVICE (device), "is-nordic")) {
		dev = g_object_new (FU_TYPE_UNIFYING_BOOTLOADER_NORDIC,
				    "version-format", FWUPD_VERSION_FORMAT_PLAIN,
				    NULL);
		fu_device_incorporate (dev, FU_DEVICE (device));
	} else if (fu_device_has_custom_flag (FU_DEVICE (device), "is-texas")) {
		dev = g_object_new (FU_TYPE_UNIFYING_BOOTLOADER_TEXAS,
				    "version-format", FWUPD_VERSION_FORMAT_PLAIN,
				    NULL);
		fu_device_incorporate (dev, FU_DEVICE (device));
		g_usleep (200*1000);
	}

	/* not supported */
	if (dev == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "bootloader device not supported");
		return FALSE;
	}

	/* open to get the version */
	locker = fu_device_locker_new (dev, error);
	if (locker == NULL)
		return FALSE;
	fu_plugin_device_add (plugin, dev);
	return TRUE;
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	/* check the kernel has CONFIG_HIDRAW */
	if (!g_file_test ("/sys/class/hidraw", G_FILE_TEST_IS_DIR)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "no kernel support for CONFIG_HIDRAW");
		return FALSE;
	}
	return TRUE;
}

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_REQUIRES_QUIRK, FU_QUIRKS_PLUGIN);
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_SUPPORTS_PROTOCOL, "com.logitech.unifying");
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_SUPPORTS_PROTOCOL, "com.logitech.unifyingsigned");
	fu_plugin_add_udev_subsystem (plugin, "hidraw");
}
