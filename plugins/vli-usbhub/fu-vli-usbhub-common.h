/*
 * Copyright (C) 2017-2019 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

typedef enum {
	FU_VLI_USBHUB_DEVICE_KIND_VL120		= 0x0120,
	FU_VLI_USBHUB_DEVICE_KIND_VL210		= 0x0210,
	FU_VLI_USBHUB_DEVICE_KIND_VL211		= 0x0211,
	FU_VLI_USBHUB_DEVICE_KIND_VL212		= 0x0212,
	FU_VLI_USBHUB_DEVICE_KIND_VL810		= 0x0810,
	FU_VLI_USBHUB_DEVICE_KIND_VL811		= 0x0811,
	FU_VLI_USBHUB_DEVICE_KIND_VL811PB0	= 0x8110,
	FU_VLI_USBHUB_DEVICE_KIND_VL811PB3	= 0x8113,
	FU_VLI_USBHUB_DEVICE_KIND_VL812B0	= 0xA812,
	FU_VLI_USBHUB_DEVICE_KIND_VL812B3	= 0xB812,
	FU_VLI_USBHUB_DEVICE_KIND_VL812Q4S	= 0xC812,
	FU_VLI_USBHUB_DEVICE_KIND_VL813		= 0x0813,
	FU_VLI_USBHUB_DEVICE_KIND_VL815		= 0x0815,
	FU_VLI_USBHUB_DEVICE_KIND_VL817		= 0x0817,
	FU_VLI_USBHUB_DEVICE_KIND_VL819		= 0x0819,
	FU_VLI_USBHUB_DEVICE_KIND_VL820Q7	= 0xA820,
	FU_VLI_USBHUB_DEVICE_KIND_VL820Q8	= 0xB820,
} FuVliUsbhubDeviceKind;

typedef struct __attribute__ ((packed)) {
	guint16		 dev_id;		/* 0x00, BE */
	guint8		 unknown_02;		/* 0x02 */
	guint8		 unknown_03;		/* 0x03 */
	guint16		 u3_addr;		/* 0x04 */
	guint16		 u3_sz;			/* 0x06, BE */
	guint16		 u2_addr;		/* 0x08 */
	guint16		 unknown_0a;		/* 0x0a */
	guint8		 u3_addr_h;		/* 0x0c */
	guint8		 unknown_0d[15];	/* 0x0d */
	guint8		 prev_ptr;		/* 0x1c */
	guint8		 next_ptr;		/* 0x1d */
	guint8		 unknown_1e;		/* 0x1e */
	guint8		 checksum;		/* 0x1f */
} FuVliUsbhubHeader;

G_STATIC_ASSERT(sizeof(FuVliUsbhubHeader) == 0x20);

guint8		 fu_vli_usbhub_header_crc8		(FuVliUsbhubHeader	*hdr);
void		 fu_vli_usbhub_header_to_string		(FuVliUsbhubHeader	*hdr,
							 guint			 idt,
							 GString		 *str);
const gchar	*fu_vli_usbhub_device_kind_to_string	(FuVliUsbhubDeviceKind	 device_kind);
