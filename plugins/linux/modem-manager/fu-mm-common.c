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
	if (port_type == MM_MODEM_PORT_TYPE_IGNORED)
		return "ignored";
	return NULL;
}

MMModemPortType
fu_mm_device_port_type_from_string(const gchar *port_type)
{
	if (g_strcmp0(port_type, "net") == 0)
		return MM_MODEM_PORT_TYPE_NET;
	if (g_strcmp0(port_type, "at") == 0)
		return MM_MODEM_PORT_TYPE_AT;
	if (g_strcmp0(port_type, "qcdm") == 0)
		return MM_MODEM_PORT_TYPE_QCDM;
	if (g_strcmp0(port_type, "gps") == 0)
		return MM_MODEM_PORT_TYPE_GPS;
	if (g_strcmp0(port_type, "qmi") == 0)
		return MM_MODEM_PORT_TYPE_QMI;
	if (g_strcmp0(port_type, "mbim") == 0)
		return MM_MODEM_PORT_TYPE_MBIM;
	if (g_strcmp0(port_type, "ignored") == 0)
		return MM_MODEM_PORT_TYPE_IGNORED;
	return MM_MODEM_PORT_TYPE_UNKNOWN;
}
