/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elantp-common.h"
#include "fu-elantp-firmware.h"
#include "fu-elantp-struct.h"

struct _FuElantpFirmware {
	FuFirmware parent_instance;
	guint16 module_id;
	guint16 ic_type;
	guint16 iap_addr;
	guint16 iap_ver;
	gboolean force_table_support;
	guint32 force_table_addr;
};

G_DEFINE_TYPE(FuElantpFirmware, fu_elantp_firmware, FU_TYPE_FIRMWARE)

#define FU_ELANTP_FIRMWARE_OFFSET_IAP 0x100 /* bytes */

guint16
fu_elantp_firmware_get_module_id(FuElantpFirmware *self)
{
	g_return_val_if_fail(FU_IS_ELANTP_FIRMWARE(self), 0);
	return self->module_id;
}

guint16
fu_elantp_firmware_get_ic_type(FuElantpFirmware *self)
{
	g_return_val_if_fail(FU_IS_ELANTP_FIRMWARE(self), 0);
	return self->ic_type;
}

guint16
fu_elantp_firmware_get_iap_addr(FuElantpFirmware *self)
{
	g_return_val_if_fail(FU_IS_ELANTP_FIRMWARE(self), 0);
	return self->iap_addr;
}

gboolean
fu_elantp_firmware_get_forcetable_support(FuElantpFirmware *self)
{
	g_return_val_if_fail(FU_IS_ELANTP_FIRMWARE(self), FALSE);
	return self->force_table_support;
}

guint32
fu_elantp_firmware_get_forcetable_addr(FuElantpFirmware *self)
{
	g_return_val_if_fail(FU_IS_ELANTP_FIRMWARE(self), 0);
	return self->force_table_addr;
}

static void
fu_elantp_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuElantpFirmware *self = FU_ELANTP_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "ic_type", self->ic_type);
	fu_xmlb_builder_insert_kx(bn, "iap_addr", self->iap_addr);
	fu_xmlb_builder_insert_kx(bn, "module_id", self->module_id);
}

static gboolean
fu_elantp_firmware_validate(FuFirmware *firmware,
			    GInputStream *stream,
			    gsize offset,
			    GError **error)
{
	gsize streamsz = 0;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz < FU_STRUCT_ELANTP_FIRMWARE_FTR_SIZE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "stream was too small");
		return FALSE;
	}
	if (!fu_struct_elantp_firmware_ftr_validate_stream(stream,
							   streamsz -
							       FU_STRUCT_ELANTP_FIRMWARE_FTR_SIZE,
							   error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_firmware_parse(FuFirmware *firmware,
			 GInputStream *stream,
			 FuFirmwareParseFlags flags,
			 GError **error)
{
	FuElantpFirmware *self = FU_ELANTP_FIRMWARE(firmware);
	guint16 force_table_addr_wrds;
	guint16 module_id_wrds;
	guint32 iap_addr_tmp;
	g_autoptr(FuStructElantpFirmwareHdr) st = NULL;

	st = fu_struct_elantp_firmware_hdr_parse_stream(stream,
							FU_ELANTP_FIRMWARE_OFFSET_IAP,
							error);
	if (st == NULL)
		return FALSE;

	/* convert from words */
	iap_addr_tmp = (guint32)fu_struct_elantp_firmware_hdr_get_iap_start(st) * 2;
	if (iap_addr_tmp >= G_MAXUINT16 ||
	    iap_addr_tmp < (FU_ELANTP_FIRMWARE_OFFSET_IAP + FU_STRUCT_ELANTP_FIRMWARE_HDR_SIZE)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "IAP address invalid: 0x%x",
			    iap_addr_tmp);
		return FALSE;
	}
	self->iap_addr = (guint16)iap_addr_tmp;
	self->ic_type = fu_struct_elantp_firmware_hdr_get_ic_type(st);
	self->iap_ver = fu_struct_elantp_firmware_hdr_get_iap_ver(st);

	/* read module ID */
	if (!fu_input_stream_read_u16(stream,
				      self->iap_addr,
				      &module_id_wrds,
				      G_LITTLE_ENDIAN,
				      error))
		return FALSE;
	if (module_id_wrds == 0xFFFF) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "module ID address invalid: 0x%x",
			    module_id_wrds);
		return FALSE;
	}
	if (!fu_input_stream_read_u16(stream,
				      module_id_wrds * 2,
				      &self->module_id,
				      G_LITTLE_ENDIAN,
				      error))
		return FALSE;

	/* get the forcetable address */
	if (self->ic_type == FU_ETP_IC_NUM12 || self->ic_type == FU_ETP_IC_NUM13 ||
	    self->ic_type == FU_ETP_IC_NUM14 || self->ic_type == FU_ETP_IC_NUM15) {
		if (self->iap_ver <= 4) {
			if (!fu_input_stream_read_u16(stream,
						      self->iap_addr + 6,
						      &force_table_addr_wrds,
						      G_LITTLE_ENDIAN,
						      error))
				return FALSE;
		} else {
			force_table_addr_wrds =
			    fu_struct_elantp_firmware_hdr_get_iap_forcetable(st);
		}
		if (force_table_addr_wrds == 0xFFFF || force_table_addr_wrds % 32 != 0 ||
		    force_table_addr_wrds >= 0x7FFF) {
			self->force_table_support = FALSE;
		} else {
			self->force_table_addr = force_table_addr_wrds * 2;
			self->force_table_support = TRUE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuElantpFirmware *self = FU_ELANTP_FIRMWARE(firmware);
	guint64 tmp;

	/* optional properties */
	tmp = xb_node_query_text_as_uint(n, "ic_type", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		self->ic_type = tmp;
	tmp = xb_node_query_text_as_uint(n, "module_id", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		self->module_id = tmp;
	tmp = xb_node_query_text_as_uint(n, "iap_addr", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		self->iap_addr = tmp;

	/* success */
	return TRUE;
}

static GByteArray *
fu_elantp_firmware_write(FuFirmware *firmware, GError **error)
{
	FuElantpFirmware *self = FU_ELANTP_FIRMWARE(firmware);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuStructElantpFirmwareHdr) st_hdr = fu_struct_elantp_firmware_hdr_new();
	g_autoptr(FuStructElantpFirmwareFtr) st_ftr = fu_struct_elantp_firmware_ftr_new();

	/* sanity check */
	if (self->iap_addr < FU_ELANTP_FIRMWARE_OFFSET_IAP + FU_STRUCT_ELANTP_FIRMWARE_HDR_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "IAP address 0x%x would truncate header",
			    self->iap_addr);
		return NULL;
	}

	/* header */
	fu_byte_array_set_size(buf, FU_ELANTP_FIRMWARE_OFFSET_IAP, 0x00);
	fu_struct_elantp_firmware_hdr_set_ic_type(st_hdr, self->ic_type);
	fu_struct_elantp_firmware_hdr_set_iap_ver(st_hdr, self->iap_ver);
	fu_struct_elantp_firmware_hdr_set_iap_start(st_hdr, self->iap_addr / 2);
	fu_struct_elantp_firmware_hdr_set_iap_forcetable(
	    st_hdr,
	    self->force_table_support ? self->force_table_addr / 2 : 0xFFFF);
	g_byte_array_append(buf, st_hdr->buf->data, st_hdr->buf->len);

	/* IAP */
	fu_byte_array_set_size(buf, self->iap_addr + 0x4, 0x00);
	if (!fu_memwrite_uint16_safe(buf->data,
				     buf->len,
				     self->iap_addr,
				     (self->iap_addr + 2) / 2,
				     G_LITTLE_ENDIAN,
				     error))
		return NULL;
	if (!fu_memwrite_uint16_safe(buf->data,
				     buf->len,
				     self->iap_addr + 0x2,
				     self->module_id,
				     G_LITTLE_ENDIAN,
				     error))
		return NULL;

	/* data */
	blob = fu_firmware_get_bytes_with_patches(firmware, error);
	if (blob == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, blob);

	/* footer */
	g_byte_array_append(buf, st_ftr->buf->data, st_ftr->buf->len);
	return g_steal_pointer(&buf);
}

static void
fu_elantp_firmware_init(FuElantpFirmware *self)
{
	fu_firmware_set_size_max(FU_FIRMWARE(self), 16 * FU_MB);
}

static void
fu_elantp_firmware_class_init(FuElantpFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_elantp_firmware_validate;
	firmware_class->parse = fu_elantp_firmware_parse;
	firmware_class->build = fu_elantp_firmware_build;
	firmware_class->write = fu_elantp_firmware_write;
	firmware_class->export = fu_elantp_firmware_export;
}
