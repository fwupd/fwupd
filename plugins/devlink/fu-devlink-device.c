/*
 * Copyright 2025 NVIDIA Corporation & Affiliates
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <complex.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <linux/devlink.h>
#include <linux/netlink.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "fwupd.h"

#include "fu-devlink-component.h"
#include "fu-devlink-device.h"
#include "fu-devlink-netlink.h"
#include "glibconfig.h"

/**
 * FU_DEVLINK_DEVICE_FLAG_OMIT_COMPONENT_NAME:
 *
 * Do not set the DEVLINK_ATTR_FLASH_UPDATE_COMPONENT attribute when flashing firmware.
 * This allows for firmware updates without specifying a specific component name.
 *
 * Since: 2.0.4
 */
#define FU_DEVLINK_DEVICE_FLAG_OMIT_COMPONENT_NAME "omit-component-name"

struct _FuDevlinkDevice {
	FuDevice parent_instance;
	gchar *bus_name;
	gchar *dev_name;
};

G_DEFINE_TYPE(FuDevlinkDevice, fu_devlink_device, FU_TYPE_DEVICE)

/* Flash status context for monitoring progress */
typedef struct {
	FuProgress *progress;
	FuDevlinkDevice *self;
} FuDevlinkFlashMonCtx;

/* Handle flash update status and end messages */
static int
fu_devlink_device_flash_mon_cb(const struct nlmsghdr *nlh, void *data)
{
	FuDevlinkFlashMonCtx *ctx = data;
	struct genlmsghdr *genl = mnl_nlmsg_get_payload(nlh);
	struct nlattr *tb[DEVLINK_ATTR_MAX + 1] = {};
	const gchar *bus_name, *dev_name;
	const gchar *status_msg = NULL;
	guint64 done = 0, total = 0;

	/* Only handle flash update status and end messages */
	if (genl->cmd != DEVLINK_CMD_FLASH_UPDATE_STATUS &&
	    genl->cmd != DEVLINK_CMD_FLASH_UPDATE_END)
		return MNL_CB_OK;

	/* Parse message attributes */
	mnl_attr_parse(nlh, sizeof(*genl), fu_devlink_netlink_attr_cb, tb);

	/* Verify this is for our device */
	if (!tb[DEVLINK_ATTR_BUS_NAME] || !tb[DEVLINK_ATTR_DEV_NAME])
		return MNL_CB_OK;

	bus_name = mnl_attr_get_str(tb[DEVLINK_ATTR_BUS_NAME]);
	dev_name = mnl_attr_get_str(tb[DEVLINK_ATTR_DEV_NAME]);

	if (g_strcmp0(bus_name, ctx->self->bus_name) || g_strcmp0(dev_name, ctx->self->dev_name))
		return MNL_CB_OK;

	if (genl->cmd == DEVLINK_CMD_FLASH_UPDATE_END) {
		g_debug("received flash update end notification");
		fu_progress_set_percentage(ctx->progress, 100);
		return MNL_CB_STOP;
	}

	/* Extract progress information from status message */
	if (tb[DEVLINK_ATTR_FLASH_UPDATE_STATUS_DONE])
		done = mnl_attr_get_u64(tb[DEVLINK_ATTR_FLASH_UPDATE_STATUS_DONE]);
	if (tb[DEVLINK_ATTR_FLASH_UPDATE_STATUS_TOTAL])
		total = mnl_attr_get_u64(tb[DEVLINK_ATTR_FLASH_UPDATE_STATUS_TOTAL]);
	if (tb[DEVLINK_ATTR_FLASH_UPDATE_STATUS_MSG])
		status_msg = mnl_attr_get_str(tb[DEVLINK_ATTR_FLASH_UPDATE_STATUS_MSG]);

	if (total > 0) {
		guint percentage = (done * 100) / total;

		fu_progress_set_percentage(ctx->progress, percentage);
		g_debug("flash progress: %u%% (%lu/%lu bytes)", percentage, done, total);
	}

	if (status_msg)
		g_debug("flash status: %s", status_msg);

	return MNL_CB_OK;
}
/* Thread function that monitors the flash update progress */
static gpointer
fu_devlink_device_flash_mon_thread(gpointer user_data)
{
	FuDevlinkFlashMonCtx *ctx = user_data;
	g_auto(FuDevlinkGenSocket) nlg = {};

	g_debug("flash mon thread started for %s/%s", ctx->self->bus_name, ctx->self->dev_name);
	if (!fu_devlink_netlink_gen_socket_open(&nlg, NULL))
		return NULL;
	if (!fu_devlink_netlink_mcast_group_subscribe(&nlg))
		return NULL;
	if (!fu_devlink_netlink_msg_recv_run(&nlg, 0, fu_devlink_device_flash_mon_cb, ctx, NULL))
		return NULL;
	return GINT_TO_POINTER(1);
}

/* Send flash command */
static gboolean
fu_devlink_device_flash_send(FuDevlinkDevice *self,
			     const gchar *component_name,
			     const gchar *filename,
			     GError **error)
{
	g_auto(FuDevlinkGenSocket) nlg = {};
	struct nlmsghdr *nlh;

	/* Open local netlink socket for flash command */
	if (!fu_devlink_netlink_gen_socket_open(&nlg, error))
		return FALSE;

	/* Prepare flash update command */
	nlh = fu_devlink_netlink_cmd_prepare(&nlg, DEVLINK_CMD_FLASH_UPDATE, FALSE);

	mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, self->bus_name);
	mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, self->dev_name);

	if (component_name) {
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

	/* Send flash update command - this will block until completion */
	if (!fu_devlink_netlink_msg_send(&nlg, nlh, error))
		return FALSE;
	g_debug("flash update command completed successfully");

	return TRUE;
}

static gboolean
fu_devlink_device_flash(FuDevlinkDevice *self,
			const gchar *component_name,
			const gchar *filename,
			FuProgress *progress,
			GError **error)
{
	g_autoptr(GThread) flash_mon_thread = NULL;
	FuDevlinkFlashMonCtx flash_mon_ctx = {
	    .progress = progress,
	    .self = self,
	};
	gboolean ret;

	fu_progress_set_percentage(progress, 0);

	/* Start the flash monitor thread */
	flash_mon_thread =
	    g_thread_new("devlink-flash-mon", fu_devlink_device_flash_mon_thread, &flash_mon_ctx);
	if (!flash_mon_thread) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to create flash monitor thread");
		return FALSE;
	}

	/* Send the flash command */
	ret = fu_devlink_device_flash_send(self, component_name, filename, error);

	/* Wait for the monitoring thread to complete and clean up its socket */
	g_thread_join(flash_mon_thread);

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
	g_autofree gchar *fw_basename = NULL;
	g_autofree gchar *fw_fullpath = NULL;
	g_autoptr(GBytes) fw = NULL;
	const guchar *fw_data;
	gsize fw_size;
	gboolean ret;

	/* Get firmware data */
	fw = fu_firmware_get_bytes(firmware, error);
	if (!fw)
		return FALSE;

	fw_basename = g_strdup_printf("fwupd-devlink-%s-%s-%s.bin",
				      self->bus_name,
				      self->dev_name,
				      component_name ? component_name : "default");

	/* Create firmware file in /lib/firmware/ for devlink */
	fw_fullpath = g_build_filename("/lib/firmware", fw_basename, NULL);

	fw_data = g_bytes_get_data(fw, NULL);
	fw_size = g_bytes_get_size(fw);

	/* Write firmware to /lib/firmware/ */
	if (!g_file_set_contents(fw_fullpath, (const gchar *)fw_data, fw_size, error))
		return FALSE;

	g_debug("wrote %lu bytes of firmware to %s", fw_size, fw_fullpath);

	ret = fu_devlink_device_flash(self, component_name, fw_basename, progress, error);

	g_unlink(fw_fullpath);

	return ret;
}

typedef struct {
	const gchar *fixed;
	const gchar *running;
	const gchar *stored;
} FuDevlinkVersionInfo;

static GHashTable *
fu_devlink_device_get_version_table(const struct nlmsghdr *nlh)
{
	GHashTable *version_table = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free);
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

		if (!ver_tb[DEVLINK_ATTR_INFO_VERSION_NAME] ||
		    !ver_tb[DEVLINK_ATTR_INFO_VERSION_VALUE])
			continue;

		name = mnl_attr_get_str(ver_tb[DEVLINK_ATTR_INFO_VERSION_NAME]);
		value = mnl_attr_get_str(ver_tb[DEVLINK_ATTR_INFO_VERSION_VALUE]);

		version_info = g_hash_table_lookup(version_table, name);
		if (!version_info) {
			version_info = g_new0(FuDevlinkVersionInfo, 1);
			g_hash_table_insert(version_table, (void *)name, version_info);
		}

		switch (mnl_attr_get_type(attr)) {
		case DEVLINK_ATTR_INFO_VERSION_FIXED:
			version_info->fixed = value;
			break;
		case DEVLINK_ATTR_INFO_VERSION_RUNNING:
			version_info->running = value;
			break;
		case DEVLINK_ATTR_INFO_VERSION_STORED:
			version_info->stored = value;
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

	if (!version_info->fixed)
		return;
	if (g_strcmp0(instance_id->str, FU_DEVLINK_DEVICE_INSTANCE_ID_PREFIX))
		g_string_append_printf(instance_id, "&");
	g_string_append_printf(instance_id, "%s_%s", name, version_info->fixed);
}

static FuDevice *
fu_devlink_device_get_component(FuDevice *device, const gchar *name)
{
	GPtrArray *children = fu_device_get_children(device);

	for (gint i = children->len - 1; i >= 0; i--) {
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

	if (version_info->stored)
		version = version_info->stored;
	else if (version_info->running)
		version = version_info->running;
	else
		return;

	component = fu_devlink_device_get_component(ctx->device, name);
	if (!component) {
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

	if (version_info->stored && version_info->running) {
		if (g_strcmp0(version_info->stored, version_info->running))
			fu_device_add_flag(component, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
		else
			fu_device_remove_flag(component, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
	}
}

/* Callback for parsing devlink dev info response */
static int
fu_devlink_device_info_cb(const struct nlmsghdr *nlh, void *data)
{
	g_autoptr(GString) instance_id = g_string_new(FU_DEVLINK_DEVICE_INSTANCE_ID_PREFIX);
	struct genlmsghdr *genl = mnl_nlmsg_get_payload(nlh);
	struct nlattr *tb[DEVLINK_ATTR_MAX + 1] = {};
	FuDevice *device = FU_DEVICE(data);
	GPtrArray *children = fu_device_get_children(device);
	g_autoptr(GHashTable) version_table = NULL;
	FuDevlinkDeviceUpdateComponentCtx ctx;

	if (genl->cmd != DEVLINK_CMD_INFO_GET)
		return MNL_CB_OK;

	/* Parse main attributes */
	mnl_attr_parse(nlh, sizeof(*genl), fu_devlink_netlink_attr_cb, tb);

	/* Parse driver name */
	if (tb[DEVLINK_ATTR_INFO_DRIVER_NAME]) {
		const gchar *driver_name = mnl_attr_get_str(tb[DEVLINK_ATTR_INFO_DRIVER_NAME]);
		g_debug("device driver name: %s", driver_name);
		g_string_append_printf(instance_id, "DRIVER_%s", driver_name);
	}

	if (tb[DEVLINK_ATTR_INFO_SERIAL_NUMBER]) {
		const gchar *serial_number = mnl_attr_get_str(tb[DEVLINK_ATTR_INFO_SERIAL_NUMBER]);

		g_debug("device serial number: %s", serial_number);
		fu_device_set_serial(device, serial_number);
	}

	version_table = fu_devlink_device_get_version_table(nlh);

	/* Append fixed versions to instance id */
	g_hash_table_foreach(version_table, fu_devlink_device_instance_id_cb, instance_id);

	if (!g_strcmp0(instance_id->str, FU_DEVLINK_DEVICE_INSTANCE_ID_PREFIX)) {
		g_warning("no instance id items found, ignoring component creation");
		return MNL_CB_OK;
	}

	/* Remove components that are not in the version table */
	for (gint i = children->len - 1; i >= 0; i--) {
		FuDevice *component = g_ptr_array_index(children, i);
		const gchar *name = fu_device_get_name(component);
		FuDevlinkVersionInfo *version_info = g_hash_table_lookup(version_table, name);

		if (!version_info || (!version_info->stored && !version_info->running)) {
			g_debug("removed component %s", name);
			fu_device_remove_child(device, component);
		}
	}

	ctx.device = device;
	ctx.instance_id = instance_id;
	g_hash_table_foreach(version_table, fu_devlink_device_update_component_cb, &ctx);

	return MNL_CB_OK;
}

/* Get device information using devlink dev info */
static gboolean
fu_devlink_device_get_info(FuDevice *device, GError **error)
{
	FuDevlinkDevice *self = FU_DEVLINK_DEVICE(device);
	g_auto(FuDevlinkGenSocket) nlg = {};
	struct nlmsghdr *nlh;

	/* Open netlink socket */
	if (!fu_devlink_netlink_gen_socket_open(&nlg, error))
		return FALSE;

	/* Prepare dev info command */
	nlh = fu_devlink_netlink_cmd_prepare(&nlg, DEVLINK_CMD_INFO_GET, FALSE);
	mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, self->bus_name);
	mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, self->dev_name);

	g_debug("getting device info for %s/%s", self->bus_name, self->dev_name);

	/* Send command and process response */
	if (!fu_devlink_netlink_msg_send_recv(&nlg,
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
	/* Get device information and version */
	return fu_devlink_device_get_info(device, error);
}

/* Perform firmware activation using devlink reload with fw_activate action */
static gboolean
fu_devlink_device_activate(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevlinkDevice *self = FU_DEVLINK_DEVICE(device);
	g_auto(FuDevlinkGenSocket) nlg = {};
	struct nlmsghdr *nlh;

	g_debug("activating firmware for %s/%s", self->bus_name, self->dev_name);

	/* Open netlink socket */
	if (!fu_devlink_netlink_gen_socket_open(&nlg, error))
		return FALSE;

	/* Prepare reload command with fw_activate action */
	nlh = fu_devlink_netlink_cmd_prepare(&nlg, DEVLINK_CMD_RELOAD, FALSE);
	mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, self->bus_name);
	mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, self->dev_name);
	mnl_attr_put_u8(nlh, DEVLINK_ATTR_RELOAD_ACTION, DEVLINK_RELOAD_ACTION_FW_ACTIVATE);

	g_debug("sending devlink reload command with fw_activate action for %s/%s",
		self->bus_name,
		self->dev_name);

	/* Send command and wait for response */
	if (!fu_devlink_netlink_msg_send(&nlg, nlh, error)) {
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

	/* Create object and assign the strings */
	self = g_object_new(FU_TYPE_DEVLINK_DEVICE, "context", ctx, NULL);
	self->bus_name = g_strdup(bus_name);
	self->dev_name = g_strdup(dev_name);

	device_id = g_strdup_printf("%s/%s", bus_name, dev_name);
	fu_device_set_physical_id(FU_DEVICE(self), device_id);
	fu_device_set_name(FU_DEVICE(self), device_id);

	return g_steal_pointer(&self);
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
	device_class->to_string = fu_devlink_device_to_string;
	device_class->setup = fu_devlink_device_setup;
	device_class->reload = fu_devlink_device_setup;
	device_class->activate = fu_devlink_device_activate;
}
