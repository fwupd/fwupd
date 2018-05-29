/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

#include "fu-csr-device.h"

gboolean
fu_plugin_usb_device_added (FuPlugin *plugin, GUsbDevice *usb_device, GError **error)
{
	g_autoptr(FuCsrDevice) device = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	device = fu_csr_device_new (usb_device);
	fu_device_set_quirks (FU_DEVICE (device), fu_plugin_get_quirks (plugin));
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	fu_plugin_device_add (plugin, FU_DEVICE (device));
	return TRUE;
}

gboolean
fu_plugin_verify (FuPlugin *plugin, FuDevice *device,
		  FuPluginVerifyFlags flags, GError **error)
{
	g_autoptr(GBytes) blob_fw = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	GChecksumType checksum_types[] = {
		G_CHECKSUM_SHA1,
		G_CHECKSUM_SHA256,
		0 };

	/* get data */
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	blob_fw = fu_device_read_firmware (device, error);
	if (blob_fw == NULL)
		return FALSE;
	for (guint i = 0; checksum_types[i] != 0; i++) {
		g_autofree gchar *hash = NULL;
		hash = g_compute_checksum_for_bytes (checksum_types[i], blob_fw);
		fu_device_add_checksum (device, hash);
	}
	return TRUE;
}

gboolean
fu_plugin_update (FuPlugin *plugin, FuDevice *device, GBytes *blob_fw,
		  FwupdInstallFlags flags, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	if (!fu_device_write_firmware (device, blob_fw, error))
		return FALSE;
	return fu_device_attach (device, error);
}
