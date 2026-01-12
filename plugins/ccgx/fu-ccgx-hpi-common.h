/*
 * Copyright 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

/*
 * NOTE: DO NOT ALLOW ANY MORE MAGIC CONSTANTS IN THIS FILE
 * nocheck:magic-defines=33
 */

#define I2C_READ_WRITE_DELAY_MS 10 /* ms */

#define CY_SCB_INDEX_POS	      15
#define CY_I2C_WRITE_COMMAND_POS      3
#define CY_I2C_WRITE_COMMAND_LEN_POS  4
#define CY_I2C_GET_STATUS_LEN	      3
#define CY_I2C_MODE_WRITE	      1
#define CY_I2C_MODE_READ	      0
#define CY_I2C_ERROR_BIT	      1
#define CY_I2C_ARBITRATION_ERROR_BIT  (1 << 1)
#define CY_I2C_NAK_ERROR_BIT	      (1 << 2)
#define CY_I2C_BUS_ERROR_BIT	      (1 << 3)
#define CY_I2C_STOP_BIT_ERROR	      (1 << 4)
#define CY_I2C_BUS_BUSY_ERROR	      (1 << 5)
#define CY_I2C_ENABLE_PRECISE_TIMING  1
#define CY_I2C_EVENT_NOTIFICATION_LEN 3

#define PD_I2C_TARGET_ADDRESS 0x08

/* timeout (ms)	for  USB I2C communication */
#define FU_CCGX_HPI_WAIT_TIMEOUT 5000

/* max i2c frequency */
#define FU_CCGX_HPI_FREQ 400000

typedef struct __attribute__((packed)) { /* nocheck:blocked */
	guint32 frequency;     /* frequency of operation. Only valid values are 100KHz and 400KHz */
	guint8 target_address; /* target address to be used when in target mode */
	guint8 is_msb_first;   /* whether to transmit most significant bit first */
	guint8 is_initiator;   /* whether to block is to be configured as a initiator */
	guint8 s_ignore;       /* ignore general call in target mode */
	guint8 is_clock_stretch; /* whether to stretch clock in case of no FIFO availability */
	guint8 is_loop_back; /* whether to loop back	TX data to RX. Valid only for debug purposes
			      */
	guint8 reserved[6];
} FuCcgxI2cConfig;

typedef struct __attribute__((packed)) { /* nocheck:blocked */
	guint16 event_code;
	guint16 event_length;
	guint8 event_data[128];
} FuCcgxHpiEvent;

#define FU_CCGX_PD_RESP_BRIDGE_MODE_CMD_SIG	     0x42
#define FU_CCGX_PD_RESP_GET_SILICON_ID_CMD_SIG	     0x53
#define FU_CCGX_PD_RESP_REG_INTR_REG_CLEAR_RQT	     0x01
#define FU_CCGX_PD_RESP_JUMP_TO_BOOT_CMD_SIG	     0x4A
#define FU_CCGX_PD_RESP_JUMP_TO_ALT_FW_CMD_SIG	     0x41
#define FU_CCGX_PD_RESP_DEVICE_RESET_CMD_SIG	     0x52
#define FU_CCGX_PD_RESP_REG_RESET_DEVICE_CMD	     0x01
#define FU_CCGX_PD_RESP_ENTER_FLASHING_MODE_CMD_SIG  0x50
#define FU_CCGX_PD_RESP_FLASH_READ_WRITE_CMD_SIG     0x46
#define FU_CCGX_PD_RESP_REG_FLASH_ROW_READ_CMD	     0x00
#define FU_CCGX_PD_RESP_REG_FLASH_ROW_WRITE_CMD	     0x01
#define FU_CCGX_PD_RESP_REG_FLASH_READ_WRITE_ROW_LSB 0x02
#define FU_CCGX_PD_RESP_REG_FLASH_READ_WRITE_ROW_MSB 0x03
#define FU_CCGX_PD_RESP_U_VDM_TYPE		     0x00

#define HPI_GET_SILICON_ID_CMD_SIG	 0x53
#define HPI_REG_INTR_REG_CLEAR_RQT	 0x01
#define HPI_JUMP_TO_BOOT_CMD_SIG	 0x4A
#define HPI_DEVICE_RESET_CMD_SIG	 0x52
#define HPI_REG_RESET_DEVICE_CMD	 0x01
#define HPI_ENTER_FLASHING_MODE_CMD_SIG	 0x50
#define HPI_FLASH_READ_WRITE_CMD_SIG	 0x46
#define HPI_REG_FLASH_ROW_READ_CMD	 0x00
#define HPI_REG_FLASH_ROW_WRITE_CMD	 0x01
#define HPI_REG_FLASH_READ_WRITE_ROW_LSB 0x02
#define HPI_REG_FLASH_READ_WRITE_ROW_MSB 0x03
#define HPI_PORT_DISABLE_CMD		 0x11

#define HPI_DEVICE_VERSION_SIZE_HPIV1 16
#define HPI_DEVICE_VERSION_SIZE_HPIV2 24
#define HPI_META_DATA_OFFSET_ROW_128  64
#define HPI_META_DATA_OFFSET_ROW_256  (64 + 128)

#define PD_I2C_USB_EP_BULK_OUT	0x01
#define PD_I2C_USB_EP_BULK_IN	0x82
#define PD_I2C_USB_EP_INTR_IN	0x83
#define PD_I2CM_USB_EP_BULK_OUT 0x02
#define PD_I2CM_USB_EP_BULK_IN	0x83
#define PD_I2CM_USB_EP_INTR_IN	0x84
