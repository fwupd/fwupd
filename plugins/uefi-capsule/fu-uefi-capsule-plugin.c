/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <glib/gi18n.h>

#include "fu-acpi-uefi.h"
#include "fu-uefi-backend.h"
#include "fu-uefi-bgrt.h"
#include "fu-uefi-bootmgr.h"
#include "fu-uefi-capsule-plugin.h"
#include "fu-uefi-cod-device.h"
#include "fu-uefi-common.h"
#include "fu-uefi-grub-device.h"
#include "fu-uefi-struct.h"
#include "fu-uefi-update-info.h"

struct _FuUefiCapsulePlugin {
	FuPlugin parent_instance;
	FuUefiBgrt *bgrt;
	FuFirmware *acpi_uefi; /* optional */
	FuVolume *esp;
	FuBackend *backend;
	GFile *fwupd_efi_file;
	GFileMonitor *fwupd_efi_monitor;
};

G_DEFINE_TYPE(FuUefiCapsulePlugin, fu_uefi_capsule_plugin, FU_TYPE_PLUGIN)

/* defaults changed here will also be reflected in the fwupd.conf man page */
#define FU_UEFI_CAPSULE_CONFIG_DEFAULT_ENABLE_GRUB_CHAIN_LOAD	      FALSE
#define FU_UEFI_CAPSULE_CONFIG_DEFAULT_DISABLE_SHIM_FOR_SECURE_BOOT   FALSE
#define FU_UEFI_CAPSULE_CONFIG_DEFAULT_REQUIRE_ESP_FREE_SPACE	      "0" /* in MB */
#define FU_UEFI_CAPSULE_CONFIG_DEFAULT_DISABLE_CAPSULE_UPDATE_ON_DISK FALSE
#define FU_UEFI_CAPSULE_CONFIG_DEFAULT_ENABLE_EFI_DEBUGGING	      FALSE

static void
fu_uefi_capsule_plugin_to_string(FuPlugin *plugin, guint idt, GString *str)
{
	FuUefiCapsulePlugin *self = FU_UEFI_CAPSULE_PLUGIN(plugin);
	fu_backend_add_string(self->backend, idt, str);
	if (self->bgrt != NULL) {
		fu_string_append_kb(str,
				    idt,
				    "BgrtSupported",
				    fu_uefi_bgrt_get_supported(self->bgrt));
	}
}

static gboolean
fu_uefi_capsule_plugin_fwupd_efi_parse(FuUefiCapsulePlugin *self, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(FU_PLUGIN(self));
	const guint8 needle[] = "f\0w\0u\0p\0d\0-\0e\0f\0i\0 \0v\0e\0r\0s\0i\0o\0n\0 ";
	gsize offset = 0;
	g_autofree gchar *fn = g_file_get_path(self->fwupd_efi_file);
	g_autofree gchar *version = NULL;
	g_autoptr(GBytes) buf = NULL;
	g_autoptr(GBytes) ubuf = NULL;

	/* find the UTF-16 version string */
	buf = fu_bytes_get_contents(fn, error);
	if (buf == NULL)
		return FALSE;
	if (!fu_memmem_safe(g_bytes_get_data(buf, NULL),
			    g_bytes_get_size(buf),
			    needle,
			    sizeof(needle),
			    &offset,
			    error)) {
		g_prefix_error(error, "searching %s: ", fn);
		return FALSE;
	}
	ubuf = fu_bytes_new_offset(buf, offset + sizeof(needle), 30, error);
	if (ubuf == NULL)
		return FALSE;

	/* convert to UTF-8 */
	version = fu_utf16_to_utf8_bytes(ubuf, error);
	if (version == NULL) {
		g_prefix_error(error, "converting %s: ", fn);
		return FALSE;
	}

	/* success */
	fu_context_add_runtime_version(ctx, "org.freedesktop.fwupd-efi", version);
	return TRUE;
}

static void
fu_uefi_capsule_plugin_fwupd_efi_changed_cb(GFileMonitor *monitor,
					    GFile *file,
					    GFile *other_file,
					    GFileMonitorEvent event_type,
					    gpointer user_data)
{
	FuUefiCapsulePlugin *self = FU_UEFI_CAPSULE_PLUGIN(user_data);
	FuContext *ctx = fu_plugin_get_context(FU_PLUGIN(self));
	g_autoptr(GError) error_local = NULL;

	if (!fu_uefi_capsule_plugin_fwupd_efi_parse(self, &error_local)) {
		fu_context_add_runtime_version(ctx, "org.freedesktop.fwupd-efi", "1.0");
		g_warning("failed to get new fwupd efi runtime version: %s", error_local->message);
		return;
	}
}

static gboolean
fu_uefi_capsule_plugin_fwupd_efi_probe(FuUefiCapsulePlugin *self, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(FU_PLUGIN(self));
	g_autofree gchar *fn = NULL;

	/* find the app binary */
	fn = fu_uefi_get_built_app_path("fwupd", error);
	if (fn == NULL)
		return FALSE;
	self->fwupd_efi_file = g_file_new_for_path(fn);
	self->fwupd_efi_monitor =
	    g_file_monitor_file(self->fwupd_efi_file, G_FILE_MONITOR_NONE, NULL, error);
	if (self->fwupd_efi_monitor == NULL)
		return FALSE;
	g_signal_connect(G_FILE_MONITOR(self->fwupd_efi_monitor),
			 "changed",
			 G_CALLBACK(fu_uefi_capsule_plugin_fwupd_efi_changed_cb),
			 self);
	if (!fu_uefi_capsule_plugin_fwupd_efi_parse(self, error)) {
		fu_context_add_runtime_version(ctx, "org.freedesktop.fwupd-efi", "1.0");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_uefi_capsule_plugin_clear_results(FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuUefiDevice *device_uefi = FU_UEFI_DEVICE(device);
	return fu_uefi_device_clear_status(device_uefi, error);
}

static gchar *
fu_uefi_capsule_plugin_efivar_attrs_to_string(guint32 attrs)
{
	const gchar *data[7] = {0};
	guint idx = 0;
	if (attrs & FU_EFIVAR_ATTR_NON_VOLATILE)
		data[idx++] = "non-volatile";
	if (attrs & FU_EFIVAR_ATTR_BOOTSERVICE_ACCESS)
		data[idx++] = "bootservice-access";
	if (attrs & FU_EFIVAR_ATTR_RUNTIME_ACCESS)
		data[idx++] = "runtime-access";
	if (attrs & FU_EFIVAR_ATTR_HARDWARE_ERROR_RECORD)
		data[idx++] = "hardware-error-record";
	if (attrs & FU_EFIVAR_ATTR_AUTHENTICATED_WRITE_ACCESS)
		data[idx++] = "authenticated-write-access";
	if (attrs & FU_EFIVAR_ATTR_TIME_BASED_AUTHENTICATED_WRITE_ACCESS)
		data[idx++] = "time-based-authenticated-write-access";
	if (attrs & FU_EFIVAR_ATTR_APPEND_WRITE)
		data[idx++] = "append-write";
	return g_strjoinv(",", (gchar **)data);
}

static void
fu_uefi_capsule_plugin_add_security_attrs_secureboot(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT);
	fu_security_attrs_append(attrs, attr);

	/* SB not available or disabled */
	if (!fu_efivar_secure_boot_enabled(&error)) {
		if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
			return;
		}
		fu_security_attr_add_bios_target_value(attr, "SecureBoot", "enable");
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
}

static void
fu_uefi_capsule_plugin_add_security_attrs_bootservices(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	const gchar *guids[] = {FU_EFIVAR_GUID_SECURITY_DATABASE, FU_EFIVAR_GUID_EFI_GLOBAL};

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_UEFI_BOOTSERVICE_VARS);
	fu_security_attrs_append(attrs, attr);
	for (guint j = 0; j < G_N_ELEMENTS(guids); j++) {
		g_autoptr(GPtrArray) names = fu_efivar_get_names(guids[j], NULL);

		/* sanity check */
		if (names == NULL)
			continue;
		for (guint i = 0; i < names->len; i++) {
			const gchar *name = g_ptr_array_index(names, i);
			g_autoptr(GError) error_local = NULL;
			gsize data_sz = 0;
			guint32 data_attr = 0;

			if (!fu_efivar_get_data(guids[j],
						name,
						NULL,
						&data_sz,
						&data_attr,
						&error_local)) {
				g_warning("failed to read %s-%s: %s",
					  name,
					  guids[j],
					  error_local->message);
				continue;
			}
			if ((data_attr & FU_EFIVAR_ATTR_BOOTSERVICE_ACCESS) > 0 &&
			    (data_attr & FU_EFIVAR_ATTR_RUNTIME_ACCESS) == 0) {
				g_debug("%s-%s attr of size 0x%x had flags %s",
					name,
					guids[j],
					(guint)data_sz,
					fu_uefi_capsule_plugin_efivar_attrs_to_string(data_attr));
				fwupd_security_attr_add_metadata(attr, "guid", guids[j]);
				fwupd_security_attr_add_metadata(attr, "name", name);
				fwupd_security_attr_add_flag(
				    attr,
				    FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
				fwupd_security_attr_set_result(
				    attr,
				    FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
				return;
			}
		}
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
}

static void
fu_uefi_capsule_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	fu_uefi_capsule_plugin_add_security_attrs_secureboot(plugin, attrs);
	fu_uefi_capsule_plugin_add_security_attrs_bootservices(plugin, attrs);
}

static GBytes *
fu_uefi_capsule_plugin_get_splash_data(guint width, guint height, GError **error)
{
	const gchar *const *langs = g_get_language_names();
	g_autofree gchar *datadir_pkg = NULL;
	g_autofree gchar *filename_archive = NULL;
	g_autofree gchar *langs_str = NULL;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) blob_archive = NULL;

	/* load archive */
	datadir_pkg = fu_path_from_kind(FU_PATH_KIND_DATADIR_PKG);
	filename_archive = g_build_filename(datadir_pkg, "uefi-capsule-ux.tar.xz", NULL);
	blob_archive = fu_bytes_get_contents(filename_archive, error);
	if (blob_archive == NULL)
		return NULL;
	archive = fu_archive_new(blob_archive, FU_ARCHIVE_FLAG_NONE, error);
	if (archive == NULL)
		return NULL;

	/* find the closest locale match, falling back to `en` and `C` */
	for (guint i = 0; langs[i] != NULL; i++) {
		GBytes *blob_tmp;
		g_autofree gchar *fn = NULL;
		if (g_str_has_suffix(langs[i], ".UTF-8"))
			continue;
		fn = g_strdup_printf("fwupd-%s-%u-%u.bmp", langs[i], width, height);
		blob_tmp = fu_archive_lookup_by_fn(archive, fn, NULL);
		if (blob_tmp != NULL) {
			g_debug("using UX image %s", fn);
			return g_bytes_ref(blob_tmp);
		}
		g_debug("no %s found", fn);
	}

	/* we found nothing */
	langs_str = g_strjoinv(",", (gchar **)langs);
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "failed to get splash file for %s in %s",
		    langs_str,
		    datadir_pkg);
	return NULL;
}

static gboolean
fu_uefi_capsule_plugin_write_splash_data(FuUefiCapsulePlugin *self,
					 FuDevice *device,
					 GBytes *blob,
					 GError **error)
{
	guint32 screen_x, screen_y;
	gsize buf_size = g_bytes_get_size(blob);
	gssize size;
	guint32 height, width;
	guint8 csum = 0;
	fwupd_guid_t guid = {0x0};
	g_autofree gchar *capsule_path = NULL;
	g_autofree gchar *esp_path = NULL;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *directory = NULL;
	g_autoptr(GByteArray) st_cap = fu_struct_efi_capsule_header_new();
	g_autoptr(GByteArray) st_uxh = fu_struct_efi_ux_capsule_header_new();
	g_autoptr(GFile) ofile = NULL;
	g_autoptr(GOutputStream) ostream = NULL;

	/* get screen dimensions */
	if (!fu_uefi_get_framebuffer_size(&screen_x, &screen_y, error))
		return FALSE;
	if (!fu_uefi_get_bitmap_size((const guint8 *)g_bytes_get_data(blob, NULL),
				     buf_size,
				     &width,
				     &height,
				     error)) {
		g_prefix_error(error, "splash invalid: ");
		return FALSE;
	}

	/* save to a predictable filename */
	esp_path = fu_volume_get_mount_point(self->esp);
	directory = fu_uefi_get_esp_path_for_os();
	basename = g_strdup_printf("fwupd-%s.cap", FU_EFIVAR_GUID_UX_CAPSULE);
	capsule_path = g_build_filename(directory, "fw", basename, NULL);
	fn = g_build_filename(esp_path, capsule_path, NULL);
	if (!fu_path_mkdir_parent(fn, error))
		return FALSE;
	ofile = g_file_new_for_path(fn);
	ostream =
	    G_OUTPUT_STREAM(g_file_replace(ofile, NULL, FALSE, G_FILE_CREATE_NONE, NULL, error));
	if (ostream == NULL)
		return FALSE;

	fu_struct_efi_capsule_header_set_flags(st_cap,
					       EFI_CAPSULE_HEADER_FLAGS_PERSIST_ACROSS_RESET);
	if (!fwupd_guid_from_string(FU_EFIVAR_GUID_UX_CAPSULE,
				    &guid,
				    FWUPD_GUID_FLAG_MIXED_ENDIAN,
				    error))
		return FALSE;
	fu_struct_efi_capsule_header_set_guid(st_cap, &guid);
	fu_struct_efi_capsule_header_set_image_size(st_cap,
						    g_bytes_get_size(blob) +
							FU_STRUCT_EFI_CAPSULE_HEADER_SIZE +
							FU_STRUCT_EFI_UX_CAPSULE_HEADER_SIZE);

	fu_struct_efi_ux_capsule_header_set_x_offset(st_uxh, (screen_x / 2) - (width / 2));
	if (screen_y == fu_uefi_bgrt_get_height(self->bgrt)) {
		fu_struct_efi_ux_capsule_header_set_y_offset(st_uxh, (gdouble)screen_y * 0.8f);
	} else {
		fu_struct_efi_ux_capsule_header_set_y_offset(
		    st_uxh,
		    fu_uefi_bgrt_get_yoffset(self->bgrt) + fu_uefi_bgrt_get_height(self->bgrt));
	};

	/* header, payload and image has to add to zero */
	csum += fu_sum8(st_cap->data, st_cap->len);
	csum += fu_sum8(st_uxh->data, st_uxh->len);
	csum += fu_sum8_bytes(blob);
	fu_struct_efi_ux_capsule_header_set_checksum(st_uxh, 0x100 - csum);

	/* write capsule file */
	size = g_output_stream_write(ostream, st_cap->data, st_cap->len, NULL, error);
	if (size < 0)
		return FALSE;
	size = g_output_stream_write(ostream, st_uxh->data, st_uxh->len, NULL, error);
	if (size < 0)
		return FALSE;
	size = g_output_stream_write_bytes(ostream, blob, NULL, error);
	if (size < 0)
		return FALSE;

	/* write display capsule location as UPDATE_INFO */
	return fu_uefi_device_write_update_info(FU_UEFI_DEVICE(device),
						capsule_path,
						"fwupd-ux-capsule",
						FU_EFIVAR_GUID_UX_CAPSULE,
						error);
}

static gboolean
fu_uefi_capsule_plugin_update_splash(FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuUefiCapsulePlugin *self = FU_UEFI_CAPSULE_PLUGIN(plugin);
	guint best_idx = G_MAXUINT;
	guint32 lowest_border_pixels = G_MAXUINT;
	guint32 screen_height = 768;
	guint32 screen_width = 1024;
	g_autoptr(GBytes) image_bmp = NULL;

	struct {
		guint32 width;
		guint32 height;
	} sizes[] = {{640, 480}, /* matching the sizes in po/make-images */
		     {800, 600},
		     {1024, 768},
		     {1920, 1080},
		     {3840, 2160},
		     {5120, 2880},
		     {5688, 3200},
		     {7680, 4320},
		     {0, 0}};

	/* no UX capsule support, so deleting var if it exists */
	if (fu_device_has_private_flag(device, FU_UEFI_DEVICE_FLAG_NO_UX_CAPSULE)) {
		g_info("not providing UX capsule");
		return fu_efivar_delete(FU_EFIVAR_GUID_FWUPDATE, "fwupd-ux-capsule", error);
	}

	/* get the boot graphics resource table data */
	if (!fu_uefi_bgrt_get_supported(self->bgrt)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "BGRT is not supported");
		return FALSE;
	}
	if (!fu_uefi_get_framebuffer_size(&screen_width, &screen_height, error))
		return FALSE;
	g_debug("framebuffer size %" G_GUINT32_FORMAT " x%" G_GUINT32_FORMAT,
		screen_width,
		screen_height);

	/* find the 'best sized' pre-generated image */
	for (guint i = 0; sizes[i].width != 0; i++) {
		guint32 border_pixels;

		/* disregard any images that are bigger than the screen */
		if (sizes[i].width > screen_width)
			continue;
		if (sizes[i].height > screen_height)
			continue;

		/* is this the best fit for the display */
		border_pixels = (screen_width * screen_height) - (sizes[i].width * sizes[i].height);
		if (border_pixels < lowest_border_pixels) {
			lowest_border_pixels = border_pixels;
			best_idx = i;
		}
	}
	if (best_idx == G_MAXUINT) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to find a suitable image to use");
		return FALSE;
	}

	/* get the raw data */
	image_bmp = fu_uefi_capsule_plugin_get_splash_data(sizes[best_idx].width,
							   sizes[best_idx].height,
							   error);
	if (image_bmp == NULL)
		return FALSE;

	/* perform the upload */
	return fu_uefi_capsule_plugin_write_splash_data(self, device, image_bmp, error);
}

static gboolean
fu_uefi_capsule_plugin_write_firmware(FuPlugin *plugin,
				      FuDevice *device,
				      GBytes *blob_fw,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	const gchar *str;
	guint32 flashes_left;
	g_autoptr(GError) error_splash = NULL;

	/* test the flash counter */
	flashes_left = fu_device_get_flashes_left(device);
	if (flashes_left > 0) {
		g_debug("%s has %" G_GUINT32_FORMAT " flashes left",
			fu_device_get_name(device),
			flashes_left);
		if ((flags & FWUPD_INSTALL_FLAG_FORCE) == 0 && flashes_left <= 2) {
			g_set_error(
			    error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "%s only has %" G_GUINT32_FORMAT " flashes left -- "
			    "see https://github.com/fwupd/fwupd/wiki/Dell-TPM:-flashes-left for "
			    "more information.",
			    fu_device_get_name(device),
			    flashes_left);
			return FALSE;
		}
	}

	/* TRANSLATORS: this is shown when updating the firmware after the reboot */
	str = _("Installing firmware update…");
	g_assert(str != NULL);

	/* perform the update */
	fu_progress_set_status(progress, FWUPD_STATUS_SCHEDULING);
	if (!fu_uefi_capsule_plugin_update_splash(plugin, device, &error_splash))
		g_info("failed to upload UEFI UX capsule text: %s", error_splash->message);

	return fu_device_write_firmware(device, blob_fw, progress, flags, error);
}

static void
fu_uefi_capsule_plugin_load_config(FuPlugin *plugin, FuDevice *device)
{
	guint64 sz_reqd = 0;
	g_autofree gchar *require_esp_free_space = NULL;
	g_autoptr(GError) error_local = NULL;

	/* parse free space needed for ESP */
	require_esp_free_space =
	    fu_plugin_get_config_value(plugin,
				       "RequireESPFreeSpace",
				       FU_UEFI_CAPSULE_CONFIG_DEFAULT_REQUIRE_ESP_FREE_SPACE);
	if (!fu_strtoull(require_esp_free_space, &sz_reqd, 0, G_MAXUINT64, &error_local))
		g_warning("invalid ESP free space specified: %s", error_local->message);
	fu_uefi_device_set_require_esp_free_space(FU_UEFI_DEVICE(device), sz_reqd);

	/* shim used for SB or not? */
	if (!fu_plugin_get_config_value_boolean(
		plugin,
		"DisableShimForSecureBoot",
		FU_UEFI_CAPSULE_CONFIG_DEFAULT_DISABLE_SHIM_FOR_SECURE_BOOT))
		fu_device_add_private_flag(device, FU_UEFI_DEVICE_FLAG_USE_SHIM_FOR_SB);

	/* enable the fwupd.efi debug log? */
	if (fu_plugin_get_config_value_boolean(plugin,
					       "EnableEfiDebugging",
					       FU_UEFI_CAPSULE_CONFIG_DEFAULT_ENABLE_EFI_DEBUGGING))
		fu_device_add_private_flag(device, FU_UEFI_DEVICE_FLAG_ENABLE_EFI_DEBUGGING);
}

static gboolean
fu_uefi_capsule_plugin_is_esp_linux(FuVolume *esp, GError **error)
{
	const gchar *basenames_root[] = {"grub", "shim", "systemd-boot", NULL};
	const gchar *efi_suffix;
	g_autofree gchar *basenames_str = NULL;
	g_autofree gchar *mount_point = fu_volume_get_mount_point(esp);
	g_auto(GStrv) basenames = g_new0(gchar *, G_N_ELEMENTS(basenames_root));
	g_autoptr(GPtrArray) files = NULL;

	/* build a list of possible files, e.g. grubx64.efi, shimaa64.efi, etc. */
	efi_suffix = fu_uefi_bootmgr_get_suffix(error);
	if (efi_suffix == NULL)
		return FALSE;
	for (guint i = 0; basenames_root[i] != NULL; i++)
		basenames[i] = g_strdup_printf("%s%s.efi", basenames_root[i], efi_suffix);

	/* look for any likely basenames */
	files = fu_path_get_files(mount_point, error);
	if (files == NULL)
		return FALSE;
	for (guint i = 0; i < files->len; i++) {
		const gchar *fn = g_ptr_array_index(files, i);
		g_autofree gchar *basename = g_path_get_basename(fn);
		g_autofree gchar *basename_lower = g_utf8_strdown(basename, -1);
		if (g_strv_contains((const gchar *const *)basenames, basename_lower)) {
			g_info("found %s which indicates a Linux ESP, using %s", fn, mount_point);
			return TRUE;
		}
	}

	/* failed */
	basenames_str = g_strjoinv("|", (gchar **)basenames);
	g_set_error(error,
		    G_IO_ERROR,
		    G_IO_ERROR_NOT_FOUND,
		    "did not find %s in %s",
		    basenames_str,
		    mount_point);
	return FALSE;
}

static gint
fu_uefi_capsule_plugin_sort_volume_score_cb(gconstpointer a, gconstpointer b, gpointer user_data)
{
	GHashTable *esp_scores = (GHashTable *)user_data;
	guint esp1_score = GPOINTER_TO_UINT(g_hash_table_lookup(esp_scores, *((FuVolume **)a)));
	guint esp2_score = GPOINTER_TO_UINT(g_hash_table_lookup(esp_scores, *((FuVolume **)b)));
	if (esp1_score < esp2_score)
		return 1;
	if (esp1_score > esp2_score)
		return -1;
	return 0;
}

static FuVolume *
fu_uefi_capsule_plugin_get_default_esp(FuPlugin *plugin, GError **error)
{
	g_autoptr(GPtrArray) esp_volumes = NULL;

	/* show which volumes we're choosing from */
	esp_volumes = fu_context_get_esp_volumes(fu_plugin_get_context(plugin), error);
	if (esp_volumes == NULL)
		return NULL;

	/* we found more than one: lets look for the best one */
	if (esp_volumes->len > 1) {
		g_autoptr(GString) str = g_string_new("more than one ESP possible:");
		g_autoptr(GHashTable) esp_scores = g_hash_table_new(g_direct_hash, g_direct_equal);
		for (guint i = 0; i < esp_volumes->len; i++) {
			FuVolume *esp = g_ptr_array_index(esp_volumes, i);
			guint score = 0;
			g_autoptr(FuDeviceLocker) locker = NULL;
			g_autoptr(GError) error_local = NULL;

			/* ignore the volume completely if we cannot mount it */
			locker = fu_volume_locker(esp, &error_local);
			if (locker == NULL) {
				g_warning("failed to mount ESP: %s", error_local->message);
				continue;
			}

			/* big partitions are better than small partitions */
			score += fu_volume_get_size(esp) / (1024 * 1024);

			/* prefer linux ESP */
			if (!fu_uefi_capsule_plugin_is_esp_linux(esp, &error_local)) {
				g_debug("not a Linux ESP: %s", error_local->message);
			} else {
				score += 0x10000;
			}
			g_hash_table_insert(esp_scores, (gpointer)esp, GUINT_TO_POINTER(score));
		}
		g_ptr_array_sort_with_data(esp_volumes,
					   fu_uefi_capsule_plugin_sort_volume_score_cb,
					   esp_scores);
		for (guint i = 0; i < esp_volumes->len; i++) {
			FuVolume *esp = g_ptr_array_index(esp_volumes, i);
			guint score = GPOINTER_TO_UINT(g_hash_table_lookup(esp_scores, esp));
			g_string_append_printf(str, "\n - 0x%x:\t%s", score, fu_volume_get_id(esp));
		}
		g_debug("%s", str->str);
	}

	/* "success" */
	return g_object_ref(g_ptr_array_index(esp_volumes, 0));
}

static void
fu_uefi_capsule_plugin_register_proxy_device(FuPlugin *plugin, FuDevice *device)
{
	FuUefiCapsulePlugin *self = FU_UEFI_CAPSULE_PLUGIN(plugin);
	g_autoptr(FuUefiDevice) dev = NULL;
	g_autoptr(GError) error_local = NULL;

	/* load all configuration variables */
	dev = fu_uefi_backend_device_new_from_dev(FU_UEFI_BACKEND(self->backend), device);
	fu_uefi_capsule_plugin_load_config(plugin, FU_DEVICE(dev));
	if (self->esp == NULL)
		self->esp = fu_uefi_capsule_plugin_get_default_esp(plugin, &error_local);
	if (self->esp == NULL) {
		fu_device_inhibit(device, "no-esp", error_local->message);
	} else {
		fu_uefi_device_set_esp(dev, self->esp);
		fu_device_uninhibit(device, "no-esp");
	}
	fu_plugin_device_add(plugin, FU_DEVICE(dev));
}

static void
fu_uefi_capsule_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	if (fu_device_get_metadata(device, FU_DEVICE_METADATA_UEFI_DEVICE_KIND) != NULL) {
		if (fu_device_get_guid_default(device) == NULL) {
			g_autofree gchar *dbg = fu_device_to_string(device);
			g_warning("cannot create proxy device as no GUID: %s", dbg);
			return;
		}
		fu_uefi_capsule_plugin_register_proxy_device(plugin, device);
	}
}

static const gchar *
fu_uefi_capsule_plugin_uefi_type_to_string(FuUefiDeviceKind device_kind)
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
fu_uefi_capsule_plugin_get_name_for_type(FuPlugin *plugin, FuUefiDeviceKind device_kind)
{
	GString *display_name;

	/* set Display Name prefix for capsules that are not PCI cards */
	display_name = g_string_new(fu_uefi_capsule_plugin_uefi_type_to_string(device_kind));
	if (device_kind == FU_UEFI_DEVICE_KIND_DEVICE_FIRMWARE)
		g_string_prepend(display_name, "UEFI ");
	return g_string_free(display_name, FALSE);
}

static gboolean
fu_uefi_capsule_plugin_coldplug_device(FuPlugin *plugin, FuUefiDevice *dev, GError **error)
{
	FuUefiCapsulePlugin *self = FU_UEFI_CAPSULE_PLUGIN(plugin);
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuUefiDeviceKind device_kind;

	/* probe to get add GUIDs (and hence any quirk fixups) */
	if (!fu_device_probe(FU_DEVICE(dev), error))
		return FALSE;
	if (!fu_device_setup(FU_DEVICE(dev), error))
		return FALSE;

	/* if not already set by quirks */
	if (fu_context_has_hwid_flag(ctx, "use-legacy-bootmgr-desc")) {
		fu_device_add_private_flag(FU_DEVICE(dev),
					   FU_UEFI_DEVICE_FLAG_USE_LEGACY_BOOTMGR_DESC);
	}
	if (fu_context_has_hwid_flag(ctx, "supports-boot-order-lock")) {
		fu_device_add_private_flag(FU_DEVICE(dev),
					   FU_UEFI_DEVICE_FLAG_SUPPORTS_BOOT_ORDER_LOCK);
	}
	if (fu_context_has_hwid_flag(ctx, "no-ux-capsule"))
		fu_device_add_private_flag(FU_DEVICE(dev), FU_UEFI_DEVICE_FLAG_NO_UX_CAPSULE);
	if (fu_context_has_hwid_flag(ctx, "no-lid-closed"))
		fu_device_add_internal_flag(FU_DEVICE(dev), FU_DEVICE_INTERNAL_FLAG_NO_LID_CLOSED);

	/* detected InsydeH2O */
	if (self->acpi_uefi != NULL &&
	    fu_acpi_uefi_cod_indexed_filename(FU_ACPI_UEFI(self->acpi_uefi))) {
		fu_device_add_private_flag(FU_DEVICE(dev),
					   FU_UEFI_DEVICE_FLAG_COD_INDEXED_FILENAME);
	}

	/* set fallback name if nothing else is set */
	device_kind = fu_uefi_device_get_kind(dev);
	if (fu_device_get_name(FU_DEVICE(dev)) == NULL) {
		g_autofree gchar *name = NULL;
		name = fu_uefi_capsule_plugin_get_name_for_type(plugin, device_kind);
		if (name != NULL)
			fu_device_set_name(FU_DEVICE(dev), name);
		if (device_kind != FU_UEFI_DEVICE_KIND_SYSTEM_FIRMWARE) {
			fu_device_add_internal_flag(FU_DEVICE(dev),
						    FU_DEVICE_INTERNAL_FLAG_MD_SET_NAME_CATEGORY);
		}
	}
	/* set fallback vendor if nothing else is set */
	if (fu_device_get_vendor(FU_DEVICE(dev)) == NULL &&
	    device_kind == FU_UEFI_DEVICE_KIND_SYSTEM_FIRMWARE) {
		const gchar *vendor = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_MANUFACTURER);
		if (vendor != NULL)
			fu_device_set_vendor(FU_DEVICE(dev), vendor);
	}

	/* set vendor ID as the BIOS vendor */
	if (device_kind != FU_UEFI_DEVICE_KIND_FMP) {
		const gchar *dmi_vendor;
		dmi_vendor = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BIOS_VENDOR);
		if (dmi_vendor != NULL) {
			g_autofree gchar *vendor_id = g_strdup_printf("DMI:%s", dmi_vendor);
			fu_device_add_vendor_id(FU_DEVICE(dev), vendor_id);
		}
	}

	/* success */
	return TRUE;
}

static void
fu_uefi_capsule_plugin_test_secure_boot(FuPlugin *plugin)
{
	const gchar *result_str = "Disabled";
	if (fu_efivar_secure_boot_enabled(NULL))
		result_str = "Enabled";
	fu_plugin_add_report_metadata(plugin, "SecureBoot", result_str);
}

static gboolean
fu_uefi_capsule_plugin_parse_acpi_uefi(FuUefiCapsulePlugin *self, GError **error)
{
	g_autofree gchar *fn = NULL;
	g_autofree gchar *path = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* if we have a table, parse it and validate it */
	path = fu_path_from_kind(FU_PATH_KIND_ACPI_TABLES);
	fn = g_build_filename(path, "UEFI", NULL);
	blob = fu_bytes_get_contents(fn, error);
	if (blob == NULL)
		return FALSE;
	self->acpi_uefi = fu_acpi_uefi_new();
	return fu_firmware_parse(self->acpi_uefi, blob, FWUPD_INSTALL_FLAG_NONE, error);
}

static gboolean
fu_uefi_capsule_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuUefiCapsulePlugin *self = FU_UEFI_CAPSULE_PLUGIN(plugin);
	FuContext *ctx = fu_plugin_get_context(plugin);
	guint64 nvram_total;
	g_autofree gchar *esp_path = NULL;
	g_autofree gchar *nvram_total_str = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GError) error_acpi_uefi = NULL;

	/* don't let user's environment influence test suite failures */
	if (g_getenv("FWUPD_UEFI_TEST") != NULL)
		return TRUE;

	/* for the uploaded report */
	if (fu_context_has_hwid_flag(ctx, "use-legacy-bootmgr-desc"))
		fu_plugin_add_report_metadata(plugin, "BootMgrDesc", "legacy");

	/* some platforms have broken SMBIOS data */
	if (fu_context_has_hwid_flag(ctx, "uefi-force-enable"))
		return TRUE;

	/* use GRUB to load updates */
	if (fu_plugin_get_config_value_boolean(
		plugin,
		"EnableGrubChainLoad",
		FU_UEFI_CAPSULE_CONFIG_DEFAULT_ENABLE_GRUB_CHAIN_LOAD)) {
		fu_uefi_backend_set_device_gtype(FU_UEFI_BACKEND(self->backend),
						 FU_TYPE_UEFI_GRUB_DEVICE);
	}

	/* check we can use this backend */
	if (!fu_backend_setup(self->backend, progress, &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_WRITE)) {
			fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_EFIVAR_NOT_MOUNTED);
			fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE);
			fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_USER_WARNING);
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	/* are the EFI dirs set up so we can update each device */
	if (!fu_efivar_supported(error))
		return FALSE;
	nvram_total = fu_efivar_space_used(error);
	if (nvram_total == G_MAXUINT64)
		return FALSE;
	nvram_total_str = g_strdup_printf("%" G_GUINT64_FORMAT, nvram_total);
	fu_plugin_add_report_metadata(plugin, "EfivarNvramUsed", nvram_total_str);

	/* override the default ESP path */
	esp_path = fu_plugin_get_config_value(plugin, "OverrideESPMountPoint", NULL);
	if (esp_path != NULL) {
		self->esp = fu_volume_new_esp_for_path(esp_path, error);
		if (self->esp == NULL) {
			g_prefix_error(error,
				       "invalid OverrideESPMountPoint=%s "
				       "specified in config: ",
				       esp_path);
			return FALSE;
		}
	}

	/* we use this both for quirking the CoD implementation sanity and the CoD filename */
	if (!fu_uefi_capsule_plugin_parse_acpi_uefi(self, &error_acpi_uefi))
		g_debug("failed to load ACPI UEFI table: %s", error_acpi_uefi->message);

	/* test for invalid ESP in coldplug, and set the update-error rather
	 * than showing no output if the plugin had self-disabled here */
	return TRUE;
}

static gboolean
fu_uefi_capsule_plugin_unlock(FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuUefiDevice *device_uefi = FU_UEFI_DEVICE(device);
	FuDevice *device_alt = NULL;
	FwupdDeviceFlags device_flags_alt = 0;
	guint flashes_left = 0;
	guint flashes_left_alt = 0;

	if (fu_uefi_device_get_kind(device_uefi) != FU_UEFI_DEVICE_KIND_DELL_TPM_FIRMWARE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Unable to unlock %s",
			    fu_device_get_name(device));
		return FALSE;
	}

	/* for unlocking TPM1.2 <-> TPM2.0 switching */
	g_debug("unlocking upgrades for: %s (%s)",
		fu_device_get_name(device),
		fu_device_get_id(device));
	device_alt = fu_device_get_alternate(device);
	if (device_alt == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "No alternate device for %s",
			    fu_device_get_name(device));
		return FALSE;
	}
	g_debug("preventing upgrades for: %s (%s)",
		fu_device_get_name(device_alt),
		fu_device_get_id(device_alt));

	flashes_left = fu_device_get_flashes_left(device);
	flashes_left_alt = fu_device_get_flashes_left(device_alt);
	if (flashes_left == 0) {
		/* flashes left == 0 on both means no flashes left */
		if (flashes_left_alt == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "ERROR: %s has no flashes left.",
				    fu_device_get_name(device));
			/* flashes left == 0 on just unlocking device is ownership */
		} else {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "ERROR: %s is currently OWNED. "
				    "Ownership must be removed to switch modes.",
				    fu_device_get_name(device_alt));
		}
		return FALSE;
	}

	/* clone the info from real device but prevent it from being flashed */
	device_flags_alt = fu_device_get_flags(device_alt);
	fu_device_set_flags(device, device_flags_alt);
	fu_device_inhibit(device_alt, "alt-device", "Preventing upgrades as alternate");

	/* make sure that this unlocked device can be updated */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_version(device, "0.0.0.0");
	return TRUE;
}

static void
fu_plugin_uefi_update_state_notify_cb(GObject *object, GParamSpec *pspec, FuPlugin *plugin)
{
	FuDevice *device = FU_DEVICE(object);
	GPtrArray *devices;
	g_autofree gchar *msg = NULL;

	/* device is not in needs-reboot state */
	if (fu_device_get_update_state(device) != FWUPD_UPDATE_STATE_NEEDS_REBOOT)
		return;

	/* only do this on hardware that cannot coalesce multiple capsules */
	if (!fu_context_has_hwid_flag(fu_plugin_get_context(plugin), "no-coalesce"))
		return;

	/* mark every other device for this plugin as non-updatable */
	msg = g_strdup_printf("Cannot update as %s [%s] needs reboot",
			      fu_device_get_name(device),
			      fu_device_get_id(device));
	devices = fu_plugin_get_devices(plugin);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device_tmp = g_ptr_array_index(devices, i);
		if (device_tmp == device)
			continue;
		fu_device_inhibit(device_tmp, "no-coalesce", msg);
	}
}

static gboolean
fu_uefi_capsule_plugin_check_cod_support(FuUefiCapsulePlugin *self, GError **error)
{
	gsize bufsz = 0;
	guint64 value = 0;
	g_autofree guint8 *buf = NULL;

	if (!fu_efivar_get_data(FU_EFIVAR_GUID_EFI_GLOBAL,
				"OsIndicationsSupported",
				&buf,
				&bufsz,
				NULL,
				error)) {
		g_prefix_error(error, "failed to read EFI variable: ");
		return FALSE;
	}
	if (!fu_memread_uint64_safe(buf, bufsz, 0x0, &value, G_LITTLE_ENDIAN, error))
		return FALSE;
	if ((value & EFI_OS_INDICATIONS_FILE_CAPSULE_DELIVERY_SUPPORTED) == 0) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "Capsule-on-Disk is not supported");
		return FALSE;
	}

	/* no table, nothing to check */
	if (self->acpi_uefi == NULL)
		return TRUE;
	return fu_acpi_uefi_cod_functional(FU_ACPI_UEFI(self->acpi_uefi), error);
}

static gboolean
fu_uefi_capsule_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuUefiCapsulePlugin *self = FU_UEFI_CAPSULE_PLUGIN(plugin);
	const gchar *str;
	gboolean has_fde = FALSE;
	g_autoptr(GError) error_udisks2 = NULL;
	g_autoptr(GError) error_fde = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 63, "find-esp");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "check-cod");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 8, "check-bitlocker");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "coldplug");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 26, "add-devices");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "setup-bgrt");

	if (self->esp == NULL) {
		self->esp = fu_uefi_capsule_plugin_get_default_esp(plugin, &error_udisks2);
		if (self->esp == NULL) {
			fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_ESP_NOT_FOUND);
			fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE);
			fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_USER_WARNING);
			g_warning("cannot find default ESP: %s", error_udisks2->message);
		} else {
			g_autofree gchar *kind = fu_volume_get_partition_kind(self->esp);
			if (g_strcmp0(kind, FU_VOLUME_KIND_ESP) != 0) {
				fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_ESP_NOT_VALID);
				fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_USER_WARNING);
			}
		}
	}
	fu_progress_step_done(progress);

	/* firmware may lie */
	if (!fu_plugin_get_config_value_boolean(
		plugin,
		"DisableCapsuleUpdateOnDisk",
		FU_UEFI_CAPSULE_CONFIG_DEFAULT_DISABLE_CAPSULE_UPDATE_ON_DISK)) {
		g_autoptr(GError) error_cod = NULL;
		if (!fu_uefi_capsule_plugin_check_cod_support(self, &error_cod)) {
			g_debug("not using CapsuleOnDisk support: %s", error_cod->message);
		} else {
			fu_uefi_backend_set_device_gtype(FU_UEFI_BACKEND(self->backend),
							 FU_TYPE_UEFI_COD_DEVICE);
		}
	}
	fu_progress_step_done(progress);

	/*  warn the user that BitLocker might ask for recovery key after fw update */
	if (!fu_common_check_full_disk_encryption(&error_fde)) {
		g_debug("FDE in use, set flag: %s", error_fde->message);
		has_fde = TRUE;
	}
	fu_progress_step_done(progress);

	/* add each device */
	if (!fu_backend_coldplug(self->backend, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);
	devices = fu_backend_get_devices(self->backend);
	for (guint i = 0; i < devices->len; i++) {
		FuUefiDevice *dev = g_ptr_array_index(devices, i);
		g_autoptr(GError) error_device = NULL;

		if (self->esp != NULL)
			fu_uefi_device_set_esp(dev, self->esp);
		if (!fu_uefi_capsule_plugin_coldplug_device(plugin, dev, &error_device)) {
			if (g_error_matches(error_device, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
				g_warning("skipping device that failed coldplug: %s",
					  error_device->message);
				continue;
			}
			g_propagate_error(error, g_steal_pointer(&error_device));
			return FALSE;
		}
		fu_device_add_flag(FU_DEVICE(dev), FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_device_add_flag(FU_DEVICE(dev), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);

		/* only system firmware "BIOS" can change the PCRx registers */
		if (fu_uefi_device_get_kind(dev) == FU_UEFI_DEVICE_KIND_SYSTEM_FIRMWARE && has_fde)
			fu_device_add_flag(FU_DEVICE(dev), FWUPD_DEVICE_FLAG_AFFECTS_FDE);

		/* load all configuration variables */
		fu_uefi_capsule_plugin_load_config(plugin, FU_DEVICE(dev));

		/* watch in case we set needs-reboot in the engine */
		g_signal_connect(FU_DEVICE(dev),
				 "notify::update-state",
				 G_CALLBACK(fu_plugin_uefi_update_state_notify_cb),
				 plugin);

		fu_plugin_device_add(plugin, FU_DEVICE(dev));
	}
	fu_progress_step_done(progress);

	/* for debugging problems later */
	fu_uefi_capsule_plugin_test_secure_boot(plugin);
	if (!fu_uefi_bgrt_setup(self->bgrt, &error_local))
		g_debug("BGRT setup failed: %s", error_local->message);
	str = fu_uefi_bgrt_get_supported(self->bgrt) ? "Enabled" : "Disabled";
	g_info("UX capsule support : %s", str);
	fu_plugin_add_report_metadata(plugin, "UEFIUXCapsule", str);
	fu_progress_step_done(progress);

	return TRUE;
}

static void
fu_uefi_capsule_plugin_init(FuUefiCapsulePlugin *self)
{
	self->bgrt = fu_uefi_bgrt_new();
	fu_plugin_add_flag(FU_PLUGIN(self), FWUPD_PLUGIN_FLAG_MEASURE_SYSTEM_INTEGRITY);
}

static void
fu_uefi_capsule_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuUefiCapsulePlugin *self = FU_UEFI_CAPSULE_PLUGIN(plugin);
	g_autoptr(GError) error_local = NULL;

	self->backend = fu_uefi_backend_new(ctx);
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_RUN_AFTER, "upower");
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_METADATA_SOURCE, "tpm");
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_METADATA_SOURCE, "dell");
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_METADATA_SOURCE, "linux_lockdown");
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_METADATA_SOURCE, "acpi_phat");
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_CONFLICTS, "uefi"); /* old name */
	fu_plugin_add_firmware_gtype(FU_PLUGIN(self), NULL, FU_TYPE_ACPI_UEFI);
	fu_plugin_add_firmware_gtype(FU_PLUGIN(self), NULL, FU_TYPE_UEFI_UPDATE_INFO);

	/* add a requirement on the fwupd-efi version -- which can change  */
	if (!fu_uefi_capsule_plugin_fwupd_efi_probe(self, &error_local))
		g_debug("failed to get fwupd-efi runtime version: %s", error_local->message);
}

static void
fu_uefi_capsule_finalize(GObject *obj)
{
	FuUefiCapsulePlugin *self = FU_UEFI_CAPSULE_PLUGIN(obj);
	if (self->esp != NULL)
		g_object_unref(self->esp);
	if (self->fwupd_efi_file != NULL)
		g_object_unref(self->fwupd_efi_file);
	if (self->acpi_uefi != NULL)
		g_object_unref(self->acpi_uefi);
	if (self->fwupd_efi_monitor != NULL) {
		g_file_monitor_cancel(self->fwupd_efi_monitor);
		g_object_unref(self->fwupd_efi_monitor);
	}
	if (self->backend != NULL)
		g_object_unref(self->backend);
	if (self->bgrt != NULL)
		g_object_unref(self->bgrt);
	G_OBJECT_CLASS(fu_uefi_capsule_plugin_parent_class)->finalize(obj);
}

static void
fu_uefi_capsule_plugin_class_init(FuUefiCapsulePluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_uefi_capsule_finalize;
	plugin_class->constructed = fu_uefi_capsule_plugin_constructed;
	plugin_class->to_string = fu_uefi_capsule_plugin_to_string;
	plugin_class->clear_results = fu_uefi_capsule_plugin_clear_results;
	plugin_class->add_security_attrs = fu_uefi_capsule_plugin_add_security_attrs;
	plugin_class->device_registered = fu_uefi_capsule_plugin_device_registered;
	plugin_class->startup = fu_uefi_capsule_plugin_startup;
	plugin_class->unlock = fu_uefi_capsule_plugin_unlock;
	plugin_class->coldplug = fu_uefi_capsule_plugin_coldplug;
	plugin_class->write_firmware = fu_uefi_capsule_plugin_write_firmware;
}
