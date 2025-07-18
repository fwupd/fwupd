/*
 * Copyright 2025 NVIDIA Corporation & Affiliates
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <complex.h>
#include <errno.h>
#include <libmnl/libmnl.h>
#include <linux/genetlink.h>
#include <linux/netlink.h>
#include <string.h>
#include <time.h>

#include "fu-devlink-netlink.h"

struct nlmsghdr *
fu_devlink_netlink_msg_prepare(void *buf,
			       uint32_t nlmsg_type,
			       gboolean dump,
			       void *extra_header,
			       size_t extra_header_size)
{
	struct nlmsghdr *nlh;
	void *eh;
	g_autoptr(GError) error = NULL;

	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type = nlmsg_type;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	if (dump)
		nlh->nlmsg_flags |= NLM_F_DUMP;
	nlh->nlmsg_seq = time(NULL);

	eh = mnl_nlmsg_put_extra_header(nlh, extra_header_size);
	if (!fu_memcpy_safe(eh,
			    extra_header_size,
			    0,
			    extra_header,
			    extra_header_size,
			    0,
			    extra_header_size,
			    &error)) {
		g_warning("failed to copy extra header: %s", error->message);
		return NULL;
	}

	return nlh;
}

/* Attribute parser callback for netlink error attributes */
static int
fu_devlink_netlink_nlmsgerr_attr_cb(const struct nlattr *attr, void *data)
{
	const struct nlattr **tb = data;
	int type = mnl_attr_get_type(attr);

	if (mnl_attr_type_valid(attr, NLMSGERR_ATTR_MAX) < 0)
		return MNL_CB_OK;

	tb[type] = attr;
	return MNL_CB_OK;
}

typedef struct {
	void *user_data;
	mnl_cb_t user_cb;
	GError **error;
} FuDevlinkCbCtx;

static gboolean
fu_devlink_netlink_error_cb_extack(const struct nlmsghdr *nlh, GError **error)
{
	const struct nlmsgerr *err = mnl_nlmsg_get_payload(nlh);
	struct nlattr *tb[NLMSGERR_ATTR_MAX + 1] = {};
	unsigned int hlen = sizeof(struct nlmsgerr);

	if (!(nlh->nlmsg_flags & NLM_F_ACK_TLVS))
		return FALSE;
	if (!(nlh->nlmsg_flags & NLM_F_CAPPED))
		hlen += mnl_nlmsg_get_payload_len(&err->msg);
	if (mnl_attr_parse(nlh, hlen, fu_devlink_netlink_nlmsgerr_attr_cb, tb) != MNL_CB_OK)
		return FALSE;
	if (!tb[NLMSGERR_ATTR_MSG])
		return FALSE;
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "netlink error: %s (%s)",
		    fwupd_strerror(-err->error),
		    mnl_attr_get_str(tb[NLMSGERR_ATTR_MSG]));
	return TRUE;
}

/* Error callback parses extack messages */
static int
fu_devlink_netlink_error_cb(const struct nlmsghdr *nlh, void *data)
{
	const struct nlmsgerr *err = mnl_nlmsg_get_payload(nlh);
	FuDevlinkCbCtx *ctx = data;

	if (mnl_nlmsg_get_payload_len(nlh) < sizeof(*err))
		return MNL_CB_STOP;
	if (!err->error)
		return MNL_CB_STOP;
	if (fu_devlink_netlink_error_cb_extack(nlh, ctx->error))
		return MNL_CB_ERROR;
	g_set_error(ctx->error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "netlink error: %s",
		    fwupd_strerror(-err->error));
	return MNL_CB_ERROR;
}

static int
fu_devlink_netlink_noop_cb(const struct nlmsghdr *nlh, void *data)
{
	return MNL_CB_STOP;
}

static int
fu_devlink_netlink_done_cb(const struct nlmsghdr *nlh, void *data)
{
	return MNL_CB_STOP;
}

/* Data callback wrapper that calls the user callback */
static int
fu_devlink_netlink_data_cb(const struct nlmsghdr *nlh, void *data)
{
	FuDevlinkCbCtx *ctx = data;

	if (!ctx->user_cb)
		return MNL_CB_OK;
	return ctx->user_cb(nlh, ctx->user_data);
}

static const mnl_cb_t fu_devlink_cbs[NLMSG_MIN_TYPE] = {
    [NLMSG_NOOP] = fu_devlink_netlink_noop_cb,
    [NLMSG_ERROR] = fu_devlink_netlink_error_cb,
    [NLMSG_DONE] = fu_devlink_netlink_done_cb,
    [NLMSG_OVERRUN] = fu_devlink_netlink_noop_cb,
};

/* Receive and run callback on netlink messages with proper error handling */
gboolean
fu_devlink_netlink_msg_recv_run(FuDevlinkGenSocket *nlg,
				guint32 seq,
				mnl_cb_t cb,
				void *data,
				GError **error)
{
	guint32 portid = mnl_socket_get_portid(nlg->nl);
	FuDevlinkCbCtx ctx = {
	    .user_data = data,
	    .user_cb = cb,
	    .error = error,
	};
	gint rc;

	do {
		rc = mnl_socket_recvfrom(nlg->nl, nlg->buf, MNL_SOCKET_BUFFER_SIZE);
		if (rc < 0) {
			if (!rc)
				return TRUE;
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to receive netlink message: %s",
				    fwupd_strerror(errno));
			return FALSE;
		}

		rc = mnl_cb_run2(nlg->buf,
				 rc,
				 seq,
				 portid,
				 fu_devlink_netlink_data_cb,
				 &ctx,
				 fu_devlink_cbs,
				 MNL_ARRAY_SIZE(fu_devlink_cbs));
	} while (rc > MNL_CB_STOP);

	return rc == MNL_CB_STOP;
}

/* Send message and receive response */
gboolean
fu_devlink_netlink_msg_send_recv(FuDevlinkGenSocket *nlg,
				 struct nlmsghdr *nlh,
				 mnl_cb_t cb,
				 void *data,
				 GError **error)
{
	guint32 seq = nlh->nlmsg_seq;
	gint rc;

	rc = mnl_socket_sendto(nlg->nl, nlh, nlh->nlmsg_len);
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to send netlink message: %s",
			    fwupd_strerror(errno));
		return FALSE;
	}
	return fu_devlink_netlink_msg_recv_run(nlg, seq, cb, data, error);
}

gboolean
fu_devlink_netlink_msg_send(FuDevlinkGenSocket *nlg, struct nlmsghdr *nlh, GError **error)
{
	return fu_devlink_netlink_msg_send_recv(nlg, nlh, NULL, NULL, error);
}

/* Generic Netlink control attribute callback */
static int
fu_devlink_netlink_genl_ctrl_attr_cb(const struct nlattr *attr, void *data)
{
	const struct nlattr **tb = data;
	int type = mnl_attr_get_type(attr);

	if (mnl_attr_type_valid(attr, CTRL_ATTR_MAX) < 0)
		return MNL_CB_OK;

	tb[type] = attr;
	return MNL_CB_OK;
}

static int
fu_devlink_netlink_genl_mcast_group_attr_cb(const struct nlattr *attr, void *data)
{
	const struct nlattr **tb = data;
	int type = mnl_attr_get_type(attr);

	if (mnl_attr_type_valid(attr, CTRL_ATTR_MCAST_GRP_MAX) < 0)
		return MNL_CB_OK;

	tb[type] = attr;
	return MNL_CB_OK;
}

/* Family resolution callback */
static int
fu_devlink_netlink_fu_devlink_netlink_genl_family_get_cb(const struct nlmsghdr *nlh, void *data)
{
	FuDevlinkGenSocket *nlg = data;
	struct genlmsghdr *genl = mnl_nlmsg_get_payload(nlh);
	struct nlattr *tb[CTRL_ATTR_MAX + 1] = {};
	struct nlattr *mcgrp;

	mnl_attr_parse(nlh, sizeof(*genl), fu_devlink_netlink_genl_ctrl_attr_cb, tb);
	if (!tb[CTRL_ATTR_FAMILY_ID])
		return MNL_CB_ERROR;
	nlg->family_id = mnl_attr_get_u16(tb[CTRL_ATTR_FAMILY_ID]);

	if (!tb[CTRL_ATTR_MCAST_GROUPS])
		return MNL_CB_ERROR;

	mnl_attr_for_each_nested(mcgrp, tb[CTRL_ATTR_MCAST_GROUPS])
	{
		struct nlattr *tb_grp[CTRL_ATTR_MCAST_GRP_MAX + 1] = {};

		mnl_attr_parse_nested(mcgrp, fu_devlink_netlink_genl_mcast_group_attr_cb, tb_grp);

		if (!tb_grp[CTRL_ATTR_MCAST_GRP_NAME] || !tb_grp[CTRL_ATTR_MCAST_GRP_ID])
			continue;

		if (!g_strcmp0(mnl_attr_get_str(tb_grp[CTRL_ATTR_MCAST_GRP_NAME]),
			       DEVLINK_GENL_MCGRP_CONFIG_NAME)) {
			nlg->config_group_id = mnl_attr_get_u32(tb_grp[CTRL_ATTR_MCAST_GRP_ID]);
			return MNL_CB_OK;
		}
	}

	return MNL_CB_ERROR;
}

/* Get generic netlink family ID */
static gboolean
fu_devlink_netlink_genl_family_get(FuDevlinkGenSocket *nlg,
				   const gchar *family_name,
				   GError **error)
{
	struct genlmsghdr hdr = {
	    .cmd = CTRL_CMD_GETFAMILY,
	    .version = 0x1,
	};
	struct nlmsghdr *nlh;

	nlh = fu_devlink_netlink_msg_prepare(nlg->buf, GENL_ID_CTRL, FALSE, &hdr, sizeof(hdr));
	mnl_attr_put_strz(nlh, CTRL_ATTR_FAMILY_NAME, family_name);
	return fu_devlink_netlink_msg_send_recv(
	    nlg,
	    nlh,
	    fu_devlink_netlink_fu_devlink_netlink_genl_family_get_cb,
	    nlg,
	    error);
}

/* Open generic netlink socket for devlink family */
gboolean
fu_devlink_netlink_gen_socket_open(FuDevlinkGenSocket *nlg, GError **error)
{
	gint one = 1;
	gint rc;

	/* Initialize structure with properly aligned buffer */
	nlg->buf = g_malloc0(MNL_SOCKET_BUFFER_SIZE);

	/* Open netlink socket */
	nlg->nl = mnl_socket_open(NETLINK_GENERIC);
	if (!nlg->nl) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to open netlink socket: %s",
			    fwupd_strerror(errno));
		g_free(nlg->buf);
		return FALSE;
	}

	rc = mnl_socket_setsockopt(nlg->nl, NETLINK_CAP_ACK, &one, sizeof(one));
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to set netlink CAP_ACK: %s",
			    fwupd_strerror(errno));
		mnl_socket_close(nlg->nl);
		g_free(nlg->buf);
		return FALSE;
	}

	rc = mnl_socket_setsockopt(nlg->nl, NETLINK_EXT_ACK, &one, sizeof(one));
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to set netlink EXT_ACK: %s",
			    fwupd_strerror(errno));
		mnl_socket_close(nlg->nl);
		g_free(nlg->buf);
		return FALSE;
	}

	rc = mnl_socket_bind(nlg->nl, 0, MNL_SOCKET_AUTOPID);
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to bind netlink socket: %s",
			    fwupd_strerror(errno));
		mnl_socket_close(nlg->nl);
		g_free(nlg->buf);
		return FALSE;
	}

	/* Resolve devlink family ID dynamically */
	if (!fu_devlink_netlink_genl_family_get(nlg, DEVLINK_GENL_NAME, error)) {
		g_prefix_error(error, "failed to resolve devlink family ID: ");
		mnl_socket_close(nlg->nl);
		g_free(nlg->buf);
		return FALSE;
	}

	return TRUE;
}

/* Close generic netlink socket */
void
fu_devlink_netlink_gen_socket_close(FuDevlinkGenSocket *nlg)
{
	if (nlg->nl) {
		mnl_socket_close(nlg->nl);
		nlg->nl = NULL;
	}
	if (nlg->buf) {
		g_free(nlg->buf);
		nlg->buf = NULL;
	}
}

gint
fu_devlink_netlink_gen_socket_get_fd(FuDevlinkGenSocket *nlg)
{
	return mnl_socket_get_fd(nlg->nl);
}

/* Prepare devlink command message */
struct nlmsghdr *
fu_devlink_netlink_cmd_prepare(FuDevlinkGenSocket *nlg, guint8 cmd, gboolean dump)
{
	struct genlmsghdr hdr = {
	    .cmd = cmd,
	    .version = DEVLINK_GENL_VERSION,
	};

	return fu_devlink_netlink_msg_prepare(nlg->buf, nlg->family_id, dump, &hdr, sizeof(hdr));
}

gboolean
fu_devlink_netlink_mcast_group_subscribe(FuDevlinkGenSocket *nlg)
{
	guint32 devlink_config_grp = nlg->config_group_id;
	gint rc;

	rc = mnl_socket_setsockopt(nlg->nl,
				   NETLINK_ADD_MEMBERSHIP,
				   &devlink_config_grp,
				   sizeof(devlink_config_grp));
	if (rc < 0) {
		g_debug("failed to subscribe to devlink notifications: %s", fwupd_strerror(errno));
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_devlink_netlink_mcast_group_unsubscribe(FuDevlinkGenSocket *nlg)
{
	guint32 devlink_config_grp = nlg->config_group_id;
	gint rc;

	rc = mnl_socket_setsockopt(nlg->nl,
				   NETLINK_DROP_MEMBERSHIP,
				   &devlink_config_grp,
				   sizeof(devlink_config_grp));
	if (rc < 0) {
		g_debug("failed to unsubscribe from devlink notifications: %s",
			fwupd_strerror(errno));
		return FALSE;
	}

	return TRUE;
}

/* Simple attribute parser callback for devlink attributes */
int
fu_devlink_netlink_attr_cb(const struct nlattr *attr, void *data)
{
	const struct nlattr **tb = data;
	int type = mnl_attr_get_type(attr);
	int rc;

	rc = mnl_attr_type_valid(attr, DEVLINK_ATTR_MAX);
	if (rc < 0)
		return MNL_CB_OK;

	tb[type] = attr;
	return MNL_CB_OK;
}
