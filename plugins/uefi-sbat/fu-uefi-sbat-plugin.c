/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-sbat-device.h"
#include "fu-uefi-sbat-firmware.h"
#include "fu-uefi-sbat-plugin.h"

struct _FuUefiSbatPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuUefiSbatPlugin, fu_uefi_sbat_plugin, FU_TYPE_PLUGIN)

static void
fu_uefi_sbat_plugin_init(FuUefiSbatPlugin *self)
{
}

static gboolean
fu_uefi_sbat_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	gboolean secureboot_enabled = FALSE;

	if (!fu_efivars_get_secure_boot(efivars, &secureboot_enabled, error))
		return FALSE;
	if (!secureboot_enabled) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "SecureBoot is not enabled");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_uefi_sbat_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuUefiSbatDevice) device = NULL;
	g_autoptr(GBytes) blob = NULL;

	blob = fu_efivars_get_data_bytes(efivars, FU_EFIVARS_GUID_SHIM, "SbatLevelRT", NULL, error);
	if (blob == NULL)
		return FALSE;
	device = fu_uefi_sbat_device_new(ctx, blob, error);
	if (device == NULL)
		return FALSE;
	locker = fu_device_locker_new(FU_DEVICE(device), error);
	if (locker == NULL)
		return FALSE;
	fu_plugin_device_add(plugin, FU_DEVICE(device));

	/* success */
	return TRUE;
}

static gboolean
fu_uefi_sbat_plugin_reboot_cleanup(FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(GPtrArray) esp_files = NULL;

	/* delete any revocations that have been processed */
	esp_files =
	    fu_context_get_esp_files(ctx, FU_CONTEXT_ESP_FILE_FLAG_INCLUDE_REVOCATIONS, error);
	if (esp_files == NULL)
		return FALSE;
	for (guint i = 0; i < esp_files->len; i++) {
		FuFirmware *firmware = g_ptr_array_index(esp_files, i);
		g_autoptr(GFile) file = g_file_new_for_path(fu_firmware_get_filename(firmware));
		if (g_file_query_exists(file, NULL)) {
			g_debug("deleting %s", fu_firmware_get_filename(firmware));
			if (!g_file_delete(file, NULL, error))
				return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static void
fu_uefi_sbat_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_set_device_gtype_default(plugin, FU_TYPE_UEFI_SBAT_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_UEFI_SBAT_FIRMWARE);
}

static void
fu_uefi_sbat_plugin_class_init(FuUefiSbatPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_uefi_sbat_plugin_constructed;
	plugin_class->startup = fu_uefi_sbat_plugin_startup;
	plugin_class->coldplug = fu_uefi_sbat_plugin_coldplug;
	plugin_class->reboot_cleanup = fu_uefi_sbat_plugin_reboot_cleanup;
}
