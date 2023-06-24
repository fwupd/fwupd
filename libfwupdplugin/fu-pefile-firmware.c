/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-bytes.h"
#include "fu-coswid-firmware.h"
#include "fu-dump.h"
#include "fu-mem.h"
#include "fu-pefile-firmware.h"
#include "fu-string.h"

/**
 * FuPefileFirmware:
 *
 * A PE file consists of a Microsoft MS-DOS stub, the PE signature, the COFF file header, and an
 * optional header, followed by section data.
 *
 * Documented:
 * https://learn.microsoft.com/en-gb/windows/win32/debug/pe-format
 */

G_DEFINE_TYPE(FuPefileFirmware, fu_pefile_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_pefile_firmware_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	guint16 magic = 0x0;

	if (!fu_memread_uint16_safe(g_bytes_get_data(fw, NULL),
				    g_bytes_get_size(fw),
				    offset,
				    &magic,
				    G_LITTLE_ENDIAN,
				    error)) {
		g_prefix_error(error, "failed to read magic: ");
		return FALSE;
	}
	if (magic != 0x5A4D) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid magic for file");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pefile_firmware_parse(FuFirmware *firmware,
			 GBytes *fw,
			 gsize offset,
			 FwupdInstallFlags flags,
			 GError **error)
{
	gsize bufsz = 0;
	gsize hdr_offset = offset;
	guint16 opt_hdrsz = 0x0;
	guint32 nr_sections = 0x0;
	guint32 offset_sig = 0x0;
	guint32 signature = 0x0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	/* we already checked the MS-DOS magic, check the signature now */
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    hdr_offset + 0x3C,
				    &offset_sig,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	hdr_offset += offset_sig;
	if (!fu_memread_uint32_safe(buf, bufsz, hdr_offset, &signature, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (signature != 0x4550) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid signature for file");
		return FALSE;
	}
	hdr_offset += 4;

	/* read number of sections */
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    hdr_offset + 0x02,
				    &nr_sections,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (nr_sections == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid number of sections");
		return FALSE;
	}

	/* read optional extra header size  */
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    hdr_offset + 0x10,
				    &opt_hdrsz,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	hdr_offset += 0x14 + opt_hdrsz;

	/* read out COFF header */
	for (guint idx = 0; idx < nr_sections; idx++) {
		guint32 sect_length = 0x0;
		guint32 sect_offset = 0x0;
		guint8 name[8] = {0x0};
		g_autofree gchar *sect_id = NULL;
		g_autoptr(FuFirmware) fw_sect = NULL;
		g_autoptr(GBytes) sect_blob = NULL;

		/* read the section name */
		if (!fu_memcpy_safe(name,
				    sizeof(name),
				    0x0,
				    buf,
				    bufsz,
				    hdr_offset,
				    sizeof(name),
				    error))
			return FALSE;
		sect_id = fu_strsafe((const gchar *)name, sizeof(name));
		if (sect_id == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "no section name");
			return FALSE;
		}

		/* create new firmware */
		if (g_strcmp0(sect_id, ".sbom") == 0) {
			fw_sect = fu_coswid_firmware_new();
		} else {
			fw_sect = fu_firmware_new();
		}
		fu_firmware_set_idx(fw_sect, idx);
		fu_firmware_set_id(fw_sect, sect_id);

		/* add data */
		if (!fu_memread_uint32_safe(buf,
					    bufsz,
					    hdr_offset + 0x08,
					    &sect_length,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		fu_firmware_set_size(fw_sect, sect_length);
		if (!fu_memread_uint32_safe(buf,
					    bufsz,
					    hdr_offset + 0x14,
					    &sect_offset,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		fu_firmware_set_offset(fw_sect, sect_offset);
		sect_blob = fu_bytes_new_offset(fw, sect_offset, sect_length, error);
		if (sect_blob == NULL) {
			g_prefix_error(error, "failed to get raw data for %s: ", sect_id);
			return FALSE;
		}
		if (!fu_firmware_parse(fw_sect, sect_blob, flags, error))
			return FALSE;
		if (!fu_firmware_add_image_full(firmware, fw_sect, error))
			return FALSE;

		/* next! */
		hdr_offset += 0x28;
	}

	/* success */
	return TRUE;
}

static void
fu_pefile_firmware_init(FuPefileFirmware *self)
{
	fu_firmware_set_images_max(FU_FIRMWARE(self), 100);
}

static void
fu_pefile_firmware_class_init(FuPefileFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_pefile_firmware_check_magic;
	klass_firmware->parse = fu_pefile_firmware_parse;
}

/**
 * fu_pefile_firmware_new:
 *
 * Creates a new #FuPefileFirmware
 *
 * Since: 1.8.10
 **/
FuFirmware *
fu_pefile_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_PEFILE_FIRMWARE, NULL));
}
