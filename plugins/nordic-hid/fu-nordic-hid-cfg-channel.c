/*
 * Copyright (C) 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 * Copyright (C) 2021 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#ifdef HAVE_HIDRAW_H
#include <linux/hidraw.h>
#include <linux/input.h>
#endif
#include <fwupdplugin.h>

#include "fu-nordic-hid-archive.h"
#include "fu-nordic-hid-cfg-channel.h"

#define HID_REPORT_ID	     6
#define REPORT_SIZE	     30
#define REPORT_DATA_MAX_LEN  (REPORT_SIZE - 5)
#define HWID_LEN	     8
#define END_OF_TRANSFER_CHAR 0x0a
#define INVALID_PEER_ID	     0xFF

#define FU_NORDIC_HID_CFG_CHANNEL_RETRIES	  10
#define FU_NORDIC_HID_CFG_CHANNEL_RETRY_DELAY	  50  /* ms */
#define FU_NORDIC_HID_CFG_CHANNEL_DFU_RETRY_DELAY 500 /* ms */

#define FU_NORDIC_HID_CFG_CHANNEL_IOCTL_TIMEOUT 5000 /* ms */

typedef enum {
	CONFIG_STATUS_PENDING,
	CONFIG_STATUS_GET_MAX_MOD_ID,
	CONFIG_STATUS_GET_HWID,
	CONFIG_STATUS_GET_BOARD_NAME,
	CONFIG_STATUS_INDEX_PEERS,
	CONFIG_STATUS_GET_PEER,
	CONFIG_STATUS_SET,
	CONFIG_STATUS_FETCH,
	CONFIG_STATUS_SUCCESS,
	CONFIG_STATUS_TIMEOUT,
	CONFIG_STATUS_REJECT,
	CONFIG_STATUS_WRITE_FAIL,
	CONFIG_STATUS_DISCONNECTED,
	CONFIG_STATUS_FAULT = 99,
} FuNordicCfgStatus;

typedef enum {
	DFU_STATE_INACTIVE,
	DFU_STATE_ACTIVE,
	DFU_STATE_STORING,
	DFU_STATE_CLEANING,
} FuNordicCfgSyncState;

typedef struct __attribute__((packed)) {
	guint8 report_id;
	guint8 recipient;
	guint8 event_id;
	guint8 status;
	guint8 data_len;
	guint8 data[REPORT_DATA_MAX_LEN];
} FuNordicCfgChannelMsg;

typedef struct {
	guint8 idx;
	gchar *name;
} FuNordicCfgChannelModuleOption;

typedef struct {
	guint8 idx;
	gchar *name;
	GPtrArray *options; /* of FuNordicCfgChannelModuleOption */
} FuNordicCfgChannelModule;

typedef struct {
	guint8 status;
	guint8 *buf;
	gsize bufsz;
} FuNordicCfgChannelRcvHelper;

typedef struct {
	guint8 dfu_state;
	guint32 img_length;
	guint32 img_csum;
	guint32 offset;
	guint16 sync_buffer_size;
} FuNordicCfgChannelDfuInfo;

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuNordicCfgChannelMsg, g_free);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuNordicCfgChannelDfuInfo, g_free);

struct _FuNordicHidCfgChannel {
	FuUdevDevice parent_instance;
	gchar *board_name;
	gchar *bl_name;
	guint8 flash_area_id;
	guint32 flashed_image_len;
	guint8 peer_id;
	GPtrArray *modules; /* of FuNordicCfgChannelModule */
};

G_DEFINE_TYPE(FuNordicHidCfgChannel, fu_nordic_hid_cfg_channel, FU_TYPE_UDEV_DEVICE)

static void
fu_nordic_hid_cfg_channel_module_option_free(FuNordicCfgChannelModuleOption *opt)
{
	g_free(opt->name);
	g_free(opt);
}

static void
fu_nordic_hid_cfg_channel_module_free(FuNordicCfgChannelModule *mod)
{
	if (mod->options != NULL)
		g_ptr_array_unref(mod->options);
	g_free(mod->name);
	g_free(mod);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuNordicCfgChannelModule, fu_nordic_hid_cfg_channel_module_free);

#ifdef HAVE_HIDRAW_H
static FuUdevDevice *
fu_nordic_hid_cfg_channel_get_udev_device(FuNordicHidCfgChannel *self, GError **error)
{
	FuDevice *parent;

	/* ourselves */
	if (self->peer_id == 0)
		return FU_UDEV_DEVICE(self);

	/* parent */
	parent = fu_device_get_parent(FU_DEVICE(self));
	if (parent == NULL) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "no parent for peer 0x%02x",
			    self->peer_id);
		return NULL;
	}
	return FU_UDEV_DEVICE(parent);
}
#endif

static gboolean
fu_nordic_hid_cfg_channel_send(FuNordicHidCfgChannel *self,
			       guint8 *buf,
			       gsize bufsz,
			       GError **error)
{
#ifdef HAVE_HIDRAW_H
	FuUdevDevice *udev_device = fu_nordic_hid_cfg_channel_get_udev_device(self, error);
	if (udev_device == NULL)
		return FALSE;
	if (g_getenv("FWUPD_NORDIC_HID_VERBOSE") != NULL)
		fu_dump_raw(G_LOG_DOMAIN, "Sent", buf, bufsz);
	if (!fu_udev_device_ioctl(udev_device,
				  HIDIOCSFEATURE(bufsz),
				  buf,
				  NULL,
				  FU_NORDIC_HID_CFG_CHANNEL_IOCTL_TIMEOUT,
				  error))
		return FALSE;
	return TRUE;
#else
	g_set_error_literal(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "<linux/hidraw.h> not available");
	return FALSE;
#endif
}

static gboolean
fu_nordic_hid_cfg_channel_receive(FuNordicHidCfgChannel *self,
				  guint8 *buf,
				  gsize bufsz,
				  GError **error)
{
	g_autoptr(FuNordicCfgChannelMsg) recv_msg = g_new0(FuNordicCfgChannelMsg, 1);
#ifdef HAVE_HIDRAW_H
	FuUdevDevice *udev_device = fu_nordic_hid_cfg_channel_get_udev_device(self, error);
	if (udev_device == NULL)
		return FALSE;
	for (gint i = 1; i < 100; i++) {
		recv_msg->report_id = HID_REPORT_ID;
		recv_msg->recipient = self->peer_id;
		if (!fu_udev_device_ioctl(udev_device,
					  HIDIOCGFEATURE(sizeof(*recv_msg)),
					  (guint8 *)recv_msg,
					  NULL,
					  FU_NORDIC_HID_CFG_CHANNEL_IOCTL_TIMEOUT,
					  error))
			return FALSE;
		/* if the device is busy it return 06 00 00 00 00 response */
		if (recv_msg->report_id == HID_REPORT_ID &&
		    (recv_msg->recipient | recv_msg->event_id | recv_msg->status |
		     recv_msg->data_len))
			break;
		g_usleep(i * 50);
	}
	if (!fu_memcpy_safe(buf,
			    bufsz,
			    0,
			    (guint8 *)recv_msg,
			    sizeof(*recv_msg),
			    0,
			    sizeof(*recv_msg),
			    error)) {
		return FALSE;
	}

	if (g_getenv("FWUPD_NORDIC_HID_VERBOSE") != NULL)
		fu_dump_raw(G_LOG_DOMAIN, "Received", buf, bufsz);
	/*
	 * [TODO]: Possibly add the report-id fix for Bluez versions < 5.56:
	 * https://github.com/bluez/bluez/commit/35a2c50437cca4d26ac6537ce3a964bb509c9b62
	 *
	 * See fu_pxi_ble_device_get_feature() in
	 * plugins/pixart-rf/fu-pxi-ble-device.c for an example.
	 */
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "<linux/hidraw.h> not available");
	return FALSE;
#endif
}

static gboolean
fu_nordic_hid_cfg_channel_receive_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNordicCfgChannelRcvHelper *args = (FuNordicCfgChannelRcvHelper *)user_data;
	FuNordicHidCfgChannel *self = FU_NORDIC_HID_CFG_CHANNEL(device);
	FuNordicCfgChannelMsg *recv_msg = NULL;

	if (!fu_nordic_hid_cfg_channel_receive(self, args->buf, args->bufsz, error))
		return FALSE;
	recv_msg = (FuNordicCfgChannelMsg *)args->buf;
	if (recv_msg->status != args->status) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "received status: 0x%02x, expected: 0x%02x",
			    recv_msg->status,
			    args->status);
		return FALSE;
	}

	/* success */
	return TRUE;
}

/*
 * fu_nordic_hid_cfg_channel_get_event_id:
 * @module_name: module name, NULL for generic operations
 * @option_name: option name, NULL for generic module operations
 *
 * Construct Event ID from module and option names.
 *
 * Returns: %TRUE if module/option pair found
 */
static gboolean
fu_nordic_hid_cfg_channel_get_event_id(FuNordicHidCfgChannel *self,
				       const gchar *module_name,
				       const gchar *option_name,
				       guint8 *event_id)
{
	FuNordicCfgChannelModule *mod = NULL;
	guint id = 0;

	*event_id = 0;

	/* for generic operations */
	if (module_name == NULL)
		return TRUE;

	for (id = 0; id < self->modules->len; id++) {
		mod = g_ptr_array_index(self->modules, id);
		if (g_strcmp0(module_name, mod->name) == 0)
			break;
	}
	if (mod == NULL || id > 0x0f)
		return FALSE;

	*event_id = id << 4;

	/* for generic module operations */
	if (option_name == NULL)
		return TRUE;

	for (guint i = 0; i < mod->options->len && i <= 0x0f; i++) {
		FuNordicCfgChannelModuleOption *opt = g_ptr_array_index(mod->options, i);
		if (g_strcmp0(option_name, opt->name) == 0) {
			*event_id = (id << 4) + opt->idx;
			return TRUE;
		}
	}

	/* module have no requested option */
	return FALSE;
}

static gboolean
fu_nordic_hid_cfg_channel_cmd_send_by_id(FuNordicHidCfgChannel *self,
					 guint8 event_id,
					 guint8 status,
					 guint8 *data,
					 guint8 data_len,
					 GError **error)
{
	g_autoptr(FuNordicCfgChannelMsg) msg = g_new0(FuNordicCfgChannelMsg, 1);

	msg->report_id = HID_REPORT_ID;
	msg->recipient = self->peer_id;
	msg->event_id = event_id;
	msg->status = status;
	msg->data_len = 0;

	if (data != NULL) {
		if (data_len > REPORT_DATA_MAX_LEN) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "requested to send %d bytes, while maximum is %d",
				    data_len,
				    REPORT_DATA_MAX_LEN);
			return FALSE;
		}
		if (!fu_memcpy_safe(msg->data,
				    REPORT_DATA_MAX_LEN,
				    0,
				    data,
				    data_len,
				    0,
				    data_len,
				    error))
			return FALSE;
		msg->data_len = data_len;
	}

	if (!fu_nordic_hid_cfg_channel_send(self, (guint8 *)msg, sizeof(*msg), error)) {
		g_prefix_error(error, "failed to send: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_nordic_hid_cfg_channel_cmd_send(FuNordicHidCfgChannel *self,
				   const gchar *module_name,
				   const gchar *option_name,
				   guint8 status,
				   guint8 *data,
				   guint8 data_len,
				   GError **error)
{
	guint8 event_id = 0;

	if (!fu_nordic_hid_cfg_channel_get_event_id(self, module_name, option_name, &event_id)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "requested non-existing module %s with option %s",
			    module_name,
			    option_name);
		return FALSE;
	}

	if (!fu_nordic_hid_cfg_channel_cmd_send_by_id(self,
						      event_id,
						      status,
						      data,
						      data_len,
						      error)) {
		g_prefix_error(error, "failed to send: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_nordic_hid_cfg_channel_cmd_receive(FuNordicHidCfgChannel *self,
				      guint8 status,
				      FuNordicCfgChannelMsg *res,
				      GError **error)
{
	FuNordicCfgChannelRcvHelper helper;

	res->report_id = HID_REPORT_ID;
	helper.status = status;
	helper.buf = (guint8 *)res;
	helper.bufsz = sizeof(*res);
	if (!fu_device_retry(FU_DEVICE(self),
			     fu_nordic_hid_cfg_channel_receive_cb,
			     FU_NORDIC_HID_CFG_CHANNEL_RETRIES,
			     &helper,
			     error)) {
		g_prefix_error(error, "Failed on receive: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_nordic_hid_cfg_channel_add_peers(FuNordicHidCfgChannel *self, GError **error)
{
	guint cnt = 0;
	g_autoptr(FuNordicCfgChannelMsg) res = g_new0(FuNordicCfgChannelMsg, 1);
	g_autoptr(GError) error_local = NULL;

	if (self->peer_id != 0)
		return TRUE;

	if (!fu_nordic_hid_cfg_channel_cmd_send(self,
						NULL,
						NULL,
						CONFIG_STATUS_INDEX_PEERS,
						NULL,
						0,
						error))
		return FALSE;
	if (fu_nordic_hid_cfg_channel_cmd_receive(self,
						  CONFIG_STATUS_DISCONNECTED,
						  res,
						  &error_local)) {
		/* no peers */
		return TRUE;
	}

	/* Peers available */
	if (!fu_nordic_hid_cfg_channel_cmd_receive(self, CONFIG_STATUS_SUCCESS, res, error))
		return FALSE;

	while (cnt++ <= 0xFF) {
		g_autoptr(FuNordicHidCfgChannel) peer = NULL;

		if (!fu_nordic_hid_cfg_channel_cmd_send(self,
							NULL,
							NULL,
							CONFIG_STATUS_GET_PEER,
							NULL,
							0,
							error))
			return FALSE;
		if (!fu_nordic_hid_cfg_channel_cmd_receive(self, CONFIG_STATUS_SUCCESS, res, error))
			return FALSE;

		/* end of the list */
		if (res->data[8] == INVALID_PEER_ID)
			return TRUE;

		if (g_getenv("FWUPD_NORDIC_HID_VERBOSE") != NULL)
			g_debug("detected peer: 0x%02x", res->data[8]);

		peer = fu_nordic_hid_cfg_channel_new(res->data[8]);
		/* prohibit to close close parent's communication descriptor */
		fu_device_add_internal_flag(FU_DEVICE(peer),
					    FU_DEVICE_INTERNAL_FLAG_USE_PARENT_FOR_OPEN);
		/* probe&setup are the part of adding child */
		fu_device_add_child(FU_DEVICE(self), FU_DEVICE(peer));
	}

	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_BROKEN_PIPE, "too many peers detected");
	return FALSE;
}

static gboolean
fu_nordic_hid_cfg_channel_get_board_name(FuNordicHidCfgChannel *self, GError **error)
{
	g_autoptr(FuNordicCfgChannelMsg) res = g_new0(FuNordicCfgChannelMsg, 1);

	if (!fu_nordic_hid_cfg_channel_cmd_send(self,
						NULL,
						NULL,
						CONFIG_STATUS_GET_BOARD_NAME,
						NULL,
						0,
						error))
		return FALSE;
	if (!fu_nordic_hid_cfg_channel_cmd_receive(self, CONFIG_STATUS_SUCCESS, res, error))
		return FALSE;
	self->board_name = fu_strsafe((const gchar *)res->data, res->data_len);

	/* success */
	return TRUE;
}

static gboolean
fu_nordic_hid_cfg_channel_get_bl_name(FuNordicHidCfgChannel *self, GError **error)
{
	guint8 event_id = 0;
	g_autoptr(FuNordicCfgChannelMsg) res = g_new0(FuNordicCfgChannelMsg, 1);

	/* query for the bootloader name if the board support it */
	if (fu_nordic_hid_cfg_channel_get_event_id(self, "dfu", "module_variant", &event_id)) {
		if (!fu_nordic_hid_cfg_channel_cmd_send(self,
							"dfu",
							"module_variant",
							CONFIG_STATUS_FETCH,
							NULL,
							0,
							error))
			return FALSE;
		if (!fu_nordic_hid_cfg_channel_cmd_receive(self, CONFIG_STATUS_SUCCESS, res, error))
			return FALSE;

		/* check if not set via quirk */
		if (self->bl_name != NULL &&
		    strncmp(self->bl_name, (const char *)res->data, res->data_len) != 0) {
			g_set_error(
			    error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "bootloader in qiurk file is '%s' while the board is supporting '%s'",
			    self->bl_name,
			    g_strndup((const gchar *)res->data, res->data_len));
			return FALSE;
		}
		self->bl_name = fu_strsafe((const gchar *)res->data, res->data_len);
	} else if (g_getenv("FWUPD_NORDIC_HID_VERBOSE") != NULL) {
		g_debug("the board have no support of bootloader runtime detection");
	}

	if (self->bl_name == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "the bootloader is not detected nor set via quirk");
		return FALSE;
	}

	/* success */
	return TRUE;
}

/*
 * NOTE:
 * For devices connected directly to the host,
 * hw_id = HID_UNIQ = logical_id.
 */
static gboolean
fu_nordic_hid_cfg_channel_get_hwid(FuNordicHidCfgChannel *self, GError **error)
{
	guint8 hw_id[HWID_LEN] = {0x0};
	g_autofree gchar *physical_id = NULL;
	g_autoptr(FuNordicCfgChannelMsg) res = g_new0(FuNordicCfgChannelMsg, 1);

	if (!fu_nordic_hid_cfg_channel_cmd_send(self,
						NULL,
						NULL,
						CONFIG_STATUS_GET_HWID,
						NULL,
						0,
						error))
		return FALSE;
	if (!fu_nordic_hid_cfg_channel_cmd_receive(self, CONFIG_STATUS_SUCCESS, res, error))
		return FALSE;

	if (!fu_memcpy_safe(hw_id, HWID_LEN, 0, res->data, REPORT_DATA_MAX_LEN, 0, HWID_LEN, error))
		return FALSE;

	/* allows to detect the single device connected via several interfaces */
	physical_id = g_strdup_printf("%s-%02x%02x%02x%02x%02x%02x%02x%02x-%s",
				      self->board_name,
				      hw_id[0],
				      hw_id[1],
				      hw_id[2],
				      hw_id[3],
				      hw_id[4],
				      hw_id[5],
				      hw_id[6],
				      hw_id[7],
				      self->bl_name);
	fu_device_set_physical_id(FU_DEVICE(self), physical_id);

	/* success */
	return TRUE;
}

static gboolean
fu_nordic_hid_cfg_channel_load_module_opts(FuNordicHidCfgChannel *self,
					   FuNordicCfgChannelModule *mod,
					   GError **error)
{
	for (guint8 i = 0; i < 0xFF; i++) {
		FuNordicCfgChannelModuleOption *opt = NULL;
		g_autoptr(FuNordicCfgChannelMsg) res = g_new0(FuNordicCfgChannelMsg, 1);

		if (!fu_nordic_hid_cfg_channel_cmd_send_by_id(self,
							      mod->idx << 4,
							      CONFIG_STATUS_FETCH,
							      NULL,
							      0,
							      error))
			return FALSE;
		if (!fu_nordic_hid_cfg_channel_cmd_receive(self, CONFIG_STATUS_SUCCESS, res, error))
			return FALSE;

		/* res->data: option name */
		if (res->data[0] == END_OF_TRANSFER_CHAR)
			break;
		opt = g_new0(FuNordicCfgChannelModuleOption, 1);
		opt->name = fu_strsafe((const gchar *)res->data, res->data_len);
		opt->idx = i;
		g_ptr_array_add(mod->options, opt);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_nordic_hid_cfg_channel_load_module_info(FuNordicHidCfgChannel *self,
					   guint8 module_idx,
					   GError **error)
{
	g_autoptr(FuNordicCfgChannelModule) mod = g_new0(FuNordicCfgChannelModule, 1);

	mod->idx = module_idx;
	mod->options = g_ptr_array_new_with_free_func(
	    (GDestroyNotify)fu_nordic_hid_cfg_channel_module_option_free);
	if (!fu_nordic_hid_cfg_channel_load_module_opts(self, mod, error))
		return FALSE;
	/* module description is the 1st loaded option */
	if (mod->options->len > 0) {
		FuNordicCfgChannelModuleOption *opt = g_ptr_array_index(mod->options, 0);
		mod->name = g_strdup(opt->name);
		if (!g_ptr_array_remove_index(mod->options, 0)) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "cannot remove option");
			return FALSE;
		}
	}

	/* success */
	g_ptr_array_add(self->modules, g_steal_pointer(&mod));
	return TRUE;
}

static gboolean
fu_nordic_hid_cfg_channel_get_modinfo(FuNordicHidCfgChannel *self, GError **error)
{
	g_autoptr(FuNordicCfgChannelMsg) res = g_new0(FuNordicCfgChannelMsg, 1);

	if (!fu_nordic_hid_cfg_channel_cmd_send(self,
						NULL,
						NULL,
						CONFIG_STATUS_GET_MAX_MOD_ID,
						NULL,
						0,
						error))
		return FALSE;
	if (!fu_nordic_hid_cfg_channel_cmd_receive(self, CONFIG_STATUS_SUCCESS, res, error))
		return FALSE;

	/* res->data[0]: maximum module idx */
	for (guint i = 0; i <= res->data[0]; i++) {
		if (!fu_nordic_hid_cfg_channel_load_module_info(self, i, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_nordic_hid_cfg_channel_dfu_fwinfo(FuNordicHidCfgChannel *self, GError **error)
{
	guint16 ver_rev;
	guint32 ver_build_nr;
	g_autofree gchar *version = NULL;
	g_autoptr(FuNordicCfgChannelMsg) res = g_new0(FuNordicCfgChannelMsg, 1);

	if (!fu_nordic_hid_cfg_channel_cmd_send(self,
						"dfu",
						"fwinfo",
						CONFIG_STATUS_FETCH,
						NULL,
						0,
						error))
		return FALSE;
	if (!fu_nordic_hid_cfg_channel_cmd_receive(self, CONFIG_STATUS_SUCCESS, res, error))
		return FALSE;

	/* parsing fwinfo answer */
	/* TODO: add banks amount into quirk */
	if (res->data[0] > 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "invalid flash area returned by device");
		return FALSE;
	}
	/* set the target flash ID area */
	self->flash_area_id = res->data[0] ^ 1;
	/* always use the bank 0 for MCUBOOT bootloader */
	if (g_strcmp0(self->bl_name, "MCUBOOT") == 0)
		self->flash_area_id = 0;

	if (!fu_memread_uint32_safe(res->data,
				    REPORT_SIZE,
				    0x01,
				    &self->flashed_image_len,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint16_safe(res->data, REPORT_SIZE, 0x07, &ver_rev, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_memread_uint32_safe(res->data,
				    REPORT_SIZE,
				    0x09,
				    &ver_build_nr,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	version = g_strdup_printf("%u.%u.%u.%u", res->data[4], res->data[5], ver_rev, ver_build_nr);
	fu_device_set_version(FU_DEVICE(self), version);

	/* success */
	return TRUE;
}

static gboolean
fu_nordic_hid_cfg_channel_dfu_reboot(FuNordicHidCfgChannel *self, GError **error)
{
	g_autoptr(FuNordicCfgChannelMsg) res = g_new0(FuNordicCfgChannelMsg, 1);

	if (!fu_nordic_hid_cfg_channel_cmd_send(self,
						"dfu",
						"reboot",
						CONFIG_STATUS_FETCH,
						NULL,
						0,
						error))
		return FALSE;
	if (!fu_nordic_hid_cfg_channel_cmd_receive(self, CONFIG_STATUS_SUCCESS, res, error))
		return FALSE;
	if (res->data_len != 1 || res->data[0] != 0x01) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "reboot data was invalid");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_nordic_hid_cfg_channel_dfu_sync_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNordicHidCfgChannel *self = FU_NORDIC_HID_CFG_CHANNEL(device);
	FuNordicCfgChannelRcvHelper *args = (FuNordicCfgChannelRcvHelper *)user_data;
	g_autoptr(FuNordicCfgChannelMsg) recv_msg = g_new0(FuNordicCfgChannelMsg, 1);

	/* allow to sync buffer more precisely and without annoying messages
	 * it may take some time and depending on device workload */
	for (gint i = 1; i < 30; i++) {
		if (!fu_nordic_hid_cfg_channel_cmd_send(self,
							"dfu",
							"sync",
							CONFIG_STATUS_FETCH,
							NULL,
							0,
							error))
			return FALSE;

		recv_msg->report_id = HID_REPORT_ID;
		g_usleep(i * 5000);
		if (!fu_nordic_hid_cfg_channel_receive(self,
						       (guint8 *)recv_msg,
						       sizeof(*recv_msg),
						       error)) {
			return FALSE;
		}
		if (recv_msg->data_len != 0x0F) {
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_NOT_SUPPORTED,
					    "incorrect length of reply");
			return FALSE;
		}
		if (recv_msg->data[0] == DFU_STATE_INACTIVE ||
		    recv_msg->data[0] == DFU_STATE_ACTIVE) {
			break;
		}
	}

	if (recv_msg->data[0] != args->status) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "sync received status: 0x%02x, expected: 0x%02x",
			    recv_msg->data[0],
			    args->status);
		return FALSE;
	}

	return fu_memcpy_safe(args->buf,
			      args->bufsz,
			      0,
			      (guint8 *)recv_msg,
			      sizeof(*recv_msg),
			      0,
			      sizeof(*recv_msg),
			      error);
}

static gboolean
fu_nordic_hid_cfg_channel_dfu_sync(FuNordicHidCfgChannel *self,
				   FuNordicCfgChannelDfuInfo *dfu_info,
				   guint8 expecting_state,
				   GError **error)
{
	FuNordicCfgChannelRcvHelper helper;
	g_autoptr(FuNordicCfgChannelMsg) res = g_new0(FuNordicCfgChannelMsg, 1);

	helper.status = expecting_state;
	helper.buf = (guint8 *)res;
	helper.bufsz = sizeof(*res);

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_nordic_hid_cfg_channel_dfu_sync_cb,
				  FU_NORDIC_HID_CFG_CHANNEL_RETRIES,
				  FU_NORDIC_HID_CFG_CHANNEL_DFU_RETRY_DELAY,
				  &helper,
				  error)) {
		g_prefix_error(error, "failed on dfu sync: ");
		return FALSE;
	}
	dfu_info->dfu_state = res->data[0];
	if (!fu_memread_uint32_safe(res->data,
				    REPORT_SIZE,
				    0x01,
				    &dfu_info->img_length,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint32_safe(res->data,
				    REPORT_SIZE,
				    0x05,
				    &dfu_info->img_csum,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint32_safe(res->data,
				    REPORT_SIZE,
				    0x09,
				    &dfu_info->offset,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint16_safe(res->data,
				    REPORT_SIZE,
				    0x0D,
				    &dfu_info->sync_buffer_size,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_nordic_hid_cfg_channel_dfu_start(FuNordicHidCfgChannel *self,
				    gsize img_length,
				    guint32 img_crc,
				    guint32 offset,
				    GError **error)
{
	guint8 data[REPORT_DATA_MAX_LEN] = {0};
	g_autoptr(FuNordicCfgChannelMsg) res = g_new0(FuNordicCfgChannelMsg, 1);

	/* sanity check */
	if (img_length > G_MAXUINT32) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "payload was too large");
		return FALSE;
	}

	if (!fu_memwrite_uint32_safe(data,
				     REPORT_DATA_MAX_LEN,
				     0x00,
				     (guint32)img_length,
				     G_LITTLE_ENDIAN,
				     error))
		return FALSE;
	if (!fu_memwrite_uint32_safe(data,
				     REPORT_DATA_MAX_LEN,
				     0x04,
				     img_crc,
				     G_LITTLE_ENDIAN,
				     error))
		return FALSE;
	if (!fu_memwrite_uint32_safe(data,
				     REPORT_DATA_MAX_LEN,
				     0x08,
				     offset,
				     G_LITTLE_ENDIAN,
				     error))
		return FALSE;

	if (!fu_nordic_hid_cfg_channel_cmd_send(self,
						"dfu",
						"start",
						CONFIG_STATUS_SET,
						data,
						0x0C,
						error))
		return FALSE;
	return fu_nordic_hid_cfg_channel_cmd_receive(self, CONFIG_STATUS_SUCCESS, res, error);
}

static gboolean
fu_nordic_hid_cfg_channel_probe(FuDevice *device, GError **error)
{
	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "hid", error);
}

static gboolean
fu_nordic_hid_cfg_channel_setup(FuDevice *device, GError **error)
{
	FuNordicHidCfgChannel *self = FU_NORDIC_HID_CFG_CHANNEL(device);

	/* get the board name */
	if (!fu_nordic_hid_cfg_channel_get_board_name(self, error))
		return FALSE;
	/* detect available modules first */
	if (!fu_nordic_hid_cfg_channel_get_modinfo(self, error))
		return FALSE;
	/* detect bootloader type */
	if (!fu_nordic_hid_cfg_channel_get_bl_name(self, error))
		return FALSE;
	/* set the physical id based on name, HW id and bootloader type of the board
	 * to detect if the device is connected via several interfaces */
	if (!fu_nordic_hid_cfg_channel_get_hwid(self, error))
		return FALSE;
	/* get device info and version */
	if (!fu_nordic_hid_cfg_channel_dfu_fwinfo(self, error))
		return FALSE;
	/* check if any peer is connected via this device */
	if (!fu_nordic_hid_cfg_channel_add_peers(self, error))
		return FALSE;

	/* generate the custom visible name for the device if absent */
	if (fu_device_get_name(device) == NULL) {
		const gchar *physical_id = NULL;
		physical_id = fu_device_get_physical_id(device);
		fu_device_set_name(device, physical_id);
	}

	/* generate IDs */
	fu_device_add_instance_strsafe(device, "BOARD", self->board_name);
	fu_device_add_instance_strsafe(device, "BL", self->bl_name);
	return fu_device_build_instance_id(device,
					   error,
					   "HIDRAW",
					   "VEN",
					   "DEV",
					   "BOARD",
					   "BL",
					   NULL);
}

static void
fu_nordic_hid_cfg_channel_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 97, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static void
fu_nordic_hid_cfg_channel_module_to_string(FuNordicCfgChannelModule *mod, guint idt, GString *str)
{
	for (guint i = 0; i < mod->options->len; i++) {
		FuNordicCfgChannelModuleOption *opt = g_ptr_array_index(mod->options, i);
		g_autofree gchar *title = g_strdup_printf("Option%02x", i);
		fu_string_append(str, idt, title, opt->name);
	}
}

static void
fu_nordic_hid_cfg_channel_to_string(FuDevice *device, guint idt, GString *str)
{
	FuNordicHidCfgChannel *self = FU_NORDIC_HID_CFG_CHANNEL(device);
	fu_string_append(str, idt, "BoardName", self->board_name);
	fu_string_append(str, idt, "Bootloader", self->bl_name);
	fu_string_append_kx(str, idt, "FlashAreaId", self->flash_area_id);
	fu_string_append_kx(str, idt, "FlashedImageLen", self->flashed_image_len);
	fu_string_append_kx(str, idt, "PeerId", self->peer_id);
	for (guint i = 0; i < self->modules->len; i++) {
		FuNordicCfgChannelModule *mod = g_ptr_array_index(self->modules, i);
		g_autofree gchar *title = g_strdup_printf("Module%02x", i);
		fu_string_append(str, idt, title, mod->name);
		fu_nordic_hid_cfg_channel_module_to_string(mod, idt + 1, str);
	}
}

static gboolean
fu_nordic_hid_cfg_channel_write_firmware_chunk(FuNordicHidCfgChannel *self,
					       FuChunk *chk,
					       gboolean is_last,
					       GError **error)
{
	guint32 chunk_len;
	guint32 offset = 0;
	guint8 sync_state = DFU_STATE_ACTIVE;
	g_autoptr(FuNordicCfgChannelDfuInfo) dfu_info = g_new0(FuNordicCfgChannelDfuInfo, 1);

	chunk_len = fu_chunk_get_data_sz(chk);
	while (offset < chunk_len) {
		guint8 data_len;
		guint8 data[REPORT_DATA_MAX_LEN] = {0};
		g_autoptr(FuNordicCfgChannelMsg) res = g_new0(FuNordicCfgChannelMsg, 1);

		data_len = ((offset + REPORT_DATA_MAX_LEN) < chunk_len)
			       ? REPORT_DATA_MAX_LEN
			       : (guint8)(chunk_len - offset);

		if (!fu_memcpy_safe(data,
				    REPORT_DATA_MAX_LEN,
				    0,
				    fu_chunk_get_data(chk),
				    chunk_len,
				    offset,
				    data_len,
				    error)) {
			return FALSE;
		}

		if (!fu_nordic_hid_cfg_channel_cmd_send(self,
							"dfu",
							"data",
							CONFIG_STATUS_SET,
							data,
							data_len,
							error))
			return FALSE;
		if (!fu_nordic_hid_cfg_channel_cmd_receive(self, CONFIG_STATUS_SUCCESS, res, error))
			return FALSE;

		offset += data_len;
	}

	/* sync should return inactive for the last chunk */
	if (is_last)
		sync_state = DFU_STATE_INACTIVE;
	return fu_nordic_hid_cfg_channel_dfu_sync(self, dfu_info, sync_state, error);
}

static gboolean
fu_nordic_hid_cfg_channel_write_firmware_blob(FuNordicHidCfgChannel *self,
					      GBytes *blob,
					      FuProgress *progress,
					      GError **error)
{
	g_autoptr(FuNordicCfgChannelDfuInfo) dfu_info = g_new0(FuNordicCfgChannelDfuInfo, 1);
	g_autoptr(GPtrArray) chunks = NULL;

	if (!fu_nordic_hid_cfg_channel_dfu_sync(self, dfu_info, DFU_STATE_ACTIVE, error))
		return FALSE;

	chunks = fu_chunk_array_new_from_bytes(blob, 0, 0, dfu_info->sync_buffer_size);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);

	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		gboolean is_last = (i == chunks->len - 1);
		if (!fu_nordic_hid_cfg_channel_write_firmware_chunk(self, chk, is_last, error)) {
			g_prefix_error(error, "chunk %u: ", fu_chunk_get_idx(chk));
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_nordic_hid_cfg_channel_write_firmware(FuDevice *device,
					 FuFirmware *firmware,
					 FuProgress *progress,
					 FwupdInstallFlags flags,
					 GError **error)
{
	FuNordicHidCfgChannel *self = FU_NORDIC_HID_CFG_CHANNEL(device);
	guint32 checksum;
	g_autofree gchar *csum_str = NULL;
	g_autofree gchar *image_id = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuNordicCfgChannelDfuInfo) dfu_info = g_new0(FuNordicCfgChannelDfuInfo, 1);

	/* select correct firmware per target board, bootloader and bank */
	image_id =
	    g_strdup_printf("%s_%s_bank%01u", self->board_name, self->bl_name, self->flash_area_id);
	firmware = fu_firmware_get_image_by_id(firmware, image_id, error);
	if (firmware == NULL)
		return FALSE;

	/* explicitly request a custom checksum calculation */
	csum_str = fu_firmware_get_checksum(firmware, -1, error);
	if (csum_str == NULL)
		return FALSE;
	/* expecting checksum string in hex */
	checksum = g_ascii_strtoull(csum_str, NULL, 16);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);

	/* TODO: check if there is unfinished operation before? */
	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;
	if (!fu_nordic_hid_cfg_channel_dfu_sync(self, dfu_info, DFU_STATE_INACTIVE, error))
		return FALSE;
	if (!fu_nordic_hid_cfg_channel_dfu_start(self,
						 g_bytes_get_size(blob),
						 checksum,
						 0x0 /* offset */,
						 error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write */
	if (!fu_nordic_hid_cfg_channel_write_firmware_blob(self,
							   blob,
							   fu_progress_get_child(progress),
							   error))
		return FALSE;
	fu_progress_step_done(progress);

	/* attach */
	if (!fu_nordic_hid_cfg_channel_dfu_reboot(self, error))
		return FALSE;
	fu_progress_step_done(progress);
	return TRUE;
}

static gboolean
fu_nordic_hid_cfg_channel_set_quirk_kv(FuDevice *device,
				       const gchar *key,
				       const gchar *value,
				       GError **error)
{
	FuNordicHidCfgChannel *self = FU_NORDIC_HID_CFG_CHANNEL(device);

	if (g_strcmp0(key, "NordicHidBootloader") == 0) {
		if (g_strcmp0(value, "B0") == 0)
			self->bl_name = g_strdup("B0");
		else if (g_strcmp0(value, "MCUBOOT") == 0)
			self->bl_name = g_strdup("MCUBOOT");
		else {
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_DATA,
					    "must be 'B0' or 'MCUBOOT'");
			return FALSE;
		}
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "quirk key not supported");
	return FALSE;
}

static void
fu_nordic_hid_cfg_channel_finalize(GObject *object)
{
	FuNordicHidCfgChannel *self = FU_NORDIC_HID_CFG_CHANNEL(object);
	g_free(self->board_name);
	g_free(self->bl_name);
	g_ptr_array_unref(self->modules);
	G_OBJECT_CLASS(fu_nordic_hid_cfg_channel_parent_class)->finalize(object);
}

static void
fu_nordic_hid_cfg_channel_class_init(FuNordicHidCfgChannelClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	klass_device->probe = fu_nordic_hid_cfg_channel_probe;
	klass_device->set_progress = fu_nordic_hid_cfg_channel_set_progress;
	klass_device->set_quirk_kv = fu_nordic_hid_cfg_channel_set_quirk_kv;
	klass_device->setup = fu_nordic_hid_cfg_channel_setup;
	klass_device->to_string = fu_nordic_hid_cfg_channel_to_string;
	klass_device->write_firmware = fu_nordic_hid_cfg_channel_write_firmware;
	object_class->finalize = fu_nordic_hid_cfg_channel_finalize;
}

static void
fu_nordic_hid_cfg_channel_init(FuNordicHidCfgChannel *self)
{
	self->modules =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_nordic_hid_cfg_channel_module_free);

	fu_device_set_vendor(FU_DEVICE(self), "Nordic");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_add_protocol(FU_DEVICE(self), "com.nordic.hidcfgchannel");
	fu_device_retry_set_delay(FU_DEVICE(self), FU_NORDIC_HID_CFG_CHANNEL_RETRY_DELAY);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_NORDIC_HID_ARCHIVE);
}

FuNordicHidCfgChannel *
fu_nordic_hid_cfg_channel_new(guint8 id)
{
	FuNordicHidCfgChannel *self = g_object_new(FU_TYPE_NORDIC_HID_CFG_CHANNEL, NULL);
	self->peer_id = id;
	return self;
}
