/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuFirmware"

#include "config.h"

#include <string.h>

#include "fu-common.h"
#include "fu-firmware-common.h"
#include "fu-srec-firmware.h"

struct _FuSrecFirmware {
	FuFirmware		 parent_instance;
};

G_DEFINE_TYPE (FuSrecFirmware, fu_srec_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_srec_firmware_parse (FuFirmware *firmware,
			GBytes *fw,
			guint64 addr_start,
			guint64 addr_end,
			FwupdInstallFlags flags,
			GError **error)
{
	const gchar *data;
	gboolean got_eof = FALSE;
	gboolean got_hdr = FALSE;
	gsize sz = 0;
	guint16 data_cnt = 0;
	guint32 addr32_last = 0;
	guint32 img_address = 0;
	g_auto(GStrv) lines = NULL;
	g_autoptr(FuFirmwareImage) img = fu_firmware_image_new (NULL);
	g_autoptr(GBytes) img_bytes = NULL;
	g_autoptr(GString) outbuf = g_string_new (NULL);

	/* parse records */
	data = g_bytes_get_data (fw, &sz);
	lines = fu_common_strnsplit (data, sz, "\n", -1);
	for (guint ln = 0; lines[ln] != NULL; ln++) {
		const gchar *line = lines[ln];
		gsize linesz;
		guint32 rec_addr32;
		guint8 addrsz = 0;		/* bytes */
		guint8 rec_count;		/* words */
		guint8 rec_kind;

		/* ignore blank lines */
		g_strdelimit (lines[ln], "\r", '\0');
		linesz = strlen (line);
		if (linesz == 0)
			continue;

		/* check starting token */
		if (line[0] != 'S') {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "invalid starting token, got '%c' at line %u",
				     line[0], ln);
			return FALSE;
		}

		/* check there's enough data for the smallest possible record */
		if (linesz < 10) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "record incomplete at line %u, length %u",
				     ln, (guint) linesz);
			return FALSE;
		}

		/* kind, count, address, (data), checksum, linefeed */
		rec_kind = line[1] - '0';
		rec_count = fu_firmware_strparse_uint8 (line + 2);
		if (rec_count * 2 != linesz - 4) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "count incomplete at line %u, "
				     "length %u, expected %u",
				     ln, (guint) linesz - 4, (guint) rec_count * 2);
			return FALSE;
		}

		/* checksum check */
		if ((flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
			guint8 rec_csum = 0;
			guint8 rec_csum_expected;
			for (guint8 i = 0; i < rec_count; i++)
				rec_csum += fu_firmware_strparse_uint8 (line + (i * 2) + 2);
			rec_csum ^= 0xff;
			rec_csum_expected = fu_firmware_strparse_uint8 (line + (rec_count * 2) + 2);
			if (rec_csum != rec_csum_expected) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "checksum incorrect line %u, "
					     "expected %02x, got %02x",
					     ln, rec_csum_expected, rec_csum);
				return FALSE;
			}
		}

		/* set each command settings */
		switch (rec_kind) {
		case 0:
			addrsz = 2;
			if (got_hdr) {
				g_set_error_literal (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INVALID_FILE,
						     "duplicate header record");
				return FALSE;
			}
			got_hdr = TRUE;
			break;
		case 1:
			addrsz = 2;
			break;
		case 2:
			addrsz = 3;
			break;
		case 3:
			addrsz = 4;
			break;
		case 5:
			addrsz = 2;
			got_eof = TRUE;
			break;
		case 6:
			addrsz = 3;
			break;
		case 7:
			addrsz = 4;
			got_eof = TRUE;
			break;
		case 8:
			addrsz = 3;
			got_eof = TRUE;
			break;
		case 9:
			addrsz = 2;
			got_eof = TRUE;
			break;
		default:
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "invalid srec record type S%c",
				     line[1]);
			return FALSE;
		}

		/* parse address */
		switch (addrsz) {
		case 2:
			rec_addr32 = fu_firmware_strparse_uint16 (line + 4);
			break;
		case 3:
			rec_addr32 = fu_firmware_strparse_uint24 (line + 4);
			break;
		case 4:
			rec_addr32 = fu_firmware_strparse_uint32 (line + 4);
			break;
		default:
			g_assert_not_reached ();
		}

		/* header */
		if (rec_kind == 0) {
			g_autoptr(GString) modname = g_string_new (NULL);
			if (rec_addr32 != 0x0) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "invalid header record address, got %04x",
					     rec_addr32);
				return FALSE;
			}

			/* could be anything, lets assume text */
			for (guint8 i = 4 + (addrsz * 2); i <= rec_count * 2; i += 2) {
				guint8 tmp = fu_firmware_strparse_uint8 (line + i);
				if (!g_ascii_isgraph (tmp))
					break;
				g_string_append_c (modname, tmp);
			}
			if (modname->len != 0)
				fu_firmware_image_set_id (img, modname->str);
			continue;
		}

		/* verify we got all records */
		if (rec_kind == 5) {
			if (rec_addr32 != data_cnt) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "count record was not valid, got 0x%02x expected 0x%02x",
					     (guint) rec_addr32, (guint) data_cnt);
				return FALSE;
			}
		}

		/* data */
		if (rec_kind == 1 || rec_kind == 2 || rec_kind == 3) {
			/* invalid */
			if (!got_hdr) {
				g_set_error_literal (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INVALID_FILE,
						     "missing header record");
				return FALSE;
			}
			/* does not make sense */
			if (rec_addr32 < addr32_last) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "invalid address 0x%x, last was 0x%x",
					     (guint) rec_addr32,
					     (guint) addr32_last);
				return FALSE;
			}
			if (rec_addr32 < addr_start) {
				g_debug ("ignoring data at 0x%x as before start address 0x%x",
					 (guint) rec_addr32, (guint) addr_start);
			} else {
				guint bytecnt = 0;
				guint32 len_hole = rec_addr32 - addr32_last;

				/* fill any holes, but only up to 1Mb to avoid a DoS */
				if (addr32_last > 0 && len_hole > 0x100000) {
					g_set_error (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INVALID_FILE,
						     "hole of 0x%x bytes too large to fill",
						     (guint) len_hole);
					return FALSE;
				}
				if (addr32_last > 0x0 && len_hole > 1) {
					g_debug ("filling address 0x%08x to 0x%08x",
						 addr32_last + 1, addr32_last + len_hole - 1);
					for (guint j = 0; j < len_hole; j++)
						g_string_append_c (outbuf, 0xff);
				}

				/* add data */
				for (guint8 i = 4 + (addrsz * 2); i <= rec_count * 2; i += 2) {
					guint8 tmp = fu_firmware_strparse_uint8 (line + i);
					g_string_append_c (outbuf, tmp);
					bytecnt++;
				}
				if (img_address == 0x0)
					img_address = rec_addr32;
				addr32_last = rec_addr32 + bytecnt;
			}
			data_cnt++;
		}
	}

	/* no EOF */
	if (!got_eof) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no EOF, perhaps truncated file");
		return FALSE;
	}

	/* add single image */
	img_bytes = g_bytes_new (outbuf->str, outbuf->len);
	fu_firmware_image_set_bytes (img, img_bytes);
	fu_firmware_image_set_addr (img, img_address);
	fu_firmware_add_image (firmware, img);
	return TRUE;
}

static void
fu_srec_firmware_init (FuSrecFirmware *self)
{
}

static void
fu_srec_firmware_class_init (FuSrecFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_srec_firmware_parse;
}

FuFirmware *
fu_srec_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_SREC_FIRMWARE, NULL));
}
