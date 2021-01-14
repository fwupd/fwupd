/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 9elements Agency GmbH <patrick.rudolph@9elements.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
}

static void
fu_plugin_coreboot_device_set_bios_info (FuPlugin *plugin, FuDevice *device)
{
	const guint8 *buf;
	gsize bufsz;
	guint32 bios_char = 0x0;
	guint8 bios_sz = 0x0;
	g_autoptr(GBytes) bios_table = NULL;

	/* get SMBIOS info */
	bios_table = fu_plugin_get_smbios_data (plugin, FU_SMBIOS_STRUCTURE_TYPE_BIOS);
	if (bios_table == NULL)
		return;

	/* ROM size */
	buf = g_bytes_get_data (bios_table, &bufsz);
	if (fu_common_read_uint8_safe (buf, bufsz, 0x9, &bios_sz, NULL)) {
		guint64 firmware_size = (bios_sz + 1) * 64 * 1024;
		fu_device_set_firmware_size_max (device, firmware_size);
	}

	/* BIOS characteristics */
	if (fu_common_read_uint32_safe (buf, bufsz, 0xa, &bios_char, G_LITTLE_ENDIAN, NULL)) {
		if ((bios_char & (1 << 11)) == 0)
			fu_device_remove_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	}
}

static void
fu_plugin_coreboot_device_set_hwids (FuPlugin *plugin, FuDevice *device)
{
	static const gchar *hwids[] = {
		"HardwareID-3",
		"HardwareID-4",
		"HardwareID-5",
		"HardwareID-6",
		"HardwareID-10",
		/* a more useful one for coreboot branch detection */
		FU_HWIDS_KEY_MANUFACTURER "&"
		FU_HWIDS_KEY_FAMILY "&"
		FU_HWIDS_KEY_PRODUCT_NAME "&"
		FU_HWIDS_KEY_PRODUCT_SKU "&"
		FU_HWIDS_KEY_BIOS_VENDOR,
	};
	/* don't include FU_HWIDS_KEY_BIOS_VERSION */
	for (guint i = 0; i < G_N_ELEMENTS (hwids); i++) {
		g_autofree gchar *str = NULL;
		str = fu_plugin_get_hwid_replace_value (plugin, hwids[i], NULL);
		if (str != NULL)
			fu_device_add_instance_id (device, str);
	}
}

void
fu_plugin_device_registered (FuPlugin *plugin, FuDevice *device)
{
	if (g_strcmp0 (fu_device_get_plugin (device), "flashrom") != 0)
		return;
	fu_plugin_coreboot_device_set_hwids (plugin, device);
	fu_plugin_coreboot_device_set_bios_info (plugin, device);
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	const gchar *vendor;

	vendor = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_BIOS_VENDOR);
	if (g_strcmp0 (vendor, "coreboot") != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "No coreboot detected on this machine.");
		return FALSE;
	}

	return TRUE;
}
