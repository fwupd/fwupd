/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-efi-file.h"
#include "fu-efi-filesystem.h"
#include "fu-input-stream.h"
#include "fu-partial-input-stream.h"

/**
 * FuEfiFilesystem:
 *
 * A UEFI filesystem.
 *
 * See also: [class@FuFirmware]
 */

G_DEFINE_TYPE(FuEfiFilesystem, fu_efi_filesystem, FU_TYPE_FIRMWARE)

#define FU_EFI_FILESYSTEM_FILES_MAX 10000
#define FU_EFI_FILESYSTEM_SIZE_MAX  0x10000000 /* 256 MB */

static gboolean
fu_efi_filesystem_parse(FuFirmware *firmware,
			GInputStream *stream,
			FwupdInstallFlags flags,
			GError **error)
{
	gsize offset = 0;
	gsize streamsz = 0;
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	while (offset < streamsz) {
		g_autoptr(FuFirmware) img = fu_efi_file_new();
		g_autoptr(GInputStream) stream_tmp = NULL;
		gboolean is_freespace = TRUE;

		/* ignore free space */
		for (guint i = 0; i < 0x18; i++) {
			guint8 tmp = 0;
			if (!fu_input_stream_read_u8(stream, offset + i, &tmp, error))
				return FALSE;
			if (tmp != 0xff) {
				is_freespace = FALSE;
				break;
			}
		}
		if (is_freespace) {
			g_debug("ignoring free space @0x%x of 0x%x",
				(guint)offset,
				(guint)streamsz);
			break;
		}
		stream_tmp = fu_partial_input_stream_new(stream, offset, streamsz - offset, error);
		if (stream_tmp == NULL)
			return FALSE;
		if (!fu_firmware_parse_stream(img,
					      stream_tmp,
					      0x0,
					      flags | FWUPD_INSTALL_FLAG_NO_SEARCH,
					      error)) {
			g_prefix_error(error, "failed to parse EFI file at 0x%x: ", (guint)offset);
			return FALSE;
		}
		fu_firmware_set_offset(firmware, offset);
		if (!fu_firmware_add_image_full(firmware, img, error))
			return FALSE;

		/* next! */
		offset += fu_firmware_get_size(img);
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_efi_filesystem_write(FuFirmware *firmware, GError **error)
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
		if (buf->len > FU_EFI_FILESYSTEM_SIZE_MAX) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "EFI filesystem too large, 0x%02x > 0x%02x",
				    (guint)buf->len,
				    (guint)FU_EFI_FILESYSTEM_SIZE_MAX);
			return NULL;
		}
	}

	/* success */
	return g_steal_pointer(&buf);
}

static void
fu_efi_filesystem_init(FuEfiFilesystem *self)
{
	/* if fuzzing, artificially limit the number of files to avoid using large amounts of RSS
	 * when printing the FuEfiFilesystem XML output */
	fu_firmware_set_images_max(
	    FU_FIRMWARE(self),
	    g_getenv("FWUPD_FUZZER_RUNNING") == NULL ? FU_EFI_FILESYSTEM_FILES_MAX : 50);
	fu_firmware_set_alignment(FU_FIRMWARE(self), FU_FIRMWARE_ALIGNMENT_8);
	g_type_ensure(FU_TYPE_EFI_FILE);
}

static void
fu_efi_filesystem_class_init(FuEfiFilesystemClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_efi_filesystem_parse;
	firmware_class->write = fu_efi_filesystem_write;
}

/**
 * fu_efi_filesystem_new:
 *
 * Creates a new #FuFirmware
 *
 * Since: 2.0.0
 **/
FuFirmware *
fu_efi_filesystem_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_EFI_FILESYSTEM, NULL));
}
