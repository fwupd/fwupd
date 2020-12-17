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
#include "fu-ihex-firmware.h"

/**
 * SECTION:fu-ihex-firmware
 * @short_description: Ihex firmware image
 *
 * An object that represents a Ihex firmware image.
 *
 * See also: #FuFirmware
 */

struct _FuIhexFirmware {
	FuFirmware		 parent_instance;
	GPtrArray		*records;
};

G_DEFINE_TYPE (FuIhexFirmware, fu_ihex_firmware, FU_TYPE_FIRMWARE)

/**
 * fu_ihex_firmware_get_records:
 * @self: A #FuIhexFirmware
 *
 * Returns the raw lines from tokenization.
 *
 * This might be useful if the plugin is expecting the hex file to be a list
 * of operations, rather than a simple linear image with filled holes.
 *
 * Returns: (transfer none) (element-type FuIhexFirmwareRecord): records
 *
 * Since: 1.3.4
 **/
GPtrArray *
fu_ihex_firmware_get_records (FuIhexFirmware *self)
{
	g_return_val_if_fail (FU_IS_IHEX_FIRMWARE (self), NULL);
	return self->records;
}

static void
fu_ihex_firmware_record_free (FuIhexFirmwareRecord *rcd)
{
	g_string_free (rcd->buf, TRUE);
	g_byte_array_unref (rcd->data);
	g_free (rcd);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuIhexFirmwareRecord, fu_ihex_firmware_record_free)

static FuIhexFirmwareRecord *
fu_ihex_firmware_record_new (guint ln, const gchar *line,
			     FwupdInstallFlags flags, GError **error)
{
	g_autoptr(FuIhexFirmwareRecord) rcd = NULL;
	guint line_end;

	/* check starting token */
	if (line[0] != ':') {
		g_autoptr(GString) str = g_string_new (NULL);
		for (gsize i = 0; line[i] != '\0' && i < 5; i++) {
			if (!g_ascii_isprint (line[i]))
				break;
			g_string_append_c (str, line[i]);
		}
		if (str->len > 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "invalid starting token: %s",
				     str->str);
			return NULL;
		}
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "invalid starting token");
		return NULL;
	}

	/* length, 16-bit address, type */
	rcd = g_new0 (FuIhexFirmwareRecord, 1);
	rcd->ln = ln;
	rcd->data = g_byte_array_new ();
	rcd->buf = g_string_new (line);
	rcd->byte_cnt = fu_firmware_strparse_uint8 (line + 1);
	rcd->addr = fu_firmware_strparse_uint16 (line + 3);
	rcd->record_type = fu_firmware_strparse_uint8 (line + 7);

	/* check there's enough data for the smallest possible record */
	if (rcd->buf->len < 11) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "line incomplete, length: %u",
			     (guint) rcd->buf->len);
		return NULL;
	}

	/* position of checksum */
	line_end = 9 + rcd->byte_cnt * 2;
	if (line_end > (guint) rcd->buf->len) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "line malformed, length: %u",
			     line_end);
		return NULL;
	}

	/* verify checksum */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		guint8 checksum = 0;
		for (guint i = 1; i < line_end + 2; i += 2) {
			guint8 data_tmp = fu_firmware_strparse_uint8 (line + i);
			checksum += data_tmp;
		}
		if (checksum != 0)  {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "invalid checksum (0x%02x)",
				     checksum);
			return NULL;
		}
	}

	/* add data */
	for (guint i = 9; i < line_end; i += 2) {
		guint8 tmp_c = fu_firmware_strparse_uint8 (line + i);
		fu_byte_array_append_uint8 (rcd->data, tmp_c);
	}
	return g_steal_pointer (&rcd);
}

static const gchar *
fu_ihex_firmware_record_type_to_string (guint8 record_type)
{
	if (record_type == FU_IHEX_FIRMWARE_RECORD_TYPE_DATA)
		return "DATA";
	if (record_type == FU_IHEX_FIRMWARE_RECORD_TYPE_EOF)
		return "EOF";
	if (record_type == FU_IHEX_FIRMWARE_RECORD_TYPE_EXTENDED_SEGMENT)
		return "EXTENDED_SEGMENT";
	if (record_type == FU_IHEX_FIRMWARE_RECORD_TYPE_START_SEGMENT)
		return "START_SEGMENT";
	if (record_type == FU_IHEX_FIRMWARE_RECORD_TYPE_EXTENDED_LINEAR)
		return "EXTENDED_LINEAR";
	if (record_type == FU_IHEX_FIRMWARE_RECORD_TYPE_START_LINEAR)
		return "ADDR32";
	if (record_type == FU_IHEX_FIRMWARE_RECORD_TYPE_SIGNATURE)
		return "SIGNATURE";
	return NULL;
}

static gboolean
fu_ihex_firmware_tokenize (FuFirmware *firmware, GBytes *fw,
			   FwupdInstallFlags flags, GError **error)
{
	FuIhexFirmware *self = FU_IHEX_FIRMWARE (firmware);
	gsize sz = 0;
	const gchar *data = g_bytes_get_data (fw, &sz);
	g_auto(GStrv) lines = fu_common_strnsplit (data, sz, "\n", -1);

	for (guint ln = 0; lines[ln] != NULL; ln++) {
		g_autoptr(FuIhexFirmwareRecord) rcd = NULL;
		g_strdelimit (lines[ln], "\r\x1a", '\0');
		if (g_str_has_prefix (lines[ln], ";"))
			continue;
		if (lines[ln][0] == '\0')
			continue;
		rcd = fu_ihex_firmware_record_new (ln + 1, lines[ln], flags, error);
		if (rcd == NULL) {
			g_prefix_error (error, "invalid line %u: ", ln + 1);
			return FALSE;
		}
		g_ptr_array_add (self->records, g_steal_pointer (&rcd));
	}
	return TRUE;
}

static gboolean
fu_ihex_firmware_parse (FuFirmware *firmware,
			GBytes *fw,
			guint64 addr_start,
			guint64 addr_end,
			FwupdInstallFlags flags,
			GError **error)
{
	FuIhexFirmware *self = FU_IHEX_FIRMWARE (firmware);
	gboolean got_eof = FALSE;
	guint32 abs_addr = 0x0;
	guint32 addr_last = 0x0;
	guint32 img_addr = G_MAXUINT32;
	guint32 seg_addr = 0x0;
	g_autoptr(FuFirmwareImage) img = fu_firmware_image_new (NULL);
	g_autoptr(GBytes) img_bytes = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new ();

	/* parse records */
	for (guint k = 0; k < self->records->len; k++) {
		FuIhexFirmwareRecord *rcd = g_ptr_array_index (self->records, k);
		guint16 addr16 = 0;
		guint32 addr = rcd->addr + seg_addr + abs_addr;
		guint32 len_hole;

		g_debug ("%s:", fu_ihex_firmware_record_type_to_string (rcd->record_type));
		g_debug ("  length:\t0x%02x", rcd->data->len);
		g_debug ("  addr:\t0x%08x", addr);

		/* process different record types */
		switch (rcd->record_type) {
		case FU_IHEX_FIRMWARE_RECORD_TYPE_DATA:
			/* base address for element */
			if (img_addr == G_MAXUINT32)
				img_addr = addr;

			/* does not make sense */
			if (addr < addr_last) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "invalid address 0x%x, last was 0x%x on line %u",
					     (guint) addr,
					     (guint) addr_last,
					     rcd->ln);
				return FALSE;
			}

			/* any holes in the hex record */
			len_hole = addr - addr_last;
			if (addr_last > 0 && len_hole > 0x100000) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "hole of 0x%x bytes too large to fill on line %u",
					     (guint) len_hole,
					     rcd->ln);
				return FALSE;
			}
			if (addr_last > 0x0 && len_hole > 1) {
				g_debug ("filling address 0x%08x to 0x%08x on line %u",
					 addr_last + 1, addr_last + len_hole - 1, rcd->ln);
				for (guint j = 1; j < len_hole; j++) {
					/* although 0xff might be clearer,
					 * we can't write 0xffff to pic14 */
					fu_byte_array_append_uint8 (buf, 0x00);
				}
			}
			addr_last = addr + rcd->data->len - 1;

			/* write into buf */
			g_byte_array_append (buf, rcd->data->data, rcd->data->len);
			break;
		case FU_IHEX_FIRMWARE_RECORD_TYPE_EOF:
			if (got_eof) {
				g_set_error_literal (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INVALID_FILE,
						     "duplicate EOF, perhaps "
						     "corrupt file");
				return FALSE;
			}
			got_eof = TRUE;
			break;
		case FU_IHEX_FIRMWARE_RECORD_TYPE_EXTENDED_LINEAR:
			if (!fu_common_read_uint16_safe (rcd->data->data, rcd->data->len,
							 0x0, &addr16, G_BIG_ENDIAN, error))
				return FALSE;
			abs_addr = (guint32) addr16 << 16;
			g_debug ("  abs_addr:\t0x%02x on line %u", abs_addr, rcd->ln);
			break;
		case FU_IHEX_FIRMWARE_RECORD_TYPE_START_LINEAR:
			if (!fu_common_read_uint32_safe (rcd->data->data, rcd->data->len,
							 0x0, &abs_addr, G_BIG_ENDIAN, error))
				return FALSE;
			g_debug ("  abs_addr:\t0x%08x on line %u", abs_addr, rcd->ln);
			break;
		case FU_IHEX_FIRMWARE_RECORD_TYPE_EXTENDED_SEGMENT:
			if (!fu_common_read_uint16_safe (rcd->data->data, rcd->data->len,
							 0x0, &addr16, G_BIG_ENDIAN, error))
				return FALSE;
			/* segment base address, so ~1Mb addressable */
			seg_addr = (guint32) addr16 * 16;
			g_debug ("  seg_addr:\t0x%08x on line %u", seg_addr, rcd->ln);
			break;
		case FU_IHEX_FIRMWARE_RECORD_TYPE_START_SEGMENT:
			/* initial content of the CS:IP registers */
			if (!fu_common_read_uint32_safe (rcd->data->data, rcd->data->len,
							 0x0, &seg_addr, G_BIG_ENDIAN, error))
				return FALSE;
			g_debug ("  seg_addr:\t0x%02x on line %u", seg_addr, rcd->ln);
			break;
		case FU_IHEX_FIRMWARE_RECORD_TYPE_SIGNATURE:
			if (rcd->data->len > 0) {
				g_autoptr(GBytes) data_sig = g_bytes_new (rcd->data->data, rcd->data->len);
				g_autoptr(FuFirmwareImage) img_sig = fu_firmware_image_new (data_sig);
				fu_firmware_image_set_id (img_sig, FU_FIRMWARE_IMAGE_ID_SIGNATURE);
				fu_firmware_add_image (firmware, img_sig);
			}
			break;
		default:
			/* vendors sneak in nonstandard sections past the EOF */
			if (got_eof)
				break;
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "invalid ihex record type %i on line %u",
				     rcd->record_type, rcd->ln);
			return FALSE;
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
	img_bytes = g_bytes_new (buf->data, buf->len);
	fu_firmware_image_set_bytes (img, img_bytes);
	if (img_addr != G_MAXUINT32)
		fu_firmware_image_set_addr (img, img_addr);
	fu_firmware_add_image (firmware, img);
	return TRUE;
}

static void
fu_ihex_firmware_emit_chunk (GString *str,
			     guint16 address,
			     guint8 record_type,
			     const guint8 *data,
			     gsize sz)
{
	guint8 checksum = 0x00;
	g_string_append_printf (str, ":%02X%04X%02X",
				(guint) sz,
				(guint) address,
				(guint) record_type);
	for (gsize j = 0; j < sz; j++)
		g_string_append_printf (str, "%02X", data[j]);
	checksum = (guint8) sz;
	checksum += (guint8) ((address & 0xff00) >> 8);
	checksum += (guint8) (address & 0xff);
	checksum += record_type;
	for (gsize j = 0; j < sz; j++)
		checksum += data[j];
	g_string_append_printf (str, "%02X\n", (guint) (((~checksum) + 0x01) & 0xff));
}

static gboolean
fu_ihex_firmware_image_to_string (FuFirmwareImage *img, GString *str, GError **error)
{
	const guint8 *data;
	const guint chunk_size = 16;
	gsize len;
	guint32 address_offset_last = 0x0;
	guint8 record_type = FU_IHEX_FIRMWARE_RECORD_TYPE_DATA;
	g_autoptr(GBytes) bytes = NULL;

	/* get data */
	bytes = fu_firmware_image_write (img, error);
	if (bytes == NULL)
		return FALSE;

	/* special case */
	if (g_strcmp0 (fu_firmware_image_get_id (img),
		       FU_FIRMWARE_IMAGE_ID_SIGNATURE) == 0)
		record_type = FU_IHEX_FIRMWARE_RECORD_TYPE_SIGNATURE;

	/* get number of chunks */
	data = g_bytes_get_data (bytes, &len);
	for (gsize i = 0; i < len; i += chunk_size) {
		guint32 address_tmp = fu_firmware_image_get_addr (img) + i;
		guint32 address_offset = (address_tmp >> 16) & 0xffff;
		gsize chunk_len = MIN (len - i, 16);

		/* need to offset */
		if (address_offset != address_offset_last) {
			guint8 buf[2];
			fu_common_write_uint16 (buf, address_offset, G_BIG_ENDIAN);
			fu_ihex_firmware_emit_chunk (str, 0x0,
						     FU_IHEX_FIRMWARE_RECORD_TYPE_EXTENDED_LINEAR,
						     buf, 2);
			address_offset_last = address_offset;
		}
		address_tmp &= 0xffff;
		fu_ihex_firmware_emit_chunk (str, address_tmp,
					     record_type, data + i, chunk_len);
	}
	return TRUE;
}

static GBytes *
fu_ihex_firmware_write (FuFirmware *firmware, GError **error)
{
	g_autoptr(GPtrArray) imgs = NULL;
	g_autoptr(GString) str = NULL;

	/* write all the element data */
	str = g_string_new ("");
	imgs = fu_firmware_get_images (firmware);
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmwareImage *img = g_ptr_array_index (imgs, i);
		if (!fu_ihex_firmware_image_to_string (img, str, error))
			return NULL;
	}

	/* add EOF */
	fu_ihex_firmware_emit_chunk (str, 0x0, FU_IHEX_FIRMWARE_RECORD_TYPE_EOF, NULL, 0);
	return g_bytes_new (str->str, str->len);
}

static void
fu_ihex_firmware_finalize (GObject *object)
{
	FuIhexFirmware *self = FU_IHEX_FIRMWARE (object);
	g_ptr_array_unref (self->records);
	G_OBJECT_CLASS (fu_ihex_firmware_parent_class)->finalize (object);
}

static void
fu_ihex_firmware_init (FuIhexFirmware *self)
{
	self->records = g_ptr_array_new_with_free_func ((GFreeFunc) fu_ihex_firmware_record_free);
}

static void
fu_ihex_firmware_class_init (FuIhexFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	object_class->finalize = fu_ihex_firmware_finalize;
	klass_firmware->parse = fu_ihex_firmware_parse;
	klass_firmware->tokenize = fu_ihex_firmware_tokenize;
	klass_firmware->write = fu_ihex_firmware_write;
}

/**
 * fu_ihex_firmware_new:
 *
 * Creates a new #FuFirmware of sub type Ihex
 *
 * Since: 1.3.1
 **/
FuFirmware *
fu_ihex_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_IHEX_FIRMWARE, NULL));
}
