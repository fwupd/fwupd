/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-efi-firmware-file.h"
#include "fu-efi-firmware-filesystem.h"

/**
 * FuEfiFirmwareFilesystem:
 *
 * A UEFI filesystem.
 *
 * See also: [class@FuFirmware]
 */

G_DEFINE_TYPE(FuEfiFirmwareFilesystem, fu_efi_firmware_filesystem, FU_TYPE_FIRMWARE)

#define FU_EFI_FIRMWARE_FILESYSTEM_FILES_MAX 10000
#define FU_EFI_FIRMWARE_FILESYSTEM_SIZE_MAX  0x10000000 /* 256 MB */

static gboolean
fu_efi_firmware_filesystem_parse(FuFirmware *firmware,
				 GBytes *fw,
				 gsize offset,
				 FwupdInstallFlags flags,
				 GError **error)
{
	gsize bufsz = 0x0;
	guint files_max = FU_EFI_FIRMWARE_FILESYSTEM_FILES_MAX;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);

	/* if fuzzing, artificially limit the number of files to avoid using large amounts of RSS
	 * when printing the FuEfiFirmwareFilesystem XML output */
	if (g_getenv("FWUPD_FUZZER_RUNNING") != NULL)
		files_max = 50;

	while (offset + 0x18 < bufsz) {
		g_autoptr(FuFirmware) img = fu_efi_firmware_file_new();
		g_autoptr(GBytes) fw_tmp = NULL;
		gboolean is_freespace = TRUE;

		/* limit reached */
		if (imgs->len + 1 > files_max) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "too many file objects in the filesystem, limit was %u",
				    files_max);
			return FALSE;
		}

		/* ignore free space */
		for (guint i = 0; i < 0x18; i++) {
			if (buf[offset + i] != 0xff) {
				is_freespace = FALSE;
				break;
			}
		}
		if (is_freespace)
			break;

		fw_tmp = fu_bytes_new_offset(fw, offset, bufsz - offset, error);
		if (fw_tmp == NULL)
			return FALSE;
		if (!fu_firmware_parse(img, fw_tmp, flags | FWUPD_INSTALL_FLAG_NO_SEARCH, error)) {
			g_prefix_error(error, "failed to parse EFI file at 0x%x: ", (guint)offset);
			return FALSE;
		}
		fu_firmware_set_offset(firmware, offset);
		fu_firmware_add_image(firmware, img);

		/* next! */
		offset += fu_firmware_get_size(img);
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_efi_firmware_filesystem_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);

	/* sanity check */
	if (fu_firmware_get_alignment(firmware) > FU_FIRMWARE_ALIGNMENT_1M) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "alignment invalid, got 0x%02x",
			    fu_firmware_get_alignment(firmware));
		return NULL;
	}

	/* add each file */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autoptr(GBytes) blob = NULL;
		fu_firmware_set_offset(img, buf->len);
		blob = fu_firmware_write(img, error);
		if (blob == NULL)
			return NULL;
		fu_byte_array_append_bytes(buf, blob);
		fu_byte_array_align_up(buf, fu_firmware_get_alignment(firmware), 0xFF);

		/* sanity check */
		if (buf->len > FU_EFI_FIRMWARE_FILESYSTEM_SIZE_MAX) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "EFI filesystem too large, 0x%02x > 0x%02x",
				    (guint)buf->len,
				    (guint)FU_EFI_FIRMWARE_FILESYSTEM_SIZE_MAX);
			return NULL;
		}
	}

	/* success */
	return g_steal_pointer(&buf);
}

static void
fu_efi_firmware_filesystem_init(FuEfiFirmwareFilesystem *self)
{
	fu_firmware_set_alignment(FU_FIRMWARE(self), FU_FIRMWARE_ALIGNMENT_8);
}

static void
fu_efi_firmware_filesystem_class_init(FuEfiFirmwareFilesystemClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_efi_firmware_filesystem_parse;
	klass_firmware->write = fu_efi_firmware_filesystem_write;
}

/**
 * fu_efi_firmware_filesystem_new:
 *
 * Creates a new #FuFirmware
 *
 * Since: 1.6.2
 **/
FuFirmware *
fu_efi_firmware_filesystem_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_EFI_FIRMWARE_FILESYSTEM, NULL));
}
