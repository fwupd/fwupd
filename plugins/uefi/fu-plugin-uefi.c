/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2017 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fcntl.h>
#include <fnmatch.h>
#include <gio/gunixmounts.h>
#include <glib/gi18n.h>

#include "fu-plugin-vfuncs.h"

#include "fu-uefi-bgrt.h"
#include "fu-uefi-common.h"
#include "fu-uefi-device.h"
#include "fu-uefi-vars.h"

#ifndef HAVE_GIO_2_55_0
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GUnixMountEntry, g_unix_mount_free)
#pragma clang diagnostic pop
#endif

struct FuPluginData {
	gchar			*esp_path;
	gboolean		 require_shim_for_sb;
	FuUefiBgrt		*bgrt;
};

void
fu_plugin_init (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	data->bgrt = fu_uefi_bgrt_new ();
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_RUN_AFTER, "upower");
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_SUPPORTS_PROTOCOL, "org.uefi.capsule");
	fu_plugin_add_compile_version (plugin, "com.redhat.efivar", EFIVAR_LIBRARY_VERSION);
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_free (data->esp_path);
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
	if (status == FU_UEFI_DEVICE_STATUS_ERROR_PWR_EVT_AC ||
	    status == FU_UEFI_DEVICE_STATUS_ERROR_PWR_EVT_BATT) {
		fu_device_set_update_state (device, FWUPD_UPDATE_STATE_FAILED_TRANSIENT);
	} else {
		fu_device_set_update_state (device, FWUPD_UPDATE_STATE_FAILED);
	}
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

static guint8
fu_plugin_uefi_calc_checksum (const guint8 *buf, gsize sz)
{
	guint8 csum = 0;
	for (gsize i = 0; i < sz; i++)
		csum += buf[i];
	return csum;
}

static gboolean
fu_plugin_uefi_write_splash_data (FuPlugin *plugin,
				  FuDevice *device,
				  GBytes *blob,
				  GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	guint32 screen_x, screen_y;
	gsize buf_size = g_bytes_get_size (blob);
	gssize size;
	guint32 height, width;
	guint8 csum = 0;
	efi_ux_capsule_header_t header = { 0 };
	efi_capsule_header_t capsule_header = {
		.flags = EFI_CAPSULE_HEADER_FLAGS_PERSIST_ACROSS_RESET,
		.guid = efi_guid_ux_capsule,
		.header_size = sizeof(efi_capsule_header_t),
		.capsule_image_size = 0
	};
	g_autofree gchar *fn = NULL;
	g_autofree gchar *directory = NULL;
	g_autofree gchar *basename = NULL;
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
	directory = fu_uefi_get_esp_path_for_os (data->esp_path);
	basename = g_strdup_printf ("fwupd-%s.cap", FU_UEFI_VARS_GUID_UX_CAPSULE);
	fn = g_build_filename (directory, "fw", basename, NULL);
	if (!fu_common_mkdir_parent (fn, error))
		return FALSE;
	ofile = g_file_new_for_path (fn);
	ostream = G_OUTPUT_STREAM (g_file_replace (ofile, NULL, FALSE,
				   G_FILE_CREATE_NONE, NULL, error));
	if (ostream == NULL)
		return FALSE;

	capsule_header.capsule_image_size =
		g_bytes_get_size (blob) +
		sizeof(efi_capsule_header_t) +
		sizeof(efi_ux_capsule_header_t);

	header.version = 1;
	header.image_type = 0;
	header.reserved = 0;
	header.x_offset = (screen_x / 2) - (width / 2);
	header.y_offset = fu_uefi_bgrt_get_yoffset (data->bgrt) +
				fu_uefi_bgrt_get_height (data->bgrt);

	/* header, payload and image has to add to zero */
	csum += fu_plugin_uefi_calc_checksum ((guint8 *) &capsule_header,
					      sizeof(capsule_header));
	csum += fu_plugin_uefi_calc_checksum ((guint8 *) &header,
					      sizeof(header));
	csum += fu_plugin_uefi_calc_checksum (g_bytes_get_data (blob, NULL),
					      g_bytes_get_size (blob));
	header.checksum = 0x100 - csum;

	/* write capsule file */
	size = g_output_stream_write (ostream, &capsule_header,
				      capsule_header.header_size, NULL, error);
	if (size < 0)
		return FALSE;
	size = g_output_stream_write (ostream, &header, sizeof(header), NULL, error);
	if (size < 0)
		return FALSE;
	size = g_output_stream_write_bytes (ostream, blob, NULL, error);
	if (size < 0)
		return FALSE;

	/* write display capsule location as UPDATE_INFO */
	if (!fu_uefi_device_write_update_info (FU_UEFI_DEVICE (device), fn,
					       "fwupd-ux-capsule",
					       &efi_guid_ux_capsule, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_plugin_uefi_update_splash (FuPlugin *plugin, FuDevice *device, GError **error)
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

	/* no UX capsule support, so deleting var if it exists */
	if (fu_device_has_custom_flag (device, "no-ux-capsule")) {
		g_debug ("not providing UX capsule");
		return fu_uefi_vars_delete (FU_UEFI_VARS_GUID_FWUPDATE,
					    "fwupd-ux-capsule", error);
	}

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
	return fu_plugin_uefi_write_splash_data (plugin, device, image_bmp, error);
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
	guint32 flashes_left;
	g_autofree gchar *efibootmgr_path = NULL;
	g_autofree gchar *boot_variables = NULL;
	g_autoptr(GError) error_splash = NULL;

	/* test the flash counter */
	flashes_left = fu_device_get_flashes_left (device);
	if (flashes_left > 0) {
		g_debug ("%s has %" G_GUINT32_FORMAT " flashes left",
			 fu_device_get_name (device),
			 flashes_left);
		if ((flags & FWUPD_INSTALL_FLAG_FORCE) == 0 && flashes_left <= 2) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "%s only has %" G_GUINT32_FORMAT " flashes left -- "
				     "see https://github.com/fwupd/fwupd/wiki/Dell-TPM:-flashes-left for more information.",
				     fu_device_get_name (device), flashes_left);
			return FALSE;
		}
	}

	/* TRANSLATORS: this is shown when updating the firmware after the reboot */
	str = _("Installing firmware update…");
	g_assert (str != NULL);

	/* make sure that the ESP is mounted */
	if (g_getenv ("FWUPD_UEFI_ESP_PATH") == NULL) {
		if (!fu_plugin_uefi_esp_mounted (plugin, error))
			return FALSE;
	}

	/* perform the update */
	g_debug ("Performing UEFI capsule update");
	fu_device_set_status (device, FWUPD_STATUS_SCHEDULING);
	if (!fu_plugin_uefi_update_splash (plugin, device, &error_splash)) {
		g_debug ("failed to upload UEFI UX capsule text: %s",
			 error_splash->message);
	}
	if (!fu_device_write_firmware (device, blob_fw, flags, error))
		return FALSE;

	/* record if we had an invalid header during update */
	str = fu_uefi_missing_capsule_header (device) ? "True" : "False";
	fu_plugin_add_report_metadata (plugin, "MissingCapsuleHeader", str);

	/* record boot information to system log for future debugging */
	efibootmgr_path = fu_common_find_program_in_path ("efibootmgr", NULL);
	if (efibootmgr_path != NULL) {
		g_autofree gchar *cmd = g_strdup_printf ("%s -v", efibootmgr_path);
		if (!g_spawn_command_line_sync (cmd,
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
	if (data->esp_path != NULL)
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

static FwupdVersionFormat
fu_plugin_uefi_get_version_format_for_type (FuPlugin *plugin, FuUefiDeviceKind device_kind)
{
	const gchar *content;
	const gchar *quirk;
	g_autofree gchar *group = NULL;

	/* we have no information for devices */
	if (device_kind == FU_UEFI_DEVICE_KIND_DEVICE_FIRMWARE)
		return FWUPD_VERSION_FORMAT_TRIPLET;

	content = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_MANUFACTURER);
	if (content == NULL)
		return FWUPD_VERSION_FORMAT_TRIPLET;

	/* any quirks match */
	group = g_strdup_printf ("SmbiosManufacturer=%s", content);
	quirk = fu_plugin_lookup_quirk_by_id (plugin, group,
					      FU_QUIRKS_UEFI_VERSION_FORMAT);
	if (quirk == NULL)
		return FWUPD_VERSION_FORMAT_TRIPLET;
	return fwupd_version_format_from_string (quirk);
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
	FwupdVersionFormat version_format;

	/* set default version format */
	device_kind = fu_uefi_device_get_kind (dev);
	version_format = fu_plugin_uefi_get_version_format_for_type (plugin, device_kind);
	fu_device_set_version_format (FU_DEVICE (dev), version_format);

	/* probe to get add GUIDs (and hence any quirk fixups) */
	if (!fu_device_probe (FU_DEVICE (dev), error))
		return FALSE;

	/* if not already set by quirks */
	if (fu_device_get_custom_flags (FU_DEVICE (dev)) == NULL) {
		/* for all Lenovo hardware */
		if (fu_plugin_check_hwid (plugin, "6de5d951-d755-576b-bd09-c5cf66b27234")) {
			fu_device_set_custom_flags (FU_DEVICE (dev), "use-legacy-bootmgr-desc");
			fu_plugin_add_report_metadata (plugin, "BootMgrDesc", "legacy");
		}
	}

	/* set fallback name if nothing else is set */
	if (fu_device_get_name (FU_DEVICE (dev)) == 0) {
		g_autofree gchar *name = NULL;
		name = fu_plugin_uefi_get_name_for_type (plugin, fu_uefi_device_get_kind (dev));
		if (name != NULL)
			fu_device_set_name (FU_DEVICE (dev), name);
	}
	/* set fallback vendor if nothing else is set */
	if (fu_device_get_vendor (FU_DEVICE (dev)) == NULL &&
	    fu_uefi_device_get_kind (dev) == FU_UEFI_DEVICE_KIND_SYSTEM_FIRMWARE) {
		const gchar *vendor = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_MANUFACTURER);
		if (vendor != NULL)
			fu_device_set_vendor (FU_DEVICE (dev), vendor);
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
	pattern = g_build_filename (data->esp_path, "EFI/*/fw/fwupd-*.cap", NULL);
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

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	const guint8 *data;
	gsize sz;
	g_autoptr(GBytes) bios_information = fu_plugin_get_smbios_data (plugin, 0);
	if (bios_information == NULL) {
		const gchar *tmp = g_getenv ("FWUPD_DELL_FAKE_SMBIOS");
		if (tmp != NULL)
			return TRUE;
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "SMBIOS not supported");
		return FALSE;
	}
	data = g_bytes_get_data (bios_information, &sz);
	if (sz < 0x13) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "offset bigger than size %" G_GSIZE_FORMAT, sz);
		return FALSE;
	}
	if (data[1] < 0x13) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "SMBIOS 2.3 not supported");
		return FALSE;
	}
	if (!(data[0x13] & (1 << 3))) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "System does not support UEFI mode");
		return FALSE;
	}
	/* test for invalid ESP in coldplug, and set the update-error rather
	 * than showing no output if the plugin had self-disabled here */
	return TRUE;
}

static gboolean
fu_plugin_uefi_ensure_esp_path (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	guint64 sz_reqd = FU_UEFI_COMMON_REQUIRED_ESP_FREE_SPACE;
	g_autofree gchar *require_esp_free_space = NULL;
	g_autofree gchar *require_shim_for_sb = NULL;

	/* parse free space */
	require_esp_free_space = fu_plugin_get_config_value (plugin, "RequireESPFreeSpace");
	if (require_esp_free_space != NULL)
		sz_reqd = fu_common_strtoull (require_esp_free_space);

	/* load from file */
	data->esp_path = fu_plugin_get_config_value (plugin, "OverrideESPMountPoint");
	if (data->esp_path != NULL) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_uefi_check_esp_path (data->esp_path, &error_local)) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_FILENAME,
				     "invalid OverrideESPMountPoint=%s specified in config: %s",
				     data->esp_path, error_local->message);
			return FALSE;
		}
		return fu_uefi_check_esp_free_space (data->esp_path, sz_reqd, error);
	}
	require_shim_for_sb = fu_plugin_get_config_value (plugin, "RequireShimForSecureBoot");
	if (require_shim_for_sb == NULL ||
	    g_ascii_strcasecmp (require_shim_for_sb, "true") == 0)
		data->require_shim_for_sb = TRUE;

	/* try to guess from heuristics */
	data->esp_path = fu_uefi_guess_esp_path ();
	if (data->esp_path == NULL) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_FILENAME,
				     "Unable to determine EFI system partition location, "
				     "See https://github.com/fwupd/fwupd/wiki/Determining-EFI-system-partition-location");
		return FALSE;
	}

	/* check free space */
	if (!fu_uefi_check_esp_free_space (data->esp_path, sz_reqd, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_plugin_uefi_ensure_efivarfs_rw (GError **error)
{
	g_autofree gchar *sysfsfwdir = fu_common_get_path (FU_PATH_KIND_SYSFSDIR_FW);
	g_autofree gchar *sysfsefivardir = g_build_filename (sysfsfwdir, "efi", "efivars", NULL);
	g_autoptr(GUnixMountEntry) mount = g_unix_mount_at (sysfsefivardir, NULL);

	if (mount == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "%s was not mounted", sysfsefivardir);
		return FALSE;
	}
	if (g_unix_mount_is_readonly (mount)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "%s is read only", sysfsefivardir);
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_plugin_unlock (FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuUefiDevice *device_uefi = FU_UEFI_DEVICE (device);
	FuDevice *device_alt = NULL;
	FwupdDeviceFlags device_flags_alt = 0;
	guint flashes_left = 0;
	guint flashes_left_alt = 0;

	if (fu_uefi_device_get_kind (device_uefi) !=
	    FU_UEFI_DEVICE_KIND_DELL_TPM_FIRMWARE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Unable to unlock %s",
			     fu_device_get_name (device));
		return FALSE;
	}

	/* for unlocking TPM1.2 <-> TPM2.0 switching */
	g_debug ("Unlocking upgrades for: %s (%s)", fu_device_get_name (device),
		 fu_device_get_id (device));
	device_alt = fu_device_get_alternate (device);
	if (device_alt == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "No alternate device for %s",
			     fu_device_get_name (device));
		return FALSE;
	}
	g_debug ("Preventing upgrades for: %s (%s)", fu_device_get_name (device_alt),
		 fu_device_get_id (device_alt));

	flashes_left = fu_device_get_flashes_left (device);
	flashes_left_alt = fu_device_get_flashes_left (device_alt);
	if (flashes_left == 0) {
		/* flashes left == 0 on both means no flashes left */
		if (flashes_left_alt == 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "ERROR: %s has no flashes left.",
				     fu_device_get_name (device));
		/* flashes left == 0 on just unlocking device is ownership */
		} else {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "ERROR: %s is currently OWNED. "
				     "Ownership must be removed to switch modes.",
				     fu_device_get_name (device_alt));
		}
		return FALSE;
	}

	/* clone the info from real device but prevent it from being flashed */
	device_flags_alt = fu_device_get_flags (device_alt);
	fu_device_set_flags (device, device_flags_alt);
	fu_device_set_flags (device_alt, device_flags_alt & ~FWUPD_DEVICE_FLAG_UPDATABLE);

	/* make sure that this unlocked device can be updated */
	fu_device_set_version (device, "0.0.0.0", FWUPD_VERSION_FORMAT_QUAD);
	return TRUE;
}

static gboolean
fu_plugin_uefi_create_dummy (FuPlugin *plugin, GError **error)
{
	const gchar *key;
	g_autoptr(FuDevice) dev = fu_device_new ();

	key = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_MANUFACTURER);
	if (key != NULL)
		fu_device_set_vendor (dev, key);
	key = fu_plugin_uefi_get_name_for_type (plugin, FU_UEFI_DEVICE_KIND_SYSTEM_FIRMWARE);
	fu_device_set_name (dev, key);
	key = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_BIOS_VERSION);
	if (key != NULL)
		fu_device_set_version (dev, key, FWUPD_VERSION_FORMAT_PLAIN);
	key = "Firmware can not be updated in legacy mode, switch to UEFI mode.";
	fu_device_set_update_error (dev, key);

	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_REQUIRE_AC);

	fu_device_add_icon (dev, "computer");
	fu_device_set_plugin (dev, fu_plugin_get_name (plugin));
	fu_device_set_id (dev, "UEFI-dummy");
	fu_device_add_instance_id (dev, "main-system-firmware");
	if (!fu_device_setup (dev, error))
		return FALSE;
	fu_plugin_device_add (plugin, dev);

	return TRUE;
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	const gchar *str;
	g_autofree gchar *bootloader = NULL;
	g_autofree gchar *esrt_path = NULL;
	g_autofree gchar *sysfsfwdir = NULL;
	g_autoptr(GError) error_bootloader = NULL;
	g_autoptr(GError) error_efivarfs = NULL;
	g_autoptr(GError) error_esp = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) entries = NULL;

	/* are the EFI dirs set up so we can update each device */
	if (!fu_uefi_vars_supported (&error_local)) {
		g_warning ("%s", error_local->message);
		return fu_plugin_uefi_create_dummy (plugin, error);
	}

	/* get the directory of ESRT entries */
	sysfsfwdir = fu_common_get_path (FU_PATH_KIND_SYSFSDIR_FW);
	esrt_path = g_build_filename (sysfsfwdir, "efi", "esrt", NULL);
	entries = fu_uefi_get_esrt_entry_paths (esrt_path, error);
	if (entries == NULL)
		return FALSE;

	/* make sure that efivarfs is rw */
	if (!fu_plugin_uefi_ensure_efivarfs_rw (&error_efivarfs))
		g_warning ("%s", error_efivarfs->message);

	/* if secure boot is enabled ensure we have a signed fwupd.efi */
	bootloader = fu_uefi_get_built_app_path (&error_bootloader);
	if (bootloader == NULL) {
		if (fu_uefi_secure_boot_enabled ())
			g_prefix_error (&error_bootloader, "missing signed bootloader for secure boot: ");
		g_warning ("%s", error_bootloader->message);
	}

	/* ensure the ESP is detected */
	if (!fu_plugin_uefi_ensure_esp_path (plugin, &error_esp))
		g_warning ("%s", error_esp->message);

	/* add each device */
	for (guint i = 0; i < entries->len; i++) {
		const gchar *path = g_ptr_array_index (entries, i);
		g_autoptr(GError) error_parse = NULL;
		g_autoptr(FuUefiDevice) dev = fu_uefi_device_new_from_entry (path, &error_parse);
		if (dev == NULL) {
			g_warning ("failed to add %s: %s", path, error_parse->message);
			continue;
		}
		fu_device_set_quirks (FU_DEVICE (dev), fu_plugin_get_quirks (plugin));
		if (!fu_plugin_uefi_coldplug_device (plugin, dev, error))
			return FALSE;
		if (error_esp != NULL) {
			fu_device_set_update_error (FU_DEVICE (dev), error_esp->message);
		} else if (error_bootloader != NULL) {
			fu_device_set_update_error (FU_DEVICE (dev), error_bootloader->message);
		} else if (error_efivarfs != NULL) {
			fu_device_set_update_error (FU_DEVICE (dev), error_efivarfs->message);
		} else {
			fu_device_set_metadata (FU_DEVICE (dev), "EspPath", data->esp_path);
			fu_device_set_metadata_boolean (FU_DEVICE (dev),
							"RequireShimForSecureBoot",
							data->require_shim_for_sb);
			fu_device_add_flag (FU_DEVICE (dev), FWUPD_DEVICE_FLAG_UPDATABLE);
		}
		fu_plugin_device_add (plugin, FU_DEVICE (dev));
	}

	/* no devices are updatable */
	if (error_esp != NULL || error_bootloader != NULL)
		return TRUE;

	/* delete any existing .cap files to avoid the small ESP partition
	 * from running out of space when we've done lots of firmware updates
	 * -- also if the distro has changed the ESP may be different anyway */
	if (fu_uefi_vars_exists (FU_UEFI_VARS_GUID_EFI_GLOBAL, "BootNext")) {
		g_debug ("detected BootNext, not cleaning up");
	} else {
		if (!fu_plugin_uefi_delete_old_capsules (plugin, error))
			return FALSE;
		if (!fu_uefi_vars_delete_with_glob (FU_UEFI_VARS_GUID_FWUPDATE, "fwupd-*", error))
			return FALSE;
	}

	/* save in report metadata */
	g_debug ("ESP mountpoint set as %s", data->esp_path);
	fu_plugin_add_report_metadata (plugin, "ESPMountPoint", data->esp_path);

	/* for debugging problems later */
	fu_plugin_uefi_test_secure_boot (plugin);
	if (!fu_uefi_bgrt_setup (data->bgrt, &error_local))
		g_debug ("BGRT setup failed: %s", error_local->message);
	str = fu_uefi_bgrt_get_supported (data->bgrt) ? "Enabled" : "Disabled";
	g_debug ("UX Capsule support : %s", str);
	fu_plugin_add_report_metadata (plugin, "UEFIUXCapsule", str);

	return TRUE;
}
