/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Victor Cheng <victor_cheng@usiglobal.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#define USB_HID_REPORT_ID1 1u
#define USB_HID_REPORT_ID2 2u

#define USBUID_ISP_DEVICE_CMD_MCU_NONE		   0x0
#define USBUID_ISP_DEVICE_CMD_MCU_STATUS	   0x1
#define USBUID_ISP_DEVICE_CMD_MCU_JUMP2BOOT	   0x2
#define USBUID_ISP_DEVICE_CMD_READ_MCU_VERSIONPAGE 0x3
#define USBUID_ISP_DEVICE_CMD_SET_I225_PWR	   0x4
#define USBUID_ISP_DEVICE_CMD_DOCK_RESET	   0x5
#define USBUID_ISP_DEVICE_CMD_VERSION_WRITEBACK	   0x6
#define USBUID_ISP_DEVICE_CMD_SET_CHIP_TYPE	   0x9

#define USBUID_ISP_DEVICE_CMD_FWBUFER_INITIAL	      0x01
#define USBUID_ISP_DEVICE_CMD_FWBUFER_ERASE_FLASH     0x02
#define USBUID_ISP_DEVICE_CMD_FWBUFER_PROGRAM	      0x03
#define USBUID_ISP_DEVICE_CMD_FWBUFER_WRITE_RESPONSE  0x04
#define USBUID_ISP_DEVICE_CMD_FWBUFER_READ_STATUS     0x05
#define USBUID_ISP_DEVICE_CMD_FWBUFER_CHECKSUM	      0x06
#define USBUID_ISP_DEVICE_CMD_FWBUFER_END	      0x07
#define USBUID_ISP_DEVICE_CMD_FWBUFER_TRANSFER_FINISH 0x08
#define USBUID_ISP_DEVICE_CMD_FWBUFER_ERROR_END	      0x09

#define USBUID_ISP_INTERNAL_FW_CMD_INITAL	   0x0A
#define USBUID_ISP_INTERNAL_FW_CMD_UPDATE_FW	   0x0B
#define USBUID_ISP_INTERNAL_FW_CMD_TARGET_CHECKSUM 0x0C
#define USBUID_ISP_INTERNAL_FW_CMD_ISP_END	   0x0D
#define USBUID_ISP_CMD_ALL			   0xFF

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
#define TX_ISP_LENGTH	     61

#define W25Q16DV_PAGE_SIZE 256
