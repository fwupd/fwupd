/*
 * Copyright 2017 VIA Corporation
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-vli-struct.h"
#include "fu-vli-usbhub-firmware.h"

struct _FuVliUsbhubFirmware {
	FuFirmware parent_instance;
	FuVliDeviceKind device_kind;
	guint16 dev_id;
};

G_DEFINE_TYPE(FuVliUsbhubFirmware, fu_vli_usbhub_firmware, FU_TYPE_FIRMWARE)

FuVliDeviceKind
fu_vli_usbhub_firmware_get_device_kind(FuVliUsbhubFirmware *self)
{
	g_return_val_if_fail(FU_IS_VLI_USBHUB_FIRMWARE(self), 0);
	return self->device_kind;
}

guint16
fu_vli_usbhub_firmware_get_device_id(FuVliUsbhubFirmware *self)
{
	g_return_val_if_fail(FU_IS_VLI_USBHUB_FIRMWARE(self), 0);
	return self->dev_id;
}

static void
fu_vli_usbhub_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuVliUsbhubFirmware *self = FU_VLI_USBHUB_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kv(bn,
				  "device_kind",
				  fu_vli_device_kind_to_string(self->device_kind));
}

static gboolean
fu_vli_usbhub_firmware_parse_version(FuVliUsbhubFirmware *self,
				     GInputStream *stream,
				     guint8 strapping1,
				     GError **error)
{
	guint16 adr_ofs = 0;
	guint16 version = 0x0;
	guint32 adr_ofs32 = 0;
	guint8 tmp = 0x0;

	switch (self->dev_id) {
	case 0x0d12:
		/* VL81x */
		if (!fu_input_stream_read_u16(stream, 0x1f4c, &version, G_LITTLE_ENDIAN, error)) {
			g_prefix_error(error, "failed to get version: ");
			return FALSE;
		}
		version |= (strapping1 >> 4) & 0x07;
		if ((version & 0x0f) == 0x04) {
			if (!fu_input_stream_read_u8(stream, 0x700d, &tmp, error)) {
				g_prefix_error(error, "failed to get version increment: ");
				return FALSE;
			}
			if (tmp & 0x40)
				version += 1;
		}
		break;
	case 0x0507:
		/* VL210 */
		if (!fu_input_stream_read_u16(stream, 0x8f0c, &version, G_LITTLE_ENDIAN, error)) {
			g_prefix_error(error, "failed to get version: ");
			return FALSE;
		}
		version |= (strapping1 >> 4) & 0x07;
		if ((version & 0x0f) == 0x04)
			version += 1;
		break;
	case 0x0566:
		/* U4ID_Address_In_FW_Zone */
		if (!fu_input_stream_read_u24(stream, 0x3F80, &adr_ofs32, G_LITTLE_ENDIAN, error)) {
			g_prefix_error(error, "failed to get offset addr: ");
			return FALSE;
		}
		if (adr_ofs32 < 0x20000 + 0x2000 + 4) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid U4ID_Address_In_FW_Zone=0x%x",
				    adr_ofs32);
			return FALSE;
		}
		if (!fu_input_stream_read_u16(stream,
					      adr_ofs32 - 0x20000 + 0x2000 + 4,
					      &version,
					      G_LITTLE_ENDIAN,
					      error)) {
			g_prefix_error(error, "failed to get offset version: ");
			return FALSE;
		}
		version |= (strapping1 >> 4) & 0x07;
		break;
	default:
		/* U3ID_Address_In_FW_Zone */
		if (!fu_input_stream_read_u16(stream, 0x8000, &adr_ofs, G_BIG_ENDIAN, error)) {
			g_prefix_error(error, "failed to get offset addr: ");
			return FALSE;
		}
		if (!fu_input_stream_read_u16(stream,
					      adr_ofs + 0x2000 + 0x04, /* U3-M? */
					      &version,
					      G_LITTLE_ENDIAN,
					      error)) {
			g_prefix_error(error, "failed to get offset version: ");
			return FALSE;
		}
		version |= (strapping1 >> 4) & 0x07;
	}

	/* version is set */
	if (version != 0x0)
		fu_firmware_set_version_raw(FU_FIRMWARE(self), version);
	return TRUE;
}

static gboolean
fu_vli_usbhub_firmware_parse(FuFirmware *firmware,
			     GInputStream *stream,
			     FuFirmwareParseFlags flags,
			     GError **error)
{
	FuVliUsbhubFirmware *self = FU_VLI_USBHUB_FIRMWARE(firmware);
	guint16 adr_ofs = 0;
	guint8 tmp = 0x0;
	guint8 fwtype = 0x0;
	guint8 strapping1;
	g_autoptr(GByteArray) st = NULL;

	/* map into header */
	st = fu_struct_vli_usbhub_hdr_parse_stream(stream, 0x0, error);
	if (st == NULL) {
		g_prefix_error(error, "failed to read header: ");
		return FALSE;
	}
	self->dev_id = fu_struct_vli_usbhub_hdr_get_dev_id(st);
	strapping1 = fu_struct_vli_usbhub_hdr_get_strapping1(st);

	/* get firmware versions */
	if (!fu_vli_usbhub_firmware_parse_version(self, stream, strapping1, error))
		return FALSE;

	/* get device type from firmware image */
	switch (self->dev_id) {
	case 0x0d12: {
		guint16 binver1 = 0x0;
		guint16 binver2 = 0x0;
		guint16 usb2_fw_addr = fu_struct_vli_usbhub_hdr_get_usb2_fw_addr(st) + 0x1ff1;
		guint16 usb3_fw_addr = fu_struct_vli_usbhub_hdr_get_usb3_fw_addr(st) + 0x1ffa;
		if (!fu_input_stream_read_u16(stream,
					      usb2_fw_addr,
					      &binver1,
					      G_LITTLE_ENDIAN,
					      error)) {
			g_prefix_error(error, "failed to get binver1: ");
			return FALSE;
		}
		if (!fu_input_stream_read_u16(stream,
					      usb3_fw_addr,
					      &binver2,
					      G_LITTLE_ENDIAN,
					      error)) {
			g_prefix_error(error, "failed to get binver2: ");
			return FALSE;
		}

		/* VL813 == VT3470 */
		if ((binver1 == 0xb770 && binver2 == 0xb770) ||
		    (binver1 == 0xb870 && binver2 == 0xb870)) {
			self->device_kind = FU_VLI_DEVICE_KIND_VL813;

			/* VLQ4S == VT3470 (Q4S) */
		} else if (strapping1 & FU_VLI_USBHUB_HEADER_STRAPPING1_Q4S) {
			self->device_kind = FU_VLI_DEVICE_KIND_VL812Q4S;

			/* VL812 == VT3470 (812/813) */
		} else if (strapping1 & FU_VLI_USBHUB_HEADER_STRAPPING1_76PIN) {
			/* is B3 */
			if (strapping1 & FU_VLI_USBHUB_HEADER_STRAPPING1_B3UP)
				self->device_kind = FU_VLI_DEVICE_KIND_VL812B3;
			else
				self->device_kind = FU_VLI_DEVICE_KIND_VL812B0;

			/* VL811P == VT3470 */
		} else {
			/* is B3 */
			if (strapping1 & FU_VLI_USBHUB_HEADER_STRAPPING1_B3UP)
				self->device_kind = FU_VLI_DEVICE_KIND_VL811PB3;
			else
				self->device_kind = FU_VLI_DEVICE_KIND_VL811PB0;
		}
		break;
	}
	case 0x0507:
		/* VL210 == VT3507 */
		self->device_kind = FU_VLI_DEVICE_KIND_VL210;
		break;
	case 0x0545:
		/* VL211 == VT3545 */
		self->device_kind = FU_VLI_DEVICE_KIND_VL211;
		break;
	case 0x0518:
		/* VL819~VL822 == VT3518 */
		if (!fu_input_stream_read_u8(stream, 0x8021, &tmp, error)) {
			g_prefix_error(error, "failed to get 820/822 byte: ");
			return FALSE;
		}
		/* Q5/Q7/Q8 requires searching two addresses for offset value */
		if (!fu_input_stream_read_u16(stream, 0x8018, &adr_ofs, G_BIG_ENDIAN, error)) {
			g_prefix_error(error, "failed to get Q7/Q8 offset mapping: ");
			return FALSE;
		}
		/* VL819, VL821, VL822 */
		if (tmp == 0xF0) {
			if (!fu_input_stream_read_u8(stream, adr_ofs + 0x2000, &tmp, error)) {
				g_prefix_error(error, "failed to get offset version: ");
				return FALSE;
			}
			/* VL819 */
			if ((tmp == 0x05) || (tmp == 0x07))
				fwtype = tmp & 0x7;
			else
				fwtype = (tmp & 0x1) << 1 | (tmp & 0x2) << 1 | (tmp & 0x4) >> 2;
			/* matching Q5/Q7/Q8 */
			switch (fwtype) {
			case 0x00:
				self->device_kind = FU_VLI_DEVICE_KIND_VL822Q7;
				break;
			case 0x01:
				self->device_kind = FU_VLI_DEVICE_KIND_VL822Q5;
				break;
			case 0x02:
				self->device_kind = FU_VLI_DEVICE_KIND_VL822Q8;
				break;
			case 0x04:
				self->device_kind = FU_VLI_DEVICE_KIND_VL821Q7;
				break;
			case 0x05:
				self->device_kind = FU_VLI_DEVICE_KIND_VL819Q7;
				break;
			case 0x06:
				self->device_kind = FU_VLI_DEVICE_KIND_VL821Q8;
				break;
			case 0x07:
				self->device_kind = FU_VLI_DEVICE_KIND_VL819Q8;
				break;
			default:
				g_prefix_error(error, "failed to match Q5/Q7/Q8 fw type: ");
				return FALSE;
			}
			/* VL820 */
		} else if (tmp == 0xC0 || tmp == 0xC1) {
			self->device_kind = FU_VLI_DEVICE_KIND_VL822C0;
		} else {
			if (!fu_input_stream_read_u8(stream, 0xf000, &tmp, error)) {
				g_prefix_error(error, "failed to get Q7/Q8 difference: ");
				return FALSE;
			}
			if (tmp & (1 << 0))
				self->device_kind = FU_VLI_DEVICE_KIND_VL820Q8;
			else
				self->device_kind = FU_VLI_DEVICE_KIND_VL820Q7;
		}
		break;
	case 0x0595:
		/* VL822T == VT3595 */
		self->device_kind = FU_VLI_DEVICE_KIND_VL822T;
		break;
	case 0x0538:
		/* VL817 == VT3538 */
		self->device_kind = FU_VLI_DEVICE_KIND_VL817;
		break;
	case 0x0590:
		/* VL817S == VT3590 */
		self->device_kind = FU_VLI_DEVICE_KIND_VL817S;
		break;
	case 0x0553:
		/* VL120 == VT3553 */
		self->device_kind = FU_VLI_DEVICE_KIND_VL120;
		break;
	case 0x0592:
		/* VL122 == VT3592 */
		self->device_kind = FU_VLI_DEVICE_KIND_VL122;
		break;
	case 0x0566: {
		/* VL830 VL832 = VT3566 */
		guint32 binveraddr = 0;
		guint8 binver = 0;
		if (!fu_input_stream_read_u24(stream,
					      0x3FBC,
					      &binveraddr,
					      G_LITTLE_ENDIAN,
					      error)) {
			g_prefix_error(error, "failed to get binveraddr: ");
			return FALSE;
		}
		if (binveraddr < 0x20000 + 0x2000) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "stream was too small");
			return FALSE;
		}
		if (!fu_input_stream_read_u8(stream,
					     binveraddr - 0x20000 + 0x2000,
					     &binver,
					     error)) {
			g_prefix_error(error, "failed to get binver2: ");
			return FALSE;
		}

		/* VL813 == VT3470 */
		if (binver <= 0xC0)
			self->device_kind = FU_VLI_DEVICE_KIND_VL830;
		else
			self->device_kind = FU_VLI_DEVICE_KIND_VL832;
		break;
	}
	default:
		break;
	}

	/* device not supported */
	if (self->device_kind == FU_VLI_DEVICE_KIND_UNKNOWN) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device kind unknown");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gchar *
fu_vli_usbhub_firmware_convert_version(FuFirmware *firmware, guint64 version_raw)
{
	return fu_version_from_uint16(version_raw, fu_firmware_get_version_format(firmware));
}

static void
fu_vli_usbhub_firmware_init(FuVliUsbhubFirmware *self)
{
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_BCD);
}

static void
fu_vli_usbhub_firmware_class_init(FuVliUsbhubFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->convert_version = fu_vli_usbhub_firmware_convert_version;
	firmware_class->parse = fu_vli_usbhub_firmware_parse;
	firmware_class->export = fu_vli_usbhub_firmware_export;
}

FuFirmware *
fu_vli_usbhub_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_VLI_USBHUB_FIRMWARE, NULL));
}
