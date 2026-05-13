/*
 * Copyright 2025 Raydium.inc <Maker.Tsai@rad-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-raydium-tp-firmware.h"
#include "fu-raydium-tp-image.h"
#include "fu-raydium-tp-struct.h"

struct _FuRaydiumTpFirmware {
	FuFirmware parent_instance;
	guint16 vendor_id;
	guint16 product_id;
};

G_DEFINE_TYPE(FuRaydiumTpFirmware, fu_raydium_tp_firmware, FU_TYPE_FIRMWARE)

static void
fu_raydium_tp_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuRaydiumTpFirmware *self = FU_RAYDIUM_TP_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "vendor_id", self->vendor_id);
	fu_xmlb_builder_insert_kx(bn, "product_id", self->product_id);
}

guint16
fu_raydium_tp_firmware_get_vendor_id(FuRaydiumTpFirmware *self)
{
	g_return_val_if_fail(FU_IS_RAYDIUM_TP_FIRMWARE(self), 0);
	return self->vendor_id;
}

guint16
fu_raydium_tp_firmware_get_product_id(FuRaydiumTpFirmware *self)
{
	g_return_val_if_fail(FU_IS_RAYDIUM_TP_FIRMWARE(self), 0);
	return self->product_id;
}

static gboolean
fu_raydium_tp_firmware_parse(FuFirmware *firmware,
			     GInputStream *stream,
			     FuFirmwareParseFlags flags,
			     GError **error)
{
	FuRaydiumTpFirmware *self = FU_RAYDIUM_TP_FIRMWARE(firmware);
	g_autoptr(FuFirmware) firmware_desc = g_object_new(FU_TYPE_RAYDIUM_TP_IMAGE, NULL);
	g_autoptr(FuFirmware) firmware_pram = g_object_new(FU_TYPE_RAYDIUM_TP_IMAGE, NULL);
	g_autoptr(FuStructRaydiumTpFwHdr) st = NULL;
	g_autoptr(GInputStream) stream_desc = NULL;
	g_autoptr(GInputStream) stream_fw = NULL;

	st = fu_struct_raydium_tp_fw_hdr_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;
	self->vendor_id = fu_struct_raydium_tp_fw_hdr_get_vendor_id(st);
	self->product_id = fu_struct_raydium_tp_fw_hdr_get_product_id(st);

	/* fw */
	fu_firmware_set_id(firmware_pram, FU_FIRMWARE_ID_PAYLOAD);
	fu_firmware_set_addr(firmware_pram, fu_struct_raydium_tp_fw_hdr_get_fw_base(st));
	stream_fw = fu_partial_input_stream_new(stream,
						fu_struct_raydium_tp_fw_hdr_get_start(st),
						fu_struct_raydium_tp_fw_hdr_get_length(st),
						error);
	if (stream_fw == NULL)
		return FALSE;
	if (!fu_firmware_parse_stream(firmware_pram, stream_fw, 0x0, flags, error))
		return FALSE;
	if (!fu_firmware_add_image(firmware, firmware_pram, error))
		return FALSE;

	/* desc */
	fu_firmware_set_id(firmware_desc, FU_FIRMWARE_ID_HEADER);
	fu_firmware_set_addr(firmware_desc, fu_struct_raydium_tp_fw_hdr_get_desc_base(st));
	stream_desc = fu_partial_input_stream_new(stream,
						  fu_struct_raydium_tp_fw_hdr_get_desc_start(st),
						  fu_struct_raydium_tp_fw_hdr_get_desc_length(st),
						  error);
	if (stream_desc == NULL)
		return FALSE;
	if (!fu_firmware_parse_stream(firmware_desc, stream_desc, 0x0, flags, error))
		return FALSE;
	if (!fu_firmware_add_image(firmware, firmware_desc, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_raydium_tp_firmware_init(FuRaydiumTpFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_VID_PID);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
	fu_firmware_add_image_gtype(FU_FIRMWARE(self), FU_TYPE_RAYDIUM_TP_IMAGE);
}

static void
fu_raydium_tp_firmware_class_init(FuRaydiumTpFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->export = fu_raydium_tp_firmware_export;
	klass_firmware->parse = fu_raydium_tp_firmware_parse;
}
