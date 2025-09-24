/*
 * Copyright 2025 NVIDIA Corporation & Affiliates
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <errno.h>
#include <glib/gstdio.h>

#include "fu-devlink-component.h"
#include "fu-devlink-device.h"
#include "fu-devlink-netlink.h"

struct _FuDevlinkDevice {
	FuDevice parent_instance;
	gchar *bus_name;
	gchar *dev_name;
	FuDevlinkGenSocket *nlg;
	FuKernelSearchPathLocker *search_path_locker;
	GPtrArray *fixed_versions;
};

G_DEFINE_TYPE(FuDevlinkDevice, fu_devlink_device, FU_TYPE_DEVICE)

typedef struct {
	gchar *fixed;
	gchar *running;
	gchar *stored;
} FuDevlinkVersionInfo;

static void
fu_devlink_device_version_info_free(FuDevlinkVersionInfo *version_info)
{
	g_free(version_info->fixed);
	g_free(version_info->running);
	g_free(version_info->stored);
	g_free(version_info);
}

static GHashTable *
fu_devlink_device_populate_attrs_map(const struct nlmsghdr *nlh)
{
	FuDevlinkVersionInfo *version_info;
	struct nlattr *attr;
	g_autoptr(GHashTable) version_table = NULL;

	version_table = g_hash_table_new_full(g_str_hash,
					      g_str_equal,
					      g_free,
					      (GDestroyNotify)fu_devlink_device_version_info_free);
	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr))
	{
		struct nlattr *ver_tb[DEVLINK_ATTR_MAX + 1] = {};
		g_autofree gchar *name = NULL;
		g_autofree gchar *value = NULL;

		if (mnl_attr_get_type(attr) != DEVLINK_ATTR_INFO_VERSION_FIXED &&
		    mnl_attr_get_type(attr) != DEVLINK_ATTR_INFO_VERSION_RUNNING &&
		    mnl_attr_get_type(attr) != DEVLINK_ATTR_INFO_VERSION_STORED)
			continue;
		if (mnl_attr_parse_nested(attr, fu_devlink_netlink_attr_cb, ver_tb) != MNL_CB_OK)
			continue;
		if (ver_tb[DEVLINK_ATTR_INFO_VERSION_NAME] == NULL ||
		    ver_tb[DEVLINK_ATTR_INFO_VERSION_VALUE] == NULL)
			continue;

		name =
		    fu_strsafe(mnl_attr_get_str(ver_tb[DEVLINK_ATTR_INFO_VERSION_NAME]), G_MAXSIZE);
		value = fu_strsafe(mnl_attr_get_str(ver_tb[DEVLINK_ATTR_INFO_VERSION_VALUE]),
				   G_MAXSIZE);
		version_info = g_hash_table_lookup(version_table, name);
		if (version_info == NULL) {
			version_info = g_new0(FuDevlinkVersionInfo, 1);
			g_hash_table_insert(version_table, g_strdup(name), version_info);
		}

		/* There are three types of versions: "fixed", "running", "stored". When "running"
		   and "stored" are tightly coupled and describe one component, "fixed" is
		   a different beast. "fixed" is used for static device identification, like ASIC
		   ID, ASIC revision, BOARD ID, etc. */
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

	return g_steal_pointer(&version_table);
}

typedef struct {
	const gchar *component_name;
	gboolean *needs_activation;
} FuDevlinkDeviceNeedsActivationHelper;

static gint
fu_devlink_device_needs_activation_cb(const struct nlmsghdr *nlh, gpointer data)
{
	FuDevlinkDeviceNeedsActivationHelper *helper = data;
	struct genlmsghdr *genl = mnl_nlmsg_get_payload(nlh);
	FuDevlinkVersionInfo *version_info;
	g_autoptr(GHashTable) version_table = NULL;

	if (genl->cmd != DEVLINK_CMD_INFO_GET)
		return MNL_CB_OK;
	version_table = fu_devlink_device_populate_attrs_map(nlh);
	version_info = g_hash_table_lookup(version_table, helper->component_name);
	if (version_info == NULL)
		return MNL_CB_OK;
	*helper->needs_activation = version_info->stored != NULL && version_info->running != NULL &&
				    g_strcmp0(version_info->stored, version_info->running) != 0;
	return MNL_CB_OK;
}

static gboolean
fu_devlink_device_needs_activation(FuDevlinkDevice *self,
				   const gchar *component_name,
				   gboolean *needs_activation,
				   GError **error)
{
	FuDevlinkDeviceNeedsActivationHelper helper = {
	    .component_name = component_name,
	    .needs_activation = needs_activation,
	};
	struct nlmsghdr *nlh;

	/* prepare dev info command */
	nlh = fu_devlink_netlink_cmd_prepare(self->nlg, DEVLINK_CMD_INFO_GET, FALSE);
	mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, self->bus_name);
	mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, self->dev_name);

	/* send command and process response */
	if (!fu_devlink_netlink_msg_send_recv(self->nlg,
					      nlh,
					      fu_devlink_device_needs_activation_cb,
					      &helper,
					      error)) {
		g_prefix_error_literal(error, "failed to get device info: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

/* perform firmware activation using devlink reload with fw_activate action */
static gboolean
fu_devlink_device_ensure_activate(FuDevlinkDevice *self,
				  const gchar *component_name,
				  GError **error)
{
	struct nlmsghdr *nlh;
	gboolean needs_activation = FALSE;

	if (!fu_devlink_device_needs_activation(self, component_name, &needs_activation, error))
		return FALSE;
	if (!needs_activation)
		return TRUE;

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
		g_prefix_error_literal(error, "failed to send devlink reload command: ");
		return FALSE;
	}

	/* success */
	g_debug("firmware activation completed for %s/%s", self->bus_name, self->dev_name);
	return TRUE;
}

/* flash status context for monitoring progress */
typedef struct {
	FuProgress *progress;
	FuDevlinkDevice *self;
	FuDevlinkGenSocket *nlg;
} FuDevlinkFlashMonHelper;

/* flash send context for thread */
typedef struct {
	FuDevlinkDevice *self;
	const gchar *component_name;
	const gchar *filename;
	GError **error;
	GMainLoop *loop;
} FuDevlinkFlashSendHelper;

/* handle flash update status and end messages */
static gint
fu_devlink_device_flash_mon_cb(const struct nlmsghdr *nlh, gpointer data)
{
	FuDevlinkFlashMonHelper *helper = data;
	const gchar *bus_name;
	const gchar *dev_name;
	guint64 done = 0;
	guint64 total = 0;
	struct genlmsghdr *genl = mnl_nlmsg_get_payload(nlh);
	struct nlattr *tb[DEVLINK_ATTR_MAX + 1] = {};

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

	if (g_strcmp0(bus_name, helper->self->bus_name) != 0 ||
	    g_strcmp0(dev_name, helper->self->dev_name) != 0)
		return MNL_CB_OK;

	if (genl->cmd == DEVLINK_CMD_FLASH_UPDATE_END) {
		fu_progress_set_percentage(helper->progress, 100);
		return MNL_CB_STOP;
	}

	/* extract progress information from status message */
	if (tb[DEVLINK_ATTR_FLASH_UPDATE_STATUS_DONE] != NULL)
		done = mnl_attr_get_u64(tb[DEVLINK_ATTR_FLASH_UPDATE_STATUS_DONE]);
	if (tb[DEVLINK_ATTR_FLASH_UPDATE_STATUS_TOTAL] != NULL)
		total = mnl_attr_get_u64(tb[DEVLINK_ATTR_FLASH_UPDATE_STATUS_TOTAL]);

	if (total > 0)
		fu_progress_set_percentage_full(helper->progress, done, total);

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
	return fu_devlink_netlink_msg_send(self->nlg, nlh, error);
}

/* thread function that sends the flash update command and quits main loop */
static gpointer
fu_devlink_device_flash_send_thread(gpointer user_data)
{
	FuDevlinkFlashSendHelper *helper = user_data;
	gboolean ret;

	g_debug("flash send thread started for %s/%s",
		helper->self->bus_name,
		helper->self->dev_name);
	ret = fu_devlink_device_flash_send(helper->self,
					   helper->component_name,
					   helper->filename,
					   helper->error);

	/* signal completion by quitting the main loop  */
	g_main_loop_quit(helper->loop);

	return GINT_TO_POINTER(ret ? 1 : 0);
}

/* netlink callback for flash progress monitoring */
static gboolean
fu_devlink_device_flash_mon_netlink_cb(GIOChannel *channel,
				       GIOCondition condition,
				       gpointer user_data)
{
	FuDevlinkFlashMonHelper *helper = user_data;
	gsize len;
	GIOStatus status;
	g_autoptr(GError) error = NULL;

	if (condition & (G_IO_ERR | G_IO_HUP)) {
		g_debug("devlink netlink socket error during flash monitoring");
		return FALSE;
	}

	/* read netlink message via GIOChannel */
	status = g_io_channel_read_chars(channel,
					 fu_devlink_netlink_gen_socket_get_buf(helper->nlg),
					 FU_DEVLINK_NETLINK_BUF_SIZE,
					 &len,
					 &error);
	if (status != G_IO_STATUS_NORMAL) {
		if (error != NULL)
			g_debug("failed to read devlink netlink message: %s", error->message);
		return TRUE;
	}

	/* process netlink messages */
	if (!fu_devlink_netlink_msg_run(helper->nlg,
					len,
					0,
					fu_devlink_device_flash_mon_cb,
					helper,
					&error)) {
		g_warning("failed to process netlink message: %s", error->message);
		/* We should not return FALSE here, because we want to continue monitoring */
	}

	/* success */
	return TRUE;
}

static gboolean
fu_devlink_device_flash(FuDevlinkDevice *self,
			const gchar *component_name,
			const gchar *filename,
			FuProgress *progress,
			GError **error)
{
	FuDevlinkFlashMonHelper flash_mon_ctx = {
	    .progress = progress,
	    .self = self,
	};
	g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
	FuDevlinkFlashSendHelper flash_send_ctx = {
	    .self = self,
	    .component_name = component_name,
	    .filename = filename,
	    .loop = loop,
	    .error = error,
	};
	guint watch_id;
	gint fd;
	gpointer thread_result;
	gboolean ret;
	g_autoptr(FuDevlinkGenSocket) nlg = NULL;
	g_autoptr(GIOChannel) channel = NULL;
	g_autoptr(GThread) flash_send_thread = NULL;

	/* open netlink socket and subscribe to multicast */
	nlg = fu_devlink_netlink_gen_socket_open(NULL, error);
	if (nlg == NULL)
		return FALSE;
	if (!fu_devlink_netlink_mcast_group_subscribe(nlg, error))
		return FALSE;

	flash_mon_ctx.nlg = nlg;
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
		g_set_error_literal(error,
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
					   gboolean omit_component_name,
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

	/* create firmware file in the kernel search path for devlink */
	fw_search_path = fu_kernel_search_path_locker_get_path(self->search_path_locker);
	fw_basename = g_strdup_printf("%s-%s-%s.bin",
				      self->bus_name,
				      self->dev_name,
				      omit_component_name ? "default" : component_name);
	fw_fullpath = g_build_filename(fw_search_path, fw_basename, NULL);
	g_debug("writing firmware to %s", fw_fullpath);

	/* write firmware to kernel search path */
	if (!fu_bytes_set_contents(fw_fullpath, fw, error))
		return FALSE;

	ret = fu_devlink_device_flash(self,
				      omit_component_name ? NULL : component_name,
				      fw_basename,
				      progress,
				      error);

	/* clean up temporary firmware file */
	if (g_unlink(fw_fullpath) != 0) {
		g_warning("failed to delete temporary firmware file %s: %s",
			  fw_fullpath,
			  fwupd_strerror(errno));
	}

	if (!ret)
		return FALSE;

	/* check if activation is needed */
	return fu_devlink_device_ensure_activate(self, component_name, error);
}

static FuDevice *
fu_devlink_device_get_component_by_logical_id(FuDevice *device, const gchar *name)
{
	GPtrArray *children = fu_device_get_children(device);
	for (guint i = 0; i < children->len; i++) {
		FuDevice *component = g_ptr_array_index(children, i);
		if (g_strcmp0(fu_device_get_logical_id(component), name) == 0)
			return g_object_ref(component);
	}
	return NULL;
}

typedef struct {
	FuDevice *device;
	GHashTable *version_table;
} FuDevlinkDeviceUpdateComponentHelper;

static void
fu_devlink_device_add_component_instance_strs(FuDevice *component,
					      FuDevlinkDeviceUpdateComponentHelper *helper)
{
	FuDevlinkDevice *self = FU_DEVLINK_DEVICE(helper->device);

	if (self->fixed_versions == NULL)
		return;

	/* There might me multiple arrays of fixed versions obtained from quirk file.
	   Iterate over all of them and add instance strings to component device. */
	for (guint i = 0; i < self->fixed_versions->len; i++) {
		gboolean complete_set = TRUE;
		const gchar **names = g_ptr_array_index(self->fixed_versions, i);
		g_autoptr(GStrvBuilder) keys_builder = g_strv_builder_new();

		for (guint j = 0; names[j] != NULL; j++) {
			FuDevlinkVersionInfo *version_info =
			    g_hash_table_lookup(helper->version_table, names[j]);
			g_autofree gchar *key = NULL;

			if (version_info == NULL || version_info->fixed == NULL) {
				complete_set = FALSE;
				continue;
			}
			key = g_ascii_strup(names[j], -1);
			g_strv_builder_add(keys_builder, key);

			/* avoid re-insertion of the same key */
			if (fu_device_get_instance_str(component, key) == NULL)
				fu_device_add_instance_str(component, key, version_info->fixed);
		}
		/* In case all keys are present in version table obtained from kernel,
		   add the set to component to build instance id for it during probe. */
		if (complete_set)
			fu_devlink_component_add_instance_keys(component,
							       g_strv_builder_end(keys_builder));
	}
}

static void
fu_devlink_device_update_component_cb(gpointer key, gpointer value, gpointer user_data)
{
	FuDevlinkDeviceUpdateComponentHelper *helper = user_data;
	const gchar *name = (const gchar *)key;
	FuDevlinkVersionInfo *version_info = value;
	const gchar *version;
	g_autofree gchar *instance_id = g_strdup_printf("DEVLINK\\COMPONENT_%s", name);
	g_autoptr(FuDevice) component = NULL;
	g_autoptr(GError) error_local = NULL;

	/* "fw.bootloader" is a special case. If there is a fixed version of it present,
	   set it as the bootloader version. */
	if (g_strcmp0(name, "fw.bootloader") == 0)
		fu_device_set_version_bootloader(helper->device, version_info->fixed);

	/* A component and running-stored tuple has 1:1 relationship. No guarantee
	   that both are present, if either is present, try to create component. */
	if (version_info->stored != NULL)
		version = version_info->stored;
	else if (version_info->running != NULL)
		version = version_info->running;
	else
		return;

	component = fu_devlink_device_get_component_by_logical_id(helper->device, name);
	if (component != NULL) {
		fu_device_set_version(component, version);
		g_debug("updated component %s (version: %s)", name, version);
		return;
	}

	/* create new component and lookup quirk to added as a child */
	component = fu_devlink_component_new(helper->device, name);
	fu_device_add_instance_id_full(component, instance_id, FU_DEVICE_INSTANCE_FLAG_QUIRKS);
	if (fu_device_get_name(component) == NULL) {
		g_debug("ignoring %s", name);
		return;
	}
	fu_device_incorporate(component, helper->device, FU_DEVICE_INCORPORATE_FLAG_INSTANCE_KEYS);
	fu_devlink_device_add_component_instance_strs(component, helper);
	fu_device_set_version_format(component, fu_version_guess_format(version));
	fu_device_set_version(component, version);
	if (!fu_device_probe(component, &error_local)) {
		g_warning("failed to probe %s: %s", name, error_local->message);
		return;
	}
	fu_device_add_child(helper->device, component);
	g_debug("added component %s (version: %s)", name, version);
}

/* callback for parsing devlink dev info response */
static gint
fu_devlink_device_info_cb(const struct nlmsghdr *nlh, gpointer data)
{
	FuDevice *device = FU_DEVICE(data);
	FuDevlinkDeviceUpdateComponentHelper helper = {0};
	GPtrArray *children = fu_device_get_children(device);
	struct genlmsghdr *genl = mnl_nlmsg_get_payload(nlh);
	struct nlattr *tb[DEVLINK_ATTR_MAX + 1] = {};
	g_autoptr(GHashTable) version_table = NULL;
	g_autoptr(GPtrArray) components_to_remove = g_ptr_array_new();

	if (genl->cmd != DEVLINK_CMD_INFO_GET)
		return MNL_CB_OK;

	/* parse main attributes */
	mnl_attr_parse(nlh, sizeof(*genl), fu_devlink_netlink_attr_cb, tb);

	version_table = fu_devlink_device_populate_attrs_map(nlh);

	/* remove components that are not in the attrs map */
	for (guint i = 0; i < children->len; i++) {
		FuDevice *component = g_ptr_array_index(children, i);
		const gchar *logical_id = fu_device_get_logical_id(component);
		FuDevlinkVersionInfo *version_info = g_hash_table_lookup(version_table, logical_id);

		if (version_info == NULL) {
			g_debug("removed component %s", logical_id);
			g_ptr_array_add(components_to_remove, component);
		}
	}
	for (guint i = 0; i < components_to_remove->len; i++) {
		FuDevice *component = g_ptr_array_index(components_to_remove, i);
		fu_device_remove_child(device, component);
	}

	helper.device = device;
	helper.version_table = version_table;
	g_hash_table_foreach(version_table, fu_devlink_device_update_component_cb, &helper);

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

	/* send command and process response */
	if (!fu_devlink_netlink_msg_send_recv(self->nlg,
					      nlh,
					      fu_devlink_device_info_cb,
					      device,
					      error)) {
		g_prefix_error_literal(error, "failed to get device info: ");
		return FALSE;
	}

	/* success */
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
	FuDevlinkDevice *self = FU_DEVLINK_DEVICE(device);
	g_autofree gchar *subsystem = NULL;
	g_autofree gchar *summary = NULL;

	/* check if device has been properly initialized */
	if (self->bus_name == NULL || self->dev_name == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "devlink device not properly initialized");
		return FALSE;
	}

	subsystem = g_ascii_strup(self->bus_name, -1);

	/* set summary with devlink handle for better user visibility */
	summary = g_strdup_printf("Devlink device (%s/%s)", self->bus_name, self->dev_name);
	fu_device_set_summary(device, summary);

	/* use quirk database for a better name */
	if (fu_device_get_vid(device) != 0 && fu_device_get_pid(device) != 0) {
		fu_device_add_instance_u16(device, "VEN", fu_device_get_vid(device));
		fu_device_add_instance_u16(device, "DEV", fu_device_get_pid(device));
		if (!fu_device_build_instance_id_full(device,
						      FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						      error,
						      subsystem,
						      "VEN",
						      "DEV",
						      NULL)) {
			g_prefix_error_literal(error, "failed to create quirk for name: ");
			return FALSE;
		}
	}

	/* get device information and version */
	return fu_devlink_device_get_info(device, error);
}

const gchar *
fu_devlink_device_get_bus_name(FuDevlinkDevice *self)
{
	return self->bus_name;
}

static void
fu_devlink_device_set_bus_name(FuDevlinkDevice *self, const gchar *bus_name)
{
	g_free(self->bus_name);
	self->bus_name = g_strdup(bus_name);
}

static void
fu_devlink_device_set_dev_name(FuDevlinkDevice *self, const gchar *dev_name)
{
	g_free(self->dev_name);
	self->dev_name = g_strdup(dev_name);
}

FuDevice *
fu_devlink_device_new(FuContext *ctx,
		      const gchar *bus_name,
		      const gchar *dev_name,
		      const gchar *serial_number)
{
	g_autoptr(FuDevlinkDevice) self = NULL;
	g_autofree gchar *device_id = NULL;

	g_return_val_if_fail(bus_name != NULL, NULL);
	g_return_val_if_fail(dev_name != NULL, NULL);

	/* create object and assign the strings */
	self = g_object_new(FU_TYPE_DEVLINK_DEVICE, "context", ctx, NULL);
	fu_devlink_device_set_bus_name(self, bus_name);
	fu_devlink_device_set_dev_name(self, dev_name);

	if (serial_number != NULL) {
		fu_device_set_serial(FU_DEVICE(self), serial_number);
		fu_device_set_physical_id(FU_DEVICE(self), serial_number);
	} else {
		device_id = g_strdup_printf("%s/%s", bus_name, dev_name);
		fu_device_set_physical_id(FU_DEVICE(self), device_id);
	}

	return FU_DEVICE(g_steal_pointer(&self));
}

static gboolean
fu_devlink_device_open(FuDevice *device, GError **error)
{
	FuDevlinkDevice *self = FU_DEVLINK_DEVICE(device);

	/* open devlink netlink socket */
	self->nlg = fu_devlink_netlink_gen_socket_open(device, error);
	if (self->nlg == NULL)
		return FALSE;

	/* success */
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
			    "failed to create '%s': %s",
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

	/* success */
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
fu_devlink_device_add_json(FuDevice *device, JsonBuilder *builder, FwupdCodecFlags flags)
{
	FuDevlinkDevice *self = FU_DEVLINK_DEVICE(device);
	GPtrArray *events = fu_device_get_events(device);

	/* add device type identifier */
	fwupd_codec_json_append(builder, "GType", "FuDevlinkDevice");

	/* add devlink-specific properties for regular devices */
	fwupd_codec_json_append(builder, "BusName", self->bus_name);
	fwupd_codec_json_append(builder, "DevName", self->dev_name);

	/* serialize recorded events */
	if (events->len > 0) {
		json_builder_set_member_name(builder, "Events");
		json_builder_begin_array(builder);
		for (guint i = 0; i < events->len; i++) {
			FuDeviceEvent *event = g_ptr_array_index(events, i);

			json_builder_begin_object(builder);
			fwupd_codec_to_json(FWUPD_CODEC(event),
					    builder,
					    events->len > 1000 ? flags | FWUPD_CODEC_FLAG_COMPRESSED
							       : flags);
			json_builder_end_object(builder);
		}
		json_builder_end_array(builder);
	}
}

static gboolean
fu_devlink_device_from_json(FuDevice *device, JsonObject *json_object, GError **error)
{
	FuDevlinkDevice *self = FU_DEVLINK_DEVICE(device);
	const gchar *bus_name;
	const gchar *dev_name;
	g_autofree gchar *device_id = NULL;

	/* devlink-specific properties */
	bus_name = json_object_get_string_member_with_default(json_object, "BusName", NULL);
	dev_name = json_object_get_string_member_with_default(json_object, "DevName", NULL);

	if (bus_name == NULL || dev_name == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "BusName and DevName required for devlink device");
		return FALSE;
	}

	fu_devlink_device_set_bus_name(self, bus_name);
	fu_devlink_device_set_dev_name(self, dev_name);

	device_id = g_strdup_printf("%s/%s", bus_name, dev_name);
	fu_device_set_physical_id(FU_DEVICE(self), device_id);
	fu_device_set_name(FU_DEVICE(self), device_id);
	fu_device_set_backend_id(device, device_id);

	/* array of events */
	if (json_object_has_member(json_object, "Events")) {
		JsonArray *json_array = json_object_get_array_member(json_object, "Events");

		for (guint i = 0; i < json_array_get_length(json_array); i++) {
			JsonNode *node_tmp = json_array_get_element(json_array, i);
			g_autoptr(FuDeviceEvent) event = fu_device_event_new(NULL);

			if (!fwupd_codec_from_json(FWUPD_CODEC(event), node_tmp, error))
				return FALSE;
			fu_device_add_event(device, event);
		}
	}

	/* success */
	return TRUE;
}

static void
fu_devlink_device_incorporate(FuDevice *device, FuDevice *donor_device)
{
	FuDevlinkDevice *self = FU_DEVLINK_DEVICE(device);
	FuDevlinkDevice *donor = FU_DEVLINK_DEVICE(donor_device);

	g_return_if_fail(FU_IS_DEVLINK_DEVICE(device));
	g_return_if_fail(FU_IS_DEVLINK_DEVICE(donor_device));

	/* copy bus_name if not already set */
	if (self->bus_name == NULL && donor->bus_name != NULL)
		fu_devlink_device_set_bus_name(self, donor->bus_name);

	/* copy dev_name if not already set */
	if (self->dev_name == NULL && donor->dev_name != NULL)
		fu_devlink_device_set_dev_name(self, donor->dev_name);
}

static void
fu_devlink_device_add_fixed_versions(FuDevlinkDevice *self, gchar **fixed_versions)
{
	if (self->fixed_versions == NULL)
		self->fixed_versions = g_ptr_array_new_with_free_func((GDestroyNotify)g_strfreev);
	g_ptr_array_add(self->fixed_versions, fixed_versions);
}

static gboolean
fu_devlink_device_set_quirk_kv(FuDevice *device,
			       const gchar *key,
			       const gchar *value,
			       GError **error)
{
	FuDevlinkDevice *self = FU_DEVLINK_DEVICE(device);

	if (g_strcmp0(key, "DevlinkFixedVersions") == 0) {
		fu_devlink_device_add_fixed_versions(self, g_strsplit(value, ",", -1));
		return TRUE;
	}

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_devlink_device_init(FuDevlinkDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "org.kernel.devlink");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_EMULATION_TAG);
	fu_device_add_possible_plugin(FU_DEVICE(self), "devlink");
}

static void
fu_devlink_device_finalize(GObject *object)
{
	FuDevlinkDevice *self = FU_DEVLINK_DEVICE(object);

	g_free(self->bus_name);
	g_free(self->dev_name);
	if (self->fixed_versions != NULL)
		g_ptr_array_unref(self->fixed_versions);

	G_OBJECT_CLASS(fu_devlink_device_parent_class)->finalize(object);
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
	device_class->add_json = fu_devlink_device_add_json;
	device_class->from_json = fu_devlink_device_from_json;
	device_class->incorporate = fu_devlink_device_incorporate;
	device_class->set_quirk_kv = fu_devlink_device_set_quirk_kv;
}
