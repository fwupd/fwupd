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
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>

#include "fwupd-build.h"
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
	gchar *hostname;
	GList *addresses; /* (element-type GInetAddress) */
	gboolean done;
	gint refcount;
	GMutex mutex;
	GCond cond;
} FwupdNetworkResolveData;

static void
fwupd_network_resolve_data_unref(FwupdNetworkResolveData *data)
{
	if (!g_atomic_int_dec_and_test(&data->refcount))
		return;

	g_list_free_full(data->addresses, g_object_unref);
	g_free(data->hostname);
	g_mutex_clear(&data->mutex);
	g_cond_clear(&data->cond);
	g_free(data);
}

/*
 * Runs on a throwaway thread so a stalled getaddrinfo() cannot block us past the
 * timeout: if we give up first, the thread keeps running until the resolver
 * returns and then drops the last reference, freeing the (now unwanted) result.
 */
static gpointer
fwupd_network_resolve_worker(gpointer user_data)
{
	FwupdNetworkResolveData *data = user_data;
	struct addrinfo hints = {.ai_socktype = SOCK_STREAM};
	struct addrinfo *res = NULL;
	GList *addresses = NULL;

	if (getaddrinfo(data->hostname, NULL, &hints, &res) == 0) {
		for (struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next) {
			GInetAddress *addr = NULL;

			if (ai->ai_family == AF_INET) {
				struct sockaddr_in *sin = (struct sockaddr_in *)ai->ai_addr;
				addr = g_inet_address_new_from_bytes((guint8 *)&sin->sin_addr,
								     G_SOCKET_FAMILY_IPV4);
			} else if (ai->ai_family == AF_INET6) {
				struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)ai->ai_addr;
				addr = g_inet_address_new_from_bytes((guint8 *)&sin6->sin6_addr,
								     G_SOCKET_FAMILY_IPV6);
			}
			if (addr != NULL)
				addresses = g_list_prepend(addresses, addr);
		}
		freeaddrinfo(res);
	}

	g_mutex_lock(&data->mutex);
	data->addresses = g_list_reverse(addresses);
	data->done = TRUE;
	g_cond_signal(&data->cond);
	g_mutex_unlock(&data->mutex);

	fwupd_network_resolve_data_unref(data);
	return NULL;
}

/*
 * fwupd_network_lookup_by_name_timeout:
 * @hostname: the hostname to resolve
 * @error: (nullable): optional return location for an error
 *
 * Resolve @hostname with getaddrinfo(), bounded by %FWUPD_NETWORK_DNS_TIMEOUT_MS.
 * getaddrinfo() is used rather than GResolver because the latter, since GLib
 * 2.85, instantiates the default GNetworkMonitor on the first lookup -- whose
 * netlink backend dumps the entire kernel route table, the very cost #10525 is
 * about. getaddrinfo() touches no GNetworkMonitor.
 *
 * Returns: (transfer full) (element-type GInetAddress): the addresses, or %NULL.
 */
static GList *
fwupd_network_lookup_by_name_timeout(const gchar *hostname, GError **error)
{
	FwupdNetworkResolveData *data = g_new0(FwupdNetworkResolveData, 1);
	GThread *thread;
	GList *addresses = NULL;
	gboolean done;
	gint64 deadline;

	g_mutex_init(&data->mutex);
	g_cond_init(&data->cond);
	data->hostname = g_strdup(hostname);
	data->refcount = 2; /* this function + the worker thread */

	thread = g_thread_new("fwupd-resolve", fwupd_network_resolve_worker, data);
	g_thread_unref(thread); /* detached; reclaimed via the shared refcount */

	deadline =
	    g_get_monotonic_time() + (FWUPD_NETWORK_DNS_TIMEOUT_MS * G_TIME_SPAN_MILLISECOND);
	g_mutex_lock(&data->mutex);
	while (!data->done) {
		if (!g_cond_wait_until(&data->cond, &data->mutex, deadline))
			break; /* timed out */
	}
	done = data->done;
	if (done)
		addresses = g_steal_pointer(&data->addresses);
	g_mutex_unlock(&data->mutex);

	fwupd_network_resolve_data_unref(data);

	if (!done) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_REACHABLE,
				    "timed out resolving hostname");
		return NULL;
	}
	if (addresses == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_REACHABLE,
				    "failed to resolve hostname");
		return NULL;
	}
	return addresses;
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
