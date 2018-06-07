/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "dfu-element.h"
#include "dfu-format-dfu.h"
#include "dfu-format-metadata.h"
#include "dfu-format-dfuse.h"
#include "dfu-format-raw.h"
#include "dfu-image.h"

#include "fwupd-error.h"

typedef struct __attribute__((packed)) {
	guint16		release;
	guint16		pid;
	guint16		vid;
	guint16		ver;
	guint8		sig[3];
	guint8		len;
	guint32		crc;
} DfuFirmwareFooter;

/**
 * dfu_firmware_detect_dfu: (skip)
 * @bytes: data to parse
 *
 * Attempts to sniff the data and work out the firmware format
 *
 * Returns: a #DfuFirmwareFormat, e.g. %DFU_FIRMWARE_FORMAT_RAW
 **/
DfuFirmwareFormat
dfu_firmware_detect_dfu (GBytes *bytes)
{
	DfuFirmwareFooter *ftr;
	guint8 *data;
	gsize len;

	/* check data size */
	data = (guint8 *) g_bytes_get_data (bytes, &len);
	if (len < 16)
		return DFU_FIRMWARE_FORMAT_UNKNOWN;

	/* check for DFU signature */
	ftr = (DfuFirmwareFooter *) &data[len - sizeof(DfuFirmwareFooter)];
	if (memcmp (ftr->sig, "UFD", 3) != 0)
		return DFU_FIRMWARE_FORMAT_UNKNOWN;

	/* check versions */
	switch (GUINT16_FROM_LE (ftr->ver)) {
	case DFU_VERSION_DFU_1_0:
	case DFU_VERSION_DFU_1_1:
		return DFU_FIRMWARE_FORMAT_DFU;
	case DFU_VERSION_DFUSE:
		return DFU_FIRMWARE_FORMAT_DFUSE;
	default:
		break;
	}
	return DFU_FIRMWARE_FORMAT_UNKNOWN;
}

static guint32 _crctbl[] = {
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
	0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
	0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
	0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
	0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
	0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
	0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
	0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
	0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
	0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
	0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
	0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
	0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
	0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
	0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
	0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
	0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
	0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
	0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
	0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
	0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
	0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
	0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
	0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
	0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
	0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
	0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
	0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
	0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
	0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
	0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
	0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
	0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
	0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
	0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
	0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
	0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
	0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
	0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
	0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
	0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
	0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
	0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d };

static guint32
dfu_firmware_generate_crc32 (const guint8 *data, gsize length)
{
	guint32 accum = 0xffffffff;
	for (guint i = 0; i < length; i++)
		accum = _crctbl[(accum^data[i]) & 0xff] ^ (accum >> 8);
	return accum;
}

/**
 * dfu_firmware_from_dfu: (skip)
 * @firmware: a #DfuFirmware
 * @bytes: data to parse
 * @flags: some #DfuFirmwareParseFlags
 * @error: a #GError, or %NULL
 *
 * Unpacks into a firmware object from dfu data.
 *
 * Returns: %TRUE for success
 **/
gboolean
dfu_firmware_from_dfu (DfuFirmware *firmware,
		       GBytes *bytes,
		       DfuFirmwareParseFlags flags,
		       GError **error)
{
	DfuFirmwareFooter *ftr;
	const gchar *cipher_str;
	gsize len;
	guint32 crc;
	guint32 crc_new;
	guint8 *data;
	g_autoptr(GBytes) contents = NULL;

	/* check data size */
	data = (guint8 *) g_bytes_get_data (bytes, &len);
	if (len < 16) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "size check failed, too small");
		return FALSE;
	}

	/* check for DFU signature */
	ftr = (DfuFirmwareFooter *) &data[len - sizeof(DfuFirmwareFooter)];
	if (memcmp (ftr->sig, "UFD", 3) != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "no DFU signature");
		return FALSE;
	}

	/* check version */
	if ((flags & DFU_FIRMWARE_PARSE_FLAG_NO_VERSION_TEST) == 0) {
		if (dfu_firmware_get_format (firmware) != DFU_FIRMWARE_FORMAT_DFU &&
		    dfu_firmware_get_format (firmware) != DFU_FIRMWARE_FORMAT_DFUSE) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "version check failed, got %04x",
				     dfu_firmware_get_format (firmware));
			return FALSE;
		}
	}

	/* verify the checksum */
	crc = GUINT32_FROM_LE (ftr->crc);
	if ((flags & DFU_FIRMWARE_PARSE_FLAG_NO_CRC_TEST) == 0) {
		crc_new = dfu_firmware_generate_crc32 (data, len - 4);
		if (crc != crc_new) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "CRC failed, expected %04x, got %04x",
				     crc_new, GUINT32_FROM_LE (ftr->crc));
			return FALSE;
		}
	}

	/* set from footer */
	dfu_firmware_set_vid (firmware, GUINT16_FROM_LE (ftr->vid));
	dfu_firmware_set_pid (firmware, GUINT16_FROM_LE (ftr->pid));
	dfu_firmware_set_release (firmware, GUINT16_FROM_LE (ftr->release));

	/* check reported length */
	if (ftr->len > len) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "reported firmware size %04x larger than file %04x",
			     (guint) ftr->len, (guint) len);
		return FALSE;
	}

	/* parse the optional metadata segment */
	if ((flags & DFU_FIRMWARE_PARSE_FLAG_NO_METADATA) == 0) {
		gsize offset = len - ftr->len;
		g_autoptr(GBytes) md = g_bytes_new (&data[offset], ftr->len);
		if (!dfu_firmware_from_metadata (firmware, md, flags, error))
			return FALSE;
	}

	/* set this automatically */
	cipher_str = dfu_firmware_get_metadata (firmware, DFU_METADATA_KEY_CIPHER_KIND);
	if (cipher_str != NULL) {
		if (g_strcmp0 (cipher_str, "XTEA") == 0)
			dfu_firmware_set_cipher_kind (firmware, DFU_CIPHER_KIND_XTEA);
		else
			g_warning ("Unknown CipherKind: %s", cipher_str);
	}

	/* parse DfuSe prefix */
	contents = g_bytes_new_from_bytes (bytes, 0, len - ftr->len);
	if (dfu_firmware_get_format (firmware) == DFU_FIRMWARE_FORMAT_DFUSE)
		return dfu_firmware_from_dfuse (firmware, contents, flags, error);

	/* just copy old-plain DFU file */
	return dfu_firmware_from_raw (firmware, contents, flags, error);
}

static DfuVersion
dfu_convert_version (DfuFirmwareFormat format)
{
	if (format == DFU_FIRMWARE_FORMAT_DFU)
		return DFU_VERSION_DFU_1_0;
	if (format == DFU_FIRMWARE_FORMAT_DFUSE)
		return DFU_VERSION_DFUSE;
	return DFU_VERSION_UNKNOWN;
}

static GBytes *
dfu_firmware_add_footer (DfuFirmware *firmware, GBytes *contents, GError **error)
{
	DfuFirmwareFooter *ftr;
	const guint8 *data_bin;
	const guint8 *data_md;
	gsize length_bin = 0;
	gsize length_md = 0;
	guint32 crc_new;
	guint8 *buf;
	g_autoptr(GBytes) metadata_table = NULL;

	/* get any file metadata */
	metadata_table = dfu_firmware_to_metadata (firmware, error);
	if (metadata_table == NULL)
		return NULL;
	data_md = g_bytes_get_data (metadata_table, &length_md);

	/* add the raw firmware data */
	data_bin = g_bytes_get_data (contents, &length_bin);
	buf = g_malloc0 (length_bin + length_md + 0x10);
	memcpy (buf + 0, data_bin, length_bin);

	/* add the metadata table */
	memcpy (buf + length_bin, data_md, length_md);

	/* set up LE footer */
	ftr = (DfuFirmwareFooter *) (buf + length_bin + length_md);
	ftr->release = GUINT16_TO_LE (dfu_firmware_get_release (firmware));
	ftr->pid = GUINT16_TO_LE (dfu_firmware_get_pid (firmware));
	ftr->vid = GUINT16_TO_LE (dfu_firmware_get_vid (firmware));
	ftr->ver = GUINT16_TO_LE (dfu_convert_version (dfu_firmware_get_format (firmware)));
	ftr->len = (guint8) (sizeof (DfuFirmwareFooter) + length_md);
	memcpy(ftr->sig, "UFD", 3);
	crc_new = dfu_firmware_generate_crc32 (buf, length_bin + length_md + 12);
	ftr->crc = GUINT32_TO_LE (crc_new);

	/* return all data */
	return g_bytes_new_take (buf, length_bin + length_md + 0x10);
}

/**
 * dfu_firmware_to_dfu: (skip)
 * @firmware: a #DfuFirmware
 * @error: a #GError, or %NULL
 *
 * Packs dfu firmware
 *
 * Returns: (transfer full): the packed data
 **/
GBytes *
dfu_firmware_to_dfu (DfuFirmware *firmware, GError **error)
{
	/* plain DFU */
	if (dfu_firmware_get_format (firmware) == DFU_FIRMWARE_FORMAT_DFU) {
		GBytes *contents;
		DfuElement *element;
		DfuImage *image;
		image = dfu_firmware_get_image_default (firmware);
		g_assert (image != NULL);
		element = dfu_image_get_element (image, 0);
		if (element == NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "no firmware element data to write");
			return NULL;
		}
		contents = dfu_element_get_contents (element);
		return dfu_firmware_add_footer (firmware, contents, error);
	}

	/* DfuSe */
	if (dfu_firmware_get_format (firmware) == DFU_FIRMWARE_FORMAT_DFUSE) {
		g_autoptr(GBytes) contents = NULL;
		contents = dfu_firmware_to_dfuse (firmware, error);
		if (contents == NULL)
			return NULL;
		return dfu_firmware_add_footer (firmware, contents, error);
	}

	g_assert_not_reached ();
	return NULL;
}
