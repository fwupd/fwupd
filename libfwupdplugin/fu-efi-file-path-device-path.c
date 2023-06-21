/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuEfiDevicePath"

#include "config.h"

#include "fu-common.h"
#include "fu-efi-file-path-device-path.h"
#include "fu-efi-struct.h"
#include "fu-string.h"

/**
 * FuEfiFilePathDevicePath:
 *
 * See also: [class@FuEfiDevicePath]
 */

struct _FuEfiFilePathDevicePath {
	FuEfiDevicePath parent_instance;
};

G_DEFINE_TYPE(FuEfiFilePathDevicePath, fu_efi_file_path_device_path, FU_TYPE_EFI_DEVICE_PATH)

/**
 * fu_efi_file_path_device_path_get_name:
 * @self: a #FuEfiFilePathDevicePath
 * @error: (nullable): optional return location for an error
 *
 * Gets the `DEVICE_PATH` name.
 * Any backslash characters are automatically converted to forward slashes.
 *
 * Returns: (transfer full): UTF-8 filename, or %NULL on error
 *
 * Since: 1.9.3
 **/
gchar *
fu_efi_file_path_device_path_get_name(FuEfiFilePathDevicePath *self, GError **error)
{
	g_autofree gchar *name = NULL;
	g_autoptr(GBytes) blob = NULL;

	g_return_val_if_fail(FU_IS_EFI_FILE_PATH_DEVICE_PATH(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	blob = fu_firmware_get_bytes(FU_FIRMWARE(self), error);
	if (blob == NULL)
		return NULL;
	name = fu_utf16_to_utf8_bytes(blob, error);
	if (name == NULL)
		return NULL;
	g_strdelimit(name, "\\", '/');
	return g_steal_pointer(&name);
}

/**
 * fu_efi_file_path_device_path_set_name:
 * @self: a #FuEfiFilePathDevicePath
 * @name: (nullable): a path to a EFI binary, typically prefixed with a backslash
 * @error: (nullable): optional return location for an error
 *
 * Sets the `DEVICE_PATH` name.
 * Any forward slash characters are automatically converted to backslashes.
 *
 * Since: 1.9.3
 **/
gboolean
fu_efi_file_path_device_path_set_name(FuEfiFilePathDevicePath *self,
				      const gchar *name,
				      GError **error)
{
	g_autoptr(GBytes) blob = NULL;

	g_return_val_if_fail(FU_IS_EFI_FILE_PATH_DEVICE_PATH(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (name != NULL) {
		g_autofree gchar *name_bs = g_strdup(name);
		g_autoptr(GByteArray) buf = NULL;
		g_strdelimit(name_bs, "/", '\\');
		buf = fu_utf8_to_utf16_byte_array(name_bs, FU_UTF_CONVERT_FLAG_APPEND_NUL, error);
		if (buf == NULL)
			return FALSE;
		blob = g_bytes_new(buf->data, buf->len);
	} else {
		blob = g_bytes_new(NULL, 0);
	}
	fu_firmware_set_bytes(FU_FIRMWARE(self), blob);
	return TRUE;
}

static void
fu_efi_file_path_device_path_export(FuFirmware *firmware,
				    FuFirmwareExportFlags flags,
				    XbBuilderNode *bn)
{
	FuEfiFilePathDevicePath *self = FU_EFI_FILE_PATH_DEVICE_PATH(firmware);
	g_autofree gchar *name = fu_efi_file_path_device_path_get_name(self, NULL);
	fu_xmlb_builder_insert_kv(bn, "name", name);
}

static gboolean
fu_efi_file_path_device_path_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuEfiFilePathDevicePath *self = FU_EFI_FILE_PATH_DEVICE_PATH(firmware);
	g_autoptr(XbNode) data = NULL;

	/* optional data */
	data = xb_node_query_first(n, "name", NULL);
	if (data != NULL) {
		if (!fu_efi_file_path_device_path_set_name(self, xb_node_get_text(data), error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_efi_file_path_device_path_init(FuEfiFilePathDevicePath *self)
{
	fu_firmware_set_idx(FU_FIRMWARE(self), FU_EFI_DEVICE_PATH_TYPE_MEDIA);
	fu_efi_device_path_set_subtype(FU_EFI_DEVICE_PATH(self),
				       FU_EFI_HARD_DRIVE_DEVICE_PATH_SUBTYPE_FILE_PATH);
}

static void
fu_efi_file_path_device_path_class_init(FuEfiFilePathDevicePathClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->export = fu_efi_file_path_device_path_export;
	klass_firmware->build = fu_efi_file_path_device_path_build;
}

/**
 * fu_efi_file_path_device_path_new:
 *
 * Creates a new EFI `DEVICE_PATH`.
 *
 * Returns: (transfer full): a #FuEfiFilePathDevicePath
 *
 * Since: 1.9.3
 **/
FuEfiFilePathDevicePath *
fu_efi_file_path_device_path_new(void)
{
	return g_object_new(FU_TYPE_EFI_FILE_PATH_DEVICE_PATH, NULL);
}
