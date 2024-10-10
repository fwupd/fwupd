/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-error.h"

#include "fu-byte-array.h"
#include "fu-efi-device-path-list.h"
#include "fu-efi-file-path-device-path.h"
#include "fu-efi-hard-drive-device-path.h"
#include "fu-efivars-private.h"
#include "fu-mem.h"
#include "fu-pefile-firmware.h"
#include "fu-volume-private.h"

G_DEFINE_TYPE(FuEfivars, fu_efivars, G_TYPE_OBJECT)

/**
 * fu_efivars_supported:
 * @self: a #FuEfivars
 * @error: #GError
 *
 * Determines if the kernel supports EFI variables
 *
 * Returns: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_efivars_supported(FuEfivars *self, GError **error)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (efivars_class->supported == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not supported");
		return FALSE;
	}
	return efivars_class->supported(self, error);
}

/**
 * fu_efivars_delete:
 * @self: a #FuEfivars
 * @guid: Globally unique identifier
 * @name: Variable name
 * @error: #GError
 *
 * Removes a variable from NVRAM, returning an error if it does not exist.
 *
 * Returns: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_efivars_delete(FuEfivars *self, const gchar *guid, const gchar *name, GError **error)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(guid != NULL, FALSE);
	g_return_val_if_fail(name != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (efivars_class->delete == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not supported");
		return FALSE;
	}
	return efivars_class->delete (self, guid, name, error);
}

/**
 * fu_efivars_delete_with_glob:
 * @self: a #FuEfivars
 * @guid: Globally unique identifier
 * @name_glob: Variable name
 * @error: #GError
 *
 * Removes a group of variables from NVRAM
 *
 * Returns: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_efivars_delete_with_glob(FuEfivars *self,
			    const gchar *guid,
			    const gchar *name_glob,
			    GError **error)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(guid != NULL, FALSE);
	g_return_val_if_fail(name_glob != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (efivars_class->delete_with_glob == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not supported");
		return FALSE;
	}
	return efivars_class->delete_with_glob(self, guid, name_glob, error);
}

/**
 * fu_efivars_exists:
 * @self: a #FuEfivars
 * @guid: Globally unique identifier
 * @name: (nullable): Variable name
 *
 * Test if a variable exists
 *
 * Returns: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_efivars_exists(FuEfivars *self, const gchar *guid, const gchar *name)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(guid != NULL, FALSE);

	if (efivars_class->exists == NULL)
		return FALSE;
	return efivars_class->exists(self, guid, name);
}

/**
 * fu_efivars_get_data:
 * @self: a #FuEfivars
 * @guid: Globally unique identifier
 * @name: Variable name
 * @data: Data to set
 * @data_sz: size of data
 * @attr: Attributes
 * @error: (nullable): optional return location for an error
 *
 * Gets the data from a UEFI variable in NVRAM
 *
 * Returns: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_efivars_get_data(FuEfivars *self,
		    const gchar *guid,
		    const gchar *name,
		    guint8 **data,
		    gsize *data_sz,
		    guint32 *attr,
		    GError **error)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(guid != NULL, FALSE);
	g_return_val_if_fail(name != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (efivars_class->get_data == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not supported");
		return FALSE;
	}
	return efivars_class->get_data(self, guid, name, data, data_sz, attr, error);
}

/**
 * fu_efivars_get_data_bytes:
 * @self: a #FuEfivars
 * @guid: Globally unique identifier
 * @name: Variable name
 * @attr: (nullable): Attributes
 * @error: (nullable): optional return location for an error
 *
 * Gets the data from a UEFI variable in NVRAM
 *
 * Returns: (transfer full): a #GBytes, or %NULL
 *
 * Since: 2.0.0
 **/
GBytes *
fu_efivars_get_data_bytes(FuEfivars *self,
			  const gchar *guid,
			  const gchar *name,
			  guint32 *attr,
			  GError **error)
{
	guint8 *data = NULL;
	gsize datasz = 0;

	g_return_val_if_fail(FU_IS_EFIVARS(self), NULL);
	g_return_val_if_fail(guid != NULL, NULL);
	g_return_val_if_fail(name != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (!fu_efivars_get_data(self, guid, name, &data, &datasz, attr, error))
		return NULL;
	return g_bytes_new_take(data, datasz);
}

/**
 * fu_efivars_get_names:
 * @self: a #FuEfivars
 * @guid: Globally unique identifier
 * @error: (nullable): optional return location for an error
 *
 * Gets the list of names where the GUID matches. An error is set if there are
 * no names matching the GUID.
 *
 * Returns: (transfer container) (element-type utf8): array of names
 *
 * Since: 2.0.0
 **/
GPtrArray *
fu_efivars_get_names(FuEfivars *self, const gchar *guid, GError **error)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_EFIVARS(self), NULL);
	g_return_val_if_fail(guid != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (efivars_class->get_names == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not supported");
		return NULL;
	}
	return efivars_class->get_names(self, guid, error);
}

/**
 * fu_efivars_get_monitor:
 * @self: a #FuEfivars
 * @guid: Globally unique identifier
 * @name: Variable name
 * @error: (nullable): optional return location for an error
 *
 * Returns a file monitor for a specific key.
 *
 * Returns: (transfer full): a #GFileMonitor, or %NULL for an error
 *
 * Since: 2.0.0
 **/
GFileMonitor *
fu_efivars_get_monitor(FuEfivars *self, const gchar *guid, const gchar *name, GError **error)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_EFIVARS(self), NULL);
	g_return_val_if_fail(guid != NULL, NULL);
	g_return_val_if_fail(name != NULL, NULL);

	if (efivars_class->get_monitor == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not supported");
		return NULL;
	}
	return efivars_class->get_monitor(self, guid, name, error);
}

/**
 * fu_efivars_space_used:
 * @self: a #FuEfivars
 * @error: (nullable): optional return location for an error
 *
 * Gets the total size used by all EFI variables. This may be less than the size reported by the
 * kernel as some (hopefully small) variables are hidden from userspace.
 *
 * Returns: total allocated size of all visible variables, or %G_MAXUINT64 on error
 *
 * Since: 2.0.0
 **/
guint64
fu_efivars_space_used(FuEfivars *self, GError **error)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_EFIVARS(self), G_MAXUINT64);
	g_return_val_if_fail(error == NULL || *error == NULL, G_MAXUINT64);

	if (efivars_class->space_used == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not supported");
		return G_MAXUINT64;
	}
	return efivars_class->space_used(self, error);
}

/**
 * fu_efivars_set_data:
 * @self: a #FuEfivars
 * @guid: Globally unique identifier
 * @name: Variable name
 * @data: Data to set
 * @sz: size of @data
 * @attr: Attributes
 * @error: (nullable): optional return location for an error
 *
 * Sets the data to a UEFI variable in NVRAM
 *
 * Returns: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_efivars_set_data(FuEfivars *self,
		    const gchar *guid,
		    const gchar *name,
		    const guint8 *data,
		    gsize sz,
		    guint32 attr,
		    GError **error)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(guid != NULL, FALSE);
	g_return_val_if_fail(name != NULL, FALSE);
	g_return_val_if_fail(data != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (efivars_class->set_data == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not supported");
		return FALSE;
	}
	return efivars_class->set_data(self, guid, name, data, sz, attr, error);
}

/**
 * fu_efivars_set_data_bytes:
 * @self: a #FuEfivars
 * @guid: globally unique identifier
 * @name: variable name
 * @bytes: data blob
 * @attr: attributes
 * @error: (nullable): optional return location for an error
 *
 * Sets the data to a UEFI variable in NVRAM
 *
 * Returns: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_efivars_set_data_bytes(FuEfivars *self,
			  const gchar *guid,
			  const gchar *name,
			  GBytes *bytes,
			  guint32 attr,
			  GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf;

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(guid != NULL, FALSE);
	g_return_val_if_fail(name != NULL, FALSE);
	g_return_val_if_fail(bytes != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	buf = g_bytes_get_data(bytes, &bufsz);
	return fu_efivars_set_data(self, guid, name, buf, bufsz, attr, error);
}

/**
 * fu_efivars_get_secure_boot:
 * @self: a #FuEfivars
 * @enabled: (out): SecureBoot value
 * @error: (nullable): optional return location for an error
 *
 * Determines if secure boot was enabled
 *
 * Returns: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_efivars_get_secure_boot(FuEfivars *self, gboolean *enabled, GError **error)
{
	gsize data_size = 0;
	g_autofree guint8 *data = NULL;

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_efivars_get_data(self,
				 FU_EFIVARS_GUID_EFI_GLOBAL,
				 "SecureBoot",
				 &data,
				 &data_size,
				 NULL,
				 NULL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "SecureBoot is not available");
		return FALSE;
	}
	if (data_size == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "SecureBoot variable was empty");
		return FALSE;
	}

	/* available, but not enabled */
	if (enabled != NULL)
		*enabled = (data[0] & 0x01) > 0;

	/* success */
	return TRUE;
}

/**
 * fu_efivars_set_secure_boot: (skip):
 **/
gboolean
fu_efivars_set_secure_boot(FuEfivars *self, gboolean enabled, GError **error)
{
	guint8 value = enabled ? 0x01 : 0x00;
	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return fu_efivars_set_data(self,
				   FU_EFIVARS_GUID_EFI_GLOBAL,
				   "SecureBoot",
				   &value,
				   sizeof(value),
				   FU_EFIVARS_ATTR_BOOTSERVICE_ACCESS,
				   error);
}

/**
 * fu_efivars_get_boot_next:
 * @self: a #FuEfivars
 * @idx: (out) (nullable): boot index, typically 0x0001
 * @error: #GError
 *
 * Gets the index of the `BootNext` variable.
 *
 * Returns: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_efivars_get_boot_next(FuEfivars *self, guint16 *idx, GError **error)
{
	g_autofree guint8 *buf = NULL;
	gsize bufsz = 0;

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_efivars_get_data(self,
				 FU_EFIVARS_GUID_EFI_GLOBAL,
				 "BootNext",
				 &buf,
				 &bufsz,
				 NULL,
				 error))
		return FALSE;
	if (bufsz != sizeof(guint16)) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "invalid size");
		return FALSE;
	}
	if (idx != NULL)
		*idx = fu_memread_uint16(buf, G_LITTLE_ENDIAN);

	/* success */
	return TRUE;
}

/**
 * fu_efivars_set_boot_next:
 * @self: a #FuEfivars
 * @idx: boot index, typically 0x0001
 * @error: #GError
 *
 * Sets the index of the `BootNext` variable.
 *
 * Returns: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_efivars_set_boot_next(FuEfivars *self, guint16 idx, GError **error)
{
	guint8 buf[2] = {0};
	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	fu_memwrite_uint16(buf, idx, G_LITTLE_ENDIAN);
	return fu_efivars_set_data(self,
				   FU_EFIVARS_GUID_EFI_GLOBAL,
				   "BootNext",
				   buf,
				   sizeof(buf),
				   FU_EFIVARS_ATTR_NON_VOLATILE |
				       FU_EFIVARS_ATTR_BOOTSERVICE_ACCESS |
				       FU_EFIVARS_ATTR_RUNTIME_ACCESS,
				   error);
}

/**
 * fu_efivars_get_boot_current:
 * @self: a #FuEfivars
 * @idx: (out): boot index, typically 0x0001
 * @error: #GError
 *
 * Gets the index of the `BootCurrent` variable.
 *
 * Returns: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_efivars_get_boot_current(FuEfivars *self, guint16 *idx, GError **error)
{
	g_autofree guint8 *buf = NULL;
	gsize bufsz = 0;

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_efivars_get_data(self,
				 FU_EFIVARS_GUID_EFI_GLOBAL,
				 "BootCurrent",
				 &buf,
				 &bufsz,
				 NULL,
				 error))
		return FALSE;
	if (bufsz != sizeof(guint16)) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "invalid size");
		return FALSE;
	}
	if (idx != NULL)
		*idx = fu_memread_uint16(buf, G_LITTLE_ENDIAN);

	/* success */
	return TRUE;
}

/**
 * fu_efivars_set_boot_current: (skip):
 **/
gboolean
fu_efivars_set_boot_current(FuEfivars *self, guint16 idx, GError **error)
{
	guint8 buf[2] = {0};

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	fu_memwrite_uint16(buf, idx, G_LITTLE_ENDIAN);
	return fu_efivars_set_data(self,
				   FU_EFIVARS_GUID_EFI_GLOBAL,
				   "BootCurrent",
				   buf,
				   sizeof(buf),
				   FU_EFIVARS_ATTR_NON_VOLATILE | FU_EFIVARS_ATTR_RUNTIME_ACCESS,
				   error);
}

/**
 * fu_efivars_get_boot_order:
 * @self: a #FuEfivars
 * @error: #GError
 *
 * Gets the indexes of the `BootOrder` variable.
 *
 * Returns: (transfer full) (element-type guint16): boot order, or %NULL on error
 *
 * Since: 2.0.0
 **/
GArray *
fu_efivars_get_boot_order(FuEfivars *self, GError **error)
{
	gsize bufsz = 0;
	g_autofree guint8 *buf = NULL;
	g_autoptr(GArray) order = g_array_new(FALSE, FALSE, sizeof(guint16));

	g_return_val_if_fail(FU_IS_EFIVARS(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (!fu_efivars_get_data(self,
				 FU_EFIVARS_GUID_EFI_GLOBAL,
				 "BootOrder",
				 &buf,
				 &bufsz,
				 NULL,
				 error))
		return NULL;
	if (bufsz % sizeof(guint16) != 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "invalid size");
		return NULL;
	}
	for (gsize i = 0; i < bufsz; i += sizeof(guint16)) {
		guint16 idx = fu_memread_uint16(buf + i, G_LITTLE_ENDIAN);
		g_array_append_val(order, idx);
	}

	/* success */
	return g_steal_pointer(&order);
}

/**
 * fu_efivars_set_boot_order:
 * @self: a #FuEfivars
 * @order: (element-type guint16): boot order
 * @error: #GError
 *
 * Sets the index of the `BootNext` variable.
 *
 * Returns: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_efivars_set_boot_order(FuEfivars *self, GArray *order, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(order != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	for (guint i = 0; i < order->len; i++) {
		guint16 idx = g_array_index(order, guint16, i);
		fu_byte_array_append_uint16(buf, idx, G_LITTLE_ENDIAN);
	}
	return fu_efivars_set_data(self,
				   FU_EFIVARS_GUID_EFI_GLOBAL,
				   "BootOrder",
				   buf->data,
				   buf->len,
				   FU_EFIVARS_ATTR_NON_VOLATILE |
				       FU_EFIVARS_ATTR_BOOTSERVICE_ACCESS |
				       FU_EFIVARS_ATTR_RUNTIME_ACCESS,
				   error);
}

/**
 * fu_efivars_build_boot_order: (skip)
 **/
gboolean
fu_efivars_build_boot_order(FuEfivars *self, GError **error, ...)
{
	va_list args;
	g_autoptr(GArray) order = g_array_new(FALSE, FALSE, sizeof(guint16));

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	va_start(args, error);
	while (TRUE) {
		guint16 idx = va_arg(args, guint);
		if (idx == G_MAXUINT16)
			break;
		g_array_append_val(order, idx);
	}
	va_end(args);

	/* success */
	return fu_efivars_set_boot_order(self, order, error);
}

/**
 * fu_efivars_create_boot_entry_for_volume: (skip)
 **/
gboolean
fu_efivars_create_boot_entry_for_volume(FuEfivars *self,
					guint16 idx,
					FuVolume *volume,
					const gchar *name,
					const gchar *target,
					GError **error)
{
	g_autoptr(FuEfiDevicePathList) devpath_list = fu_efi_device_path_list_new();
	g_autoptr(FuEfiFilePathDevicePath) dp_fp = NULL;
	g_autoptr(FuEfiHardDriveDevicePath) dp_hdd = NULL;
	g_autoptr(FuEfiLoadOption) entry = fu_efi_load_option_new();
	g_autoptr(GFile) file = NULL;
	g_autofree gchar *mount_point = NULL;

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(FU_IS_VOLUME(volume), FALSE);
	g_return_val_if_fail(name != NULL, FALSE);
	g_return_val_if_fail(target != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* create plausible EFI file if not already exists */
	mount_point = fu_volume_get_mount_point(volume);
	if (mount_point == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "volume has no mount point");
		return FALSE;
	}
	file = g_file_new_build_filename(mount_point, target, NULL);
	if (!g_file_query_exists(file, NULL)) {
		g_autoptr(FuFirmware) img_text = fu_firmware_new();
		g_autoptr(FuFirmware) pefile = fu_pefile_firmware_new();
		g_autoptr(GBytes) img_blob = g_bytes_new_static("hello", 5);
		fu_firmware_set_id(img_text, ".text");
		fu_firmware_set_bytes(img_text, img_blob);
		fu_firmware_add_image(pefile, img_text);
		if (!fu_firmware_write_file(pefile, file, error))
			return FALSE;
	}

	dp_hdd = fu_efi_hard_drive_device_path_new_from_volume(volume, error);
	if (dp_hdd == NULL)
		return FALSE;
	dp_fp = fu_efi_file_path_device_path_new();
	if (!fu_efi_file_path_device_path_set_name(dp_fp, target, error))
		return FALSE;
	fu_firmware_add_image(FU_FIRMWARE(devpath_list), FU_FIRMWARE(dp_hdd));
	fu_firmware_add_image(FU_FIRMWARE(devpath_list), FU_FIRMWARE(dp_fp));

	fu_firmware_set_id(FU_FIRMWARE(entry), name);
	fu_firmware_add_image(FU_FIRMWARE(entry), FU_FIRMWARE(devpath_list));
	return fu_efivars_set_boot_entry(self, idx, entry, error);
}

/**
 * fu_efivars_get_boot_data:
 * @self: a #FuEfivars
 * @idx: boot index, typically 0x0001
 * @error: #GError
 *
 * Gets the raw data of the `BootXXXX` variable.
 *
 * Returns: (transfer full): boot data
 *
 * Since: 2.0.0
 **/
GBytes *
fu_efivars_get_boot_data(FuEfivars *self, guint16 idx, GError **error)
{
	g_autofree gchar *name = g_strdup_printf("Boot%04X", idx);
	g_return_val_if_fail(FU_IS_EFIVARS(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return fu_efivars_get_data_bytes(self, FU_EFIVARS_GUID_EFI_GLOBAL, name, NULL, error);
}

/**
 * fu_efivars_set_boot_data:
 * @self: a #FuEfivars
 * @idx: boot index, typically 0x0001
 * @blob: #GBytes
 * @error: #GError
 *
 * Sets the raw data of the `BootXXXX` variable.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.0
 **/
gboolean
fu_efivars_set_boot_data(FuEfivars *self, guint16 idx, GBytes *blob, GError **error)
{
	g_autofree gchar *name = g_strdup_printf("Boot%04X", idx);
	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(blob != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return fu_efivars_set_data_bytes(self,
					 FU_EFIVARS_GUID_EFI_GLOBAL,
					 name,
					 blob,
					 FU_EFIVARS_ATTR_NON_VOLATILE |
					     FU_EFIVARS_ATTR_BOOTSERVICE_ACCESS |
					     FU_EFIVARS_ATTR_RUNTIME_ACCESS,
					 error);
}

/**
 * fu_efivars_get_boot_entry:
 * @self: a #FuEfivars
 * @idx: boot index, typically 0x0001
 * @error: #GError
 *
 * Gets the loadopt data of the `BootXXXX` variable.
 *
 * Returns: (transfer full): a #FuEfiLoadOption, or %NULL
 *
 * Since: 2.0.0
 **/
FuEfiLoadOption *
fu_efivars_get_boot_entry(FuEfivars *self, guint16 idx, GError **error)
{
	g_autofree gchar *name = g_strdup_printf("Boot%04X", idx);
	g_autoptr(FuEfiLoadOption) loadopt = fu_efi_load_option_new();
	g_autoptr(GBytes) blob = NULL;

	g_return_val_if_fail(FU_IS_EFIVARS(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* get data */
	blob = fu_efivars_get_data_bytes(self, FU_EFIVARS_GUID_EFI_GLOBAL, name, NULL, error);
	if (blob == NULL)
		return NULL;
	if (!fu_firmware_parse_bytes(FU_FIRMWARE(loadopt),
				     blob,
				     0x0,
				     FWUPD_INSTALL_FLAG_NONE,
				     error))
		return NULL;
	fu_firmware_set_idx(FU_FIRMWARE(loadopt), idx);
	return g_steal_pointer(&loadopt);
}

/**
 * fu_efivars_set_boot_entry:
 * @self: a #FuEfivars
 * @idx: boot index, typically 0x0001
 * @entry: a #FuEfiLoadOption
 * @error: #GError
 *
 * Sets the loadopt data of the `BootXXXX` variable.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.0
 **/
gboolean
fu_efivars_set_boot_entry(FuEfivars *self, guint16 idx, FuEfiLoadOption *entry, GError **error)
{
	g_autoptr(GBytes) blob = NULL;

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(FU_IS_EFI_LOAD_OPTION(entry), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	blob = fu_firmware_write(FU_FIRMWARE(entry), error);
	if (blob == NULL)
		return FALSE;
	return fu_efivars_set_boot_data(self, idx, blob, error);
}

/**
 * fu_efivars_get_boot_entries:
 * @self: a #FuEfivars
 * @error: #GError
 *
 * Gets the loadopt data for all the entries listed in `BootOrder`.
 *
 * Returns: (transfer full) (element-type FuEfiLoadOption): boot data
 *
 * Since: 2.0.0
 **/
GPtrArray *
fu_efivars_get_boot_entries(FuEfivars *self, GError **error)
{
	g_autoptr(GArray) order = NULL;
	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

	g_return_val_if_fail(FU_IS_EFIVARS(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	order = fu_efivars_get_boot_order(self, error);
	if (order == NULL)
		return NULL;
	for (guint i = 0; i < order->len; i++) {
		guint16 idx = g_array_index(order, guint16, i);
		g_autoptr(FuEfiLoadOption) loadopt = NULL;

		loadopt = fu_efivars_get_boot_entry(self, idx, error);
		if (loadopt == NULL) {
			g_prefix_error(error, "failed to load Boot%04X: ", i);
			return NULL;
		}
		g_ptr_array_add(array, g_steal_pointer(&loadopt));
	}

	/* success */
	return g_steal_pointer(&array);
}

static void
fu_efivars_init(FuEfivars *self)
{
}

static void
fu_efivars_class_init(FuEfivarsClass *klass)
{
}
