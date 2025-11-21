/*
 * Copyright 2017 VIA Corporation
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-vli-common.h"

#define VLI_USBHUB_PD_FLASHMAP_ADDR_LEGACY 0x4000
#define VLI_USBHUB_PD_FLASHMAP_ADDR	   0x1003

#define FU_VLI_DEVICE_FW_TAG_VL100A 0x01
#define FU_VLI_DEVICE_FW_TAG_VL100B 0x02
#define FU_VLI_DEVICE_FW_TAG_VL100C 0x03
#define FU_VLI_DEVICE_FW_TAG_VL101A 0x04
#define FU_VLI_DEVICE_FW_TAG_VL101B 0x05
#define FU_VLI_DEVICE_FW_TAG_VL101C 0x06
#define FU_VLI_DEVICE_FW_TAG_VL102A 0x07
#define FU_VLI_DEVICE_FW_TAG_VL102B 0x08
#define FU_VLI_DEVICE_FW_TAG_VL103A 0x09
#define FU_VLI_DEVICE_FW_TAG_VL103B 0x0A
#define FU_VLI_DEVICE_FW_TAG_VL104  0x0B
#define FU_VLI_DEVICE_FW_TAG_VL105  0x0C
#define FU_VLI_DEVICE_FW_TAG_VL106  0x0D
#define FU_VLI_DEVICE_FW_TAG_VL107  0x0E
#define FU_VLI_DEVICE_FW_TAG_VL108A 0xA1
#define FU_VLI_DEVICE_FW_TAG_VL108B 0xB1
#define FU_VLI_DEVICE_FW_TAG_VL109A 0xA2
#define FU_VLI_DEVICE_FW_TAG_VL109B 0xB2

FuVliDeviceKind
fu_vli_pd_common_guess_device_kind(guint32 fwver);
