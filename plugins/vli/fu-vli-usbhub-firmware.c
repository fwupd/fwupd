/*
 * Copyright (C) 2017-2019 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"

#include "fu-vli-usbhub-firmware.h"

struct _FuVliUsbhubFirmware {
	FuFirmwareClass		 parent_instance;
	FuVliDeviceKind		 device_kind;
	FuVliUsbhubHeader	 hdr;
};

G_DEFINE_TYPE (FuVliUsbhubFirmware, fu_vli_usbhub_firmware, FU_TYPE_FIRMWARE)

FuVliDeviceKind
fu_vli_usbhub_firmware_get_device_kind (FuVliUsbhubFirmware *self)
{
	g_return_val_if_fail (FU_IS_VLI_USBHUB_FIRMWARE (self), 0);
	return self->device_kind;
}

guint16
fu_vli_usbhub_firmware_get_device_id (FuVliUsbhubFirmware *self)
{
	g_return_val_if_fail (FU_IS_VLI_USBHUB_FIRMWARE (self), 0);
	return GUINT16_FROM_BE(self->hdr.dev_id);
}

static void
fu_vli_usbhub_firmware_to_string (FuFirmware *firmware, guint idt, GString *str)
{
	FuVliUsbhubFirmware *self = FU_VLI_USBHUB_FIRMWARE (firmware);
	fu_common_string_append_kv (str, idt, "DeviceKind",
				    fu_vli_common_device_kind_to_string (self->device_kind));
	fu_vli_usbhub_header_to_string (&self->hdr, idt, str);
}

static gboolean
fu_vli_usbhub_firmware_parse (FuFirmware *firmware,
			      GBytes *fw,
			      guint64 addr_start,
			      guint64 addr_end,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuVliUsbhubFirmware *self = FU_VLI_USBHUB_FIRMWARE (firmware);
	gsize bufsz = 0;
	guint16 adr_ofs = 0;
	guint16 version = 0x0;
	guint8 tmp = 0x0;
	const guint8 *buf = g_bytes_get_data (fw, &bufsz);
	g_autoptr(FuFirmwareImage) img = fu_firmware_image_new (fw);

	/* map into header */
	if (!fu_memcpy_safe ((guint8 *) &self->hdr, sizeof(self->hdr), 0x0,
			     buf, bufsz, 0x0, sizeof(self->hdr), error)) {
		g_prefix_error (error, "failed to read header: ");
		return FALSE;
	}

	/* get firmware versions */
	switch (GUINT16_FROM_BE(self->hdr.dev_id)) {
	case 0x0d12:
		/* VL81x */
		if (!fu_common_read_uint16_safe (buf, bufsz, 0x1f4c,
						 &version, G_LITTLE_ENDIAN, error)) {
			g_prefix_error (error, "failed to get version: ");
			return FALSE;
		}
		version |= (self->hdr.strapping1 >> 4) & 0x07;
		if ((version & 0x0f) == 0x04 ) {
			if (!fu_common_read_uint8_safe (buf, bufsz, 0x700d, &tmp, error)) {
				g_prefix_error (error, "failed to get version increment: ");
				return FALSE;
			}
			if (tmp & 0x40)
				version += 1;
		}
		break;
	case 0x0507:
		/* VL210 */
		if (!fu_common_read_uint16_safe (buf, bufsz, 0x8f0c,
						 &version, G_LITTLE_ENDIAN, error)) {
			g_prefix_error (error, "failed to get version: ");
			return FALSE;
		}
		version |= (self->hdr.strapping1 >> 4) & 0x07;
		if ((version & 0x0f) == 0x04)
			version += 1;
		break;
	default:
		/* U3ID_Address_In_FW_Zone */
		if (!fu_common_read_uint16_safe (buf, bufsz, 0x8000,
						 &adr_ofs, G_BIG_ENDIAN, error)) {
			g_prefix_error (error, "failed to get offset addr: ");
			return FALSE;
		}
		if (!fu_common_read_uint16_safe (buf, bufsz, adr_ofs + 0x2000 + 0x04, /* U3-M? */
						 &version, G_LITTLE_ENDIAN, error)) {
			g_prefix_error (error, "failed to get offset version: ");
			return FALSE;
		}
		version |= (self->hdr.strapping1 >> 4) & 0x07;
	}

	/* version is set */
	if (version != 0x0) {
		g_autofree gchar *version_str = NULL;
		version_str = fu_common_version_from_uint16 (version, FWUPD_VERSION_FORMAT_BCD);
		fu_firmware_set_version (firmware, version_str);
	}

	/* get device type from firmware image */
	switch (GUINT16_FROM_BE(self->hdr.dev_id)) {
	case 0x0d12:
	{
		guint16 binver1 = 0x0;
		guint16 binver2 = 0x0;
		guint16 usb2_fw_addr = GUINT16_FROM_BE(self->hdr.usb2_fw_addr) + 0x1ff1;
		guint16 usb3_fw_addr = GUINT16_FROM_BE(self->hdr.usb3_fw_addr) + 0x1ffa;
		if (!fu_common_read_uint16_safe (buf, bufsz, usb2_fw_addr,
						 &binver1, G_LITTLE_ENDIAN, error)) {
			g_prefix_error (error, "failed to get binver1: ");
			return FALSE;
		}
		if (!fu_common_read_uint16_safe (buf, bufsz, usb3_fw_addr,
						 &binver2, G_LITTLE_ENDIAN, error)) {
			g_prefix_error (error, "failed to get binver2: ");
			return FALSE;
		}

		/* VL813 == VT3470 */
		if ((binver1 == 0xb770 && binver2 == 0xb770) ||
		    (binver1 == 0xb870 && binver2 == 0xb870)) {
			self->device_kind = FU_VLI_DEVICE_KIND_VL813;

		/* VLQ4S == VT3470 (Q4S) */
		} else if (self->hdr.strapping1 & FU_VLI_USBHUB_HEADER_STRAPPING1_Q4S) {
			self->device_kind = FU_VLI_DEVICE_KIND_VL812Q4S;

		/* VL812 == VT3470 (812/813) */
		} else if (self->hdr.strapping1 & FU_VLI_USBHUB_HEADER_STRAPPING1_76PIN) {
			/* is B3 */
			if (self->hdr.strapping1 & FU_VLI_USBHUB_HEADER_STRAPPING1_B3UP)
				self->device_kind = FU_VLI_DEVICE_KIND_VL812B3;
			else
				self->device_kind = FU_VLI_DEVICE_KIND_VL812B0;

		/* VL811P == VT3470 */
		} else {
			/* is B3 */
			if (self->hdr.strapping1 & FU_VLI_USBHUB_HEADER_STRAPPING1_B3UP)
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
		/* VL820 == VT3518 */
		if (!fu_common_read_uint8_safe (buf, bufsz, 0xf000, &tmp, error)) {
			g_prefix_error (error, "failed to get Q7/Q8 difference: ");
			return FALSE;
		}
		if (tmp & (1 << 0))
			self->device_kind = FU_VLI_DEVICE_KIND_VL820Q8;
		else
			self->device_kind = FU_VLI_DEVICE_KIND_VL820Q7;
		break;
	case 0x0538:
		/* VL817 == VT3538 */
		self->device_kind = FU_VLI_DEVICE_KIND_VL817;
		break;
	case 0x0553:
		/* VL120 == VT3553 */
		self->device_kind = FU_VLI_DEVICE_KIND_VL120;
		break;
	case 0x0557:
		/* VL819 == VT3557 */
		self->device_kind = FU_VLI_DEVICE_KIND_VL819;
		break;
	default:
		break;
	}

	/* whole image */
	fu_firmware_add_image (firmware, img);
	return TRUE;
}

static void
fu_vli_usbhub_firmware_init (FuVliUsbhubFirmware *self)
{
}

static void
fu_vli_usbhub_firmware_class_init (FuVliUsbhubFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_vli_usbhub_firmware_parse;
	klass_firmware->to_string = fu_vli_usbhub_firmware_to_string;
}

FuFirmware *
fu_vli_usbhub_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_VLI_USBHUB_FIRMWARE, NULL));
}
