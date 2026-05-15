/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lenovo-dock-device.h"
#include "fu-lenovo-dock-firmware.h"
#include "fu-lenovo-dock-image.h"
#include "fu-lenovo-dock-struct.h"

#define FU_LENOVO_DOCK_DEVICE_IFACE1_LEN 64
#define FU_LENOVO_DOCK_DEVICE_IFACE2_LEN 272

#define FU_LENOVO_DOCK_DEVICE_DELAY	 25    /* ms */
#define FU_LENOVO_DOCK_DEVICE_DELAY_LONG 200 /* ms */
#define FU_LENOVO_DOCK_DEVICE_RETRIES	 200
#define FU_LENOVO_DOCK_DEVICE_PHASE2_DELAY 30000 /* ms */
#define FU_LENOVO_DOCK_DEVICE_TIMEOUT	 250 /* ms */

#define FU_LENOVO_DOCK_DEVICE_USAGE_INFO_SIZE  (4 * FU_KB)
#define FU_LENOVO_DOCK_DEVICE_USAGE_INFO_START 0xFFF000

#define FU_LENOVO_DOCK_DEVICE_FLAG_CAN_PROVISION "can-provision"

struct _FuLenovoDockDevice {
	FuHidDevice parent_instance;
	guint16 image_pid;
	FuLenovoDockProvisionStatus provision_status;
	FuStructLenovoDockUsage *st_usage;
	GPtrArray *st_usage_items; /* element-type: FuStructLenovoDockUsageItem */
};

G_DEFINE_TYPE(FuLenovoDockDevice, fu_lenovo_dock_device, FU_TYPE_HID_DEVICE)

static void
fu_lenovo_dock_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuLenovoDockDevice *self = FU_LENOVO_DOCK_DEVICE(device);

	fwupd_codec_string_append_hex(str, idt, "ImagePid", self->image_pid);
	fwupd_codec_string_append(
	    str,
	    idt,
	    "ProvisionStatus",
	    fu_lenovo_dock_provision_status_to_string(self->provision_status));
	if (self->st_usage != NULL) {
		g_autofree gchar *tmp = fu_struct_lenovo_dock_usage_to_string(self->st_usage);
		fwupd_codec_string_append(str, idt, "Usage", tmp);
	}
	for (guint i = 0; i < self->st_usage_items->len; i++) {
		FuStructLenovoDockUsageItem *st_usage_item =
		    g_ptr_array_index(self->st_usage_items, i);
		g_autofree gchar *tmp = fu_struct_lenovo_dock_usage_item_to_string(st_usage_item);
		fwupd_codec_string_append(str, idt, "UsageItem", tmp);
	}
}

static FuStructLenovoDockUsageItem *
fu_lenovo_dock_device_get_usage_item(FuLenovoDockDevice *self,
				     FuLenovoDockComponentId component_id,
				     GError **error)
{
	for (guint i = 0; i < self->st_usage_items->len; i++) {
		FuStructLenovoDockUsageItem *st_usage_item =
		    g_ptr_array_index(self->st_usage_items, i);
		if (fu_struct_lenovo_dock_usage_item_get_component_id(st_usage_item) ==
		    component_id)
			return st_usage_item;
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_FOUND,
		    "cannot find component ID 0x%x",
		    component_id);
	return NULL;
}

static gboolean
fu_lenovo_dock_device_get_report1_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuLenovoDockDevice *self = FU_LENOVO_DOCK_DEVICE(device);
	FuLenovoDockStatus status;
	GByteArray *buf = (GByteArray *)user_data;
	g_autoptr(FuStructLenovoDockGenericRes) st = NULL;

	fu_hid_device_set_interface(FU_HID_DEVICE(self), 0x01);
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      buf->data[0],
				      buf->data,
				      buf->len,
				      FU_LENOVO_DOCK_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error))
		return FALSE;
	st = fu_struct_lenovo_dock_generic_res_parse(buf->data, buf->len, 0x0, error);
	if (st == NULL)
		return FALSE;
	status = fu_struct_lenovo_dock_generic_res_get_status(st);
	if (status == FU_LENOVO_DOCK_STATUS_BUSY) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "not ready");
		return FALSE;
	}
	if (status != FU_LENOVO_DOCK_STATUS_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "status was %s",
			    fu_lenovo_dock_status_to_string(status));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_lenovo_dock_device_get_report1(FuLenovoDockDevice *self, guint retry_delay, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	fu_byte_array_append_uint8(buf, 0x0);
	fu_byte_array_set_size(buf, FU_LENOVO_DOCK_DEVICE_IFACE1_LEN, 0x0);
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_lenovo_dock_device_get_report1_cb,
				  FU_LENOVO_DOCK_DEVICE_RETRIES,
				  retry_delay,
				  buf,
				  error))
		return NULL;

	/* success */
	return g_steal_pointer(&buf);
}

static gboolean
fu_lenovo_dock_device_get_report2_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuLenovoDockDevice *self = FU_LENOVO_DOCK_DEVICE(device);
	FuLenovoDockStatus status;
	GByteArray *buf = (GByteArray *)user_data;
	g_autoptr(FuStructLenovoDockGenericRes) st = NULL;

	fu_hid_device_set_interface(FU_HID_DEVICE(self), 0x02);
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      buf->data[0],
				      buf->data,
				      buf->len,
				      FU_LENOVO_DOCK_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error))
		return FALSE;
	if (buf->data[0] == FU_STRUCT_LENOVO_DOCK_NOTIFICATION_RES_DEFAULT_REPORT_ID) {
		g_autoptr(FuStructLenovoDockNotificationRes) st_notify =
		    fu_struct_lenovo_dock_notification_res_parse(buf->data, buf->len, 0x0, error);
		if (st_notify == NULL)
			return FALSE;
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
			    "ignoring notification event: %s",
			    fu_lenovo_dock_notification_event_to_string(
				fu_struct_lenovo_dock_notification_res_get_event(st_notify)));
		return FALSE;
	}
	st = fu_struct_lenovo_dock_generic_res_parse(buf->data, buf->len, 0x1, error);
	if (st == NULL)
		return FALSE;
	status = fu_struct_lenovo_dock_generic_res_get_status(st);
	if (status == FU_LENOVO_DOCK_STATUS_BUSY) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "not ready");
		return FALSE;
	}
	if (status != FU_LENOVO_DOCK_STATUS_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "status was %s",
			    fu_lenovo_dock_status_to_string(status));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_lenovo_dock_device_get_report2(FuLenovoDockDevice *self, guint retry_delay, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	fu_byte_array_append_uint8(buf, 0x10);
	fu_byte_array_set_size(buf, FU_LENOVO_DOCK_DEVICE_IFACE2_LEN, 0x0);
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_lenovo_dock_device_get_report2_cb,
				  FU_LENOVO_DOCK_DEVICE_RETRIES,
				  retry_delay,
				  buf,
				  error))
		return NULL;

	/* success */
	g_byte_array_remove_index(buf, 0);
	return g_steal_pointer(&buf);
}

static gboolean
fu_lenovo_dock_device_set_report1(FuLenovoDockDevice *self, GByteArray *buf, GError **error)
{
	g_autoptr(GByteArray) buf2 = g_byte_array_new();
	g_byte_array_append(buf2, buf->data, buf->len);
	fu_byte_array_set_size(buf2, FU_LENOVO_DOCK_DEVICE_IFACE1_LEN, 0x0);
	fu_hid_device_set_interface(FU_HID_DEVICE(self), 0x01);
	return fu_hid_device_set_report(FU_HID_DEVICE(self),
					buf2->data[0],
					buf2->data,
					buf2->len,
					FU_LENOVO_DOCK_DEVICE_TIMEOUT,
					FU_HID_DEVICE_FLAG_IS_FEATURE,
					error);
}

static gboolean
fu_lenovo_dock_device_set_report2(FuLenovoDockDevice *self, GByteArray *buf, GError **error)
{
	g_autoptr(GByteArray) buf2 = g_byte_array_new();
	fu_byte_array_append_uint8(buf2, 0x10);
	g_byte_array_append(buf2, buf->data, buf->len);
	fu_byte_array_set_size(buf2, FU_LENOVO_DOCK_DEVICE_IFACE2_LEN, 0x0);
	fu_hid_device_set_interface(FU_HID_DEVICE(self), 0x02);
	return fu_hid_device_set_report(FU_HID_DEVICE(self),
					buf2->data[0],
					buf2->data,
					buf2->len,
					FU_LENOVO_DOCK_DEVICE_TIMEOUT,
					FU_HID_DEVICE_FLAG_IS_FEATURE,
					error);
}

static gboolean
fu_lenovo_dock_device_set_flash_memory_access(FuLenovoDockDevice *self,
					      FuLenovoDockFlashMemoryAccessCtrl ctrl,
					      GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoDockFlashSetAccessReq) st_req =
	    fu_struct_lenovo_dock_flash_set_access_req_new();
	g_autoptr(FuStructLenovoDockFlashSetAccessRes) st_res = NULL;

	fu_struct_lenovo_dock_flash_set_access_req_set_ctrl(st_req, ctrl);
	if (!fu_lenovo_dock_device_set_report2(self, st_req->buf, error))
		return FALSE;
	buf = fu_lenovo_dock_device_get_report2(self, FU_LENOVO_DOCK_DEVICE_DELAY, error);
	if (buf == NULL)
		return FALSE;
	st_res = fu_struct_lenovo_dock_flash_set_access_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_dfu_control(FuLenovoDockDevice *self,
				  FuLenovoDockFwCtrlUpgradeStatus upgrade_status,
				  FuLenovoDockFwCtrlUpgradePhaseCtrl ctrl,
				  GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoDockDfuControlReq) st_req =
	    fu_struct_lenovo_dock_dfu_control_req_new();
	g_autoptr(FuStructLenovoDockDfuControlRes) st_res = NULL;

	fu_struct_lenovo_dock_dfu_control_req_set_upgrade_status(st_req, upgrade_status);
	fu_struct_lenovo_dock_dfu_control_req_set_ctrl(st_req, ctrl);

	if (!fu_lenovo_dock_device_set_report1(self, st_req->buf, error))
		return FALSE;
	buf = fu_lenovo_dock_device_get_report1(self, FU_LENOVO_DOCK_DEVICE_DELAY, error);
	if (buf == NULL)
		return FALSE;
	st_res = fu_struct_lenovo_dock_dfu_control_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_get_dfu_control_state(FuLenovoDockDevice *self,
					    FuLenovoDockFwCtrlUpgradeStatus *upgrade_status,
					    FuLenovoDockFwCtrlUpgradePhaseCtrl *ctrl,
					    GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoDockGetDfuControlReq) st_req =
	    fu_struct_lenovo_dock_get_dfu_control_req_new();
	g_autoptr(FuStructLenovoDockGetDfuControlRes) st_res = NULL;

	if (!fu_lenovo_dock_device_set_report1(self, st_req->buf, error))
		return FALSE;
	buf = fu_lenovo_dock_device_get_report1(self, FU_LENOVO_DOCK_DEVICE_DELAY, error);
	if (buf == NULL)
		return FALSE;
	st_res = fu_struct_lenovo_dock_get_dfu_control_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
	if (upgrade_status != NULL)
		*upgrade_status =
		    fu_struct_lenovo_dock_get_dfu_control_res_get_upgrade_status(st_res);
	if (ctrl != NULL)
		*ctrl = fu_struct_lenovo_dock_get_dfu_control_res_get_ctrl(st_res);

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_ensure_version(FuLenovoDockDevice *self, GError **error)
{
	g_autofree gchar *version = NULL;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuStructLenovoDockGetVersionReq) st_req =
	    fu_struct_lenovo_dock_get_version_req_new();
	g_autoptr(FuStructLenovoDockGetVersionRes) st_res = NULL;
	FuLenovoDockFwCtrlUpgradeStatus upgrade_status =
	    FU_LENOVO_DOCK_FW_CTRL_UPGRADE_STATUS_NON_LOCK;
	FuLenovoDockFwCtrlUpgradePhaseCtrl ctrl = FU_LENOVO_DOCK_FW_CTRL_UPGRADE_PHASE_CTRL_NA;

	/* query upgrade status -- is dock in phase2? */
	if (fu_lenovo_dock_device_get_dfu_control_state(self,
							&upgrade_status,
							&ctrl,
							&error_local)) {
		if (upgrade_status == FU_LENOVO_DOCK_FW_CTRL_UPGRADE_STATUS_LOCKED &&
		    ctrl == FU_LENOVO_DOCK_FW_CTRL_UPGRADE_PHASE_CTRL_NON_UNPLUG) {
			g_debug("lenovo-dock: delaying 0x81 for %ums while dock is in phase2",
				(guint)FU_LENOVO_DOCK_DEVICE_PHASE2_DELAY);
			fu_device_sleep(FU_DEVICE(self), FU_LENOVO_DOCK_DEVICE_PHASE2_DELAY);
		}
	} else {
		g_debug("lenovo-dock: failed to query 0x8A before version: %s",
			error_local->message);
	}

	if (!fu_lenovo_dock_device_set_report1(self, st_req->buf, error))
		return FALSE;
	buf = fu_lenovo_dock_device_get_report1(self, FU_LENOVO_DOCK_DEVICE_DELAY, error);
	if (buf == NULL)
		return FALSE;
	st_res = fu_struct_lenovo_dock_get_version_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
	version = g_strdup_printf("%X.%X.%02X",
				  fu_struct_lenovo_dock_get_version_res_get_version_major(st_res),
				  fu_struct_lenovo_dock_get_version_res_get_version_minor(st_res),
				  fu_struct_lenovo_dock_get_version_res_get_version_micro(st_res));
	fu_device_set_version(FU_DEVICE(self), version);

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_ensure_edition_state(FuLenovoDockDevice *self, GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoDockGetEditionReq) st_req =
	    fu_struct_lenovo_dock_get_edition_req_new();
	g_autoptr(FuStructLenovoDockGetEditionRes) st_res = NULL;

	if (g_log_get_debug_enabled()) {
		g_autofree gchar *str = fu_struct_lenovo_dock_get_edition_req_to_string(st_req);
		g_log("FuStruct", G_LOG_LEVEL_DEBUG, "%s", str);
	}
	if (!fu_lenovo_dock_device_set_report1(self, st_req->buf, error))
		return FALSE;
	buf = fu_lenovo_dock_device_get_report1(self, FU_LENOVO_DOCK_DEVICE_DELAY, error);
	if (buf == NULL)
		return FALSE;
	st_res = fu_struct_lenovo_dock_get_edition_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
	self->provision_status = fu_struct_lenovo_dock_get_edition_res_get_provision_status(st_res);

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_get_component_attrs_list(FuLenovoDockDevice *self,
					       guint8 *component_id_total,
					       GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoDockGetComponentIdListReq) st_req =
	    fu_struct_lenovo_dock_get_component_id_list_req_new();
	g_autoptr(FuStructLenovoDockGetComponentIdListRes) st_res = NULL;

	if (g_log_get_debug_enabled()) {
		g_autofree gchar *str =
		    fu_struct_lenovo_dock_get_component_id_list_req_to_string(st_req);
		g_log("FuStruct", G_LOG_LEVEL_DEBUG, "%s", str);
	}
	if (!fu_lenovo_dock_device_set_report1(self, st_req->buf, error))
		return FALSE;
	buf = fu_lenovo_dock_device_get_report1(self, FU_LENOVO_DOCK_DEVICE_DELAY, error);
	if (buf == NULL)
		return FALSE;
	st_res =
	    fu_struct_lenovo_dock_get_component_id_list_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
	if (component_id_total != NULL)
		*component_id_total =
		    fu_struct_lenovo_dock_get_component_id_list_res_get_total(st_res);

	/* success */
	return TRUE;
}

static GByteArray *
fu_lenovo_dock_device_flash_read_memory(FuLenovoDockDevice *self,
					FuLenovoDockComponentId component_id,
					guint32 addr,
					gsize datasz,
					GError **error)
{
	g_autoptr(GByteArray) data = g_byte_array_new();

	for (gsize i = 0; i < datasz; i += FU_STRUCT_LENOVO_DOCK_FLASH_READ_RES_N_ELEMENTS_DATA) {
		const guint8 *datatmp;
		gsize datatmpsz = 0;
		g_autoptr(GByteArray) buf = NULL;
		g_autoptr(FuStructLenovoDockFlashReadReq) st_req =
		    fu_struct_lenovo_dock_flash_read_req_new();
		g_autoptr(FuStructLenovoDockFlashReadRes) st_res = NULL;

		fu_struct_lenovo_dock_flash_read_req_set_size(
		    st_req,
		    FU_STRUCT_LENOVO_DOCK_FLASH_READ_RES_N_ELEMENTS_DATA);
		fu_struct_lenovo_dock_flash_read_req_set_component_id(st_req, component_id);
		fu_struct_lenovo_dock_flash_read_req_set_addr(st_req, addr + i);
		if (g_log_get_debug_enabled()) {
			g_autofree gchar *str =
			    fu_struct_lenovo_dock_flash_read_req_to_string(st_req);
			g_log("FuStruct", G_LOG_LEVEL_DEBUG, "%s", str);
		}
		if (!fu_lenovo_dock_device_set_report2(self, st_req->buf, error))
			return NULL;
		buf = fu_lenovo_dock_device_get_report2(self, FU_LENOVO_DOCK_DEVICE_DELAY, error);
		if (buf == NULL)
			return NULL;
		st_res =
		    fu_struct_lenovo_dock_flash_read_res_parse(buf->data, buf->len, 0x0, error);
		if (st_res == NULL)
			return NULL;
		datatmp = fu_struct_lenovo_dock_flash_read_res_get_data(st_res, &datatmpsz);
		g_byte_array_append(data, datatmp, datatmpsz);
	}

	/* success */
	return g_steal_pointer(&data);
}

static gboolean
fu_lenovo_dock_device_flash_erase_chunk(FuLenovoDockDevice *self,
					FuLenovoDockComponentId component_id,
					FuChunk *chk,
					GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoDockFlashEraseReq) st_req =
	    fu_struct_lenovo_dock_flash_erase_req_new();
	g_autoptr(FuStructLenovoDockFlashEraseRes) st_res = NULL;

	fu_struct_lenovo_dock_flash_erase_req_set_component_id(st_req, component_id);
	fu_struct_lenovo_dock_flash_erase_req_set_size(st_req, fu_chunk_get_data_sz(chk));
	fu_struct_lenovo_dock_flash_erase_req_set_addr(st_req, fu_chunk_get_address(chk));
	if (g_log_get_debug_enabled()) {
		g_autofree gchar *str = fu_struct_lenovo_dock_flash_erase_req_to_string(st_req);
		g_log("FuStruct", G_LOG_LEVEL_DEBUG, "%s", str);
	}
	if (!fu_lenovo_dock_device_set_report2(self, st_req->buf, error))
		return FALSE;
	buf = fu_lenovo_dock_device_get_report2(self, FU_LENOVO_DOCK_DEVICE_DELAY, error);
	if (buf == NULL)
		return FALSE;
	st_res = fu_struct_lenovo_dock_flash_erase_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_flash_erase_memory(FuLenovoDockDevice *self,
					 FuLenovoDockComponentId component_id,
					 guint32 addr,
					 gsize datasz,
					 guint16 chunksz,
					 FuProgress *progress,
					 GError **error)
{
	g_autoptr(GPtrArray) chunks = NULL;

	chunks = fu_chunk_array_new(NULL, datasz, addr, 0x0, chunksz, error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (gsize i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_lenovo_dock_device_flash_erase_chunk(self, component_id, chk, error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_flash_write_chunk(FuLenovoDockDevice *self,
					FuLenovoDockComponentId component_id,
					FuChunk *chk,
					GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoDockFlashProgramReq) st_req =
	    fu_struct_lenovo_dock_flash_program_req_new();
	g_autoptr(FuStructLenovoDockFlashProgramRes) st_res = NULL;

	fu_struct_lenovo_dock_flash_program_req_set_component_id(st_req, component_id);
	fu_struct_lenovo_dock_flash_program_req_set_datasz(st_req, 0x8 + fu_chunk_get_data_sz(chk));
	fu_struct_lenovo_dock_flash_program_req_set_size(st_req, fu_chunk_get_data_sz(chk));
	fu_struct_lenovo_dock_flash_program_req_set_addr(st_req, fu_chunk_get_address(chk));
	g_byte_array_append(st_req->buf, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
	if (g_log_get_debug_enabled()) {
		g_autofree gchar *str = fu_struct_lenovo_dock_flash_program_req_to_string(st_req);
		g_log("FuStruct", G_LOG_LEVEL_DEBUG, "%s", str);
	}
	if (!fu_lenovo_dock_device_set_report2(self, st_req->buf, error))
		return FALSE;
	buf = fu_lenovo_dock_device_get_report2(self, FU_LENOVO_DOCK_DEVICE_DELAY, error);
	if (buf == NULL)
		return FALSE;
	st_res = fu_struct_lenovo_dock_flash_program_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_flash_write_memory(FuLenovoDockDevice *self,
					 FuLenovoDockComponentId component_id,
					 guint32 addr,
					 const guint8 *data,
					 gsize datasz,
					 guint16 chunksz,
					 FuProgress *progress,
					 GError **error)
{
	g_autoptr(GPtrArray) chunks = NULL;

	chunks = fu_chunk_array_new(data, datasz, addr, 0x0, chunksz, error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (gsize i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_lenovo_dock_device_flash_write_chunk(self, component_id, chk, error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_flash_verify_signature(FuLenovoDockDevice *self,
					     FuLenovoDockComponentId component_id,
					     GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoDockFlashVerifySignatureReq) st_req =
	    fu_struct_lenovo_dock_flash_verify_signature_req_new();
	g_autoptr(FuStructLenovoDockFlashVerifySignatureRes) st_res = NULL;

	fu_struct_lenovo_dock_flash_verify_signature_req_set_component_id(st_req, component_id);
	if (!fu_lenovo_dock_device_set_report1(self, st_req->buf, error))
		return FALSE;
	buf = fu_lenovo_dock_device_get_report1(self, FU_LENOVO_DOCK_DEVICE_DELAY_LONG, error);
	if (buf == NULL)
		return FALSE;
	st_res =
	    fu_struct_lenovo_dock_flash_verify_signature_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
	if (fu_struct_lenovo_dock_flash_verify_signature_res_get_result(st_res) !=
	    FU_LENOVO_DOCK_FLASH_VERIFY_SIGNATURE_RESULT_PASS) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "signature did not validate on-device");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_get_component_crc(FuLenovoDockDevice *self,
					FuLenovoDockComponentId component_id,
					guint32 *crc,
					GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoDockFlashVerifyCrcReq) st_req =
	    fu_struct_lenovo_dock_flash_verify_crc_req_new();
	g_autoptr(FuStructLenovoDockFlashVerifyCrcRes) st_res = NULL;

	fu_struct_lenovo_dock_flash_verify_crc_req_set_component_id(st_req, component_id);
	if (!fu_lenovo_dock_device_set_report1(self, st_req->buf, error))
		return FALSE;
	buf = fu_lenovo_dock_device_get_report1(self, FU_LENOVO_DOCK_DEVICE_DELAY_LONG, error);
	if (buf == NULL)
		return FALSE;
	st_res = fu_struct_lenovo_dock_flash_verify_crc_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
	if (crc != NULL)
		*crc = fu_struct_lenovo_dock_flash_verify_crc_res_get_crc(st_res);

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_setup(FuDevice *device, GError **error)
{
	FuLenovoDockDevice *self = FU_LENOVO_DOCK_DEVICE(device);

	/* FuHidrawDevice->setup */
	if (!FU_DEVICE_CLASS(fu_lenovo_dock_device_parent_class)->setup(device, error))
		return FALSE;

	/* get version and edition (if IoT) */
	if (!fu_lenovo_dock_device_ensure_version(self, error)) {
		g_prefix_error_literal(error, "failed to ensure version: ");
		return FALSE;
	}
	if (fu_device_has_private_flag(device, FU_LENOVO_DOCK_DEVICE_FLAG_CAN_PROVISION)) {
		if (!fu_lenovo_dock_device_ensure_edition_state(self, error)) {
			g_prefix_error_literal(error, "failed to ensure edition state: ");
			return FALSE;
		}
	}

	/* quirk file did not override */
	if (self->image_pid == 0x0)
		self->image_pid = fu_device_get_pid(device);

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_check_firmware(FuDevice *device,
				     FuFirmware *firmware,
				     FuFirmwareParseFlags flags,
				     GError **error)
{
	FuLenovoDockDevice *self = FU_LENOVO_DOCK_DEVICE(device);

	/* PID matches? */
	if ((flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_VID_PID) == 0 &&
	    self->image_pid != fu_lenovo_dock_firmware_get_pid(FU_LENOVO_DOCK_FIRMWARE(firmware))) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware PID mismatch, got 0x%04x, expected 0x%04x",
			    fu_lenovo_dock_firmware_get_pid(FU_LENOVO_DOCK_FIRMWARE(firmware)),
			    self->image_pid);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_ensure_usage(FuLenovoDockDevice *self, gboolean *valid, GError **error)
{
	guint32 crc_actual = 0;
	guint32 crc_calculated;
	guint8 usage_items;
	gsize offset = 0;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoDockUsage) st = NULL;

	/* read the device metadata table */
	buf = fu_lenovo_dock_device_flash_read_memory(self,
						      FU_LENOVO_DOCK_COMPONENT_ID_USAGE,
						      FU_LENOVO_DOCK_DEVICE_USAGE_INFO_START,
						      FU_LENOVO_DOCK_DEVICE_USAGE_INFO_SIZE,
						      error);
	if (buf == NULL) {
		g_prefix_error_literal(error, "failed to get usage info: ");
		return FALSE;
	}
	if (buf->len < FU_LENOVO_DOCK_DEVICE_USAGE_INFO_SIZE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "not enough usage info");
		return FALSE;
	}
	crc_calculated = fu_crc32(FU_CRC_KIND_B32_STANDARD, buf->data, buf->len - 4);
	if (!fu_memread_uint32_safe(buf->data,
				    buf->len,
				    buf->len - 4,
				    &crc_actual,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	g_debug("usage info CRC got 0x%08x, expected 0x%08x", crc_actual, crc_calculated);

	/* parse the usage metadata */
	st = fu_struct_lenovo_dock_usage_parse(buf->data, buf->len, offset, error);
	if (st == NULL)
		return FALSE;
	usage_items = fu_struct_lenovo_dock_usage_get_total_number(st);

	/* are payloads signed */
	if (fu_struct_lenovo_dock_usage_get_dsa(st) != FU_LENOVO_DOCK_DSA_TYPE_NONE)
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);

	/* save so we can reconstruct for erase + program */
	if (self->st_usage != NULL)
		fu_struct_lenovo_dock_usage_unref(self->st_usage);
	self->st_usage = fu_struct_lenovo_dock_usage_ref(st);

	/* parse each usage metadata item */
	g_ptr_array_set_size(self->st_usage_items, 0);
	offset += FU_STRUCT_LENOVO_DOCK_USAGE_SIZE;
	for (guint i = 0; i < usage_items; i++) {
		g_autoptr(FuStructLenovoDockUsageItem) st_item = NULL;
		st_item =
		    fu_struct_lenovo_dock_usage_item_parse(buf->data, buf->len, offset, error);
		if (st_item == NULL)
			return FALSE;
		g_ptr_array_add(self->st_usage_items,
				fu_struct_lenovo_dock_usage_item_ref(st_item));
		offset += FU_STRUCT_LENOVO_DOCK_USAGE_ITEM_SIZE;
	}

	/* success */
	if (valid != NULL)
		*valid = crc_calculated == crc_actual;
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_get_component_attrs(FuLenovoDockDevice *self,
					  FuLenovoDockComponentId component_id,
					  FuLenovoDockComponentPurpose *purpose,
					  guint32 *storage_size,
					  guint16 *erase_size,
					  guint16 *program_size,
					  GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoDockFlashGetAttrsReq) st_req =
	    fu_struct_lenovo_dock_flash_get_attrs_req_new();
	g_autoptr(FuStructLenovoDockFlashGetAttrsRes) st_res = NULL;

	fu_struct_lenovo_dock_flash_get_attrs_req_set_component_id(st_req, component_id);
	if (!fu_lenovo_dock_device_set_report1(self, st_req->buf, error))
		return FALSE;
	buf = fu_lenovo_dock_device_get_report1(self, FU_LENOVO_DOCK_DEVICE_DELAY, error);
	if (buf == NULL)
		return FALSE;
	st_res = fu_struct_lenovo_dock_flash_get_attrs_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
	if (storage_size != NULL)
		*storage_size = fu_struct_lenovo_dock_flash_get_attrs_res_get_storage_size(st_res);
	if (erase_size != NULL)
		*erase_size = fu_struct_lenovo_dock_flash_get_attrs_res_get_erase_size(st_res);
	if (program_size != NULL)
		*program_size = fu_struct_lenovo_dock_flash_get_attrs_res_get_program_size(st_res);
	if (purpose != NULL)
		*purpose = fu_struct_lenovo_dock_flash_get_attrs_res_get_purpose(st_res);

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_write_usage(FuLenovoDockDevice *self, FuProgress *progress, GError **error)
{
	guint16 erase_size = 0;
	guint16 program_size = 0;
	guint32 storage_size = 0;
	guint32 crc_actual = 0;
	guint32 crc_calculated;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 17, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 82, NULL);

	/* get the usage chunk sizes */
	if (!fu_lenovo_dock_device_get_component_attrs(self,
						       FU_LENOVO_DOCK_COMPONENT_ID_USAGE,
						       NULL,
						       &storage_size,
						       &erase_size,
						       &program_size,
						       error)) {
		g_prefix_error_literal(error, "failed to ensure usage attrs: ");
		return FALSE;
	}
	if (storage_size != FU_LENOVO_DOCK_DEVICE_USAGE_INFO_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "usage table invalid size, got 0x%x and expected 0x%x",
			    storage_size,
			    (guint)FU_LENOVO_DOCK_DEVICE_USAGE_INFO_SIZE);
		return FALSE;
	}

	/* erase and write new table */
	g_byte_array_append(buf, self->st_usage->buf->data, self->st_usage->buf->len);
	for (guint i = 0; i < self->st_usage_items->len; i++) {
		FuStructLenovoDockUsageItem *st_usage_item =
		    g_ptr_array_index(self->st_usage_items, i);
		g_byte_array_append(buf, st_usage_item->buf->data, st_usage_item->buf->len);
	}
	if (buf->len > FU_LENOVO_DOCK_DEVICE_USAGE_INFO_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "usage table too large: 0x%x > 0x%x",
			    buf->len,
			    (guint)FU_LENOVO_DOCK_DEVICE_USAGE_INFO_SIZE);
		return FALSE;
	}
	fu_byte_array_set_size(buf, FU_LENOVO_DOCK_DEVICE_USAGE_INFO_SIZE, 0x0);

	/* erase */
	if (!fu_lenovo_dock_device_flash_erase_memory(self,
						      FU_LENOVO_DOCK_COMPONENT_ID_USAGE,
						      FU_LENOVO_DOCK_DEVICE_USAGE_INFO_START,
						      buf->len,
						      erase_size,
						      fu_progress_get_child(progress),
						      error))
		return FALSE;
	fu_progress_step_done(progress);

	/* fixup CRC and write */
	crc_calculated = fu_crc32(FU_CRC_KIND_B32_STANDARD, buf->data, buf->len - 4);
	if (!fu_memwrite_uint32_safe(buf->data,
				     buf->len,
				     buf->len - 0x4,
				     crc_calculated,
				     G_LITTLE_ENDIAN,
				     error))
		return FALSE;
	if (!fu_lenovo_dock_device_flash_write_memory(self,
						      FU_LENOVO_DOCK_COMPONENT_ID_USAGE,
						      FU_LENOVO_DOCK_DEVICE_USAGE_INFO_START,
						      buf->data,
						      buf->len,
						      program_size,
						      fu_progress_get_child(progress),
						      error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify on-device CRC */
	if (!fu_lenovo_dock_device_get_component_crc(self,
						     FU_LENOVO_DOCK_COMPONENT_ID_USAGE,
						     &crc_actual,
						     error)) {
		g_prefix_error_literal(error, "failed to self verify: ");
		return FALSE;
	}
	if (crc_actual != crc_calculated) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to write usage, got 0x%08x and expected 0x%08x",
			    crc_actual,
			    crc_calculated);
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_write_image(FuLenovoDockDevice *self,
				  FuLenovoDockComponentId component_id,
				  FuFirmware *img,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	guint16 erase_size = 0;
	guint16 program_size = 0;
	guint32 storage_size = 0;
	guint32 crc_provided;
	guint32 crc_calculated = 0;
	FuLenovoDockComponentPurpose purpose = FU_LENOVO_DOCK_COMPONENT_PURPOSE_FIRMWARE;
	FuStructLenovoDockUsageItem *st_usage_item;
	g_autoptr(GBytes) blob = NULL;

	/* sanity check */
	if (fu_firmware_get_size(img) < FU_LENOVO_DOCK_FIRMWARE_SIGNATURE_SIZE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "component too small for signature");
		return FALSE;
	}

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 2, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 78, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 16, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 4, "usage");

	/* get image + item */
	st_usage_item = fu_lenovo_dock_device_get_usage_item(self, component_id, error);
	if (st_usage_item == NULL)
		return FALSE;

	/* get the usage chunk sizes */
	if (!fu_lenovo_dock_device_get_component_attrs(self,
						       component_id,
						       &purpose,
						       &storage_size,
						       &erase_size,
						       &program_size,
						       error)) {
		g_prefix_error_literal(error, "failed to ensure usage attrs: ");
		return FALSE;
	}
	if (purpose != FU_LENOVO_DOCK_COMPONENT_PURPOSE_FIRMWARE) {
		fu_progress_finished(progress);
		return TRUE;
	}
	if (fu_firmware_get_size(img) - FU_LENOVO_DOCK_FIRMWARE_SIGNATURE_SIZE > storage_size) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "component too large for storage: 0x%x > 0x%x",
			    (guint)fu_firmware_get_size(img) -
				FU_LENOVO_DOCK_FIRMWARE_SIGNATURE_SIZE,
			    storage_size);
		return FALSE;
	}

	/* erase entire block and write blob */
	if (!fu_lenovo_dock_device_flash_erase_memory(
		self,
		component_id,
		fu_struct_lenovo_dock_usage_item_get_address(st_usage_item),
		fu_struct_lenovo_dock_usage_item_get_max_size(st_usage_item),
		erase_size,
		fu_progress_get_child(progress),
		error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write */
	blob = fu_firmware_get_bytes(img, error);
	if (blob == NULL)
		return FALSE;
	if (!fu_lenovo_dock_device_flash_write_memory(
		self,
		component_id,
		fu_struct_lenovo_dock_usage_item_get_address(st_usage_item),
		g_bytes_get_data(blob, NULL),
		g_bytes_get_size(blob),
		program_size,
		fu_progress_get_child(progress),
		error)) {
		g_prefix_error_literal(error, "failed to write component: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* verify */
	if (!fu_lenovo_dock_device_flash_verify_signature(self, component_id, error)) {
		g_prefix_error_literal(error, "failed to get component signature: ");
		return FALSE;
	}
	if (!fu_lenovo_dock_device_get_component_crc(self, component_id, &crc_calculated, error)) {
		g_prefix_error_literal(error, "failed to get component CRC: ");
		return FALSE;
	}
	crc_provided = fu_lenovo_dock_image_get_crc(FU_LENOVO_DOCK_IMAGE(img));
	if (crc_provided != crc_calculated) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "image %s provided 0x%08x and calculated 0x%08x",
			    fu_firmware_get_id(img),
			    crc_provided,
			    crc_calculated);
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write target fw version - but do *not* set flag=DoUpdate */
	fu_struct_lenovo_dock_usage_item_set_target_version(st_usage_item,
							    fu_firmware_get_version_raw(img));
	fu_struct_lenovo_dock_usage_item_set_target_size(
	    st_usage_item,
	    fu_firmware_get_size(img) - FU_LENOVO_DOCK_FIRMWARE_SIGNATURE_SIZE);
	fu_struct_lenovo_dock_usage_item_set_target_crc32(st_usage_item, crc_provided);
	if (!fu_lenovo_dock_device_write_usage(self, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_write_images(FuLenovoDockDevice *self,
				   FuFirmware *firmware,
				   FuProgress *progress,
				   FwupdInstallFlags flags,
				   GError **error)
{
	guint8 total_number = fu_struct_lenovo_dock_usage_get_total_number(self->st_usage);

	/* sanity check */
	if (total_number == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no usage items");
		return FALSE;
	}

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, total_number - 1);
	for (guint component_id = 1; component_id < total_number; component_id++) {
		g_autoptr(FuFirmware) img = NULL;
		g_autoptr(GError) error_local = NULL;

		img = fu_firmware_get_image_by_idx(firmware, component_id, &error_local);
		if (img == NULL) {
			g_debug("ignoring: %s", error_local->message);
			fu_progress_step_done(progress);
			continue;
		}
		if (!fu_lenovo_dock_device_write_image(self,
						       component_id,
						       img,
						       fu_progress_get_child(progress),
						       flags,
						       error)) {
			g_prefix_error(error, "failed to write component ID %u: ", component_id);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_flash_memory_access_request_cb(FuDevice *device, GError **error)
{
	FuLenovoDockDevice *self = FU_LENOVO_DOCK_DEVICE(device);

	if (!fu_lenovo_dock_device_set_flash_memory_access(
		self,
		FU_LENOVO_DOCK_FLASH_MEMORY_ACCESS_CTRL_REQUEST,
		error)) {
		g_prefix_error_literal(error, "failed to request flash memory access: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_flash_memory_access_release_cb(FuDevice *device, GError **error)
{
	FuLenovoDockDevice *self = FU_LENOVO_DOCK_DEVICE(device);

	if (!fu_lenovo_dock_device_set_flash_memory_access(
		self,
		FU_LENOVO_DOCK_FLASH_MEMORY_ACCESS_CTRL_RELEASE,
		error)) {
		g_prefix_error_literal(error, "failed to release flash memory access: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_write_firmware(FuDevice *device,
				     FuFirmware *firmware,
				     FuProgress *progress,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuLenovoDockDevice *self = FU_LENOVO_DOCK_DEVICE(device);
	gboolean usage_info_valid = FALSE;
	guint8 component_id_total = 0;
	guint8 total_number;
	g_autoptr(FuDeviceLocker) flash_locker = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 95, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, NULL);

	/* request access to the SPI and get the flash ID list */
	flash_locker =
	    fu_device_locker_new_full(device,
				      fu_lenovo_dock_device_flash_memory_access_request_cb,
				      fu_lenovo_dock_device_flash_memory_access_release_cb,
				      error);
	if (flash_locker == NULL)
		return FALSE;
	if (!fu_lenovo_dock_device_get_component_attrs_list(self, &component_id_total, error)) {
		g_prefix_error_literal(error, "failed to get flash ID list: ");
		return FALSE;
	}
	g_debug("flash ID total: 0x%x", component_id_total);
	fu_progress_step_done(progress);

	/* verify existing CRC */
	if (!fu_lenovo_dock_device_ensure_usage(self, &usage_info_valid, error)) {
		g_prefix_error_literal(error, "failed to validate usage info: ");
		return FALSE;
	}
	if (!usage_info_valid) {
		if ((flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "device usage info CRC was not valid");
			return FALSE;
		}
		g_warning("usage info CRC was not valid, but continuing");
	}
	fu_progress_step_done(progress);

	/* set dock FW Update Ctrl */
	if (!fu_lenovo_dock_device_dfu_control(self,
					       FU_LENOVO_DOCK_FW_CTRL_UPGRADE_STATUS_NON_LOCK,
					       FU_LENOVO_DOCK_FW_CTRL_UPGRADE_PHASE_CTRL_IN_PHASE1,
					       error)) {
		g_prefix_error_literal(error, "failed to trigger phase1: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* clear all the target versions and write the new usage table with new sizes  */
	total_number = fu_struct_lenovo_dock_usage_get_total_number(self->st_usage);
	fu_struct_lenovo_dock_usage_set_composite_version(self->st_usage,
							  fu_firmware_get_version_raw(firmware));
	for (guint component_id = 1; component_id < total_number; component_id++) {
		FuStructLenovoDockUsageItem *st_usage_item;
		g_autoptr(FuFirmware) img = NULL;

		st_usage_item = fu_lenovo_dock_device_get_usage_item(self, component_id, error);
		if (st_usage_item == NULL)
			return FALSE;
		img = fu_firmware_get_image_by_idx(firmware, component_id, NULL);
		if (img == NULL)
			continue;
		fu_struct_lenovo_dock_usage_item_set_target_version(st_usage_item, 0);
		fu_struct_lenovo_dock_usage_item_set_target_size(
		    st_usage_item,
		    fu_firmware_get_size(img) - FU_LENOVO_DOCK_FIRMWARE_SIGNATURE_SIZE);
		fu_struct_lenovo_dock_usage_item_set_target_crc32(
		    st_usage_item,
		    fu_lenovo_dock_image_get_crc(FU_LENOVO_DOCK_IMAGE(img)));
	}
	if (!fu_lenovo_dock_device_write_usage(self, fu_progress_get_child(progress), error)) {
		g_prefix_error_literal(error, "failed to write usage table: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write each flash ID */
	if (!fu_lenovo_dock_device_write_images(self,
						firmware,
						fu_progress_get_child(progress),
						flags,
						error))
		return FALSE;
	fu_progress_step_done(progress);

	/* actually write firmware to the device */
	if (!fu_device_locker_close(flash_locker, error))
		return FALSE;
	if (!fu_lenovo_dock_device_dfu_control(self,
					       FU_LENOVO_DOCK_FW_CTRL_UPGRADE_STATUS_LOCKED,
					       FU_LENOVO_DOCK_FW_CTRL_UPGRADE_PHASE_CTRL_NON_UNPLUG,
					       error)) {
		g_prefix_error_literal(error, "failed to trigger phase2: ");
		return FALSE;
	}
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_set_quirk_kv(FuDevice *device,
				   const gchar *key,
				   const gchar *value,
				   GError **error)
{
	FuLenovoDockDevice *self = FU_LENOVO_DOCK_DEVICE(device);

	if (g_strcmp0(key, "LenovoDockImagePid") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->image_pid = (guint16)tmp;
		return TRUE;
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_lenovo_dock_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_lenovo_dock_device_init(FuLenovoDockDevice *self)
{
	self->st_usage_items =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_struct_lenovo_dock_usage_item_unref);
	fu_device_set_remove_delay(FU_DEVICE(self), 300000);
	fu_device_add_protocol(FU_DEVICE(self), "com.lenovo.dock");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_DOCK_USB);
	fu_device_set_install_duration(FU_DEVICE(self), 330);
	fu_device_register_private_flag(FU_DEVICE(self), FU_LENOVO_DOCK_DEVICE_FLAG_CAN_PROVISION);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_LENOVO_DOCK_FIRMWARE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), 1);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), 2);
	fu_usb_device_set_claim_retry_count(FU_USB_DEVICE(self), 5);
}

static void
fu_lenovo_dock_device_finalize(GObject *object)
{
	FuLenovoDockDevice *self = FU_LENOVO_DOCK_DEVICE(object);
	if (self->st_usage != NULL)
		fu_struct_lenovo_dock_usage_unref(self->st_usage);
	g_ptr_array_unref(self->st_usage_items);
	G_OBJECT_CLASS(fu_lenovo_dock_device_parent_class)->finalize(object);
}

static void
fu_lenovo_dock_device_class_init(FuLenovoDockDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_lenovo_dock_device_finalize;
	device_class->to_string = fu_lenovo_dock_device_to_string;
	device_class->setup = fu_lenovo_dock_device_setup;
	device_class->check_firmware = fu_lenovo_dock_device_check_firmware;
	device_class->write_firmware = fu_lenovo_dock_device_write_firmware;
	device_class->set_progress = fu_lenovo_dock_device_set_progress;
	device_class->set_quirk_kv = fu_lenovo_dock_device_set_quirk_kv;
}
