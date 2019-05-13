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
#include "fu-device-metadata.h"
#include "fu-device-private.h"
#include "fu-plugin-coreboot.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_SUPPORTS_PROTOCOL, "org.coreboot.fmap");

	/* make sure that flashrom plugin is ready to receive devices */
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_RUN_AFTER, "org.flashrom");
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	g_autofree const gchar *triplet = NULL;
	g_autofree gchar *name = NULL;
	const gchar *major;
	const gchar *minor;
	const gchar *vendor;
	const gchar *version;
	g_autoptr(FuDevice) dev;
	GBytes *bios_table;
	gboolean updatable = TRUE;
	gint firmware_size = 0;
	struct fmap *fmap;
	gboolean ret;

	vendor = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_BIOS_VENDOR);
	if (!vendor) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_FOUND,
			     "Failed to get DMI value FU_HWIDS_KEY_BIOS_VENDOR");
		return FALSE;
	}

	version = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_BIOS_VERSION);
	if (version) {
		triplet = fu_plugin_coreboot_version_string_to_triplet (version, error);
		/* Ignore developer builds */
		if (g_strrstr (version, "dirty"))
			updatable = FALSE;
	}

	if (!version || !triplet) {
		major = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_BIOS_MAJOR_RELEASE);
		if (!major) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_FOUND,
				     "Missing BIOS major release");
			return FALSE;
		}
		minor = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_BIOS_MINOR_RELEASE);
		if (!minor) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_FOUND,
				     "Missing BIOS minor release");
			return FALSE;
		}
		triplet = g_strdup_printf ("%s.%s.0", major, minor);
	}

	if (!triplet) {
		g_set_error (error,
                	     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "No version string found");

		return FALSE;
	}
	dev = fu_device_new ();

	fu_device_set_version (dev, triplet, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_summary (dev, "Open Source system boot firmware");
	fu_device_set_id (dev, vendor);
	fu_device_set_vendor (dev, vendor);
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_icon (dev, "computer");
        name = fu_plugin_coreboot_get_name_for_type (plugin, NULL);
	if (!name)
		name = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_PRODUCT_NAME);
	fu_device_set_name (dev, name);
	fu_device_set_vendor (dev, fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_MANUFACTURER));
	fu_device_add_instance_id (dev, "main-system-firmware");

	fu_device_set_metadata (dev, FU_DEVICE_METADATA_FLASHROM_DEVICE_KIND, "system-firmware");

	bios_table = fu_plugin_get_smbios_data (plugin, FU_SMBIOS_STRUCTURE_TYPE_BIOS);
	if (bios_table) {
		guint32 bios_characteristics;
		gsize len;
		const guint8 *value = g_bytes_get_data (bios_table, &len);
		if (len >= 0x9) {
			firmware_size = (value[0x9] + 1) * 64 * 1024;
		}
		if (len >= (0xa + sizeof(guint32))) {
			memcpy (&bios_characteristics, &value[0xa], sizeof (guint32));
			if (!(bios_characteristics & (1 << 11))) // "BIOS is upgradeable (Flash)"
				updatable = FALSE;
		}
	}

	if (updatable)
		fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE);
	if (firmware_size)
		fu_device_set_firmware_size_max (dev, firmware_size);

	// Convert instances to GUID
	fu_device_convert_instance_ids (dev);

	// register with flashrom
	fu_plugin_device_register (plugin, dev);

	// now find subregions
	fmap = fu_plugin_coreboot_find_fmap (plugin, error);
	if (!fmap)
		return TRUE;

	ret = fu_plugin_coreboot_add_fmap_devices (plugin, error, dev, fmap);

	g_free(fmap);
	return ret;
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	const gchar *vendor;

	vendor = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_BIOS_VENDOR);
	if (!vendor) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_FOUND,
			     "Failed to get DMI value FU_HWIDS_KEY_BIOS_VENDOR");
		return FALSE;
	}

	if (!vendor)
		return FALSE;

	if (g_strcmp0 (vendor, "coreboot") != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "No coreboot detected on this machine.");
		return FALSE;
	}

	return TRUE;
}

