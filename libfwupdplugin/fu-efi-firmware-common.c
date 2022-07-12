/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#ifdef HAVE_LZMA
#include <lzma.h>
#endif

#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-efi-firmware-common.h"
#include "fu-efi-firmware-section.h"

/**
 * fu_efi_firmware_parse_sections:
 * @firmware: #FuFirmware
 * @fw: data
 * @flags: flags
 * @error: (nullable): optional return location for an error
 *
 * Parses a UEFI section.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.6.2
 **/
gboolean
fu_efi_firmware_parse_sections(FuFirmware *firmware,
			       GBytes *fw,
			       FwupdInstallFlags flags,
			       GError **error)
{
	gsize offset = 0;
	gsize bufsz = g_bytes_get_size(fw);

	while (offset < bufsz) {
		g_autoptr(FuFirmware) img = fu_efi_firmware_section_new();
		g_autoptr(GBytes) blob = NULL;

		/* maximum payload */
		blob = fu_bytes_new_offset(fw, offset, bufsz - offset, error);
		if (blob == NULL)
			return FALSE;

		/* parse section */
		if (!fu_firmware_parse(img, blob, flags | FWUPD_INSTALL_FLAG_NO_SEARCH, error))
			return FALSE;
		fu_firmware_set_offset(img, offset);
		fu_firmware_add_image(firmware, img);

		/* next! */
		offset += fu_firmware_get_size(img);
	}
	if (offset != g_bytes_get_size(fw)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "EFI sections overflow 0x%x of 0x%x",
			    (guint)offset,
			    (guint)g_bytes_get_size(fw));
		return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * fu_efi_firmware_decompress_lzma:
 * @blob: data
 * @error: (nullable): optional return location for an error
 *
 * Decompresses a LZMA stream.
 *
 * Returns: decompressed data
 *
 * Since: 1.6.2
 **/
GBytes *
fu_efi_firmware_decompress_lzma(GBytes *blob, GError **error)
{
#ifdef HAVE_LZMA
	const gsize tmpbufsz = 0x20000;
	lzma_ret rc;
	lzma_stream strm = LZMA_STREAM_INIT;
	uint64_t memlimit = G_MAXUINT32;
	g_autofree guint8 *tmpbuf = g_malloc0(tmpbufsz);
	g_autoptr(GByteArray) buf = g_byte_array_new();

	strm.next_in = g_bytes_get_data(blob, NULL);
	strm.avail_in = g_bytes_get_size(blob);

	rc = lzma_auto_decoder(&strm, memlimit, LZMA_TELL_UNSUPPORTED_CHECK);
	if (rc != LZMA_OK) {
		lzma_end(&strm);
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "failed to set up LZMA decoder rc=%u",
			    rc);
		return NULL;
	}
	do {
		strm.next_out = tmpbuf;
		strm.avail_out = tmpbufsz;
		rc = lzma_code(&strm, LZMA_RUN);
		if (rc != LZMA_OK && rc != LZMA_STREAM_END)
			break;
		g_byte_array_append(buf, tmpbuf, tmpbufsz - strm.avail_out);
	} while (rc == LZMA_OK);
	lzma_end(&strm);

	/* success */
	if (rc != LZMA_OK && rc != LZMA_STREAM_END) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "failed to decode LZMA data rc=%u",
			    rc);
		return NULL;
	}
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
#else
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "missing lzma support");
	return NULL;
#endif
}
