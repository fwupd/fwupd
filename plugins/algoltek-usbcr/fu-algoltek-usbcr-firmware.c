/*
 * Copyright 2024 Algoltek, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-algoltek-usbcr-firmware.h"
#include "fu-algoltek-usbcr-struct.h"

struct _FuAlgoltekUsbcrFirmware {
	FuFirmware parent_instance;
	guint16 boot_ver;
	guint16 emmc_ver;
};

G_DEFINE_TYPE(FuAlgoltekUsbcrFirmware, fu_algoltek_usbcr_firmware, FU_TYPE_FIRMWARE)

static void
fu_algoltek_usbcr_firmware_export(FuFirmware *firmware,
				  FuFirmwareExportFlags flags,
				  XbBuilderNode *bn)
{
	FuAlgoltekUsbcrFirmware *self = FU_ALGOLTEK_USBCR_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "boot_ver", self->boot_ver);
	fu_xmlb_builder_insert_kx(bn, "emmc_ver", self->emmc_ver);
}

static gboolean
fu_algoltek_usbcr_firmware_parse(FuFirmware *firmware,
				 GInputStream *stream,
				 FuFirmwareParseFlags flags,
				 GError **error)
{
	FuAlgoltekUsbcrFirmware *self = FU_ALGOLTEK_USBCR_FIRMWARE(firmware);
	gsize offset = 0;
	guint16 emmc_support_ver = 0;
	g_autoptr(FuStructAgUsbcrFirmwareHdr) st_hdr = NULL;
	g_autoptr(FuStructAgUsbcrFirmwareInfo) st_inf = NULL;

	/* emmc version */
	st_hdr = fu_struct_ag_usbcr_firmware_hdr_parse_stream(stream, offset, error);
	if (st_hdr == NULL)
		return FALSE;
	fu_firmware_set_offset(firmware, fu_struct_ag_usbcr_firmware_hdr_get_fw_addr(st_hdr));
	fu_firmware_set_size(firmware, fu_struct_ag_usbcr_firmware_hdr_get_fw_len(st_hdr));
	self->emmc_ver = fu_struct_ag_usbcr_firmware_hdr_get_emmc_ver(st_hdr);

	/* calculate the offset of the app_ver */
	offset += fu_firmware_get_offset(firmware) + fu_firmware_get_size(firmware) -
		  FU_STRUCT_AG_USBCR_FIRMWARE_INFO_SIZE;

	/* app version */
	st_inf = fu_struct_ag_usbcr_firmware_info_parse_stream(stream, offset, error);
	if (st_inf == NULL)
		return FALSE;
	fu_firmware_set_version_raw(firmware, fu_struct_ag_usbcr_firmware_info_get_app_ver(st_inf));

	/* boot version */
	self->boot_ver = fu_struct_ag_usbcr_firmware_info_get_boot_ver(st_inf);

	/* emmc support version */
	emmc_support_ver = fu_struct_ag_usbcr_firmware_info_get_emmc_support_ver(st_inf);
	if (self->emmc_ver != emmc_support_ver) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "EMMC support version is 0x%x while expecting value is 0x%x",
			    emmc_support_ver,
			    self->emmc_ver);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_algoltek_usbcr_firmware_write(FuFirmware *firmware, GError **error)
{
	FuAlgoltekUsbcrFirmware *self = FU_ALGOLTEK_USBCR_FIRMWARE(firmware);
	g_autoptr(FuStructAgUsbcrFirmwareHdr) st_hdr = fu_struct_ag_usbcr_firmware_hdr_new();
	g_autoptr(FuStructAgUsbcrFirmwareInfo) st_inf = fu_struct_ag_usbcr_firmware_info_new();
	g_autoptr(GByteArray) buf = g_byte_array_new();

	fu_struct_ag_usbcr_firmware_hdr_set_fw_addr(st_hdr, FU_STRUCT_AG_USBCR_FIRMWARE_HDR_SIZE);
	fu_struct_ag_usbcr_firmware_hdr_set_fw_len(st_hdr, FU_STRUCT_AG_USBCR_FIRMWARE_INFO_SIZE);
	fu_struct_ag_usbcr_firmware_hdr_set_emmc_ver(st_hdr, self->emmc_ver);
	g_byte_array_append(buf, st_hdr->buf->data, st_hdr->buf->len);

	fu_struct_ag_usbcr_firmware_info_set_app_ver(st_inf, fu_firmware_get_version_raw(firmware));
	fu_struct_ag_usbcr_firmware_info_set_boot_ver(st_inf, self->boot_ver);
	fu_struct_ag_usbcr_firmware_info_set_emmc_support_ver(st_inf, self->emmc_ver);
	g_byte_array_append(buf, st_inf->buf->data, st_inf->buf->len);

	/* success */
	return g_steal_pointer(&buf);
}

guint16
fu_algoltek_usbcr_firmware_get_boot_ver(FuAlgoltekUsbcrFirmware *self)
{
	g_return_val_if_fail(FU_IS_ALGOLTEK_USBCR_FIRMWARE(self), G_MAXUINT16);
	return self->boot_ver;
}

static gboolean
fu_algoltek_usbcr_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuAlgoltekUsbcrFirmware *self = FU_ALGOLTEK_USBCR_FIRMWARE(firmware);
	guint64 tmp;

	/* two simple properties */
	tmp = xb_node_query_text_as_uint(n, "boot_ver", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		self->boot_ver = tmp;
	tmp = xb_node_query_text_as_uint(n, "emmc_ver", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		self->emmc_ver = tmp;

	/* success */
	return TRUE;
}

static gchar *
fu_algoltek_usbcr_firmware_convert_version(FuFirmware *firmware, guint64 version_raw)
{
	return fu_version_from_uint16_hex(version_raw, fu_firmware_get_version_format(firmware));
}

static void
fu_algoltek_usbcr_firmware_init(FuAlgoltekUsbcrFirmware *self)
{
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
}

static void
fu_algoltek_usbcr_firmware_class_init(FuAlgoltekUsbcrFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->convert_version = fu_algoltek_usbcr_firmware_convert_version;
	firmware_class->parse = fu_algoltek_usbcr_firmware_parse;
	firmware_class->export = fu_algoltek_usbcr_firmware_export;
	firmware_class->build = fu_algoltek_usbcr_firmware_build;
	firmware_class->write = fu_algoltek_usbcr_firmware_write;
}

FuFirmware *
fu_algoltek_usbcr_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ALGOLTEK_USBCR_FIRMWARE, NULL));
}
