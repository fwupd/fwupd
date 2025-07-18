/*
 * Copyright 2025 NVIDIA Corporation & Affiliates
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>
#include <libmnl/libmnl.h>
#include <linux/devlink.h>
#include <linux/genetlink.h>
#include <linux/netlink.h>

/* Generic netlink socket wrapper */
typedef struct {
	struct mnl_socket *nl;
	gchar *buf;
	guint32 family_id;
	guint32 config_group_id;
} FuDevlinkGenSocket;

/* Message preparation */
struct nlmsghdr *
fu_devlink_netlink_msg_prepare(void *buf,
			       guint32 nlmsg_type,
			       gboolean dump,
			       void *extra_header,
			       gsize extra_header_size);

/* Send/receive functions */
gboolean
fu_devlink_netlink_msg_recv_run(FuDevlinkGenSocket *nlg,
				guint32 seq,
				mnl_cb_t cb,
				void *data,
				GError **error);

gboolean
fu_devlink_netlink_msg_send_recv(FuDevlinkGenSocket *nlg,
				 struct nlmsghdr *nlh,
				 mnl_cb_t cb,
				 void *data,
				 GError **error);

gboolean
fu_devlink_netlink_msg_send(FuDevlinkGenSocket *nlg, struct nlmsghdr *nlh, GError **error);

/* Socket management */
gboolean
fu_devlink_netlink_gen_socket_open(FuDevlinkGenSocket *nlg, GError **error);

void
fu_devlink_netlink_gen_socket_close(FuDevlinkGenSocket *nlg);

gint
fu_devlink_netlink_gen_socket_get_fd(FuDevlinkGenSocket *nlg);

struct nlmsghdr *
fu_devlink_netlink_cmd_prepare(FuDevlinkGenSocket *nlg, guint8 cmd, gboolean dump);

/* Multicast group management */
gboolean
fu_devlink_netlink_mcast_group_subscribe(FuDevlinkGenSocket *nlg);

gboolean
fu_devlink_netlink_mcast_group_unsubscribe(FuDevlinkGenSocket *nlg);

/* Attribute parsing callback */
int
fu_devlink_netlink_attr_cb(const struct nlattr *attr, void *data);

/* Cleanup function for auto cleanup */
G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(FuDevlinkGenSocket, fu_devlink_netlink_gen_socket_close)
