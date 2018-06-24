/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <efivar/efiboot.h>
#include <efivar/efivar.h>
#include <gio/gio.h>
#include <stdio.h>

#include "fu-ucs2.h"
#include "fu-uefi-bootmgr.h"
#include "fu-uefi-common.h"

/* XXX PJFIX: this should be in efiboot-loadopt.h in efivar */
#define LOAD_OPTION_ACTIVE      0x00000001

static gboolean
fu_uefi_bootmgr_add_to_boot_order (guint16 boot_entry, GError **error)
{
	gsize boot_order_size = 0;
	gint rc;
	guint i = 0;
	guint32 attr = EFI_VARIABLE_NON_VOLATILE |
			EFI_VARIABLE_BOOTSERVICE_ACCESS |
			EFI_VARIABLE_RUNTIME_ACCESS;
	g_autofree guint16 *boot_order = NULL;
	g_autofree guint16 *new_boot_order = NULL;

	/* get size of the BootOrder */
	rc = efi_get_variable_size (efi_guid_global, "BootOrder", &boot_order_size);
	if (rc == ENOENT) {
		boot_order_size = 0;
		efi_error_clear ();
	} else if (rc < 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "efi_get_variable_size() failed");
		return rc;
	}

	/* get the current boot order */
	if (boot_order_size != 0) {
		rc = efi_get_variable (efi_guid_global, "BootOrder",
				       (guint8 **)&boot_order, &boot_order_size,
				       &attr);
		if (rc < 0) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_FAILED,
					     "efi_get_variable(BootOrder) failed");
			return FALSE;
		}

		/* already set next */
		for (i = 0; i < boot_order_size / sizeof (guint16); i++) {
			guint16 val = boot_order[i];
			if (val == boot_entry)
				return TRUE;
		}
	}

	/* add the new boot index to the end of the list */
	new_boot_order = g_malloc0 (boot_order_size + sizeof (guint16));
	if (boot_order_size != 0)
		memcpy (new_boot_order, boot_order, boot_order_size);

	i = boot_order_size / sizeof (guint16);
	new_boot_order[i] = boot_entry;
	boot_order_size += sizeof (guint16);
	rc = efi_set_variable(efi_guid_global, "BootOrder",
			      (guint8 *)new_boot_order, boot_order_size,
			      attr, 0644);
	if (rc < 0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "efi_set_variable(BootOrder) failed");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_uefi_setup_bootnext_with_dp (const guint8 *dp_buf, guint8 *opt, gssize opt_size, GError **error)
{
	efi_guid_t *guid = NULL;
	efi_load_option *loadopt = NULL;
	gchar *name = NULL;
	gint rc;
	gint set_entries[0x10000 / sizeof(gint)] = {0,};
	gsize var_data_size = 0;
	guint16 real_boot16;
	guint32 attr;
	guint32 boot_next = 0x10000;
	g_autofree guint8 *var_data = NULL;

	while ((rc = efi_get_next_variable_name (&guid, &name)) > 0) {
		gint div, mod;
		gint scanned = 0;
		gssize sz;
		guint16 entry = 0;
		efidp found_dp;
		g_autofree guint8 *var_data_tmp = NULL;

		if (efi_guid_cmp (guid, &efi_guid_global))
			continue;
		rc = sscanf (name, "Boot%hX%n", &entry, &scanned);
		if (rc < 0) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "failed to parse Boot entry %s", name);
			return FALSE;
		}
		if (rc != 1)
			continue;
		if (scanned != 8)
			continue;

		div = entry / (sizeof(set_entries[0]) * 8);
		mod = entry % (sizeof(set_entries[0]) * 8);

		set_entries[div] |= 1 << mod;

		rc = efi_get_variable (*guid, name, &var_data_tmp, &var_data_size, &attr);
		if (rc < 0) {
			g_debug ("efi_get_variable(%s) failed", name);
			continue;
		}

		loadopt = (efi_load_option *)var_data_tmp;
		if (!efi_loadopt_is_valid(loadopt, var_data_size)) {
			g_debug ("load option was invalid");
			continue;
		}

		sz = efi_loadopt_pathlen(loadopt, var_data_size);
		if (sz != efidp_size((efidp)dp_buf)) {
			g_debug ("pathlen device path doesn't match");
			continue;
		}

		found_dp = efi_loadopt_path (loadopt, var_data_size);
		if (memcmp (found_dp, dp_buf, sz)) {
			g_debug ("found_dp/dp_buf device path doesn't match");
		}

		if ((gssize)var_data_size != opt_size) {
			g_debug ("variable data doesn't match");
			continue;
		}

		if (memcmp (loadopt, opt, opt_size)) {
			g_debug ("load option doesn't match");
			continue;
		}

		var_data = g_steal_pointer (&var_data_tmp);
		boot_next = entry;
		efi_error_clear ();
		break;
	}
	if (rc < 0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "failed to find boot variable");
		return FALSE;
	}

	/* already exists */
	if (var_data != NULL) {
		efi_loadopt_attr_set (loadopt, LOAD_OPTION_ACTIVE);
		rc = efi_set_variable (*guid, name, var_data,
				       var_data_size, attr, 0644);
		if (rc < 0) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_FAILED,
					     "could not set boot variable active");
			return FALSE;
		}

	/* create a new one */
	} else {
		g_autofree gchar *boot_next_name = NULL;
		for (guint32 value = 0; value < 0x10000; value++) {
			gint div = value / (sizeof(set_entries[0]) * 8);
			gint mod = value % (sizeof(set_entries[0]) * 8);
			if (set_entries[div] & (1 << mod))
				continue;
			boot_next = value;
			break;
		}
		if (boot_next >= 0x10000) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "no free boot variables (tried %x)",
				     boot_next);
			return FALSE;
		}
		boot_next_name = g_strdup_printf ("Boot%04" G_GUINT32_FORMAT "X",
						  boot_next & 0xffff);
		rc = efi_set_variable (efi_guid_global, boot_next_name, opt, opt_size,
				       EFI_VARIABLE_NON_VOLATILE |
				       EFI_VARIABLE_BOOTSERVICE_ACCESS |
				       EFI_VARIABLE_RUNTIME_ACCESS,
				       0644);
		if (rc < 0) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "could not set boot variable %s: %d",
				     boot_next_name, rc);
			return FALSE;
		}
	}

	/* TODO: conditionalize this on the UEFI version? */
	if(!fu_uefi_bootmgr_add_to_boot_order (boot_next, error))
		return FALSE;

	/* set the boot next */
	real_boot16 = boot_next;
	rc = efi_set_variable (efi_guid_global, "BootNext", (guint8 *)&real_boot16, 2,
			       EFI_VARIABLE_NON_VOLATILE |
			       EFI_VARIABLE_BOOTSERVICE_ACCESS |
			       EFI_VARIABLE_RUNTIME_ACCESS,
			       0644);
	if (rc < 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "could not set BootNext(%" G_GUINT16_FORMAT ")",
			     real_boot16);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_uefi_cmp_asset (const gchar *source, const gchar *target)
{
	gsize len = 0;
	g_autofree gchar *source_checksum = NULL;
	g_autofree gchar *source_data = NULL;
	g_autofree gchar *target_checksum = NULL;
	g_autofree gchar *target_data = NULL;

	/* nothing in target yet */
	if (!g_file_test (target, G_FILE_TEST_EXISTS))
		return FALSE;

	/* test if the file needs to be updated */
	if (!g_file_get_contents (source, &source_data, &len, NULL))
		return FALSE;
	source_checksum = g_compute_checksum_for_data (G_CHECKSUM_SHA256,
						       (guchar *) source_data, len);
	if (!g_file_get_contents (target, &target_data, &len, NULL))
		return FALSE;
	target_checksum = g_compute_checksum_for_data (G_CHECKSUM_SHA256,
						       (guchar *) target_data, len);
	return g_strcmp0 (target_checksum, source_checksum) == 0;
}

static gboolean
fu_uefi_copy_asset (const gchar *source, const gchar *target, GError **error)
{
	g_autoptr(GFile) source_file = g_file_new_for_path (source);
	g_autoptr(GFile) target_file = g_file_new_for_path (target);

	if (!g_file_copy (source_file,
			  target_file,
			  G_FILE_COPY_OVERWRITE,
			  NULL,
			  NULL,
			  NULL,
			  error)) {
		g_prefix_error (error, "Failed to copy %s to %s: ",
				source, target);
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_uefi_setup_bootnext (const gchar *esp_mountpoint, GError **error)
{
	gboolean use_fwup_path = FALSE;
	gsize loader_sz = 0;
	gssize opt_size = 0;
	gssize sz, dp_size = 0;
	guint32 attributes = LOAD_OPTION_ACTIVE;
	g_autofree guint16 *loader_str = NULL;
	g_autofree gchar *fwup_esp_path = NULL;
	g_autofree gchar *fwup_fs_basename = NULL;
	g_autofree gchar *label = NULL;
	g_autofree gchar *shim_app = NULL;
	g_autofree guint8 *dp_buf = NULL;
	g_autofree guint8 *opt = NULL;
	g_autofree gchar *source_app = NULL;
	g_autofree gchar *target_app = NULL;

	/* if secure boot was turned on this might need to be installed separately */
	source_app = fu_uefi_bootmgr_get_source_path (error);
	if (source_app == NULL)
		return FALSE;

	/* test to make sure shim is there if we need it */
	shim_app = fu_uefi_bootmgr_get_esp_app_path (esp_mountpoint, "shim");
	if (!g_file_test (shim_app, G_FILE_TEST_EXISTS)) {
		if (fu_uefi_secure_boot_enabled ()) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_FAILED,
					     "Secure boot is enabled, but shim isn't installed to the EFI system partition");
			return FALSE;
		}
		use_fwup_path = TRUE;
	}

	/* test if correct asset in place */
	target_app = fu_uefi_bootmgr_get_esp_app_path (esp_mountpoint, "fwup");
	if (!fu_uefi_cmp_asset (source_app, target_app)) {
		if (!fu_uefi_copy_asset (source_app, target_app, error))
			return FALSE;
	}

	sz = efi_generate_file_device_path (dp_buf, dp_size, use_fwup_path
							    ? target_app
							    : shim_app,
					    EFIBOOT_OPTIONS_IGNORE_FS_ERROR|
					    EFIBOOT_ABBREV_HD);
	if (sz < 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "efi_generate_file_device_path(%s) failed",
			     use_fwup_path ? target_app : shim_app);
		return FALSE;
	}

	dp_size = sz;
	dp_buf = g_malloc0 (dp_size);
	fwup_fs_basename = g_path_get_basename (target_app);
	fwup_esp_path = g_strdup_printf ("\\%s", fwup_fs_basename);
	if (!use_fwup_path) {
		loader_str = fu_uft8_to_ucs2 (fwup_esp_path, -1);
		loader_sz = fu_ucs2_strlen (loader_str, -1) * 2;
		if (loader_sz)
			loader_sz += 2;
	}

	sz = efi_generate_file_device_path (dp_buf, dp_size, use_fwup_path
							    ? target_app
							    : shim_app,
					    EFIBOOT_OPTIONS_IGNORE_FS_ERROR|
					    EFIBOOT_ABBREV_HD);
	if (sz != dp_size) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "efi_generate_file_device_path(%s) failed",
			     use_fwup_path ? target_app : shim_app);
		return FALSE;
	}

	label = g_strdup_printf ("Linux-Firmware-Updater %s", fwup_esp_path);
	sz = efi_loadopt_create (opt, opt_size, attributes,
				 (efidp)dp_buf, dp_size,
				 (guint8 *)label,
				 (guint8 *)loader_str, loader_sz);
	if (sz < 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "efi_loadopt_create(%s) failed",
			     label);
		return FALSE;
	}
	opt = g_malloc0 (sz);
	opt_size = sz;
	sz = efi_loadopt_create (opt, opt_size, attributes,
				 (efidp)dp_buf, dp_size,
				 (guint8 *)label,
				 (guint8 *)loader_str, loader_sz);
	if (sz != opt_size) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "loadopt size was unreasonable.");
		return FALSE;
	}
	if (!fu_uefi_setup_bootnext_with_dp (dp_buf, opt, opt_size, error))
		return FALSE;
	efi_error_clear();

	return TRUE;
}
