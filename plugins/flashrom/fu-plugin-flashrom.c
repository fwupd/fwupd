/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 9elements Agency GmbH <patrick.rudolph@9elements.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <string.h>

#include "fu-plugin-vfuncs.h"
#include "fu-flashrom-device.h"

#include <libflashrom.h>

#define SELFCHECK_TRUE 1

struct FuPluginData {
	gsize				 flash_size;
	struct flashrom_flashctx	*flashctx;
	struct flashrom_layout		*layout;
	struct flashrom_programmer	*flashprog;
};

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_METADATA_SOURCE, "linux_lockdown");
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_CONFLICTS, "coreboot"); /* obsoleted */
	fu_plugin_add_flag (plugin, FWUPD_PLUGIN_FLAG_REQUIRE_HWID);
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	flashrom_layout_release (data->layout);
	flashrom_programmer_shutdown (data->flashprog);
	flashrom_flash_release (data->flashctx);
}

static int
fu_plugin_flashrom_debug_cb (enum flashrom_log_level lvl, const char *fmt, va_list args)
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
	g_autofree gchar *tmp = g_strdup_vprintf (fmt, args);
#pragma clang diagnostic pop
	g_autofree gchar *str = fu_common_strstrip (tmp);
	if (g_strcmp0 (str, "OK.") == 0 || g_strcmp0 (str, ".") == 0)
		return 0;
	switch (lvl) {
	case FLASHROM_MSG_ERROR:
	case FLASHROM_MSG_WARN:
		g_warning ("%s", str);
		break;
	case FLASHROM_MSG_INFO:
		g_debug ("%s", str);
		break;
	case FLASHROM_MSG_DEBUG:
	case FLASHROM_MSG_DEBUG2:
		if (g_getenv ("FWUPD_FLASHROM_VERBOSE") != NULL)
			g_debug ("%s", str);
		break;
	case FLASHROM_MSG_SPEW:
		break;
	default:
		break;
	}
	return 0;
}

static void
fu_plugin_flashrom_device_set_version (FuPlugin *plugin, FuDevice *device)
{
	const gchar *version;
	const gchar *version_major;
	const gchar *version_minor;

	/* as-is */
	version = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_BIOS_VERSION);
	if (version != NULL) {
		/* some Lenovo hardware requires a specific prefix for the EC,
		 * so strip it before we use ensure-semver */
		if (strlen (version) > 9 && g_str_has_prefix (version, "CBET"))
			version += 9;

		/* this may not "stick" if there are no numeric chars */
		fu_device_set_version (device, version);
		if (fu_device_get_version (device) != NULL)
			return;
	}

	/* component parts only */
	version_major = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_BIOS_MAJOR_RELEASE);
	version_minor = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_BIOS_MINOR_RELEASE);
	if (version_major != NULL && version_minor != NULL) {
		g_autofree gchar *tmp = g_strdup_printf ("%s.%s.0",
							 version_major,
							 version_minor);
		fu_device_set_version (device, tmp);
		return;
	}
}
static void
fu_plugin_flashrom_device_set_bios_info (FuPlugin *plugin, FuDevice *device)
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
fu_plugin_flashrom_device_set_hwids (FuPlugin *plugin, FuDevice *device)
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

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	const gchar *dmi_vendor;
	gint rc;
	g_autoptr(FuDevice) device = fu_flashrom_device_new ();

	fu_device_set_quirks (device, fu_plugin_get_quirks (plugin));
	fu_device_set_name (device, fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_PRODUCT_NAME));
	fu_device_set_vendor (device, fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_MANUFACTURER));

	/* use same VendorID logic as with UEFI */
	dmi_vendor = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_BIOS_VENDOR);
	if (dmi_vendor != NULL) {
		g_autofree gchar *vendor_id = g_strdup_printf ("DMI:%s", dmi_vendor);
		fu_device_add_vendor_id (FU_DEVICE (device), vendor_id);
	}
	fu_plugin_flashrom_device_set_version (plugin, device);
	fu_plugin_flashrom_device_set_hwids (plugin, device);
	fu_plugin_flashrom_device_set_bios_info (plugin, device);
	if (!fu_device_setup (device, error))
		return FALSE;

	/* actually probe hardware to check for support */
	if (flashrom_init (SELFCHECK_TRUE)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "flashrom initialization error");
		return FALSE;
	}
	flashrom_set_log_callback (fu_plugin_flashrom_debug_cb);
	if (flashrom_programmer_init (&data->flashprog, "internal", NULL)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "programmer initialization failed");
		return FALSE;
	}
	rc = flashrom_flash_probe (&data->flashctx, data->flashprog, NULL);
	if (rc == 3) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "flash probe failed: multiple chips were found");
		return FALSE;
	}
	if (rc == 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "flash probe failed: no chip was found");
		return FALSE;
	}
	if (rc != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "flash probe failed: unknown error");
		return FALSE;
	}
	data->flash_size = flashrom_flash_getsize (data->flashctx);
	if (data->flash_size == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "flash size zero");
		return FALSE;
	}

	/* success */
	fu_plugin_device_add (plugin, device);
	fu_plugin_cache_add (plugin, fu_device_get_id (device), device);
	return TRUE;
}

gboolean
fu_plugin_update_prepare (FuPlugin *plugin,
			  FwupdInstallFlags flags,
			  FuDevice *device,
			  GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autofree gchar *firmware_orig = NULL;
	g_autofree gchar *basename = NULL;

	/* not us */
	if (fu_plugin_cache_lookup (plugin, fu_device_get_id (device)) == NULL)
		return TRUE;

	/* if the original firmware doesn't exist, grab it now */
	basename = g_strdup_printf ("flashrom-%s.bin", fu_device_get_id (device));
	firmware_orig = g_build_filename (FWUPD_LOCALSTATEDIR, "lib", "fwupd",
					  "builder", basename, NULL);
	if (!fu_common_mkdir_parent (firmware_orig, error))
		return FALSE;
	if (!g_file_test (firmware_orig, G_FILE_TEST_EXISTS)) {
		g_autofree guint8 *newcontents = g_malloc0 (data->flash_size);
		g_autoptr(GBytes) buf = NULL;

		fu_device_set_status (device, FWUPD_STATUS_DEVICE_READ);
		if (flashrom_image_read (data->flashctx, newcontents, data->flash_size)) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_READ,
					     "failed to back up original firmware");
			return FALSE;
		}
		buf = g_bytes_new_static (newcontents, data->flash_size);
		if (!fu_common_set_contents_bytes (firmware_orig, buf, error))
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
	FuPluginData *data = fu_plugin_get_data (plugin);
	gsize sz = 0;
	gint rc;
	const guint8 *buf = g_bytes_get_data (blob_fw, &sz);

	if (flashrom_layout_read_from_ifd (&data->layout, data->flashctx, NULL, 0)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "failed to read layout from Intel ICH descriptor");
		return FALSE;
	}

	/* include bios region for safety reasons */
	if (flashrom_layout_include_region (data->layout, "bios")) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "invalid region name");
		return FALSE;
	}

	/* write region */
	flashrom_layout_set (data->flashctx, data->layout);
	if (sz != data->flash_size) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "invalid image size 0x%x, expected 0x%x",
			     (guint) sz, (guint) data->flash_size);
		return FALSE;
	}

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	fu_device_set_progress (device, 0); /* urgh */
	rc = flashrom_image_write (data->flashctx, (void *) buf, sz, NULL /* refbuffer */);
	if (rc != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "image write failed, err=%i", rc);
		return FALSE;
	}

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_VERIFY);
	if (flashrom_image_verify (data->flashctx, (void *) buf, sz)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "image verify failed");
		return FALSE;
	}

	/* success */
	return TRUE;
}
