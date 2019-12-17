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
	return fu_vli_common_crc8 ((const guint8 *) hdr, sizeof(hdr) - 1);
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
				    ((guint32) hdr->usb3_fw_addr_high) << 16 |
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
