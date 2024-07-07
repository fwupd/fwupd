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
	FuFirmwareClass parent_instance;
	guint16 module_id;
	guint16 ic_type;
	guint16 iap_addr;
	guint16 iap_ver;
	gboolean force_table_support;
	guint32 force_table_addr;
};

G_DEFINE_TYPE(FuElantpFirmware, fu_elantp_firmware, FU_TYPE_FIRMWARE)

/* firmware block update */
#define ETP_IC_TYPE_ADDR_WRDS	   0x0080
#define ETP_IAP_VER_ADDR_WRDS	   0x0082
#define ETP_IAP_START_ADDR_WRDS	   0x0083
#define ETP_IAP_FORCETABLE_ADDR_V5 0x0085

const guint8 elantp_signature[] = {0xAA, 0x55, 0xCC, 0x33, 0xFF, 0xFF};

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
	fu_xmlb_builder_insert_kx(bn, "iap_addr", self->iap_addr);
	fu_xmlb_builder_insert_kx(bn, "module_id", self->module_id);
}

static gboolean
fu_elantp_firmware_validate(FuFirmware *firmware,
			    GInputStream *stream,
			    gsize offset,
			    GError **error)
{
	FuElantpFirmware *self = FU_ELANTP_FIRMWARE(firmware);
	gsize streamsz = 0;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz < FU_STRUCT_ELANTP_FIRMWARE_HDR_SIZE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "stream was too small");
		return FALSE;
	}
	if (!fu_struct_elantp_firmware_hdr_validate_stream(stream,
							   streamsz -
							       FU_STRUCT_ELANTP_FIRMWARE_HDR_SIZE,
							   error))
		return FALSE;
	if (self->force_table_addr != 0) {
		if (!fu_struct_elantp_firmware_hdr_validate_stream(
			stream,
			self->force_table_addr - 1 + FU_STRUCT_ELANTP_FIRMWARE_HDR_SIZE,
			error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_firmware_parse(FuFirmware *firmware,
			 GInputStream *stream,
			 gsize offset,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuElantpFirmware *self = FU_ELANTP_FIRMWARE(firmware);
	guint16 iap_addr_wrds;
	guint16 force_table_addr_wrds;
	guint16 module_id_wrds;
	g_autoptr(GError) error_local = NULL;

	/* presumably in words */
	if (!fu_input_stream_read_u16(stream,
				      offset + ETP_IAP_START_ADDR_WRDS * 2,
				      &iap_addr_wrds,
				      G_LITTLE_ENDIAN,
				      error))
		return FALSE;
	if (iap_addr_wrds < ETP_IAP_START_ADDR_WRDS || iap_addr_wrds > 0x7FFF) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "IAP address invalid: 0x%x",
			    iap_addr_wrds);
		return FALSE;
	}
	self->iap_addr = iap_addr_wrds * 2;

	/* read module ID */
	if (!fu_input_stream_read_u16(stream,
				      offset + self->iap_addr,
				      &module_id_wrds,
				      G_LITTLE_ENDIAN,
				      error))
		return FALSE;
	if (module_id_wrds > 0x7FFF) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "module ID address invalid: 0x%x",
			    module_id_wrds);
		return FALSE;
	}
	if (!fu_input_stream_read_u16(stream,
				      offset + module_id_wrds * 2,
				      &self->module_id,
				      G_LITTLE_ENDIAN,
				      error))
		return FALSE;
	if (!fu_input_stream_read_u16(stream,
				      offset + ETP_IC_TYPE_ADDR_WRDS * 2,
				      &self->ic_type,
				      G_LITTLE_ENDIAN,
				      error))
		return FALSE;
	if (!fu_input_stream_read_u16(stream,
				      offset + ETP_IAP_VER_ADDR_WRDS * 2,
				      &self->iap_ver,
				      G_LITTLE_ENDIAN,
				      error))
		return FALSE;

	if (self->ic_type != 0x12 && self->ic_type != 0x13)
		return TRUE;

	if (self->iap_ver <= 4) {
		if (!fu_input_stream_read_u16(stream,
					      offset + (self->iap_addr + 6),
					      &force_table_addr_wrds,
					      G_LITTLE_ENDIAN,
					      &error_local)) {
			g_debug("forcetable address wrong: %s", error_local->message);
			return TRUE;
		}
	} else {
		if (!fu_input_stream_read_u16(stream,
					      offset + ETP_IAP_FORCETABLE_ADDR_V5 * 2,
					      &force_table_addr_wrds,
					      G_LITTLE_ENDIAN,
					      &error_local)) {
			g_debug("forcetable address wrong: %s", error_local->message);
			return TRUE;
		}
	}

	if (force_table_addr_wrds % 32 == 0) {
		self->force_table_addr = force_table_addr_wrds * 2;
		self->force_table_support = TRUE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuElantpFirmware *self = FU_ELANTP_FIRMWARE(firmware);
	guint64 tmp;

	/* two simple properties */
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

	/* only one image supported */
	blob = fu_firmware_get_bytes_with_patches(firmware, error);
	if (blob == NULL)
		return NULL;

	/* lets build a simple firmware like this:
	 * ------ 0x0
	 * HEADER (containing IAP offset and module ID)
	 * ------ ~0x10a
	 *  DATA
	 * ------
	 *  SIGNATURE
	 * ------
	 */
	fu_byte_array_set_size(buf, self->iap_addr + 0x2 + 0x2, 0x00);
	if (!fu_memwrite_uint16_safe(buf->data,
				     buf->len,
				     ETP_IAP_START_ADDR_WRDS * 2,
				     self->iap_addr / 2,
				     G_LITTLE_ENDIAN,
				     error))
		return NULL;
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
	fu_byte_array_append_bytes(buf, blob);
	g_byte_array_append(buf, elantp_signature, sizeof(elantp_signature));
	return g_steal_pointer(&buf);
}

static void
fu_elantp_firmware_init(FuElantpFirmware *self)
{
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

FuFirmware *
fu_elantp_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ELANTP_FIRMWARE, NULL));
}
