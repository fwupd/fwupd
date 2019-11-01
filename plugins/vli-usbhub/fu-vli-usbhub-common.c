/*
 * Copyright (C) 2017-2019 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-vli-usbhub-common.h"

guint8
fu_vli_usbhub_header_crc8 (FuVliUsbhubHeader *hdr)
{
	const guint8 *data = (const guint8 *) hdr;
	guint32 crc = 0;
	for (guint32 j = 0x1f; j; j--, data++) {
		crc ^= (*data << 8);
		for (guint32 i = 8; i; i--) {
			if (crc & 0x8000)
				crc ^= (0x1070 << 3);
			crc <<= 1;
		}
	}
	return (guint8) (crc >> 8);
}

const gchar *
fu_vli_usbhub_device_kind_to_string (FuVliUsbhubDeviceKind device_kind)
{
	if (device_kind == FU_VLI_USBHUB_DEVICE_KIND_VL810)
		return "VL810";
	if (device_kind == FU_VLI_USBHUB_DEVICE_KIND_VL811)
		return "VL811";
	if (device_kind == FU_VLI_USBHUB_DEVICE_KIND_VL811PB0)
		return "VL811PB0";
	if (device_kind == FU_VLI_USBHUB_DEVICE_KIND_VL811PB3)
		return "VL811PB3";
	if (device_kind == FU_VLI_USBHUB_DEVICE_KIND_VL812B0)
		return "VL812B0";
	if (device_kind == FU_VLI_USBHUB_DEVICE_KIND_VL812B3)
		return "VL812B3";
	if (device_kind == FU_VLI_USBHUB_DEVICE_KIND_VL812Q4S)
		return "VL812Q4S";
	if (device_kind == FU_VLI_USBHUB_DEVICE_KIND_VL813)
		return "VL813";
	if (device_kind == FU_VLI_USBHUB_DEVICE_KIND_VL815)
		return "VL815";
	if (device_kind == FU_VLI_USBHUB_DEVICE_KIND_VL817)
		return "VL817";
	if (device_kind == FU_VLI_USBHUB_DEVICE_KIND_VL819)
		return "VL819";
	if (device_kind == FU_VLI_USBHUB_DEVICE_KIND_VL820Q7)
		return "VL820Q7";
	if (device_kind == FU_VLI_USBHUB_DEVICE_KIND_VL820Q8)
		return "VL820Q8";
	if (device_kind == FU_VLI_USBHUB_DEVICE_KIND_VL120)
		return "VL120";
	if (device_kind == FU_VLI_USBHUB_DEVICE_KIND_VL210)
		return "VL210";
	if (device_kind == FU_VLI_USBHUB_DEVICE_KIND_VL211)
		return "VL211";
	if (device_kind == FU_VLI_USBHUB_DEVICE_KIND_VL212)
		return "VL212";
	return NULL;
}

void
fu_vli_usbhub_header_to_string (FuVliUsbhubHeader *hdr, guint idt, GString *str)
{
	fu_common_string_append_kx (str, idt, "DevId", GUINT16_FROM_BE(hdr->dev_id));
	fu_common_string_append_kx (str, idt, "Variant", hdr->variant);
	if (hdr->usb2_fw_sz > 0) {
		fu_common_string_append_kx (str, idt, "Usb2FwAddr",
					    GUINT16_FROM_BE(hdr->usb2_fw_addr));
		fu_common_string_append_kx (str, idt, "Usb2FwSz",
					    GUINT16_FROM_BE(hdr->usb2_fw_sz));
	}
	fu_common_string_append_kx (str, idt, "Usb3FwAddr",
				    GUINT16_FROM_BE(hdr->usb3_fw_addr));
	fu_common_string_append_kx (str, idt, "Usb3FwSz",
				    GUINT16_FROM_BE(hdr->usb3_fw_sz));
	if (hdr->prev_ptr != VLI_USBHUB_FLASHMAP_IDX_INVALID) {
		fu_common_string_append_kx (str, idt, "PrevPtr",
					    VLI_USBHUB_FLASHMAP_IDX_TO_ADDR(hdr->prev_ptr));
	}
	if (hdr->next_ptr != VLI_USBHUB_FLASHMAP_IDX_INVALID) {
		fu_common_string_append_kx (str, idt, "NextPtr",
					    VLI_USBHUB_FLASHMAP_IDX_TO_ADDR(hdr->next_ptr));
	}
	fu_common_string_append_kb (str, idt, "ChecksumOK",
				    hdr->checksum == fu_vli_usbhub_header_crc8 (hdr));
}
