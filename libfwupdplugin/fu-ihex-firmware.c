/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include <string.h>

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-firmware-common.h"
#include "fu-ihex-firmware.h"
#include "fu-mem.h"
#include "fu-string.h"

/**
 * FuIhexFirmware:
 *
 * A Intel hex (ihex) firmware image.
 *
 * See also: [class@FuFirmware]
 */

typedef struct {
	GPtrArray *records;
	guint8 padding_value;
} FuIhexFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuIhexFirmware, fu_ihex_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_ihex_firmware_get_instance_private(o))

#define FU_IHEX_FIRMWARE_TOKENS_MAX 100000 /* lines */

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
fu_ihex_firmware_get_records(FuIhexFirmware *self)
{
	FuIhexFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_IHEX_FIRMWARE(self), NULL);
	return priv->records;
}

/**
 * fu_ihex_firmware_set_padding_value:
 * @self: A #FuIhexFirmware
 * @padding_value: the byte used to pad the image
 *
 * Set the padding value to fill incomplete address ranges.
 *
 * The default value of zero can be changed to `0xff` if functions like
 * fu_bytes_is_empty() are going to be used on subsections of the data.
 *
 * Since: 1.6.0
 **/
void
fu_ihex_firmware_set_padding_value(FuIhexFirmware *self, guint8 padding_value)
{
	FuIhexFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_IHEX_FIRMWARE(self));
	priv->padding_value = padding_value;
}

static void
fu_ihex_firmware_record_free(FuIhexFirmwareRecord *rcd)
{
	g_string_free(rcd->buf, TRUE);
	g_byte_array_unref(rcd->data);
	g_free(rcd);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuIhexFirmwareRecord, fu_ihex_firmware_record_free)

static FuIhexFirmwareRecord *
fu_ihex_firmware_record_new(guint ln, const gchar *line, FwupdInstallFlags flags, GError **error)
{
	g_autoptr(FuIhexFirmwareRecord) rcd = NULL;
	gsize linesz = strlen(line);
	guint line_end;
	guint16 addr16 = 0;

	/* check starting token */
	if (line[0] != ':') {
		g_autofree gchar *strsafe = fu_strsafe(line, 5);
		if (strsafe != NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid starting token: %s",
				    strsafe);
			return NULL;
		}
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid starting token");
		return NULL;
	}

	/* length, 16-bit address, type */
	rcd = g_new0(FuIhexFirmwareRecord, 1);
	rcd->ln = ln;
	rcd->data = g_byte_array_new();
	rcd->buf = g_string_new(line);
	if (!fu_firmware_strparse_uint8_safe(line, linesz, 1, &rcd->byte_cnt, error))
		return NULL;
	if (!fu_firmware_strparse_uint16_safe(line, linesz, 3, &addr16, error))
		return NULL;
	rcd->addr = addr16;
	if (!fu_firmware_strparse_uint8_safe(line, linesz, 7, &rcd->record_type, error))
		return NULL;

	/* position of checksum */
	line_end = 9 + rcd->byte_cnt * 2;
	if (line_end > (guint)rcd->buf->len) {
		g_set_error(error,
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
			guint8 data_tmp = 0;
			if (!fu_firmware_strparse_uint8_safe(line, linesz, i, &data_tmp, error))
				return NULL;
			checksum += data_tmp;
		}
		if (checksum != 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid checksum (0x%02x)",
				    checksum);
			return NULL;
		}
	}

	/* add data */
	for (guint i = 9; i < line_end; i += 2) {
		guint8 tmp_c = 0;
		if (!fu_firmware_strparse_uint8_safe(line, linesz, i, &tmp_c, error))
			return NULL;
		fu_byte_array_append_uint8(rcd->data, tmp_c);
	}
	return g_steal_pointer(&rcd);
}

static const gchar *
fu_ihex_firmware_record_type_to_string(guint8 record_type)
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

typedef struct {
	FuIhexFirmware *self;
	FwupdInstallFlags flags;
} FuIhexFirmwareTokenHelper;

static gboolean
fu_ihex_firmware_tokenize_cb(GString *token, guint token_idx, gpointer user_data, GError **error)
{
	FuIhexFirmwareTokenHelper *helper = (FuIhexFirmwareTokenHelper *)user_data;
	FuIhexFirmwarePrivate *priv = GET_PRIVATE(helper->self);
	g_autoptr(FuIhexFirmwareRecord) rcd = NULL;

	/* sanity check */
	if (token_idx > FU_IHEX_FIRMWARE_TOKENS_MAX) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "file has too many lines");
		return FALSE;
	}

	/* remove WIN32 line endings */
	g_strdelimit(token->str, "\r\x1a", '\0');
	token->len = strlen(token->str);

	/* ignore blank lines */
	if (token->len == 0)
		return TRUE;

	/* ignore comments */
	if (g_str_has_prefix(token->str, ";"))
		return TRUE;

	/* parse record */
	rcd = fu_ihex_firmware_record_new(token_idx + 1, token->str, helper->flags, error);
	if (rcd == NULL) {
		g_prefix_error(error, "invalid line %u: ", token_idx + 1);
		return FALSE;
	}
	g_ptr_array_add(priv->records, g_steal_pointer(&rcd));
	return TRUE;
}

static gboolean
fu_ihex_firmware_tokenize(FuFirmware *firmware, GBytes *fw, FwupdInstallFlags flags, GError **error)
{
	FuIhexFirmware *self = FU_IHEX_FIRMWARE(firmware);
	FuIhexFirmwareTokenHelper helper = {.self = self, .flags = flags};
	return fu_strsplit_full(g_bytes_get_data(fw, NULL),
				g_bytes_get_size(fw),
				"\n",
				fu_ihex_firmware_tokenize_cb,
				&helper,
				error);
}

static gboolean
fu_ihex_firmware_parse(FuFirmware *firmware,
		       GBytes *fw,
		       gsize offset,
		       FwupdInstallFlags flags,
		       GError **error)
{
	FuIhexFirmware *self = FU_IHEX_FIRMWARE(firmware);
	FuIhexFirmwarePrivate *priv = GET_PRIVATE(self);
	gboolean got_eof = FALSE;
	gboolean got_sig = FALSE;
	guint32 abs_addr = 0x0;
	guint32 addr_last = 0x0;
	guint32 img_addr = G_MAXUINT32;
	guint32 seg_addr = 0x0;
	g_autoptr(GBytes) img_bytes = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* parse records */
	for (guint k = 0; k < priv->records->len; k++) {
		FuIhexFirmwareRecord *rcd = g_ptr_array_index(priv->records, k);
		guint16 addr16 = 0;
		guint32 addr = rcd->addr + seg_addr + abs_addr;
		guint32 len_hole;

		g_debug("%s:", fu_ihex_firmware_record_type_to_string(rcd->record_type));
		g_debug("  length:\t0x%02x", rcd->data->len);
		g_debug("  addr:\t0x%08x", addr);

		/* sanity check */
		if (rcd->record_type != FU_IHEX_FIRMWARE_RECORD_TYPE_EOF && rcd->data->len == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "record 0x%x had zero size",
				    k);
			return FALSE;
		}

		/* process different record types */
		switch (rcd->record_type) {
		case FU_IHEX_FIRMWARE_RECORD_TYPE_DATA:

			/* does not make sense */
			if (got_eof) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_FILE,
						    "cannot process data after EOF");
				return FALSE;
			}
			if (rcd->data->len == 0) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_FILE,
						    "cannot parse invalid data");
				return FALSE;
			}

			/* base address for element */
			if (img_addr == G_MAXUINT32)
				img_addr = addr;

			/* does not make sense */
			if (addr < addr_last) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "invalid address 0x%x, last was 0x%x on line %u",
					    (guint)addr,
					    (guint)addr_last,
					    rcd->ln);
				return FALSE;
			}

			/* any holes in the hex record */
			len_hole = addr - addr_last;
			if (addr_last > 0 && len_hole > 0x100000) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "hole of 0x%x bytes too large to fill on line %u",
					    (guint)len_hole,
					    rcd->ln);
				return FALSE;
			}
			if (addr_last > 0x0 && len_hole > 1) {
				g_debug("filling address 0x%08x to 0x%08x on line %u",
					addr_last + 1,
					addr_last + len_hole - 1,
					rcd->ln);
				for (guint j = 1; j < len_hole; j++)
					fu_byte_array_append_uint8(buf, priv->padding_value);
			}
			addr_last = addr + rcd->data->len - 1;
			if (addr_last < addr) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "overflow of address 0x%x on line %u",
					    (guint)addr,
					    rcd->ln);
				return FALSE;
			}

			/* write into buf */
			g_byte_array_append(buf, rcd->data->data, rcd->data->len);
			break;
		case FU_IHEX_FIRMWARE_RECORD_TYPE_EOF:
			if (got_eof) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_FILE,
						    "duplicate EOF, perhaps "
						    "corrupt file");
				return FALSE;
			}
			got_eof = TRUE;
			break;
		case FU_IHEX_FIRMWARE_RECORD_TYPE_EXTENDED_LINEAR:
			if (!fu_memread_uint16_safe(rcd->data->data,
						    rcd->data->len,
						    0x0,
						    &addr16,
						    G_BIG_ENDIAN,
						    error))
				return FALSE;
			abs_addr = (guint32)addr16 << 16;
			g_debug("  abs_addr:\t0x%02x on line %u", abs_addr, rcd->ln);
			break;
		case FU_IHEX_FIRMWARE_RECORD_TYPE_START_LINEAR:
			if (!fu_memread_uint32_safe(rcd->data->data,
						    rcd->data->len,
						    0x0,
						    &abs_addr,
						    G_BIG_ENDIAN,
						    error))
				return FALSE;
			g_debug("  abs_addr:\t0x%08x on line %u", abs_addr, rcd->ln);
			break;
		case FU_IHEX_FIRMWARE_RECORD_TYPE_EXTENDED_SEGMENT:
			if (!fu_memread_uint16_safe(rcd->data->data,
						    rcd->data->len,
						    0x0,
						    &addr16,
						    G_BIG_ENDIAN,
						    error))
				return FALSE;
			/* segment base address, so ~1Mb addressable */
			seg_addr = (guint32)addr16 * 16;
			g_debug("  seg_addr:\t0x%08x on line %u", seg_addr, rcd->ln);
			break;
		case FU_IHEX_FIRMWARE_RECORD_TYPE_START_SEGMENT:
			/* initial content of the CS:IP registers */
			if (!fu_memread_uint32_safe(rcd->data->data,
						    rcd->data->len,
						    0x0,
						    &seg_addr,
						    G_BIG_ENDIAN,
						    error))
				return FALSE;
			g_debug("  seg_addr:\t0x%02x on line %u", seg_addr, rcd->ln);
			break;
		case FU_IHEX_FIRMWARE_RECORD_TYPE_SIGNATURE:
			if (got_sig) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_FILE,
						    "duplicate signature, perhaps "
						    "corrupt file");
				return FALSE;
			}
			if (rcd->data->len > 0) {
				g_autoptr(GBytes) data_sig =
				    g_bytes_new(rcd->data->data, rcd->data->len);
				g_autoptr(FuFirmware) img_sig =
				    fu_firmware_new_from_bytes(data_sig);
				fu_firmware_set_id(img_sig, FU_FIRMWARE_ID_SIGNATURE);
				fu_firmware_add_image(firmware, img_sig);
			}
			got_sig = TRUE;
			break;
		default:
			/* vendors sneak in nonstandard sections past the EOF */
			if (got_eof)
				break;
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid ihex record type %i on line %u",
				    rcd->record_type,
				    rcd->ln);
			return FALSE;
		}
	}

	/* no EOF */
	if (!got_eof) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no EOF, perhaps truncated file");
		return FALSE;
	}

	/* add single image */
	img_bytes = g_bytes_new(buf->data, buf->len);
	if (img_addr != G_MAXUINT32)
		fu_firmware_set_addr(firmware, img_addr);
	fu_firmware_set_bytes(firmware, img_bytes);
	return TRUE;
}

static void
fu_ihex_firmware_emit_chunk(GString *str,
			    guint16 address,
			    guint8 record_type,
			    const guint8 *data,
			    gsize sz)
{
	guint8 checksum = 0x00;
	g_string_append_printf(str, ":%02X%04X%02X", (guint)sz, (guint)address, (guint)record_type);
	for (gsize j = 0; j < sz; j++)
		g_string_append_printf(str, "%02X", data[j]);
	checksum = (guint8)sz;
	checksum += (guint8)((address & 0xff00) >> 8);
	checksum += (guint8)(address & 0xff);
	checksum += record_type;
	for (gsize j = 0; j < sz; j++)
		checksum += data[j];
	g_string_append_printf(str, "%02X\n", (guint)(((~checksum) + 0x01) & 0xff));
}

static gboolean
fu_ihex_firmware_image_to_string(GBytes *bytes,
				 guint32 addr,
				 guint8 record_type,
				 GString *str,
				 GError **error)
{
	const guint8 *data;
	const guint chunk_size = 16;
	gsize len;
	guint32 address_offset_last = 0x0;

	/* get number of chunks */
	data = g_bytes_get_data(bytes, &len);
	for (gsize i = 0; i < len; i += chunk_size) {
		guint32 address_tmp = addr + i;
		guint32 address_offset = (address_tmp >> 16) & 0xffff;
		gsize chunk_len = MIN(len - i, 16);

		/* need to offset */
		if (address_offset != address_offset_last) {
			guint8 buf[2];
			fu_memwrite_uint16(buf, address_offset, G_BIG_ENDIAN);
			fu_ihex_firmware_emit_chunk(str,
						    0x0,
						    FU_IHEX_FIRMWARE_RECORD_TYPE_EXTENDED_LINEAR,
						    buf,
						    2);
			address_offset_last = address_offset;
		}
		address_tmp &= 0xffff;
		fu_ihex_firmware_emit_chunk(str, address_tmp, record_type, data + i, chunk_len);
	}
	return TRUE;
}

static GBytes *
fu_ihex_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(FuFirmware) img_sig = NULL;
	g_autoptr(GString) str = g_string_new("");

	/* payload */
	fw = fu_firmware_get_bytes_with_patches(firmware, error);
	if (fw == NULL)
		return NULL;
	if (!fu_ihex_firmware_image_to_string(fw,
					      fu_firmware_get_addr(firmware),
					      FU_IHEX_FIRMWARE_RECORD_TYPE_DATA,
					      str,
					      error))
		return NULL;

	/* signature */
	img_sig = fu_firmware_get_image_by_id(firmware, FU_FIRMWARE_ID_SIGNATURE, NULL);
	if (img_sig != NULL) {
		g_autoptr(GBytes) img_fw = fu_firmware_get_bytes(img_sig, error);
		if (img_fw == NULL)
			return NULL;
		if (!fu_ihex_firmware_image_to_string(img_fw,
						      0,
						      FU_IHEX_FIRMWARE_RECORD_TYPE_SIGNATURE,
						      str,
						      error))
			return NULL;
	}

	/* add EOF */
	fu_ihex_firmware_emit_chunk(str, 0x0, FU_IHEX_FIRMWARE_RECORD_TYPE_EOF, NULL, 0);
	return g_bytes_new(str->str, str->len);
}

static void
fu_ihex_firmware_finalize(GObject *object)
{
	FuIhexFirmware *self = FU_IHEX_FIRMWARE(object);
	FuIhexFirmwarePrivate *priv = GET_PRIVATE(self);
	g_ptr_array_unref(priv->records);
	G_OBJECT_CLASS(fu_ihex_firmware_parent_class)->finalize(object);
}

static void
fu_ihex_firmware_init(FuIhexFirmware *self)
{
	FuIhexFirmwarePrivate *priv = GET_PRIVATE(self);
	priv->padding_value = 0x00; /* chosen as we can't write 0xffff to PIC14 */
	priv->records = g_ptr_array_new_with_free_func((GFreeFunc)fu_ihex_firmware_record_free);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
}

static void
fu_ihex_firmware_class_init(FuIhexFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
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
fu_ihex_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_IHEX_FIRMWARE, NULL));
}
