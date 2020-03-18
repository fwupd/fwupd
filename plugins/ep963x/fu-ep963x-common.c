/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ep963x-common.h"

const gchar *
fu_ep963x_smbus_strerror (guint8 val)
{
	if (val == FU_EP963_SMBUS_ERROR_NONE)
		return "none";
	if (val == FU_EP963_SMBUS_ERROR_ADDRESS)
		return "address";
	if (val == FU_EP963_SMBUS_ERROR_NO_ACK)
		return "no-ack";
	if (val == FU_EP963_SMBUS_ERROR_ARBITRATION)
		return "arbitration";
	if (val == FU_EP963_SMBUS_ERROR_COMMAND)
		return "command";
	if (val == FU_EP963_SMBUS_ERROR_TIMEOUT)
		return "timeout";
	if (val == FU_EP963_SMBUS_ERROR_BUSY)
		return "busy";
	return "unknown";
}
