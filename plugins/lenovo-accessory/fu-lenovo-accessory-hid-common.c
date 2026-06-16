/*
 * Copyright 2026 Yuchao Li <liyc44@lenovo.com>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lenovo-accessory-hid-child-device.h"
#include "fu-lenovo-accessory-hid-common.h"

#define FU_LENOVO_ACCESSORY_MAX_PAIR_SLOTS 6

static gboolean
fu_lenovo_accessory_hid_add_child(FuLenovoAccessoryImpl *self, guint8 target_slot, GError **error)
{
	guint16 pid = 0;
	guint8 bt_addr[FU_STRUCT_LENOVO_ACCESSORY_PAIR_SLOT_INFO_V2_RSP_SIZE_MAC_ADDR] = {0};
	g_autofree gchar *bt_name = NULL;
	g_autofree gchar *logical_id = NULL;
	g_autoptr(FuLenovoAccessoryHidChildDevice) child = NULL;

	child = fu_lenovo_accessory_hid_child_device_new(FU_DEVICE(self));
	if (child == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to create child device");
		return FALSE;
	}

	if (!fu_lenovo_accessory_impl_get_pair_slot_info_v2(self,
							    target_slot,
							    &pid,
							    bt_addr,
							    sizeof(bt_addr),
							    &bt_name,
							    error))
		return FALSE;
	fu_lenovo_accessory_hid_child_device_set_target_slot(child, target_slot);
	fu_lenovo_accessory_hid_child_device_set_pid(child, pid);

	logical_id = g_strdup_printf("slot%u", (guint)target_slot);
	fu_device_set_logical_id(FU_DEVICE(child), logical_id);

	fu_device_add_instance_u16(FU_DEVICE(child), "VID", fu_device_get_vid(FU_DEVICE(self)));
	fu_device_add_instance_u16(FU_DEVICE(child), "PID", pid);
	if (!fu_device_build_instance_id(FU_DEVICE(child), error, "BLUETOOTH", "VID", "PID", NULL))
		return FALSE;
	if (bt_name != NULL && bt_name[0] != '\0')
		fu_device_set_name(FU_DEVICE(child), bt_name);

	/* best-effort: a paired peripheral that fails to set up (e.g. it went to
	 * sleep) must not prevent the dongle itself from being updatable, so the
	 * caller skips the slot rather than failing setup */
	if (!fu_device_setup(FU_DEVICE(child), error))
		return FALSE;
	fu_device_add_child(FU_DEVICE(self), FU_DEVICE(child));
	return TRUE;
}

gboolean
fu_lenovo_accessory_hid_add_children(FuLenovoAccessoryImpl *self, GError **error)
{
	guint8 max_slot_num = 0;
	guint8 slot_status[FU_LENOVO_ACCESSORY_MAX_PAIR_SLOTS] = {0};
	g_autoptr(GError) error_local = NULL;
	g_return_val_if_fail(FU_IS_LENOVO_ACCESSORY_IMPL(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* best-effort: a dongle that cannot report its pairing list is still
	 * updatable itself, so skip child enumeration rather than failing setup */
	if (!fu_lenovo_accessory_impl_get_pair_support_info(self,
							    &max_slot_num,
							    slot_status,
							    G_N_ELEMENTS(slot_status),
							    &error_local)) {
		g_debug("skipping child enumeration: %s", error_local->message);
		return TRUE;
	}

	if (max_slot_num == 0)
		return TRUE;

	max_slot_num = MIN(max_slot_num, (guint8)G_N_ELEMENTS(slot_status));

	for (guint8 i = 0; i < max_slot_num; i++) {
		g_autoptr(GError) error_slot = NULL;

		if (slot_status[i] != FU_LENOVO_ACCESSORY_PAIR_SLOT_STATUS_CONNECTED)
			continue;
		if (!fu_lenovo_accessory_hid_add_child(self, i + 1, &error_slot))
			g_debug("skipping slot %u: %s", (guint)(i + 1), error_slot->message);
	}
	return TRUE;
}

GByteArray *
fu_lenovo_accessory_hid_read(FuLenovoAccessoryImpl *impl, GError **error)
{
	guint8 buf[FU_LENOVO_ACCESSORY_HID_BUFSZ] = {0x0};
	g_autoptr(GByteArray) buf_res = g_byte_array_new();

	/* GET_REPORT (Feature) over the interface-3 control endpoint; the
	 * report-id is carried in wValue, so the buffer is the raw 64-byte
	 * frame with no report-id prefix */
	if (!fu_hid_device_get_report(FU_HID_DEVICE(impl),
				      FU_LENOVO_ACCESSORY_HID_REPORT_ID,
				      buf,
				      sizeof(buf),
				      FU_LENOVO_ACCESSORY_HID_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error))
		return NULL;
	g_byte_array_append(buf_res, buf, sizeof(buf));
	return g_steal_pointer(&buf_res);
}

gboolean
fu_lenovo_accessory_hid_write(FuLenovoAccessoryImpl *impl, GByteArray *buf, GError **error)
{
	g_autoptr(GByteArray) buf_req = g_byte_array_new();

	/* SET_REPORT (Feature) over the interface-3 control endpoint; no
	 * report-id prefix, frame padded to the fixed 64-byte length */
	g_byte_array_append(buf_req, buf->data, buf->len);
	fu_byte_array_set_size(buf_req, FU_LENOVO_ACCESSORY_HID_BUFSZ, 0x00);
	return fu_hid_device_set_report(FU_HID_DEVICE(impl),
					FU_LENOVO_ACCESSORY_HID_REPORT_ID,
					buf_req->data,
					buf_req->len,
					FU_LENOVO_ACCESSORY_HID_TIMEOUT,
					FU_HID_DEVICE_FLAG_IS_FEATURE |
					    FU_HID_DEVICE_FLAG_RETRY_FAILURE,
					error);
}

#define FU_LENOVO_ACCESSORY_STALE_REWRITE_MAX 2

typedef struct {
	guint8 req_slot;
	guint8 req_cmd_class;
	guint8 req_cmd_id;
	guint stale_rewrites; /* number of times the request was re-issued */
	GByteArray *buf_req;  /* request to re-issue when a stale frame is read */
	GByteArray *buf_rsp;
} FuLenovoAccessoryPollHelper;

static gboolean
fu_lenovo_accessory_hid_poll_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuLenovoAccessoryPollHelper *helper = (FuLenovoAccessoryPollHelper *)user_data;
	GByteArray *buf_rsp = helper->buf_rsp;
	guint8 target_status;
	guint8 rsp_cmd_class;
	guint8 rsp_cmd_id;
	FuLenovoAccessoryStatus status;
	guint8 rsp_slot;
	gsize offset = 0x0;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = NULL;

	buf = fu_lenovo_accessory_hid_read(FU_LENOVO_ACCESSORY_IMPL(device), error);
	if (buf == NULL)
		return FALSE;
	st_cmd = fu_struct_lenovo_accessory_cmd_parse(buf->data, buf->len, offset, error);
	if (st_cmd == NULL)
		return FALSE;
	target_status = fu_struct_lenovo_accessory_cmd_get_target_status(st_cmd);

	/* the response slot must match the request slot, otherwise a frame
	 * for a different target leaked onto the CMD channel; retry the read */
	rsp_slot = target_status >> 4;
	if (rsp_slot != helper->req_slot) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
			    "slot mismatch: requested 0x%x, got 0x%x",
			    helper->req_slot,
			    rsp_slot);
		return FALSE;
	}

	/* the firmware can leave a frame from a previous command in the report
	 * buffer; the command_id carries the direction bit (e.g. GET 0x03 -> 0x83)
	 * so the answer must echo our class/id exactly. Detect this before the
	 * status check, as a stale frame may carry an unrelated status (e.g. a
	 * timeout from the previous command) that would otherwise be misread as a
	 * failure of the current command. Flag it so the caller re-issues the
	 * write rather than spinning on the same stale frame */
	rsp_cmd_class = fu_struct_lenovo_accessory_cmd_get_command_class(st_cmd);
	rsp_cmd_id = fu_struct_lenovo_accessory_cmd_get_command_id(st_cmd);
	if (rsp_cmd_class != helper->req_cmd_class || rsp_cmd_id != helper->req_cmd_id) {
		if (helper->stale_rewrites >= FU_LENOVO_ACCESSORY_STALE_REWRITE_MAX) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "stale frame after %u rewrites: requested class 0x%02x "
				    "id 0x%02x, got class 0x%02x id 0x%02x",
				    helper->stale_rewrites,
				    helper->req_cmd_class,
				    helper->req_cmd_id,
				    rsp_cmd_class,
				    rsp_cmd_id);
			return FALSE;
		}
		/* the buffer still holds the answer to a previous command; re-issue
		 * the request to flush it, then keep polling for the fresh answer */
		helper->stale_rewrites++;
		g_debug("stale frame (rewrite %u): requested class 0x%02x id 0x%02x, "
			"got class 0x%02x id 0x%02x",
			helper->stale_rewrites,
			helper->req_cmd_class,
			helper->req_cmd_id,
			rsp_cmd_class,
			rsp_cmd_id);
		if (!fu_lenovo_accessory_hid_write(FU_LENOVO_ACCESSORY_IMPL(device),
						   helper->buf_req,
						   error))
			return FALSE;
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "stale frame, rewrote");
		return FALSE;
	}

	status = target_status & 0x0F;
	if (status == FU_LENOVO_ACCESSORY_STATUS_COMMAND_BUSY) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "command busy");
		return FALSE;
	}
	if (status != FU_LENOVO_ACCESSORY_STATUS_COMMAND_SUCCESSFUL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "command failed with status 0x%02x",
			    status);
		return FALSE;
	}
	offset += FU_STRUCT_LENOVO_ACCESSORY_CMD_SIZE;

	/* success */
	return fu_byte_array_append_safe(buf_rsp,
					 buf->data,
					 buf->len,
					 offset,
					 buf->len - offset,
					 error);
}

GByteArray *
fu_lenovo_accessory_hid_process(FuLenovoAccessoryImpl *impl, GByteArray *buf, GError **error)
{
	guint8 req_slot = 0;
	g_autoptr(GByteArray) buf_rsp = g_byte_array_new();
	g_autoptr(FuStructLenovoAccessoryCmd) st_req = NULL;
	FuLenovoAccessoryPollHelper helper = {0};

	/* the high nibble of the first request byte selects the target slot */
	if (buf->len > 0) {
		if (!fu_memread_uint8_safe(buf->data, buf->len, 0x0, &req_slot, error))
			return NULL;
		req_slot >>= 4;
	}

	/* the command_class/command_id that the response must echo back; the
	 * command_id already carries the direction bit (e.g. GET 0x03 -> 0x83) so
	 * an exact match is expected */
	st_req = fu_struct_lenovo_accessory_cmd_parse(buf->data, buf->len, 0x0, error);
	if (st_req == NULL)
		return NULL;
	helper.req_slot = req_slot;
	helper.req_cmd_class = fu_struct_lenovo_accessory_cmd_get_command_class(st_req);
	helper.req_cmd_id = fu_struct_lenovo_accessory_cmd_get_command_id(st_req);
	helper.buf_req = buf;
	helper.buf_rsp = buf_rsp;

	if (!fu_lenovo_accessory_hid_write(impl, buf, error))
		return NULL;
	if (!fu_device_retry_full(FU_DEVICE(impl),
				  fu_lenovo_accessory_hid_poll_cb,
				  500, /* count */
				  10,  /* ms */
				  &helper,
				  error))
		return NULL;
	return g_steal_pointer(&buf_rsp);
}
