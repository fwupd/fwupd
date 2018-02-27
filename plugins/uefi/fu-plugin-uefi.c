/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
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

#include <appstream-glib.h>
#include <fwup.h>
#include <fcntl.h>
#include <glib/gi18n.h>

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

struct FuPluginData {
	gboolean	ux_capsule;
	gchar		*esp_path;
};

/* drop when upgrading minimum required version of efivar to 33 */
#if !defined (efi_guid_ux_capsule)
#define efi_guid_ux_capsule EFI_GUID(0x3b8c8162,0x188c,0x46a4,0xaec9,0xbe,0x43,0xf1,0xd6,0x56,0x97)
#endif

void
fu_plugin_init (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	data->ux_capsule = FALSE;
	data->esp_path = NULL;
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_RUN_AFTER, "upower");
	fu_plugin_add_report_metadata (plugin, "FwupdateVersion", LIBFWUP_LIBRARY_VERSION);
	fu_plugin_add_report_metadata (plugin, "EfivarVersion", EFIVAR_LIBRARY_VERSION);
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
        FuPluginData *data = fu_plugin_get_data (plugin);
        g_free (data->esp_path);
}

static gchar *
fu_plugin_uefi_guid_to_string (efi_guid_t *guid_raw)
{
	g_autofree gchar *guid = g_strdup ("00000000-0000-0000-0000-000000000000");
	if (efi_guid_to_str (guid_raw, &guid) < 0)
		return NULL;
	return g_steal_pointer (&guid);
}

static fwup_resource *
fu_plugin_uefi_find (fwup_resource_iter *iter, const gchar *guid_str, GError **error)
{
	efi_guid_t *guid_raw;
	fwup_resource *re_matched = NULL;
	fwup_resource *re = NULL;

	/* get the hardware we're referencing */
	while (fwup_resource_iter_next (iter, &re) > 0) {
		g_autofree gchar *guid_tmp = NULL;

		/* convert to strings */
		fwup_get_guid (re, &guid_raw);
		guid_tmp = fu_plugin_uefi_guid_to_string (guid_raw);
		if (guid_tmp == NULL) {
			g_warning ("failed to convert guid to string");
			continue;
		}

		/* FIXME: also match hardware_instance too */
		if (g_strcmp0 (guid_str, guid_tmp) == 0) {
			re_matched = re;
			break;
		}
	}

	/* paradoxically, no hardware matched */
	if (re_matched == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "No UEFI firmware matched %s",
			     guid_str);
	}

	return re_matched;
}

static void
_fwup_resource_iter_free (fwup_resource_iter *iter)
{
	fwup_resource_iter_destroy (&iter);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(fwup_resource_iter, _fwup_resource_iter_free);

gboolean
fu_plugin_clear_results (FuPlugin *plugin, FuDevice *device, GError **error)
{
	fwup_resource *re = NULL;
	g_autoptr(fwup_resource_iter) iter = NULL;

	/* get the hardware we're referencing */
	fwup_resource_iter_create (&iter);
	re = fu_plugin_uefi_find (iter, fu_device_get_guid_default (device), error);
	if (re == NULL)
		return FALSE;
	if (fwup_clear_status (re) < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Cannot create clear UEFI status for %s",
			     fu_device_get_guid_default (device));
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_get_results (FuPlugin *plugin, FuDevice *device, GError **error)
{
	const gchar *tmp;
	fwup_resource *re = NULL;
	guint32 status = 0;
	guint32 version = 0;
	time_t when = 0;
	g_autoptr(fwup_resource_iter) iter = NULL;

	/* get the hardware we're referencing */
	fwup_resource_iter_create (&iter);
	re = fu_plugin_uefi_find (iter, fu_device_get_guid_default (device), error);
	if (re == NULL)
		return FALSE;
	if (fwup_get_last_attempt_info (re, &version, &status, &when) < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Cannot get UEFI status for %s",
			     fu_device_get_guid_default (device));
		return FALSE;
	}
	if (status == FWUP_LAST_ATTEMPT_STATUS_SUCCESS) {
		fu_device_set_update_state (device, FWUPD_UPDATE_STATE_SUCCESS);
	} else {
		g_autofree gchar *err_msg = NULL;
		g_autofree gchar *version_str = g_strdup_printf ("%u", version);
		fu_device_set_update_state (device, FWUPD_UPDATE_STATE_FAILED);
		tmp = fwup_last_attempt_status_to_string (status);
		if (tmp == NULL) {
			err_msg = g_strdup_printf ("failed to update to %s",
						   version_str);
		} else {
			err_msg = g_strdup_printf ("failed to update to %s: %s",
						   version_str, tmp);
		}
		fu_device_set_update_error (device, err_msg);
	}
	return TRUE;
}

static gboolean
fu_plugin_uefi_update_resource (fwup_resource *re,
				guint64 hardware_instance,
				GBytes *blob,
				GError **error)
{
	int rc;
	rc = fwup_set_up_update_with_buf (re, hardware_instance,
					  g_bytes_get_data (blob, NULL),
					  g_bytes_get_size (blob));
	if (rc < 0) {
		g_autoptr(GString) str = g_string_new (NULL);
		rc = 1;
		for (int i = 0; rc > 0; i++) {
			char *filename = NULL;
			char *function = NULL;
			char *message = NULL;
			int line = 0;
			int err = 0;

			rc = efi_error_get (i, &filename, &function, &line,
					    &message, &err);
			if (rc <= 0)
				break;
			g_string_append_printf (str, "{error #%d} %s:%d %s(): %s: %s\t",
						i, filename, line, function,
						message, strerror (err));
		}
		if (str->len > 1)
			g_string_truncate (str, str->len - 1);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "UEFI firmware update failed: %s",
			     str->str);
		return FALSE;
	}
	return TRUE;
}

static GBytes *
fu_plugin_uefi_get_splash_data (guint width, guint height, GError **error)
{
	const gchar * const *langs = g_get_language_names ();
	const gchar *localedir = LOCALEDIR;
	const gsize chunk_size = 1024 * 1024;
	gsize buf_idx = 0;
	gsize buf_sz = chunk_size;
	gssize len;
	g_autofree gchar *basename = NULL;
	g_autofree guint8 *buf = NULL;
	g_autoptr(GBytes) compressed_data = NULL;
	g_autoptr(GConverter) conv = NULL;
	g_autoptr(GInputStream) stream_compressed = NULL;
	g_autoptr(GInputStream) stream_raw = NULL;

	/* ensure this is sane */
	if (!g_str_has_prefix (localedir, "/"))
		localedir = "/usr/share/locale";

	/* find the closest locale match, falling back to `en` and `C` */
	basename = g_strdup_printf ("fwupd-%u-%u.bmp.gz", width, height);
	for (guint i = 0; langs[i] != NULL; i++) {
		g_autofree gchar *fn = NULL;
		if (g_str_has_suffix (langs[i], ".UTF-8"))
			continue;
		fn = g_build_filename (localedir, langs[i],
				       "LC_IMAGES", basename, NULL);
		if (g_file_test (fn, G_FILE_TEST_EXISTS)) {
			compressed_data = fu_common_get_contents_bytes (fn, error);
			if (compressed_data == NULL)
				return NULL;
			break;
		}
		g_debug ("no %s found", fn);
	}

	/* we found nothing */
	if (compressed_data == NULL) {
		g_autofree gchar *tmp = g_strjoinv (",", (gchar **) langs);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "failed to get splash file for %s in %s",
			     tmp, localedir);
		return NULL;
	}

	/* decompress data */
	stream_compressed = g_memory_input_stream_new_from_bytes (compressed_data);
	conv = G_CONVERTER (g_zlib_decompressor_new (G_ZLIB_COMPRESSOR_FORMAT_GZIP));
	stream_raw = g_converter_input_stream_new (stream_compressed, conv);
	buf = g_malloc0 (buf_sz);
	while ((len = g_input_stream_read (stream_raw,
					   buf + buf_idx,
					   buf_sz - buf_idx,
					   NULL, error)) > 0) {
		buf_idx += len;
		if (buf_sz - buf_idx < chunk_size) {
			buf_sz += chunk_size;
			buf = g_realloc (buf, buf_sz);
		}
	}
	if (len < 0) {
		g_prefix_error (error, "failed to decompress file: ");
		return NULL;
	}
	g_debug ("decompressed image to %" G_GSIZE_FORMAT "kb", buf_idx / 1024);
	return g_bytes_new_take (g_steal_pointer (&buf), buf_idx);
}

static gboolean
fu_plugin_uefi_update_splash (GError **error)
{
	fwup_resource *re = NULL;
	guint best_idx = G_MAXUINT;
	guint32 lowest_border_pixels = G_MAXUINT;
#ifdef HAVE_FWUP_GET_BGRT_INFO
	int rc;
#endif
	guint32 screen_height = 768;
	guint32 screen_width = 1024;
	g_autoptr(fwup_resource_iter) iter = NULL;
	g_autoptr(GBytes) image_bmp = NULL;

	struct {
		guint32	 width;
		guint32	 height;
	} sizes[] = {
		{ 640, 480 },	/* matching the sizes in po/make-images */
		{ 800, 600 },
		{ 1024, 768 },
		{ 1920, 1080 },
		{ 3840, 2160 },
		{ 5120, 2880 },
		{ 5688, 3200 },
		{ 7680, 4320 },
		{ 0, 0 }
	};

	/* get the boot graphics resource table data */
#ifdef HAVE_FWUP_GET_BGRT_INFO
	rc = fwup_get_ux_capsule_info (&screen_width, &screen_height);
	if (rc < 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "failed to get BGRT screen size");
		return FALSE;
	}
	g_debug ("BGRT screen size %" G_GUINT32_FORMAT " x%" G_GUINT32_FORMAT,
		 screen_width, screen_height);
#endif

	/* find the 'best sized' pre-generated image */
	for (guint i = 0; sizes[i].width != 0; i++) {
		guint32 border_pixels;

		/* disregard any images that are bigger than the screen */
		if (sizes[i].width > screen_width)
			continue;
		if (sizes[i].height > screen_height)
			continue;

		/* is this the best fit for the display */
		border_pixels = (screen_width * screen_height) -
				(sizes[i].width * sizes[i].height);
		if (border_pixels < lowest_border_pixels) {
			lowest_border_pixels = border_pixels;
			best_idx = i;
		}
	}
	if (best_idx == G_MAXUINT) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "failed to find a suitable image to use");
		return FALSE;
	}

	/* get the raw data */
	image_bmp = fu_plugin_uefi_get_splash_data (sizes[best_idx].width,
						    sizes[best_idx].height,
						    error);
	if (image_bmp == NULL)
		return FALSE;

	/* perform the upload */
	return fu_plugin_uefi_update_resource (re, 0, image_bmp, error);
}

gboolean
fu_plugin_update (FuPlugin *plugin,
		  FuDevice *device,
		  GBytes *blob_fw,
		  FwupdInstallFlags flags,
		  GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	fwup_resource *re = NULL;
	guint64 hardware_instance = 0;	/* FIXME */
	g_autoptr(fwup_resource_iter) iter = NULL;
	const gchar *str;
	g_autofree gchar *efibootmgr_path = NULL;
	g_autofree gchar *boot_variables = NULL;
	g_autoptr(GError) error_splash = NULL;

	/* get the hardware we're referencing */
	fwup_resource_iter_create (&iter);
	re = fu_plugin_uefi_find (iter, fu_device_get_guid_default (device), error);
	if (re == NULL)
		return FALSE;

	/* TRANSLATORS: this is shown when updating the firmware after the reboot */
	str = _("Installing firmware update…");
	g_assert (str != NULL);

	/* perform the update */
	g_debug ("Performing UEFI capsule update");
	fu_device_set_status (device, FWUPD_STATUS_SCHEDULING);

#ifdef HAVE_FWUP_CUSTOM_ESP
	if (data->esp_path != NULL)
		fwup_set_esp_mountpoint (data->esp_path);
#endif
	if (data->ux_capsule) {
		if (!fu_plugin_uefi_update_splash (&error_splash)) {
			g_warning ("failed to upload UEFI UX capsule text: %s",
				   error_splash->message);
		}
	}
	if (!fu_plugin_uefi_update_resource (re, hardware_instance, blob_fw, error))
		return FALSE;

	/* record boot information to system log for future debugging */
	efibootmgr_path = g_find_program_in_path ("efibootmgr");
	if (efibootmgr_path != NULL) {
		if (!g_spawn_command_line_sync ("efibootmgr -v",
						&boot_variables, NULL, NULL, error))
			return FALSE;
		g_message ("Boot Information:\n%s", boot_variables);
	}

	return TRUE;
}

static AsVersionParseFlag
fu_plugin_uefi_get_version_format_for_type (FuPlugin *plugin, guint32 uefi_type)
{
	const gchar *content;
	const gchar *quirk;

	/* we have no information for devices */
	if (uefi_type == FWUP_RESOURCE_TYPE_DEVICE_FIRMWARE)
		return AS_VERSION_PARSE_FLAG_USE_TRIPLET;

	content = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_MANUFACTURER);
	if (content == NULL)
		return AS_VERSION_PARSE_FLAG_USE_TRIPLET;

	/* any quirks match */
	quirk = fu_plugin_lookup_quirk_by_id (plugin,
					      FU_QUIRKS_UEFI_VERSION_FORMAT,
					      content);
	if (g_strcmp0 (quirk, "none") == 0)
		return AS_VERSION_PARSE_FLAG_NONE;

	/* fall back */
	return AS_VERSION_PARSE_FLAG_USE_TRIPLET;
}

gboolean
fu_plugin_unlock (FuPlugin *plugin,
			 FuDevice *device,
			 GError **error)
{
	gint rc;
	g_debug ("unlocking UEFI device %s", fu_device_get_id (device));
	rc = fwup_enable_esrt();
	if (rc <= 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "failed to unlock UEFI device");
		return FALSE;
	} else if (rc == 1)
		g_debug ("UEFI device is already unlocked");
	else if (rc == 2)
		g_debug ("Successfully unlocked UEFI device");
	else if (rc == 3)
		g_debug ("UEFI device will be unlocked on next reboot");
	return TRUE;
}

static const gchar *
fu_plugin_uefi_uefi_type_to_string (guint32 uefi_type)
{
	if (uefi_type == FWUP_RESOURCE_TYPE_UNKNOWN)
		return "Unknown Firmware";
	if (uefi_type == FWUP_RESOURCE_TYPE_SYSTEM_FIRMWARE)
		return "System Firmware";
	if (uefi_type == FWUP_RESOURCE_TYPE_DEVICE_FIRMWARE)
		return "Device Firmware";
	if (uefi_type == FWUP_RESOURCE_TYPE_UEFI_DRIVER)
		return "UEFI Driver";
	if (uefi_type == FWUP_RESOURCE_TYPE_FMP)
		return "Firmware Management Protocol";
	return NULL;
}

static gchar *
fu_plugin_uefi_get_name_for_type (FuPlugin *plugin, guint32 uefi_type)
{
	GString *display_name;

	/* set Display Name prefix for capsules that are not PCI cards */
	display_name = g_string_new (fu_plugin_uefi_uefi_type_to_string (uefi_type));
	if (uefi_type == FWUP_RESOURCE_TYPE_DEVICE_FIRMWARE) {
		g_string_prepend (display_name, "UEFI ");
	} else {
		const gchar *tmp;
		tmp = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_PRODUCT_NAME);
		if (tmp != NULL && tmp[0] != '\0') {
			g_string_prepend (display_name, " ");
			g_string_prepend (display_name, tmp);
		}
	}
	return g_string_free (display_name, FALSE);
}

static void
fu_plugin_uefi_coldplug_resource (FuPlugin *plugin, fwup_resource *re)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	AsVersionParseFlag parse_flags;
	efi_guid_t *guid_raw;
	guint32 uefi_type;
	guint32 version_raw;
	guint64 hardware_instance = 0;	/* FIXME */
	g_autofree gchar *guid = NULL;
	g_autofree gchar *id = NULL;
	g_autofree gchar *name = NULL;
	g_autofree gchar *version_lowest = NULL;
	g_autofree gchar *version = NULL;
	g_autoptr(FuDevice) dev = NULL;

	/* detect the fake GUID used for uploading the image */
	fwup_get_guid (re, &guid_raw);
	if (efi_guid_cmp (guid_raw, &efi_guid_ux_capsule) == 0) {
		data->ux_capsule = TRUE;
		return;
	}

	/* convert to strings */
	guid = fu_plugin_uefi_guid_to_string (guid_raw);
	if (guid == NULL) {
		g_warning ("failed to convert guid to string");
		return;
	}

	fwup_get_fw_type (re, &uefi_type);
	parse_flags = fu_plugin_uefi_get_version_format_for_type (plugin, uefi_type);
	fwup_get_fw_version (re, &version_raw);
	version = as_utils_version_from_uint32 (version_raw, parse_flags);
	id = g_strdup_printf ("UEFI-%s-dev%" G_GUINT64_FORMAT,
			      guid, hardware_instance);

	dev = fu_device_new ();
	if (uefi_type == FWUP_RESOURCE_TYPE_DEVICE_FIRMWARE) {
		/* nothing better in the icon naming spec */
		fu_device_add_icon (dev, "audio-card");
	} else {
		/* this is probably system firmware */
		fu_device_add_icon (dev, "computer");
	}
	fu_device_set_id (dev, id);
	fu_device_add_guid (dev, guid);
	fu_device_set_version (dev, version);
	name = fu_plugin_uefi_get_name_for_type (plugin, uefi_type);
	if (name != NULL)
		fu_device_set_name (dev, name);
	fwup_get_lowest_supported_fw_version (re, &version_raw);
	if (version_raw != 0) {
		version_lowest = as_utils_version_from_uint32 (version_raw,
							       parse_flags);
		fu_device_set_version_lowest (dev, version_lowest);
	}
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_INTERNAL);
	if (g_file_test ("/sys/firmware/efi/efivars", G_FILE_TEST_IS_DIR) ||
	    g_file_test ("/sys/firmware/efi/vars", G_FILE_TEST_IS_DIR)) {
		fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	} else {
		g_warning ("Kernel support for EFI variables missing");
	}
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_plugin_device_add (plugin, dev);
}

static void
fu_plugin_uefi_test_secure_boot (FuPlugin *plugin)
{
	const efi_guid_t guid = EFI_GLOBAL_GUID;
	const gchar *result_str = "Disabled";
	g_autofree guint8 *data = NULL;
	gsize data_size = 0;
	guint32 attributes = 0;
	gint rc;

	rc = efi_get_variable (guid, "SecureBoot", &data, &data_size, &attributes);
	if (rc < 0)
		return;
	if (data_size >= 1 && data[0] & 1)
		result_str = "Enabled";

	g_debug ("SecureBoot is: %s", result_str);
	fu_plugin_add_report_metadata (plugin, "SecureBoot", result_str);
}

static gboolean load_custom_esp (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	const gchar *key = "OverrideESPMountPoint";

	data->esp_path = fu_plugin_get_config_value (plugin, key);
	if (data->esp_path != NULL) {
		if (!g_file_test (data->esp_path, G_FILE_TEST_IS_DIR)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Invalid %s specified in %s config: %s",
				     fu_plugin_get_name (plugin), key,
				     data->esp_path);

			return FALSE;
		}
		g_debug ("%s set to %s", key, data->esp_path);
		fu_plugin_add_report_metadata (plugin, key,
					       data->esp_path);
	}

	return TRUE;
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	fwup_resource *re;
	gint supported;
	g_autoptr(fwup_resource_iter) iter = NULL;
	g_autofree gchar *name = NULL;
	const gchar *ux_capsule_str = "Disabled";

	/* supported = 0 : ESRT unspported
	   supported = 1 : unlocked, ESRT supported
	   supported = 2 : it is locked but can be unlocked to support ESRT
	   supported = 3 : it is locked, has been marked to be unlocked on next boot
			   calling unlock again is OK.
	 */
	supported = fwup_supported ();
	if (supported == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "UEFI firmware updating not supported");
		return FALSE;
	}

	if (supported == 2) {
		g_autoptr(FuDevice) dev = fu_device_new ();
		name = fu_plugin_uefi_get_name_for_type (plugin,
							 FWUP_RESOURCE_TYPE_SYSTEM_FIRMWARE);
		if (name != NULL)
			fu_device_set_name (dev, name);
		fu_device_set_id (dev, "UEFI-dummy-dev0");
		fu_device_add_guid (dev, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
		fu_device_set_version (dev, "0");
		fu_device_add_icon (dev, "computer");
		fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_LOCKED);
		fu_plugin_device_add (plugin, dev);
		return TRUE;
	}

	/* add each device */
	if (fwup_resource_iter_create (&iter) < 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Cannot create fwup iter");
		return FALSE;
	}
	while (fwup_resource_iter_next (iter, &re) > 0)
		fu_plugin_uefi_coldplug_resource (plugin, re);

	/* load any overriden options */
	if (!load_custom_esp (plugin, error))
		return FALSE;

	/* for debugging problems later */
	fu_plugin_uefi_test_secure_boot (plugin);
	if (data->ux_capsule)
		ux_capsule_str = "Enabled";
	g_debug ("UX Capsule support : %s", ux_capsule_str);
	fu_plugin_add_report_metadata (plugin, "UEFIUXCapsule", ux_capsule_str);

	return TRUE;
}
