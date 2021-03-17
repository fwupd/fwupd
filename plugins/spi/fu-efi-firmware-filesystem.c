/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"
#include "fu-efi-firmware-file.h"
#include "fu-efi-firmware-filesystem.h"

/**
 * SECTION:fu-uefi-firmware-filesystem
 * @short_description: UEFI FFS
 *
 * An object that represents a UEFI FFS volume.
 *
 * See also: #FuFirmware
 */

G_DEFINE_TYPE (FuEfiFirmwareFilesystem, fu_efi_firmware_filesystem, FU_TYPE_FIRMWARE)

static gboolean
fu_efi_firmware_filesystem_parse (FuFirmware *firmware,
				  GBytes *fw,
				  guint64 addr_start,
				  guint64 addr_end,
				  FwupdInstallFlags flags,
				  GError **error)
{
	gsize offset = 0;
	gsize bufsz = 0x0;
	const guint8 *buf = g_bytes_get_data (fw, &bufsz);

	while (offset + 0x18 < bufsz) {
		g_autoptr(FuFirmware) img = fu_efi_firmware_file_new ();
		g_autoptr(GBytes) fw_tmp = NULL;
		gboolean is_freespace = TRUE;

		/* ignore free space */
		for (guint i = 0; i < 0x18; i++) {
			if (buf[offset + i] != 0xff) {
				is_freespace = FALSE;
				break;
			}
		}
		if (is_freespace)
			break;

		fw_tmp = fu_common_bytes_new_offset (fw, offset, bufsz - offset, error);
		if (fw_tmp == NULL)
			return FALSE;
		if (!fu_firmware_parse (img, fw_tmp, flags, error)) {
			g_prefix_error (error,
					"failed to parse EFI file at 0x%x: ",
					(guint) offset);
			return FALSE;
		}
		fu_firmware_set_offset (firmware, offset);
		fu_firmware_add_image (firmware, img);

		/* next! */
		offset += fu_firmware_get_size (img);
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_efi_firmware_filesystem_write (FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new ();
	g_autoptr(GPtrArray) images = fu_firmware_get_images (firmware);

	/* add each file */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index (images, i);
		g_autoptr(GBytes) blob = NULL;
		fu_firmware_set_offset (img, buf->len);
		blob = fu_firmware_write (img, error);
		if (blob == NULL)
			return NULL;
		fu_byte_array_append_bytes (buf, blob);
		fu_byte_array_align_up (buf, fu_firmware_get_alignment (firmware), 0xFF);
	}

	/* success */
	return g_byte_array_free_to_bytes (g_steal_pointer (&buf));
}

static void
fu_efi_firmware_filesystem_init (FuEfiFirmwareFilesystem *self)
{
	fu_firmware_set_alignment (FU_FIRMWARE (self), 3);
}

static void
fu_efi_firmware_filesystem_class_init (FuEfiFirmwareFilesystemClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_efi_firmware_filesystem_parse;
	klass_firmware->write = fu_efi_firmware_filesystem_write;
}

/**
 * fu_efi_firmware_filesystem_new:
 *
 * Creates a new #FuFirmware
 *
 * Since: 1.6.0
 **/
FuFirmware *
fu_efi_firmware_filesystem_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_EFI_FIRMWARE_FILESYSTEM, NULL));
}
