/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"
#include "fu-common-version.h"
#include "fu-firmware-common.h"

#include "fu-ccgx-common.h"
#include "fu-ccgx-cyacd-firmware-image.h"

/* offset stored appication version for CCGx */
#define CCGX_APP_VERSION_OFFSET		228  /* 128+64+32+4 */

struct _FuCcgxCyacdFirmwareImage {
	FuFirmwareImageClass	 parent_instance;
	GPtrArray		*records;
};

G_DEFINE_TYPE (FuCcgxCyacdFirmwareImage, fu_ccgx_cyacd_firmware_image, FU_TYPE_FIRMWARE_IMAGE)

GPtrArray *
fu_ccgx_cyacd_firmware_image_get_records (FuCcgxCyacdFirmwareImage *self)
{
	g_return_val_if_fail (FU_IS_CCGX_CYACD_FIRMWARE_IMAGE (self), NULL);
	return self->records;
}

static void
fu_ccgx_cyacd_firmware_image_record_free (FuCcgxCyacdFirmwareImageRecord *rcd)
{
	if (rcd->data != NULL)
		g_bytes_unref (rcd->data);
	g_free (rcd);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuCcgxCyacdFirmwareImageRecord, fu_ccgx_cyacd_firmware_image_record_free)

gboolean
fu_ccgx_cyacd_firmware_image_parse_md_block (FuCcgxCyacdFirmwareImage *self, GError **error)
{
	FuCcgxCyacdFirmwareImageRecord *rcd;
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
	version_str = fu_common_version_from_uint32 (version, FWUPD_VERSION_FORMAT_QUAD);
	fu_firmware_image_set_version (FU_FIRMWARE_IMAGE (self), version_str);
	return TRUE;
}

gboolean
fu_ccgx_cyacd_firmware_image_parse_header (FuCcgxCyacdFirmwareImage *self,
					   const gchar *line,
					   GError **error)
{
	if (strlen (line) != 12) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "invalid header, expected == 12 chars");
		return FALSE;
	}
	fu_firmware_image_set_addr (FU_FIRMWARE_IMAGE (self),
				    fu_firmware_strparse_uint32 (line));
	return TRUE;
}

gboolean
fu_ccgx_cyacd_firmware_image_add_record (FuCcgxCyacdFirmwareImage *self,
					 const gchar *line, GError **error)
{
	guint16 linesz = strlen (line);
	guint16 buflen;
	guint8 checksum_file;
	guint8 checksum_calc = 0;
	g_autoptr(FuCcgxCyacdFirmwareImageRecord) rcd = NULL;
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
	rcd = g_new0 (FuCcgxCyacdFirmwareImageRecord, 1);
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

static void
fu_ccgx_cyacd_firmware_image_init (FuCcgxCyacdFirmwareImage *self)
{
	self->records = g_ptr_array_new_with_free_func ((GFreeFunc) fu_ccgx_cyacd_firmware_image_record_free);
}

static void
fu_ccgx_cyacd_firmware_image_finalize (GObject *object)
{
	FuCcgxCyacdFirmwareImage *self = FU_CCGX_CYACD_FIRMWARE_IMAGE (object);
	g_ptr_array_unref (self->records);
	G_OBJECT_CLASS (fu_ccgx_cyacd_firmware_image_parent_class)->finalize (object);
}

static void
fu_ccgx_cyacd_firmware_image_class_init (FuCcgxCyacdFirmwareImageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_ccgx_cyacd_firmware_image_finalize;
}

FuFirmwareImage *
fu_ccgx_cyacd_firmware_image_new (void)
{
	return FU_FIRMWARE_IMAGE (g_object_new (FU_TYPE_CCGX_CYACD_FIRMWARE_IMAGE, NULL));
}
