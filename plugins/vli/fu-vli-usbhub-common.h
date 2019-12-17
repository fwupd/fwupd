/*
 * Copyright (C) 2017-2019 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#include "fu-vli-common.h"

typedef struct __attribute__ ((packed)) {
	guint16		 dev_id;		/* 0x00, BE */
	guint8		 strapping1;		/* 0x02 */
	guint8		 strapping2;		/* 0x03 */
	guint16		 usb3_fw_addr;		/* 0x04, BE */
	guint16		 usb3_fw_sz;		/* 0x06, BE */
	guint16		 usb2_fw_addr;		/* 0x08, BE */
	guint16		 usb2_fw_sz;		/* 0x0a, BE */
	guint8		 usb3_fw_addr_high;	/* 0x0c */
	guint8		 unknown_0d[3];		/* 0x0d */
	guint8		 usb2_fw_addr_high;	/* 0x10 */
	guint8		 unknown_11[10];	/* 0x11 */
	guint8		 inverse_pe41;		/* 0x1b */
	guint8		 prev_ptr;		/* 0x1c, addr / 0x20 */
	guint8		 next_ptr;		/* 0x1d, addr / 0x20 */
	guint8		 variant;		/* 0x1e */
	guint8		 checksum;		/* 0x1f */
} FuVliUsbhubHeader;

G_STATIC_ASSERT(sizeof(FuVliUsbhubHeader) == 0x20);

#define FU_VLI_USBHUB_HEADER_STRAPPING1_SELFW1	(1 << 1)
#define FU_VLI_USBHUB_HEADER_STRAPPING1_76PIN	(1 << 2)
#define FU_VLI_USBHUB_HEADER_STRAPPING1_B3UP	(1 << 3)
#define FU_VLI_USBHUB_HEADER_STRAPPING1_LPC	(1 << 4)
#define FU_VLI_USBHUB_HEADER_STRAPPING1_U1U2	(1 << 5)
#define FU_VLI_USBHUB_HEADER_STRAPPING1_BC	(1 << 6)
#define FU_VLI_USBHUB_HEADER_STRAPPING1_Q4S	(1 << 7)

#define FU_VLI_USBHUB_HEADER_STRAPPING2_IDXEN	(1 << 0)
#define FU_VLI_USBHUB_HEADER_STRAPPING2_FWRTY	(1 << 1)
#define FU_VLI_USBHUB_HEADER_STRAPPING2_SELFW2	(1 << 7)

#define VLI_USBHUB_FLASHMAP_ADDR_TO_IDX(addr)	(addr / 0x20)
#define VLI_USBHUB_FLASHMAP_IDX_TO_ADDR(addr)	(addr * 0x20)

#define VLI_USBHUB_FLASHMAP_IDX_HD1		0x00	/* factory firmware */
#define VLI_USBHUB_FLASHMAP_IDX_HD2		0x80	/* update firmware */
#define VLI_USBHUB_FLASHMAP_IDX_INVALID		0xff

#define VLI_USBHUB_FLASHMAP_ADDR_HD1		0x0
#define VLI_USBHUB_FLASHMAP_ADDR_HD1_BACKUP	0x1800
#define VLI_USBHUB_FLASHMAP_ADDR_HD2		0x1000
#define VLI_USBHUB_FLASHMAP_ADDR_FW		0x2000
#define VLI_USBHUB_FLASHMAP_ADDR_PD_LEGACY	0x10000
#define VLI_USBHUB_FLASHMAP_ADDR_PD		0x20000
#define VLI_USBHUB_FLASHMAP_ADDR_PD_BACKUP	0x30000

guint8		 fu_vli_usbhub_header_crc8		(FuVliUsbhubHeader	*hdr);
void		 fu_vli_usbhub_header_to_string		(FuVliUsbhubHeader	*hdr,
							 guint			 idt,
							 GString		 *str);
