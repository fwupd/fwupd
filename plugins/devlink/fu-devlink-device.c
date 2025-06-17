/*
 * Copyright 2025 NVIDIA Corporation & Affiliates
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include <complex.h>
#include <glib/gstdio.h>
#include <linux/devlink.h>
#include <linux/netlink.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "fu-devlink-component.h"
#include "fu-devlink-device.h"
#include "fu-devlink-netlink.h"

struct _FuDevlinkDevice {
	FuDevice parent_instance;
	gchar *bus_name;
	gchar *dev_name;
	FuDevlinkGenSocket *nlg;
	FuKernelSearchPathLocker *search_path_locker;
};

G_DEFINE_TYPE(FuDevlinkDevice, fu_devlink_device, FU_TYPE_DEVICE)

/* flash status context for monitoring progress */
typedef struct {
	FuProgress *progress;
	FuDevlinkDevice *self;
} FuDevlinkFlashMonCtx;

/* flash send context for thread */
typedef struct {
	FuDevlinkDevice *self;
	const gchar *component_name;
	const gchar *filename;
	GError **error;
	GMainLoop *loop;
} FuDevlinkFlashSendCtx;

/* handle flash update status and end messages */
static int
fu_devlink_device_flash_mon_cb(const struct nlmsghdr *nlh, void *data)
{
	FuDevlinkFlashMonCtx *ctx = data;
	struct genlmsghdr *genl = mnl_nlmsg_get_payload(nlh);
	struct nlattr *tb[DEVLINK_ATTR_MAX + 1] = {};
	const gchar *bus_name, *dev_name;
	guint64 done = 0, total = 0;

	/* only handle flash update status and end messages */
	if (genl->cmd != DEVLINK_CMD_FLASH_UPDATE_STATUS &&
	    genl->cmd != DEVLINK_CMD_FLASH_UPDATE_END)
		return MNL_CB_OK;

	/* parse message attributes */
	mnl_attr_parse(nlh, sizeof(*genl), fu_devlink_netlink_attr_cb, tb);

	/* verify this is for our device */
	if (tb[DEVLINK_ATTR_BUS_NAME] == NULL || tb[DEVLINK_ATTR_DEV_NAME] == NULL)
		return MNL_CB_OK;

	bus_name = mnl_attr_get_str(tb[DEVLINK_ATTR_BUS_NAME]);
	dev_name = mnl_attr_get_str(tb[DEVLINK_ATTR_DEV_NAME]);

	if (g_strcmp0(bus_name, ctx->self->bus_name) || g_strcmp0(dev_name, ctx->self->dev_name))
		return MNL_CB_OK;

	if (genl->cmd == DEVLINK_CMD_FLASH_UPDATE_END) {
		fu_progress_set_percentage(ctx->progress, 100);
		return MNL_CB_STOP;
	}

	/* extract progress information from status message */
	if (tb[DEVLINK_ATTR_FLASH_UPDATE_STATUS_DONE] != NULL)
		done = mnl_attr_get_u64(tb[DEVLINK_ATTR_FLASH_UPDATE_STATUS_DONE]);
	if (tb[DEVLINK_ATTR_FLASH_UPDATE_STATUS_TOTAL] != NULL)
		total = mnl_attr_get_u64(tb[DEVLINK_ATTR_FLASH_UPDATE_STATUS_TOTAL]);

	if (total > 0)
		fu_progress_set_percentage_full(ctx->progress, done, total);

	return MNL_CB_OK;
}

/* send flash command */
static gboolean
fu_devlink_device_flash_send(FuDevlinkDevice *self,
			     const gchar *component_name,
			     const gchar *filename,
			     GError **error)
{
	struct nlmsghdr *nlh;

	/* prepare flash update command */
	nlh = fu_devlink_netlink_cmd_prepare(self->nlg, DEVLINK_CMD_FLASH_UPDATE, FALSE);

	mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, self->bus_name);
	mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, self->dev_name);

	if (component_name != NULL) {
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_FLASH_UPDATE_COMPONENT, component_name);
		g_debug("sending flash update command for %s/%s with component %s and file %s",
			self->bus_name,
			self->dev_name,
			component_name,
			filename);
	} else {
		g_debug("sending flash update command for %s/%s with file %s",
			self->bus_name,
			self->dev_name,
			filename);
	}

	mnl_attr_put_strz(nlh, DEVLINK_ATTR_FLASH_UPDATE_FILE_NAME, filename);

	/* send flash update command - this will block until completion */
	if (!fu_devlink_netlink_msg_send(self->nlg, nlh, error))
		return FALSE;
	g_debug("flash update command completed successfully");

	return TRUE;
}

/* thread function that sends the flash update command and quits main loop */
static gpointer
fu_devlink_device_flash_send_thread(gpointer user_data)
{
	FuDevlinkFlashSendCtx *ctx = user_data;
	gboolean ret;

	g_debug("flash send thread started for %s/%s", ctx->self->bus_name, ctx->self->dev_name);
	ret =
	    fu_devlink_device_flash_send(ctx->self, ctx->component_name, ctx->filename, ctx->error);

	/* signal completion by quitting the main loop  */
	g_main_loop_quit(ctx->loop);

	return GINT_TO_POINTER(ret ? 1 : 0);
}

/* netlink callback for flash progress monitoring */
static gboolean
fu_devlink_device_flash_mon_netlink_cb(GIOChannel *channel,
				       GIOCondition condition,
				       gpointer user_data)
{
	FuDevlinkFlashMonCtx *ctx = user_data;
	gsize len;
	GIOStatus status;
	g_autoptr(GError) error = NULL;

	if (condition & (G_IO_ERR | G_IO_HUP)) {
		g_debug("devlink netlink socket error during flash monitoring");
		return FALSE;
	}

	/* read netlink message via GIOChannel */
	status = g_io_channel_read_chars(channel,
					 fu_devlink_netlink_gen_socket_get_buf(ctx->self->nlg),
					 FU_DEVLINK_NETLINK_BUF_SIZE,
					 &len,
					 &error);
	if (status != G_IO_STATUS_NORMAL) {
		if (error != NULL)
			g_debug("failed to read devlink netlink message: %s", error->message);
		return TRUE;
	}

	/* process netlink messages */
	if (!fu_devlink_netlink_msg_run(ctx->self->nlg,
					len,
					0,
					fu_devlink_device_flash_mon_cb,
					ctx,
					&error))
		g_warning("failed to process netlink message: %s", error->message);

	return TRUE;
}

static gboolean
fu_devlink_device_flash(FuDevlinkDevice *self,
			const gchar *component_name,
			const gchar *filename,
			FuProgress *progress,
			GError **error)
{
	FuDevlinkFlashMonCtx flash_mon_ctx = {
	    .progress = progress,
	    .self = self,
	};
	g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
	FuDevlinkFlashSendCtx flash_send_ctx = {
	    .self = self,
	    .component_name = component_name,
	    .filename = filename,
	    .loop = loop,
	};
	guint watch_id;
	gint fd;
	gpointer thread_result;
	gboolean ret;
	g_autoptr(FuDevlinkGenSocket) nlg = NULL;
	g_autoptr(GIOChannel) channel = NULL;
	g_autoptr(GThread) flash_send_thread = NULL;

	/* open netlink socket and subscribe to multicast */
	nlg = fu_devlink_netlink_gen_socket_open(error);
	if (nlg == NULL)
		return FALSE;
	if (!fu_devlink_netlink_mcast_group_subscribe(nlg)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to subscribe to multicast group");
		return FALSE;
	}

	/* setup netlink channel and watch */
	fd = fu_devlink_netlink_gen_socket_get_fd(nlg);
	channel = g_io_channel_unix_new(fd);
	g_io_channel_set_encoding(channel, NULL, NULL);
	g_io_channel_set_buffered(channel, FALSE);
	watch_id = g_io_add_watch(channel,
				  G_IO_IN | G_IO_ERR | G_IO_HUP,
				  fu_devlink_device_flash_mon_netlink_cb,
				  &flash_mon_ctx);

	fu_progress_set_percentage(progress, 0);

	/* start the flash send thread */
	flash_send_thread = g_thread_new("devlink-flash-send",
					 fu_devlink_device_flash_send_thread,
					 &flash_send_ctx);
	if (flash_send_thread == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to create flash send thread");
		g_source_remove(watch_id);
		return FALSE;
	}

	/* run the main loop until completion */
	g_main_loop_run(loop);

	g_source_remove(watch_id);

	/* get thread result */
	thread_result = g_thread_join(flash_send_thread);
	g_steal_pointer(&flash_send_thread);
	ret = GPOINTER_TO_INT(thread_result) != 0;
	if (ret)
		fu_progress_set_percentage(progress, 100);

	return ret;
}

gboolean
fu_devlink_device_write_firmware_component(FuDevlinkDevice *self,
					   const gchar *component_name,
					   FuFirmware *firmware,
					   FuProgress *progress,
					   FwupdInstallFlags flags,
					   GError **error)
{
	gboolean ret;
	const gchar *fw_search_path;
	g_autofree gchar *fw_basename = NULL;
	g_autofree gchar *fw_fullpath = NULL;
	g_autoptr(GBytes) fw = NULL;

	/* get firmware data */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* get the firmware search path from the locker */
	fw_search_path = fu_kernel_search_path_locker_get_path(self->search_path_locker);

	fw_basename = g_strdup_printf("%s-%s-%s.bin",
				      self->bus_name,
				      self->dev_name,
				      component_name != NULL ? component_name : "default");

	/* create firmware file in the kernel search path for devlink */
	fw_fullpath = g_build_filename(fw_search_path, fw_basename, NULL);

	/* write firmware to kernel search path */
	if (!fu_bytes_set_contents(fw_fullpath, fw, error))
		return FALSE;

	g_debug("wrote firmware to %s", fw_fullpath);

	ret = fu_devlink_device_flash(self, component_name, fw_basename, progress, error);

	g_unlink(fw_fullpath);

	return ret;
}

typedef struct {
	const gchar *fixed;
	const gchar *running;
	const gchar *stored;
} FuDevlinkVersionInfo;

static void
fu_devlink_device_version_info_free(gpointer value)
{
	FuDevlinkVersionInfo *version_info = value;

	g_free((gpointer)version_info->fixed);
	g_free((gpointer)version_info->running);
	g_free((gpointer)version_info->stored);
	g_free(version_info);
}

static GHashTable *
fu_devlink_device_get_version_table(const struct nlmsghdr *nlh)
{
	GHashTable *version_table = g_hash_table_new_full(g_str_hash,
							  g_str_equal,
							  g_free,
							  fu_devlink_device_version_info_free);
	FuDevlinkVersionInfo *version_info;
	struct nlattr *attr;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr))
	{
		struct nlattr *ver_tb[DEVLINK_ATTR_MAX + 1] = {};
		const gchar *name, *value;

		if (mnl_attr_get_type(attr) != DEVLINK_ATTR_INFO_VERSION_FIXED &&
		    mnl_attr_get_type(attr) != DEVLINK_ATTR_INFO_VERSION_RUNNING &&
		    mnl_attr_get_type(attr) != DEVLINK_ATTR_INFO_VERSION_STORED)
			continue;

		if (mnl_attr_parse_nested(attr, fu_devlink_netlink_attr_cb, ver_tb) != MNL_CB_OK)
			continue;

		if (ver_tb[DEVLINK_ATTR_INFO_VERSION_NAME] == NULL ||
		    ver_tb[DEVLINK_ATTR_INFO_VERSION_VALUE] == NULL)
			continue;

		name = mnl_attr_get_str(ver_tb[DEVLINK_ATTR_INFO_VERSION_NAME]);
		value = mnl_attr_get_str(ver_tb[DEVLINK_ATTR_INFO_VERSION_VALUE]);

		version_info = g_hash_table_lookup(version_table, name);
		if (version_info == NULL) {
			version_info = g_new0(FuDevlinkVersionInfo, 1);
			g_hash_table_insert(version_table, g_strdup(name), version_info);
		}

		switch (mnl_attr_get_type(attr)) {
		case DEVLINK_ATTR_INFO_VERSION_FIXED:
			version_info->fixed = g_strdup(value);
			break;
		case DEVLINK_ATTR_INFO_VERSION_RUNNING:
			version_info->running = g_strdup(value);
			break;
		case DEVLINK_ATTR_INFO_VERSION_STORED:
			version_info->stored = g_strdup(value);
			break;
		default:
			continue;
		}
	}

	return version_table;
}

#define FU_DEVLINK_DEVICE_INSTANCE_ID_PREFIX "DEVLINK\\"

static void
fu_devlink_device_instance_id_cb(gpointer key, gpointer value, gpointer user_data)
{
	FuDevlinkVersionInfo *version_info = value;
	GString *instance_id = user_data;
	gchar *name = key;

	if (version_info->fixed == NULL)
		return;
	if (g_strcmp0(instance_id->str, FU_DEVLINK_DEVICE_INSTANCE_ID_PREFIX))
		g_string_append_printf(instance_id, "&");
	g_string_append_printf(instance_id, "%s_%s", name, version_info->fixed);
}

static FuDevice *
fu_devlink_device_get_component(FuDevice *device, const gchar *name)
{
	GPtrArray *children = fu_device_get_children(device);

	for (guint i = 0; i < children->len; i++) {
		FuDevice *component = g_ptr_array_index(children, i);

		if (!g_strcmp0(fu_device_get_name(component), name))
			return g_object_ref(component);
	}
	return NULL;
}

typedef struct {
	FuDevice *device;
	GString *instance_id;
} FuDevlinkDeviceUpdateComponentCtx;

static void
fu_devlink_device_update_component_cb(gpointer key, gpointer value, gpointer user_data)
{
	FuDevlinkDeviceUpdateComponentCtx *ctx = user_data;
	FuDevlinkVersionInfo *version_info = value;
	const gchar *name = key;
	const gchar *version;
	g_autoptr(FuDevice) component = NULL;

	if (version_info->stored != NULL)
		version = version_info->stored;
	else if (version_info->running != NULL)
		version = version_info->running;
	else
		return;

	component = fu_devlink_device_get_component(ctx->device, name);
	if (component == NULL) {
		g_autofree gchar *component_instance_id =
		    g_strdup_printf("%s&COMPONENT_%s", ctx->instance_id->str, name);
		component = fu_devlink_component_new(fu_device_get_context(ctx->device),
						     component_instance_id,
						     name);
		fu_device_set_version(component, version);
		fu_device_add_child(ctx->device, component);
		g_debug("added component %s (version: %s)", name, version);
	} else {
		fu_device_set_version(component, version);
		g_debug("updated component %s (version: %s)", name, version);
	}

	if (version_info->stored != NULL && version_info->running != NULL) {
		if (g_strcmp0(version_info->stored, version_info->running))
			fu_device_add_flag(component, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
		else
			fu_device_remove_flag(component, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
	}
}

/* callback for parsing devlink dev info response */
static int
fu_devlink_device_info_cb(const struct nlmsghdr *nlh, void *data)
{
	struct genlmsghdr *genl = mnl_nlmsg_get_payload(nlh);
	struct nlattr *tb[DEVLINK_ATTR_MAX + 1] = {};
	FuDevice *device = FU_DEVICE(data);
	GPtrArray *children = fu_device_get_children(device);
	FuDevlinkDeviceUpdateComponentCtx ctx;
	g_autoptr(GString) instance_id = g_string_new(FU_DEVLINK_DEVICE_INSTANCE_ID_PREFIX);
	g_autoptr(GHashTable) version_table = NULL;

	if (genl->cmd != DEVLINK_CMD_INFO_GET)
		return MNL_CB_OK;

	/* parse main attributes */
	mnl_attr_parse(nlh, sizeof(*genl), fu_devlink_netlink_attr_cb, tb);

	/* parse driver name */
	if (tb[DEVLINK_ATTR_INFO_DRIVER_NAME] != NULL) {
		const gchar *driver_name = mnl_attr_get_str(tb[DEVLINK_ATTR_INFO_DRIVER_NAME]);
		g_debug("device driver name: %s", driver_name);
		g_string_append_printf(instance_id, "DRIVER_%s", driver_name);
	}

	if (tb[DEVLINK_ATTR_INFO_SERIAL_NUMBER] != NULL) {
		const gchar *serial_number = mnl_attr_get_str(tb[DEVLINK_ATTR_INFO_SERIAL_NUMBER]);

		g_debug("device serial number: %s", serial_number);
		fu_device_set_serial(device, serial_number);
	}

	version_table = fu_devlink_device_get_version_table(nlh);

	/* append fixed versions to instance id */
	g_hash_table_foreach(version_table, fu_devlink_device_instance_id_cb, instance_id);

	if (!g_strcmp0(instance_id->str, FU_DEVLINK_DEVICE_INSTANCE_ID_PREFIX)) {
		g_warning("no instance id items found, ignoring component creation");
		return MNL_CB_OK;
	}

	/* remove components that are not in the version table */
	for (guint i = 0; i < children->len; i++) {
		FuDevice *component = g_ptr_array_index(children, children->len - i - 1);
		const gchar *name = fu_device_get_name(component);
		FuDevlinkVersionInfo *version_info = g_hash_table_lookup(version_table, name);

		if (version_info == NULL ||
		    (version_info->stored == NULL && version_info->running == NULL)) {
			g_debug("removed component %s", name);
			fu_device_remove_child(device, component);
		}
	}

	ctx.device = device;
	ctx.instance_id = instance_id;
	g_hash_table_foreach(version_table, fu_devlink_device_update_component_cb, &ctx);

	return MNL_CB_OK;
}

/* get device information using devlink dev info */
static gboolean
fu_devlink_device_get_info(FuDevice *device, GError **error)
{
	FuDevlinkDevice *self = FU_DEVLINK_DEVICE(device);
	struct nlmsghdr *nlh;

	/* prepare dev info command */
	nlh = fu_devlink_netlink_cmd_prepare(self->nlg, DEVLINK_CMD_INFO_GET, FALSE);
	mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, self->bus_name);
	mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, self->dev_name);

	g_debug("getting device info for %s/%s", self->bus_name, self->dev_name);

	/* send command and process response */
	if (!fu_devlink_netlink_msg_send_recv(self->nlg,
					      nlh,
					      fu_devlink_device_info_cb,
					      device,
					      error)) {
		g_prefix_error(error, "failed to get device info: ");
		return FALSE;
	}

	return TRUE;
}

static void
fu_devlink_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuDevlinkDevice *self = FU_DEVLINK_DEVICE(device);

	fwupd_codec_string_append(str, idt, "BusName", self->bus_name);
	fwupd_codec_string_append(str, idt, "DevName", self->dev_name);
}

static gboolean
fu_devlink_device_setup(FuDevice *device, GError **error)
{
	/* get device information and version */
	return fu_devlink_device_get_info(device, error);
}

/* perform firmware activation using devlink reload with fw_activate action */
static gboolean
fu_devlink_device_activate(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevlinkDevice *self = FU_DEVLINK_DEVICE(device);
	struct nlmsghdr *nlh;

	g_debug("activating firmware for %s/%s", self->bus_name, self->dev_name);

	/* prepare reload command with fw_activate action */
	nlh = fu_devlink_netlink_cmd_prepare(self->nlg, DEVLINK_CMD_RELOAD, FALSE);
	mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, self->bus_name);
	mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, self->dev_name);
	mnl_attr_put_u8(nlh, DEVLINK_ATTR_RELOAD_ACTION, DEVLINK_RELOAD_ACTION_FW_ACTIVATE);

	g_debug("sending devlink reload command with fw_activate action for %s/%s",
		self->bus_name,
		self->dev_name);

	/* send command and wait for response */
	if (!fu_devlink_netlink_msg_send(self->nlg, nlh, error)) {
		g_prefix_error(error, "failed to send devlink reload command: ");
		return FALSE;
	}

	g_debug("firmware activation completed for %s/%s", self->bus_name, self->dev_name);
	return TRUE;
}

/**
 * fu_devlink_device_new:
 * @ctx: (nullable): optional #FuContext
 * @bus_name: devlink bus name
 * @dev_name: devlink device name
 *
 * Creates a new #FuDevlinkDevice.
 *
 * Returns: (transfer full): a #FuDevlinkDevice
 */
FuDevlinkDevice *
fu_devlink_device_new(FuContext *ctx, const gchar *bus_name, const gchar *dev_name)
{
	g_autoptr(FuDevlinkDevice) self = NULL;
	g_autofree gchar *device_id = NULL;

	g_return_val_if_fail(bus_name, NULL);
	g_return_val_if_fail(dev_name, NULL);

	/* create object and assign the strings */
	self = g_object_new(FU_TYPE_DEVLINK_DEVICE, "context", ctx, NULL);
	self->bus_name = g_strdup(bus_name);
	self->dev_name = g_strdup(dev_name);

	device_id = g_strdup_printf("%s/%s", bus_name, dev_name);
	fu_device_set_physical_id(FU_DEVICE(self), device_id);
	fu_device_set_name(FU_DEVICE(self), device_id);

	return g_steal_pointer(&self);
}

static gboolean
fu_devlink_device_open(FuDevice *device, GError **error)
{
	FuDevlinkDevice *self = FU_DEVLINK_DEVICE(device);

	/* open devlink netlink socket */
	self->nlg = fu_devlink_netlink_gen_socket_open(error);
	if (self->nlg == NULL)
		return FALSE;

	return TRUE;
}

static gboolean
fu_devlink_device_close(FuDevice *device, GError **error)
{
	FuDevlinkDevice *self = FU_DEVLINK_DEVICE(device);

	/* close devlink netlink socket */
	fu_devlink_netlink_gen_socket_close(self->nlg);

	return TRUE;
}

static FuKernelSearchPathLocker *
fu_devlink_device_search_path_locker_new(FuDevlinkDevice *self, GError **error)
{
	g_autofree gchar *cachedir = NULL;
	g_autofree gchar *devlink_fw_dir = NULL;
	g_autoptr(FuKernelSearchPathLocker) locker = NULL;

	/* create a directory to store firmware files for devlink plugin */
	cachedir = fu_path_from_kind(FU_PATH_KIND_CACHEDIR_PKG);
	devlink_fw_dir = g_build_filename(cachedir, "devlink", "firmware", NULL);
	if (g_mkdir_with_parents(devlink_fw_dir, 0700) == -1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to create '%s': %s",
			    devlink_fw_dir,
			    fwupd_strerror(errno));
		return NULL;
	}
	locker = fu_kernel_search_path_locker_new(devlink_fw_dir, error);
	if (locker == NULL)
		return NULL;
	return g_steal_pointer(&locker);
}

static gboolean
fu_devlink_device_prepare(FuDevice *device,
			  FuProgress *progress,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuDevlinkDevice *self = FU_DEVLINK_DEVICE(device);

	/* setup kernel firmware search path for devlink device */
	self->search_path_locker = fu_devlink_device_search_path_locker_new(self, error);
	if (self->search_path_locker == NULL)
		return FALSE;

	return TRUE;
}

static gboolean
fu_devlink_device_cleanup(FuDevice *device,
			  FuProgress *progress,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuDevlinkDevice *self = FU_DEVLINK_DEVICE(device);

	/* restore the firmware search path */
	g_clear_object(&self->search_path_locker);

	return TRUE;
}

static void
fu_devlink_device_finalize(GObject *object)
{
	FuDevlinkDevice *self = FU_DEVLINK_DEVICE(object);

	g_free(self->bus_name);
	g_free(self->dev_name);

	G_OBJECT_CLASS(fu_devlink_device_parent_class)->finalize(object);
}

static void
fu_devlink_device_init(FuDevlinkDevice *self)
{
	fu_device_set_summary(FU_DEVICE(self), "Devlink device");
	fu_device_add_protocol(FU_DEVICE(self), "org.kernel.devlink");
}

static void
fu_devlink_device_class_init(FuDevlinkDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	object_class->finalize = fu_devlink_device_finalize;
	device_class->open = fu_devlink_device_open;
	device_class->close = fu_devlink_device_close;
	device_class->prepare = fu_devlink_device_prepare;
	device_class->cleanup = fu_devlink_device_cleanup;
	device_class->to_string = fu_devlink_device_to_string;
	device_class->setup = fu_devlink_device_setup;
	device_class->reload = fu_devlink_device_setup;
	device_class->activate = fu_devlink_device_activate;
}
