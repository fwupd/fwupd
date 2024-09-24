/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <stdio.h>

#include "fu-uefi-bootmgr.h"
#include "fu-uefi-common.h"
#include "fu-uefi-device.h"

static gboolean
fu_uefi_bootmgr_add_to_boot_order(FuEfivars *efivars, guint16 boot_entry, GError **error)
{
	g_autoptr(GArray) order = NULL;

	/* get the current boot order */
	order = fu_efivars_get_boot_order(efivars, error);
	if (order == NULL)
		return FALSE;

	/* already set */
	for (guint i = 0; i < order->len; i++) {
		guint16 val = g_array_index(order, guint16, i);
		if (val == boot_entry)
			return TRUE;
	}

	/* add the new boot index to the end of the list */
	g_array_append_val(order, boot_entry);
	if (!fu_efivars_set_boot_order(efivars, order, error)) {
		g_prefix_error(error, "could not set BootOrder(%u): ", boot_entry);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static guint16
fu_uefi_bootmgr_parse_name(const gchar *name)
{
	gint rc;
	gint scanned = 0;
	guint16 entry = 0;

	/* BootXXXX */
	rc = sscanf(name, "Boot%hX%n", &entry, &scanned);
	if (rc != 1 || scanned != 8)
		return G_MAXUINT16;
	return entry;
}

gboolean
fu_uefi_bootmgr_verify_fwupd(FuEfivars *efivars, GError **error)
{
	g_autoptr(GPtrArray) names = NULL;

	names = fu_efivars_get_names(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, error);
	if (names == NULL)
		return FALSE;
	for (guint i = 0; i < names->len; i++) {
		const gchar *desc;
		const gchar *name = g_ptr_array_index(names, i);
		guint16 entry;
		g_autoptr(FuEfiLoadOption) loadopt = NULL;
		g_autoptr(GError) error_local = NULL;

		/* not BootXXXX */
		entry = fu_uefi_bootmgr_parse_name(name);
		if (entry == G_MAXUINT16)
			continue;

		/* parse key */
		loadopt = fu_efivars_get_boot_entry(efivars, entry, &error_local);
		if (loadopt == NULL) {
			g_debug("%s -> load option was invalid: %s", name, error_local->message);
			continue;
		}
		desc = fu_firmware_get_id(FU_FIRMWARE(loadopt));
		if (g_strcmp0(desc, "Linux Firmware Updater") == 0 ||
		    g_strcmp0(desc, "Linux-Firmware-Updater") == 0) {
			g_debug("found %s at Boot%04X", desc, entry);
			return TRUE;
		}
	}

	/* did not find */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "no 'Linux Firmware Updater' entry found");
	return FALSE;
}

static gboolean
fu_uefi_bootmgr_setup_bootnext_with_loadopt(FuEfivars *efivars,
					    FuEfiLoadOption *loadopt,
					    FuUefiBootmgrFlags flags,
					    GError **error)
{
	const gchar *name = NULL;
	guint16 boot_next = G_MAXUINT16;
	g_autofree guint8 *set_entries = g_malloc0(G_MAXUINT16);
	g_autoptr(GBytes) loadopt_blob = NULL;
	g_autoptr(GBytes) loadopt_blob_old = NULL;
	g_autoptr(GPtrArray) names = NULL;

	/* write */
	loadopt_blob = fu_firmware_write(FU_FIRMWARE(loadopt), error);
	if (loadopt_blob == NULL)
		return FALSE;

	/* find existing BootXXXX entry for fwupd */
	names = fu_efivars_get_names(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, error);
	if (names == NULL)
		return FALSE;
	for (guint i = 0; i < names->len; i++) {
		const gchar *desc;
		guint16 entry = 0;
		g_autoptr(GBytes) loadopt_blob_tmp = NULL;
		g_autoptr(GError) error_local = NULL;
		g_autoptr(FuEfiLoadOption) loadopt_tmp = fu_efi_load_option_new();

		/* not BootXXXX */
		name = g_ptr_array_index(names, i);
		entry = fu_uefi_bootmgr_parse_name(name);
		if (entry == G_MAXUINT16)
			continue;

		/* mark this as used */
		set_entries[entry] = 1;

		loadopt_blob_tmp = fu_efivars_get_boot_data(efivars, entry, &error_local);
		if (loadopt_blob_tmp == NULL) {
			g_debug("failed to get data for name %s: %s", name, error_local->message);
			continue;
		}
		if (!fu_firmware_parse(FU_FIRMWARE(loadopt_tmp),
				       loadopt_blob_tmp,
				       FWUPD_INSTALL_FLAG_NONE,
				       &error_local)) {
			g_debug("%s -> load option was invalid: %s", name, error_local->message);
			continue;
		}
		desc = fu_firmware_get_id(FU_FIRMWARE(loadopt_tmp));
		if (g_strcmp0(desc, "Linux Firmware Updater") != 0 &&
		    g_strcmp0(desc, "Linux-Firmware-Updater") != 0) {
			g_debug("%s -> '%s' : does not match", name, desc);
			continue;
		}

		loadopt_blob_old = g_steal_pointer(&loadopt_blob_tmp);
		boot_next = entry;
		break;
	}

	/* already exists */
	if (loadopt_blob_old != NULL) {
		/* is different than before */
		if (!fu_bytes_compare(loadopt_blob, loadopt_blob_old, NULL)) {
			g_debug("%s: updating existing boot entry", name);
			if (!fu_efivars_set_boot_data(efivars, boot_next, loadopt_blob, error)) {
				g_prefix_error(error, "could not set boot variable active: ");
				return FALSE;
			}
		} else {
			g_debug("%s: re-using existing boot entry", name);
		}
		/* create a new one */
	} else {
		g_autofree gchar *boot_next_name = NULL;
		for (guint16 value = 0; value < G_MAXUINT16; value++) {
			if (set_entries[value])
				continue;
			boot_next = value;
			break;
		}
		if (boot_next == G_MAXUINT16) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no free boot variables (tried %x)",
				    boot_next);
			return FALSE;
		}
		boot_next_name = g_strdup_printf("Boot%04X", (guint)boot_next);
		g_debug("%s -> creating new entry", boot_next_name);
		if (!fu_efivars_set_data_bytes(efivars,
					       FU_EFIVARS_GUID_EFI_GLOBAL,
					       boot_next_name,
					       loadopt_blob,
					       FU_EFIVARS_ATTR_NON_VOLATILE |
						   FU_EFIVARS_ATTR_BOOTSERVICE_ACCESS |
						   FU_EFIVARS_ATTR_RUNTIME_ACCESS,
					       error)) {
			g_prefix_error(error, "could not set boot variable %s: ", boot_next_name);
			return FALSE;
		}
	}

	/* TODO: conditionalize this on the UEFI version? */
	if (flags & FU_UEFI_BOOTMGR_FLAG_MODIFY_BOOTORDER) {
		if (!fu_uefi_bootmgr_add_to_boot_order(efivars, boot_next, error))
			return FALSE;
	}

	/* set the boot next */
	if (!fu_efivars_set_boot_next(efivars, boot_next, error)) {
		g_prefix_error(error, "could not set BootNext(%u): ", boot_next);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_uefi_bootmgr_shim_is_safe(FuEfivars *efivars, const gchar *source_shim, GError **error)
{
	g_autoptr(GBytes) current_sbatlevel_bytes = NULL;
	g_autoptr(FuFirmware) shim = fu_pefile_firmware_new();
	g_autoptr(FuFirmware) sbatlevel_section = NULL;
	g_autoptr(FuFirmware) previous_sbatlevel = NULL;
	g_autoptr(FuFirmware) current_sbatlevel = fu_csv_firmware_new();
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GPtrArray) shim_entries = NULL;

	file = g_file_new_for_path(source_shim);
	if (!fu_firmware_parse_file(shim, file, FWUPD_INSTALL_FLAG_NONE, error)) {
		g_prefix_error(error, "failed to load %s: ", source_shim);
		return FALSE;
	}
	sbatlevel_section = fu_firmware_get_image_by_id(shim, ".sbatlevel", &error_local);
	if (sbatlevel_section == NULL) {
		g_debug("no sbatlevel section was found");
		/* if there is no .sbatlevel section, then it will not update, it should be safe */
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND))
			return TRUE;
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	/* not safe if variable is not set but new shim would set it */
	current_sbatlevel_bytes =
	    fu_efivars_get_data_bytes(efivars, FU_EFIVARS_GUID_SHIM, "SbatLevelRT", NULL, error);
	if (current_sbatlevel_bytes == NULL)
		return FALSE;
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(current_sbatlevel), "$id");
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(current_sbatlevel), "component_generation");
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(current_sbatlevel), "date_stamp");
	if (!fu_firmware_parse(current_sbatlevel,
			       current_sbatlevel_bytes,
			       FWUPD_INSTALL_FLAG_NONE,
			       error)) {
		g_prefix_error(error, "failed to load SbatLevelRT: ");
		return FALSE;
	}

	/*
	 * For every new shim entry, we need a matching entry in the
	 * current sbatlevel. That is the entry of the shim is not
	 * newer than current sbatlevel.
	 *
	 * The opposite way might work (for example shim's latest
	 * sbatlevel matches) or not (shim is too old), but it will
	 * not brick the current OS.
	 */
	previous_sbatlevel = fu_firmware_get_image_by_id(sbatlevel_section, "previous", error);
	if (previous_sbatlevel == NULL)
		return FALSE;
	shim_entries = fu_firmware_get_images(previous_sbatlevel);
	for (guint idx = 0; idx < shim_entries->len; idx++) {
		FuCsvEntry *current_entry = NULL;
		FuCsvEntry *shim_entry = g_ptr_array_index(shim_entries, idx);
		const gchar *entry_id = fu_firmware_get_id(FU_FIRMWARE(shim_entry));
		guint64 current_generation = 0;
		guint64 shim_generation = 0;

		current_entry =
		    FU_CSV_ENTRY(fu_firmware_get_image_by_id(FU_FIRMWARE(current_sbatlevel),
							     entry_id,
							     &error_local));
		if (current_entry == NULL) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
				g_set_error(
				    error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "shim sbatlevel for %s has a bricking update for entry %s "
				    "(missing entry in current UEFI variable)",
				    source_shim,
				    entry_id);
			} else {
				g_prefix_error(&error_local,
					       "while looking for entry in current sbatlevel: ");
				g_propagate_error(error, g_steal_pointer(&error_local));
			}
			return FALSE;
		}

		if (!fu_csv_entry_get_value_by_column_id_uint64(shim_entry,
								"component_generation",
								&shim_generation,
								error)) {
			g_prefix_error(error,
				       "sbatlevel entry %s for shim %s: ",
				       entry_id,
				       source_shim);
			return FALSE;
		}
		if (!fu_csv_entry_get_value_by_column_id_uint64(current_entry,
								"component_generation",
								&current_generation,
								error)) {
			g_prefix_error(error, "entry %s from current sbatlevel: ", entry_id);
			return FALSE;
		}
		if (current_generation < shim_generation) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "sbatlevel for shim %s has a bricking update for entry "
				    "%s (newer generation)",
				    source_shim,
				    entry_id);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

gboolean
fu_uefi_bootmgr_bootnext(FuEfivars *efivars,
			 FuVolume *esp,
			 const gchar *description,
			 FuUefiBootmgrFlags flags,
			 GError **error)
{
	const gchar *filepath = NULL;
	gboolean use_fwup_path = TRUE;
	gboolean secureboot_enabled = FALSE;
	g_autofree gchar *shim_app = NULL;
	g_autofree gchar *shim_cpy = NULL;
	g_autofree gchar *source_app = NULL;
	g_autofree gchar *source_shim = NULL;
	g_autofree gchar *target_app = NULL;
	g_autofree gchar *esp_path = fu_volume_get_mount_point(esp);
	g_autoptr(FuEfiDevicePathList) dp_buf = NULL;
	g_autoptr(FuEfiLoadOption) loadopt = fu_efi_load_option_new();

	/* skip for self tests */
	if (g_getenv("FWUPD_UEFI_TEST") != NULL)
		return TRUE;

	/* if secure boot was turned on this might need to be installed separately */
	source_app = fu_uefi_get_built_app_path(efivars, "fwupd", error);
	if (source_app == NULL)
		return FALSE;

	/* test if we should use shim */
	if (!fu_efivars_get_secure_boot(efivars, &secureboot_enabled, error))
		return FALSE;
	if (secureboot_enabled) {
		shim_app = fu_uefi_get_esp_app_path(esp_path, "shim", error);
		if (shim_app == NULL)
			return FALSE;

		/* copy in an updated shim if we have one */
		source_shim = fu_uefi_get_built_app_path(efivars, "shim", NULL);
		if (source_shim != NULL) {
			if (!fu_uefi_esp_target_verify(source_shim, esp, shim_app)) {
				if (!fu_uefi_bootmgr_shim_is_safe(efivars, source_shim, error))
					return FALSE;
				if (!fu_uefi_esp_target_copy(source_shim, esp, shim_app, error))
					return FALSE;
			}
		}

		if (fu_uefi_esp_target_exists(esp, shim_app)) {
			/* use a custom copy of shim for firmware updates */
			if (flags & FU_UEFI_BOOTMGR_FLAG_USE_SHIM_UNIQUE) {
				shim_cpy = fu_uefi_get_esp_app_path(esp_path, "shimfwupd", error);
				if (shim_cpy == NULL)
					return FALSE;
				if (!fu_uefi_esp_target_verify(shim_app, esp, shim_cpy)) {
					if (!fu_uefi_esp_target_copy(shim_app,
								     esp,
								     shim_cpy,
								     error))
						return FALSE;
				}
				filepath = shim_cpy;
			} else {
				filepath = shim_app;
			}
			use_fwup_path = FALSE;
		} else if ((flags & FU_UEFI_BOOTMGR_FLAG_USE_SHIM_FOR_SB) > 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_BROKEN_SYSTEM,
				    "Secure boot is enabled, but shim isn't installed to "
				    "%s",
				    shim_app);
			return FALSE;
		}
	}

	/* test if correct asset in place */
	target_app = fu_uefi_get_esp_app_path(esp_path, "fwupd", error);
	if (target_app == NULL)
		return FALSE;
	if (!fu_uefi_esp_target_verify(source_app, esp, target_app)) {
		if (!fu_uefi_esp_target_copy(source_app, esp, target_app, error))
			return FALSE;
	}

	/* no shim, so use this directly */
	if (use_fwup_path)
		filepath = target_app;

	/* add the fwupdx64.efi ESP path as the shim loadopt data */
	if (!use_fwup_path) {
		g_autofree gchar *fwup_fs_basename = g_path_get_basename(target_app);
		if (!fu_efi_load_option_set_optional_path(loadopt, fwup_fs_basename, error))
			return FALSE;
	}

	/* add DEVICE_PATH */
	dp_buf = fu_uefi_device_build_dp_buf(esp, filepath, error);
	if (dp_buf == NULL)
		return FALSE;
	fu_firmware_add_image(FU_FIRMWARE(loadopt), FU_FIRMWARE(dp_buf));
	fu_firmware_set_id(FU_FIRMWARE(loadopt), description);

	/* save as BootNext */
	return fu_uefi_bootmgr_setup_bootnext_with_loadopt(efivars, loadopt, flags, error);
}
