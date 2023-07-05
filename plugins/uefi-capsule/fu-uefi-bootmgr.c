/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <stdio.h>

#include "fu-uefi-bootmgr.h"
#include "fu-uefi-common.h"
#include "fu-uefi-device.h"

static gboolean
fu_uefi_bootmgr_add_to_boot_order(guint16 boot_entry, GError **error)
{
	gsize boot_order_size = 0;
	guint i = 0;
	guint32 attr = 0;
	g_autofree guint16 *boot_order = NULL;
	g_autofree guint16 *new_boot_order = NULL;

	/* get the current boot order */
	if (!fu_efivar_get_data(FU_EFIVAR_GUID_EFI_GLOBAL,
				"BootOrder",
				(guint8 **)&boot_order,
				&boot_order_size,
				&attr,
				error))
		return FALSE;

	/* already set next */
	for (i = 0; i < boot_order_size / sizeof(guint16); i++) {
		guint16 val = boot_order[i];
		if (val == boot_entry)
			return TRUE;
	}

	/* add the new boot index to the end of the list */
	new_boot_order = g_malloc0(boot_order_size + sizeof(guint16));
	if (boot_order_size != 0)
		memcpy(new_boot_order, boot_order, boot_order_size);

	attr |= FU_EFIVAR_ATTR_NON_VOLATILE | FU_EFIVAR_ATTR_BOOTSERVICE_ACCESS |
		FU_EFIVAR_ATTR_RUNTIME_ACCESS;

	i = boot_order_size / sizeof(guint16);
	new_boot_order[i] = boot_entry;
	boot_order_size += sizeof(guint16);
	return fu_efivar_set_data(FU_EFIVAR_GUID_EFI_GLOBAL,
				  "BootOrder",
				  (guint8 *)new_boot_order,
				  boot_order_size,
				  attr,
				  error);
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
fu_uefi_bootmgr_verify_fwupd(GError **error)
{
	g_autoptr(GPtrArray) names = NULL;

	names = fu_efivar_get_names(FU_EFIVAR_GUID_EFI_GLOBAL, error);
	if (names == NULL)
		return FALSE;
	for (guint i = 0; i < names->len; i++) {
		const gchar *desc;
		const gchar *name = g_ptr_array_index(names, i);
		guint16 entry;
		g_autoptr(FuEfiLoadOption) loadopt = fu_efi_load_option_new();
		g_autoptr(GBytes) loadopt_blob = NULL;
		g_autoptr(GError) error_local = NULL;

		/* not BootXXXX */
		entry = fu_uefi_bootmgr_parse_name(name);
		if (entry == G_MAXUINT16)
			continue;

		/* parse key */
		loadopt_blob =
		    fu_efivar_get_data_bytes(FU_EFIVAR_GUID_EFI_GLOBAL, name, NULL, &error_local);
		if (loadopt_blob == NULL) {
			g_debug("failed to get data for name %s: %s", name, error_local->message);
			continue;
		}
		if (!fu_firmware_parse(FU_FIRMWARE(loadopt),
				       loadopt_blob,
				       FWUPD_INSTALL_FLAG_NONE,
				       &error_local)) {
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
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "no 'Linux Firmware Updater' entry found");
	return FALSE;
}

static gboolean
fu_uefi_setup_bootnext_with_loadopt(FuEfiLoadOption *loadopt, GError **error)
{
	const gchar *name = NULL;
	guint32 attr;
	guint16 boot_next = G_MAXUINT16;
	guint8 boot_nextbuf[2] = {0};
	g_autofree guint8 *set_entries = g_malloc0(G_MAXUINT16);
	g_autoptr(GBytes) loadopt_blob = NULL;
	g_autoptr(GBytes) loadopt_blob_old = NULL;
	g_autoptr(GPtrArray) names = NULL;

	/* write */
	loadopt_blob = fu_firmware_write(FU_FIRMWARE(loadopt), error);
	if (loadopt_blob == NULL)
		return FALSE;

	/* find existing BootXXXX entry for fwupd */
	names = fu_efivar_get_names(FU_EFIVAR_GUID_EFI_GLOBAL, error);
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

		loadopt_blob_tmp =
		    fu_efivar_get_data_bytes(FU_EFIVAR_GUID_EFI_GLOBAL, name, &attr, &error_local);
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
			if (!fu_efivar_set_data_bytes(FU_EFIVAR_GUID_EFI_GLOBAL,
						      name,
						      loadopt_blob,
						      attr,
						      error)) {
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
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "no free boot variables (tried %x)",
				    boot_next);
			return FALSE;
		}
		boot_next_name = g_strdup_printf("Boot%04X", (guint)boot_next);
		g_debug("%s -> creating new entry", boot_next_name);
		if (!fu_efivar_set_data_bytes(FU_EFIVAR_GUID_EFI_GLOBAL,
					      boot_next_name,
					      loadopt_blob,
					      FU_EFIVAR_ATTR_NON_VOLATILE |
						  FU_EFIVAR_ATTR_BOOTSERVICE_ACCESS |
						  FU_EFIVAR_ATTR_RUNTIME_ACCESS,
					      error)) {
			g_prefix_error(error, "could not set boot variable %s: ", boot_next_name);
			return FALSE;
		}
	}

	/* TODO: conditionalize this on the UEFI version? */
	if (!fu_uefi_bootmgr_add_to_boot_order(boot_next, error))
		return FALSE;

	/* set the boot next */
	fu_memwrite_uint16(boot_nextbuf, boot_next, G_LITTLE_ENDIAN);
	if (!fu_efivar_set_data(FU_EFIVAR_GUID_EFI_GLOBAL,
				"BootNext",
				boot_nextbuf,
				sizeof(boot_nextbuf),
				FU_EFIVAR_ATTR_NON_VOLATILE | FU_EFIVAR_ATTR_BOOTSERVICE_ACCESS |
				    FU_EFIVAR_ATTR_RUNTIME_ACCESS,
				error)) {
		g_prefix_error(error, "could not set BootNext(%u): ", boot_next);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_uefi_bootmgr_shim_is_safe(const gchar *source_shim, GError **error)
{
	gsize idx = 0;
	g_autoptr(GBytes) current_sbatlevel_bytes = NULL;
	g_autoptr(FuFirmware) shim = NULL;
	g_autoptr(FuFirmware) sbatlevel_section = NULL;
	g_autoptr(FuFirmware) previous_sbatlevel = NULL;
	g_autoptr(FuFirmware) current_sbatlevel = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GPtrArray) shim_entries = NULL;

	shim = fu_pefile_firmware_new();
	blob = fu_bytes_get_contents(source_shim, error);
	if (blob == NULL)
		return FALSE;
	if (!fu_firmware_parse(shim, blob, FWUPD_INSTALL_FLAG_NONE, error))
		return FALSE;

	sbatlevel_section = fu_firmware_get_image_by_id(shim, ".sbatlevel", &error_local);
	if (sbatlevel_section == NULL) {
		g_debug("no sbatlevel section was found");
		/* if there is no .sbatlevel section, then it will not update, it should be safe */
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND))
			return TRUE;
		g_propagate_error(error, g_steal_pointer(&error_local));
	}

	previous_sbatlevel = fu_firmware_get_image_by_id(sbatlevel_section, "previous", error);
	if (previous_sbatlevel == NULL)
		return FALSE;

	shim_entries = fu_firmware_get_images(previous_sbatlevel);

	current_sbatlevel_bytes =
	    fu_efivar_get_data_bytes(FU_EFIVAR_GUID_SHIM, "SbatLevelRT", NULL, error);
	/* not safe if variable is not set but new shim would set it */
	if (current_sbatlevel_bytes == NULL)
		return FALSE;

	current_sbatlevel = fu_csv_firmware_new();
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(current_sbatlevel), "$id");
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(current_sbatlevel), "component_generation");
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(current_sbatlevel), "date_stamp");

	if (!fu_firmware_parse(current_sbatlevel,
			       current_sbatlevel_bytes,
			       FWUPD_INSTALL_FLAG_NONE,
			       error))
		return FALSE;

	/*
	 * For every new shim entry, we need a matching entry in the
	 * current sbatlevel. That is the entry of the shim is not
	 * newer than current sbatlevel.
	 *
	 * The opposite way might work (for example shim's latest
	 * sbatlevel matches) or not (shim is too old), but it will
	 * not brick the current OS.
	 */
	for (idx = 0; idx < shim_entries->len; idx++) {
		const gchar *entry_id = NULL;
		guint64 shim_generation = 0;
		guint64 current_generation = 0;
		FuCsvEntry *current_entry = NULL;
		FuCsvEntry *shim_entry = g_ptr_array_index(shim_entries, idx);

		entry_id = fu_firmware_get_id(FU_FIRMWARE(shim_entry));

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

	return TRUE;
}

gboolean
fu_uefi_bootmgr_bootnext(FuVolume *esp,
			 const gchar *description,
			 FuUefiBootmgrFlags flags,
			 GError **error)
{
	const gchar *filepath = NULL;
	gboolean use_fwup_path = TRUE;
	gboolean secure_boot = FALSE;
	g_autofree gchar *shim_app = NULL;
	g_autofree gchar *shim_cpy = NULL;
	g_autofree gchar *source_app = NULL;
	g_autofree gchar *source_shim = NULL;
	g_autofree gchar *target_app = NULL;
	g_autoptr(FuEfiDevicePathList) dp_buf = NULL;
	g_autoptr(FuEfiLoadOption) loadopt = fu_efi_load_option_new();

	/* skip for self tests */
	if (g_getenv("FWUPD_UEFI_TEST") != NULL)
		return TRUE;

	/* if secure boot was turned on this might need to be installed separately */
	source_app = fu_uefi_get_built_app_path("fwupd", error);
	if (source_app == NULL)
		return FALSE;

	/* test if we should use shim */
	secure_boot = fu_efivar_secure_boot_enabled(NULL);
	if (secure_boot) {
		shim_app = fu_uefi_get_esp_app_path("shim", error);
		if (shim_app == NULL)
			return FALSE;

		/* copy in an updated shim if we have one */
		source_shim = fu_uefi_get_built_app_path("shim", NULL);
		if (source_shim != NULL) {
			if (!fu_uefi_esp_target_verify(source_shim, esp, shim_app)) {
				if (!fu_uefi_bootmgr_shim_is_safe(source_shim, error))
					return FALSE;
				if (!fu_uefi_esp_target_copy(source_shim, esp, shim_app, error))
					return FALSE;
			}
		}

		if (fu_uefi_esp_target_exists(esp, shim_app)) {
			/* use a custom copy of shim for firmware updates */
			if (flags & FU_UEFI_BOOTMGR_FLAG_USE_SHIM_UNIQUE) {
				shim_cpy = fu_uefi_get_esp_app_path("shimfwupd", error);
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
	target_app = fu_uefi_get_esp_app_path("fwupd", error);
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
	return fu_uefi_setup_bootnext_with_loadopt(loadopt, error);
}
