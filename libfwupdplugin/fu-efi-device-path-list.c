/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuEfiDevicePath"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-efi-device-path-list.h"
#include "fu-efi-file-path-device-path.h"
#include "fu-efi-hard-drive-device-path.h"
#include "fu-efi-struct.h"

struct _FuEfiDevicePathList {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuEfiDevicePathList, fu_efi_device_path_list, FU_TYPE_FIRMWARE)

#define FU_EFI_DEVICE_PATH_MAX_CHILDREN 1000u

static gboolean
fu_efi_device_path_list_parse(FuFirmware *firmware,
			      GBytes *fw,
			      gsize offset,
			      FwupdInstallFlags flags,
			      GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	while (offset < g_bytes_get_size(fw)) {
		g_autoptr(FuEfiDevicePath) efi_dp = NULL;
		g_autoptr(GByteArray) st_dp = NULL;
		g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);

		/* sanity check */
		if (imgs->len > FU_EFI_DEVICE_PATH_MAX_CHILDREN) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "invalid DEVICE_PATH count, limit is %u",
				    FU_EFI_DEVICE_PATH_MAX_CHILDREN);
			return FALSE;
		}

		/* parse the header so we can work out what GType to create */
		st_dp = fu_struct_efi_device_path_parse(buf, bufsz, offset, error);
		if (st_dp == NULL)
			return FALSE;
		if (fu_struct_efi_device_path_get_type(st_dp) == FU_EFI_DEVICE_PATH_TYPE_END)
			break;
		if (fu_struct_efi_device_path_get_type(st_dp) == FU_EFI_DEVICE_PATH_TYPE_MEDIA &&
		    fu_struct_efi_device_path_get_subtype(st_dp) ==
			FU_EFI_HARD_DRIVE_DEVICE_PATH_SUBTYPE_FILE_PATH) {
			efi_dp = FU_EFI_DEVICE_PATH(fu_efi_file_path_device_path_new());
		} else if (fu_struct_efi_device_path_get_type(st_dp) ==
			       FU_EFI_DEVICE_PATH_TYPE_MEDIA &&
			   fu_struct_efi_device_path_get_subtype(st_dp) ==
			       FU_EFI_HARD_DRIVE_DEVICE_PATH_SUBTYPE_HARD_DRIVE) {
			efi_dp = FU_EFI_DEVICE_PATH(fu_efi_hard_drive_device_path_new());
		} else {
			efi_dp = fu_efi_device_path_new();
		}
		fu_firmware_set_offset(FU_FIRMWARE(efi_dp), offset);
		if (!fu_firmware_parse_full(FU_FIRMWARE(efi_dp), fw, offset, flags, error))
			return FALSE;
		fu_firmware_add_image(firmware, FU_FIRMWARE(efi_dp));
		offset += fu_firmware_get_size(FU_FIRMWARE(efi_dp));
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_efi_device_path_list_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GByteArray) st_dp_end = NULL;

	/* add each image */
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		g_autoptr(GBytes) dp_blob = fu_firmware_write(img, error);
		if (dp_blob == NULL)
			return NULL;
		fu_byte_array_append_bytes(buf, dp_blob);
	}

	/* add end marker */
	st_dp_end = fu_struct_efi_device_path_new();
	fu_struct_efi_device_path_set_type(st_dp_end, FU_EFI_DEVICE_PATH_TYPE_END);
	fu_struct_efi_device_path_set_subtype(st_dp_end, 0xFF);
	g_byte_array_append(buf, st_dp_end->data, st_dp_end->len);

	/* success */
	return g_steal_pointer(&buf);
}

static void
fu_efi_device_path_list_class_init(FuEfiDevicePathListClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_efi_device_path_list_parse;
	klass_firmware->write = fu_efi_device_path_list_write;
}

static void
fu_efi_device_path_list_init(FuEfiDevicePathList *self)
{
	g_type_ensure(FU_TYPE_EFI_FILE_PATH_DEVICE_PATH);
	g_type_ensure(FU_TYPE_EFI_HARD_DRIVE_DEVICE_PATH);
}

/**
 * fu_efi_device_path_list_new:
 *
 * Creates a new EFI DEVICE_PATH list.
 *
 * Returns: (transfer full): a #FuEfiDevicePathList
 *
 * Since: 1.9.3
 **/
FuEfiDevicePathList *
fu_efi_device_path_list_new(void)
{
	return g_object_new(FU_TYPE_EFI_DEVICE_PATH_LIST, NULL);
}
