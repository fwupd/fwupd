/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#define USB_HID_REPORT_ID1 1
#define USB_HID_REPORT_ID2 2

#define USBUID_ISP_DEVICE_CMD_MCU_NONE		   0x0
#define USBUID_ISP_DEVICE_CMD_MCU_STATUS	   0x1
#define USBUID_ISP_DEVICE_CMD_MCU_JUMP2BOOT	   0x2
#define USBUID_ISP_DEVICE_CMD_READ_MCU_VERSIONPAGE 0x3
#define USBUID_ISP_DEVICE_CMD_SET_I225_PWR	   0x4
#define USBUID_ISP_DEVICE_CMD_DOCK_RESET	   0x5
#define USBUID_ISP_DEVICE_CMD_VERSION_WRITEBACK	   0x6
#define USBUID_ISP_INTERNAL_FW_CMD_INITAL	   0xA0
#define USBUID_ISP_INTERNAL_FW_CMD_UPDATE_FW	   0xA1
#define USBUID_ISP_INTERNAL_FW_CMD_TARGET_CHECKSUM 0xA2
#define USBUID_ISP_INTERNAL_FW_CMD_ISP_END	   0xA3

#define TAG_TAG2_ISP_BOOT      0    /* before Common CMD for bootload, with TAG0, TAG1, CMD */
#define TAG_TAG2_ISP	       0x5a /* before Common, with TAG0, TAG1, CMD */
#define TAG_TAG2_CMD_MCU       0x6a /* USB->MCU(Common-cmd mode), with TAG0, TAG1, CMD */
#define TAG_TAG2_CMD_SPI       0x7a /* USB->MCU->SPI(Common-cmd mode), with TAG0, TAG1, CMD */
#define TAG_TAG2_CMD_I2C       0x8a /* USB->MCU->I2C(Mass data transmission) */
#define TAG_TAG2_MASS_DATA_MCU 0x6b /* MASS data transfer for MCU 0xA0 */
#define TAG_TAG2_MASS_DATA_SPI 0x7b /* MASS data transfer for External flash 0xA1 */
#define TAG_TAG2_MASS_DATA_I2C 0x8b /* MASS data transfer for TBT flash */

#define DP_VERSION_FROM_MCU  0x01 /* if in use */
#define NIC_VERSION_FROM_MCU 0x2  /* if in use */

#define External_Valid_Value 0x37

#define FIRMWARE_IDX_NONE   0x00
#define FIRMWARE_IDX_DMC_PD 0x01
#define FIRMWARE_IDX_DP	    0x02
#define FIRMWARE_IDX_TBT4   0x04
#define FIRMWARE_IDX_USB3   0x08
#define FIRMWARE_IDX_USB2   0x10
#define FIRMWARE_IDX_AUDIO  0x20
#define FIRMWARE_IDX_I225   0x40
#define FIRMWARE_IDX_MCU    0x80

typedef struct {
	guint8 DMC[5];
	guint8 PD[5];
	guint8 DP5x[5];
	guint8 DP6x[5];
	guint8 TBT4[5];
	guint8 USB3[5];
	guint8 USB2[5];
	guint8 AUDIO[5];
	guint8 I255[5];
	guint8 MCU[2];
	guint8 bcdVersion[2];
} IspVersionInMcu_t;

const gchar *
fu_lenovo_dock_idx_to_string(guint8 val);
