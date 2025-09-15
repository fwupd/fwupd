/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-capsule-device.h"
#include "fu-uefi-common.h"

static gboolean fu_uefi_get_os_paths(gchar **os_release_id_out, gchar **id_like_out);

static const gchar *
fu_uefi_bootmgr_get_suffix(GError **error)
{
	guint64 firmware_bits;
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
	g_autofree gchar *sysfsfwdir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR_FW);
	g_autofree gchar *sysfsefidir = g_build_filename(sysfsfwdir, "efi", NULL);
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

/**
 * fu_uefi_get_os_paths:
 * @os_release_id_out: (out) (optional): Output for the OS release ID
 * @id_like_out: (out) (optional): Output for the ID_LIKE value
 *
 * Helper function to get OS identification information from /etc/os-release.
 *
 * Returns: %TRUE if at least one identifier was found, %FALSE otherwise
 */
static gboolean
fu_uefi_get_os_paths(gchar **os_release_id_out, gchar **id_like_out)
{
	g_autofree gchar *os_release_id = NULL;
	g_autofree gchar *id_like = NULL;

	/* try to lookup /etc/os-release ID key */
	os_release_id = g_get_os_info(G_OS_INFO_KEY_ID);

	/* try ID_LIKE if available */
	id_like = g_get_os_info("ID_LIKE");

	if (os_release_id_out != NULL)
		*os_release_id_out = g_steal_pointer(&os_release_id);
	if (id_like_out != NULL)
		*id_like_out = g_steal_pointer(&id_like);

	return os_release_id != NULL || id_like != NULL;
}

/**
 * fu_uefi_find_esp_path_for_shim:
 * @esp_base: The base path of the EFI System Partition (ESP).
 * @filename: The shim filename to search for (e.g., "shimx64.efi").
 *
 * Searches for a shim file across multiple possible ESP directories.
 * This is needed because when systemd-boot is in use, the shim may be
 * in a distro-specific directory rather than the systemd directory.
 *
 * Returns: (transfer full): A newly allocated string containing the directory
 * structure within the ESP where the shim was found, or %NULL if not found.
 */
static gchar *
fu_uefi_find_esp_path_for_shim(const gchar *esp_base, const gchar *filename)
{
	g_autofree gchar *os_release_id = NULL;
	g_autofree gchar *id_like = NULL;
	const gchar *search_paths[] = {NULL,
				       NULL,
				       NULL,
				       NULL}; /* systemd, os_id, id_like paths, NULL terminator */
	guint search_idx = 0;

	/* first try systemd directory */
	search_paths[search_idx++] = "systemd";

	/* get OS identification info */
	fu_uefi_get_os_paths(&os_release_id, &id_like);
	if (os_release_id != NULL)
		search_paths[search_idx++] = os_release_id;

	if (id_like != NULL) {
		/* only check the first ID_LIKE entry for simplicity */
		g_auto(GStrv) split = g_strsplit(id_like, " ", -1);
		if (split[0] != NULL)
			search_paths[search_idx++] = split[0];
	}

	/* search in each directory for the shim file */
	for (guint i = 0; search_paths[i] != NULL; i++) {
		g_autofree gchar *esp_path = g_build_filename("EFI", search_paths[i], NULL);
		g_autofree gchar *full_dir_path = g_build_filename(esp_base, esp_path, NULL);
		g_autofree gchar *full_file_path = g_build_filename(full_dir_path, filename, NULL);

		if (g_file_test(full_file_path, G_FILE_TEST_IS_REGULAR)) {
			g_debug("found shim at %s", full_file_path);
			return g_steal_pointer(&esp_path);
		}
	}

	return NULL;
}

/* return without the ESP dir prepended */
gchar *
fu_uefi_get_esp_app_path(const gchar *esp_path, const gchar *cmd, GError **error)
{
	const gchar *suffix = fu_uefi_bootmgr_get_suffix(error);
	g_autofree gchar *base = NULL;
	g_autofree gchar *filename = NULL;
	if (suffix == NULL)
		return NULL;

	filename = g_strdup_printf("%s%s.efi", cmd, suffix);

	/* special case for shim: search across multiple directories when systemd-boot is present */
	if (g_strcmp0(cmd, "shim") == 0) {
		base = fu_uefi_find_esp_path_for_shim(esp_path, filename);
		if (base != NULL)
			return g_strdup_printf("%s/%s", base, filename);
	}

	base = fu_uefi_get_esp_path_for_os(esp_path);
	return g_strdup_printf("%s/%s", base, filename);
}

/**
 * fu_uefi_get_built_app_path:
 * @basename: the prefix for the binary
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
fu_uefi_get_built_app_path(FuEfivars *efivars, const gchar *binary, GError **error)
{
	const gchar *suffix;
	g_autofree gchar *prefix = NULL;
	g_autofree gchar *source_path = NULL;
	g_autofree gchar *source_path_signed = NULL;
	gboolean secureboot_enabled = FALSE;
	gboolean source_path_exists = FALSE;
	gboolean source_path_signed_exists = FALSE;

	suffix = fu_uefi_bootmgr_get_suffix(error);
	if (suffix == NULL)
		return NULL;
	prefix = fu_path_from_kind(FU_PATH_KIND_EFIAPPDIR);

	source_path = g_strdup_printf("%s/%s%s.efi", prefix, binary, suffix);
	source_path_signed = g_strdup_printf("%s.signed", source_path);

	source_path_exists = g_file_test(source_path, G_FILE_TEST_EXISTS);
	source_path_signed_exists = g_file_test(source_path_signed, G_FILE_TEST_EXISTS);

	if (!fu_efivars_get_secure_boot(efivars, &secureboot_enabled, error))
		return NULL;
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
fu_uefi_get_framebuffer_size(guint32 *width, guint32 *height, GError **error)
{
	guint32 height_tmp;
	guint32 width_tmp;
	g_autofree gchar *sysfsdriverdir = NULL;
	g_autofree gchar *fbdir = NULL;

	sysfsdriverdir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR_DRIVERS);
	fbdir = g_build_filename(sysfsdriverdir, "efi-framebuffer", "efi-framebuffer.0", NULL);
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

/**
 * fu_uefi_get_esp_path_for_os:
 * @esp_base: The base path of the EFI System Partition (ESP).
 *
 * Retrieves the directory structure of the EFI System Partition (ESP) for
 * the operating system.
 *
 * This function constructs and returns the path of the directory to use
 * within the ESP based on the provided base path.
 *
 * Returns: (transfer full): A newly allocated string containing the directory
 * structure within the ESP for the operating system. The caller is
 * responsible for freeing the returned string.
 */
gchar *
fu_uefi_get_esp_path_for_os(const gchar *esp_base)
{
#ifndef EFI_OS_DIR
	g_autofree gchar *os_release_id = NULL;
	g_autofree gchar *id_like = NULL;
	g_autofree gchar *esp_path = NULL;
	g_autofree gchar *full_path = NULL;
	g_autofree gchar *systemd_path = NULL;
	g_autofree gchar *full_systemd_path = NULL;

	/* distro (or user) is using systemd-boot */
	systemd_path = g_build_filename("EFI", "systemd", NULL);
	full_systemd_path = g_build_filename(esp_base, systemd_path, NULL);
	if (g_file_test(full_systemd_path, G_FILE_TEST_IS_DIR))
		return g_steal_pointer(&systemd_path);

	/* get OS identification info */
	fu_uefi_get_os_paths(&os_release_id, &id_like);
	if (os_release_id == NULL)
		os_release_id = g_strdup("unknown");

	/* if ID key points at something existing return it */
	esp_path = g_build_filename("EFI", os_release_id, NULL);
	full_path = g_build_filename(esp_base, esp_path, NULL);
	if (g_file_test(full_path, G_FILE_TEST_IS_DIR))
		return g_steal_pointer(&esp_path);

	/* if ID key doesn't exist, try ID_LIKE */
	if (id_like != NULL) {
		g_auto(GStrv) split = g_strsplit(id_like, " ", -1);
		for (guint i = 0; split[i] != NULL; i++) {
			g_autofree gchar *id_like_path = g_build_filename("EFI", split[i], NULL);
			g_autofree gchar *id_like_full_path =
			    g_build_filename(esp_base, id_like_path, NULL);
			if (!g_file_test(id_like_full_path, G_FILE_TEST_IS_DIR))
				continue;
			g_debug("using ID_LIKE key from os-release");
			return g_steal_pointer(&id_like_path);
		}
	}
	return g_steal_pointer(&esp_path);
#else
	return g_build_filename("EFI", EFI_OS_DIR, NULL);
#endif
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
fu_uefi_esp_target_verify(const gchar *source_fn, FuVolume *esp, const gchar *target_no_mountpoint)
{
	gsize len = 0;
	g_autofree gchar *source_csum = NULL;
	g_autofree gchar *source_data = NULL;
	g_autofree gchar *target_csum = NULL;
	g_autofree gchar *target_data = NULL;
	g_autofree gchar *esp_path = fu_volume_get_mount_point(esp);
	g_autofree gchar *target_fn = g_build_filename(esp_path, target_no_mountpoint, NULL);

	/* nothing in target yet */
	if (!g_file_test(target_fn, G_FILE_TEST_EXISTS))
		return FALSE;

	/* test if the file needs to be updated */
	if (!g_file_get_contents(source_fn, &source_data, &len, NULL))
		return FALSE;
	source_csum = g_compute_checksum_for_data(G_CHECKSUM_SHA256, (guchar *)source_data, len);
	if (!g_file_get_contents(target_fn, &target_data, &len, NULL))
		return FALSE;
	target_csum = g_compute_checksum_for_data(G_CHECKSUM_SHA256, (guchar *)target_data, len);
	return g_strcmp0(target_csum, source_csum) == 0;
}

gboolean
fu_uefi_esp_target_exists(FuVolume *esp, const gchar *target_no_mountpoint)
{
	g_autofree gchar *esp_path = fu_volume_get_mount_point(esp);
	g_autofree gchar *target_fn = g_build_filename(esp_path, target_no_mountpoint, NULL);
	return g_file_test(target_fn, G_FILE_TEST_EXISTS);
}

gboolean
fu_uefi_esp_target_copy(const gchar *source_fn,
			FuVolume *esp,
			const gchar *target_no_mountpoint,
			GError **error)
{
	g_autofree gchar *esp_path = fu_volume_get_mount_point(esp);
	g_autofree gchar *target_fn = g_build_filename(esp_path, target_no_mountpoint, NULL);
	g_autoptr(GFile) source_file = g_file_new_for_path(source_fn);
	g_autoptr(GFile) target_file = g_file_new_for_path(target_fn);

	if (!g_file_copy(source_file,
			 target_file,
			 G_FILE_COPY_OVERWRITE,
			 NULL,
			 NULL,
			 NULL,
			 error)) {
		g_prefix_error(error, "failed to copy %s to %s: ", source_fn, target_fn);
		return FALSE;
	}

	return TRUE;
}
