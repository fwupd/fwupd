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

/* generic netlink socket wrapper */
typedef struct {
	struct mnl_socket *nl;
	gchar *buf;
	guint32 family_id;
	guint32 config_group_id;
} FuDevlinkGenSocket;

#define FU_DEVLINK_NETLINK_BUF_SIZE MNL_SOCKET_BUFFER_SIZE

/* send/receive functions */
gboolean
fu_devlink_netlink_msg_run(FuDevlinkGenSocket *nlg,
			   gsize len,
			   guint32 seq,
			   mnl_cb_t cb,
			   void *data,
			   GError **error) G_GNUC_NON_NULL(1, 4);

gboolean
fu_devlink_netlink_msg_send_recv(FuDevlinkGenSocket *nlg,
				 struct nlmsghdr *nlh,
				 mnl_cb_t cb,
				 void *data,
				 GError **error) G_GNUC_NON_NULL(1, 2);

gboolean
fu_devlink_netlink_msg_send(FuDevlinkGenSocket *nlg, struct nlmsghdr *nlh, GError **error) G_GNUC_NON_NULL(1, 2);

/* socket management */
FuDevlinkGenSocket *
fu_devlink_netlink_gen_socket_open(GError **error);

void
fu_devlink_netlink_gen_socket_close(FuDevlinkGenSocket *nlg);

gint
fu_devlink_netlink_gen_socket_get_fd(FuDevlinkGenSocket *nlg) G_GNUC_NON_NULL(1);

gchar *
fu_devlink_netlink_gen_socket_get_buf(FuDevlinkGenSocket *nlg) G_GNUC_NON_NULL(1);

/* prepare devlink command message */
struct nlmsghdr *
fu_devlink_netlink_cmd_prepare(FuDevlinkGenSocket *nlg, guint8 cmd, gboolean dump) G_GNUC_NON_NULL(1);

/* multicast group management */
gboolean
fu_devlink_netlink_mcast_group_subscribe(FuDevlinkGenSocket *nlg, GError **error) G_GNUC_NON_NULL(1);

/* attribute parsing callback */
int
fu_devlink_netlink_attr_cb(const struct nlattr *attr, void *data) G_GNUC_NON_NULL(1, 2);

/* cleanup function for auto cleanup */
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuDevlinkGenSocket, fu_devlink_netlink_gen_socket_close)
