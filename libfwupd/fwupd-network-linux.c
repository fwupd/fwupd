/*
 * Copyright 2026 Sorah Fukumori <her@sorah.jp>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <string.h>
#include <sys/socket.h>

#include "fwupd-error.h"
#include "fwupd-network.h"

/*
 * Cap on the synchronous DNS lookup, which can be tens of seconds when
 * DNS servers are unreachable.
 */
#define FWUPD_NETWORK_DNS_TIMEOUT_MS 2000

/*
 * fwupd_network_addr_is_routable:
 * @addr: destination address
 * @error: (nullable): optional return location for an error
 *
 * Ask the kernel whether it has a usable route to a single destination using
 * RTM_GETROUTE with RTM_F_FIB_MATCH. Cost is independent of the kernel FIB
 * size (#10525).
 *
 * Returns: %TRUE if a usable route exists.
 */
static gboolean
fwupd_network_addr_is_routable(GInetAddress *addr, GError **error)
{
	g_autofd gint fd = -1;
	gssize len;
	gsize addr_len = g_inet_address_get_native_size(addr);
	gboolean is_ipv6 = g_inet_address_get_family(addr) == G_SOCKET_FAMILY_IPV6;
	struct sockaddr_nl sa = {.nl_family = AF_NETLINK};
	const struct nlmsghdr *nlh;
	const struct rtmsg *rtm;
	struct {
		struct nlmsghdr nlh;
		struct rtmsg rtm;
		struct rtattr rta;
		guint8 addr[16];
	} req = {0};
	guint8 buf[8192] = {0};

	req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(req.rtm)) + RTA_LENGTH(addr_len);
	req.nlh.nlmsg_type = RTM_GETROUTE;
	req.nlh.nlmsg_flags = NLM_F_REQUEST;
	req.nlh.nlmsg_seq = 1;
	req.rtm.rtm_family = is_ipv6 ? AF_INET6 : AF_INET;
	req.rtm.rtm_dst_len = addr_len * 8; /* subnet length, /128 or /64 bits */
	req.rtm.rtm_flags = RTM_F_FIB_MATCH;
	req.rta.rta_type = RTA_DST;
	req.rta.rta_len = RTA_LENGTH(addr_len);

	/* nocheck:blocked - bounded by addr_len, which is 4 or 16 by construction */
	memcpy(req.addr, g_inet_address_to_bytes(addr), addr_len);

	fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
	if (fd < 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_REACHABLE,
				    "failed to create netlink socket");
		return FALSE;
	}
	if (sendto(fd, &req, req.nlh.nlmsg_len, 0, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_REACHABLE,
				    "failed to send netlink route request");
		return FALSE;
	}
	len = recv(fd, buf, sizeof(buf), 0);
	if (len < 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_REACHABLE,
				    "failed to receive netlink route reply");
		return FALSE;
	}

	nlh = (const struct nlmsghdr *)buf;
	if (!NLMSG_OK(nlh, (gsize)len)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_REACHABLE,
				    "truncated netlink route reply");
		return FALSE;
	}

	/* an unreachable destination comes back as a netlink error */
	if (nlh->nlmsg_type != RTM_NEWROUTE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_REACHABLE,
				    "network is unreachable");
		return FALSE;
	}

	/* a route may still be a reject type (unreachable/blackhole/prohibit) */
	rtm = NLMSG_DATA(nlh);
	if (rtm->rtm_type == RTN_UNREACHABLE || rtm->rtm_type == RTN_BLACKHOLE ||
	    rtm->rtm_type == RTN_PROHIBIT || rtm->rtm_type == RTN_THROW) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_REACHABLE,
				    "route to destination is unreachable");
		return FALSE;
	}

	return TRUE;
}

typedef struct {
	GMainLoop *loop;
	GList *addresses; /* (element-type GInetAddress) */
	GError *error;
} FwupdNetworkResolveData;

static void
fwupd_network_resolve_done_cb(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	FwupdNetworkResolveData *data = user_data;
	data->addresses =
	    g_resolver_lookup_by_name_finish(G_RESOLVER(source_object), res, &data->error);
	g_main_loop_quit(data->loop);
}

static gboolean
fwupd_network_resolve_timeout_cb(gpointer user_data)
{
	GCancellable *cancellable = G_CANCELLABLE(user_data);
	/* makes the async lookup complete with G_IO_ERROR_CANCELLED */
	g_cancellable_cancel(cancellable);
	return G_SOURCE_REMOVE;
}

/*
 * fwupd_network_lookup_by_name_timeout:
 * @hostname: the hostname to resolve
 * @error: (nullable): optional return location for an error
 *
 * Resolve @hostname like g_resolver_lookup_by_name(), but give up after
 * %FWUPD_NETWORK_DNS_TIMEOUT_MS so a stalled resolver cannot block the caller
 * indefinitely.
 *
 * Returns: (transfer full) (element-type GInetAddress): the addresses, or %NULL.
 */
static GList *
fwupd_network_lookup_by_name_timeout(const gchar *hostname, GError **error)
{
	g_autoptr(GMainContext) context = g_main_context_new();
	g_autoptr(GMainLoop) loop = NULL;
	g_autoptr(GResolver) resolver = g_resolver_get_default();
	g_autoptr(GCancellable) cancellable = g_cancellable_new();
	g_autoptr(GSource) timeout_source = NULL;
	FwupdNetworkResolveData data = {0};

	g_main_context_push_thread_default(context);
	loop = g_main_loop_new(context, FALSE);
	data.loop = loop;

	timeout_source = g_timeout_source_new(FWUPD_NETWORK_DNS_TIMEOUT_MS);
	g_source_set_callback(timeout_source, fwupd_network_resolve_timeout_cb, cancellable, NULL);
	g_source_attach(timeout_source, context);

	g_resolver_lookup_by_name_async(resolver,
					hostname,
					cancellable,
					fwupd_network_resolve_done_cb,
					&data);
	g_main_loop_run(loop);

	g_source_destroy(timeout_source);
	g_main_context_pop_thread_default(context);

	if (data.addresses != NULL)
		return data.addresses;

	/* our timeout surfaces as a cancellation; turn it into a clear error */
	if (g_error_matches(data.error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		g_clear_error(&data.error);
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_REACHABLE,
				    "timed out resolving hostname");
		return NULL;
	}
	g_propagate_error(error, g_steal_pointer(&data.error));
	return NULL;
}

/**
 * fwupd_network_is_reachable:
 * @hostname: the hostname to check reachability for
 * @port: the port number
 * @error: (nullable): optional return location for an error
 *
 * Check if the network is reachable without spawning subprocesses, using
 * GNetworkMonitor, or enumerating the kernel route table.
 *
 * The check works as follows:
 *
 * 1. Resolve the hostname; DNS working is itself a sign of connectivity, and
 *    a concrete destination address is needed to query the kernel
 * 2. For each resolved address, perform a single netlink query to the kernel
 *    for FIB lookup and report reachable if the kernel has a usable route
 *
 * This avoids the GNetworkMonitor path, whose Linux netlink backend (used when
 * NetworkManager is absent) dumps the entire kernel route table into memory,
 * causing CPU/RAM exhaustion on systems with very large route tables (#10525).
 *
 * Returns: %TRUE if the network is reachable
 *
 * Since: 2.1.6
 **/
gboolean
fwupd_network_is_reachable(const gchar *hostname, gint port, GError **error)
{
	g_autolist(GInetAddress) addresses = NULL;

	g_debug("checking reachability of %s:%d", hostname, port);

	/* verify DNS resolution works for the target host */
	addresses = fwupd_network_lookup_by_name_timeout(hostname, error);
	if (addresses == NULL)
		return FALSE;

	/* reachable if the kernel has a usable route to any resolved address */
	for (GList *l = addresses; l != NULL; l = l->next) {
		g_autoptr(GError) error_local = NULL;
		if (fwupd_network_addr_is_routable(G_INET_ADDRESS(l->data), &error_local))
			return TRUE;
		g_debug("%s", error_local->message);
	}

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_REACHABLE,
			    "network is unreachable");
	return FALSE;
}
