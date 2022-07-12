/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-elantp-common.h"
#include "fu-elantp-firmware.h"

struct _FuElantpFirmware {
	FuFirmwareClass parent_instance;
	guint16 module_id;
	guint16 iap_addr;
};

G_DEFINE_TYPE(FuElantpFirmware, fu_elantp_firmware, FU_TYPE_FIRMWARE)

/* firmware block update */
#define ETP_IAP_START_ADDR_WRDS 0x0083

const guint8 elantp_signature[] = {0xAA, 0x55, 0xCC, 0x33, 0xFF, 0xFF};

guint16
fu_elantp_firmware_get_module_id(FuElantpFirmware *self)
{
	g_return_val_if_fail(FU_IS_ELANTP_FIRMWARE(self), 0);
	return self->module_id;
}

guint16
fu_elantp_firmware_get_iap_addr(FuElantpFirmware *self)
{
	g_return_val_if_fail(FU_IS_ELANTP_FIRMWARE(self), 0);
	return self->iap_addr;
}

static void
fu_elantp_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuElantpFirmware *self = FU_ELANTP_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "iap_addr", self->iap_addr);
	fu_xmlb_builder_insert_kx(bn, "module_id", self->module_id);
}

static gboolean
fu_elantp_firmware_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	gsize bufsz = g_bytes_get_size(fw);
	const guint8 *buf = g_bytes_get_data(fw, NULL);

	for (gsize i = 0; i < sizeof(elantp_signature); i++) {
		guint8 tmp = 0x0;
		if (!fu_memread_uint8_safe(buf,
					   bufsz,
					   bufsz - sizeof(elantp_signature) + i,
					   &tmp,
					   error))
			return FALSE;
		if (tmp != elantp_signature[i]) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "signature[%u] invalid: got 0x%2x, expected 0x%02x",
				    (guint)i,
				    tmp,
				    elantp_signature[i]);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_firmware_parse(FuFirmware *firmware,
			 GBytes *fw,
			 gsize offset,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuElantpFirmware *self = FU_ELANTP_FIRMWARE(firmware);
	gsize bufsz = 0;
	guint16 iap_addr_wrds;
	guint16 module_id_wrds;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	/* presumably in words */
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
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
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
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
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    offset + module_id_wrds * 2,
				    &self->module_id,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

	/* whole image */
	fu_firmware_set_bytes(firmware, fw);
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

static GBytes *
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
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static void
fu_elantp_firmware_init(FuElantpFirmware *self)
{
}

static void
fu_elantp_firmware_class_init(FuElantpFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_elantp_firmware_check_magic;
	klass_firmware->parse = fu_elantp_firmware_parse;
	klass_firmware->build = fu_elantp_firmware_build;
	klass_firmware->write = fu_elantp_firmware_write;
	klass_firmware->export = fu_elantp_firmware_export;
}

FuFirmware *
fu_elantp_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ELANTP_FIRMWARE, NULL));
}
