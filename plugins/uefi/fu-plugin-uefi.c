/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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

#include "fu-quirks.h"
#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

static fwup_resource *
fu_plugin_uefi_find (fwup_resource_iter *iter, const gchar *guid_str, GError **error)
{
	efi_guid_t *guid_raw;
	fwup_resource *re_matched = NULL;
	fwup_resource *re = NULL;
	g_autofree gchar *guid_str_tmp = NULL;

	/* get the hardware we're referencing */
	guid_str_tmp = g_strdup ("00000000-0000-0000-0000-000000000000");
	while (fwup_resource_iter_next (iter, &re) > 0) {

		/* convert to strings */
		fwup_get_guid (re, &guid_raw);
		if (efi_guid_to_str (guid_raw, &guid_str_tmp) < 0) {
			g_warning ("failed to convert guid to string");
			continue;
		}

		/* FIXME: also match hardware_instance too */
		if (g_strcmp0 (guid_str, guid_str_tmp) == 0) {
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

static const gchar *
fu_plugin_uefi_last_attempt_status_to_str (guint32 status)
{
	if (status == FWUP_LAST_ATTEMPT_STATUS_SUCCESS)
		return "Success";
	if (status == FWUP_LAST_ATTEMPT_STATUS_ERROR_UNSUCCESSFUL)
		return "Unsuccessful";
	if (status == FWUP_LAST_ATTEMPT_STATUS_ERROR_INSUFFICIENT_RESOURCES)
		return "Insufficient resources";
	if (status == FWUP_LAST_ATTEMPT_STATUS_ERROR_INCORRECT_VERSION)
		return "Incorrect version";
	if (status == FWUP_LAST_ATTEMPT_STATUS_ERROR_INVALID_FORMAT)
		return "Invalid firmware format";
	if (status == FWUP_LAST_ATTEMPT_STATUS_ERROR_AUTH_ERROR)
		return "Authentication signing error";
	if (status == FWUP_LAST_ATTEMPT_STATUS_ERROR_PWR_EVT_AC)
		return "AC power required";
	if (status == FWUP_LAST_ATTEMPT_STATUS_ERROR_PWR_EVT_BATT)
		return "Battery level is too low";
	return NULL;
}

gboolean
fu_plugin_get_results (FuPlugin *plugin, FuDevice *device, GError **error)
{
	const gchar *tmp;
	fwup_resource *re = NULL;
	guint32 status = 0;
	guint32 version = 0;
	time_t when = 0;
	g_autofree gchar *version_str = NULL;
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
	version_str = g_strdup_printf ("%u", version);
	fu_device_set_update_version (device, version_str);
	if (status == FWUP_LAST_ATTEMPT_STATUS_SUCCESS) {
		fu_device_set_update_state (device, FWUPD_UPDATE_STATE_SUCCESS);
	} else {
		fu_device_set_update_state (device, FWUPD_UPDATE_STATE_FAILED);
		tmp = fu_plugin_uefi_last_attempt_status_to_str (status);
		if (tmp != NULL)
			fu_device_set_update_error (device, tmp);
	}
	return TRUE;
}

gboolean
fu_plugin_update (FuPlugin *plugin,
		  FuDevice *device,
		  GBytes *blob_fw,
		  FwupdInstallFlags flags,
		  GError **error)
{
	fwup_resource *re = NULL;
	guint64 hardware_instance = 0;	/* FIXME */
	int rc;
	g_autoptr(fwup_resource_iter) iter = NULL;
	const gchar *str;
	g_autofree gchar *efibootmgr_path = NULL;
	g_autofree gchar *boot_variables = NULL;

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
	fu_plugin_set_status (plugin, FWUPD_STATUS_SCHEDULING);
	rc = fwup_set_up_update_with_buf (re, hardware_instance,
					  g_bytes_get_data (blob_fw, NULL),
					  g_bytes_get_size (blob_fw));
	if (rc < 0) {
		g_autoptr(GString) err_string = g_string_new ("UEFI firmware update failed:\n");

		rc = 1;
		for (int i =0; rc > 0; i++) {
			char *filename = NULL;
			char *function = NULL;
			char *message = NULL;
			int line = 0;
			int err = 0;

			rc = efi_error_get (i, &filename, &function, &line, &message, &err);
			if (rc <= 0)
				break;
			g_string_append_printf (err_string,
						"{error #%d} %s:%d %s(): %s: %s \n",
						i, filename, line, function, message, strerror(err));
		}
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     err_string->str);
		return FALSE;
	}

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
fu_plugin_uefi_get_version_format (FuPlugin *plugin)
{
	const gchar *content;

	content = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_MANUFACTURER);
	if (content == NULL)
		return AS_VERSION_PARSE_FLAG_USE_TRIPLET;

	/* any vendors match */
	for (guint i = 0; quirk_table[i].sys_vendor != NULL; i++) {
		if (g_strcmp0 (content, quirk_table[i].sys_vendor) == 0)
			return quirk_table[i].flags;
	}

	/* fall back */
	return AS_VERSION_PARSE_FLAG_USE_TRIPLET;
}

gboolean
fu_plugin_unlock (FuPlugin *plugin,
			 FuDevice *device,
			 GError **error)
{
#ifdef HAVE_UEFI_UNLOCK
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
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Not supported, update libfwupdate!");
	return FALSE;
#endif
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

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	AsVersionParseFlag parse_flags;
	const gchar *product_name;
	fwup_resource *re;
	gint supported;
	g_autofree gchar *guid = NULL;
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(fwup_resource_iter) iter = NULL;

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
		dev = fu_device_new ();
		fu_device_set_id (dev, "UEFI-dummy-dev0");
		fu_device_add_guid (dev, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
		fu_device_set_version (dev, "0");
		fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_LOCKED);
		fu_plugin_device_add (plugin, dev);
		return TRUE;
	}

	/* this can fail if we have no permissions */
	if (fwup_resource_iter_create (&iter) < 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Cannot create fwup iter");
		return FALSE;
	}

	/* set Display Name to the system for all capsules */
	product_name = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_PRODUCT_NAME);

	/* add each device */
	guid = g_strdup ("00000000-0000-0000-0000-000000000000");
	parse_flags = fu_plugin_uefi_get_version_format (plugin);
	while (fwup_resource_iter_next (iter, &re) > 0) {
		const gchar *uefi_type_str = NULL;
		efi_guid_t *guid_raw;
		guint32 uefi_type;
		guint32 version_raw;
		guint64 hardware_instance = 0;	/* FIXME */
		g_autofree gchar *id = NULL;
		g_autofree gchar *version = NULL;
		g_autofree gchar *version_lowest = NULL;
		g_autoptr(GString) display_name = g_string_new (NULL);

		/* set up proper DisplayName */
		fwup_get_fw_type (re, &uefi_type);
		if (product_name != NULL)
			g_string_append (display_name, product_name);
		uefi_type_str = fu_plugin_uefi_uefi_type_to_string (uefi_type);
		if (uefi_type_str != NULL) {
			if (display_name->len > 0)
				g_string_append (display_name, " ");
			g_string_append (display_name, uefi_type_str);
		}

		/* convert to strings */
		fwup_get_guid (re, &guid_raw);
		if (efi_guid_to_str (guid_raw, &guid) < 0) {
			g_warning ("failed to convert guid to string");
			continue;
		}
		fwup_get_fw_version(re, &version_raw);
		version = as_utils_version_from_uint32 (version_raw,
							parse_flags);
		id = g_strdup_printf ("UEFI-%s-dev%" G_GUINT64_FORMAT,
				      guid, hardware_instance);

		dev = fu_device_new ();
		fu_device_set_id (dev, id);
		fu_device_add_guid (dev, guid);
		fu_device_set_version (dev, version);
		if (display_name->len > 0)
			fu_device_set_name(dev, display_name->str);
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
	return TRUE;
}
