/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <windows.h>

#include "fwupd-error.h"

#include "fu-byte-array.h"
#include "fu-efivar-impl.h"

gboolean
fu_efivar_supported_impl(GError **error)
{
	FIRMWARE_TYPE firmware_type = {0};
	DWORD rc;

	/* sanity check */
	if (!GetFirmwareType(&firmware_type)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot get firmware type [%u]",
			    (guint)GetLastError());
		return FALSE;
	}
	if (firmware_type != FirmwareTypeUefi) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "only supported on UEFI firmware");
		return FALSE;
	}

	/* check supported */
	rc = GetFirmwareEnvironmentVariableA("", "{00000000-0000-0000-0000-000000000000}", NULL, 0);
	if (rc == 0 && GetLastError() == ERROR_INVALID_FUNCTION) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "getting EFI variables is not supported on this system");
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_efivar_delete_impl(const gchar *guid, const gchar *name, GError **error)
{
	/* size of 0 bytes -> delete */
	return fu_efivar_set_data_impl(guid, name, NULL, 0, 0, error);
}

gboolean
fu_efivar_delete_with_glob_impl(const gchar *guid, const gchar *name_glob, GError **error)
{
	g_autoptr(GPtrArray) names = NULL;
	g_autoptr(GError) error_local = NULL;

	names = fu_efivar_get_names_impl(guid, &error_local);
	if (names == NULL) {
		if (g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return TRUE;
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	for (guint i = 0; i < names->len; i++) {
		const gchar *name = g_ptr_array_index(names, i);
		if (g_pattern_match_simple(name_glob, name)) {
			if (!fu_efivar_delete_impl(guid, name, error))
				return FALSE;
		}
	}
	return TRUE;
}

gboolean
fu_efivar_exists_impl(const gchar *guid, const gchar *name)
{
	return fu_efivar_get_data_impl(guid, name, NULL, NULL, NULL, NULL);
}

gboolean
fu_efivar_get_data_impl(const gchar *guid,
			const gchar *name,
			guint8 **data,
			gsize *data_sz,
			guint32 *attr,
			GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autofree gchar *guid_win32 = g_strdup_printf("{%s}", guid);
	fu_byte_array_set_size(buf, 0x1000, 0xFF);
	do {
		DWORD dwAttribubutes = 0;
		DWORD rc = GetFirmwareEnvironmentVariableExA(name,
							     guid_win32,
							     buf->data,
							     buf->len,
							     &dwAttribubutes);
		if (rc > 0) {
			if (data != NULL)
				*data = g_byte_array_free(g_steal_pointer(&buf), FALSE);
			if (data_sz != NULL)
				*data_sz = rc;
			if (attr != NULL)
				*attr = dwAttribubutes;
			return TRUE;
		}
		if (rc == 0 && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
			break;
		fu_byte_array_set_size(buf, buf->len * 2, 0xFF);
	} while (buf->len < 0x400000);

	/* failed */
	g_set_error(error,
		    G_IO_ERROR,
		    G_IO_ERROR_FAILED,
		    "failed to get get variable [%u]",
		    (guint)GetLastError());
	return FALSE;
}

/* there is no win32 kernel interface for GetNextVariable so use from UEFI spec v2.8 */
GPtrArray *
fu_efivar_get_names_impl(const gchar *guid, GError **error)
{
	g_autoptr(GPtrArray) names = g_ptr_array_new_with_free_func(g_free);
	struct {
		const gchar *guid;
		const gchar *name;
	} variable_names[] = {{FU_EFIVAR_GUID_EFI_GLOBAL, "AuditMode"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "BootCurrent"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "BootNext"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "BootOptionSupport"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "BootOrder"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "BootOrderDefault"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "BootXXXX"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "ConIn"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "ConInDev"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "ConOut"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "ConOutDev"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "CurrentPolicy"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "dbDefault"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "dbrDefault"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "dbtDefault"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "dbxDefault"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "DeployedMode"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "DriverOrder"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "DriverXXXX"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "ErrOut"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "ErrOutDev"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "HwErrRecSupprot"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "KEK"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "KEKDefault"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "KeyXXXX"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "Lang"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "LangCodes"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "OsIndications"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "OsIndicationsSupported"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "OsRecoveryOrder"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "PK"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "PKDefault"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "PlatformLang"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "PlatformLangCodes"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "PlatformRecoveryXXXX"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "RuntimeServicesSupported"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "SecureBoot"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "SetupMode"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "SignatureSupport"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "SysPrepOrder"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "SysPrepXXXX"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "Timeout"},
			      {FU_EFIVAR_GUID_EFI_GLOBAL, "VendorKeys"},
			      {FU_EFIVAR_GUID_FWUPDATE, "FWUPDATE_DEBUG_LOG"},
			      {FU_EFIVAR_GUID_FWUPDATE, "FWUPDATE_VERBOSE"},
			      {FU_EFIVAR_GUID_FWUPDATE, "fwupd-ux-capsule"},
			      {FU_EFIVAR_GUID_SECURITY_DATABASE, "db"},
			      {FU_EFIVAR_GUID_SECURITY_DATABASE, "dbx"},
			      {NULL, NULL}};

	/* look for each possible guid+name */
	for (guint i = 0; variable_names[i].guid != NULL; i++) {
		if (g_strcmp0(FU_EFIVAR_GUID_EFI_GLOBAL, variable_names[i].guid) != 0)
			continue;
		if (g_str_has_suffix(variable_names[i].name, "XXXX")) {
			g_autoptr(GString) name_root = g_string_new(variable_names[i].name);
			g_string_truncate(name_root, name_root->len - 4);
			for (guint j = 0; j < G_MAXUINT16; j++) {
				g_autofree gchar *name =
				    g_strdup_printf("%s%04X", name_root->str, j);
				if (fu_efivar_exists_impl(variable_names[i].guid, name))
					g_ptr_array_add(names, g_steal_pointer(&name));
			}
		} else {
			if (fu_efivar_exists_impl(variable_names[i].guid, variable_names[i].name))
				g_ptr_array_add(names, g_strdup(variable_names[i].name));
		}
	}

	/* nothing found */
	if (names->len == 0) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no names for GUID %s", guid);
		return NULL;
	}

	/* success */
	return g_steal_pointer(&names);
}

GFileMonitor *
fu_efivar_get_monitor_impl(const gchar *guid, const gchar *name, GError **error)
{
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "monitoring EFI variables is not supported on Windows");
	return NULL;
}

guint64
fu_efivar_space_used_impl(GError **error)
{
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "getting EFI used space is not supported on Windows");
	return G_MAXUINT64;
}

gboolean
fu_efivar_set_data_impl(const gchar *guid,
			const gchar *name,
			const guint8 *data,
			gsize sz,
			guint32 attr,
			GError **error)
{
	g_autofree gchar *guid_win32 = g_strdup_printf("{%s}", guid);
	if (!SetFirmwareEnvironmentVariableExA(name, guid_win32, (PVOID)data, sz, attr)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "failed to get set variable [%u]",
			    (guint)GetLastError());
		return FALSE;
	}
	return TRUE;
}
