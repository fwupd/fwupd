/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-xy-kbms-common.h"
#include "fu-xy-kbms-config.h"
#include "fu-xy-kbms-device.h"
#include "fu-xy-kbms-firmware.h"
#include "fu-xy-kbms-struct.h"

#define FU_XY_KBMS_DEVICE_TIMEOUT_MS 2000

#define FU_XY_KBMS_DEVICE_RETRIES_CMD  3
#define FU_XY_KBMS_DEVICE_RETRIES_BULK 6
#define FU_XY_KBMS_DEVICE_FILL_BYTE    0xFD

#define FU_XY_KBMS_DEVICE_USAGE_PAGE_VENDOR 0xFF00

/* the device does a USB reset in place when switching mode, rather than re-enumerating */
#define FU_XY_KBMS_DEVICE_RESET_DELAY 2000 /* ms */

struct _FuXyKbmsDevice {
	FuHidrawDevice parent_instance;
	FuXyKbmsBoardType board_type;
};

G_DEFINE_TYPE(FuXyKbmsDevice, fu_xy_kbms_device, FU_TYPE_HIDRAW_DEVICE)

static void
fu_xy_kbms_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuXyKbmsDevice *self = FU_XY_KBMS_DEVICE(device);
	fwupd_codec_string_append(str,
				  idt,
				  "BoardType",
				  fu_xy_kbms_board_type_to_string(self->board_type));
}

static gboolean
fu_xy_kbms_device_send(FuXyKbmsDevice *self, GByteArray *buf, GError **error)
{
	/* drop any reply that arrived after a previous timeout */
	return fu_hidraw_device_set_report(FU_HIDRAW_DEVICE(self),
					   buf->data,
					   buf->len,
					   FU_IO_CHANNEL_FLAG_FLUSH_INPUT,
					   error);
}

static GByteArray *
fu_xy_kbms_device_recv(FuXyKbmsDevice *self, GError **error)
{
	g_autoptr(GTimer) timer = g_timer_new();

	while (g_timer_elapsed(timer, NULL) * 1000.f < FU_XY_KBMS_DEVICE_TIMEOUT_MS) {
		g_autoptr(GByteArray) buf = NULL;

		/* the consumer-control collection uses report ID 1 on the same interface, so the
		 * report size varies and fu_hidraw_device_get_report() cannot be used */
		buf = fu_udev_device_read_byte_array(FU_UDEV_DEVICE(self),
						     64,
						     FU_XY_KBMS_DEVICE_TIMEOUT_MS,
						     FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
						     error);
		if (buf == NULL)
			return NULL;
		if (buf->len < 1 || buf->data[0] != FU_XY_KBMS_REPORT_ID_VENDOR)
			continue;
		fu_dump_raw(G_LOG_DOMAIN, "GetReport", buf->data, buf->len);

		/* a reply of the fill byte means the device was not ready */
		fu_byte_array_set_size(buf,
				       FU_STRUCT_XY_KBMS_CMD_RES_SIZE,
				       FU_XY_KBMS_DEVICE_FILL_BYTE);
		return g_steal_pointer(&buf);
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_TIMED_OUT,
			    "timed out waiting for vendor report");
	return NULL;
}

typedef struct {
	GByteArray *req; /* not owned */
	GByteArray *res;
} FuXyKbmsDeviceTransferHelper;

static gboolean
fu_xy_kbms_device_transfer_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuXyKbmsDevice *self = FU_XY_KBMS_DEVICE(device);
	FuXyKbmsDeviceTransferHelper *helper = (FuXyKbmsDeviceTransferHelper *)user_data;

	if (!fu_xy_kbms_device_send(self, helper->req, error))
		return FALSE;
	helper->res = fu_xy_kbms_device_recv(self, error);
	return helper->res != NULL;
}

/* send a request and wait for a reply, resending if required */
static GByteArray *
fu_xy_kbms_device_transfer(FuXyKbmsDevice *self, GByteArray *req, guint retries, GError **error)
{
	FuXyKbmsDeviceTransferHelper helper = {.req = req};
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_xy_kbms_device_transfer_cb,
				  retries,
				  0,
				  &helper,
				  error))
		return NULL;
	return helper.res;
}

typedef struct {
	FuXyKbmsCmdKind kind;
	guint8 cmd;
	GByteArray *data; /* no echo check if set */
} FuXyKbmsDeviceCmdHelper;

static gboolean
fu_xy_kbms_device_cmd_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuXyKbmsDevice *self = FU_XY_KBMS_DEVICE(device);
	FuXyKbmsDeviceCmdHelper *helper = (FuXyKbmsDeviceCmdHelper *)user_data;
	g_autoptr(FuStructXyKbmsCmdReq) st_req = fu_struct_xy_kbms_cmd_req_new();
	g_autoptr(FuStructXyKbmsCmdRes) st_res = NULL;
	g_autoptr(GByteArray) buf = NULL;

	fu_struct_xy_kbms_cmd_req_set_kind(st_req, helper->kind);
	fu_struct_xy_kbms_cmd_req_set_cmd(st_req, helper->cmd);
	buf = fu_xy_kbms_device_transfer(self, st_req->buf, 1, error);
	if (buf == NULL)
		return FALSE;
	st_res = fu_struct_xy_kbms_cmd_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;

	/* the reply has no header, so it cannot be checked */
	if (helper->data != NULL) {
		g_byte_array_append(helper->data, buf->data + 1, buf->len - 1);
		return TRUE;
	}

	/* control commands echo the kind and command, data commands echo just the command */
	if (helper->kind == FU_XY_KBMS_CMD_KIND_CTRL) {
		if (fu_struct_xy_kbms_cmd_res_get_kind(st_res) == helper->kind &&
		    fu_struct_xy_kbms_cmd_res_get_cmd(st_res) == helper->cmd)
			return TRUE;
	} else {
		if (fu_struct_xy_kbms_cmd_res_get_kind(st_res) == helper->cmd)
			return TRUE;
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_BUSY,
		    "invalid reply for command 0x%02x, got 0x%02x 0x%02x; "
		    "unplug and replug the keyboard, then retry",
		    helper->cmd,
		    fu_struct_xy_kbms_cmd_res_get_kind(st_res),
		    fu_struct_xy_kbms_cmd_res_get_cmd(st_res));
	return FALSE;
}

/* "02 3f <cmd>" -> "02 3f <cmd>" */
static gboolean
fu_xy_kbms_device_ctrl(FuXyKbmsDevice *self, FuXyKbmsCtrlCmd cmd, GError **error)
{
	FuXyKbmsDeviceCmdHelper helper = {.kind = FU_XY_KBMS_CMD_KIND_CTRL, .cmd = cmd};
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_xy_kbms_device_cmd_cb,
				  FU_XY_KBMS_DEVICE_RETRIES_CMD,
				  0,
				  &helper,
				  error)) {
		g_prefix_error(error, "failed to %s: ", fu_xy_kbms_ctrl_cmd_to_string(cmd));
		return FALSE;
	}
	return TRUE;
}

/* "02 3e <cmd>" -> "02 <cmd>", or "02 <data>" if @data is set */
static gboolean
fu_xy_kbms_device_data(FuXyKbmsDevice *self, FuXyKbmsDataCmd cmd, GByteArray *data, GError **error)
{
	FuXyKbmsDeviceCmdHelper helper = {.kind = FU_XY_KBMS_CMD_KIND_DATA,
					  .cmd = cmd,
					  .data = data};
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_xy_kbms_device_cmd_cb,
				  FU_XY_KBMS_DEVICE_RETRIES_CMD,
				  0,
				  &helper,
				  error)) {
		g_prefix_error(error, "failed to %s: ", fu_xy_kbms_data_cmd_to_string(cmd));
		return FALSE;
	}
	return TRUE;
}

/* an ACK means continue, anything else is the block the device wants next */
static gboolean
fu_xy_kbms_device_write_blocks(FuXyKbmsDevice *self,
			       GBytes *blob,
			       guint16 idx_end_flags,
			       FuProgress *progress,
			       GError **error)
{
	guint16 idx = 0;
	guint16 idx_done = 0;
	guint16 nblocks;
	g_autoptr(FuChunkArray) chunks = NULL;

	chunks = fu_chunk_array_new_from_bytes(blob,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       FU_STRUCT_XY_KBMS_BLOCK_REQ_SIZE_DATA,
					       error);
	if (chunks == NULL)
		return FALSE;
	nblocks = fu_chunk_array_length(chunks);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, nblocks);

	/* allow the device to rewind a few times, but not forever */
	for (guint i = 0; i < (guint)nblocks * 4; i++) {
		g_autoptr(FuStructXyKbmsBlockReq) st_req = fu_struct_xy_kbms_block_req_new();
		g_autoptr(FuChunk) chunk = NULL;
		g_autoptr(FuStructXyKbmsBlockRes) st_res = NULL;
		g_autoptr(GByteArray) res = NULL;

		/* end of transfer, which has no reply */
		if (idx >= nblocks) {
			guint8 data[FU_STRUCT_XY_KBMS_BLOCK_REQ_SIZE_DATA] = {0x0};
			memset(data, 0xFF, sizeof(data));
			fu_struct_xy_kbms_block_req_set_idx(st_req, idx | idx_end_flags);
			if (!fu_struct_xy_kbms_block_req_set_data(st_req,
								  data,
								  sizeof(data),
								  error))
				return FALSE;
			return fu_xy_kbms_device_send(self, st_req->buf, error);
		}

		/* the device can ask for any block */
		chunk = fu_chunk_array_index(chunks, idx, error);
		if (chunk == NULL)
			return FALSE;
		fu_struct_xy_kbms_block_req_set_idx(st_req, idx);
		if (!fu_struct_xy_kbms_block_req_set_data(st_req,
							  fu_chunk_get_data(chunk),
							  fu_chunk_get_data_sz(chunk),
							  error))
			return FALSE;
		res = fu_xy_kbms_device_transfer(self,
						 st_req->buf,
						 FU_XY_KBMS_DEVICE_RETRIES_BULK,
						 error);
		if (res == NULL) {
			g_prefix_error(error, "failed to write block 0x%04x: ", idx);
			return FALSE;
		}
		st_res = fu_struct_xy_kbms_block_res_parse(res->data, res->len, 0x0, error);
		if (st_res == NULL)
			return FALSE;
		if (fu_struct_xy_kbms_block_res_get_idx(st_res) == FU_XY_KBMS_BLOCK_IDX_ACK)
			idx++;
		else
			idx = fu_struct_xy_kbms_block_res_get_idx(st_res);
		idx &= FU_XY_KBMS_BLOCK_IDX_MASK;

		/* this would send the end marker before all the blocks were written */
		if (idx > nblocks) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "device requested block 0x%04x, but only 0x%04x blocks",
				    idx,
				    nblocks);
			return FALSE;
		}

		/* the device can rewind, so only count each block once */
		for (; idx_done < idx; idx_done++)
			fu_progress_step_done(progress);
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "too many retries; enter BOOT again and retry");
	return FALSE;
}

GByteArray *
fu_xy_kbms_device_read_config(FuXyKbmsDevice *self, FuProgress *progress, GError **error)
{
	guint16 idx = 0;
	guint16 idx_done = 0;
	guint16 nblocks = FU_STRUCT_XY_KBMS_CONFIG_SIZE / FU_STRUCT_XY_KBMS_BLOCK_RES_SIZE_DATA;
	g_autoptr(FuStructXyKbmsBlockReq) st_end = fu_struct_xy_kbms_block_req_new();
	g_autoptr(GByteArray) buf = g_byte_array_new();

	g_return_val_if_fail(FU_IS_XY_KBMS_DEVICE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, nblocks);

	if (!fu_xy_kbms_device_ctrl(self, FU_XY_KBMS_CTRL_CMD_READ_CONFIG, error))
		return NULL;
	fu_byte_array_set_size(buf, FU_STRUCT_XY_KBMS_CONFIG_SIZE, 0xFF);
	for (guint i = 0; idx < nblocks; i++) {
		gsize data_len = 0;
		guint16 ridx;
		const guint8 *data;
		g_autoptr(FuStructXyKbmsBlockReq) st_req = fu_struct_xy_kbms_block_req_new();
		g_autoptr(FuStructXyKbmsBlockRes) st_res = NULL;
		g_autoptr(GByteArray) res = NULL;

		/* allow the device to rewind a few times, but not forever */
		if (i > (guint)nblocks * 4) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_READ,
					    "too many retries reading config");
			return NULL;
		}

		fu_struct_xy_kbms_block_req_set_idx(st_req, idx);
		res = fu_xy_kbms_device_transfer(self,
						 st_req->buf,
						 FU_XY_KBMS_DEVICE_RETRIES_BULK,
						 error);
		if (res == NULL) {
			g_prefix_error(error, "failed to read block 0x%04x: ", idx);
			return NULL;
		}
		st_res = fu_struct_xy_kbms_block_res_parse(res->data, res->len, 0x0, error);
		if (st_res == NULL)
			return NULL;
		ridx = fu_struct_xy_kbms_block_res_get_idx(st_res);
		data = fu_struct_xy_kbms_block_res_get_data(st_res, &data_len);
		if (!fu_memcpy_safe(buf->data,
				    buf->len,
				    (gsize)ridx * FU_STRUCT_XY_KBMS_BLOCK_RES_SIZE_DATA,
				    data,
				    data_len,
				    0x0,
				    FU_STRUCT_XY_KBMS_BLOCK_RES_SIZE_DATA,
				    error)) {
			g_prefix_error(error, "invalid block 0x%04x: ", ridx);
			return NULL;
		}
		idx = ridx + 1;

		/* the device can rewind, so only count each block once */
		for (; idx_done < idx; idx_done++)
			fu_progress_step_done(progress);
	}

	/* end of transfer, which has no reply */
	fu_struct_xy_kbms_block_req_set_idx(st_end, FU_XY_KBMS_BLOCK_IDX_END);
	if (!fu_xy_kbms_device_send(self, st_end->buf, error))
		return NULL;
	if (!fu_xy_kbms_config_validate(buf, error))
		return NULL;

	/* success */
	return g_steal_pointer(&buf);
}

gboolean
fu_xy_kbms_device_write_config(FuXyKbmsDevice *self,
			       GByteArray *buf,
			       FuProgress *progress,
			       GError **error)
{
	g_autoptr(GBytes) blob = NULL;

	g_return_val_if_fail(FU_IS_XY_KBMS_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* the device does not check this itself */
	if (!fu_xy_kbms_config_validate(buf, error))
		return FALSE;
	if (!fu_xy_kbms_device_ctrl(self, FU_XY_KBMS_CTRL_CMD_WRITE_CONFIG, error))
		return FALSE;

	/* unlike firmware, the end marker has no flags */
	blob = g_bytes_new(buf->data, buf->len);
	return fu_xy_kbms_device_write_blocks(self, blob, 0x0, progress, error);
}

static gboolean
fu_xy_kbms_device_ensure_version(FuXyKbmsDevice *self, GError **error)
{
	const gchar *months[] =
	    {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	guint64 day = 0;
	guint64 year = 0;
	g_autofree gchar *str = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_auto(GStrv) split = NULL;

	/* the bootloader treats every request as a firmware block, and replies with the block it
	 * wants next -- this is out of sequence and so nothing is written */
	if (!fu_xy_kbms_device_data(self, FU_XY_KBMS_DATA_CMD_VERSION, buf, error))
		return FALSE;
	if (buf->len >= 2 && buf->data[0] == 0x00 && buf->data[1] == 0x00) {
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		return TRUE;
	}
	fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	str = fu_strsafe((const gchar *)buf->data, buf->len);
	if (str == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "no version");
		return FALSE;
	}

	/* this is the date the firmware was built, e.g. "Jun 15 2023" */
	split = g_strsplit(str, " ", -1);
	if (g_strv_length(split) == 3 &&
	    fu_strtoull(split[1], &day, 1, 31, FU_INTEGER_BASE_10, NULL) &&
	    fu_strtoull(split[2], &year, 2000, 2099, FU_INTEGER_BASE_10, NULL)) {
		for (guint i = 0; i < G_N_ELEMENTS(months); i++) {
			if (g_strcmp0(split[0], months[i]) == 0) {
				g_autofree gchar *version = g_strdup_printf(
				    "%" G_GUINT64_FORMAT ".%02u.%02" G_GUINT64_FORMAT,
				    year,
				    i + 1,
				    day);
				fu_device_set_version(FU_DEVICE(self), version);
				return TRUE;
			}
		}
	}

	/* fall back to something unparsed */
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_version(FU_DEVICE(self), str);
	return TRUE;
}

static gboolean
fu_xy_kbms_device_ensure_board_type(FuXyKbmsDevice *self, GError **error)
{
	const gchar *name;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuStructXyKbmsConfig) st = NULL;
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_xy_kbms_device_read_config(self, progress, error);
	if (buf == NULL)
		return FALSE;
	st = fu_struct_xy_kbms_config_parse(buf->data, buf->len, 0x0, error);
	if (st == NULL)
		return FALSE;
	self->board_type = fu_struct_xy_kbms_config_get_board_type(st);
	name = fu_xy_kbms_board_type_to_name(self->board_type);
	if (name != NULL)
		fu_device_set_summary(FU_DEVICE(self), name);
	return TRUE;
}

static gboolean
fu_xy_kbms_device_setup(FuDevice *device, GError **error)
{
	FuXyKbmsDevice *self = FU_XY_KBMS_DEVICE(device);
	g_autoptr(FuHidDescriptor) descriptor = NULL;
	g_autoptr(FuHidReport) report = NULL;
	g_autoptr(FuXyKbmsConfig) config = NULL;

	/* FuHidrawDevice->setup */
	if (!FU_DEVICE_CLASS(fu_xy_kbms_device_parent_class)->setup(device, error))
		return FALSE;

	/* the keyboard and mouse interfaces do not have the vendor collection */
	descriptor = fu_hidraw_device_parse_descriptor(FU_HIDRAW_DEVICE(device), error);
	if (descriptor == NULL)
		return FALSE;
	report = fu_hid_descriptor_find_report(descriptor,
					       NULL,
					       "usage-page",
					       FU_XY_KBMS_DEVICE_USAGE_PAGE_VENDOR,
					       "report-id",
					       FU_XY_KBMS_REPORT_ID_VENDOR,
					       NULL);
	if (report == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no vendor collection");
		return FALSE;
	}
	if (!fu_xy_kbms_device_ensure_version(self, error))
		return FALSE;

	/* reading the config in BOOT mode would be treated as a firmware write */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;
	if (!fu_xy_kbms_device_ensure_board_type(self, error))
		return FALSE;

	/* the pinmap is written using the same interface */
	config = fu_xy_kbms_config_new(device);
	fu_device_add_child(device, FU_DEVICE(config));

	/* success */
	return TRUE;
}

static gboolean
fu_xy_kbms_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuXyKbmsDevice *self = FU_XY_KBMS_DEVICE(device);

	/* sanity check */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in bootloader mode, skipping");
		return TRUE;
	}

	/* the device stays in BOOT mode until it is written or power cycled */
	if (!fu_xy_kbms_device_data(self, FU_XY_KBMS_DATA_CMD_ENTER_BOOT, NULL, error))
		return FALSE;
	fu_device_sleep_full(device, FU_XY_KBMS_DEVICE_RESET_DELAY, progress);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static gboolean
fu_xy_kbms_device_write_firmware(FuDevice *device,
				 FuFirmware *firmware,
				 FuProgress *progress,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuXyKbmsDevice *self = FU_XY_KBMS_DEVICE(device);
	g_autoptr(GBytes) blob = NULL;

	/* the image is encrypted, and the bootloader decrypts it */
	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;
	if (!fu_xy_kbms_device_write_blocks(self,
					    blob,
					    FU_XY_KBMS_BLOCK_IDX_FLAG_FIRMWARE_END,
					    progress,
					    error))
		return FALSE;

	/* the device resets itself into runtime mode after the end marker */
	fu_device_sleep(device, FU_XY_KBMS_DEVICE_RESET_DELAY);
	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static gboolean
fu_xy_kbms_device_reload(FuDevice *device, GError **error)
{
	FuXyKbmsDevice *self = FU_XY_KBMS_DEVICE(device);
	return fu_xy_kbms_device_ensure_version(self, error);
}

static void
fu_xy_kbms_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "reload");
}

static void
fu_xy_kbms_device_init(FuXyKbmsDevice *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_protocol(FU_DEVICE(self), "com.xy.kbms");
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_INPUT_KEYBOARD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ENFORCE_REQUIRES);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_XY_KBMS_FIRMWARE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_NONBLOCK);
}

static void
fu_xy_kbms_device_class_init(FuXyKbmsDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_xy_kbms_device_to_string;
	device_class->setup = fu_xy_kbms_device_setup;
	device_class->detach = fu_xy_kbms_device_detach;
	device_class->reload = fu_xy_kbms_device_reload;
	device_class->write_firmware = fu_xy_kbms_device_write_firmware;
	device_class->set_progress = fu_xy_kbms_device_set_progress;
}
