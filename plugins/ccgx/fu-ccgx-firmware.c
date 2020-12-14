/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-common.h"
#include "fu-common-version.h"
#include "fu-firmware-common.h"

#include "fu-ccgx-common.h"
#include "fu-ccgx-firmware.h"

struct _FuCcgxFirmware {
	FuFirmwareClass		 parent_instance;
	GPtrArray		*records;
	guint16			 app_type;
	guint16			 silicon_id;
	FWMode			 fw_mode;
};

G_DEFINE_TYPE (FuCcgxFirmware, fu_ccgx_firmware, FU_TYPE_FIRMWARE)

/* offset stored application version for CCGx */
#define CCGX_APP_VERSION_OFFSET		228  /* 128+64+32+4 */

GPtrArray *
fu_ccgx_firmware_get_records (FuCcgxFirmware *self)
{
	g_return_val_if_fail (FU_IS_CCGX_FIRMWARE (self), NULL);
	return self->records;
}

guint16
fu_ccgx_firmware_get_app_type (FuCcgxFirmware *self)
{
	g_return_val_if_fail (FU_IS_CCGX_FIRMWARE (self), 0);
	return self->app_type;
}

guint16
fu_ccgx_firmware_get_silicon_id (FuCcgxFirmware *self)
{
	g_return_val_if_fail (FU_IS_CCGX_FIRMWARE (self), 0);
	return self->silicon_id;
}

FWMode
fu_ccgx_firmware_get_fw_mode (FuCcgxFirmware *self)
{
	g_return_val_if_fail (FU_IS_CCGX_FIRMWARE (self), 0);
	return self->fw_mode;
}

static void
fu_ccgx_firmware_to_string (FuFirmware *firmware, guint idt, GString *str)
{
	FuCcgxFirmware *self = FU_CCGX_FIRMWARE (firmware);
	fu_common_string_append_kx (str, idt, "AppType", self->app_type);
	fu_common_string_append_kx (str, idt, "SiliconId", self->silicon_id);
	fu_common_string_append_ku (str, idt, "Records", self->records->len);
	fu_common_string_append_kv (str, idt, "FWMode",
				    fu_ccgx_fw_mode_to_string (self->fw_mode));
}

static void
fu_ccgx_firmware_record_free (FuCcgxFirmwareRecord *rcd)
{
	if (rcd->data != NULL)
		g_bytes_unref (rcd->data);
	g_free (rcd);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuCcgxFirmwareRecord, fu_ccgx_firmware_record_free)

static gboolean
fu_ccgx_firmware_add_record (FuCcgxFirmware *self, const gchar *line, GError **error)
{
	guint16 linesz = strlen (line);
	guint16 buflen;
	guint8 checksum_file;
	guint8 checksum_calc = 0;
	g_autoptr(FuCcgxFirmwareRecord) rcd = NULL;
	g_autoptr(GByteArray) data = g_byte_array_new ();

	/* https://community.cypress.com/docs/DOC-10562 */
	if (linesz < 12) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "invalid record, expected >= 12 chars");
		return FALSE;
	}

	/* parse */
	rcd = g_new0 (FuCcgxFirmwareRecord, 1);
	rcd->array_id = fu_firmware_strparse_uint8 (line + 0);
	rcd->row_number = fu_firmware_strparse_uint16 (line + 2);
	buflen = fu_firmware_strparse_uint16 (line + 6);
	if (linesz != (buflen * 2) + 12) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "invalid record, expected %u chars, got %u",
			     (guint) (buflen * 2) + 12, linesz);
		return FALSE;
	}

	/* parse payload, adding checksum */
	for (guint i = 0; i < buflen; i++) {
		guint8 tmp = fu_firmware_strparse_uint8 (line + 10 + (i * 2));
		fu_byte_array_append_uint8 (data, tmp);
		checksum_calc += tmp;
	}
	rcd->data = g_byte_array_free_to_bytes (g_steal_pointer (&data));

	/* verify 2s complement checksum */
	checksum_file = fu_firmware_strparse_uint8 (line + (buflen * 2) + 10);
	for (guint i = 0; i < 5; i++) {
		guint8 tmp = fu_firmware_strparse_uint8 (line + (i * 2));
		checksum_calc += tmp;
	}
	checksum_calc = 1 + ~checksum_calc;
	if (checksum_file != checksum_calc)  {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "checksum invalid, got %02x, expected %02x",
			     checksum_calc, checksum_file);
		return FALSE;
	}

	/* success */
	g_ptr_array_add (self->records, g_steal_pointer (&rcd));
	return TRUE;
}

static gboolean
fu_ccgx_firmware_parse_md_block (FuCcgxFirmware *self, FuFirmwareImage *img, GError **error)
{
	FuCcgxFirmwareRecord *rcd;
	CCGxMetaData metadata;
	const guint8 *buf;
	gsize bufsz = 0;
	gsize md_offset = 0;
	guint32 fw_size = 0;
	guint32	rcd_version_idx = 0;
	guint32 version = 0;
	guint8 checksum_calc = 0;
	g_autofree gchar *version_str = NULL;

	/* sanity check */
	if (self->records->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "no records added to image");
		return FALSE;
	}

	/* read metadata from correct ofsset */
	rcd = g_ptr_array_index (self->records, self->records->len - 1);
	buf = g_bytes_get_data (rcd->data, &bufsz);
	if (bufsz == 0) {
		g_set_error_literal (error,
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
	if (!fu_memcpy_safe ((guint8 *) &metadata, sizeof(metadata), 0x0, /* dst */
			     buf, bufsz, md_offset, sizeof(metadata), error)) /* src */
		return FALSE;

	/* sanity check */
	if (metadata.metadata_valid != CCGX_METADATA_VALID_SIG) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "invalid metadata 0x@%x, expected 0x%04x, got 0x%04x",
			     (guint) md_offset,
			     (guint) CCGX_METADATA_VALID_SIG,
			     (guint) metadata.metadata_valid);
		return FALSE;
	}
	for (guint i = 0; i < self->records->len - 1; i++) {
		rcd = g_ptr_array_index (self->records, i);
		buf = g_bytes_get_data (rcd->data, &bufsz);
		fw_size += bufsz;
		for (gsize j = 0; j < bufsz; j++)
			checksum_calc += buf[j];
	}
	if (fw_size != metadata.fw_size)  {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "firmware size invalid, got %02x, expected %02x",
			     fw_size, metadata.fw_size);
		return FALSE;
	}
	checksum_calc = 1 + ~checksum_calc;
	if (metadata.fw_checksum != checksum_calc)  {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "checksum invalid, got %02x, expected %02x",
			     checksum_calc, metadata.fw_checksum);
		return FALSE;
	}

	/* get version */
	rcd_version_idx = CCGX_APP_VERSION_OFFSET / bufsz;
	if (rcd_version_idx >= self->records->len) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "invalid version index of %02x",
			     rcd_version_idx);
		return FALSE;
	}
	rcd = g_ptr_array_index (self->records, rcd_version_idx);
	buf = g_bytes_get_data (rcd->data, &bufsz);
	if (!fu_common_read_uint32_safe (buf, bufsz, CCGX_APP_VERSION_OFFSET % bufsz,
					 &version, G_LITTLE_ENDIAN, error))
		return FALSE;
	self->app_type = version & 0xffff;
	version_str = fu_ccgx_version_to_string (version);
	fu_firmware_set_version (FU_FIRMWARE (self), version_str);

	/* work out the FWMode */
	if (self->records->len > 0) {
		rcd = g_ptr_array_index (self->records, self->records->len - 1);
		if ((rcd->row_number & 0xFF) == 0xFF) /* last row */
			self->fw_mode = FW_MODE_FW1;
		if ((rcd->row_number & 0xFF) == 0xFE) /* penultimate row */
			self->fw_mode = FW_MODE_FW2;
	}
	return TRUE;
}

static gboolean
fu_ccgx_firmware_parse (FuFirmware *firmware,
			GBytes *fw,
			guint64 addr_start,
			guint64 addr_end,
			FwupdInstallFlags flags,
			GError **error)
{
	FuCcgxFirmware *self = FU_CCGX_FIRMWARE (firmware);
	gsize sz = 0;
	const gchar *data = g_bytes_get_data (fw, &sz);
	g_auto(GStrv) lines = fu_common_strnsplit (data, sz, "\n", -1);
	g_autoptr(FuFirmwareImage) img = fu_firmware_image_new (fw);

	/* parse header */
	g_strdelimit (lines[0], "\r\x1a", '\0');
	if (lines[0] == NULL || strlen (lines[0]) != 12) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "invalid header, expected == 12 chars -- got %s",
			     lines[0]);
		return FALSE;
	}
	self->silicon_id = fu_firmware_strparse_uint32 (lines[0]) >> 16;

	/* parse data */
	for (guint ln = 1; lines[ln] != NULL; ln++) {
		g_strdelimit (lines[ln], "\r\x1a", '\0');
		if (lines[ln][0] == '\0')
			continue;
		if (!fu_ccgx_firmware_add_record (self, lines[ln] + 1, error)) {
			g_prefix_error (error, "error on line %u: ", ln + 1);
			return FALSE;
		}
	}

	/* address is first data entry */
	if (self->records->len > 0) {
		FuCcgxFirmwareRecord *rcd = g_ptr_array_index (self->records, 0);
		fu_firmware_image_set_addr (img, rcd->row_number);
	}

	/* parse metadata block */
	if (!fu_ccgx_firmware_parse_md_block (self, img, error))
		return FALSE;

	/* add something, although we'll use the records for the update */
	fu_firmware_add_image (firmware, img);
	return TRUE;
}

static void
fu_ccgx_firmware_init (FuCcgxFirmware *self)
{
	self->records = g_ptr_array_new_with_free_func ((GFreeFunc) fu_ccgx_firmware_record_free);
}

static void
fu_ccgx_firmware_finalize (GObject *object)
{
	FuCcgxFirmware *self = FU_CCGX_FIRMWARE (object);
	g_ptr_array_unref (self->records);
	G_OBJECT_CLASS (fu_ccgx_firmware_parent_class)->finalize (object);
}

static void
fu_ccgx_firmware_class_init (FuCcgxFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	object_class->finalize = fu_ccgx_firmware_finalize;
	klass_firmware->parse = fu_ccgx_firmware_parse;
	klass_firmware->to_string = fu_ccgx_firmware_to_string;
}

FuFirmware *
fu_ccgx_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_CCGX_FIRMWARE, NULL));
}
