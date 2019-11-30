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

/**
 * SECTION:fu-srec-firmware
 * @short_description: SREC firmware image
 *
 * An object that represents a SREC firmware image.
 *
 * See also: #FuFirmware
 */

struct _FuSrecFirmware {
	FuFirmware		 parent_instance;
	GPtrArray		*records;
};

G_DEFINE_TYPE (FuSrecFirmware, fu_srec_firmware, FU_TYPE_FIRMWARE)

/**
 * fu_srec_firmware_get_records:
 * @self: A #FuSrecFirmware
 *
 * Returns the raw records from SREC tokenization.
 *
 * This might be useful if the plugin is expecting the SREC file to be a list
 * of operations, rather than a simple linear image with filled holes.
 *
 * Returns: (transfer none) (element-type FuSrecFirmwareRecord): records
 *
 * Since: 1.3.2
 **/
GPtrArray *
fu_srec_firmware_get_records (FuSrecFirmware *self)
{
	g_return_val_if_fail (FU_IS_SREC_FIRMWARE (self), NULL);
	return self->records;
}

static void
fu_srec_firmware_record_free (FuSrecFirmwareRecord *rcd)
{
	g_byte_array_unref (rcd->buf);
	g_free (rcd);
}

/**
 * fu_srec_firmware_record_new: (skip):
 * @ln: unsigned integer
 * @kind: #FuFirmwareSrecRecordKind
 * @addr: unsigned integer
 *
 * Returns a single firmware record
 *
 * Returns: (transfer full) (element-type FuSrecFirmwareRecord): records
 *
 * Since: 1.3.2
 **/
FuSrecFirmwareRecord *
fu_srec_firmware_record_new (guint ln, FuFirmareSrecRecordKind kind, guint32 addr)
{
	FuSrecFirmwareRecord *rcd = g_new0 (FuSrecFirmwareRecord, 1);
	rcd->ln = ln;
	rcd->kind = kind;
	rcd->addr = addr;
	rcd->buf = g_byte_array_new ();
	return rcd;
}

static gboolean
fu_srec_firmware_tokenize (FuFirmware *firmware, GBytes *fw,
			   FwupdInstallFlags flags, GError **error)
{
	FuSrecFirmware *self = FU_SREC_FIRMWARE (firmware);
	const gchar *data;
	gboolean got_eof = FALSE;
	gsize sz = 0;
	g_auto(GStrv) lines = NULL;

	/* parse records */
	data = g_bytes_get_data (fw, &sz);
	lines = fu_common_strnsplit (data, sz, "\n", -1);
	for (guint ln = 0; lines[ln] != NULL; ln++) {
		FuSrecFirmwareRecord *rcd;
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
				     line[0], ln + 1);
			return FALSE;
		}

		/* check there's enough data for the smallest possible record */
		if (linesz < 10) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "record incomplete at line %u, length %u",
				     ln + 1, (guint) linesz);
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
				     ln + 1, (guint) linesz - 4, (guint) rec_count * 2);
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
					     ln + 1, rec_csum_expected, rec_csum);
				return FALSE;
			}
		}

		/* set each command settings */
		switch (rec_kind) {
		case FU_FIRMWARE_SREC_RECORD_KIND_S0_HEADER:
			addrsz = 2;
			break;
		case FU_FIRMWARE_SREC_RECORD_KIND_S1_DATA_16:
			addrsz = 2;
			break;
		case FU_FIRMWARE_SREC_RECORD_KIND_S2_DATA_24:
			addrsz = 3;
			break;
		case FU_FIRMWARE_SREC_RECORD_KIND_S3_DATA_32:
			addrsz = 4;
			break;
		case FU_FIRMWARE_SREC_RECORD_KIND_S5_COUNT_16:
			addrsz = 2;
			got_eof = TRUE;
			break;
		case FU_FIRMWARE_SREC_RECORD_KIND_S6_COUNT_24:
			addrsz = 3;
			break;
		case FU_FIRMWARE_SREC_RECORD_KIND_S7_COUNT_32:
			addrsz = 4;
			got_eof = TRUE;
			break;
		case FU_FIRMWARE_SREC_RECORD_KIND_S8_TERMINATION_24:
			addrsz = 3;
			got_eof = TRUE;
			break;
		case FU_FIRMWARE_SREC_RECORD_KIND_S9_TERMINATION_16:
			addrsz = 2;
			got_eof = TRUE;
			break;
		default:
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "invalid srec record type S%c at line %u",
				     line[1], ln + 1);
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

		g_debug ("line %03u S%u addr:0x%04x datalen:0x%02x",
			 ln + 1, rec_kind, rec_addr32,
			 (guint) rec_count - addrsz - 1);

		/* data */
		rcd = fu_srec_firmware_record_new (ln + 1, rec_kind, rec_addr32);
		if (rec_kind == 1 || rec_kind == 2 || rec_kind == 3) {
			for (guint8 i = 4 + (addrsz * 2); i <= rec_count * 2; i += 2) {
				guint8 tmp = fu_firmware_strparse_uint8 (line + i);
				fu_byte_array_append_uint8 (rcd->buf, tmp);
			}
		}
		g_ptr_array_add (self->records, rcd);
	}

	/* no EOF */
	if (!got_eof) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no EOF, perhaps truncated file");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_srec_firmware_parse (FuFirmware *firmware,
			GBytes *fw,
			guint64 addr_start,
			guint64 addr_end,
			FwupdInstallFlags flags,
			GError **error)
{
	FuSrecFirmware *self = FU_SREC_FIRMWARE (firmware);
	gboolean got_hdr = FALSE;
	guint16 data_cnt = 0;
	guint32 addr32_last = 0;
	guint32 img_address = 0;
	g_autoptr(FuFirmwareImage) img = fu_firmware_image_new (NULL);
	g_autoptr(GBytes) img_bytes = NULL;
	g_autoptr(GByteArray) outbuf = g_byte_array_new ();

	/* parse records */
	for (guint j = 0; j < self->records->len; j++) {
		FuSrecFirmwareRecord *rcd = g_ptr_array_index (self->records, j);

		/* header */
		if (rcd->kind == FU_FIRMWARE_SREC_RECORD_KIND_S0_HEADER) {
			g_autoptr(GString) modname = g_string_new (NULL);

			/* check for duplicate */
			if (got_hdr) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "duplicate header record at line %u",
					     rcd->ln);
				return FALSE;
			}

			/* could be anything, lets assume text */
			for (guint8 i = 0; i < rcd->buf->len; i++) {
				gchar tmp = rcd->buf->data[i];
				if (!g_ascii_isgraph (tmp))
					break;
				g_string_append_c (modname, tmp);
			}
			if (modname->len != 0)
				fu_firmware_image_set_id (img, modname->str);
			got_hdr = TRUE;
			continue;
		}

		/* verify we got all records */
		if (rcd->kind == FU_FIRMWARE_SREC_RECORD_KIND_S5_COUNT_16) {
			if (rcd->addr != data_cnt) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "count record was not valid, got 0x%02x expected 0x%02x at line %u",
					     (guint) rcd->addr, (guint) data_cnt, rcd->ln);
				return FALSE;
			}
			continue;
		}

		/* data */
		if (rcd->kind == FU_FIRMWARE_SREC_RECORD_KIND_S1_DATA_16 ||
		    rcd->kind == FU_FIRMWARE_SREC_RECORD_KIND_S2_DATA_24 ||
		    rcd->kind == FU_FIRMWARE_SREC_RECORD_KIND_S3_DATA_32) {
			/* invalid */
			if (!got_hdr) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "missing header record at line %u",
					     rcd->ln);
				return FALSE;
			}

			/* does not make sense */
			if (rcd->addr < addr32_last) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "invalid address 0x%x, last was 0x%x at line %u",
					     (guint) rcd->addr,
					     (guint) addr32_last,
					     rcd->ln);
				return FALSE;
			}
			if (rcd->addr < addr_start) {
				g_debug ("ignoring data at 0x%x as before start address 0x%x at line %u",
					 (guint) rcd->addr, (guint) addr_start, rcd->ln);
			} else {
				guint32 len_hole = rcd->addr - addr32_last;

				/* fill any holes, but only up to 1Mb to avoid a DoS */
				if (addr32_last > 0 && len_hole > 0x100000) {
					g_set_error (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INVALID_FILE,
						     "hole of 0x%x bytes too large to fill at line %u",
						     (guint) len_hole, rcd->ln);
					return FALSE;
				}
				if (addr32_last > 0x0 && len_hole > 1) {
					g_debug ("filling address 0x%08x to 0x%08x at line %u",
						 addr32_last + 1, addr32_last + len_hole - 1, rcd->ln);
					for (guint i = 0; i < len_hole; i++)
						fu_byte_array_append_uint8 (outbuf, 0xff);
				}

				/* add data */
				g_byte_array_append (outbuf, rcd->buf->data, rcd->buf->len);
				if (img_address == 0x0)
					img_address = rcd->addr;
				addr32_last = rcd->addr + rcd->buf->len;
			}
			data_cnt++;
		}
	}

	/* add single image */
	img_bytes = g_bytes_new (outbuf->data, outbuf->len);
	fu_firmware_image_set_bytes (img, img_bytes);
	fu_firmware_image_set_addr (img, img_address);
	fu_firmware_add_image (firmware, img);
	return TRUE;
}

static void
fu_srec_firmware_finalize (GObject *object)
{
	FuSrecFirmware *self = FU_SREC_FIRMWARE (object);
	g_ptr_array_unref (self->records);
	G_OBJECT_CLASS (fu_srec_firmware_parent_class)->finalize (object);
}

static void
fu_srec_firmware_init (FuSrecFirmware *self)
{
	self->records = g_ptr_array_new_with_free_func ((GFreeFunc) fu_srec_firmware_record_free);
}

static void
fu_srec_firmware_class_init (FuSrecFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	object_class->finalize = fu_srec_firmware_finalize;
	klass_firmware->parse = fu_srec_firmware_parse;
	klass_firmware->tokenize = fu_srec_firmware_tokenize;
}

/**
 * fu_srec_firmware_new:
 *
 * Creates a new #FuFirmware of sub type Srec
 *
 * Since: 1.3.2
 **/
FuFirmware *
fu_srec_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_SREC_FIRMWARE, NULL));
}
