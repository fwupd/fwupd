/*
 * Copyright (C) 2017-2019 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

typedef enum {
	FU_VLI_USBHUB_PD_CHIP_UNKNOWN		= 0x0,
	FU_VLI_USBHUB_PD_CHIP_VL100		= 0x100,
	FU_VLI_USBHUB_PD_CHIP_VL101		= 0x101,
	FU_VLI_USBHUB_PD_CHIP_VL102		= 0x102,
	FU_VLI_USBHUB_PD_CHIP_VL103		= 0x103,
	FU_VLI_USBHUB_PD_CHIP_VL104		= 0x104,
	FU_VLI_USBHUB_PD_CHIP_VL105		= 0x105,
} FuVliUsbhubPdChip;

typedef struct __attribute__ ((packed)) {
	guint32		fwver;	/* BE */
	guint16		vid;	/* LE */
	guint16		pid;	/* LE */
} FuVliUsbhubPdHdr;

#define VLI_USBHUB_PD_FLASHMAP_ADDR_LEGACY		0x4000
#define VLI_USBHUB_PD_FLASHMAP_ADDR			0x1003

guint16		 fu_vli_usbhub_pd_crc16			(const guint8		*buf,
							 gsize			 bufsz);
guint32		 fu_vli_usbhub_pd_chip_get_offset	(FuVliUsbhubPdChip	 chip);
guint32		 fu_vli_usbhub_pd_chip_get_size		(FuVliUsbhubPdChip	 chip);
const gchar	*fu_vli_usbhub_pd_chip_to_string	(FuVliUsbhubPdChip	 chip);
FuVliUsbhubPdChip fu_vli_usbhub_pd_guess_chip		(guint32		 fwver);
