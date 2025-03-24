/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <lzma.h>

#include "fu-lzma-common.h"

/**
 * fu_lzma_decompress_bytes:
 * @blob: data
 * @memlimit: decompression memory limit, in bytes
 * @error: (nullable): optional return location for an error
 *
 * Decompresses a LZMA stream.
 *
 * Returns: (transfer full): decompressed data
 *
 * Since: 2.0.7
 **/
GBytes *
fu_lzma_decompress_bytes(GBytes *blob, guint64 memlimit, GError **error)
{
	const gsize tmpbufsz = 0x20000;
	lzma_ret rc;
	lzma_stream strm = LZMA_STREAM_INIT;
	g_autofree guint8 *tmpbuf = g_malloc0(tmpbufsz);
	g_autoptr(GByteArray) buf = g_byte_array_new();

	strm.next_in = g_bytes_get_data(blob, NULL);
	strm.avail_in = g_bytes_get_size(blob);

	rc = lzma_auto_decoder(&strm, memlimit, LZMA_TELL_UNSUPPORTED_CHECK);
	if (rc != LZMA_OK) {
		lzma_end(&strm);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
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
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to decode LZMA data rc=%u",
			    rc);
		return NULL;
	}
	return g_bytes_new(buf->data, buf->len);
}

/**
 * fu_lzma_compress_bytes:
 * @blob: data
 * @error: (nullable): optional return location for an error
 *
 * Compresses into a LZMA stream.
 *
 * Returns: (transfer full): compressed data
 *
 * Since: 1.9.8
 **/
GBytes *
fu_lzma_compress_bytes(GBytes *blob, GError **error)
{
	const gsize tmpbufsz = 0x20000;
	lzma_ret rc;
	lzma_stream strm = LZMA_STREAM_INIT;
	g_autofree guint8 *tmpbuf = g_malloc0(tmpbufsz);
	g_autoptr(GByteArray) buf = g_byte_array_new();

	strm.next_in = g_bytes_get_data(blob, NULL);
	strm.avail_in = g_bytes_get_size(blob);

	rc = lzma_easy_encoder(&strm, 9, LZMA_CHECK_CRC64);
	if (rc != LZMA_OK) {
		lzma_end(&strm);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to set up LZMA encoder rc=%u",
			    rc);
		return NULL;
	}
	do {
		strm.next_out = tmpbuf;
		strm.avail_out = tmpbufsz;
		rc = lzma_code(&strm, LZMA_FINISH);
		if (rc != LZMA_OK && rc != LZMA_STREAM_END)
			break;
		g_byte_array_append(buf, tmpbuf, tmpbufsz - strm.avail_out);
	} while (rc == LZMA_OK);
	lzma_end(&strm);

	/* success */
	if (rc != LZMA_OK && rc != LZMA_STREAM_END) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to encode LZMA data rc=%u",
			    rc);
		return NULL;
	}
	return g_bytes_new(buf->data, buf->len);
}
