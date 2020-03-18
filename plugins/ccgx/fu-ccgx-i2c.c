/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "fu-ccgx-common.h"
#include "fu-ccgx-i2c.h"

#define I2C_READ_WRITE_DELAY_US	 10000 /* 10 msec */

#define CY_SCB_INDEX_POS		15
#define CY_I2C_CONFIG_LENGTH		16
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
	guint8	 is_clock_stretch;	/* wheteher to stretch clock in case of no FIFO	availability */
	guint8	 is_loop_back;		/* whether to loop back	TX data to RX. Valid only for debug purposes */
	guint8	reserved[6];		/* reserved for future use */
} CyUsI2cConfig_t;

static gboolean
i2c_reset (FuDevice *device, CyI2CDeviceHandle *handle, guint8 mode, GError **error)
{
	GUsbDevice *usb_device = NULL;
	guint32 io_timeout = FU_CCGX_I2C_WAIT_TIMEOUT;
	guint16 scb_index = 0;
	guint16 w_value, w_index, w_length;
	guint8 bm_request;
	gsize actual_length = 0;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (handle != NULL, FALSE);
	g_return_val_if_fail (FU_USB_DEVICE (device), FALSE);
	usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_return_val_if_fail (usb_device != NULL, FALSE);

	scb_index = handle->inf_num;
	if (scb_index > 0)
		scb_index = 1;

	bm_request = CY_I2C_RESET_CMD;
	w_value = ((scb_index << CY_SCB_INDEX_POS) | mode);
	w_index = 0;
	w_length = 0;

	if (!g_usb_device_control_transfer (usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   bm_request,
					   w_value,
					   w_index,
					   NULL,
					   w_length,
					   &actual_length,
					   io_timeout,
					   NULL,
					   &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "failed to reset i2c:%s", 
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

static gboolean
i2c_get_status (FuDevice *device, CyI2CDeviceHandle* handle, guint8 mode, guint8 *i2c_status, GError **error)
{
	g_autoptr(GError) error_local =	NULL;
	GUsbDevice *usb_device = NULL;
	guint32 io_timeout = FU_CCGX_I2C_WAIT_TIMEOUT;
	guint16 scb_index = 0;
	guint16 w_value, w_index, w_length;
	guint8 bm_request;
	gsize actual_length = 0;

	g_return_val_if_fail (handle != NULL, FALSE);
	g_return_val_if_fail (FU_USB_DEVICE (device), FALSE);
	usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_return_val_if_fail (usb_device != NULL, FALSE);

	scb_index = handle->inf_num;

	if (scb_index > 0)
		scb_index = 1;

	bm_request = CY_I2C_GET_STATUS_CMD;
	w_value = ((scb_index << CY_SCB_INDEX_POS) | mode);
	w_index = 0;
	w_length = CY_I2C_GET_STATUS_LEN;
	if (!g_usb_device_control_transfer (usb_device,
					   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   bm_request,
					   w_value,
					   w_index,
					   (guint8*)&i2c_status,
					   w_length,
					   &actual_length,
					   io_timeout,
					   NULL,
					   &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "failed to get i2c status:%s", 
			     error_local->message);

		return FALSE;
	}
	return TRUE;

}

static gboolean
wait_for_notification (FuDevice *device, CyI2CDeviceHandle* handle, guint16 *bytes_pending, guint32 io_timeout, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	GUsbDevice *usb_device = NULL;
	gsize actual_length = 0;
	guint8 ep_num;
	guint8 i2c_status[CY_I2C_EVENT_NOTIFICATION_LEN] = {0};

	g_return_val_if_fail (handle != NULL, FALSE);
	g_return_val_if_fail (FU_USB_DEVICE (device), FALSE);
	usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_return_val_if_fail (usb_device != NULL, FALSE);

	ep_num = handle->ep.intr_in;

	if (g_usb_device_interrupt_transfer (usb_device,
					     ep_num,
					     (guint8*)i2c_status,
					     CY_I2C_EVENT_NOTIFICATION_LEN,
					     (gsize*)&actual_length,
					     io_timeout,
					     NULL,
					     &error_local)) {

		if (i2c_status[0] & CY_I2C_ERROR_BIT) {
			if (i2c_status[0] & 0x80) { /* write */
				if (i2c_reset (device,handle,CY_I2C_MODE_WRITE,NULL)  == FALSE) {
					g_warning ("failed to reset i2c for write while getting i2c event");
				}
				memcpy(bytes_pending, &i2c_status[1], 2);

			} else { /* read */
				if (i2c_reset (device,handle,CY_I2C_MODE_READ,NULL) == FALSE) {
					g_warning ("failed to reset i2c for read while getting i2c event");
				}
				memcpy(bytes_pending, &i2c_status[1], 2);
			}
			g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "i2c status error in i2c event : 0x%x", 
			     (guint8) i2c_status[0]);

			return FALSE;
		}
	} else {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "failed to get i2c event:%s", 
			     error_local->message);
		if (!g_usb_device_reset (usb_device, NULL)) {
			g_warning ("failed to reset i2c while getting i2c event");
		}
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_ccgx_i2c_read:
 * @device: #FuDevice
 * @handle: I2C handle
 * @data_cfg  I2C data configuration
 * @data_buffer  I2C data buffer
 * @error: a #GError or %NULL
 *
 * Read	data through I2C
 *
 * Returns: %TRUE for success
*/
gboolean
fu_ccgx_i2c_read (FuDevice *device, CyI2CDeviceHandle *handle, CyI2CDataConfig *data_cfg, CyDataBuffer *data_buffer, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	guint16 scb_index = 0;
	guint32 io_timeout = FU_CCGX_I2C_WAIT_TIMEOUT;
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	guint16 w_value, w_index, w_length;
	guint8 bm_request;
	gsize actual_length = 0;
	guint8 ep_num;
	guint8 i2c_status[CY_I2C_EVENT_NOTIFICATION_LEN] = {0};
	guint8 mode = CY_I2C_MODE_READ;
	guint8 slave_address = 0;
	guint16 byte_pending = 0;
	guint64 elapsed_time = 0;
	g_autoptr(GTimer) start_time = g_timer_new ();

	if (!i2c_get_status (device, handle, mode, (guint8*)i2c_status, error)) {
		g_prefix_error (error, "i2c read error: ");
		return FALSE;
	}

	scb_index = handle->inf_num;
	if (scb_index > 0)
		scb_index = 1;

	slave_address = ((handle->slave_address & 0x7F) | (scb_index << 7));
	bm_request = CY_I2C_READ_CMD;
	w_value = ((data_cfg->is_stop_bit) | (data_cfg->is_nak_bit << 1));
	w_value |= (((slave_address) << 8));
	w_index = data_buffer->length;
	w_length = 0;

	if (!g_usb_device_control_transfer (usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   bm_request,
					   w_value,
					   w_index,
					   NULL,
					   w_length,
					   &actual_length,
					   io_timeout,
					   NULL,
					   &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "i2c read error: control xfer: %s", 
			     error_local->message);
		return FALSE;
	}

	ep_num = handle->ep.bulk_in;

	if (!g_usb_device_bulk_transfer (usb_device,
					ep_num,
					data_buffer->buffer,
					data_buffer->length,
					(gsize*)&data_buffer->transfer_count,
					io_timeout,
					NULL,
					&error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "i2c read error: bulk xfer: %s", 
			     error_local->message);

		if (g_error_matches (error_local,
				     G_USB_DEVICE_ERROR,
				     G_USB_DEVICE_ERROR_TIMED_OUT)) {
			g_autoptr(GError) error_reset1 = NULL;
			if (!i2c_reset (device, handle, mode, &error_reset1)) {
				g_warning ("i2c reset error in %s: ", error_reset1->message);
				return FALSE;
			}
		} else if (g_error_matches (error_local,
					    G_USB_DEVICE_ERROR,
					    G_USB_DEVICE_ERROR_IO)) {
			g_autoptr(GError) error_reset1 = NULL;
			g_autoptr(GError) error_reset2 = NULL;
			if (!g_usb_device_reset (usb_device, &error_reset1)) {
				g_warning ("usb dev error: %s", error_reset1->message);
			}

			/* 10 msec delay */
			g_usleep (I2C_READ_WRITE_DELAY_US);

			if (!i2c_reset (device, handle, mode, &error_reset2)) {
				g_warning ("i2c reset error in %s", error_reset2->message);
			}
		}
		return FALSE;
	}

	/* 10 msec delay */
	g_usleep (I2C_READ_WRITE_DELAY_US);

	elapsed_time = g_timer_elapsed (start_time, NULL) * 1000.f;

	/* giving an extra 10 msec to notification to findout the status */
	if (io_timeout > elapsed_time)
		io_timeout = io_timeout - elapsed_time;
	if (io_timeout < 10)
		io_timeout = 10;

	byte_pending = data_buffer->length;

	if (!wait_for_notification (device, handle, &byte_pending, io_timeout, error)) {
		data_buffer->transfer_count = data_buffer->length;
		g_prefix_error (error, "i2c read error: ");
		return FALSE;
	}

	data_buffer->transfer_count = (data_buffer->length - byte_pending);
	return TRUE;
}

/**
 * fu_ccgx_i2c_write:
 * @device: #FuDevice
 * @handle: I2C handle
 * @data_cfg  I2C data configuration
 * @data_buffer  I2C data buffer
 * @error: a #GError or %NULL
 *
 * Read	data through I2C
 *
 * Returns: %TRUE for success
*/
gboolean
fu_ccgx_i2c_write (FuDevice *device, CyI2CDeviceHandle *handle, CyI2CDataConfig *data_cfg, CyDataBuffer *data_buffer, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	guint16 scb_index = 0;
	guint32 io_timeout = FU_CCGX_I2C_WAIT_TIMEOUT;
	guint16 w_value, w_index, w_length;
	guint8 bm_request;
	gsize actual_length = 0;
	guint8 ep_num;
	guint8 i2c_status[CY_I2C_EVENT_NOTIFICATION_LEN] = {0};
	guint8 mode = CY_I2C_MODE_WRITE;
	guint8 slave_address = 0;
	guint16 byte_pending = 0;
	guint64 elapsed_time = 0;
	g_autoptr(GTimer) start_time = g_timer_new ();
	g_autoptr(GError) error_local = NULL;

	if (!i2c_get_status (device, handle, mode, (guint8*)i2c_status, error)) {
		g_prefix_error (error, "i2c get status error: ");
		return FALSE;
	}

	scb_index = handle->inf_num;
	if (scb_index > 0)
		scb_index = 1;

	slave_address = ((handle->slave_address & 0x7F) | (scb_index << 7));
	bm_request = CY_I2C_WRITE_CMD;
	w_value = data_cfg->is_stop_bit;
	w_value |= (((slave_address) << 8));
	w_index = data_buffer->length;
	w_length = 0;

	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    bm_request,
					    w_value,
					    w_index,
					    NULL,
					    w_length,
					    &actual_length,
					    io_timeout,
					    NULL,
					    &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "i2c write error: control xfer: %s", 
			     error_local->message);
		return FALSE;
	}

	ep_num = handle->ep.bulk_out;

	if  (g_usb_device_bulk_transfer(usb_device,
					ep_num,
					data_buffer->buffer,
					data_buffer->length,
					(gsize*)&data_buffer->transfer_count,
					io_timeout,
					NULL,
					error)) {
		g_usleep (I2C_READ_WRITE_DELAY_US); /* 10 msec delay */

		elapsed_time = g_timer_elapsed (start_time, NULL) * 1000.f;

		/* giving an extra 10 msec to notification to findout the status */
		if (io_timeout > elapsed_time) {
			io_timeout = io_timeout - elapsed_time;
		}

		if (io_timeout < 10)
			io_timeout = 10;

		byte_pending = data_buffer->length;

		if (wait_for_notification (device, handle, &byte_pending, io_timeout, error)) {
			data_buffer->transfer_count = (data_buffer->length - byte_pending);
			return TRUE;
		} else {
			data_buffer->transfer_count = data_buffer->length;
			g_prefix_error (error, "i2c wait for notification error: ");
		}

	} else {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "i2c read error: bulk xfer: %s", 
			     error_local->message);
		if (g_error_matches (error_local,
				     G_USB_DEVICE_ERROR,
				     G_USB_DEVICE_ERROR_TIMED_OUT)){
			if (!i2c_reset (device, handle, mode, NULL)) {
				g_warning ("i2c reset error");
			}
		}

		if (g_error_matches (error_local,
				     G_USB_DEVICE_ERROR,
				     G_USB_DEVICE_ERROR_IO)) {

			if (!g_usb_device_reset(usb_device, NULL)) {
				g_warning ("usb dev error");
			}

			g_usleep (I2C_READ_WRITE_DELAY_US); /* 10 msec delay */

			if (!i2c_reset (device, handle,	mode, NULL)) {
				g_warning ("i2c	reset error");
			}
		}
	}
	return FALSE;
}

/**
 * fu_ccgx_i2c_write_no_resp:
 * @device: #FuDevice
 * @handle: I2C handle
 * @data_cfg  I2C data configuration
 * @data_buffer  I2C data buffer
 * @error: a #GError or %NULL
 *
 *  Write data through I2C without interrupt response
 *  It is used for reset HPI command
 *
 * Returns: %TRUE for success
*/
gboolean
fu_ccgx_i2c_write_no_resp (FuDevice *device,CyI2CDeviceHandle* handle, CyI2CDataConfig* data_cfg, CyDataBuffer* data_buffer, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	guint16 scb_index = 0;
	guint32 io_timeout = FU_CCGX_I2C_WAIT_TIMEOUT;
	guint16 w_value, w_index, w_length;
	guint8 bm_request;
	gsize actual_length = 0;
	guint8 ep_num;
	guint8 i2c_status[CY_I2C_EVENT_NOTIFICATION_LEN] = {0};
	guint8 mode = CY_I2C_MODE_WRITE;
	guint8 slave_address = 0;
	g_autoptr(GTimer) start_time = g_timer_new ();

	if (!i2c_get_status (device, handle, mode, (guint8*)i2c_status, error)) {
		g_prefix_error (error, "i2c write error: ");
		return FALSE;
	}

	scb_index = handle->inf_num;
	if (scb_index > 0)
		scb_index = 1;

	slave_address = ((handle->slave_address & 0x7F) | (scb_index <<	7));
	bm_request = CY_I2C_WRITE_CMD;
	w_value = data_cfg->is_stop_bit;
	w_value |= (((slave_address) << 8));
	w_index = data_buffer->length;
	w_length = 0;

	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    bm_request,
					    w_value,
					    w_index,
					    NULL,
					    w_length,
					    &actual_length,
					    io_timeout,
					    NULL,
					    &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "i2c write	error: control xfer: %s", 
			     error_local->message);
		return FALSE;
	}

	ep_num = handle->ep.bulk_out;

	if  (g_usb_device_bulk_transfer(usb_device,
					ep_num,
					data_buffer->buffer,
					data_buffer->length,
					(gsize*)&data_buffer->transfer_count,
					io_timeout,
					NULL,
					error)) {
		return TRUE;
	} else {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "i2c read error: bulk xfer: %s", 
			     error_local->message);

		if (g_error_matches (error_local,
				     G_USB_DEVICE_ERROR,
				     G_USB_DEVICE_ERROR_TIMED_OUT)) {
			if (i2c_reset (device, handle,	mode, NULL) == FALSE) {
				g_warning ("i2c reset error");
			}
		} else if (g_error_matches (error_local,
					    G_USB_DEVICE_ERROR,
					    G_USB_DEVICE_ERROR_IO)) {

			if (g_usb_device_reset(usb_device, NULL) == FALSE) {
				g_warning ("usb dev error");
			}

			g_usleep (I2C_READ_WRITE_DELAY_US); /* 10 msec delay */

			if (i2c_reset (device, handle, mode, NULL) == FALSE) {
				g_warning ("i2c reset error");
			}
		}
	}

	return FALSE;
}

/**
 * fu_ccgx_i2c_get_config:
 * @device: #FuDevice
 * @handle: I2C handle
 * @i2c_config: (out): I2C config buffer
 * @error: a #GError or %NULL
 *
 * Get I2C config data from device
 *
 * Returns: %TRUE for success
*/
gboolean
fu_ccgx_i2c_get_config (FuDevice *device,
			CyI2CDeviceHandle *handle,
			CyI2CConfig *i2c_config,
			GError **error)
{
	CyUsI2cConfig_t local_i2c_config = { 0x0 };
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	guint32 io_timeout = FU_CCGX_I2C_WAIT_TIMEOUT;
	guint16 scb_index = 0;
	guint16 w_value, w_index, w_length;
	guint8 bm_request;
	gsize actual_length = 0;
	g_autoptr(GError) error_local = NULL;

	scb_index = handle->inf_num;
	if (scb_index > 0)
		scb_index = 1;

	bm_request = CY_I2C_GET_CONFIG_CMD;
	w_value = (scb_index << CY_SCB_INDEX_POS);
	w_index = 0;
	w_length = CY_I2C_CONFIG_LENGTH;

	if (!g_usb_device_control_transfer (usb_device,
					   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   bm_request,
					   w_value,
					   w_index,
					   (guint8*)&local_i2c_config,
					   w_length,
					   &actual_length,
					   io_timeout,
					   NULL,
					   &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "i2c get config error: control xfer: %s", 
			     error_local->message);
		return FALSE;
	}

	i2c_config->frequency = local_i2c_config.frequency;
	i2c_config->slave_address = local_i2c_config.slave_address;
	i2c_config->is_master = local_i2c_config.is_master;
	i2c_config->is_clock_stretch = local_i2c_config.is_clock_stretch;
	return TRUE;
}

/**
 * fu_ccgx_i2c_set_config:
 * @device: #FuDevice
 * @handle: I2C handle
 * @i2c_config: (in): I2C config buffer
 * @error: a #GError or %NULL
 *
 * Set I2C config to device
 *
 * Returns: %TRUE for success
*/
gboolean
fu_ccgx_i2c_set_config (FuDevice *device,
			CyI2CDeviceHandle *handle,
			CyI2CConfig *i2c_config,
			GError **error)
{
	CyUsI2cConfig_t local_i2c_config = { 0x0 };
	GUsbDevice *usb_device = NULL;
	guint32 io_timeout = FU_CCGX_I2C_WAIT_TIMEOUT;
	guint16 scb_index = 0;
	guint16 w_value, w_index, w_length;
	guint8 bm_request;
	gsize actual_length = 0;
	g_autoptr(GError) error_local = NULL;

	usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));

	scb_index = handle->inf_num;
	if (scb_index > 0)
		scb_index = 1;

	bm_request = CY_I2C_SET_CONFIG_CMD;
	w_value = (scb_index << CY_SCB_INDEX_POS);
	w_index = 0;
	w_length = CY_I2C_CONFIG_LENGTH;

	memset (&local_i2c_config, 0, CY_I2C_CONFIG_LENGTH);
	local_i2c_config.frequency = i2c_config->frequency;
	local_i2c_config.slave_address = i2c_config->slave_address;
	local_i2c_config.is_master = i2c_config->is_master;
	local_i2c_config.is_clock_stretch = i2c_config->is_clock_stretch;
	local_i2c_config.is_msb_first = 1;

	if (!g_usb_device_control_transfer (usb_device,
					     G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
						 G_USB_DEVICE_REQUEST_TYPE_VENDOR,
						 G_USB_DEVICE_RECIPIENT_DEVICE,
						 bm_request,
						 w_value,
						 w_index,
						 (guint8*)&local_i2c_config,
						 w_length,
						 &actual_length,
						 io_timeout,
						 NULL,
						 &error_local))	{
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "i2c set config error: control xfer: %s", 
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}
