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

#define FU_LENOVO_DOCK_DEVICE_DELAY   25 /* ms */
#define FU_LENOVO_DOCK_DEVICE_RETRIES 1600

#define FU_LENOVO_DOCK_DEVICE_USAGE_INFO_SIZE  0x1000
#define FU_LENOVO_DOCK_DEVICE_USAGE_INFO_START 0xFFF000

struct _FuLenovoDockDevice {
	FuHidrawDevice parent_instance;
	FuStructLenovoDockUsage *st_usage;
	GPtrArray *st_usage_items; /* element-type: FuStructLenovoDockUsageItem */
};

G_DEFINE_TYPE(FuLenovoDockDevice, fu_lenovo_dock_device, FU_TYPE_HIDRAW_DEVICE)

static void
fu_lenovo_dock_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuLenovoDockDevice *self = FU_LENOVO_DOCK_DEVICE(device);

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
		    "cannot find flash ID 0x%x",
		    component_id);
	return NULL;
}

static gboolean
fu_lenovo_dock_device_get_report_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuLenovoDockDevice *self = FU_LENOVO_DOCK_DEVICE(device);
	FuLenovoDockStatus status;
	GByteArray *buf = (GByteArray *)user_data;
	g_autoptr(FuStructLenovoDockGenericRes) st = NULL;

	if (!fu_hidraw_device_get_report(FU_HIDRAW_DEVICE(self),
					 buf->data,
					 buf->len,
					 FU_IO_CHANNEL_FLAG_NONE,
					 error))
		return FALSE;
	st = fu_struct_lenovo_dock_generic_res_parse(
	    buf->data,
	    buf->len,
	    buf->len == FU_LENOVO_DOCK_DEVICE_IFACE2_LEN ? 0x01 : 0x00,
	    error);
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
			    FWUPD_ERROR_TIMED_OUT,
			    "status was %s",
			    fu_lenovo_dock_status_to_string(status));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_lenovo_dock_device_get_report(FuLenovoDockDevice *self, gsize ifacesz, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	fu_byte_array_append_uint8(buf, ifacesz == FU_LENOVO_DOCK_DEVICE_IFACE2_LEN ? 0x10 : 0x0);
	fu_byte_array_set_size(buf, ifacesz, 0x0);
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_lenovo_dock_device_get_report_cb,
				  FU_LENOVO_DOCK_DEVICE_RETRIES,
				  FU_LENOVO_DOCK_DEVICE_DELAY,
				  buf,
				  error))
		return NULL;

	/* success */
	if (ifacesz == FU_LENOVO_DOCK_DEVICE_IFACE2_LEN)
		g_byte_array_remove_index(buf, 0);
	return g_steal_pointer(&buf);
}

static gboolean
fu_lenovo_dock_device_set_report(FuLenovoDockDevice *self,
				 GByteArray *buf,
				 gsize ifacesz,
				 GError **error)
{
	g_autoptr(GByteArray) buf2 = g_byte_array_new();

	if (ifacesz == FU_LENOVO_DOCK_DEVICE_IFACE2_LEN)
		fu_byte_array_append_uint8(buf2, 0x10);
	g_byte_array_append(buf2, buf->data, buf->len);
	fu_byte_array_set_size(buf2, ifacesz, 0x0);
	return fu_hidraw_device_set_report(FU_HIDRAW_DEVICE(self),
					   buf2->data,
					   buf2->len,
					   FU_IO_CHANNEL_FLAG_NONE,
					   error);
}

static GByteArray *
fu_lenovo_dock_device_txfer1(FuLenovoDockDevice *self, GByteArray *buf, GError **error)
{
	if (!fu_lenovo_dock_device_set_report(self, buf, FU_LENOVO_DOCK_DEVICE_IFACE1_LEN, error))
		return NULL;
	return fu_lenovo_dock_device_get_report(self, FU_LENOVO_DOCK_DEVICE_IFACE1_LEN, error);
}

static GByteArray *
fu_lenovo_dock_device_txfer2(FuLenovoDockDevice *self, GByteArray *buf, GError **error)
{
	if (!fu_lenovo_dock_device_set_report(self, buf, FU_LENOVO_DOCK_DEVICE_IFACE2_LEN, error))
		return NULL;
	return fu_lenovo_dock_device_get_report(self, FU_LENOVO_DOCK_DEVICE_IFACE2_LEN, error);
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
	buf = fu_lenovo_dock_device_txfer2(self, st_req->buf, error);
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
				  FuLenovoDockDockFwCtrlUpgradeStatus upgrade_status,
				  FuLenovoDockDockFwCtrlUpgradePhaseCtrl ctrl,
				  GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoDockDfuControlReq) st_req =
	    fu_struct_lenovo_dock_dfu_control_req_new();
	g_autoptr(FuStructLenovoDockDfuControlRes) st_res = NULL;

	fu_struct_lenovo_dock_dfu_control_req_set_upgrade_status(st_req, upgrade_status);
	fu_struct_lenovo_dock_dfu_control_req_set_ctrl(st_req, ctrl);
	buf = fu_lenovo_dock_device_txfer1(self, st_req->buf, error);
	if (buf == NULL)
		return FALSE;
	st_res = fu_struct_lenovo_dock_dfu_control_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_ensure_version(FuLenovoDockDevice *self, GError **error)
{
	g_autofree gchar *version = NULL;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoDockGetVersionReq) st_req =
	    fu_struct_lenovo_dock_get_version_req_new();
	g_autoptr(FuStructLenovoDockGetVersionRes) st_res = NULL;

	buf = fu_lenovo_dock_device_txfer1(self, st_req->buf, error);
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
fu_lenovo_dock_device_get_component_id_list(FuLenovoDockDevice *self,
					    guint8 *component_id_total,
					    GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoDockGetComponentIdListReq) st_req =
	    fu_struct_lenovo_dock_get_component_id_list_req_new();
	g_autoptr(FuStructLenovoDockGetComponentIdListRes) st_res = NULL;

	buf = fu_lenovo_dock_device_txfer1(self, st_req->buf, error);
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
					guint32 addr,
					gsize datasz,
					GError **error)
{
	g_autoptr(GByteArray) data = g_byte_array_new();

	for (gsize i = 0; i < datasz; i += 256) {
		const guint8 *datatmp;
		gsize datatmpsz = 0;
		g_autoptr(GByteArray) buf = NULL;
		g_autoptr(FuStructLenovoDockFlashReadReq) st_req =
		    fu_struct_lenovo_dock_flash_read_req_new();
		g_autoptr(FuStructLenovoDockFlashReadRes) st_res = NULL;

		fu_struct_lenovo_dock_flash_read_req_set_size(st_req, 256);
		fu_struct_lenovo_dock_flash_read_req_set_addr(st_req, addr + i);
		buf = fu_lenovo_dock_device_txfer2(self, st_req->buf, error);
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
fu_lenovo_dock_device_flash_erase_memory(FuLenovoDockDevice *self,
					 FuLenovoDockComponentId component_id,
					 guint32 addr,
					 gsize datasz,
					 guint16 chunksz,
					 GError **error)
{
	g_autoptr(GPtrArray) chunks = fu_chunk_array_new(NULL, datasz, addr, 0x0, chunksz);
	for (gsize i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		g_autoptr(GByteArray) buf = NULL;
		g_autoptr(FuStructLenovoDockFlashEraseReq) st_req =
		    fu_struct_lenovo_dock_flash_erase_req_new();
		g_autoptr(FuStructLenovoDockFlashEraseRes) st_res = NULL;

		fu_struct_lenovo_dock_flash_erase_req_set_component_id(st_req, component_id);
		fu_struct_lenovo_dock_flash_erase_req_set_size(st_req, fu_chunk_get_data_sz(chk));
		fu_struct_lenovo_dock_flash_erase_req_set_addr(st_req, fu_chunk_get_address(chk));
		buf = fu_lenovo_dock_device_txfer2(self, st_req->buf, error);
		if (buf == NULL)
			return FALSE;
		st_res =
		    fu_struct_lenovo_dock_flash_erase_res_parse(buf->data, buf->len, 0x0, error);
		if (st_res == NULL)
			return FALSE;
	}

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
					 GError **error)
{
	g_autoptr(GPtrArray) chunks = fu_chunk_array_new(NULL, datasz, addr, 0x0, chunksz);
	for (gsize i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		g_autoptr(GByteArray) buf = NULL;
		g_autoptr(FuStructLenovoDockFlashProgramReq) st_req =
		    fu_struct_lenovo_dock_flash_program_req_new();
		g_autoptr(FuStructLenovoDockFlashProgramRes) st_res = NULL;

		fu_struct_lenovo_dock_flash_program_req_set_component_id(st_req, component_id);
		fu_struct_lenovo_dock_flash_program_req_set_datasz(st_req,
								   0x8 + fu_chunk_get_data_sz(chk));
		fu_struct_lenovo_dock_flash_program_req_set_size(st_req, fu_chunk_get_data_sz(chk));
		fu_struct_lenovo_dock_flash_program_req_set_addr(st_req, fu_chunk_get_address(chk));
		g_byte_array_append(st_req->buf, data, datasz);
		buf = fu_lenovo_dock_device_txfer2(self, st_req->buf, error);
		if (buf == NULL)
			return FALSE;
		st_res =
		    fu_struct_lenovo_dock_flash_program_res_parse(buf->data, buf->len, 0x0, error);
		if (st_res == NULL)
			return FALSE;
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
	buf = fu_lenovo_dock_device_txfer1(self, st_req->buf, error);
	if (buf == NULL)
		return FALSE;
	st_res =
	    fu_struct_lenovo_dock_flash_verify_signature_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_flash_verify_crc(FuLenovoDockDevice *self,
				       FuLenovoDockComponentId component_id,
				       guint32 *crc,
				       GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoDockFlashVerifyCrcReq) st_req =
	    fu_struct_lenovo_dock_flash_verify_crc_req_new();
	g_autoptr(FuStructLenovoDockFlashVerifyCrcRes) st_res = NULL;

	fu_struct_lenovo_dock_flash_verify_crc_req_set_component_id(st_req, component_id);
	buf = fu_lenovo_dock_device_txfer1(self, st_req->buf, error);
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

	/* FuhidrawdeviceDevice->setup */
	if (!FU_DEVICE_CLASS(fu_lenovo_dock_device_parent_class)->setup(device, error))
		return FALSE;

	/* get version */
	if (!fu_lenovo_dock_device_ensure_version(self, error)) {
		g_prefix_error_literal(error, "failed to ensure version: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_check_firmware(FuDevice *device,
				     FuFirmware *firmware,
				     FuFirmwareParseFlags flags,
				     GError **error)
{
	/* PID matches? */
	if (fu_device_get_pid(device) !=
	    fu_lenovo_dock_firmware_get_pid(FU_LENOVO_DOCK_FIRMWARE(firmware))) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "device PID mismatch, got 0x%04x, expected 0x%04x",
			    fu_lenovo_dock_firmware_get_pid(FU_LENOVO_DOCK_FIRMWARE(firmware)),
			    fu_device_get_pid(device));
		return FALSE;
	}

	/* success */
	return TRUE;
}

// FIXME: _ensure_
static gboolean
fu_lenovo_dock_device_verify_usage_info(FuLenovoDockDevice *self, gboolean *valid, GError **error)
{
	guint32 crc_actual;
	guint32 crc_calculated;
	guint8 usage_items;
	gsize offset = 0;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoDockUsage) st = NULL;

	/* read the device metadata table */
	buf = fu_lenovo_dock_device_flash_read_memory(self,
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
	crc_actual = fu_memread_uint32(buf->data - 4, G_LITTLE_ENDIAN);
	g_debug("usage info CRC got 0x%4x, expected 0x%4x", crc_actual, crc_calculated);

	/* parse the usage metadata */
	st = fu_struct_lenovo_dock_usage_parse(buf->data, buf->len, offset, error);
	if (st == NULL)
		return FALSE;
	usage_items = fu_struct_lenovo_dock_usage_get_total_number(st);

	/* are payloads signed */
	if (fu_struct_lenovo_dock_usage_get_dsa(st) != FU_LENOVO_DOCK_SIGN_TYPE_NONE)
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
fu_lenovo_dock_device_get_component_id(FuLenovoDockDevice *self,
				       FuLenovoDockComponentId component_id,
				       FuLenovoDockFlashIdPurpose *purpose,
				       guint16 *erase_size,
				       guint16 *program_size,
				       GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoDockFlashGetAttrsReq) st_req =
	    fu_struct_lenovo_dock_flash_get_attrs_req_new();
	g_autoptr(FuStructLenovoDockFlashGetAttrsRes) st_res = NULL;

	fu_struct_lenovo_dock_flash_get_attrs_req_set_component_id(st_req, component_id);
	buf = fu_lenovo_dock_device_txfer1(self, st_req->buf, error);
	if (buf == NULL)
		return FALSE;
	st_res = fu_struct_lenovo_dock_flash_get_attrs_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
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
fu_lenovo_dock_device_write_usage(FuLenovoDockDevice *self, GError **error)
{
	guint16 erase_size = 0;
	guint16 program_size = 0;
	guint32 crc_actual = 0;
	guint32 crc_calculated;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* get the usage chunk sizes */
	if (!fu_lenovo_dock_device_get_component_id(self,
						    FU_LENOVO_DOCK_COMPONENT_ID_USAGE,
						    NULL,
						    &erase_size,
						    &program_size,
						    error)) {
		g_prefix_error_literal(error, "failed to ensure usage attrs: ");
		return FALSE;
	}

	/* erase and write new table */
	g_byte_array_append(buf, self->st_usage->buf->data, self->st_usage->buf->len);
	for (guint i = 0; i < self->st_usage_items->len; i++) {
		FuStructLenovoDockUsageItem *st_usage_item =
		    g_ptr_array_index(self->st_usage_items, i);
		g_byte_array_append(buf, st_usage_item->buf->data, st_usage_item->buf->len);
	}
	fu_byte_array_set_size(buf, FU_LENOVO_DOCK_DEVICE_USAGE_INFO_SIZE, 0x0);
	if (!fu_lenovo_dock_device_flash_erase_memory(self,
						      FU_LENOVO_DOCK_COMPONENT_ID_USAGE,
						      FU_LENOVO_DOCK_DEVICE_USAGE_INFO_START,
						      buf->len,
						      erase_size,
						      error))
		return FALSE;
	if (!fu_lenovo_dock_device_flash_write_memory(self,
						      FU_LENOVO_DOCK_COMPONENT_ID_USAGE,
						      FU_LENOVO_DOCK_DEVICE_USAGE_INFO_START,
						      buf->data,
						      buf->len,
						      program_size,
						      error))
		return FALSE;

	/* verify on-device CRC */
	if (!fu_lenovo_dock_device_flash_verify_crc(self,
						    FU_LENOVO_DOCK_COMPONENT_ID_USAGE,
						    &crc_actual,
						    error)) {
		g_prefix_error_literal(error, "failed to self verify: ");
		return FALSE;
	}
	crc_calculated = fu_crc32(FU_CRC_KIND_B32_STANDARD, buf->data, buf->len - 4);
	if (crc_actual != crc_calculated) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to write usage, got 0x%04x and expected 0x%04x",
			    crc_actual,
			    crc_calculated);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_device_write_image(FuLenovoDockDevice *self,
				  FuLenovoDockComponentId component_id,
				  FuFirmware *firmware,
				  FwupdInstallFlags flags,
				  GError **error)
{
	guint16 erase_size = 0;
	guint16 program_size = 0;
	guint32 crc_provided;
	guint32 crc_calculated = 0;
	FuLenovoDockFlashIdPurpose purpose = FU_LENOVO_DOCK_FLASH_ID_PURPOSE_FIRMWARE;
	FuStructLenovoDockUsageItem *st_usage_item;
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error_local = NULL;

	/* get image + item */
	img = fu_firmware_get_image_by_idx(firmware, component_id, &error_local);
	if (img == NULL) {
		g_debug("ignoring: %s", error_local->message);
		return TRUE;
	}
	st_usage_item = fu_lenovo_dock_device_get_usage_item(self, component_id, error);
	if (st_usage_item == NULL)
		return FALSE;

	/* get the usage chunk sizes */
	if (!fu_lenovo_dock_device_get_component_id(self,
						    component_id,
						    &purpose,
						    &erase_size,
						    &program_size,
						    error)) {
		g_prefix_error_literal(error, "failed to ensure usage attrs: ");
		return FALSE;
	}
	if (purpose != FU_LENOVO_DOCK_FLASH_ID_PURPOSE_FIRMWARE)
		return TRUE;

	/* is the same version? */
	if (fu_firmware_get_version_raw(img) ==
	    fu_struct_lenovo_dock_usage_item_get_current_version(st_usage_item)) {
		return TRUE;
	}

	/* erase entire block and write blob */
	if (!fu_lenovo_dock_device_flash_erase_memory(
		self,
		component_id,
		fu_struct_lenovo_dock_usage_item_get_address(st_usage_item),
		fu_struct_lenovo_dock_usage_item_get_max_size(st_usage_item),
		erase_size,
		error))
		return FALSE;
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
		error))
		return FALSE;
	if (!fu_lenovo_dock_device_flash_verify_signature(self, component_id, error))
		return FALSE;
	if (!fu_lenovo_dock_device_flash_verify_crc(self, component_id, &crc_calculated, error))
		return FALSE;
	crc_provided = fu_lenovo_dock_image_get_crc(FU_LENOVO_DOCK_IMAGE(img));
	if (crc_provided != crc_calculated) { /* FIXME */
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "usage item provided 0x%04x and calculated 0x%04x",
			    crc_provided,
			    crc_calculated);
		return FALSE;
	}

	/* write target fw version */
	fu_struct_lenovo_dock_usage_item_set_flag(st_usage_item,
						  FU_LENOVO_DOCK_USAGE_ITEM_FLAG_DO_UPDATE);
	fu_struct_lenovo_dock_usage_item_set_target_version(st_usage_item,
							    fu_firmware_get_version_raw(img));
	fu_struct_lenovo_dock_usage_item_set_target_size(st_usage_item, fu_firmware_get_size(img));
	fu_struct_lenovo_dock_usage_item_set_target_crc32(st_usage_item, crc_provided);

	/* write usage table */
	if (!fu_lenovo_dock_device_write_usage(self, error))
		return FALSE;

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

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 20, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 44, NULL);

	/* request access to the SPI */
	if (!fu_lenovo_dock_device_set_flash_memory_access(
		self,
		FU_LENOVO_DOCK_FLASH_MEMORY_ACCESS_CTRL_REQUEST,
		error)) {
		g_prefix_error_literal(error, "failed to request flash memory access: ");
		return FALSE;
	}

	/* get the flash ID list */
	if (!fu_lenovo_dock_device_get_component_id_list(self, &component_id_total, error)) {
		g_prefix_error_literal(error, "failed to get flash ID list: ");
		return FALSE;
	}
	g_debug("flash ID total: 0x%x", component_id_total);
	fu_progress_step_done(progress);

	/* verify existing CRC */
	if (!fu_lenovo_dock_device_verify_usage_info(self, &usage_info_valid, error)) {
		g_prefix_error_literal(error, "failed to validate usage info: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* TODO: check each usage item in the file against the device and set flags = DoUpdate on
	 * each item if not the same */

	/* Set Dock FW Update Ctrl */
	if (!fu_lenovo_dock_device_dfu_control(
		self,
		FU_LENOVO_DOCK_DOCK_FW_CTRL_UPGRADE_STATUS_NON_LOCK,
		FU_LENOVO_DOCK_DOCK_FW_CTRL_UPGRADE_PHASE_CTRL_IN_PHASE1,
		error)) {
		g_prefix_error_literal(error, "failed to trigger phase2: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write the new usage table */
	if (!fu_lenovo_dock_device_write_usage(self, error)) {
		g_prefix_error_literal(error, "failed to write usage table: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write each flash ID */
	total_number = fu_struct_lenovo_dock_usage_get_total_number(self->st_usage);
	for (guint component_id = 1; component_id < total_number; component_id++) {
		if (!fu_lenovo_dock_device_write_image(self, component_id, firmware, flags, error))
			return FALSE;
	}
	fu_progress_step_done(progress);

	/* actually write firmware to the device */
	if (!fu_lenovo_dock_device_set_flash_memory_access(
		self,
		FU_LENOVO_DOCK_FLASH_MEMORY_ACCESS_CTRL_RELEASE,
		error)) {
		g_prefix_error_literal(error, "failed to release flash memory access: ");
		return FALSE;
	}
	if (!fu_lenovo_dock_device_dfu_control(
		self,
		FU_LENOVO_DOCK_DOCK_FW_CTRL_UPGRADE_STATUS_LOCKED,
		FU_LENOVO_DOCK_DOCK_FW_CTRL_UPGRADE_PHASE_CTRL_NON_UNPLUG,
		error)) {
		g_prefix_error_literal(error, "failed to trigger phase2: ");
		return FALSE;
	}
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static void
fu_lenovo_dock_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 57, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 43, "reload");
}

static void
fu_lenovo_dock_device_init(FuLenovoDockDevice *self)
{
	self->st_usage_items =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_struct_lenovo_dock_usage_item_unref);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_protocol(FU_DEVICE(self), "com.lenovo.ldc");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_icon(FU_DEVICE(self), "icon-name");
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_LENOVO_DOCK_FIRMWARE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
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
}
