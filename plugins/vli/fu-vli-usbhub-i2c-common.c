/*
 * Copyright (C) 2017-2019 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-vli-usbhub-i2c-common.h"

gboolean
fu_vli_usbhub_i2c_check_status (FuVliUsbhubI2cStatus status, GError **error)
{
	if (status == FU_VLI_USBHUB_I2C_STATUS_OK)
		return TRUE;
	if (status == FU_VLI_USBHUB_I2C_STATUS_HEADER) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Incorrect header value of data frame");
		return FALSE;
	}
	if (status == FU_VLI_USBHUB_I2C_STATUS_COMMAND) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid command data");
		return FALSE;
	}
	if (status == FU_VLI_USBHUB_I2C_STATUS_ADDRESS) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid address range");
		return FALSE;
	}
	if (status == FU_VLI_USBHUB_I2C_STATUS_PACKETSIZE) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Incorrect payload data length");
		return FALSE;
	}
	if (status == FU_VLI_USBHUB_I2C_STATUS_CHECKSUM) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Incorrect frame data checksum");
		return FALSE;
	}
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_INTERNAL,
		     "Unknown error [0x%02x]", status);
	return FALSE;
}
