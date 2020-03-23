/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-usb-device.h"

#include "fu-ccgx-common.h"
#include "fu-ccgx-hid.h"

#define CCGX_HID_TIMEOUT_MS	5000 /* ms */

static gboolean
fu_ccgx_hid_set_report (FuDevice *self,
			gint inf_num,
			guint8 *data,
			gsize data_size,
			GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	gsize actual_len = 0;
	guint16 value = 0;
	guint8 report_number;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (data != NULL, FALSE);

	report_number = data[0];
	if (report_number == 0x0) {
		data++;
		data_size--;
	}
	value = (2 /* hid output */ << 8) | report_number;

	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    FU_HID_REPORT_SET, /* request*/
					    value,   /* value */
					    inf_num, /* idx */
					    data,
					    data_size,
					    &actual_len,
					    CCGX_HID_TIMEOUT_MS, NULL, &error_local)) {
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
fu_ccgx_hid_handle_rqt_cmd (FuDevice *self,
			    gint inf_num,
			    guint8 cmd,
			    guint8 param_0,
			    guint8 param_1,
			    GError **error)
{
	guint8 data[HID_RQT_CMD_SIZE] = { HID_REPORT_ID_RQT, cmd, param_0, param_1 };
	return fu_ccgx_hid_set_report (self, inf_num, data, HID_RQT_CMD_SIZE, error);
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
	if (!fu_ccgx_hid_set_report (self, inf_num, data, sizeof(data), error)) {
		g_prefix_error (error, "mfg mode error: ");
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_ccgx_hid_enable_usb_bridge_mode:
 * @self: #FuDevice
 * @inf_num: USB Interface number
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
	if (!fu_ccgx_hid_handle_rqt_cmd (self,
					 inf_num,
					 HID_RQT_CMD_I2C_BRIDGE_CTRL,
					 'B', 0,
					 error)) {
		g_prefix_error (error, "usb bridge mode error: ");
		return FALSE;
	}
	return TRUE;
}
