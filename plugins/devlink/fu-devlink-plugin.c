/*
 * Copyright 2025 NVIDIA Corporation & Affiliates
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-devlink-backend.h"
#include "fu-devlink-device.h"
#include "fu-devlink-netlink.h"
#include "fu-devlink-plugin.h"

struct _FuDevlinkPlugin {
	FuPlugin parent_instance;
	FuDevlinkGenSocket *nlg;
	GSource *netlink_source;
	FuDevlinkBackend *backend;
};

G_DEFINE_TYPE(FuDevlinkPlugin, fu_devlink_plugin, FU_TYPE_PLUGIN)

static void
fu_devlink_plugin_device_added_from_netlink(FuDevlinkPlugin *self, const struct nlmsghdr *nlh)
{
	struct nlattr *tb[DEVLINK_ATTR_MAX + 1] = {};
	const gchar *bus_name = NULL;
	const gchar *dev_name = NULL;
	g_autoptr(GError) error_local = NULL;

	/* parse netlink attributes using libmnl */
	mnl_attr_parse(nlh, sizeof(struct genlmsghdr), fu_devlink_netlink_attr_cb, tb);

	/* extract bus and device names */
	if (tb[DEVLINK_ATTR_BUS_NAME] != NULL)
		bus_name = mnl_attr_get_str(tb[DEVLINK_ATTR_BUS_NAME]);
	if (tb[DEVLINK_ATTR_DEV_NAME] != NULL)
		dev_name = mnl_attr_get_str(tb[DEVLINK_ATTR_DEV_NAME]);

	if (bus_name == NULL || dev_name == NULL) {
		g_debug("devlink device notification missing bus_name or dev_name");
		return;
	}

	g_debug("devlink device added: %s/%s", bus_name, dev_name);

	/* use backend to create device with proper hierarchy */
	if (!fu_devlink_backend_device_added(self->backend, bus_name, dev_name, &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			g_debug("failed to add devlink device %s/%s: %s",
				bus_name,
				dev_name,
				error_local->message);
		} else {
			g_warning("failed to add devlink device %s/%s: %s",
				  bus_name,
				  dev_name,
				  error_local->message);
		}
		return;
	}
}

static void
fu_devlink_plugin_device_removed_from_netlink(FuDevlinkPlugin *self, const struct nlmsghdr *nlh)
{
	struct nlattr *tb[DEVLINK_ATTR_MAX + 1] = {};
	const gchar *bus_name = NULL;
	const gchar *dev_name = NULL;

	/* parse netlink attributes using libmnl */
	mnl_attr_parse(nlh, sizeof(struct genlmsghdr), fu_devlink_netlink_attr_cb, tb);

	/* extract bus and device names */
	if (tb[DEVLINK_ATTR_BUS_NAME] != NULL)
		bus_name = mnl_attr_get_str(tb[DEVLINK_ATTR_BUS_NAME]);
	if (tb[DEVLINK_ATTR_DEV_NAME] != NULL)
		dev_name = mnl_attr_get_str(tb[DEVLINK_ATTR_DEV_NAME]);

	if (bus_name == NULL || dev_name == NULL) {
		g_debug("devlink device removal notification missing bus_name or dev_name");
		return;
	}

	g_debug("devlink device removed: %s/%s", bus_name, dev_name);

	/* use backend to remove device */
	fu_devlink_backend_device_removed(self->backend, bus_name, dev_name);
}

/* callback for processing individual netlink messages */
static gint
fu_devlink_plugin_process_message_cb(const struct nlmsghdr *nlh, gpointer data)
{
	FuDevlinkPlugin *self = FU_DEVLINK_PLUGIN(data);
	struct genlmsghdr *genl;

	genl = mnl_nlmsg_get_payload(nlh);
	switch (genl->cmd) {
	case DEVLINK_CMD_NEW:
		fu_devlink_plugin_device_added_from_netlink(self, nlh);
		break;
	case DEVLINK_CMD_DEL:
		fu_devlink_plugin_device_removed_from_netlink(self, nlh);
		break;
	default:
		break;
	}

	return MNL_CB_OK;
}

static gboolean
fu_devlink_plugin_netlink_cb(GIOChannel *channel, GIOCondition condition, gpointer user_data)
{
	FuDevlinkPlugin *self = FU_DEVLINK_PLUGIN(user_data);
	gsize len;
	GIOStatus status;
	g_autoptr(GError) error_local = NULL;

	if (condition & (G_IO_ERR | G_IO_HUP)) {
		g_debug("devlink netlink socket error");
		return FALSE;
	}

	/* read netlink message via GIOChannel */
	status = g_io_channel_read_chars(channel,
					 fu_devlink_netlink_gen_socket_get_buf(self->nlg),
					 FU_DEVLINK_NETLINK_BUF_SIZE,
					 &len,
					 &error_local);
	if (status != G_IO_STATUS_NORMAL) {
		if (error_local != NULL)
			g_debug("failed to read devlink netlink message: %s", error_local->message);
		return TRUE;
	}

	/* process netlink messages */
	if (!fu_devlink_netlink_msg_run(self->nlg,
					len,
					0,
					fu_devlink_plugin_process_message_cb,
					self,
					&error_local))
		g_warning("failed to process netlink message: %s", error_local->message);

	return TRUE;
}

static gboolean
fu_devlink_plugin_setup_netlink(FuDevlinkPlugin *self, GError **error)
{
	gint fd;
	guint watch_id;
	g_autoptr(GIOChannel) channel = NULL;

	/* open devlink netlink socket */
	self->nlg = fu_devlink_netlink_gen_socket_open(NULL, error);
	if (self->nlg == NULL)
		return FALSE;

	/* subscribe to devlink multicast notifications */
	if (!fu_devlink_netlink_mcast_group_subscribe(self->nlg, error))
		return FALSE;

	/* create GIOChannel for the netlink socket */
	fd = fu_devlink_netlink_gen_socket_get_fd(self->nlg);
	channel = g_io_channel_unix_new(fd);
	g_io_channel_set_encoding(channel, NULL, NULL);
	g_io_channel_set_buffered(channel, FALSE);

	/* setup monitoring source using proper GIO pattern */
	watch_id = g_io_add_watch(channel,
				  G_IO_IN | G_IO_ERR | G_IO_HUP,
				  fu_devlink_plugin_netlink_cb,
				  self);
	self->netlink_source = g_main_context_find_source_by_id(NULL, watch_id);
	if (self->netlink_source != NULL)
		g_source_ref(self->netlink_source);

	return TRUE;
}

/* device enumeration callback */
static gint
fu_devlink_plugin_enumerate_cb(const struct nlmsghdr *nlh, gpointer data)
{
	FuDevlinkPlugin *self = FU_DEVLINK_PLUGIN(data);
	struct genlmsghdr *genl;

	genl = mnl_nlmsg_get_payload(nlh);
	if (genl->cmd == DEVLINK_CMD_NEW)
		fu_devlink_plugin_device_added_from_netlink(self, nlh);

	return MNL_CB_OK;
}

static gboolean
fu_devlink_plugin_enumerate_devices(FuDevlinkPlugin *self, GError **error)
{
	struct nlmsghdr *nlh;

	/* prepare device enumeration command */
	nlh = fu_devlink_netlink_cmd_prepare(self->nlg, DEVLINK_CMD_GET, TRUE);

	/* send enumeration request and receive responses */
	if (!fu_devlink_netlink_msg_send_recv(self->nlg,
					      nlh,
					      fu_devlink_plugin_enumerate_cb,
					      self,
					      error)) {
		g_prefix_error_literal(error, "failed to enumerate devlink devices: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_devlink_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuDevlinkPlugin *self = FU_DEVLINK_PLUGIN(plugin);

	/* setup devlink netlink monitoring */
	if (!fu_devlink_plugin_setup_netlink(self, error)) {
		g_prefix_error_literal(error, "failed to setup devlink netlink: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_devlink_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuDevlinkPlugin *self = FU_DEVLINK_PLUGIN(plugin);

	/* enumerate existing devlink devices */
	if (!fu_devlink_plugin_enumerate_devices(self, error)) {
		g_prefix_error_literal(error, "failed to enumerate devlink devices: ");
		return FALSE;
	}

	return TRUE;
}

static void
fu_devlink_plugin_finalize(GObject *object)
{
	FuDevlinkPlugin *self = FU_DEVLINK_PLUGIN(object);

	/* clean up netlink resources */
	if (self->netlink_source != NULL) {
		g_source_destroy(self->netlink_source);
		g_source_unref(self->netlink_source);
	}

	fu_devlink_netlink_gen_socket_close(self->nlg);

	/* clean up backend */
	if (self->backend != NULL)
		g_object_unref(self->backend);

	G_OBJECT_CLASS(fu_devlink_plugin_parent_class)->finalize(object);
}

static void
fu_devlink_plugin_constructed(GObject *obj)
{
	FuDevlinkPlugin *self = FU_DEVLINK_PLUGIN(obj);
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);

	/* create and add devlink backend */
	self->backend = FU_DEVLINK_BACKEND(fu_devlink_backend_new(ctx));
	fu_context_add_backend(ctx, FU_BACKEND(self->backend));
	fu_context_add_quirk_key(ctx, "DevlinkFixedVersions");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DEVLINK_DEVICE);
}

static void
fu_devlink_plugin_init(FuDevlinkPlugin *self)
{
}

static void
fu_devlink_plugin_class_init(FuDevlinkPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_devlink_plugin_finalize;
	object_class->constructed = fu_devlink_plugin_constructed;
	plugin_class->startup = fu_devlink_plugin_startup;
	plugin_class->coldplug = fu_devlink_plugin_coldplug;
}
