/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

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

typedef enum {
	CY_GET_VERSION_CMD = 0xB0,     /* get the version of the boot-loader
					* value = 0, index = 0, length = 4;
					* data_in = 32 bit version */
	CY_GET_SIGNATURE_CMD = 0xBD,   /* get the signature of the firmware
					* It is suppose to be 'CYUS' for normal firmware
					* and 'CYBL' for Bootloader */
	CY_UART_GET_CONFIG_CMD = 0xC0, /* retrieve the 16 byte UART configuration information
					*  MS bit of value indicates the SCB index
					* length = 16, data_in = 16 byte configuration */
	CY_UART_SET_CONFIG_CMD,	       /* update the 16 byte UART configuration information
					* MS bit of value indicates the SCB index.
					* length = 16, data_out = 16 byte configuration information */
	CY_SPI_GET_CONFIG_CMD,	       /* retrieve the 16 byte SPI configuration information
					* MS bit of value indicates the SCB index
					* length = 16, data_in = 16 byte configuration */
	CY_SPI_SET_CONFIG_CMD,	       /* update the 16 byte SPI configuration	information
					* MS bit of value indicates the SCB index
					* length = 16, data_out = 16 byte configuration information */
	CY_I2C_GET_CONFIG_CMD,	       /* retrieve the 16 byte I2C configuration information
					* MS bit of value indicates the SCB index
					* length = 16, data_in = 16 byte configuration */
	CY_I2C_SET_CONFIG_CMD =
	    0xC5,	       /* update the 16 byte I2C configuration information
				* MS bit of value indicates the SCB index
				* length = 16, data_out = 16 byte configuration information */
	CY_I2C_WRITE_CMD,      /* perform I2C write operation
				* value = bit0 - start, bit1 - stop, bit3 - start on idle,
				* bits[14:8] - target address, bit15 - scbIndex. length = 0 the
				* data	is provided over the bulk endpoints */
	CY_I2C_READ_CMD,       /* perform I2C read operation.
				* value = bit0 - start, bit1 - stop, bit2 - Nak last byte,
				* bit3 - start on idle, bits[14:8] - target address, bit15 - scbIndex,
				* length = 0. The data is provided over the bulk endpoints */
	CY_I2C_GET_STATUS_CMD, /* retrieve the I2C bus status.
				* value = bit0 - 0: TX 1: RX, bit15 - scbIndex, length = 3,
				* data_in = byte0: bit0 - flag, bit1 -	bus_state, bit2 - SDA state,
				* bit3 - TX underflow, bit4 - arbitration error, bit5 - NAK
				* bit6 - bus error,
				* byte[2:1] Data count remaining */
	CY_I2C_RESET_CMD,      /* the command cleans up the I2C state machine and frees the bus
				* value = bit0 - 0: TX path, 1: RX path; bit15 - scbIndex,
				* length = 0 */
	CY_SPI_READ_WRITE_CMD = 0xCA, /* the command starts a read / write operation at SPI
				       * value = bit 0 - RX enable, bit 1 - TX enable, bit 15 -
				       * scbIndex; index = length of transfer */
	CY_SPI_RESET_CMD,	   /* the command resets the SPI pipes and allows it to receive new
				    * request
				    * value = bit 15 - scbIndex */
	CY_SPI_GET_STATUS_CMD,	   /* the command returns the current transfer status
				    * the count will match the TX pipe status at SPI end
				    * for completion of read, read all data
				    * at the USB end signifies the	end of transfer
				    * value = bit 15 - scbIndex */
	CY_JTAG_ENABLE_CMD = 0xD0, /* enable JTAG module */
	CY_JTAG_DISABLE_CMD,	   /* disable JTAG module */
	CY_JTAG_READ_CMD,	   /* jtag read vendor command */
	CY_JTAG_WRITE_CMD,	   /* jtag write vendor command */
	CY_GPIO_GET_CONFIG_CMD = 0xD8, /* get the GPIO configuration */
	CY_GPIO_SET_CONFIG_CMD,	       /* set the GPIO configuration */
	CY_GPIO_GET_VALUE_CMD,	       /* get GPIO value */
	CY_GPIO_SET_VALUE_CMD,	       /* set the GPIO value */
	CY_PROG_USER_FLASH_CMD = 0xE0, /* program user flash area. The total space available is 512
					* bytes this can be accessed by the user from USB. The flash
					* area address offset is from 0x0000 to 0x00200 and can be
					* written to page wise (128 byte) */
	CY_READ_USER_FLASH_CMD,	    /* read user flash area. The total space available is 512 bytes
				     * this	can be accessed by the user from USB. The flash	area
				     * address offset is from 0x0000 to 0x00200 and can be written to
				     * page wise (128 byte) */
	CY_DEVICE_RESET_CMD = 0xE3, /* performs a device reset from firmware */
} CyVendorCommand;

typedef struct __attribute__((packed)) {
	guint32 frequency;     /* frequency of operation. Only valid values are 100KHz and 400KHz */
	guint8 target_address; /* target address to be used when in target mode */
	guint8 is_msb_first;   /* whether to transmit most significant bit first */
	guint8 is_initiator;   /* whether to block is to be configured as a initiator */
	guint8 s_ignore;       /* ignore general call in target mode */
	guint8 is_clock_stretch; /* whether to stretch clock in case of no FIFO availability */
	guint8 is_loop_back; /* whether to loop back	TX data to RX. Valid only for debug purposes
			      */
	guint8 reserved[6];
} CyI2CConfig;

typedef enum {
	CY_I2C_DATA_CONFIG_NONE = 0,
	CY_I2C_DATA_CONFIG_STOP = 1 << 0,
	CY_I2C_DATA_CONFIG_NAK = 1 << 1, /* only for read */
} CyI2CDataConfigBits;

typedef enum {
	HPI_DEV_REG_DEVICE_MODE = 0,
	HPI_DEV_REG_BOOT_MODE_REASON,
	HPI_DEV_REG_SI_ID,
	HPI_DEV_REG_SI_ID_LSB,
	HPI_DEV_REG_BL_LAST_ROW,
	HPI_DEV_REG_BL_LAST_ROW_LSB,
	HPI_DEV_REG_INTR_ADDR,
	HPI_DEV_REG_JUMP_TO_BOOT,
	HPI_DEV_REG_RESET_ADDR,
	HPI_DEV_REG_RESET_CMD,
	HPI_DEV_REG_ENTER_FLASH_MODE,
	HPI_DEV_REG_VALIDATE_FW_ADDR,
	HPI_DEV_REG_FLASH_READ_WRITE,
	HPI_DEV_REG_FLASH_READ_WRITE_CMD,
	HPI_DEV_REG_FLASH_ROW,
	HPI_DEV_REG_FLASH_ROW_LSB,
	HPI_DEV_REG_ALL_VERSION,
	HPI_DEV_REG_ALL_VERSION_BYTE_1,
	HPI_DEV_REG_ALL_VERSION_BYTE_2,
	HPI_DEV_REG_ALL_VERSION_BYTE_3,
	HPI_DEV_REG_ALL_VERSION_BYTE_4,
	HPI_DEV_REG_ALL_VERSION_BYTE_5,
	HPI_DEV_REG_ALL_VERSION_BYTE_6,
	HPI_DEV_REG_ALL_VERSION_BYTE_7,
	HPI_DEV_REG_ALL_VERSION_BYTE_8,
	HPI_DEV_REG_ALL_VERSION_BYTE_9,
	HPI_DEV_REG_ALL_VERSION_BYTE_10,
	HPI_DEV_REG_ALL_VERSION_BYTE_11,
	HPI_DEV_REG_ALL_VERSION_BYTE_12,
	HPI_DEV_REG_ALL_VERSION_BYTE_13,
	HPI_DEV_REG_ALL_VERSION_BYTE_14,
	HPI_DEV_REG_ALL_VERSION_BYTE_15,
	HPI_DEV_REG_FW_2_VERSION,
	HPI_DEV_REG_FW_2_VERSION_BYTE_1,
	HPI_DEV_REG_FW_2_VERSION_BYTE_2,
	HPI_DEV_REG_FW_2_VERSION_BYTE_3,
	HPI_DEV_REG_FW_2_VERSION_BYTE_4,
	HPI_DEV_REG_FW_2_VERSION_BYTE_5,
	HPI_DEV_REG_FW_2_VERSION_BYTE_6,
	HPI_DEV_REG_FW_2_VERSION_BYTE_7,
	HPI_DEV_REG_FW_BIN_LOC,
	HPI_DEV_REG_FW_1_BIN_LOC_LSB,
	HPI_DEV_REG_FW_2_BIN_LOC_MSB,
	HPI_DEV_REG_FW_2_BIN_LOC_LSB,
	HPI_DEV_REG_PORT_ENABLE,
	HPI_DEV_SPACE_REG_LEN,
	HPI_DEV_REG_RESPONSE = 0x007E,
	HPI_DEV_REG_FLASH_MEM = 0x0200
} HPIDevReg;

typedef enum {
	HPI_REG_SECTION_DEV = 0, /* device information */
	HPI_REG_SECTION_PORT_0,	 /* USB-PD Port 0 related */
	HPI_REG_SECTION_PORT_1,	 /* USB-PD Port 1 related */
	HPI_REG_SECTION_ALL	 /* select all registers */
} HPIRegSection;

typedef struct __attribute__((packed)) {
	guint16 event_code;
	guint16 event_length;
	guint8 event_data[128];
} HPIEvent;

typedef enum {
	HPI_REG_PART_REG = 0,	       /* register region */
	HPI_REG_PART_DATA = 1,	       /* data memory */
	HPI_REG_PART_FLASH = 2,	       /* flash memory	*/
	HPI_REG_PART_PDDATA_READ = 4,  /* read data memory */
	HPI_REG_PART_PDDATA_WRITE = 8, /* write data memory */
} HPIRegPart;

typedef enum {
	CY_PD_REG_DEVICE_MODE_ADDR,
	CY_PD_BOOT_MODE_REASON,
	CY_PD_SILICON_ID,
	CY_PD_BL_LAST_ROW = 0x04,
	CY_PD_REG_INTR_REG_ADDR = 0x06,
	CY_PD_JUMP_TO_BOOT_REG_ADDR,
	CY_PD_REG_RESET_ADDR,
	CY_PD_REG_ENTER_FLASH_MODE_ADDR = 0x0A,
	CY_PD_REG_VALIDATE_FW_ADDR,
	CY_PD_REG_FLASH_READ_WRITE_ADDR,
	CY_PD_GET_VERSION = 0x10,
	CY_PD_REG_DBG_PD_INIT = 0x12,
	CY_PD_REG_U_VDM_CTRL_ADDR = 0x20,
	CY_PD_REG_READ_PD_PROFILE = 0x22,
	CY_PD_REG_EFFECTIVE_SOURCE_PDO_MASK = 0x24,
	CY_PD_REG_EFFECTIVE_SINK_PDO_MASK,
	CY_PD_REG_SELECT_SOURCE_PDO,
	CY_PD_REG_SELECT_SINK_PDO,
	CY_PD_REG_PD_CONTROL,
	CY_PD_REG_PD_STATUS = 0x2C,
	CY_PD_REG_TYPE_C_STATUS = 0x30,
	CY_PD_REG_CURRENT_PDO = 0x34,
	CY_PD_REG_CURRENT_RDO = 0x38,
	CY_PD_REG_CURRENT_CABLE_VDO = 0x3C,
	CY_PD_REG_DISPLAY_PORT_STATUS = 0x40,
	CY_PD_REG_DISPLAY_PORT_CONFIG = 0x44,
	CY_PD_REG_ALTERNATE_MODE_MUX_SELECTION = 0X45,
	CY_PD_REG_EVENT_MASK = 0x48,
	CY_PD_REG_RESPONSE_ADDR = 0x7E,
	CY_PD_REG_BOOTDATA_MEMORY_ADDR = 0x80,
	CY_PD_REG_FWDATA_MEMORY_ADDR = 0xC0,
} CyPDReg;

#define CY_PD_BRIDGE_MODE_CMD_SIG	   0x42
#define CY_PD_GET_SILICON_ID_CMD_SIG	   0x53
#define CY_PD_REG_INTR_REG_CLEAR_RQT	   0x01
#define CY_PD_JUMP_TO_BOOT_CMD_SIG	   0x4A
#define CY_PD_JUMP_TO_ALT_FW_CMD_SIG	   0x41
#define CY_PD_DEVICE_RESET_CMD_SIG	   0x52
#define CY_PD_REG_RESET_DEVICE_CMD	   0x01
#define CY_PD_ENTER_FLASHING_MODE_CMD_SIG  0x50
#define CY_PD_FLASH_READ_WRITE_CMD_SIG	   0x46
#define CY_PD_REG_FLASH_ROW_READ_CMD	   0x00
#define CY_PD_REG_FLASH_ROW_WRITE_CMD	   0x01
#define CY_PD_REG_FLASH_READ_WRITE_ROW_LSB 0x02
#define CY_PD_REG_FLASH_READ_WRITE_ROW_MSB 0x03
#define CY_PD_U_VDM_TYPE		   0x00
#define HPI_GET_SILICON_ID_CMD_SIG	   0x53
#define HPI_REG_INTR_REG_CLEAR_RQT	   0x01
#define HPI_JUMP_TO_BOOT_CMD_SIG	   0x4A
#define HPI_DEVICE_RESET_CMD_SIG	   0x52
#define HPI_REG_RESET_DEVICE_CMD	   0x01
#define HPI_ENTER_FLASHING_MODE_CMD_SIG	   0x50
#define HPI_FLASH_READ_WRITE_CMD_SIG	   0x46
#define HPI_REG_FLASH_ROW_READ_CMD	   0x00
#define HPI_REG_FLASH_ROW_WRITE_CMD	   0x01
#define HPI_REG_FLASH_READ_WRITE_ROW_LSB   0x02
#define HPI_REG_FLASH_READ_WRITE_ROW_MSB   0x03
#define HPI_PORT_DISABLE_CMD		   0x11

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

typedef enum {
	/* responses */
	CY_PD_RESP_NO_RESPONSE,
	CY_PD_RESP_SUCCESS = 0x02,
	CY_PD_RESP_FLASH_DATA_AVAILABLE,
	CY_PD_RESP_INVALID_COMMAND = 0x05,
	CY_PD_RESP_COLLISION_DETECTED,
	CY_PD_RESP_FLASH_UPDATE_FAILED,
	CY_PD_RESP_INVALID_FW,
	CY_PD_RESP_INVALID_ARGUMENTS,
	CY_PD_RESP_NOT_SUPPORTED,
	CY_PD_RESP_TRANSACTION_FAILED = 0x0C,
	CY_PD_RESP_PD_COMMAND_FAILED,
	CY_PD_RESP_UNDEFINED,
	CY_PD_RESP_RA_DETECT = 0x10,
	CY_PD_RESP_RA_REMOVED,

	/* device specific events */
	CY_PD_RESP_RESET_COMPLETE = 0x80,
	CY_PD_RESP_MESSAGE_QUEUE_OVERFLOW,

	/* type-c specific events */
	CY_PD_RESP_OVER_CURRENT_DETECTED,
	CY_PD_RESP_OVER_VOLTAGE_DETECTED,
	CY_PD_RESP_TYPC_C_CONNECTED,
	CY_PD_RESP_TYPE_C_DISCONNECTED,

	/* pd specific events and asynchronous messages */
	CY_PD_RESP_PD_CONTRACT_ESTABLISHED,
	CY_PD_RESP_DR_SWAP,
	CY_PD_RESP_PR_SWAP,
	CY_PD_RESP_VCON_SWAP,
	CY_PD_RESP_PS_RDY,
	CY_PD_RESP_GOTOMIN,
	CY_PD_RESP_ACCEPT_MESSAGE,
	CY_PD_RESP_REJECT_MESSAGE,
	CY_PD_RESP_WAIT_MESSAGE,
	CY_PD_RESP_HARD_RESET,
	CY_PD_RESP_VDM_RECEIVED,
	CY_PD_RESP_SRC_CAP_RCVD,
	CY_PD_RESP_SINK_CAP_RCVD,
	CY_PD_RESP_DP_ALTERNATE_MODE,
	CY_PD_RESP_DP_DEVICE_CONNECTED,
	CY_PD_RESP_DP_DEVICE_NOT_CONNECTED,
	CY_PD_RESP_DP_SID_NOT_FOUND,
	CY_PD_RESP_MULTIPLE_SVID_DISCOVERED,
	CY_PD_RESP_DP_FUNCTION_NOT_SUPPORTED,
	CY_PD_RESP_DP_PORT_CONFIG_NOT_SUPPORTED,
	CY_PD_HARD_RESET_SENT,
	CY_PD_SOFT_RESET_SENT,
	CY_PD_CABLE_RESET_SENT,
	CY_PD_SOURCE_DISABLED_STATE_ENTERED,
	CY_PD_SENDER_RESPONSE_TIMER_TIMEOUT,
	CY_PD_NO_VDM_RESPONSE_RECEIVED
} CyPDResp;

typedef enum {
	HPI_RESPONSE_NO_RESPONSE,
	HPI_RESPONSE_SUCCESS = 0x02,
	HPI_RESPONSE_FLASH_DATA_AVAILABLE,
	HPI_RESPONSE_INVALID_COMMAND = 0x05,
	HPI_RESPONSE_FLASH_UPDATE_FAILED = 0x07,
	HPI_RESPONSE_INVALID_FW,
	HPI_RESPONSE_INVALID_ARGUMENT,
	HPI_RESPONSE_NOT_SUPPORTED,
	HPI_RESPONSE_PD_TRANSACTION_FAILED = 0x0C,
	HPI_RESPONSE_PD_COMMAND_FAILED,
	HPI_RESPONSE_UNDEFINED_ERROR = 0x0F,
	HPI_EVENT_RESET_COMPLETE = 0x80,
	HPI_EVENT_MSG_OVERFLOW,
	HPI_EVENT_OC_DETECT,
	HPI_EVENT_OV_DETECT,
	HPI_EVENT_CONNECT_DETECT,
	HPI_EVENT_DISCONNECT_DETECT,
	HPI_EVENT_NEGOTIATION_COMPLETE,
	HPI_EVENT_SWAP_COMPLETE,
	HPI_EVENT_PS_RDY_RECEIVED = 0x8A,
	HPI_EVENT_GOTO_MIN_RECEIVED,
	HPI_EVENT_ACCEPT_RECEIVED,
	HPI_EVENT_REJECT_RECEIVED,
	HPI_EVENT_WAIT_RECEIVED,
	HPI_EVENT_HARD_RESET_RECEIVED,
	HPI_EVENT_VDM_RECEIVED = 0x90,
	HPI_EVENT_SOURCE_CAP_RECEIVED,
	HPI_EVENT_SINK_CAP_RECEIVED,
	HPI_EVENT_DP_MODE_ENTERED,
	HPI_EVENT_DP_STATUS_UPDATE,
	HPI_EVENT_DP_SID_NOT_FOUND = 0x96,
	HPI_EVENT_DP_MANY_SID_FOUND,
	HPI_EVENT_DP_NO_CABLE_SUPPORT,
	HPI_EVENT_DP_NO_UFP_SUPPORT,
	HPI_EVENT_HARD_RESET_SENT,
	HPI_EVENT_SOFT_RESET_SENT,
	HPI_EVENT_CABLE_RESET_SENT,
	HPI_EVENT_SOURCE_DISABLED,
	HPI_EVENT_SENDER_TIMEOUT,
	HPI_EVENT_VDM_NO_RESPONSE,
	HPI_EVENT_UNEXPECTED_VOLTAGE,
	HPI_EVENT_ERROR_RECOVERY,
	HPI_EVENT_EMCA_DETECT = 0xA6,
	HPI_EVENT_RP_CHANGE_DETECT = 0xAA,
	HPI_EVENT_TB_ENTERED = 0xB0,
	HPI_EVENT_TB_EXITED
} HPIResp;

const gchar *
fu_ccgx_pd_resp_to_string(CyPDResp val);
