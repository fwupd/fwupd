/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuWindowsEfivars"

#include "config.h"

#include <windows.h>

#include "fwupd-error.h"

#include "fu-byte-array.h"
#include "fu-windows-efivars.h"

struct _FuWindowsEfivars {
	FuEfivars parent_instance;
};

G_DEFINE_TYPE(FuWindowsEfivars, fu_windows_efivars, FU_TYPE_EFIVARS)

static gboolean
fu_windows_efivars_supported(FuEfivars *efivars, GError **error)
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

static gboolean
fu_windows_efivars_is_running_under_wine(void)
{
	HKEY hKey = NULL;
	LONG lResult = RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Software\\Wine", 0, KEY_READ, &hKey);
	if (lResult == ERROR_SUCCESS) {
		RegCloseKey(hKey);
		return TRUE;
	}
	return FALSE;
}

static gboolean
fu_windows_efivars_get_data(FuEfivars *efivars,
			    const gchar *guid,
			    const gchar *name,
			    guint8 **data,
			    gsize *data_sz,
			    guint32 *attr,
			    GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autofree gchar *guid_win32 = g_strdup_printf("{%s}", guid);
	fu_byte_array_set_size(buf, 0x1000, 0xFF);

	/* unimplemented function KERNEL32.dll.GetFirmwareEnvironmentVariableExA on wine */
	if (fu_windows_efivars_is_running_under_wine()) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "GetFirmwareEnvironmentVariableExA is not implemented");
		return FALSE;
	}

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
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "failed to get get variable [%u]",
		    (guint)GetLastError());
	return FALSE;
}

static gboolean
fu_windows_efivars_exists(FuEfivars *efivars, const gchar *guid, const gchar *name)
{
	return fu_windows_efivars_get_data(efivars, guid, name, NULL, NULL, NULL, NULL);
}

/* there is no win32 kernel interface for GetNextVariable so use from UEFI spec v2.8 */
static GPtrArray *
fu_windows_efivars_get_names(FuEfivars *efivars, const gchar *guid, GError **error)
{
	g_autoptr(GPtrArray) names = g_ptr_array_new_with_free_func(g_free);
	struct {
		const gchar *guid;
		const gchar *name;
	} variable_names[] = {{FU_EFIVARS_GUID_EFI_GLOBAL, "AuditMode"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "BootCurrent"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "BootNext"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "BootOptionSupport"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "BootOrder"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "BootOrderDefault"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "BootXXXX"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "ConIn"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "ConInDev"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "ConOut"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "ConOutDev"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "CurrentPolicy"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "dbDefault"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "dbrDefault"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "dbtDefault"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "dbxDefault"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "DeployedMode"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "DriverOrder"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "DriverXXXX"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "ErrOut"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "ErrOutDev"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "HwErrRecSupprot"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "KEK"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "KEKDefault"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "KeyXXXX"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "Lang"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "LangCodes"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "OsIndications"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "OsIndicationsSupported"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "OsRecoveryOrder"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "PK"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "PKDefault"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "PlatformLang"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "PlatformLangCodes"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "PlatformRecoveryXXXX"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "RuntimeServicesSupported"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "SecureBoot"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "SetupMode"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "SignatureSupport"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "SysPrepOrder"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "SysPrepXXXX"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "Timeout"},
			      {FU_EFIVARS_GUID_EFI_GLOBAL, "VendorKeys"},
			      {FU_EFIVARS_GUID_FWUPDATE, "FWUPDATE_DEBUG_LOG"},
			      {FU_EFIVARS_GUID_FWUPDATE, "FWUPDATE_VERBOSE"},
			      {FU_EFIVARS_GUID_FWUPDATE, "fwupd-ux-capsule"},
			      {FU_EFIVARS_GUID_SECURITY_DATABASE, "db"},
			      {FU_EFIVARS_GUID_SECURITY_DATABASE, "dbx"},
			      {NULL, NULL}};

	/* look for each possible guid+name */
	for (guint i = 0; variable_names[i].guid != NULL; i++) {
		if (g_strcmp0(FU_EFIVARS_GUID_EFI_GLOBAL, variable_names[i].guid) != 0)
			continue;
		if (g_str_has_suffix(variable_names[i].name, "XXXX")) {
			g_autoptr(GString) name_root = g_string_new(variable_names[i].name);
			g_string_truncate(name_root, name_root->len - 4);
			for (guint j = 0; j < G_MAXUINT16; j++) {
				g_autofree gchar *name =
				    g_strdup_printf("%s%04X", name_root->str, j);
				if (fu_windows_efivars_exists(efivars,
							      variable_names[i].guid,
							      name))
					g_ptr_array_add(names, g_steal_pointer(&name));
			}
		} else {
			if (fu_windows_efivars_exists(efivars,
						      variable_names[i].guid,
						      variable_names[i].name))
				g_ptr_array_add(names, g_strdup(variable_names[i].name));
		}
	}

	/* nothing found */
	if (names->len == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "no names for GUID %s",
			    guid);
		return NULL;
	}

	/* success */
	return g_steal_pointer(&names);
}

static gboolean
fu_windows_efivars_set_data(FuEfivars *efivars,
			    const gchar *guid,
			    const gchar *name,
			    const guint8 *data,
			    gsize sz,
			    guint32 attr,
			    GError **error)
{
	g_autofree gchar *guid_win32 = g_strdup_printf("{%s}", guid);

	/* unimplemented function KERNEL32.dll.SetFirmwareEnvironmentVariableExA on wine */
	if (fu_windows_efivars_is_running_under_wine()) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "SetFirmwareEnvironmentVariableExA is not implemented");
		return FALSE;
	}
	if (!SetFirmwareEnvironmentVariableExA(name, guid_win32, (PVOID)data, sz, attr)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to get set variable [%u]",
			    (guint)GetLastError());
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_windows_efivars_delete(FuEfivars *efivars, const gchar *guid, const gchar *name, GError **error)
{
	/* size of 0 bytes -> delete */
	return fu_windows_efivars_set_data(efivars, guid, name, NULL, 0, 0, error);
}

static gboolean
fu_windows_efivars_delete_with_glob(FuEfivars *efivars,
				    const gchar *guid,
				    const gchar *name_glob,
				    GError **error)
{
	g_autoptr(GPtrArray) names = NULL;
	g_autoptr(GError) error_local = NULL;

	names = fu_windows_efivars_get_names(efivars, guid, &error_local);
	if (names == NULL) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND))
			return TRUE;
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	for (guint i = 0; i < names->len; i++) {
		const gchar *name = g_ptr_array_index(names, i);
		if (g_pattern_match_simple(name_glob, name)) {
			if (!fu_windows_efivars_delete(efivars, guid, name, error))
				return FALSE;
		}
	}
	return TRUE;
}

static void
fu_windows_efivars_init(FuWindowsEfivars *self)
{
}

static void
fu_windows_efivars_class_init(FuWindowsEfivarsClass *klass)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_CLASS(klass);
	efivars_class->supported = fu_windows_efivars_supported;
	efivars_class->exists = fu_windows_efivars_exists;
	efivars_class->get_data = fu_windows_efivars_get_data;
	efivars_class->set_data = fu_windows_efivars_set_data;
	efivars_class->delete = fu_windows_efivars_delete;
	efivars_class->delete_with_glob = fu_windows_efivars_delete_with_glob;
	efivars_class->get_names = fu_windows_efivars_get_names;
}

FuEfivars *
fu_efivars_new(void)
{
	return FU_EFIVARS(g_object_new(FU_TYPE_FREEBSD_EFIVARS, NULL));
}
