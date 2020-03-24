/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

#define I2C_READ_WRITE_DELAY_US		10000 /* 10 msec */

#define CY_SCB_INDEX_POS		15
#define CY_I2C_WRITE_COMMAND_POS	3
#define CY_I2C_WRITE_COMMAND_LEN_POS	4
#define CY_I2C_GET_STATUS_LEN		3
#define CY_I2C_MODE_WRITE		1
#define CY_I2C_MODE_READ		0
#define CY_I2C_ERROR_BIT		1
#define CY_I2C_ARBITRATION_ERROR_BIT	(1 << 1)
#define CY_I2C_NAK_ERROR_BIT		(1 << 2)
#define CY_I2C_BUS_ERROR_BIT		(1 << 3)
#define CY_I2C_STOP_BIT_ERROR		(1 << 4)
#define CY_I2C_BUS_BUSY_ERROR		(1 << 5)
#define CY_I2C_ENABLE_PRECISE_TIMING	1
#define CY_I2C_EVENT_NOTIFICATION_LEN	3

#define PD_I2C_SLAVE_ADDRESS		0x08

/* timeout (ms)	for  USB I2C communication */
#define FU_CCGX_HPI_WAIT_TIMEOUT	5000

/* max i2c frequency */
#define FU_CCGX_HPI_FREQ		400000

typedef enum {
	CY_GET_VERSION_CMD = 0xB0,	/* get the version of the boot-loader
					 * value = 0, index = 0, length = 4;
					 * data_in = 32 bit version */
	CY_GET_SIGNATURE_CMD = 0xBD,	/* get the signature of the firmware
					 * It is suppose to be 'CYUS' for normal firmware
					 * and 'CYBL' for Bootloader */
	CY_UART_GET_CONFIG_CMD = 0xC0,	/* retreive the 16 byte UART configuration information
					 *  MS bit of value indicates the SCB index
					 * length = 16, data_in = 16 byte configuration */
	CY_UART_SET_CONFIG_CMD,		/* update the 16 byte UART configuration information
					 * MS bit of value indicates the SCB index.
					 * length = 16, data_out = 16 byte configuration information */
	CY_SPI_GET_CONFIG_CMD,		/* retreive the 16 byte SPI configuration information
					 * MS bit of value indicates the SCB index
					 * length = 16, data_in = 16 byte configuration */
	CY_SPI_SET_CONFIG_CMD,		/* update the 16 byte SPI configuration	information
					 * MS bit of value indicates the SCB index
					 * length = 16, data_out = 16 byte configuration information */
	CY_I2C_GET_CONFIG_CMD,		/* retreive the 16 byte I2C configuration information
					 * MS bit of value indicates the SCB index
					 * length = 16, data_in = 16 byte configuration */
	CY_I2C_SET_CONFIG_CMD =	0xC5,	/* update the 16 byte I2C configuration information
					 * MS bit of value indicates the SCB index
					 * length = 16, data_out = 16 byte configuration information */
	CY_I2C_WRITE_CMD,		/* perform I2C write operation
					 * value = bit0 - start, bit1 - stop, bit3 - start on idle,
					 * bits[14:8] - slave address, bit15 - scbIndex. length = 0 the
					 * data	is provided over the bulk endpoints */
	CY_I2C_READ_CMD,		/* rerform I2C read operation.
					 * value = bit0 - start, bit1 - stop, bit2 - Nak last byte,
					 * bit3 - start on idle, bits[14:8] - slave address, bit15 - scbIndex,
					 * length = 0. The data is provided over the bulk endpoints */
	CY_I2C_GET_STATUS_CMD,		/* retreive the I2C bus status.
					 * value = bit0 - 0: TX 1: RX, bit15 - scbIndex, length = 3,
					 * data_in = byte0: bit0 - flag, bit1 -	bus_state, bit2 - SDA state,
					 * bit3 - TX underflow, bit4 - arbitration error, bit5 - NAK
					 * bit6 - bus error,
					 * byte[2:1] Data count remaining */
	CY_I2C_RESET_CMD,		/* the command cleans up the I2C state machine and frees the bus
					 * value = bit0 - 0: TX path, 1: RX path; bit15 - scbIndex,
					 * length = 0 */
	CY_SPI_READ_WRITE_CMD =	0xCA,	/* the command starts a read / write operation at SPI
					 * value = bit 0 - RX enable, bit 1 - TX enable, bit 15 - scbIndex;
					 * index = length of transfer */
	CY_SPI_RESET_CMD,		/* the command resets the SPI pipes and allows it to receive new
					 * request
					 * value = bit 15 - scbIndex */
	CY_SPI_GET_STATUS_CMD,		/* the command returns the current transfer status
					 * the count will match the TX pipe status at SPI end
					 * for completion of read, read all data
					 * at the USB end signifies the	end of transfer
					 * value = bit 15 - scbIndex */
	CY_JTAG_ENABLE_CMD = 0xD0,	/* enable JTAG module */
	CY_JTAG_DISABLE_CMD,		/* disable JTAG module */
	CY_JTAG_READ_CMD,		/* jtag read vendor command */
	CY_JTAG_WRITE_CMD,		/* jtag write vendor command */
	CY_GPIO_GET_CONFIG_CMD = 0xD8,	/* get the GPIO configuration */
	CY_GPIO_SET_CONFIG_CMD,		/* set the GPIO configuration */
	CY_GPIO_GET_VALUE_CMD,		/* get GPIO value */
	CY_GPIO_SET_VALUE_CMD,		/* set the GPIO value */
	CY_PROG_USER_FLASH_CMD = 0xE0,	/* program user flash area. The total space available is 512 bytes
					 * this can be accessed by the user from USB. The flash	area
					 * address offset is from 0x0000 to 0x00200 and can be written to
					 * page wise (128 byte) */
	CY_READ_USER_FLASH_CMD,		/* read user flash area. The total space available is 512 bytes
					 * this	can be accessed by the user from USB. The flash	area
					 * address offset is from 0x0000 to 0x00200 and can be written to
					 * page wise (128 byte) */
	CY_DEVICE_RESET_CMD = 0xE3,	/* performs a device reset from firmware */
} CyVendorCommand;

typedef struct __attribute__((packed)) {
	guint32	 frequency;		/* frequency of operation. Only valid values are 100KHz and 400KHz */
	guint8	 slave_address;		/* slave address to be used when in slave mode */
	guint8	 is_msb_first;		/* whether to transmit most significant bit first */
	guint8	 is_master;		/* whether to block is to be configured as a master*/
	guint8	 s_ignore;		/* ignore general call in slave mode */
	guint8	 is_clock_stretch;	/* whether to stretch clock in case of no FIFO availability */
	guint8	 is_loop_back;		/* whether to loop back	TX data to RX. Valid only for debug purposes */
	guint8	 reserved[6];
} CyI2CConfig;

