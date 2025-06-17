/*
 * Copyright 2025 NVIDIA Corporation & Affiliates
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <glib-unix.h>
#include <libmnl/libmnl.h>
#include <linux/genetlink.h>
#include <linux/netlink.h>

#include "fu-devlink-netlink.h"

static struct nlmsghdr *
fu_devlink_netlink_msg_prepare(gpointer buf,
			       guint32 nlmsg_type,
			       gboolean dump,
			       gpointer extra_header,
			       gsize extra_header_size)
{
	struct nlmsghdr *nlh;
	gpointer eh;

	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type = nlmsg_type;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	if (dump)
		nlh->nlmsg_flags |= NLM_F_DUMP;
	nlh->nlmsg_seq = (guint32)time(NULL);

	eh = mnl_nlmsg_put_extra_header(nlh, extra_header_size);
	memcpy(eh, extra_header, extra_header_size); /* nocheck:blocked */

	return nlh;
}

/* attribute parser callback for netlink error attributes */
static gint
fu_devlink_netlink_nlmsgerr_attr_cb(const struct nlattr *attr, gpointer data)
{
	const struct nlattr **tb = data;
	gint type = mnl_attr_get_type(attr);

	if (mnl_attr_type_valid(attr, NLMSGERR_ATTR_MAX) < 0)
		return MNL_CB_OK;

	tb[type] = attr;
	return MNL_CB_OK;
}

typedef struct {
	gpointer user_data;
	mnl_cb_t user_cb;
	GError **error;
} FuDevlinkCbHelper;

static gboolean
fu_devlink_netlink_error_cb_extack(const struct nlmsghdr *nlh, GError **error)
{
	const struct nlmsgerr *err = mnl_nlmsg_get_payload(nlh);
	struct nlattr *tb[NLMSGERR_ATTR_MAX + 1] = {};
	guint hlen = sizeof(struct nlmsgerr);

	if ((nlh->nlmsg_flags & NLM_F_ACK_TLVS) == 0)
		return FALSE;
	if ((nlh->nlmsg_flags & NLM_F_CAPPED) == 0)
		hlen += mnl_nlmsg_get_payload_len(&err->msg);
	if (mnl_attr_parse(nlh, hlen, fu_devlink_netlink_nlmsgerr_attr_cb, tb) != MNL_CB_OK)
		return FALSE;
	if (tb[NLMSGERR_ATTR_MSG] == NULL)
		return FALSE;
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "netlink error: %s; %s",
		    fwupd_strerror(-err->error),
		    mnl_attr_get_str(tb[NLMSGERR_ATTR_MSG]));
	return TRUE;
}

/* error callback parses extack messages */
static gint
fu_devlink_netlink_error_cb(const struct nlmsghdr *nlh, gpointer data)
{
	const struct nlmsgerr *err = mnl_nlmsg_get_payload(nlh);
	FuDevlinkCbHelper *helper = data;

	if (mnl_nlmsg_get_payload_len(nlh) < sizeof(*err))
		return MNL_CB_STOP;
	if (err->error == 0)
		return MNL_CB_STOP;
	if (fu_devlink_netlink_error_cb_extack(nlh, helper->error))
		return MNL_CB_ERROR;
	g_set_error(helper->error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "netlink error: %s",
		    fwupd_strerror(-err->error));
	return MNL_CB_ERROR;
}

static gint
fu_devlink_netlink_noop_cb(const struct nlmsghdr *nlh, gpointer data)
{
	return MNL_CB_STOP;
}

static gint
fu_devlink_netlink_done_cb(const struct nlmsghdr *nlh, gpointer data)
{
	return MNL_CB_STOP;
}

/* data callback wrapper that calls the user callback */
static gint
fu_devlink_netlink_data_cb(const struct nlmsghdr *nlh, gpointer data)
{
	FuDevlinkCbHelper *helper = data;

	if (helper->user_cb == NULL)
		return MNL_CB_OK;
	return helper->user_cb(nlh, helper->user_data);
}

static gint
fu_devlink_netlink_msg_cb_run(FuDevlinkGenSocket *nlg,
			      gsize len,
			      guint32 seq,
			      mnl_cb_t cb,
			      gpointer data,
			      GError **error)
{
	guint32 portid;
	FuDevlinkCbHelper helper = {
	    .user_data = data,
	    .user_cb = cb,
	    .error = error,
	};
	mnl_cb_t cbs[NLMSG_MIN_TYPE] = {
	    [NLMSG_NOOP] = fu_devlink_netlink_noop_cb,
	    [NLMSG_ERROR] = fu_devlink_netlink_error_cb,
	    [NLMSG_DONE] = fu_devlink_netlink_done_cb,
	    [NLMSG_OVERRUN] = fu_devlink_netlink_noop_cb,
	};

	if (nlg->is_emulated) {
		struct nlmsghdr *nlh = (gpointer)nlg->buf;

		nlh->nlmsg_seq = seq;
		portid = nlh->nlmsg_pid;
	} else {
		portid = mnl_socket_get_portid(nlg->nl);
	}

	return mnl_cb_run2(nlg->buf,
			   len,
			   seq,
			   portid,
			   fu_devlink_netlink_data_cb,
			   &helper,
			   cbs,
			   MNL_ARRAY_SIZE(cbs));
}

/* run callback on netlink messages */
gboolean
fu_devlink_netlink_msg_run(FuDevlinkGenSocket *nlg,
			   gsize len,
			   guint32 seq,
			   mnl_cb_t cb,
			   gpointer data,
			   GError **error)
{
	g_return_val_if_fail(nlg != NULL, FALSE);
	return fu_devlink_netlink_msg_cb_run(nlg, len, seq, cb, data, error) != MNL_CB_ERROR;
}

/* callback for extracting event_id from devlink messages */
static gint
fu_devlink_netlink_event_id_msg_cb(const struct nlmsghdr *nlh, gpointer data)
{
	struct genlmsghdr *genl = mnl_nlmsg_get_payload(nlh);
	struct nlattr *tb[DEVLINK_ATTR_MAX + 1] = {};
	GStrvBuilder *tuples_builder = data;

	switch (genl->cmd) {
	case DEVLINK_CMD_GET:
		g_strv_builder_add(tuples_builder, "cmd=get");
		break;
	case DEVLINK_CMD_RELOAD:
		g_strv_builder_add(tuples_builder, "cmd=reload");
		break;
	case DEVLINK_CMD_INFO_GET:
		g_strv_builder_add(tuples_builder, "cmd=info_get");
		break;
	case DEVLINK_CMD_FLASH_UPDATE:
		g_strv_builder_add(tuples_builder, "cmd=flash_update");
		break;
	default:
		break;
	}

	/* parse attributes */
	mnl_attr_parse(nlh, sizeof(*genl), fu_devlink_netlink_attr_cb, tb);

	/* extract attribute name and value based on type */
	if (tb[DEVLINK_ATTR_BUS_NAME] != NULL) {
		g_strv_builder_add(
		    tuples_builder,
		    g_strdup_printf("bus_name=%s", mnl_attr_get_str(tb[DEVLINK_ATTR_BUS_NAME])));
	}
	if (tb[DEVLINK_ATTR_DEV_NAME] != NULL) {
		g_strv_builder_add(
		    tuples_builder,
		    g_strdup_printf("dev_name=%s", mnl_attr_get_str(tb[DEVLINK_ATTR_DEV_NAME])));
	}
	if (tb[DEVLINK_ATTR_FLASH_UPDATE_FILE_NAME] != NULL) {
		g_strv_builder_add(
		    tuples_builder,
		    g_strdup_printf("file_name=%s",
				    mnl_attr_get_str(tb[DEVLINK_ATTR_FLASH_UPDATE_FILE_NAME])));
	}
	if (tb[DEVLINK_ATTR_FLASH_UPDATE_COMPONENT] != NULL) {
		g_strv_builder_add(
		    tuples_builder,
		    g_strdup_printf("component=%s",
				    mnl_attr_get_str(tb[DEVLINK_ATTR_FLASH_UPDATE_COMPONENT])));
	}
	if (tb[DEVLINK_ATTR_RELOAD_ACTION] != NULL) {
		g_strv_builder_add(
		    tuples_builder,
		    g_strdup_printf("reload_action=%u",
				    mnl_attr_get_u8(tb[DEVLINK_ATTR_RELOAD_ACTION])));
	}

	return MNL_CB_OK;
}

static gchar *
fu_devlink_netlink_get_event_id(FuDevlinkGenSocket *nlg, struct nlmsghdr *nlh)
{
	g_autoptr(GStrvBuilder) tuples_builder = g_strv_builder_new();
	g_auto(GStrv) strv = NULL;

	/* parse the message and build get event_id tuples */
	mnl_cb_run(nlh,
		   nlh->nlmsg_len,
		   nlh->nlmsg_seq,
		   nlh->nlmsg_pid,
		   fu_devlink_netlink_event_id_msg_cb,
		   tuples_builder);

	strv = g_strv_builder_end(tuples_builder);

	/* return the constructed event_id */
	return g_strjoinv(",", strv);
}

/* receive and run callback on netlink messages */
static gboolean
fu_devlink_netlink_msg_recv_run(FuDevlinkGenSocket *nlg,
				struct nlmsghdr *nlh,
				mnl_cb_t cb,
				gpointer data,
				GError **error)
{
	FuDeviceEvent *event = NULL;
	guint i = 0;
	guint32 seq = nlh->nlmsg_seq;
	gint rc;
	g_autofree gchar *event_id = NULL;

	/* generate event ID if device is available and we need emulation/recording */
	if (nlg->is_emulated || nlg->save_events)
		event_id = fu_devlink_netlink_get_event_id(nlg, nlh);

	if (nlg->is_emulated) {
		event = fu_device_load_event(nlg->device, event_id, error);
		if (event == NULL)
			return FALSE;
	}

	if (nlg->save_events)
		event = fu_device_save_event(nlg->device, event_id);

	do {
		if (nlg->is_emulated) {
			const guint8 *response_buf;
			gsize response_len;
			g_autofree gchar *response_key = g_strdup_printf("ResponseData%u", i++);
			g_autoptr(GBytes) response_data =
			    fu_device_event_get_bytes(event, response_key, NULL);

			if (response_data == NULL)
				return TRUE;

			response_buf = g_bytes_get_data(response_data, &response_len);
			if (response_len > (gsize)FU_DEVLINK_NETLINK_BUF_SIZE) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "recorded response too large: %zu > %ld",
					    response_len,
					    FU_DEVLINK_NETLINK_BUF_SIZE);
				return FALSE;
			}
			memcpy(nlg->buf, response_buf, response_len); /* nocheck:blocked */
			rc = response_len;
		} else {
			rc = mnl_socket_recvfrom(nlg->nl, nlg->buf, MNL_SOCKET_BUFFER_SIZE);
			if (rc == 0)
				return TRUE;
			if (rc < 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "failed to receive netlink message: %s",
					    fwupd_strerror(errno));
				return FALSE;
			}
			if (nlg->save_events) {
				g_autoptr(GBytes) response_data = g_bytes_new(nlg->buf, rc);
				g_autofree gchar *response_key =
				    g_strdup_printf("ResponseData%u", i++);

				fu_device_event_set_bytes(event, response_key, response_data);
			}
		}

		rc = fu_devlink_netlink_msg_cb_run(nlg, rc, seq, cb, data, error);
	} while (rc > MNL_CB_STOP);

	return rc == MNL_CB_ERROR ? FALSE : TRUE;
}

/* send message and receive response */
gboolean
fu_devlink_netlink_msg_send_recv(FuDevlinkGenSocket *nlg,
				 struct nlmsghdr *nlh,
				 mnl_cb_t cb,
				 gpointer data,
				 GError **error)
{
	gint rc;

	g_return_val_if_fail(nlg != NULL, FALSE);
	g_return_val_if_fail(nlh != NULL, FALSE);

	if (!nlg->is_emulated) {
		/* send netlink message */
		rc = mnl_socket_sendto(nlg->nl, nlh, nlh->nlmsg_len);
		if (rc < 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to send netlink message: %s",
				    fwupd_strerror(errno));
			return FALSE;
		}
	}
	return fu_devlink_netlink_msg_recv_run(nlg, nlh, cb, data, error);
}

gboolean
fu_devlink_netlink_msg_send(FuDevlinkGenSocket *nlg, struct nlmsghdr *nlh, GError **error)
{
	g_return_val_if_fail(nlg != NULL, FALSE);
	return fu_devlink_netlink_msg_send_recv(nlg, nlh, NULL, NULL, error);
}

/* generic netlink control attribute callback */
static gint
fu_devlink_netlink_genl_ctrl_attr_cb(const struct nlattr *attr, gpointer data)
{
	const struct nlattr **tb = data;
	gint type = mnl_attr_get_type(attr);

	if (mnl_attr_type_valid(attr, CTRL_ATTR_MAX) < 0)
		return MNL_CB_OK;

	tb[type] = attr;
	return MNL_CB_OK;
}

static gint
fu_devlink_netlink_genl_mcast_group_attr_cb(const struct nlattr *attr, gpointer data)
{
	const struct nlattr **tb = data;
	gint type = mnl_attr_get_type(attr);

	if (mnl_attr_type_valid(attr, CTRL_ATTR_MCAST_GRP_MAX) < 0)
		return MNL_CB_OK;

	tb[type] = attr;
	return MNL_CB_OK;
}

/* family resolution callback */
static gint
fu_devlink_netlink_fu_devlink_netlink_genl_family_get_cb(const struct nlmsghdr *nlh, gpointer data)
{
	FuDevlinkGenSocket *nlg = data;
	struct genlmsghdr *genl = mnl_nlmsg_get_payload(nlh);
	struct nlattr *tb[CTRL_ATTR_MAX + 1] = {};
	struct nlattr *mcgrp;

	g_return_val_if_fail(nlh != NULL, MNL_CB_ERROR);

	mnl_attr_parse(nlh, sizeof(*genl), fu_devlink_netlink_genl_ctrl_attr_cb, tb);
	if (tb[CTRL_ATTR_FAMILY_ID] == NULL)
		return MNL_CB_ERROR;
	nlg->family_id = mnl_attr_get_u16(tb[CTRL_ATTR_FAMILY_ID]);

	if (tb[CTRL_ATTR_MCAST_GROUPS] == NULL)
		return MNL_CB_ERROR;

	mnl_attr_for_each_nested(mcgrp, tb[CTRL_ATTR_MCAST_GROUPS])
	{
		struct nlattr *tb_grp[CTRL_ATTR_MCAST_GRP_MAX + 1] = {};

		mnl_attr_parse_nested(mcgrp, fu_devlink_netlink_genl_mcast_group_attr_cb, tb_grp);

		if (tb_grp[CTRL_ATTR_MCAST_GRP_NAME] == NULL ||
		    tb_grp[CTRL_ATTR_MCAST_GRP_ID] == NULL)
			continue;

		if (g_strcmp0(mnl_attr_get_str(tb_grp[CTRL_ATTR_MCAST_GRP_NAME]),
			      DEVLINK_GENL_MCGRP_CONFIG_NAME) == 0) {
			nlg->config_group_id = mnl_attr_get_u32(tb_grp[CTRL_ATTR_MCAST_GRP_ID]);
			return MNL_CB_OK;
		}
	}

	return MNL_CB_ERROR;
}

/* get generic netlink family ID */
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

	g_return_val_if_fail(nlg != NULL, FALSE);
	g_return_val_if_fail(family_name != NULL, FALSE);

	nlh = fu_devlink_netlink_msg_prepare(nlg->buf, GENL_ID_CTRL, FALSE, &hdr, sizeof(hdr));
	mnl_attr_put_strz(nlh, CTRL_ATTR_FAMILY_NAME, family_name);
	return fu_devlink_netlink_msg_send_recv(
	    nlg,
	    nlh,
	    fu_devlink_netlink_fu_devlink_netlink_genl_family_get_cb,
	    nlg,
	    error);
}

/* open generic netlink socket for devlink family */
FuDevlinkGenSocket *
fu_devlink_netlink_gen_socket_open(FuDevice *device, GError **error)
{
	g_autoptr(FuDevlinkGenSocket) nlg = NULL;
	gint one = 1;
	gint rc;

	g_return_val_if_fail(FU_IS_DEVICE(device) || device == NULL, NULL);

	/* allocate and initialize structure */
	nlg = g_new0(FuDevlinkGenSocket, 1);

	/* initialize structure with properly aligned buffer */
	nlg->buf = g_malloc0(FU_DEVLINK_NETLINK_BUF_SIZE);

	if (device != NULL) {
		nlg->device = g_object_ref(device);
		if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED)) {
			/* skip actual socket operations if emulated */
			/* create dummy pipe for emulation */
			if (!g_unix_open_pipe(nlg->pipe_fds, O_CLOEXEC, error)) {
				g_prefix_error_literal(error,
						       "failed to create pipe for emulation: ");
				return NULL;
			}
			nlg->is_emulated = TRUE;

			/* set family ID to a valid value */
			nlg->family_id = NLMSG_MIN_TYPE + 1;
			return g_steal_pointer(&nlg);
		}
	}

	/* open netlink socket */
	nlg->nl = mnl_socket_open(NETLINK_GENERIC);
	if (nlg->nl == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to open netlink socket: %s",
			    fwupd_strerror(errno));
		return NULL;
	}

	rc = mnl_socket_setsockopt(nlg->nl, NETLINK_CAP_ACK, &one, sizeof(one));
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to set netlink CAP_ACK: %s",
			    fwupd_strerror(errno));
		return NULL;
	}

	rc = mnl_socket_setsockopt(nlg->nl, NETLINK_EXT_ACK, &one, sizeof(one));
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to set netlink EXT_ACK: %s",
			    fwupd_strerror(errno));
		return NULL;
	}

	rc = mnl_socket_bind(nlg->nl, 0, MNL_SOCKET_AUTOPID);
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to bind netlink socket: %s",
			    fwupd_strerror(errno));
		return NULL;
	}

	/* resolve devlink family ID dynamically */
	if (!fu_devlink_netlink_genl_family_get(nlg, DEVLINK_GENL_NAME, error)) {
		g_prefix_error_literal(error, "failed to resolve devlink family ID: ");
		return NULL;
	}

	if (device != NULL &&
	    fu_context_has_flag(fu_device_get_context(nlg->device), FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		nlg->save_events = TRUE;
	}

	return g_steal_pointer(&nlg);
}

/* close generic netlink socket */
void
fu_devlink_netlink_gen_socket_close(FuDevlinkGenSocket *nlg)
{
	if (nlg == NULL)
		return;
	if (nlg->nl != NULL)
		mnl_socket_close(nlg->nl);
	/* close both ends of the pipe if they were created */
	if (nlg->pipe_fds[0] != 0)
		g_close(nlg->pipe_fds[0], NULL);
	if (nlg->pipe_fds[1] != 0)
		g_close(nlg->pipe_fds[1], NULL);
	g_free(nlg->buf);
	if (nlg->device != NULL)
		g_object_unref(nlg->device);
	g_free(nlg);
}

gint
fu_devlink_netlink_gen_socket_get_fd(FuDevlinkGenSocket *nlg)
{
	g_return_val_if_fail(nlg != NULL, -1);

	/* return read side of pipe for emulated devices */
	if (nlg->is_emulated)
		return nlg->pipe_fds[0]; /* read end of the pipe */
	return mnl_socket_get_fd(nlg->nl);
}

gchar *
fu_devlink_netlink_gen_socket_get_buf(FuDevlinkGenSocket *nlg)
{
	g_return_val_if_fail(nlg != NULL, NULL);
	return nlg->buf;
}

/* prepare devlink command message */
struct nlmsghdr *
fu_devlink_netlink_cmd_prepare(FuDevlinkGenSocket *nlg, guint8 cmd, gboolean dump)
{
	struct genlmsghdr hdr = {
	    .cmd = cmd,
	    .version = DEVLINK_GENL_VERSION,
	};

	g_return_val_if_fail(nlg != NULL, NULL);

	return fu_devlink_netlink_msg_prepare(nlg->buf, nlg->family_id, dump, &hdr, sizeof(hdr));
}

gboolean
fu_devlink_netlink_mcast_group_subscribe(FuDevlinkGenSocket *nlg, GError **error)
{
	guint32 devlink_config_grp = nlg->config_group_id;
	gint rc;

	g_return_val_if_fail(nlg != NULL, FALSE);

	/* skip multicast subscription for emulated devices */
	if (nlg->is_emulated)
		return TRUE;

	rc = mnl_socket_setsockopt(nlg->nl,
				   NETLINK_ADD_MEMBERSHIP,
				   &devlink_config_grp,
				   sizeof(devlink_config_grp));
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to subscribe to devlink notifications: %s",
			    fwupd_strerror(errno));
		return FALSE;
	}

	/* success */
	return TRUE;
}

/* simple attribute parser callback for devlink attributes */
gint
fu_devlink_netlink_attr_cb(const struct nlattr *attr, gpointer data)
{
	const struct nlattr **tb = data;
	gint type = mnl_attr_get_type(attr);
	gint rc;

	rc = mnl_attr_type_valid(attr, DEVLINK_ATTR_MAX);
	if (rc < 0)
		return MNL_CB_OK;

	tb[type] = attr;
	return MNL_CB_OK;
}
