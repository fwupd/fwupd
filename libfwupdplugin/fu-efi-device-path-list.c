/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuEfiDevicePath"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-efi-device-path-list.h"
#include "fu-efi-file-path-device-path.h"
#include "fu-efi-hard-drive-device-path.h"
#include "fu-efi-struct.h"
#include "fu-input-stream.h"

struct _FuEfiDevicePathList {
	FuFirmware parent_instance;
};

static void
fu_efi_device_path_list_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FuEfiDevicePathList,
		       fu_efi_device_path_list,
		       FU_TYPE_FIRMWARE,
		       0,
		       G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC,
					     fu_efi_device_path_list_codec_iface_init))

#define FU_EFI_DEVICE_PATH_LIST_IMAGES_MAX 1000u

static const gchar *
fu_efi_device_path_list_gtype_to_member_name(GType gtype)
{
	if (gtype == FU_TYPE_EFI_DEVICE_PATH)
		return "Dp";
	if (gtype == FU_TYPE_EFI_FILE_PATH_DEVICE_PATH)
		return "Fp";
	if (gtype == FU_TYPE_EFI_HARD_DRIVE_DEVICE_PATH)
		return "Hd";
	return g_type_name(gtype);
}

static void
fu_efi_device_path_list_add_json(FwupdCodec *codec, JsonBuilder *builder, FwupdCodecFlags flags)
{
	FuFirmware *firmware = FU_FIRMWARE(codec);
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);

	json_builder_set_member_name(builder, "DPs");
	json_builder_begin_array(builder);
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		json_builder_begin_object(builder);
		json_builder_set_member_name(
		    builder,
		    fu_efi_device_path_list_gtype_to_member_name(G_OBJECT_TYPE(img)));
		json_builder_begin_object(builder);
		fwupd_codec_to_json(FWUPD_CODEC(img), builder, flags);
		json_builder_end_object(builder);
		json_builder_end_object(builder);
	}
	json_builder_end_array(builder);
}

static gboolean
fu_efi_device_path_list_parse(FuFirmware *firmware,
			      GInputStream *stream,
			      FwupdInstallFlags flags,
			      GError **error)
{
	gsize offset = 0;
	gsize streamsz = 0;
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	while (offset < streamsz) {
		g_autoptr(FuEfiDevicePath) efi_dp = NULL;
		g_autoptr(GByteArray) st_dp = NULL;

		/* parse the header so we can work out what GType to create */
		st_dp = fu_struct_efi_device_path_parse_stream(stream, offset, error);
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
		if (!fu_firmware_parse_stream(FU_FIRMWARE(efi_dp), stream, offset, flags, error))
			return FALSE;
		if (!fu_firmware_add_image_full(firmware, FU_FIRMWARE(efi_dp), error))
			return FALSE;
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
fu_efi_device_path_list_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_json = fu_efi_device_path_list_add_json;
}

static void
fu_efi_device_path_list_class_init(FuEfiDevicePathListClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_efi_device_path_list_parse;
	firmware_class->write = fu_efi_device_path_list_write;
}

static void
fu_efi_device_path_list_init(FuEfiDevicePathList *self)
{
	g_type_ensure(FU_TYPE_EFI_FILE_PATH_DEVICE_PATH);
	g_type_ensure(FU_TYPE_EFI_HARD_DRIVE_DEVICE_PATH);
	fu_firmware_set_images_max(FU_FIRMWARE(self), FU_EFI_DEVICE_PATH_LIST_IMAGES_MAX);
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
