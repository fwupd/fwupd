/*
 * Copyright 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 * Copyright 2021 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-nordic-hid-archive.h"
#include "fu-nordic-hid-cfg-channel.h"

#define HID_REPORT_ID	     6
#define REPORT_SIZE	     30
#define REPORT_DATA_MAX_LEN  (REPORT_SIZE - 5)
#define HWID_LEN	     8
#define PEERS_CACHE_LEN	     16
#define END_OF_TRANSFER_CHAR 0x0a
#define INVALID_PEER_ID	     0xFF
#define SELF_PEER_ID	     0x00

#define FU_NORDIC_HID_CFG_CHANNEL_RETRIES	      10
#define FU_NORDIC_HID_CFG_CHANNEL_RETRY_DELAY	      50   /* ms */
#define FU_NORDIC_HID_CFG_CHANNEL_DFU_RETRY_DELAY     500  /* ms */
#define FU_NORDIC_HID_CFG_CHANNEL_PEERS_POLL_INTERVAL 2000 /* ms */

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
	CONFIG_STATUS_GET_PEERS_CACHE,
	CONFIG_STATUS_FAULT = 99,
} FuNordicCfgStatus;

typedef enum {
	DFU_STATE_INACTIVE,
	DFU_STATE_ACTIVE,
	DFU_STATE_STORING,
	DFU_STATE_CLEANING,
} FuNordicCfgSyncState;

typedef struct __attribute__((packed)) { /* nocheck:blocked */
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
	FuHidrawDevice parent_instance;
	gboolean dfu_support;
	gboolean peers_cache_support;
	guint8 peers_cache[PEERS_CACHE_LEN];
	gchar *board_name;
	gchar *bl_name;
	gchar *generation;
	guint16 vid;
	guint16 pid;
	guint8 flash_area_id;
	guint32 flashed_image_len;
	guint8 peer_id;
	FuUdevDevice *parent_udev;
	GPtrArray *modules; /* of FuNordicCfgChannelModule */
};

G_DEFINE_TYPE(FuNordicHidCfgChannel, fu_nordic_hid_cfg_channel, FU_TYPE_HIDRAW_DEVICE)

static FuNordicHidCfgChannel *
fu_nordic_hid_cfg_channel_new(guint8 id, FuNordicHidCfgChannel *parent);

static void
fu_nordic_hid_cfg_channel_module_option_free(FuNordicCfgChannelModuleOption *opt)
{
	g_free(opt->name);
	g_free(opt);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuNordicCfgChannelModuleOption,
			      fu_nordic_hid_cfg_channel_module_option_free);

static void
fu_nordic_hid_cfg_channel_module_free(FuNordicCfgChannelModule *mod)
{
	if (mod->options != NULL)
		g_ptr_array_unref(mod->options);
	g_free(mod->name);
	g_free(mod);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuNordicCfgChannelModule, fu_nordic_hid_cfg_channel_module_free);

static FuUdevDevice *
fu_nordic_hid_cfg_channel_get_udev_device(FuNordicHidCfgChannel *self, GError **error)
{
	/* ourselves */
	if (self->peer_id == SELF_PEER_ID)
		return FU_UDEV_DEVICE(self);

	/* parent */
	if (self->parent_udev == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no parent for peer 0x%02x",
			    self->peer_id);
		return NULL;
	}

	return self->parent_udev;
}

static gboolean
fu_nordic_hid_cfg_channel_send(FuNordicHidCfgChannel *self,
			       guint8 *buf,
			       gsize bufsz,
			       GError **error)
{
	FuUdevDevice *udev_device = fu_nordic_hid_cfg_channel_get_udev_device(self, error);
	if (udev_device == NULL)
		return FALSE;
	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(udev_device),
					    buf,
					    bufsz,
					    FU_IOCTL_FLAG_NONE,
					    error);
}

static gboolean
fu_nordic_hid_cfg_channel_receive(FuNordicHidCfgChannel *self,
				  guint8 *buf,
				  gsize bufsz,
				  GError **error)
{
	g_autoptr(FuNordicCfgChannelMsg) recv_msg = g_new0(FuNordicCfgChannelMsg, 1);
	FuUdevDevice *udev_device = fu_nordic_hid_cfg_channel_get_udev_device(self, error);
	if (udev_device == NULL)
		return FALSE;
	for (gint i = 1; i < 100; i++) {
		recv_msg->report_id = HID_REPORT_ID;
		recv_msg->recipient = self->peer_id;
		if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(udev_device),
						  (guint8 *)recv_msg,
						  sizeof(*recv_msg),
						  FU_IOCTL_FLAG_NONE,
						  error))
			return FALSE;
		/* if the device is busy it return 06 00 00 00 00 response */
		if (recv_msg->report_id == HID_REPORT_ID &&
		    (recv_msg->recipient | recv_msg->event_id | recv_msg->status |
		     recv_msg->data_len))
			break;
		fu_device_sleep(FU_DEVICE(self), 1); /* ms */
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

	fu_dump_raw(G_LOG_DOMAIN, "Received", buf, bufsz);
	/*
	 * [TODO]: Possibly add the report-id fix for Bluez versions < 5.56:
	 * https://github.com/bluez/bluez/commit/35a2c50437cca4d26ac6537ce3a964bb509c9b62
	 *
	 * See fu_pxi_ble_device_get_feature() in
	 * plugins/pixart-rf/fu-pxi-ble-device.c for an example.
	 */
	return TRUE;
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
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
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
fu_nordic_hid_cfg_channel_is_cached_peer_connected(guint8 peer_cache_val)
{
	return (peer_cache_val % 2) != 0;
}

static void
fu_nordic_hid_cfg_channel_check_children_update_pending_cb(FuDevice *device,
							   GParamSpec *pspec,
							   gpointer user_data)
{
	FuNordicHidCfgChannel *self = FU_NORDIC_HID_CFG_CHANNEL(user_data);
	GPtrArray *children = fu_device_get_children(FU_DEVICE(self));
	gboolean update_pending = FALSE;

	for (guint i = 0; i < children->len; i++) {
		FuDevice *peer = g_ptr_array_index(children, i);
		if (fu_device_has_private_flag(peer, FU_DEVICE_PRIVATE_FLAG_UPDATE_PENDING)) {
			update_pending = TRUE;
			break;
		}
	}
	if (update_pending) {
		fu_device_add_problem(FU_DEVICE(self), FWUPD_DEVICE_PROBLEM_UPDATE_PENDING);
	} else {
		fu_device_remove_problem(FU_DEVICE(self), FWUPD_DEVICE_PROBLEM_UPDATE_PENDING);
	}
}

static void
fu_nordic_hid_cfg_channel_add_peer(FuNordicHidCfgChannel *self, guint8 peer_id)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuNordicHidCfgChannel) peer = NULL;

	peer = fu_nordic_hid_cfg_channel_new(peer_id, self);

	/* ensure that the general quirk content for Nordic HID devices is applied */
	fu_device_add_instance_id_full(FU_DEVICE(peer),
				       "HIDRAW\\VEN_1915",
				       FU_DEVICE_INSTANCE_FLAG_QUIRKS);

	if (!fu_device_setup(FU_DEVICE(peer), &error_local)) {
		g_debug("failed to discover peer 0x%02x: %s", peer_id, error_local->message);
		return;
	}

	g_debug("peer 0x%02x discovered", peer_id);

	/* if any of the peripherals have a pending update, inhibit the dongle */
	g_signal_connect(FU_DEVICE(peer),
			 "notify::private-flags",
			 G_CALLBACK(fu_nordic_hid_cfg_channel_check_children_update_pending_cb),
			 self);

	fu_device_add_child(FU_DEVICE(self), FU_DEVICE(peer));
	/* prohibit to close parent's communication descriptor */
	fu_device_add_private_flag(FU_DEVICE(peer), FU_DEVICE_PRIVATE_FLAG_USE_PARENT_FOR_OPEN);
}

static void
fu_nordic_hid_cfg_channel_remove_peer(FuNordicHidCfgChannel *self, guint8 peer_id)
{
	GPtrArray *children = fu_device_get_children(FU_DEVICE(self));

	/* remove child device if already discovered */
	for (guint i = 0; i < children->len; i++) {
		FuDevice *child_dev = g_ptr_array_index(children, i);
		FuNordicHidCfgChannel *child = FU_NORDIC_HID_CFG_CHANNEL(child_dev);

		if (child->peer_id == peer_id) {
			fu_device_remove_child(FU_DEVICE(self), child_dev);
			break;
		}
	}
}

static void
fu_nordic_hid_cfg_channel_remove_disconnected_peers(FuNordicHidCfgChannel *self,
						    guint8 peers_cache[PEERS_CACHE_LEN])
{
	for (guint i = 0; i < PEERS_CACHE_LEN; i++) {
		guint8 peer_id = i + 1;

		if (peers_cache == NULL ||
		    !fu_nordic_hid_cfg_channel_is_cached_peer_connected(peers_cache[i])) {
			fu_nordic_hid_cfg_channel_remove_peer(self, peer_id);
			if (peers_cache != NULL)
				self->peers_cache[i] = peers_cache[i];
		}
	}
}

static gboolean
fu_nordic_hid_cfg_channel_index_peers_cmd(FuNordicHidCfgChannel *self,
					  gboolean *cmd_supported,
					  GError **error)
{
	g_autoptr(FuNordicCfgChannelMsg) res = g_new0(FuNordicCfgChannelMsg, 1);
	g_autoptr(GError) error_local = NULL;

	*cmd_supported = FALSE;

	if (!fu_nordic_hid_cfg_channel_cmd_send(self,
						NULL,
						NULL,
						CONFIG_STATUS_INDEX_PEERS,
						NULL,
						0,
						error)) {
		g_prefix_error(error, "INDEX_PEERS cmd_send failed: ");
		return FALSE;
	}

	if (fu_nordic_hid_cfg_channel_cmd_receive(self,
						  CONFIG_STATUS_DISCONNECTED,
						  res,
						  &error_local)) {
		/* forwarding configuration channel to peers not supported */
		return TRUE;
	}

	/* Peers available */
	if (!fu_nordic_hid_cfg_channel_cmd_receive(self, CONFIG_STATUS_SUCCESS, res, error)) {
		g_prefix_error(error, "INDEX_PEERS cmd_receive failed: ");
		return FALSE;
	}

	*cmd_supported = TRUE;
	return TRUE;
}

static gboolean
fu_nordic_hid_cfg_channel_get_next_peer_id_cmd(FuNordicHidCfgChannel *self,
					       guint8 *peer_id,
					       GError **error)
{
	g_autoptr(FuNordicCfgChannelMsg) res = g_new0(FuNordicCfgChannelMsg, 1);

	if (!fu_nordic_hid_cfg_channel_cmd_send(self,
						NULL,
						NULL,
						CONFIG_STATUS_GET_PEER,
						NULL,
						0,
						error)) {
		g_prefix_error(error, "GET_PEER cmd_send failed: ");
		return FALSE;
	}

	if (!fu_nordic_hid_cfg_channel_cmd_receive(self, CONFIG_STATUS_SUCCESS, res, error)) {
		g_prefix_error(error, "GET_PEER cmd_receive failed: ");
		return FALSE;
	}

	*peer_id = res->data[8];

	return TRUE;
}

static gboolean
fu_nordic_hid_cfg_channel_read_peers_cache_cmd(FuNordicHidCfgChannel *self,
					       gboolean *cmd_supported,
					       guint8 peers_cache[PEERS_CACHE_LEN],
					       GError **error)
{
	g_autoptr(FuNordicCfgChannelMsg) res = g_new0(FuNordicCfgChannelMsg, 1);
	g_autoptr(GError) error_local = NULL;

	*cmd_supported = FALSE;

	if (!fu_nordic_hid_cfg_channel_cmd_send(self,
						NULL,
						NULL,
						CONFIG_STATUS_GET_PEERS_CACHE,
						NULL,
						0,
						error)) {
		g_prefix_error(error, "GET_PEERS_CACHE cmd_send failed: ");
		return FALSE;
	}

	if (fu_nordic_hid_cfg_channel_cmd_receive(self,
						  CONFIG_STATUS_DISCONNECTED,
						  res,
						  &error_local)) {
		/* configuration channel peers cache not supported */
		return TRUE;
	}

	/* configuration channel peer caching available */
	if (!fu_nordic_hid_cfg_channel_cmd_receive(self, CONFIG_STATUS_SUCCESS, res, error)) {
		g_prefix_error(error, "GET_PEERS_CACHE cmd_receive failed: ");
		return FALSE;
	}

	if (!fu_memcpy_safe(peers_cache,
			    PEERS_CACHE_LEN,
			    0,
			    res->data,
			    PEERS_CACHE_LEN,
			    0,
			    PEERS_CACHE_LEN,
			    error))
		return FALSE;

	*cmd_supported = TRUE;

	return TRUE;
}

static gboolean
fu_nordic_hid_cfg_channel_update_peers(FuNordicHidCfgChannel *self,
				       guint8 peers_cache[PEERS_CACHE_LEN],
				       GError **error)
{
	gboolean peers_supported = FALSE;
	guint8 peer_id;
	guint cnt = 0;

	if (!fu_nordic_hid_cfg_channel_index_peers_cmd(self, &peers_supported, error))
		return FALSE;

	if (!peers_supported)
		return TRUE;

	/* a device that does not support peers caching, would drop all of the peers because it
	 * cannot determine if the previously discovered peer is still connected
	 */
	fu_nordic_hid_cfg_channel_remove_disconnected_peers(self, peers_cache);

	while (cnt++ <= 0xFF) {
		if (!fu_nordic_hid_cfg_channel_get_next_peer_id_cmd(self, &peer_id, error))
			return FALSE;

		/* end of the list */
		if (peer_id == INVALID_PEER_ID)
			break;

		g_debug("detected peer: 0x%02x", peer_id);

		if (peers_cache == NULL) {
			/* allow to properly discover dongles without peers cache support */
			fu_nordic_hid_cfg_channel_add_peer(self, peer_id);
		} else {
			guint8 idx = peer_id - 1;

			if (self->peers_cache[idx] != peers_cache[idx] &&
			    fu_nordic_hid_cfg_channel_is_cached_peer_connected(peers_cache[idx])) {
				fu_nordic_hid_cfg_channel_remove_peer(self, peer_id);
				fu_nordic_hid_cfg_channel_add_peer(self, peer_id);
				self->peers_cache[idx] = peers_cache[idx];
			}
		}
	}

	if (peer_id != INVALID_PEER_ID) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "too many peers detected");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_nordic_hid_cfg_channel_setup_peers(FuNordicHidCfgChannel *self, GError **error)
{
	gboolean peers_supported = FALSE;
	gboolean peers_cache_supported = FALSE;
	guint8 peers_cache[PEERS_CACHE_LEN] = {0x00};

	if (self->peer_id != SELF_PEER_ID) {
		/* device connected through dongle cannot support peers */
		return TRUE;
	}

	/* Send index peers command to a device before accessing peers cache. This is done to
	 * prevent assertion failure on peripheral with legacy firmware that enables debug logs.
	 */
	if (!fu_nordic_hid_cfg_channel_index_peers_cmd(self, &peers_supported, error))
		return FALSE;

	if (!peers_supported)
		return TRUE;

	if (!fu_nordic_hid_cfg_channel_read_peers_cache_cmd(self,
							    &peers_cache_supported,
							    peers_cache,
							    error))
		return FALSE;

	if (!peers_cache_supported) {
		if (!fu_nordic_hid_cfg_channel_update_peers(self, NULL, error))
			return FALSE;
	} else {
		if (!fu_nordic_hid_cfg_channel_update_peers(self, peers_cache, error))
			return FALSE;

		/* device must be kept open to allow polling */
		if (!fu_device_open(FU_DEVICE(self), error))
			return FALSE;

		/* mark device as supporting peers cache, ensure periodic polling for peers */
		self->peers_cache_support = TRUE;
		fu_device_set_poll_interval(FU_DEVICE(self),
					    FU_NORDIC_HID_CFG_CHANNEL_PEERS_POLL_INTERVAL);
	}

	return TRUE;
}

static gboolean
fu_nordic_hid_cfg_channel_poll_peers(FuDevice *device, GError **error)
{
	FuNordicHidCfgChannel *self = FU_NORDIC_HID_CFG_CHANNEL(device);
	gboolean peers_cache_supported = FALSE;
	guint8 peers_cache[PEERS_CACHE_LEN] = {0x00};

	if (!fu_nordic_hid_cfg_channel_read_peers_cache_cmd(self,
							    &peers_cache_supported,
							    peers_cache,
							    error))
		return FALSE;

	if (!self->peers_cache_support || !peers_cache_supported) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "unexpected poll of device without peers caching support");
		return FALSE;
	}

	/* skip update if not needed */
	if (!memcmp(self->peers_cache, peers_cache, PEERS_CACHE_LEN))
		return TRUE;

	if (!fu_nordic_hid_cfg_channel_update_peers(self, peers_cache, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_nordic_hid_cfg_channel_get_board_name_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuNordicHidCfgChannel *self = FU_NORDIC_HID_CFG_CHANNEL(device);
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
	self->board_name = fu_memstrsafe(res->data, res->data_len, 0x0, res->data_len, error);
	if (self->board_name == NULL)
		return FALSE;

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
		if (self->bl_name != NULL) {
			g_autofree gchar *tmp = g_strndup((const gchar *)res->data, res->data_len);

			g_debug("Bootloader readout '%s' overrides bootloader from quirk '%s'",
				tmp,
				self->bl_name);
			g_free(self->bl_name);
		}
		self->bl_name = fu_memstrsafe(res->data, res->data_len, 0x0, res->data_len, error);
		if (self->bl_name == NULL)
			return FALSE;
	} else {
		g_debug("the board has no support of bootloader runtime detection");
	}

	/* always use the bank 0 for MCUBOOT bootloader that swaps images */
	if (g_strcmp0(self->bl_name, "MCUBOOT") == 0)
		self->flash_area_id = 0;

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

static gboolean
fu_nordic_hid_cfg_channel_get_devinfo(FuNordicHidCfgChannel *self, GError **error)
{
	guint8 event_id = 0;
	g_autoptr(FuNordicCfgChannelMsg) res = g_new0(FuNordicCfgChannelMsg, 1);

	/* query for the devinfo if the board supports it */
	if (fu_nordic_hid_cfg_channel_get_event_id(self, "dfu", "devinfo", &event_id)) {
		gchar *generation;

		if (!fu_nordic_hid_cfg_channel_cmd_send(self,
							"dfu",
							"devinfo",
							CONFIG_STATUS_FETCH,
							NULL,
							0,
							error))
			return FALSE;
		if (!fu_nordic_hid_cfg_channel_cmd_receive(self, CONFIG_STATUS_SUCCESS, res, error))
			return FALSE;

		if (!fu_memread_uint16_safe(res->data,
					    REPORT_SIZE,
					    0x00,
					    &self->vid,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		if (!fu_memread_uint16_safe(res->data,
					    REPORT_SIZE,
					    0x02,
					    &self->pid,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		generation =
		    fu_memstrsafe(res->data, res->data_len, 0x4, res->data_len - 0x04, error);
		if (generation == NULL)
			return FALSE;

		/* check if not set via quirk */
		if (self->generation != NULL) {
			g_debug("generation readout '%s' overrides generation from quirk '%s'",
				generation,
				self->generation);
			g_free(self->generation);
		}
		self->generation = generation;
	} else {
		g_debug("the board has no support of devinfo runtime detection");
	}

	if (self->generation == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "the generation is not detected nor set via quirk");
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
	physical_id = g_strdup_printf("%s-%02x%02x%02x%02x%02x%02x%02x%02x",
				      self->board_name,
				      hw_id[0],
				      hw_id[1],
				      hw_id[2],
				      hw_id[3],
				      hw_id[4],
				      hw_id[5],
				      hw_id[6],
				      hw_id[7]);
	fu_device_set_physical_id(FU_DEVICE(self), physical_id);

	/* avoid inheriting name from the dongle */
	if (self->peer_id != SELF_PEER_ID)
		fu_device_set_name(FU_DEVICE(self), physical_id);

	/* success */
	return TRUE;
}

static gboolean
fu_nordic_hid_cfg_channel_load_module_opts(FuNordicHidCfgChannel *self,
					   FuNordicCfgChannelModule *mod,
					   GError **error)
{
	for (guint8 i = 0; i < 0xFF; i++) {
		g_autoptr(FuNordicCfgChannelModuleOption) opt = NULL;
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
		opt->name = fu_memstrsafe(res->data, res->data_len, 0x0, res->data_len, error);
		if (opt->name == NULL)
			return FALSE;
		opt->idx = i;
		g_ptr_array_add(mod->options, g_steal_pointer(&opt));
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
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
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
		fu_device_sleep(device, 2); /* ms */
		if (!fu_nordic_hid_cfg_channel_receive(self,
						       (guint8 *)recv_msg,
						       sizeof(*recv_msg),
						       error)) {
			return FALSE;
		}
		if (recv_msg->data_len != 0x0F) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
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
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
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
fu_nordic_hid_cfg_channel_generate_ids(FuNordicHidCfgChannel *self, GError **error)
{
	FuDevice *device = FU_DEVICE(self);

	/* generate IDs */
	fu_device_add_instance_strsafe(device, "BOARD", self->board_name);
	fu_device_add_instance_strsafe(device, "BL", self->bl_name);
	fu_device_add_instance_strsafe(device, "GEN", self->generation);

	/* If available, use VID and PID fetched in devinfo. Otherwise, use hardcoded VID and PID of
	 * 0x00 only for devices connected via dongle. This prevents from inheriting VID and PID of
	 * the dongle.
	 */
	if ((self->vid != 0x00 && self->pid != 0x00) || (self->peer_id != SELF_PEER_ID)) {
		fu_device_add_instance_u16(device, "VEN", self->vid);
		fu_device_add_instance_u16(device, "DEV", self->pid);
	}

	/* For the default generation, generate GUID without the generation parameter.
	 * Required for compatibility with already released application images.
	 */
	if (g_strcmp0(self->generation, "default") == 0) {
		if (!fu_device_build_instance_id(device,
						 error,
						 "HIDRAW",
						 "VEN",
						 "DEV",
						 "BOARD",
						 "BL",
						 NULL)) {
			g_prefix_error(error, "failed to add ID without generation: ");
			return FALSE;
		}
	}

	if (!fu_device_build_instance_id(device,
					 error,
					 "HIDRAW",
					 "VEN",
					 "DEV",
					 "BOARD",
					 "BL",
					 "GEN",
					 NULL)) {
		g_prefix_error(error, "failed to add complete ID: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_nordic_hid_cfg_channel_direct_discovery(FuNordicHidCfgChannel *self, GError **error)
{
	g_autoptr(GError) error_board_name = NULL;
	g_autoptr(GError) error_fwinfo = NULL;
	FuDevice *device = FU_DEVICE(self);

	/* Get the board name. The first configuration channel operation is used to check if
	 * hidraw instance supports the protocol. In case of failure, the hidraw instance is ignored
	 * and predefined error code is returned to suppress warning log. This is needed to properly
	 * handle hidraw instances that do not handle configuration channel requests. A device may
	 * not support configuration channel at all (no configuration channel HID feature report).
	 * The configuration channel requests are handled only by the first HID instance on device
	 * (other instances reject the configuration channel operations).
	 *
	 * If the HID device is connected over BLE, the configuration channel operations right after
	 * reconnection may fail with an ioctl error. Retry after a delay to ensure that the device
	 * will be properly recognized by the fwupd tool.
	 */
	if (!fu_device_retry_full(device,
				  fu_nordic_hid_cfg_channel_get_board_name_cb,
				  3,
				  50,
				  NULL,
				  &error_board_name)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Get board name failed: %s",
			    error_board_name->message);
		return FALSE;
	}

	/* set the physical id based on board name and HW id to detect if the device is connected
	 * via several interfaces
	 */
	if (!fu_nordic_hid_cfg_channel_get_hwid(self, error))
		return FALSE;
	/* detect available modules first */
	if (!fu_nordic_hid_cfg_channel_get_modinfo(self, error))
		return FALSE;

	/* generate the custom visible name for the device if absent */
	if (fu_device_get_name(device) == NULL) {
		const gchar *physical_id = NULL;
		physical_id = fu_device_get_physical_id(device);
		fu_device_set_name(device, physical_id);
	}

	/* get device info and version */
	if (!fu_nordic_hid_cfg_channel_dfu_fwinfo(self, &error_fwinfo))
		/* lack of firmware info support indicates that device does not support DFU. */
		return TRUE;

	/* detect bootloader type */
	if (!fu_nordic_hid_cfg_channel_get_bl_name(self, error))
		return FALSE;
	/* detect vendor ID, product ID and generation */
	if (!fu_nordic_hid_cfg_channel_get_devinfo(self, error))
		return FALSE;

	/* generate device IDs. */
	if (!fu_nordic_hid_cfg_channel_generate_ids(self, error))
		return FALSE;

	self->dfu_support = TRUE;
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);

	return TRUE;
}

static gboolean
fu_nordic_hid_cfg_channel_setup(FuDevice *device, GError **error)
{
	FuNordicHidCfgChannel *self = FU_NORDIC_HID_CFG_CHANNEL(device);

	if (!fu_nordic_hid_cfg_channel_direct_discovery(self, error))
		return FALSE;

	if (!fu_nordic_hid_cfg_channel_setup_peers(self, error))
		return FALSE;

	return TRUE;
}

static void
fu_nordic_hid_cfg_channel_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
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
		fwupd_codec_string_append(str, idt, title, opt->name);
	}
}

static void
fu_nordic_hid_cfg_channel_to_string(FuDevice *device, guint idt, GString *str)
{
	FuNordicHidCfgChannel *self = FU_NORDIC_HID_CFG_CHANNEL(device);

	fwupd_codec_string_append(str, idt, "BoardName", self->board_name);
	fwupd_codec_string_append_hex(str, idt, "PeerId", self->peer_id);
	fwupd_codec_string_append_hex(str, idt, "VendorId", self->vid);
	fwupd_codec_string_append_hex(str, idt, "ProductId", self->pid);

	if (self->dfu_support) {
		fwupd_codec_string_append(str, idt, "Bootloader", self->bl_name);
		fwupd_codec_string_append(str, idt, "Generation", self->generation);
		fwupd_codec_string_append_hex(str, idt, "FlashAreaId", self->flash_area_id);
		fwupd_codec_string_append_hex(str, idt, "FlashedImageLen", self->flashed_image_len);
	}

	for (guint i = 0; i < self->modules->len; i++) {
		FuNordicCfgChannelModule *mod = g_ptr_array_index(self->modules, i);
		g_autofree gchar *title = g_strdup_printf("Module%02x", i);
		fwupd_codec_string_append(str, idt, title, mod->name);
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
					      GInputStream *stream,
					      FuProgress *progress,
					      GError **error)
{
	g_autoptr(FuNordicCfgChannelDfuInfo) dfu_info = g_new0(FuNordicCfgChannelDfuInfo, 1);
	g_autoptr(FuChunkArray) chunks = NULL;

	if (!fu_nordic_hid_cfg_channel_dfu_sync(self, dfu_info, DFU_STATE_ACTIVE, error))
		return FALSE;

	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						dfu_info->sync_buffer_size,
						error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		gboolean is_last;
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		is_last = i == fu_chunk_array_length(chunks) - 1;
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
	gsize streamsz = 0;
	guint32 checksum;
	guint64 val = 0;
	g_autofree gchar *csum_str = NULL;
	g_autofree gchar *image_id = NULL;
	g_autoptr(GInputStream) stream = NULL;
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
	if (!fu_strtoull(csum_str, &val, 0, G_MAXUINT32, FU_INTEGER_BASE_16, error))
		return FALSE;
	checksum = (guint32)val;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);

	/* TODO: check if there is unfinished operation before? */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	if (!fu_nordic_hid_cfg_channel_dfu_sync(self, dfu_info, DFU_STATE_INACTIVE, error))
		return FALSE;
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (!fu_nordic_hid_cfg_channel_dfu_start(self, streamsz, checksum, 0x0 /* offset */, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write */
	if (!fu_nordic_hid_cfg_channel_write_firmware_blob(self,
							   stream,
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
		if (g_strcmp0(value, "B0") != 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "can be only 'B0' in quirk");
			return FALSE;
		}
		self->bl_name = g_strdup(value);
		return TRUE;
	}

	if (g_strcmp0(key, "NordicHidGeneration") == 0) {
		if (g_strcmp0(value, "default") != 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "can be only 'default' in quirk");
			return FALSE;
		}
		self->generation = g_strdup(value);
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_nordic_hid_cfg_channel_finalize(GObject *object)
{
	FuNordicHidCfgChannel *self = FU_NORDIC_HID_CFG_CHANNEL(object);
	g_free(self->board_name);
	g_free(self->bl_name);
	g_free(self->generation);
	g_ptr_array_unref(self->modules);
	G_OBJECT_CLASS(fu_nordic_hid_cfg_channel_parent_class)->finalize(object);
}

static void
fu_nordic_hid_cfg_channel_class_init(FuNordicHidCfgChannelClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	device_class->set_progress = fu_nordic_hid_cfg_channel_set_progress;
	device_class->set_quirk_kv = fu_nordic_hid_cfg_channel_set_quirk_kv;
	device_class->setup = fu_nordic_hid_cfg_channel_setup;
	device_class->poll = fu_nordic_hid_cfg_channel_poll_peers;
	device_class->to_string = fu_nordic_hid_cfg_channel_to_string;
	device_class->write_firmware = fu_nordic_hid_cfg_channel_write_firmware;
	object_class->finalize = fu_nordic_hid_cfg_channel_finalize;
}

static void
fu_nordic_hid_cfg_channel_init(FuNordicHidCfgChannel *self)
{
	self->modules =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_nordic_hid_cfg_channel_module_free);

	fu_device_set_vendor(FU_DEVICE(self), "Nordic");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_add_protocol(FU_DEVICE(self), "com.nordic.hidcfgchannel");
	fu_device_retry_set_delay(FU_DEVICE(self), FU_NORDIC_HID_CFG_CHANNEL_RETRY_DELAY);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_NORDIC_HID_ARCHIVE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
}

static FuNordicHidCfgChannel *
fu_nordic_hid_cfg_channel_new(guint8 id, FuNordicHidCfgChannel *parent)
{
	FuNordicHidCfgChannel *self = g_object_new(FU_TYPE_NORDIC_HID_CFG_CHANNEL,
						   "context",
						   fu_device_get_context(FU_DEVICE(parent)),
						   NULL);
	fu_device_incorporate(FU_DEVICE(self),
			      FU_DEVICE(parent),
			      FU_DEVICE_INCORPORATE_FLAG_BACKEND_ID);
	self->peer_id = id;
	self->parent_udev = FU_UDEV_DEVICE(parent);
	return self;
}
