/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <string.h>

#include "fu-ccgx-common.h"
#include "fu-ccgx-firmware.h"

struct _FuCcgxFirmware {
	FuFirmwareClass parent_instance;
	GPtrArray *records;
	guint16 app_type;
	guint16 silicon_id;
	FWMode fw_mode;
};

G_DEFINE_TYPE(FuCcgxFirmware, fu_ccgx_firmware, FU_TYPE_FIRMWARE)

/* offset stored application version for CCGx */
#define CCGX_APP_VERSION_OFFSET 228 /* 128+64+32+4 */

#define FU_CCGX_FIRMWARE_TOKENS_MAX 100000 /* lines */

GPtrArray *
fu_ccgx_firmware_get_records(FuCcgxFirmware *self)
{
	g_return_val_if_fail(FU_IS_CCGX_FIRMWARE(self), NULL);
	return self->records;
}

guint16
fu_ccgx_firmware_get_app_type(FuCcgxFirmware *self)
{
	g_return_val_if_fail(FU_IS_CCGX_FIRMWARE(self), 0);
	return self->app_type;
}

guint16
fu_ccgx_firmware_get_silicon_id(FuCcgxFirmware *self)
{
	g_return_val_if_fail(FU_IS_CCGX_FIRMWARE(self), 0);
	return self->silicon_id;
}

FWMode
fu_ccgx_firmware_get_fw_mode(FuCcgxFirmware *self)
{
	g_return_val_if_fail(FU_IS_CCGX_FIRMWARE(self), 0);
	return self->fw_mode;
}

static void
fu_ccgx_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuCcgxFirmware *self = FU_CCGX_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "silicon_id", self->silicon_id);
	if (flags & FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG) {
		fu_xmlb_builder_insert_kx(bn, "app_type", self->app_type);
		fu_xmlb_builder_insert_kx(bn, "records", self->records->len);
		fu_xmlb_builder_insert_kv(bn, "fw_mode", fu_ccgx_fw_mode_to_string(self->fw_mode));
	}
}

static void
fu_ccgx_firmware_record_free(FuCcgxFirmwareRecord *rcd)
{
	if (rcd->data != NULL)
		g_bytes_unref(rcd->data);
	g_free(rcd);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuCcgxFirmwareRecord, fu_ccgx_firmware_record_free)

static gboolean
fu_ccgx_firmware_add_record(FuCcgxFirmware *self,
			    GString *token,
			    FwupdInstallFlags flags,
			    GError **error)
{
	guint16 buflen;
	guint8 checksum_calc = 0;
	g_autoptr(FuCcgxFirmwareRecord) rcd = NULL;
	g_autoptr(GByteArray) data = g_byte_array_new();

	/* this is not in the specification, but exists in reality */
	if (token->str[0] == ':')
		g_string_erase(token, 0, 1);

	/* parse according to https://community.cypress.com/docs/DOC-10562 */
	rcd = g_new0(FuCcgxFirmwareRecord, 1);
	if (!fu_firmware_strparse_uint8_safe(token->str, token->len, 0, &rcd->array_id, error))
		return FALSE;
	if (!fu_firmware_strparse_uint16_safe(token->str, token->len, 2, &rcd->row_number, error))
		return FALSE;
	if (!fu_firmware_strparse_uint16_safe(token->str, token->len, 6, &buflen, error))
		return FALSE;
	if (token->len != ((gsize)buflen * 2) + 12) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "invalid record, expected %u chars, got %u",
			    (guint)(buflen * 2) + 12,
			    (guint)token->len);
		return FALSE;
	}

	/* parse payload, adding checksum */
	for (guint i = 0; i < buflen; i++) {
		guint8 tmp = 0;
		if (!fu_firmware_strparse_uint8_safe(token->str,
						     token->len,
						     10 + (i * 2),
						     &tmp,
						     error))
			return FALSE;
		fu_byte_array_append_uint8(data, tmp);
		checksum_calc += tmp;
	}
	rcd->data = g_byte_array_free_to_bytes(g_steal_pointer(&data));

	/* verify 2s complement checksum */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		guint8 checksum_file;
		if (!fu_firmware_strparse_uint8_safe(token->str,
						     token->len,
						     (buflen * 2) + 10,
						     &checksum_file,
						     error))
			return FALSE;
		for (guint i = 0; i < 5; i++) {
			guint8 tmp = 0;
			if (!fu_firmware_strparse_uint8_safe(token->str,
							     token->len,
							     i * 2,
							     &tmp,
							     error))
				return FALSE;
			checksum_calc += tmp;
		}
		checksum_calc = 1 + ~checksum_calc;
		if (checksum_file != checksum_calc) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "checksum invalid, got %02x, expected %02x",
				    checksum_calc,
				    checksum_file);
			return FALSE;
		}
	}

	/* success */
	g_ptr_array_add(self->records, g_steal_pointer(&rcd));
	return TRUE;
}

static gboolean
fu_ccgx_firmware_parse_md_block(FuCcgxFirmware *self, FwupdInstallFlags flags, GError **error)
{
	FuCcgxFirmwareRecord *rcd;
	CCGxMetaData metadata;
	const guint8 *buf;
	gsize bufsz = 0;
	gsize md_offset = 0;
	guint32 fw_size = 0;
	guint32 rcd_version_idx = 0;
	guint32 version = 0;
	guint8 checksum_calc = 0;

	/* sanity check */
	if (self->records->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no records added to image");
		return FALSE;
	}

	/* read metadata from correct ofsset */
	rcd = g_ptr_array_index(self->records, self->records->len - 1);
	buf = g_bytes_get_data(rcd->data, &bufsz);
	if (bufsz == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "invalid buffer size");
		return FALSE;
	}
	switch (bufsz) {
	case 0x80:
		md_offset = 0x40;
		break;
	case 0x100:
		md_offset = 0xC0;
		break;
	default:
		break;
	}
	if (!fu_memcpy_safe((guint8 *)&metadata,
			    sizeof(metadata),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    md_offset,
			    sizeof(metadata),
			    error)) /* src */
		return FALSE;

	/* sanity check */
	if (metadata.metadata_valid != CCGX_METADATA_VALID_SIG) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "invalid metadata 0x@%x, expected 0x%04x, got 0x%04x",
			    (guint)md_offset,
			    (guint)CCGX_METADATA_VALID_SIG,
			    (guint)metadata.metadata_valid);
		return FALSE;
	}
	for (guint i = 0; i < self->records->len - 1; i++) {
		rcd = g_ptr_array_index(self->records, i);
		checksum_calc += fu_sum8_bytes(rcd->data);
		fw_size += g_bytes_get_size(rcd->data);
	}
	if (fw_size != metadata.fw_size) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware size invalid, got %02x, expected %02x",
			    fw_size,
			    metadata.fw_size);
		return FALSE;
	}
	checksum_calc = 1 + ~checksum_calc;
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		if (metadata.fw_checksum != checksum_calc) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "checksum invalid, got %02x, expected %02x",
				    checksum_calc,
				    metadata.fw_checksum);
			return FALSE;
		}
	}

	/* get version if enough data */
	rcd_version_idx = CCGX_APP_VERSION_OFFSET / bufsz;
	if (rcd_version_idx < self->records->len) {
		g_autofree gchar *version_str = NULL;
		rcd = g_ptr_array_index(self->records, rcd_version_idx);
		buf = g_bytes_get_data(rcd->data, &bufsz);
		if (bufsz == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "metadata record had zero size");
			return FALSE;
		}
		if (!fu_memread_uint32_safe(buf,
					    bufsz,
					    CCGX_APP_VERSION_OFFSET % bufsz,
					    &version,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		self->app_type = version & 0xffff;
		version_str = fu_ccgx_version_to_string(version);
		fu_firmware_set_version(FU_FIRMWARE(self), version_str);
		fu_firmware_set_version_raw(FU_FIRMWARE(self), version);
	}

	/* work out the FWMode */
	if (self->records->len > 0) {
		rcd = g_ptr_array_index(self->records, self->records->len - 1);
		if ((rcd->row_number & 0xFF) == 0xFF) /* last row */
			self->fw_mode = FW_MODE_FW1;
		if ((rcd->row_number & 0xFF) == 0xFE) /* penultimate row */
			self->fw_mode = FW_MODE_FW2;
	}
	return TRUE;
}

typedef struct {
	FuCcgxFirmware *self;
	FwupdInstallFlags flags;
} FuCcgxFirmwareTokenHelper;

static gboolean
fu_ccgx_firmware_tokenize_cb(GString *token, guint token_idx, gpointer user_data, GError **error)
{
	FuCcgxFirmwareTokenHelper *helper = (FuCcgxFirmwareTokenHelper *)user_data;
	FuCcgxFirmware *self = FU_CCGX_FIRMWARE(helper->self);

	/* sanity check */
	if (token_idx > FU_CCGX_FIRMWARE_TOKENS_MAX) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "file has too many lines");
		return FALSE;
	}

	/* remove WIN32 line endings */
	g_strdelimit(token->str, "\r\x1a", '\0');
	token->len = strlen(token->str);

	/* header */
	if (token_idx == 0) {
		guint32 device_id = 0;
		if (token->len != 12) {
			g_autofree gchar *strsafe = fu_strsafe(token->str, 12);
			if (strsafe != NULL) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "invalid header, expected == 12 chars -- got %s",
					    strsafe);
				return FALSE;
			}
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "invalid header, expected == 12 chars");
			return FALSE;
		}
		if (!fu_firmware_strparse_uint32_safe(token->str, token->len, 0, &device_id, error))
			return FALSE;
		self->silicon_id = device_id >> 16;
		return TRUE;
	}

	/* ignore blank lines */
	if (token->len == 0)
		return TRUE;

	/* parse record */
	if (!fu_ccgx_firmware_add_record(self, token, helper->flags, error)) {
		g_prefix_error(error, "error on line %u: ", token_idx + 1);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_ccgx_firmware_parse(FuFirmware *firmware,
		       GBytes *fw,
		       gsize offset,
		       FwupdInstallFlags flags,
		       GError **error)
{
	FuCcgxFirmware *self = FU_CCGX_FIRMWARE(firmware);
	FuCcgxFirmwareTokenHelper helper = {.self = self, .flags = flags};

	/* tokenize */
	if (!fu_strsplit_full(g_bytes_get_data(fw, NULL),
			      g_bytes_get_size(fw),
			      "\n",
			      fu_ccgx_firmware_tokenize_cb,
			      &helper,
			      error))
		return FALSE;

	/* address is first data entry */
	if (self->records->len > 0) {
		FuCcgxFirmwareRecord *rcd = g_ptr_array_index(self->records, 0);
		fu_firmware_set_addr(firmware, rcd->row_number);
	}

	/* parse metadata block */
	if (!fu_ccgx_firmware_parse_md_block(self, flags, error)) {
		g_prefix_error(error, "failed to parse metadata: ");
		return FALSE;
	}

	/* add something, although we'll use the records for the update */
	fu_firmware_set_bytes(firmware, fw);
	return TRUE;
}

static void
fu_ccgx_firmware_write_record(GString *str,
			      guint8 array_id,
			      guint8 row_number,
			      const guint8 *buf,
			      guint16 bufsz)
{
	guint8 checksum_calc = 0xff;
	g_autoptr(GString) datastr = g_string_new(NULL);

	/* offset for bootloader perhaps? */
	row_number += 0xE;

	checksum_calc += array_id;
	checksum_calc += row_number;
	checksum_calc += bufsz & 0xff;
	checksum_calc += (bufsz >> 8) & 0xff;
	for (guint j = 0; j < bufsz; j++) {
		g_string_append_printf(datastr, "%02X", buf[j]);
		checksum_calc += buf[j];
	}
	g_string_append_printf(str,
			       ":%02X%04X%04X%s%02X\n",
			       array_id,
			       row_number,
			       bufsz,
			       datastr->str,
			       (guint)((guint8)~checksum_calc));
}

static GBytes *
fu_ccgx_firmware_write(FuFirmware *firmware, GError **error)
{
	FuCcgxFirmware *self = FU_CCGX_FIRMWARE(firmware);
	CCGxMetaData metadata = {0x0};
	gsize fwbufsz = 0;
	guint8 checksum_img = 0xff;
	const guint8 *fwbuf;
	g_autoptr(GByteArray) mdbuf = g_byte_array_new();
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(GString) str = g_string_new(NULL);

	/* header record */
	g_string_append_printf(str,
			       "%04X%04X%02X%02X\n",
			       self->silicon_id,
			       (guint)0x11AF, /* SiliconID */
			       (guint)0x0,    /* SiliconRev */
			       (guint)0x0);   /* Checksum, or 0x0 */

	/* add image in chunks */
	fw = fu_firmware_get_bytes_with_patches(firmware, error);
	if (fw == NULL)
		return NULL;
	chunks = fu_chunk_array_new_from_bytes(fw, 0x0, 0x0, 0x100);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		fu_ccgx_firmware_write_record(str,
					      0x0,
					      i,
					      fu_chunk_get_data(chk),
					      fu_chunk_get_data_sz(chk));
	}

	/* add metadata */
	fwbuf = g_bytes_get_data(fw, &fwbufsz);
	for (guint j = 0; j < fwbufsz; j++)
		checksum_img += fwbuf[j];
	metadata.fw_checksum = ~checksum_img;
	metadata.fw_entry = 0x0; /* unknown */
	metadata.last_boot_row = 0x13;
	metadata.fw_size = fwbufsz;
	metadata.metadata_valid = CCGX_METADATA_VALID_SIG;
	metadata.boot_seq = 0x0; /* unknown */

	/* copy into place */
	fu_byte_array_set_size(mdbuf, 0x80, 0x00);
	if (!fu_memcpy_safe(mdbuf->data,
			    mdbuf->len,
			    0x40, /* dst */
			    (const guint8 *)&metadata,
			    sizeof(metadata),
			    0x0, /* src */
			    sizeof(metadata),
			    error))
		return NULL;
	fu_ccgx_firmware_write_record(str,
				      0x0,
				      0xFE, /* FW2: penultimate row  */
				      mdbuf->data,
				      mdbuf->len);

	return g_string_free_to_bytes(g_steal_pointer(&str));
}

static gboolean
fu_ccgx_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuCcgxFirmware *self = FU_CCGX_FIRMWARE(firmware);
	guint64 tmp;

	/* optional properties */
	tmp = xb_node_query_text_as_uint(n, "silicon_id", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		self->silicon_id = tmp;

	/* success */
	return TRUE;
}

static void
fu_ccgx_firmware_init(FuCcgxFirmware *self)
{
	self->records = g_ptr_array_new_with_free_func((GFreeFunc)fu_ccgx_firmware_record_free);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_VID_PID);
}

static void
fu_ccgx_firmware_finalize(GObject *object)
{
	FuCcgxFirmware *self = FU_CCGX_FIRMWARE(object);
	g_ptr_array_unref(self->records);
	G_OBJECT_CLASS(fu_ccgx_firmware_parent_class)->finalize(object);
}

static void
fu_ccgx_firmware_class_init(FuCcgxFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_ccgx_firmware_finalize;
	klass_firmware->parse = fu_ccgx_firmware_parse;
	klass_firmware->write = fu_ccgx_firmware_write;
	klass_firmware->build = fu_ccgx_firmware_build;
	klass_firmware->export = fu_ccgx_firmware_export;
}

FuFirmware *
fu_ccgx_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_CCGX_FIRMWARE, NULL));
}
