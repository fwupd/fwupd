/*
 * Copyright 2026 Mario Limonciello <superm1@kernel.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fcntl.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include "fwupd-error.h"
#include "fwupd-network.h"

/*
 * Defined locally rather than including linux/route.h, which pulls in
 * linux/if.h and redefines IFF_* in conflict with net/if.h.
 */
#ifndef RTF_UP
#define RTF_UP 0x0001 /* route usable */
#endif

/*
 * fwupd_network_ensure_interface_up:
 * @iface: the interface name to check
 * @error: (nullable): optional return location for an error
 *
 * Use ioctl(SIOCGIFFLAGS) to verify the given interface is up
 * and running.
 *
 * Returns: %TRUE on success, %FALSE if the interface is down or on error
 */
static gboolean
fwupd_network_ensure_interface_up(const gchar *iface, GError **error)
{
	g_autofd gint fd = -1;
	struct ifreq ifr = {};

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_REACHABLE,
			    "failed to create socket for interface %s",
			    iface);
		return FALSE;
	}

	strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
	ifr.ifr_name[IFNAMSIZ - 1] = '\0';

	/* nocheck:blocked */
	if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_REACHABLE,
			    "failed to get flags for interface %s",
			    iface);
		return FALSE;
	}

	if ((ifr.ifr_flags & IFF_UP) == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_REACHABLE,
			    "interface %s is not up",
			    iface);
		return FALSE;
	}
	if ((ifr.ifr_flags & IFF_RUNNING) == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_REACHABLE,
			    "interface %s is not running",
			    iface);
		return FALSE;
	}

	return TRUE;
}

/*
 * fwupd_network_has_default_route_with_iface:
 * Check for a default route in /proc/net/route and verify
 * its interface is up via ioctl.
 *
 * Returns: %TRUE if a valid default route exists and its
 *          interface is IFF_UP and IFF_RUNNING.
 */
static gboolean
fwupd_network_has_default_route_with_iface(GError **error)
{
	g_autofree gchar *route_data = NULL;
	g_auto(GStrv) lines = NULL;

	if (!g_file_get_contents("/proc/net/route", &route_data, NULL, error))
		return FALSE;

	/* the first line is a header, so skip it */
	lines = g_strsplit(route_data, "\n", -1);
	for (guint i = 1; lines[i] != NULL; i++) {
		guint64 dst = 0;
		guint64 flags = 0;
		g_auto(GStrv) fields = NULL;
		g_auto(GStrv) fields_filtered = NULL;
		guint field_count = 0;
		g_autoptr(GError) error_local = NULL;

		/*
		 * fields (tab/space separated) are:
		 * Iface Destination Gateway Flags RefCnt Use Metric Mask ...
		 */
		fields = g_strsplit_set(lines[i], "\t ", -1);

		/* filter out empty strings from consecutive delimiters */
		for (guint j = 0; fields[j] != NULL; j++) {
			if (fields[j][0] != '\0')
				field_count++;
		}
		if (field_count < 4)
			continue;

		fields_filtered = g_new0(gchar *, field_count + 1);
		for (guint j = 0, k = 0; fields[j] != NULL; j++) {
			if (fields[j][0] != '\0')
				fields_filtered[k++] = g_strdup(fields[j]);
		}

		/* a default route has a destination of 0.0.0.0 */
		dst = g_ascii_strtoull(fields_filtered[1], NULL, 16); /* nocheck:blocked */
		if (dst != 0x0)
			continue;

		/* the route has to be usable */
		flags = g_ascii_strtoull(fields_filtered[3], NULL, 16); /* nocheck:blocked */
		if ((flags & RTF_UP) == 0)
			continue;

		if (!fwupd_network_ensure_interface_up(fields_filtered[0], &error_local)) {
			g_debug("%s", error_local->message);
			continue;
		}

		/* found a working route with a valid interface */
		return TRUE;
	}

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_REACHABLE,
			    "network is unreachable");
	return FALSE;
}

/**
 * fwupd_network_is_reachable:
 * @hostname: the hostname to check reachability for
 * @port: the port number
 * @error: (nullable): optional return location for an error
 *
 * Check if the network is reachable using a multi-layered approach
 * that avoids spawning subprocesses or using GNetworkMonitor.
 *
 * The check works as follows:
 *
 * 1. Parse /proc/net/route for a default route (RTF_UP)
 * 2. Use ioctl(SIOCGIFFLAGS) to verify the interface is IFF_UP | IFF_RUNNING
 * 3. Resolve the hostname to verify DNS works
 * 4. connect() a UDP socket to verify routing (does not generate traffic)
 *
 * This avoids the GNetworkMonitor path which on Linux uses
 * NetworkManager and pulls the entire kernel route table into memory,
 * causing CPU/RAM exhaustion on systems with very large route tables
 * (see #10525).
 *
 * Returns: %TRUE if the network is reachable
 *
 * Since: 2.1.6
 **/
gboolean
fwupd_network_is_reachable(const gchar *hostname, gint port, GError **error)
{
	g_autofd gint fd = -1;
	g_autoptr(GResolver) resolver = NULL;
	g_autolist(GInetAddress) addresses = NULL;
	g_autoptr(GInetAddress) inet_addr = NULL;

	if (!fwupd_network_has_default_route_with_iface(error))
		return FALSE;

	/* verify DNS resolution works for the target host */
	resolver = g_resolver_get_default();
	addresses = g_resolver_lookup_by_name(resolver, hostname, NULL, error);
	if (addresses == NULL)
		return FALSE;

	/* prefer IPv4, fall back to IPv6 */
	for (GList *l = addresses; l != NULL; l = l->next) {
		GInetAddress *addr_tmp = G_INET_ADDRESS(l->data);
		if (g_inet_address_get_family(addr_tmp) == G_SOCKET_FAMILY_IPV4) {
			inet_addr = g_object_ref(addr_tmp);
			break;
		}
	}
	/* if no IPv4, use first IPv6 */
	if (inet_addr == NULL)
		inet_addr = g_object_ref(G_INET_ADDRESS(addresses->data));

	/* verify routing to the resolved address works */
	if (g_inet_address_get_family(inet_addr) == G_SOCKET_FAMILY_IPV4) {
		struct sockaddr_in addr = {};

		fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
		if (fd < 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_REACHABLE,
					    "network is unreachable");
			return FALSE;
		}

		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		/* nocheck:blocked - safe since we just verified it's IPv4 */
		memcpy(&addr.sin_addr, g_inet_address_to_bytes(inet_addr), sizeof(addr.sin_addr));
		if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_REACHABLE,
					    "network is unreachable");
			return FALSE;
		}
	} else {
		struct sockaddr_in6 addr6 = {};

		fd = socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
		if (fd < 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_REACHABLE,
					    "network is unreachable");
			return FALSE;
		}

		addr6.sin6_family = AF_INET6;
		addr6.sin6_port = htons(port);
		/* nocheck:blocked - safe since we just verified it's IPv6 */
		memcpy(&addr6.sin6_addr,
		       g_inet_address_to_bytes(inet_addr),
		       sizeof(addr6.sin6_addr));
		if (connect(fd, (struct sockaddr *)&addr6, sizeof(addr6)) < 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_REACHABLE,
					    "network is unreachable");
			return FALSE;
		}
	}

	return TRUE;
}
