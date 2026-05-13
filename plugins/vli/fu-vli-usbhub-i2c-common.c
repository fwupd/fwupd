/*
 * Copyright 2017 VIA Corporation
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-vli-usbhub-i2c-common.h"

gboolean
fu_vli_usbhub_i2c_check_status(FuVliUsbhubI2cStatus status, GError **error)
{
	const FuErrorMapEntry entries[] = {
	    {FU_VLI_USBHUB_I2C_STATUS_OK, FWUPD_ERROR_LAST, NULL},
	    {FU_VLI_USBHUB_I2C_STATUS_HEADER, FWUPD_ERROR_INTERNAL, "incorrect header value"},
	    {FU_VLI_USBHUB_I2C_STATUS_COMMAND, FWUPD_ERROR_INTERNAL, "invalid command data"},
	    {FU_VLI_USBHUB_I2C_STATUS_ADDRESS, FWUPD_ERROR_INTERNAL, "invalid address range"},
	    {FU_VLI_USBHUB_I2C_STATUS_PACKETSIZE, FWUPD_ERROR_INTERNAL, "invalid payload length"},
	    {FU_VLI_USBHUB_I2C_STATUS_CHECKSUM, FWUPD_ERROR_INTERNAL, "invalid frame checksum"},
	};
	return fu_error_map_entry_to_gerror(status, entries, G_N_ELEMENTS(entries), error);
}
