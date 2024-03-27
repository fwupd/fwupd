/*
 * Copyright 2017 VIA Corporation
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-vli-common.h"

#define FU_VLI_USBHUB_HEADER_STRAPPING1_SELFW1 (1 << 1)
#define FU_VLI_USBHUB_HEADER_STRAPPING1_76PIN  (1 << 2)
#define FU_VLI_USBHUB_HEADER_STRAPPING1_B3UP   (1 << 3)
#define FU_VLI_USBHUB_HEADER_STRAPPING1_LPC    (1 << 4)
#define FU_VLI_USBHUB_HEADER_STRAPPING1_U1U2   (1 << 5)
#define FU_VLI_USBHUB_HEADER_STRAPPING1_BC     (1 << 6)
#define FU_VLI_USBHUB_HEADER_STRAPPING1_Q4S    (1 << 7)

#define FU_VLI_USBHUB_HEADER_STRAPPING2_IDXEN  (1 << 0)
#define FU_VLI_USBHUB_HEADER_STRAPPING2_FWRTY  (1 << 1)
#define FU_VLI_USBHUB_HEADER_STRAPPING2_SELFW2 (1 << 7)

#define VLI_USBHUB_FLASHMAP_ADDR_TO_IDX(addr) (addr / 0x20)
#define VLI_USBHUB_FLASHMAP_IDX_TO_ADDR(addr) (addr * 0x20)

#define VLI_USBHUB_FLASHMAP_IDX_HD1	0x00 /* factory firmware */
#define VLI_USBHUB_FLASHMAP_IDX_HD2	0x80 /* update firmware */
#define VLI_USBHUB_FLASHMAP_IDX_INVALID 0xff

#define VLI_USBHUB_FLASHMAP_ADDR_HD1	    0x0
#define VLI_USBHUB_FLASHMAP_ADDR_HD1_BACKUP 0x1800
#define VLI_USBHUB_FLASHMAP_ADDR_HD2	    0x1000
#define VLI_USBHUB_FLASHMAP_ADDR_FW	    0x2000
#define VLI_USBHUB_FLASHMAP_ADDR_PD_LEGACY  0x10000
#define VLI_USBHUB_FLASHMAP_ADDR_PD	    0x20000
#define VLI_USBHUB_FLASHMAP_ADDR_PD_BACKUP  0x30000
