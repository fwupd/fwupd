/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "fu-ccgx-common.h"
#include "fu-ccgx-hid.h"

#define CCGX_HID_TIMEOUT  5000  /* 5000 msec */

static gboolean
hid_set_report (FuDevice *self, gint inf_num , guint8 *data, gsize data_size, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	GUsbDevice *usb_device = NULL;
	guint16 value = 0;
	gsize actual_len = 0;
	guint8 *buffer;
	gsize buffer_size;
	int report_number;

	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (FU_USB_DEVICE (self), FALSE);

	usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));

	g_return_val_if_fail (data != NULL, FALSE);

	buffer = data;
	buffer_size = data_size;

	report_number = buffer[0];

	if (report_number == 0x0) {
		buffer++;
		buffer_size--;
	}

	value = (2 /* hid output */ << 8) | report_number;

	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    FU_HID_REPORT_SET, /* request*/
					    value,   /* value */
					    inf_num, /* idx */
					    buffer,
					    buffer_size,
					    &actual_len,
					    CCGX_HID_TIMEOUT, NULL, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "USB HID write error: control xfer: %s", 
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

static gboolean
hid_handle_rqt_cmd(FuDevice *self, gint inf_num, guint8 cmd, guint8 param_0, guint8 param_1, GError **error)
{
	guint8 data[HID_RQT_CMD_SIZE] = {0};

	data[0] = HID_REPORT_ID_RQT;
	data[1] = cmd;
	data[2] = param_0;
	data[3] = param_1;
	return hid_set_report(self, inf_num, data, HID_RQT_CMD_SIZE, error);
}

/**
 * fu_ccgx_hid_enable_mfg_mode:
 * @self: #FuDevice
 * @inf_num: USB Interface number
 * @error: a #GError or %NULL
 *
 * Change Billboard device to USB serial device
 * It is command for external Billboard device
 *
 * Returns: %TRUE for success
*/
gboolean
fu_ccgx_hid_enable_mfg_mode (FuDevice *self, gint inf_num, GError **error)
{
	guint8 data[5] = {0xEE, 0xBC, 0xA6, 0xB9, 0xA8};
	if (!hid_set_report(self, inf_num, data, sizeof(data), error)) {
		g_prefix_error (error, "mfg mode error: ");
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_ccgx_hid_enable_usb_bridge_mode:
 * @self #FuDevice
 * @inf_num USB Interface number
 * @error: a #GError or %NULL
 *
 *  Change Billboard device to USB bridge mode device
 *  It is command for internal Billboard device
 *
 * Returns: %TRUE for success
*/
gboolean
fu_ccgx_hid_enable_usb_bridge_mode (FuDevice *self, gint inf_num, GError **error)
{
	if (!hid_handle_rqt_cmd(self,inf_num,HID_RQT_CMD_I2C_BRIDGE_CTRL,'B',0,error)) {
		g_prefix_error (error, "usb bridge mode error: ");
		return FALSE;
	}
	return TRUE;
}
