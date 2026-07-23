/*
 * Copyright 2026 Mario Limonciello <superm1@kernel.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <gio/gio.h>

#include "fwupd-error.h"
#include "fwupd-network.h"

/**
 * fwupd_network_is_reachable:
 * @hostname: the hostname to check reachability for
 * @port: the port number
 * @error: (nullable): optional return location for an error
 *
 * Fallback implementation for non-Linux platforms.
 * Uses GNetworkMonitor which is fine on these platforms (the performance
 * issue with netlink route table enumeration is Linux-specific).
 *
 * Returns: %TRUE if the network is reachable
 *
 * Since: 2.1.6
 **/
gboolean
fwupd_network_is_reachable(const gchar *hostname, gint port, GError **error) /* nocheck:name */
{
	GNetworkMonitor *monitor;
	g_autoptr(GSocketConnectable) address = NULL;

	/* check connectivity to the target host */
	address = g_network_address_new(hostname, port);
	monitor = g_network_monitor_get_default();
	if (!g_network_monitor_can_reach(monitor, address, NULL, error))
		return FALSE;

	return TRUE;
}
