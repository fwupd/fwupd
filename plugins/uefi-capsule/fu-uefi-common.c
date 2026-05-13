/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-capsule-device.h"
#include "fu-uefi-common.h"

static const gchar *
fu_uefi_bootmgr_get_suffix(FuPathStore *pstore, GError **error)
{
	guint64 firmware_bits;
	g_autofree gchar *sysfsefidir = NULL;
	struct {
		guint64 bits;
		const gchar *arch;
	} suffixes[] = {
#if defined(__x86_64__)
	    {64, "x64"},
#elif defined(__aarch64__)
	    {64, "aa64"},
#elif defined(__loongarch_lp64)
	    {64, "loongarch64"},
#elif (defined(__riscv) && __riscv_xlen == 64)
	    {64, "riscv64"},
#endif
#if defined(__i386__) || defined(__i686__)
	    {32, "ia32"},
#elif defined(__arm__)
	    {32, "arm"},
#endif
	    {0, NULL}};

	sysfsefidir =
	    fu_path_store_build_filename(pstore, error, FU_PATH_KIND_SYSFSDIR_FW, "efi", NULL);
	if (sysfsefidir == NULL)
		return NULL;
	firmware_bits = fu_uefi_read_file_as_uint64(sysfsefidir, "fw_platform_size");
	if (firmware_bits == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "%s/fw_platform_size cannot be found",
			    sysfsefidir);
		return NULL;
	}
	for (guint i = 0; suffixes[i].arch != NULL; i++) {
		if (firmware_bits != suffixes[i].bits)
			continue;
		return suffixes[i].arch;
	}

	/* this should exist */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_FOUND,
		    "%s/fw_platform_size has unknown value %" G_GUINT64_FORMAT,
		    sysfsefidir,
		    firmware_bits);
	return NULL;
}

gchar *
fu_uefi_capsule_build_app_basename(FuPathStore *pstore, const gchar *cmd, GError **error)
{
	const gchar *suffix;

	suffix = fu_uefi_bootmgr_get_suffix(pstore, error);
	if (suffix == NULL)
		return NULL;
	return g_strdup_printf("%s%s.efi", cmd, suffix);
}

/**
 * fu_uefi_get_built_app_path:
 * @basename: the binary basename, e.g. `shimx64.efi`
 * @error: (nullable): optional return location for an error
 *
 * Gets the path intended to be used for an EFI binary on the local system.
 * The binary is matched against the correct architecture and if secure
 * boot is enabled.
 *
 * Returns: The full path to the binary, or %NULL if not found
 *
 * Since: 1.8.1
 **/
gchar *
fu_uefi_get_built_app_path(FuPathStore *pstore,
			   FuEfivars *efivars,
			   const gchar *basename,
			   GError **error)
{
	const gchar *prefix = NULL;
	g_autofree gchar *source_path = NULL;
	g_autofree gchar *source_path_signed = NULL;
	g_autofree gchar *basename_signed = g_strdup_printf("%s.signed", basename);
	gboolean secureboot_enabled = FALSE;
	gboolean source_path_exists = FALSE;
	gboolean source_path_signed_exists = FALSE;
	g_autoptr(GError) error_local = NULL;

	prefix = fu_path_store_get_path(pstore, FU_PATH_KIND_EFIAPPDIR, error);
	if (prefix == NULL)
		return NULL;

	source_path = g_build_filename(prefix, basename, NULL);
	source_path_signed = g_build_filename(prefix, basename_signed, NULL);
	source_path_exists = g_file_test(source_path, G_FILE_TEST_EXISTS);
	source_path_signed_exists = g_file_test(source_path_signed, G_FILE_TEST_EXISTS);

	if (!fu_efivars_get_secure_boot(efivars, &secureboot_enabled, &error_local))
		g_debug("ignoring: %s", error_local->message);
	if (secureboot_enabled) {
		if (!source_path_signed_exists) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "%s cannot be found",
				    source_path_signed);
			return NULL;
		}
		return g_steal_pointer(&source_path_signed);
	}

	if (source_path_exists)
		return g_steal_pointer(&source_path);
	if (source_path_signed_exists)
		return g_steal_pointer(&source_path_signed);

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_FOUND,
		    "%s and %s cannot be found",
		    source_path,
		    source_path_signed);
	return NULL;
}

gboolean
fu_uefi_get_framebuffer_size(FuPathStore *pstore, guint32 *width, guint32 *height, GError **error)
{
	guint32 height_tmp;
	guint32 width_tmp;
	g_autofree gchar *fbdir = NULL;

	fbdir = fu_path_store_build_filename(pstore,
					     error,
					     FU_PATH_KIND_SYSFSDIR_DRIVERS,
					     "efi-framebuffer",
					     "efi-framebuffer.0",
					     NULL);
	if (fbdir == NULL)
		return FALSE;
	if (!g_file_test(fbdir, G_FILE_TEST_EXISTS)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "EFI framebuffer not found");
		return FALSE;
	}
	height_tmp = fu_uefi_read_file_as_uint64(fbdir, "height");
	width_tmp = fu_uefi_read_file_as_uint64(fbdir, "width");
	if (width_tmp == 0 || height_tmp == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "EFI framebuffer has invalid size "
			    "%" G_GUINT32_FORMAT "x%" G_GUINT32_FORMAT,
			    width_tmp,
			    height_tmp);
		return FALSE;
	}
	if (width != NULL)
		*width = width_tmp;
	if (height != NULL)
		*height = height_tmp;
	return TRUE;
}

guint64
fu_uefi_read_file_as_uint64(const gchar *path, const gchar *attr_name)
{
	guint64 tmp = 0;
	g_autofree gchar *data = NULL;
	g_autofree gchar *fn = g_build_filename(path, attr_name, NULL);
	g_autoptr(GError) error_local = NULL;

	if (!g_file_get_contents(fn, &data, NULL, NULL))
		return 0x0;
	if (!fu_strtoull(data, &tmp, 0, G_MAXUINT64, FU_INTEGER_BASE_AUTO, &error_local)) {
		g_warning("invalid string specified: %s", error_local->message);
		return G_MAXUINT64;
	}
	return tmp;
}

gboolean
fu_uefi_esp_target_verify(const gchar *fn_src, const gchar *fn_dst)
{
	gsize len = 0;
	g_autofree gchar *source_csum = NULL;
	g_autofree gchar *source_data = NULL;
	g_autofree gchar *target_csum = NULL;
	g_autofree gchar *target_data = NULL;

	/* nothing in target yet */
	if (!g_file_test(fn_dst, G_FILE_TEST_EXISTS))
		return FALSE;

	/* test if the file needs to be updated */
	if (!g_file_get_contents(fn_src, &source_data, &len, NULL))
		return FALSE;
	source_csum = g_compute_checksum_for_data(G_CHECKSUM_SHA256, (guchar *)source_data, len);
	if (!g_file_get_contents(fn_dst, &target_data, &len, NULL))
		return FALSE;
	target_csum = g_compute_checksum_for_data(G_CHECKSUM_SHA256, (guchar *)target_data, len);
	return g_strcmp0(target_csum, source_csum) == 0;
}

gboolean
fu_uefi_esp_target_copy(const gchar *fn_src, const gchar *fn_dst, GError **error)
{
	g_autoptr(GFile) source_file = g_file_new_for_path(fn_src);
	g_autoptr(GFile) target_file = g_file_new_for_path(fn_dst);

	if (!g_file_copy(source_file,
			 target_file,
			 G_FILE_COPY_OVERWRITE,
			 NULL,
			 NULL,
			 NULL,
			 error)) {
		g_prefix_error(error, "failed to copy %s to %s: ", fn_src, fn_dst);
		return FALSE;
	}

	return TRUE;
}
