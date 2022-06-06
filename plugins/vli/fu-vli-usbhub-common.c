/*
 * Copyright (C) 2017 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-vli-usbhub-common.h"

guint8
fu_vli_usbhub_header_crc8(FuVliUsbhubHeader *hdr)
{
	return ~fu_crc8((const guint8 *)hdr, sizeof(*hdr) - 1);
}

void
fu_vli_usbhub_header_export(FuVliUsbhubHeader *hdr, XbBuilderNode *bn)
{
	fu_xmlb_builder_insert_kx(bn, "dev_id", GUINT16_FROM_BE(hdr->dev_id));
	fu_xmlb_builder_insert_kx(bn, "variant", hdr->variant);
	if (hdr->usb2_fw_sz > 0) {
		fu_xmlb_builder_insert_kx(bn, "usb2_fw_addr", GUINT16_FROM_BE(hdr->usb2_fw_addr));
		fu_xmlb_builder_insert_kx(bn, "usb2_fw_sz", GUINT16_FROM_BE(hdr->usb2_fw_sz));
	}
	fu_xmlb_builder_insert_kx(bn,
				  "usb3_fw_addr",
				  ((guint32)hdr->usb3_fw_addr_high) << 16 |
				      GUINT16_FROM_BE(hdr->usb3_fw_addr));
	fu_xmlb_builder_insert_kx(bn, "usb3_fw_sz", GUINT16_FROM_BE(hdr->usb3_fw_sz));
	if (hdr->prev_ptr != VLI_USBHUB_FLASHMAP_IDX_INVALID) {
		fu_xmlb_builder_insert_kx(bn,
					  "prev_ptr",
					  VLI_USBHUB_FLASHMAP_IDX_TO_ADDR(hdr->prev_ptr));
	}
	if (hdr->next_ptr != VLI_USBHUB_FLASHMAP_IDX_INVALID) {
		fu_xmlb_builder_insert_kx(bn,
					  "next_ptr",
					  VLI_USBHUB_FLASHMAP_IDX_TO_ADDR(hdr->next_ptr));
	}
	fu_xmlb_builder_insert_kb(bn,
				  "checksum_ok",
				  hdr->checksum == fu_vli_usbhub_header_crc8(hdr));
}

void
fu_vli_usbhub_header_to_string(FuVliUsbhubHeader *hdr, guint idt, GString *str)
{
	g_autoptr(XbBuilderNode) bn = xb_builder_node_new("header");
	g_autofree gchar *xml = NULL;
	fu_vli_usbhub_header_export(hdr, bn);
	xml = xb_builder_node_export(bn,
				     XB_NODE_EXPORT_FLAG_FORMAT_MULTILINE |
#if LIBXMLB_CHECK_VERSION(0, 2, 2)
					 XB_NODE_EXPORT_FLAG_COLLAPSE_EMPTY |
#endif
					 XB_NODE_EXPORT_FLAG_FORMAT_INDENT,
				     NULL);
	fu_string_append(str, idt, "xml", xml);
}
