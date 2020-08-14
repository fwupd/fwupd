/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"
#include "fu-efivar.h"
#include "fu-hash.h"
#include "fu-efi-signature-common.h"
#include "fu-efi-signature-parser.h"
#include "fu-uefi-dbx-common.h"
#include "fu-uefi-dbx-device.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	g_autoptr(FuUefiDbxDevice) device = fu_uefi_dbx_device_new ();
	if (!fu_device_probe (FU_DEVICE (device), error))
		return FALSE;
	if (!fu_device_setup (FU_DEVICE (device), error))
		return FALSE;
	fu_plugin_device_add (plugin, FU_DEVICE (device));
	return TRUE;
}

void
fu_plugin_add_security_attrs (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	gsize bufsz = 0;
	g_autofree guint8 *buf_system = NULL;
	g_autofree guint8 *buf_update = NULL;
	g_autoptr(GPtrArray) dbx_system = NULL;
	g_autoptr(GPtrArray) dbx_update = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *fn = NULL;

	/* find the latest DBX on the system */
	fn = fu_uefi_dbx_get_dbxupdate (&error_local);
	if (fn == NULL) {
		g_warning ("cannot find any updates: %s", error_local->message);
		return;
	}

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_UEFI_DBX);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fu_security_attrs_append (attrs, attr);

	/* no binary blob */
	if (!fu_plugin_get_enabled (plugin)) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		fwupd_security_attr_set_url (attr, "https://github.com/fwupd/fwupd/wiki/Missingdbx");
		return;
	}

	/* get update dbx */
	if (!g_file_get_contents (fn, (gchar **) &buf_update, &bufsz, &error_local)) {
		g_warning ("failed to load %s: %s", fn, error_local->message);
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}
	dbx_update = fu_efi_signature_parser_new (buf_update, bufsz,
						  FU_EFI_SIGNATURE_PARSER_FLAGS_IGNORE_HEADER,
						  &error_local);
	if (dbx_update == NULL) {
		g_warning ("failed to parse %s: %s", fn, error_local->message);
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}

	/* get system dbx */
	if (!fu_efivar_get_data (FU_EFIVAR_GUID_SECURITY_DATABASE, "dbx",
				 &buf_system, &bufsz, NULL, &error_local)) {
		g_warning ("failed to load EFI dbx: %s", error_local->message);
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}
	dbx_system = fu_efi_signature_parser_new (buf_system, bufsz,
						  FU_EFI_SIGNATURE_PARSER_FLAGS_NONE,
						  &error_local);
	if (dbx_system == NULL) {
		g_warning ("failed to parse EFI dbx: %s", error_local->message);
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}

	/* look for each checksum in the update in the system version */
	if (!fu_efi_signature_list_array_inclusive (dbx_system, dbx_update)) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_FOUND);
}
