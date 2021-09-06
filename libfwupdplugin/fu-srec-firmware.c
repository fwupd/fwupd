/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include <string.h>

#include "fu-common.h"
#include "fu-firmware-common.h"
#include "fu-srec-firmware.h"

/**
 * FuSrecFirmware:
 *
 * A SREC firmware image.
 *
 * See also: [class@FuFirmware]
 */

typedef struct {
	GPtrArray *records;
} FuSrecFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuSrecFirmware, fu_srec_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_srec_firmware_get_instance_private(o))

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
fu_srec_firmware_get_records(FuSrecFirmware *self)
{
	FuSrecFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_SREC_FIRMWARE(self), NULL);
	return priv->records;
}

static void
fu_srec_firmware_record_free(FuSrecFirmwareRecord *rcd)
{
	g_byte_array_unref(rcd->buf);
	g_free(rcd);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuSrecFirmwareRecord, fu_srec_firmware_record_free);
#pragma clang diagnostic pop

/**
 * fu_srec_firmware_record_new: (skip):
 * @ln: unsigned integer
 * @kind: a record kind, e.g. #FU_FIRMWARE_SREC_RECORD_KIND_S3_DATA_32
 * @addr: unsigned integer
 *
 * Returns a single firmware record
 *
 * Returns: (transfer full) (element-type FuSrecFirmwareRecord): records
 *
 * Since: 1.3.2
 **/
FuSrecFirmwareRecord *
fu_srec_firmware_record_new(guint ln, FuFirmareSrecRecordKind kind, guint32 addr)
{
	FuSrecFirmwareRecord *rcd = g_new0(FuSrecFirmwareRecord, 1);
	rcd->ln = ln;
	rcd->kind = kind;
	rcd->addr = addr;
	rcd->buf = g_byte_array_new();
	return rcd;
}

static FuSrecFirmwareRecord *
fu_srec_firmware_record_dup(const FuSrecFirmwareRecord *rcd)
{
	FuSrecFirmwareRecord *dest;
	g_return_val_if_fail(rcd != NULL, NULL);
	dest = fu_srec_firmware_record_new(rcd->ln, rcd->kind, rcd->addr);
	dest->buf = g_byte_array_ref(rcd->buf);
	return dest;
}

/**
 * fu_srec_firmware_record_get_type:
 *
 * Gets a specific type.
 *
 * Return value: a #GType
 *
 * Since: 1.6.1
 **/
GType
fu_srec_firmware_record_get_type(void)
{
	static GType type_id = 0;
	if (!type_id) {
		type_id =
		    g_boxed_type_register_static("FuSrecFirmwareRecord",
						 (GBoxedCopyFunc)fu_srec_firmware_record_dup,
						 (GBoxedFreeFunc)fu_srec_firmware_record_free);
	}
	return type_id;
}

static gboolean
fu_srec_firmware_tokenize(FuFirmware *firmware, GBytes *fw, FwupdInstallFlags flags, GError **error)
{
	FuSrecFirmware *self = FU_SREC_FIRMWARE(firmware);
	FuSrecFirmwarePrivate *priv = GET_PRIVATE(self);
	const gchar *data;
	gboolean got_eof = FALSE;
	gsize sz = 0;
	g_auto(GStrv) lines = NULL;

	/* parse records */
	data = g_bytes_get_data(fw, &sz);
	lines = fu_common_strnsplit(data, sz, "\n", -1);
	for (guint ln = 0; lines[ln] != NULL; ln++) {
		g_autoptr(FuSrecFirmwareRecord) rcd = NULL;
		const gchar *line = lines[ln];
		gsize linesz;
		guint32 rec_addr32;
		guint16 rec_addr16;
		guint8 addrsz = 0; /* bytes */
		guint8 rec_count;  /* words */
		guint8 rec_kind;

		/* ignore blank lines */
		g_strdelimit(lines[ln], "\r", '\0');
		linesz = strlen(line);
		if (linesz == 0)
			continue;

		/* check starting token */
		if (line[0] != 'S') {
			g_autofree gchar *strsafe = fu_common_strsafe(line, 3);
			if (strsafe != NULL) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "invalid starting token, got '%s' at line %u",
					    strsafe,
					    ln + 1);
				return FALSE;
			}
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid starting token at line %u",
				    ln + 1);
			return FALSE;
		}

		/* kind, count, address, (data), checksum, linefeed */
		rec_kind = line[1] - '0';
		if (!fu_firmware_strparse_uint8_safe(line, linesz, 2, &rec_count, error))
			return FALSE;
		if (rec_count * 2 != linesz - 4) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "count incomplete at line %u, "
				    "length %u, expected %u",
				    ln + 1,
				    (guint)linesz - 4,
				    (guint)rec_count * 2);
			return FALSE;
		}

		/* checksum check */
		if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
			guint8 rec_csum = 0;
			guint8 rec_csum_expected;
			for (guint8 i = 0; i < rec_count; i++) {
				guint8 csum_tmp = 0;
				if (!fu_firmware_strparse_uint8_safe(line,
								     linesz,
								     (i * 2) + 2,
								     &csum_tmp,
								     error))
					return FALSE;
				rec_csum += csum_tmp;
			}
			rec_csum ^= 0xff;
			if (!fu_firmware_strparse_uint8_safe(line,
							     linesz,
							     (rec_count * 2) + 2,
							     &rec_csum_expected,
							     error))
				return FALSE;
			if (rec_csum != rec_csum_expected) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "checksum incorrect line %u, "
					    "expected %02x, got %02x",
					    ln + 1,
					    rec_csum_expected,
					    rec_csum);
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
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid srec record type S%c at line %u",
				    line[1],
				    ln + 1);
			return FALSE;
		}

		/* parse address */
		switch (addrsz) {
		case 2:
			if (!fu_firmware_strparse_uint16_safe(line, linesz, 4, &rec_addr16, error))
				return FALSE;
			rec_addr32 = rec_addr16;
			break;
		case 3:
			if (!fu_firmware_strparse_uint24_safe(line, linesz, 4, &rec_addr32, error))
				return FALSE;
			break;
		case 4:
			if (!fu_firmware_strparse_uint32_safe(line, linesz, 4, &rec_addr32, error))
				return FALSE;
			break;
		default:
			g_assert_not_reached();
		}
		if (g_getenv("FU_SREC_FIRMWARE_VERBOSE") != NULL) {
			g_debug("line %03u S%u addr:0x%04x datalen:0x%02x",
				ln + 1,
				rec_kind,
				rec_addr32,
				(guint)rec_count - addrsz - 1);
		}

		/* data */
		rcd = fu_srec_firmware_record_new(ln + 1, rec_kind, rec_addr32);
		if (rec_kind == 1 || rec_kind == 2 || rec_kind == 3) {
			for (gsize i = 4 + (addrsz * 2); i <= rec_count * 2; i += 2) {
				guint8 tmp = 0;
				if (!fu_firmware_strparse_uint8_safe(line, linesz, i, &tmp, error))
					return FALSE;
				fu_byte_array_append_uint8(rcd->buf, tmp);
			}
		}
		g_ptr_array_add(priv->records, g_steal_pointer(&rcd));
	}

	/* no EOF */
	if (!got_eof) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no EOF, perhaps truncated file");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_srec_firmware_parse(FuFirmware *firmware,
		       GBytes *fw,
		       guint64 addr_start,
		       guint64 addr_end,
		       FwupdInstallFlags flags,
		       GError **error)
{
	FuSrecFirmware *self = FU_SREC_FIRMWARE(firmware);
	FuSrecFirmwarePrivate *priv = GET_PRIVATE(self);
	gboolean got_hdr = FALSE;
	guint16 data_cnt = 0;
	guint32 addr32_last = 0;
	guint32 img_address = 0;
	g_autoptr(GBytes) img_bytes = NULL;
	g_autoptr(GByteArray) outbuf = g_byte_array_new();

	/* parse records */
	for (guint j = 0; j < priv->records->len; j++) {
		FuSrecFirmwareRecord *rcd = g_ptr_array_index(priv->records, j);

		/* header */
		if (rcd->kind == FU_FIRMWARE_SREC_RECORD_KIND_S0_HEADER) {
			g_autoptr(GString) modname = g_string_new(NULL);

			/* check for duplicate */
			if (got_hdr) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "duplicate header record at line %u",
					    rcd->ln);
				return FALSE;
			}

			/* could be anything, lets assume text */
			for (guint8 i = 0; i < rcd->buf->len; i++) {
				gchar tmp = rcd->buf->data[i];
				if (!g_ascii_isgraph(tmp))
					break;
				g_string_append_c(modname, tmp);
			}
			if (modname->len != 0)
				fu_firmware_set_id(firmware, modname->str);
			got_hdr = TRUE;
			continue;
		}

		/* verify we got all records */
		if (rcd->kind == FU_FIRMWARE_SREC_RECORD_KIND_S5_COUNT_16) {
			if (rcd->addr != data_cnt) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "count record was not valid, got 0x%02x expected "
					    "0x%02x at line %u",
					    (guint)rcd->addr,
					    (guint)data_cnt,
					    rcd->ln);
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
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "missing header record at line %u",
					    rcd->ln);
				return FALSE;
			}

			/* does not make sense */
			if (rcd->addr < addr32_last) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "invalid address 0x%x, last was 0x%x at line %u",
					    (guint)rcd->addr,
					    (guint)addr32_last,
					    rcd->ln);
				return FALSE;
			}
			if (rcd->addr < addr_start) {
				g_debug(
				    "ignoring data at 0x%x as before start address 0x%x at line %u",
				    (guint)rcd->addr,
				    (guint)addr_start,
				    rcd->ln);
			} else {
				guint32 len_hole = rcd->addr - addr32_last;

				/* fill any holes, but only up to 1Mb to avoid a DoS */
				if (addr32_last > 0 && len_hole > 0x100000) {
					g_set_error(
					    error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "hole of 0x%x bytes too large to fill at line %u",
					    (guint)len_hole,
					    rcd->ln);
					return FALSE;
				}
				if (addr32_last > 0x0 && len_hole > 1) {
					g_debug("filling address 0x%08x to 0x%08x at line %u",
						addr32_last + 1,
						addr32_last + len_hole - 1,
						rcd->ln);
					for (guint i = 0; i < len_hole; i++)
						fu_byte_array_append_uint8(outbuf, 0xff);
				}

				/* add data */
				g_byte_array_append(outbuf, rcd->buf->data, rcd->buf->len);
				if (img_address == 0x0)
					img_address = rcd->addr;
				addr32_last = rcd->addr + rcd->buf->len;
				if (addr32_last < rcd->addr) {
					g_set_error(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_FILE,
						    "overflow from address 0x%x at line %u",
						    (guint)rcd->addr,
						    rcd->ln);
					return FALSE;
				}
			}
			data_cnt++;
		}
	}

	/* add single image */
	img_bytes = g_bytes_new(outbuf->data, outbuf->len);
	fu_firmware_set_bytes(firmware, img_bytes);
	fu_firmware_set_addr(firmware, img_address);
	return TRUE;
}

static void
fu_srec_firmware_write_line(GString *str,
			    FuFirmareSrecRecordKind kind,
			    guint32 addr,
			    const guint8 *buf,
			    gsize bufsz)
{
	guint8 csum = 0;
	g_autoptr(GByteArray) buf_addr = g_byte_array_new();

	if (kind == FU_FIRMWARE_SREC_RECORD_KIND_S0_HEADER ||
	    kind == FU_FIRMWARE_SREC_RECORD_KIND_S1_DATA_16 ||
	    kind == FU_FIRMWARE_SREC_RECORD_KIND_S5_COUNT_16 ||
	    kind == FU_FIRMWARE_SREC_RECORD_KIND_S9_TERMINATION_16) {
		fu_byte_array_append_uint16(buf_addr, addr, G_BIG_ENDIAN);
	} else if (kind == FU_FIRMWARE_SREC_RECORD_KIND_S2_DATA_24 ||
		   kind == FU_FIRMWARE_SREC_RECORD_KIND_S6_COUNT_24 ||
		   kind == FU_FIRMWARE_SREC_RECORD_KIND_S8_TERMINATION_24) {
		fu_byte_array_append_uint32(buf_addr, addr, G_BIG_ENDIAN);
		g_byte_array_remove_index(buf_addr, 0);
	} else if (kind == FU_FIRMWARE_SREC_RECORD_KIND_S3_DATA_32 ||
		   kind == FU_FIRMWARE_SREC_RECORD_KIND_S7_COUNT_32) {
		fu_byte_array_append_uint32(buf_addr, addr, G_BIG_ENDIAN);
	}

	/* bytecount + address + data */
	csum = buf_addr->len + bufsz + 1;
	for (guint i = 0; i < buf_addr->len; i++)
		csum += buf_addr->data[i];
	for (guint i = 0; i < bufsz; i++)
		csum += buf[i];
	csum ^= 0xff;

	/* output record */
	g_string_append_printf(str, "S%X", kind);
	g_string_append_printf(str, "%02X", (guint)(buf_addr->len + bufsz + 1));
	for (guint i = 0; i < buf_addr->len; i++)
		g_string_append_printf(str, "%02X", buf_addr->data[i]);
	for (guint i = 0; i < bufsz; i++)
		g_string_append_printf(str, "%02X", buf[i]);
	g_string_append_printf(str, "%02X\n", csum);
}

static GBytes *
fu_srec_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GString) str = g_string_new(NULL);
	g_autoptr(GBytes) buf_blob = NULL;
	const gchar *id = fu_firmware_get_id(firmware);
	gsize id_strlen = id != NULL ? strlen(id) : 0;
	FuFirmareSrecRecordKind kind_data = FU_FIRMWARE_SREC_RECORD_KIND_S1_DATA_16;
	FuFirmareSrecRecordKind kind_coun = FU_FIRMWARE_SREC_RECORD_KIND_S5_COUNT_16;
	FuFirmareSrecRecordKind kind_term = FU_FIRMWARE_SREC_RECORD_KIND_S9_TERMINATION_16;

	/* upgrade to longer addresses? */
	if (fu_firmware_get_addr(firmware) >= (1ull << 24)) {
		kind_data = FU_FIRMWARE_SREC_RECORD_KIND_S3_DATA_32;
		kind_coun = FU_FIRMWARE_SREC_RECORD_KIND_S6_COUNT_24;
		kind_term = FU_FIRMWARE_SREC_RECORD_KIND_S7_COUNT_32; /* intentional... */
	} else if (fu_firmware_get_addr(firmware) >= (1ull << 16)) {
		kind_data = FU_FIRMWARE_SREC_RECORD_KIND_S2_DATA_24;
		kind_coun = FU_FIRMWARE_SREC_RECORD_KIND_S6_COUNT_24;
		kind_term = FU_FIRMWARE_SREC_RECORD_KIND_S8_TERMINATION_24;
	}

	/* main blob */
	buf_blob = fu_firmware_get_bytes(firmware, error);
	if (buf_blob == NULL)
		return NULL;

	/* header */
	fu_srec_firmware_write_line(str,
				    FU_FIRMWARE_SREC_RECORD_KIND_S0_HEADER,
				    0x0,
				    (const guint8 *)id,
				    id_strlen);

	/* payload */
	if (g_bytes_get_size(buf_blob) > 0) {
		g_autoptr(GPtrArray) chunks = NULL;
		chunks = fu_chunk_array_new_from_bytes(buf_blob,
						       fu_firmware_get_addr(firmware),
						       0x0,
						       64);
		for (guint i = 0; i < chunks->len; i++) {
			FuChunk *chk = g_ptr_array_index(chunks, i);
			fu_srec_firmware_write_line(str,
						    kind_data,
						    fu_chunk_get_address(chk),
						    fu_chunk_get_data(chk),
						    fu_chunk_get_data_sz(chk));
		}
		fu_srec_firmware_write_line(str, kind_coun, chunks->len, NULL, 0);
	}

	/* EOF */
	fu_srec_firmware_write_line(str, kind_term, 0x0, NULL, 0);

	/* success */
	return g_string_free_to_bytes(g_steal_pointer(&str));
}

static void
fu_srec_firmware_finalize(GObject *object)
{
	FuSrecFirmware *self = FU_SREC_FIRMWARE(object);
	FuSrecFirmwarePrivate *priv = GET_PRIVATE(self);
	g_ptr_array_unref(priv->records);
	G_OBJECT_CLASS(fu_srec_firmware_parent_class)->finalize(object);
}

static void
fu_srec_firmware_init(FuSrecFirmware *self)
{
	FuSrecFirmwarePrivate *priv = GET_PRIVATE(self);
	priv->records = g_ptr_array_new_with_free_func((GFreeFunc)fu_srec_firmware_record_free);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
}

static void
fu_srec_firmware_class_init(FuSrecFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_srec_firmware_finalize;
	klass_firmware->parse = fu_srec_firmware_parse;
	klass_firmware->tokenize = fu_srec_firmware_tokenize;
	klass_firmware->write = fu_srec_firmware_write;
}

/**
 * fu_srec_firmware_new:
 *
 * Creates a new #FuFirmware of type SREC
 *
 * Since: 1.3.2
 **/
FuFirmware *
fu_srec_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_SREC_FIRMWARE, NULL));
}
