/*
 * Copyright (C) 2023 Puliang Lu <puliang.lu@fibocom.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-fibocom-plugin.h"

struct _FuFibocomPlugin {
	FuPlugin parent_instance;

	/* fibocom modem needs to be marked When is flash finished */
	int fibocom_flash_end;
};

G_DEFINE_TYPE(FuFibocomPlugin, fu_fibocom_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_fibocom_plugin_write_firmware(FuPlugin *plugin,
				 FuDevice *device,
				 GBytes *blob_fw,
				 FuProgress *progress,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuFibocomPlugin *self = FU_FIBOCOM_PLUGIN(plugin);
	FuDevice *proxy = fu_device_get_proxy_with_fallback(device);
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuFirmware) manifest = NULL;
	g_autoptr(FuFirmware) flash_end_file = NULL;

	locker = fu_device_locker_new(proxy, error);
	if (locker == NULL)
		return FALSE;

	firmware = fu_device_prepare_firmware(device, blob_fw, flags, error);

	flash_end_file = fu_firmware_get_image_by_id(firmware, "flash_end", NULL);
	manifest = fu_firmware_get_image_by_id(firmware, "partition_nand.xml", NULL);

	if (flash_end_file != NULL && manifest == NULL) {
		self->fibocom_flash_end -= 1;
		g_info("fibocom flash end number: %d", self->fibocom_flash_end);
		return TRUE;
	} else if (flash_end_file == NULL && manifest != NULL) {
		self->fibocom_flash_end += 1;
	}
	g_info("fibocom flash end number: %d", self->fibocom_flash_end);

	return fu_device_write_firmware(device, blob_fw, progress, flags, error);
}

static gboolean
fu_fibocom_plugin_attach(FuPlugin *plugin, FuDevice *device, FuProgress *progress, GError **error)
{
	FuFibocomPlugin *self = FU_FIBOCOM_PLUGIN(plugin);
	FuDevice *proxy = fu_device_get_proxy_with_fallback(device);
	g_autoptr(FuDeviceLocker) locker = NULL;
	locker = fu_device_locker_new(proxy, error);
	if (locker == NULL)
		return FALSE;

	/* if flash_end is greater than zero, flash is not finished and attach cannot be executed */
	if (self->fibocom_flash_end > 0) {
		return TRUE;
	}

	return fu_device_attach_full(device, progress, error);
}

static gboolean
fu_fibocom_plugin_backend_device_removed(FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuFibocomPlugin *self = FU_FIBOCOM_PLUGIN(plugin);
	self->fibocom_flash_end = 0;
	return TRUE;
}

static void
fu_fibocom_plugin_init(FuFibocomPlugin *self)
{
	self->fibocom_flash_end = 0;
}

static void
fu_fibocom_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "BlockSize");
	fu_context_add_quirk_key(ctx, "OperationDelay");
}

static void
fu_fibocom_plugin_class_init(FuFibocomPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_fibocom_plugin_constructed;
	plugin_class->write_firmware = fu_fibocom_plugin_write_firmware;
	plugin_class->attach = fu_fibocom_plugin_attach;
	plugin_class->backend_device_removed = fu_fibocom_plugin_backend_device_removed;
}
