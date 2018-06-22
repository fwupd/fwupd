/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2017 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <appstream-glib.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <glib/gi18n.h>
#include <efivar.h>

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

#include "fu-uefi-bgrt.h"
#include "fu-uefi-common.h"
#include "fu-uefi-device.h"
#include "fu-uefi-device-info.h"

struct FuPluginData {
	gchar			*esp_path;
	gchar			*esrt_path;
	FuUefiBgrt		*bgrt;
};

/* drop when upgrading minimum required version of efivar to 33 */
#if !defined (efi_guid_ux_capsule)
#define efi_guid_ux_capsule EFI_GUID(0x3b8c8162,0x188c,0x46a4,0xaec9,0xbe,0x43,0xf1,0xd6,0x56,0x97)
#endif

void
fu_plugin_init (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	data->bgrt = fu_uefi_bgrt_new ();
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_RUN_AFTER, "upower");
	fu_plugin_add_compile_version (plugin, "com.redhat.efivar", EFIVAR_LIBRARY_VERSION);
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_free (data->esp_path);
	g_free (data->esrt_path);
	g_object_unref (data->bgrt);
}

gboolean
fu_plugin_clear_results (FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuUefiDevice *device_uefi = FU_UEFI_DEVICE (device);
	return fu_uefi_device_clear_status (device_uefi, error);
}

gboolean
fu_plugin_get_results (FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuUefiDevice *device_uefi = FU_UEFI_DEVICE (device);
	FuUefiDeviceStatus status = fu_uefi_device_get_status (device_uefi);
	const gchar *tmp;
	g_autofree gchar *err_msg = NULL;
	g_autofree gchar *version_str = NULL;

	/* trivial case */
	if (status == FU_UEFI_DEVICE_STATUS_SUCCESS) {
		fu_device_set_update_state (device, FWUPD_UPDATE_STATE_SUCCESS);
		return TRUE;
	}

	/* something went wrong */
	fu_device_set_update_state (device, FWUPD_UPDATE_STATE_FAILED);
	version_str = g_strdup_printf ("%u", fu_uefi_device_get_version_error (device_uefi));
	tmp = fu_uefi_device_status_to_string (status);
	if (tmp == NULL) {
		err_msg = g_strdup_printf ("failed to update to %s",
					   version_str);
	} else {
		err_msg = g_strdup_printf ("failed to update to %s: %s",
					   version_str, tmp);
	}
	fu_device_set_update_error (device, err_msg);
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
fu_plugin_uefi_write_splash_data (FuPlugin *plugin, GBytes *blob, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	guint32 screen_x, screen_y;
	gsize buf_size = 0;
	gssize size;
	guint32 height, width;
	ux_capsule_header_t header;
	efi_capsule_header_t capsule_header = {
		.flags = CAPSULE_FLAGS_PERSIST_ACROSS_RESET,
		.guid = efi_guid_ux_capsule,
		.header_size = sizeof(efi_capsule_header_t),
		.capsule_image_size = 0
	};
	FuUefiDeviceInfo info = {
		.dp_ptr = NULL,
		.guid = efi_guid_ux_capsule,
	};
	g_autofree gchar *fn = NULL;
	g_autofree guint8 *buf = NULL;
	g_autoptr(GFile) ofile = NULL;
	g_autoptr(GOutputStream) ostream = NULL;

	/* get screen dimensions */
	if (!fu_uefi_get_framebuffer_size (&screen_x, &screen_y, error))
		return FALSE;
	if (!fu_uefi_get_bitmap_size ((const guint8 *) g_bytes_get_data (blob, NULL),
				      buf_size, &width, &height, error)) {
		g_prefix_error (error, "splash invalid: ");
		return FALSE;
	}

	/* save to a predicatable filename */
	fn = fu_uefi_device_info_get_media_path (data->esp_path, &info);
	ofile = g_file_new_for_path (fn);
	ostream = G_OUTPUT_STREAM (g_file_replace (ofile, NULL, FALSE, G_FILE_CREATE_NONE, NULL, error));
	if (ostream == NULL)
		return FALSE;

	capsule_header.capsule_image_size =
		g_bytes_get_size (blob) +
		sizeof(efi_capsule_header_t) +
		sizeof(header);

	memset (&header, '\0', sizeof(header));
	header.version = 1;
	header.image_type = 0;
	header.reserved = 0;
	header.x_offset = (screen_x / 2) - (width / 2);
	header.y_offset = fu_uefi_bgrt_get_yoffset (data->bgrt) +
				fu_uefi_bgrt_get_height (data->bgrt);

	/* write capsule file */
	size = g_output_stream_write (ostream, &capsule_header, capsule_header.header_size, NULL, error);
	if (size < 0)
		return FALSE;
	size = g_output_stream_write (ostream, &header, sizeof(header), NULL, error);
	if (size < 0)
		return FALSE;
	size = g_output_stream_write_bytes (ostream, blob, NULL, error);
	if (size < 0)
		return FALSE;

	//FIXME: don't we have to set efidp header()?
	return TRUE;
}

static gboolean
fu_plugin_uefi_update_splash (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	guint best_idx = G_MAXUINT;
	guint32 lowest_border_pixels = G_MAXUINT;
	guint32 screen_height = 768;
	guint32 screen_width = 1024;
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
	if (!fu_uefi_bgrt_get_supported (data->bgrt)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "BGRT is not supported");
		return FALSE;
	}
	if (!fu_uefi_get_framebuffer_size (&screen_width, &screen_height, error))
		return FALSE;
	g_debug ("framebuffer size %" G_GUINT32_FORMAT " x%" G_GUINT32_FORMAT,
		 screen_width, screen_height);

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
	return fu_plugin_uefi_write_splash_data (plugin, image_bmp, error);
}

static gboolean
fu_plugin_uefi_esp_mounted (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autofree gchar *contents = NULL;
	g_auto(GStrv) lines = NULL;
	gsize length;

	if (!g_file_get_contents ("/proc/mounts", &contents, &length, error))
		return FALSE;
	lines = g_strsplit (contents, "\n", 0);

	for (guint i = 0; lines[i] != NULL; i++) {
		if (lines[i] != NULL && g_strrstr (lines[i], data->esp_path))
			return TRUE;
	}
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_SUPPORTED,
		     "EFI System partition %s is not mounted",
		     data->esp_path);
	return FALSE;
}

gboolean
fu_plugin_update (FuPlugin *plugin,
		  FuDevice *device,
		  GBytes *blob_fw,
		  FwupdInstallFlags flags,
		  GError **error)
{
	const gchar *str;
	guint flashes_left;
	g_autofree gchar *efibootmgr_path = NULL;
	g_autofree gchar *boot_variables = NULL;
	g_autoptr(GError) error_splash = NULL;

	/* test the flash counter */
	flashes_left = fu_device_get_flashes_left (device);
	if (flashes_left > 0) {
		g_debug ("%s has %u flashes left",
			 fu_device_get_name (device),
			 flashes_left);
		if ((flags & FWUPD_INSTALL_FLAG_FORCE) == 0 && flashes_left <= 2) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "%s only has %u flashes left -- "
				     "see https://github.com/hughsie/fwupd/wiki/Dell-TPM:-flashes-left for more information.",
				     fu_device_get_name (device), flashes_left);
			return FALSE;
		}
	}

	/* TRANSLATORS: this is shown when updating the firmware after the reboot */
	str = _("Installing firmware update…");
	g_assert (str != NULL);

	/* make sure that the ESP is mounted */
	if (fu_device_get_metadata (device, "UEFI::FakeESP") == NULL) {
		if (!fu_plugin_uefi_esp_mounted (plugin, error))
			return FALSE;
	}

	/* perform the update */
	g_debug ("Performing UEFI capsule update");
	fu_device_set_status (device, FWUPD_STATUS_SCHEDULING);
	if (!fu_plugin_uefi_update_splash (plugin, &error_splash)) {
		g_debug ("failed to upload UEFI UX capsule text: %s",
			 error_splash->message);
	}
	if (!fu_device_write_firmware (device, blob_fw, error))
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

static void
fu_plugin_uefi_register_proxy_device (FuPlugin *plugin, FuDevice *device)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autoptr(FuUefiDevice) dev = fu_uefi_device_new_from_dev (device);
	fu_device_set_metadata (FU_DEVICE (dev), "EspPath", data->esp_path);
	fu_plugin_device_add (plugin, FU_DEVICE (dev));
}

void
fu_plugin_device_registered (FuPlugin *plugin, FuDevice *device)
{
	if (fu_device_get_metadata (device, "UefiDeviceKind") != NULL) {
		if (fu_device_get_guid_default (device) == NULL) {
			g_autofree gchar *dbg = fu_device_to_string (device);
			g_warning ("cannot create proxy device as no GUID: %s", dbg);
			return;
		}
		fu_plugin_uefi_register_proxy_device (plugin, device);
	}
}

static AsVersionParseFlag
fu_plugin_uefi_get_version_format_for_type (FuPlugin *plugin, FuUefiDeviceKind device_kind)
{
	const gchar *content;
	const gchar *quirk;

	/* we have no information for devices */
	if (device_kind == FU_UEFI_DEVICE_KIND_DEVICE_FIRMWARE)
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

static const gchar *
fu_plugin_uefi_uefi_type_to_string (FuUefiDeviceKind device_kind)
{
	if (device_kind == FU_UEFI_DEVICE_KIND_UNKNOWN)
		return "Unknown Firmware";
	if (device_kind == FU_UEFI_DEVICE_KIND_SYSTEM_FIRMWARE)
		return "System Firmware";
	if (device_kind == FU_UEFI_DEVICE_KIND_DEVICE_FIRMWARE)
		return "Device Firmware";
	if (device_kind == FU_UEFI_DEVICE_KIND_UEFI_DRIVER)
		return "UEFI Driver";
	if (device_kind == FU_UEFI_DEVICE_KIND_FMP)
		return "Firmware Management Protocol";
	return NULL;
}

static gchar *
fu_plugin_uefi_get_name_for_type (FuPlugin *plugin, FuUefiDeviceKind device_kind)
{
	GString *display_name;

	/* set Display Name prefix for capsules that are not PCI cards */
	display_name = g_string_new (fu_plugin_uefi_uefi_type_to_string (device_kind));
	if (device_kind == FU_UEFI_DEVICE_KIND_DEVICE_FIRMWARE) {
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

static gboolean
fu_plugin_uefi_coldplug_device (FuPlugin *plugin, FuUefiDevice *dev, GError **error)
{
	FuUefiDeviceKind device_kind;
	AsVersionParseFlag parse_flags;
	guint32 version_raw;
	g_autofree gchar *name = NULL;
	g_autofree gchar *version_lowest = NULL;
	g_autofree gchar *version = NULL;

	/* add details to the device */
	device_kind = fu_uefi_device_get_kind (dev);
	parse_flags = fu_plugin_uefi_get_version_format_for_type (plugin, device_kind);
	version_raw = fu_uefi_device_get_version (dev);
	version = as_utils_version_from_uint32 (version_raw, parse_flags);
	fu_device_set_version (dev, version);
	name = fu_plugin_uefi_get_name_for_type (plugin, fu_uefi_device_get_kind (dev));
	if (name != NULL)
		fu_device_set_name (FU_DEVICE (dev), name);
	version_raw = fu_uefi_device_get_version_lowest (dev);
	if (version_raw != 0) {
		version_lowest = as_utils_version_from_uint32 (version_raw,
							       parse_flags);
		fu_device_set_version_lowest (FU_DEVICE (dev), version_lowest);
	}
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_INTERNAL);
	if (g_file_test ("/sys/firmware/efi/efivars", G_FILE_TEST_IS_DIR) ||
	    g_file_test ("/sys/firmware/efi/vars", G_FILE_TEST_IS_DIR) ||
	    g_getenv ("FWUPD_UEFI_IN_TESTS") != NULL) {
		fu_device_add_flag (FU_DEVICE (dev), FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_device_add_flag (FU_DEVICE (dev), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	} else {
		g_warning ("Kernel support for EFI variables missing");
	}
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	if (device_kind == FU_UEFI_DEVICE_KIND_DEVICE_FIRMWARE) {
		/* nothing better in the icon naming spec */
		fu_device_add_icon (FU_DEVICE (dev), "audio-card");
	} else {
		/* this is probably system firmware */
		fu_device_add_icon (FU_DEVICE (dev), "computer");
		fu_device_add_guid (FU_DEVICE (dev), "main-system-firmware");
	}

	/* success */
	return TRUE;
}

static void
fu_plugin_uefi_test_secure_boot (FuPlugin *plugin)
{
	const gchar *result_str = "Disabled";
	if (fu_uefi_secure_boot_enabled ())
		result_str = "Enabled";
	g_debug ("SecureBoot is: %s", result_str);
	fu_plugin_add_report_metadata (plugin, "SecureBoot", result_str);
}

static gboolean
fu_plugin_uefi_delete_old_capsules (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autofree gchar *pattern = NULL;
	g_autoptr(GPtrArray) files = NULL;

	/* delete any files matching the glob in the ESP */
	files = fu_common_get_files_recursive (data->esp_path, error);
	if (files == NULL)
		return FALSE;
	pattern = g_build_filename (data->esp_path, "EFI/*/fw/fwupdate-*.cap", NULL);
	for (guint i = 0; i < files->len; i++) {
		const gchar *fn = g_ptr_array_index (files, i);
		if (fnmatch (pattern, fn, 0) == 0) {
			g_autoptr(GFile) file = g_file_new_for_path (fn);
			g_debug ("deleting %s", fn);
			if (!g_file_delete (file, NULL, error))
				return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_plugin_uefi_delete_old_efivars (FuPlugin *plugin, GError **error)
{
	char *name = NULL;
	efi_guid_t fwupdate_guid = FWUPDATE_GUID;
	efi_guid_t *guid = NULL;
	int rc;
	while ((rc = efi_get_next_variable_name (&guid, &name)) > 0) {
		if (efi_guid_cmp (guid, &fwupdate_guid) != 0)
			continue;
		if (g_str_has_prefix (name, "fwupdate-")) {
			g_debug ("deleting %s", name);
			rc = efi_del_variable (fwupdate_guid, name);
			if (rc < 0) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "failed to delete efi var %s: %s",
					     name, strerror (errno));
				return FALSE;
			}
		}
	}
	if (rc < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "error listing variables: %s",
			     strerror (errno));
		return FALSE;
	}
	return TRUE;
}

/* remove when https://github.com/rhboot/efivar/pull/100 merged */
static int
_efi_get_variable_exists (efi_guid_t guid, const char *name)
{
	uint32_t unused_attrs = 0;
	return efi_get_variable_attributes (guid, name, &unused_attrs);
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	const gchar *key = "OverrideESPMountPoint";
	g_autofree gchar *sysfsfwdir = NULL;

	/* load from file */
	data->esp_path = fu_plugin_get_config_value (plugin, key);
	if (data->esp_path != NULL) {
		//FIXME: remove OverrideESPMountPoint runtime config?
		if (!g_file_test (data->esp_path, G_FILE_TEST_IS_DIR)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Invalid %s specified in %s config: %s",
				     fu_plugin_get_name (plugin), key,
				     data->esp_path);

			return FALSE;
		}
	}

	/* fall back to a sane default */
	if (data->esp_path == NULL)
		data->esp_path = fu_common_get_path (FU_PATH_KIND_ESPDIR);

	/* get the directory of ESRT entries */
	sysfsfwdir = fu_common_get_path (FU_PATH_KIND_SYSFSDIR_FW);
	data->esrt_path = g_build_filename (sysfsfwdir, "efi", "esrt", NULL);

	/* delete any existing .cap files to avoid the small ESP partition
	 * from running out of space when we've done lots of firmware updates
	 * -- also if the distro has changed the ESP may be different anyway */
	if (g_getenv ("FWUPD_UEFI_IN_TESTS") != NULL ||
	    _efi_get_variable_exists (EFI_GLOBAL_GUID, "BootNext") == 0) {
		g_debug ("detected BootNext, not cleaning up");
	} else {
		if (!fu_plugin_uefi_delete_old_capsules (plugin, error))
			return FALSE;
		if (!fu_plugin_uefi_delete_old_efivars (plugin, error))
			return FALSE;
	}

	/* save in report metadata */
	g_debug ("ESP mountpoint set as %s", data->esp_path);
	fu_plugin_add_report_metadata (plugin, "OverrideESPMountPoint", data->esp_path);
	return TRUE;
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	const gchar *str;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) entries = NULL;

	/* add each device */
	entries = fu_uefi_get_esrt_entry_paths (data->esrt_path, error);
	if (entries == NULL)
		return FALSE;
	for (guint i = 0; i < entries->len; i++) {
		const gchar *path = g_ptr_array_index (entries, i);
		g_autoptr(FuUefiDevice) dev = fu_uefi_device_new_from_entry (path);
		if (!fu_plugin_uefi_coldplug_device (plugin, dev, error))
			return FALSE;
		fu_device_set_metadata (FU_DEVICE (dev), "EspPath", data->esp_path);
		fu_plugin_device_add (plugin, FU_DEVICE (dev));
	}

	/* for debugging problems later */
	fu_plugin_uefi_test_secure_boot (plugin);
	if (!fu_uefi_bgrt_setup (data->bgrt, &error_local))
		g_debug ("BGRT setup failed: %s", error_local->message);
	str = fu_uefi_bgrt_get_supported (data->bgrt) ? "Enabled" : "Disabled";
	g_debug ("UX Capsule support : %s", str);
	fu_plugin_add_report_metadata (plugin, "UEFIUXCapsule", str);
	return TRUE;
}
