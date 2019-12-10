/*
 * Copyright (C) 2019 9elements Agency GmbH <patrick.rudolph@9elements.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include <stdio.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"
#include "fu-device-metadata.h"
#include "fu-device-private.h"
#include "fu-plugin-coreboot.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	const gchar *major;
	const gchar *minor;
	const gchar *version;
	GBytes *bios_table;
	gboolean updatable = FALSE; /* TODO: Implement update support */
	g_autofree gchar *name = NULL;
	g_autofree gchar *triplet = NULL;
	g_autoptr(FuDevice) dev = NULL;

	/* don't include FU_HWIDS_KEY_BIOS_VERSION */
	static const gchar *hwids[] = {
		"HardwareID-3",
		"HardwareID-4",
		"HardwareID-5",
		"HardwareID-6",
		"HardwareID-10",
	};

	version = fu_plugin_coreboot_get_version_string (plugin);
	if (version != NULL)
		triplet = fu_plugin_coreboot_version_string_to_triplet (version, error);

	if (version == NULL || triplet == NULL) {
		major = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_BIOS_MAJOR_RELEASE);
		if (major == NULL) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_FOUND,
				     "Missing BIOS major release");
			return FALSE;
		}
		minor = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_BIOS_MINOR_RELEASE);
		if (minor == NULL) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_FOUND,
				     "Missing BIOS minor release");
			return FALSE;
		}
		triplet = g_strdup_printf ("%s.%s.0", major, minor);
	}

	if (triplet == NULL) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "No version string found");

		return FALSE;
	}
	dev = fu_device_new ();

	fu_device_set_version (dev, triplet, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_summary (dev, "Open Source system boot firmware");
	fu_device_set_id (dev, "coreboot");
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_icon (dev, "computer");
	name = fu_plugin_coreboot_get_name_for_type (plugin, NULL);
	if (name != NULL) {
		fu_device_set_name (dev, name);
	} else {
		fu_device_set_name (dev, fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_PRODUCT_NAME));
	}
	fu_device_set_vendor (dev, fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_MANUFACTURER));
	fu_device_add_instance_id (dev, "main-system-firmware");
	fu_device_set_vendor_id (dev, "DMI:coreboot");

	for (guint i = 0; i < G_N_ELEMENTS (hwids); i++) {
		char *str;
		str = fu_plugin_get_hwid_replace_value (plugin, hwids[i], NULL);
		if (str != NULL)
			fu_device_add_instance_id (dev, str);
	}

	bios_table = fu_plugin_get_smbios_data (plugin, FU_SMBIOS_STRUCTURE_TYPE_BIOS);
	if (bios_table != NULL) {
		guint32 bios_characteristics;
		gsize len;
		const guint8 *value = g_bytes_get_data (bios_table, &len);
		if (len >= 0x9) {
			gint firmware_size = (value[0x9] + 1) * 64 * 1024;
			fu_device_set_firmware_size_max (dev, firmware_size);
		}
		if (len >= (0xa + sizeof(guint32))) {
			memcpy (&bios_characteristics, &value[0xa], sizeof (guint32));
			/* Read the "BIOS is upgradeable (Flash)" flag */
			if (!(bios_characteristics & (1 << 11)))
				updatable = FALSE;
		}
	}

	if (updatable)
		fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE);

	/* convert instances to GUID */
	fu_device_convert_instance_ids (dev);

	fu_plugin_device_add (plugin, dev);

	return TRUE;
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

