/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

#include "fu-altos-device.h"

gboolean
fu_plugin_usb_device_added (FuPlugin *plugin, GUsbDevice *usb_device, GError **error)
{
	const gchar *platform_id = NULL;
	g_autofree gchar *runtime_id = NULL;
	g_autoptr(FuAltosDevice) dev = NULL;

	/* get kind */
	dev = fu_altos_device_new (usb_device);
	if (dev == NULL)
		return TRUE;

	/* get device properties */
	if (!fu_altos_device_probe (dev, error))
		return FALSE;

	/* only the bootloader can do the update */
	platform_id = g_usb_device_get_platform_id (usb_device);
	runtime_id = g_strdup_printf ("%s-runtime", platform_id);
	if (fu_altos_device_get_kind (dev) == FU_ALTOS_DEVICE_KIND_BOOTLOADER) {
		FuDevice *dev_runtime;
		dev_runtime = fu_plugin_cache_lookup (plugin, runtime_id);
		if (dev_runtime != NULL) {
			const gchar *guid = fu_device_get_guid_default (dev_runtime);
			g_debug ("adding runtime GUID of %s", guid);
			fu_device_add_guid (FU_DEVICE (dev), guid);
			fu_device_set_version (FU_DEVICE (dev),
					       fu_device_get_version (dev_runtime));
		}
	} else {
		fu_plugin_cache_add (plugin, runtime_id, dev);
	}

	/* success */
	fu_plugin_device_add (plugin, FU_DEVICE (dev));
	return TRUE;
}

gboolean
fu_plugin_verify (FuPlugin *plugin,
		  FuDevice *dev,
		  FuPluginVerifyFlags flags,
		  GError **error)
{
	g_autoptr(GBytes) blob_fw = NULL;
	GChecksumType checksum_types[] = {
		G_CHECKSUM_SHA1,
		G_CHECKSUM_SHA256,
		0 };

	/* get data */
	fu_device_set_status (dev, FWUPD_STATUS_DEVICE_VERIFY);
	blob_fw = fu_device_read_firmware (dev, error);
	if (blob_fw == NULL)
		return FALSE;
	for (guint i = 0; checksum_types[i] != 0; i++) {
		g_autofree gchar *hash = NULL;
		hash = g_compute_checksum_for_bytes (checksum_types[i], blob_fw);
		fu_device_add_checksum (dev, hash);
	}
	return TRUE;
}

gboolean
fu_plugin_update (FuPlugin *plugin,
		  FuDevice *dev,
		  GBytes *blob_fw,
		  FwupdInstallFlags flags,
		  GError **error)
{
	fu_device_set_status (dev, FWUPD_STATUS_DEVICE_WRITE);
	return fu_device_write_firmware (dev, blob_fw, error);
}
