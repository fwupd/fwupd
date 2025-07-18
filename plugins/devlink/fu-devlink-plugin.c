/*
 * Copyright 2025 NVIDIA Corporation & Affiliates
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <errno.h>
#include <gio/gio.h>
#include <sys/socket.h>
#include <unistd.h>

#include "fwupd.h"

#include "fu-devlink-backend.h"
#include "fu-devlink-device.h"
#include "fu-devlink-netlink.h"
#include "fu-devlink-plugin.h"

struct _FuDevlinkPlugin {
	FuPlugin parent_instance;
	FuDevlinkGenSocket nlg;
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
	g_autoptr(FuDevice) device = NULL;
	g_autofree gchar *physical_id = NULL;
	g_autoptr(GError) error = NULL;

	/* Parse netlink attributes using libmnl */
	mnl_attr_parse(nlh, sizeof(struct genlmsghdr), fu_devlink_netlink_attr_cb, tb);

	/* Extract bus and device names */
	if (tb[DEVLINK_ATTR_BUS_NAME])
		bus_name = mnl_attr_get_str(tb[DEVLINK_ATTR_BUS_NAME]);
	if (tb[DEVLINK_ATTR_DEV_NAME])
		dev_name = mnl_attr_get_str(tb[DEVLINK_ATTR_DEV_NAME]);

	if (!bus_name || !dev_name) {
		g_debug("devlink device notification missing bus_name or dev_name");
		return;
	}

	g_debug("devlink device added: %s/%s", bus_name, dev_name);

	/* Use backend to create device with proper hierarchy */
	device = fu_devlink_backend_device_added(self->backend, bus_name, dev_name, &error);
	if (device == NULL) {
		g_debug("failed to add devlink device %s/%s: %s",
			bus_name,
			dev_name,
			error->message);
		return;
	}

	/* Add device to plugin and cache for removal */
	physical_id = g_strdup_printf("%s/%s", bus_name, dev_name);
	fu_plugin_cache_add(FU_PLUGIN(self), physical_id, device);
	fu_plugin_device_add(FU_PLUGIN(self), device);
}

static void
fu_devlink_plugin_device_removed_from_netlink(FuDevlinkPlugin *self, const struct nlmsghdr *nlh)
{
	struct nlattr *tb[DEVLINK_ATTR_MAX + 1] = {};
	g_autofree gchar *physical_id = NULL;
	const gchar *bus_name = NULL;
	const gchar *dev_name = NULL;
	FuDevice *device = NULL;

	/* Parse netlink attributes using libmnl */
	mnl_attr_parse(nlh, sizeof(struct genlmsghdr), fu_devlink_netlink_attr_cb, tb);

	/* Extract bus and device names */
	if (tb[DEVLINK_ATTR_BUS_NAME])
		bus_name = mnl_attr_get_str(tb[DEVLINK_ATTR_BUS_NAME]);
	if (tb[DEVLINK_ATTR_DEV_NAME])
		dev_name = mnl_attr_get_str(tb[DEVLINK_ATTR_DEV_NAME]);

	if (!bus_name || !dev_name) {
		g_debug("devlink device removal notification missing bus_name or dev_name");
		return;
	}

	g_debug("devlink device removed: %s/%s", bus_name, dev_name);

	/* Find and remove device */
	physical_id = g_strdup_printf("%s/%s", bus_name, dev_name);
	device = fu_plugin_cache_lookup(FU_PLUGIN(self), physical_id);
	if (device) {
		fu_plugin_cache_remove(FU_PLUGIN(self), physical_id);
		fu_plugin_device_remove(FU_PLUGIN(self), device);
	}
}

/* Callback for processing individual netlink messages */
static int
fu_devlink_plugin_process_message_cb(const struct nlmsghdr *nlh, void *data)
{
	FuDevlinkPlugin *self = FU_DEVLINK_PLUGIN(data);
	struct genlmsghdr *genl;

	if (nlh->nlmsg_type == NLMSG_ERROR) {
		g_debug("devlink netlink error message");
		return MNL_CB_OK;
	}

	if (nlh->nlmsg_type != self->nlg.family_id)
		return MNL_CB_OK;

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
	const size_t buf_size = 8192;
	g_autofree gchar *buf = g_malloc0(buf_size);
	gsize len;
	GIOStatus status;
	GError *error = NULL;
	gint rc;

	if (condition & (G_IO_ERR | G_IO_HUP)) {
		g_debug("devlink netlink socket error");
		return FALSE;
	}

	/* Read netlink message via GIOChannel */
	status = g_io_channel_read_chars(channel, buf, buf_size, &len, &error);
	if (status != G_IO_STATUS_NORMAL) {
		if (error) {
			g_debug("failed to read devlink netlink message: %s", error->message);
			g_error_free(error);
		}
		return TRUE;
	}

	/* Process netlink messages using libmnl */
	rc = mnl_cb_run(buf, len, 0, 0, fu_devlink_plugin_process_message_cb, self);
	if (rc == -1)
		g_debug("failed to process netlink message: %s", fwupd_strerror(errno));

	return TRUE;
}

static gboolean
fu_devlink_plugin_setup_netlink(FuDevlinkPlugin *self, GError **error)
{
	GIOChannel *channel;
	gint fd;

	/* Open devlink netlink socket */
	if (!fu_devlink_netlink_gen_socket_open(&self->nlg, error))
		return FALSE;

	/* Subscribe to devlink multicast notifications */
	if (!fu_devlink_netlink_mcast_group_subscribe(&self->nlg)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to subscribe to devlink notifications");
		fu_devlink_netlink_gen_socket_close(&self->nlg);
		return FALSE;
	}

	g_debug("subscribed to devlink multicast notifications");

	/* Create GIOChannel for the netlink socket */
	fd = mnl_socket_get_fd(self->nlg.nl);
	channel = g_io_channel_unix_new(fd);
	g_io_channel_set_encoding(channel, NULL, NULL);
	g_io_channel_set_buffered(channel, FALSE);

	/* Setup monitoring source */
	self->netlink_source = g_io_create_watch(channel, G_IO_IN | G_IO_ERR | G_IO_HUP);
	g_source_set_callback(self->netlink_source,
			      (GSourceFunc)fu_devlink_plugin_netlink_cb,
			      self,
			      NULL);
	g_source_attach(self->netlink_source, NULL);

	g_io_channel_unref(channel);

	g_debug("devlink netlink monitoring setup completed");
	return TRUE;
}

/* Device enumeration callback */
static int
fu_devlink_plugin_enumerate_cb(const struct nlmsghdr *nlh, void *data)
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

	g_debug("enumerating devlink devices...");

	/* Prepare device enumeration command */
	nlh = fu_devlink_netlink_cmd_prepare(&self->nlg, DEVLINK_CMD_GET, TRUE);

	/* Send enumeration request and receive responses */
	if (!fu_devlink_netlink_msg_send_recv(&self->nlg,
					      nlh,
					      fu_devlink_plugin_enumerate_cb,
					      self,
					      error)) {
		g_prefix_error(error, "failed to enumerate devlink devices: ");
		return FALSE;
	}

	g_debug("devlink device enumeration completed");
	return TRUE;
}

static gboolean
fu_devlink_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuDevlinkPlugin *self = FU_DEVLINK_PLUGIN(plugin);

	g_debug("devlink plugin starting up...");

	/* Setup devlink netlink monitoring */
	if (!fu_devlink_plugin_setup_netlink(self, error)) {
		g_debug("failed to setup devlink netlink: %s",
			error ? (*error)->message : "unknown");
		return FALSE;
	}

	g_debug("devlink plugin startup completed successfully");
	return TRUE;
}

static gboolean
fu_devlink_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuDevlinkPlugin *self = FU_DEVLINK_PLUGIN(plugin);

	g_debug("devlink plugin coldplug starting...");

	/* Enumerate existing devlink devices */
	if (!fu_devlink_plugin_enumerate_devices(self, error)) {
		g_debug("failed to enumerate devlink devices: %s",
			error ? (*error)->message : "unknown");
		return FALSE;
	}

	g_debug("devlink plugin coldplug completed");
	return TRUE;
}

static void
fu_devlink_plugin_finalize(GObject *object)
{
	FuDevlinkPlugin *self = FU_DEVLINK_PLUGIN(object);

	/* Clean up netlink resources */
	if (self->netlink_source) {
		g_source_destroy(self->netlink_source);
		g_source_unref(self->netlink_source);
		self->netlink_source = NULL;
	}

	fu_devlink_netlink_gen_socket_close(&self->nlg);

	/* Clean up backend */
	if (self->backend) {
		g_object_unref(self->backend);
		self->backend = NULL;
	}

	G_OBJECT_CLASS(fu_devlink_plugin_parent_class)->finalize(object);
}

static void
fu_devlink_plugin_init(FuDevlinkPlugin *self)
{
	self->nlg.nl = NULL;
	self->nlg.buf = NULL;
	self->netlink_source = NULL;
}

static void
fu_devlink_plugin_constructed(GObject *obj)
{
	FuDevlinkPlugin *self = FU_DEVLINK_PLUGIN(obj);
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);

	/* Create and add devlink backend */
	self->backend = FU_DEVLINK_BACKEND(fu_devlink_backend_new(ctx));
	fu_context_add_backend(ctx, FU_BACKEND(self->backend));

	fu_plugin_add_device_gtype(plugin, FU_TYPE_DEVLINK_DEVICE);
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
