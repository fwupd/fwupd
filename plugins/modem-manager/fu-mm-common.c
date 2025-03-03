/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mm-common.h"

const gchar *
fu_mm_device_port_type_to_string(MMModemPortType port_type)
{
	if (port_type == MM_MODEM_PORT_TYPE_NET)
		return "net";
	if (port_type == MM_MODEM_PORT_TYPE_AT)
		return "at";
	if (port_type == MM_MODEM_PORT_TYPE_QCDM)
		return "qcdm";
	if (port_type == MM_MODEM_PORT_TYPE_GPS)
		return "gps";
	if (port_type == MM_MODEM_PORT_TYPE_QMI)
		return "qmi";
	if (port_type == MM_MODEM_PORT_TYPE_MBIM)
		return "mbim";
	return NULL;
}
