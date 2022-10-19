/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <efivar/efiboot.h>
#include <efivar/efivar-dp.h>

#include "fu-efi-image.h"
#include "fu-uefi-dbx-common.h"

gchar *
fu_uefi_dbx_get_authenticode_hash(const gchar *fn, GError **error)
{
	g_autoptr(FuEfiImage) img = NULL;
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GMappedFile) mmap = NULL;

	g_debug("getting Authenticode hash of %s", fn);
	mmap = g_mapped_file_new(fn, FALSE, error);
	if (mmap == NULL)
		return NULL;
	bytes = g_mapped_file_get_bytes(mmap);

	img = fu_efi_image_new(bytes, error);
	if (img == NULL)
		return NULL;
	g_debug("SHA256 was %s", fu_efi_image_get_checksum(img));
	return g_strdup(fu_efi_image_get_checksum(img));
}

static gboolean
fu_uefi_dbx_entry_name_is_boot(const gchar *name)
{
	gint rc;
	gint scanned = 0;
	guint16 entry = 0;

	/* BootXXXX */
	rc = sscanf(name, "Boot%hX%n", &entry, &scanned);
	if (rc != 1 || scanned != 8)
		return FALSE;
	return TRUE;
}

static GPtrArray *
fu_uefi_dbx_bootable_efi_path(const gchar *guid, GError **error)
{
	g_autoptr(GPtrArray) names = NULL;
	g_autoptr(GPtrArray) efi_paths = g_ptr_array_new_with_free_func(g_free);

	names = fu_efivar_get_names(guid, error);
	if (names == NULL)
		return NULL;
	for (guint i = 0; i < names->len; i++) {
		const gchar *name = g_ptr_array_index(names, i);
		efi_load_option *loadopt;
		gsize var_data_size = 0;
		efidp dp = NULL;
		guint16 fp_list_len;
		gssize dp_len;
		g_autofree const gchar *text_path = NULL;
		gsize text_path_len = 0;
		g_autofree guint8 *var_data_tmp = NULL;
		g_autoptr(GError) error_local = NULL;
		g_auto(GStrv) tp_toks = NULL;
		guint tp_toks_len = 0;

		/* only BootXXXX */
		if (!fu_uefi_dbx_entry_name_is_boot(name))
			continue;

		/* parse efivars data file */
		if (!fu_efivar_get_data(guid,
					name,
					&var_data_tmp,
					&var_data_size,
					NULL,
					&error_local)) {
			g_debug("(%s) failed to get data: %s", name, error_local->message);
			continue;
		}

		/* retrieve load option from data */
		loadopt = (efi_load_option *)var_data_tmp;
		if (!efi_loadopt_is_valid(loadopt, var_data_size)) {
			g_debug("(%s) load option was invalid", name);
			continue;
		}

		/* construct device path object */
		dp = efi_loadopt_path(loadopt, var_data_size);

		/* only Media Device Path type which holds the bootable image */
		if (dp->type != 0x4)
			continue;

		/* know the length of device path list */
		fp_list_len = efi_loadopt_pathlen(loadopt, var_data_size);

		/* know the length of device path */
		dp_len = efidp_format_device_path(NULL, 0, dp, fp_list_len);
		if (dp_len < 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "(%s) could not parse device path",
				    name);
			return NULL;
		}

		/* allocate memory for device path in text */
		text_path_len = dp_len + 1;
		text_path = (gchar *)g_malloc(text_path_len);
		if (!text_path) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "(%s) couldn't allocate memory for text path",
				    name);
			return NULL;
		}

		/* convert device path to text */
		dp_len =
		    efidp_format_device_path((char *)text_path, text_path_len, dp, fp_list_len);
		if (dp_len < 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "(%s) incorrect device path",
				    name);
			return NULL;
		}

		/* Tokenize the dp text to extract the relative path of the
		 * efi image, typically after the last appearance of '/'.
		 *
		 * Two examples for the text path after '/'
		 * 1) File(\\EFI\\ubuntu\\shimx64.efi)
		 * 2) \\EFI\\ubuntu\\shimx64.efi
		 *
		 * The second example is introduced in:
		 * https://github.com/rhboot/efivar/commit/9a5e710
		 */
		tp_toks = g_strsplit(text_path, "/", -1);
		tp_toks_len = g_strv_length(tp_toks);
		if (tp_toks_len > 0) {
			g_autoptr(GString) efip = g_string_new(tp_toks[tp_toks_len - 1]);
			fu_string_replace(efip, "File(", "");
			fu_string_replace(efip, ")", "");
			fu_string_replace(efip, "\\", "/");

			g_ptr_array_add(efi_paths, g_strdup(efip->str));
			g_debug("(%s) added efi candidate: %s", name, efip->str);
		}
	}
	return g_steal_pointer(&efi_paths);
}

static gboolean
fu_uefi_dbx_signature_list_validate_volume(FuEfiSignatureList *siglist,
					   FuVolume *esp,
					   GError **error)
{
	g_autofree gchar *esp_path = NULL;
	g_autoptr(GPtrArray) files = NULL;

	/* get list of files contained in the ESP */
	esp_path = fu_volume_get_mount_point(esp);
	if (esp_path == NULL)
		return TRUE;
	files = fu_uefi_dbx_bootable_efi_path(FU_EFIVAR_GUID_EFI_GLOBAL, error);
	if (files == NULL)
		return FALSE;

	/* verify each file does not exist in the ESP */
	for (guint i = 0; i < files->len; i++) {
		const gchar *fn = NULL;
		const gchar *ep_candidate = g_ptr_array_index(files, i);
		g_autofree gchar *checksum = NULL;
		g_autoptr(FuFirmware) img = NULL;
		g_autoptr(GError) error_local = NULL;

		fn = g_build_filename(esp_path, ep_candidate, NULL);
		if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
			g_debug("file doesn't exist: %s", fn);
			continue;
		}

		/* get checksum of file */
		checksum = fu_uefi_dbx_get_authenticode_hash(fn, &error_local);
		if (checksum == NULL) {
			g_debug("failed to get checksum for %s: %s", fn, error_local->message);
			continue;
		}

		/* Authenticode signature is present in dbx! */
		g_debug("fn=%s, checksum=%s", fn, checksum);
		img = fu_firmware_get_image_by_checksum(FU_FIRMWARE(siglist), checksum, NULL);
		if (img != NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NEEDS_USER_ACTION,
				    "%s Authenticode checksum [%s] is present in dbx",
				    fn,
				    checksum);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

gboolean
fu_uefi_dbx_signature_list_validate(FuContext *ctx, FuEfiSignatureList *siglist, GError **error)
{
	g_autoptr(GPtrArray) volumes = NULL;
	volumes = fu_context_get_esp_volumes(ctx, error);
	if (volumes == NULL)
		return FALSE;
	for (guint i = 0; i < volumes->len; i++) {
		FuVolume *esp = g_ptr_array_index(volumes, i);
		g_autoptr(FuDeviceLocker) locker = NULL;
		locker = fu_volume_locker(esp, error);
		if (locker == NULL)
			return FALSE;
		if (!fu_uefi_dbx_signature_list_validate_volume(siglist, esp, error))
			return FALSE;
	}
	return TRUE;
}
