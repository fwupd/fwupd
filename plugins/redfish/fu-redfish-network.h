 /*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

gchar		*fu_redfish_network_ip_for_mac_addr	(const gchar	*mac_addr,
							 GError		**error);
gchar		*fu_redfish_network_ip_for_vid_pid	(guint16	 vid,
							 guint16	 pid,
							 GError		**error);
