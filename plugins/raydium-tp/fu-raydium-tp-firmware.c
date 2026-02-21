/*
 * Copyright 2025 Raydium.inc <Maker.Tsai@rad-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-raydium-tp-common.h"
#include "fu-raydium-tp-firmware.h"
#include "fu-raydium-tp-struct.h"

struct _FuRaydiumtpFirmware {
	FuFirmware parent_instance;
	guint16 vendor_id;
	guint16 product_id;
	guint32 fw_base;
	guint32 desc_base;
	guint32 fw_start;
	guint32 fw_len;
	guint32 desc_start;
	guint32 desc_len;
};

G_DEFINE_TYPE(FuRaydiumtpFirmware, fu_raydium_tp_firmware, FU_TYPE_FIRMWARE)

guint16
fu_raydium_tp_firmware_get_vendor_id(FuRaydiumtpFirmware *self)
{
	g_return_val_if_fail(FU_IS_RAYDIUM_TP_FIRMWARE(self), 0);
	return self->vendor_id;
}

guint16
fu_raydium_tp_firmware_get_product_id(FuRaydiumtpFirmware *self)
{
	g_return_val_if_fail(FU_IS_RAYDIUM_TP_FIRMWARE(self), 0);
	return self->product_id;
}

guint32
fu_raydium_tp_firmware_get_fw_base(FuRaydiumtpFirmware *self)
{
	g_return_val_if_fail(FU_IS_RAYDIUM_TP_FIRMWARE(self), 0);
	return self->fw_base;
}

guint32
fu_raydium_tp_firmware_get_desc_base(FuRaydiumtpFirmware *self)
{
	g_return_val_if_fail(FU_IS_RAYDIUM_TP_FIRMWARE(self), 0);
	return self->desc_base;
}

guint32
fu_raydium_tp_firmware_get_fw_start(FuRaydiumtpFirmware *self)
{
	g_return_val_if_fail(FU_IS_RAYDIUM_TP_FIRMWARE(self), 0);
	return self->fw_start;
}

guint32
fu_raydium_tp_firmware_get_fw_len(FuRaydiumtpFirmware *self)
{
	g_return_val_if_fail(FU_IS_RAYDIUM_TP_FIRMWARE(self), 0);
	return self->fw_len;
}

guint32
fu_raydium_tp_firmware_get_desc_start(FuRaydiumtpFirmware *self)
{
	g_return_val_if_fail(FU_IS_RAYDIUM_TP_FIRMWARE(self), 0);
	return self->desc_start;
}

guint32
fu_raydium_tp_firmware_get_desc_len(FuRaydiumtpFirmware *self)
{
	g_return_val_if_fail(FU_IS_RAYDIUM_TP_FIRMWARE(self), 0);
	return self->desc_len;
}

static gboolean
fu_raydium_tp_firmware_parse(FuFirmware *firmware,
			     GInputStream *stream,
			     FuFirmwareParseFlags flags,
			     GError **error)
{
	FuRaydiumtpFirmware *self = FU_RAYDIUM_TP_FIRMWARE(firmware);
	g_autoptr(FuStructRaydiumTpFwHdr) st = NULL;
	gsize streamsz = 0;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;

	st = fu_struct_raydium_tp_fw_hdr_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;

	self->vendor_id = fu_struct_raydium_tp_fw_hdr_get_vendor_id(st);
	self->product_id = fu_struct_raydium_tp_fw_hdr_get_product_id(st);
	self->fw_base = fu_struct_raydium_tp_fw_hdr_get_fw_base(st);
	self->desc_base = fu_struct_raydium_tp_fw_hdr_get_desc_base(st);
	self->fw_start = fu_struct_raydium_tp_fw_hdr_get_start(st);
	self->fw_len = fu_struct_raydium_tp_fw_hdr_get_length(st);
	self->desc_start = fu_struct_raydium_tp_fw_hdr_get_desc_start(st);
	self->desc_len = fu_struct_raydium_tp_fw_hdr_get_desc_length(st);

	return TRUE;
}

static void
fu_raydium_tp_firmware_init(FuRaydiumtpFirmware *self)
{
}

static void
fu_raydium_tp_firmware_class_init(FuRaydiumtpFirmwareClass *klass)
{
	FuFirmwareClass *fw_class = FU_FIRMWARE_CLASS(klass);
	fw_class->parse = fu_raydium_tp_firmware_parse;
}

FuFirmware *
fu_raydium_tp_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_RAYDIUM_TP_FIRMWARE, NULL));
}
