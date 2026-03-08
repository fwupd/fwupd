/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lenovo-ldc-device.h"
#include "fu-lenovo-ldc-struct.h"

/* this can be set using Flags=example in the quirk file  */
#define FU_LENOVO_LDC_DEVICE_FLAG_EXAMPLE "example"

#define FU_LENOVO_LDC_DEVICE_IFACE1_LEN 64
#define FU_LENOVO_LDC_DEVICE_IFACE2_LEN 272

#define FU_LENOVO_LDC_DEVICE_DELAY   25 /* ms */
#define FU_LENOVO_LDC_DEVICE_RETRIES 1600

#define FU_LENOVO_LDC_DEVICE_USAGE_INFO_SIZE  0x1000
#define FU_LENOVO_LDC_DEVICE_USAGE_INFO_START 0xFFF000

struct _FuLenovoLdcDevice {
	FuHidrawDevice parent_instance;
	guint16 start_addr;
};

G_DEFINE_TYPE(FuLenovoLdcDevice, fu_lenovo_ldc_device, FU_TYPE_HIDRAW_DEVICE)

static void
fu_lenovo_ldc_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuLenovoLdcDevice *self = FU_LENOVO_LDC_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "StartAddr", self->start_addr);
}

static gboolean
fu_lenovo_ldc_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuLenovoLdcDevice *self = FU_LENOVO_LDC_DEVICE(device);

	/* sanity check */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in bootloader mode, skipping");
		return TRUE;
	}

	/* TODO: switch the device into bootloader mode */
	g_assert(self != NULL);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_lenovo_ldc_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuLenovoLdcDevice *self = FU_LENOVO_LDC_DEVICE(device);

	/* sanity check */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in runtime mode, skipping");
		return TRUE;
	}

	/* TODO: switch the device into runtime mode */
	g_assert(self != NULL);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_lenovo_ldc_device_get_report_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuLenovoLdcDevice *self = FU_LENOVO_LDC_DEVICE(device);
	FuLenovoLdcTargetStatus status;
	GByteArray *buf = (GByteArray *)user_data;
	g_autoptr(FuStructLenovoLdcGenericRes) st = NULL;

	if (!fu_hidraw_device_get_report(FU_HIDRAW_DEVICE(self),
					 buf->data,
					 buf->len,
					 FU_IO_CHANNEL_FLAG_NONE,
					 error))
		return FALSE;
	st = fu_struct_lenovo_ldc_generic_res_parse(buf->data, buf->len, 0x0, error);
	if (st == NULL)
		return FALSE;
	status = fu_struct_lenovo_ldc_generic_res_get_target_status(st);
	if (status == FU_LENOVO_LDC_TARGET_STATUS_COMMAND_BUSY) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "not ready");
		return FALSE;
	}
	if (status != FU_LENOVO_LDC_TARGET_STATUS_COMMAND_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_TIMED_OUT,
			    "status was %s",
			    fu_lenovo_ldc_target_status_to_string(status));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_lenovo_ldc_device_get_report(FuLenovoLdcDevice *self, gsize ifacesz, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	fu_byte_array_append_uint8(buf, ifacesz == FU_LENOVO_LDC_DEVICE_IFACE2_LEN ? 0x10 : 0x0);
	fu_byte_array_set_size(buf, ifacesz, 0x0);
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_lenovo_ldc_device_get_report_cb,
				  FU_LENOVO_LDC_DEVICE_RETRIES,
				  FU_LENOVO_LDC_DEVICE_DELAY,
				  buf,
				  error))
		return NULL;

	/* success */
	return g_steal_pointer(&buf);
}

static gboolean
fu_lenovo_ldc_device_set_report(FuLenovoLdcDevice *self,
				GByteArray *buf,
				gsize ifacesz,
				GError **error)
{
	fu_byte_array_set_size(buf, ifacesz, 0x0);
	return fu_hidraw_device_set_report(FU_HIDRAW_DEVICE(self),
					   buf->data,
					   buf->len,
					   FU_IO_CHANNEL_FLAG_NONE,
					   error);
}

static GByteArray *
fu_lenovo_ldc_device_txfer1(FuLenovoLdcDevice *self, GByteArray *buf, GError **error)
{
	if (!fu_lenovo_ldc_device_set_report(self, buf, FU_LENOVO_LDC_DEVICE_IFACE1_LEN, error))
		return NULL;
	return fu_lenovo_ldc_device_get_report(self, FU_LENOVO_LDC_DEVICE_IFACE1_LEN, error);
}

static GByteArray *
fu_lenovo_ldc_device_txfer2(FuLenovoLdcDevice *self, GByteArray *buf, GError **error)
{
	if (!fu_lenovo_ldc_device_set_report(self, buf, FU_LENOVO_LDC_DEVICE_IFACE2_LEN, error))
		return NULL;
	return fu_lenovo_ldc_device_get_report(self, FU_LENOVO_LDC_DEVICE_IFACE2_LEN, error);
}

static gboolean
fu_lenovo_ldc_device_set_flash_memory_access(FuLenovoLdcDevice *self,
					     FuLenovoLdcFlashMemoryAccessCtrl ctrl,
					     GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoLdcSetFlashMemoryAccessReq) st_req =
	    fu_struct_lenovo_ldc_set_flash_memory_access_req_new();
	g_autoptr(FuStructLenovoLdcSetFlashMemoryAccessRes) st_res = NULL;

	fu_struct_lenovo_ldc_set_flash_memory_access_req_set_ctrl(st_req, ctrl);
	buf = fu_lenovo_ldc_device_txfer2(self, st_req->buf, error);
	if (buf == NULL)
		return FALSE;
	st_res =
	    fu_struct_lenovo_ldc_set_flash_memory_access_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_ldc_device_trigger_phase2(FuLenovoLdcDevice *self, GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoLdcDfuControlReq) st_req =
	    fu_struct_lenovo_ldc_dfu_control_req_new();
	g_autoptr(FuStructLenovoLdcDfuControlRes) st_res = NULL;

	buf = fu_lenovo_ldc_device_txfer1(self, st_req->buf, error);
	if (buf == NULL)
		return FALSE;
	st_res = fu_struct_lenovo_ldc_dfu_control_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_ldc_device_ensure_version(FuLenovoLdcDevice *self, GError **error)
{
	g_autofree gchar *version = NULL;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoLdcGetCompositeVersionReq) st_req =
	    fu_struct_lenovo_ldc_get_composite_version_req_new();
	g_autoptr(FuStructLenovoLdcGetCompositeVersionRes) st_res = NULL;

	buf = fu_lenovo_ldc_device_txfer1(self, st_req->buf, error);
	if (buf == NULL)
		return FALSE;
	st_res =
	    fu_struct_lenovo_ldc_get_composite_version_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
	version = g_strdup_printf(
	    "%X.%X.%02X",
	    fu_struct_lenovo_ldc_get_composite_version_res_get_version_major(st_res),
	    fu_struct_lenovo_ldc_get_composite_version_res_get_version_minor(st_res),
	    fu_struct_lenovo_ldc_get_composite_version_res_get_version_micro(st_res));
	fu_device_set_version(FU_DEVICE(self), version);

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_ldc_device_get_flash_id_list(FuLenovoLdcDevice *self,
				       guint8 *flash_id_total,
				       GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoLdcGetFlashIdListReq) st_req =
	    fu_struct_lenovo_ldc_get_flash_id_list_req_new();
	g_autoptr(FuStructLenovoLdcGetFlashIdListRes) st_res = NULL;

	buf = fu_lenovo_ldc_device_txfer1(self, st_req->buf, error);
	if (buf == NULL)
		return FALSE;
	st_res = fu_struct_lenovo_ldc_get_flash_id_list_res_parse(buf->data, buf->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
	if (flash_id_total != NULL)
		*flash_id_total = fu_struct_lenovo_ldc_get_flash_id_list_res_get_total(st_res);

	/* success */
	return TRUE;
}

static GByteArray *
fu_lenovo_ldc_device_flash_read_memory(FuLenovoLdcDevice *self,
				       guint32 addr,
				       gsize datasz,
				       GError **error)
{
	g_autoptr(GByteArray) data = g_byte_array_new();

	for (gsize i = 0; i < datasz; i += 256) {
		const guint8 *datatmp;
		gsize datatmpsz = 0;
		g_autoptr(GByteArray) buf = NULL;
		g_autoptr(FuStructLenovoLdcDockReadWithAddressReq) st_req =
		    fu_struct_lenovo_ldc_dock_read_with_address_req_new();
		g_autoptr(FuStructLenovoLdcDockReadWithAddressRes) st_res = NULL;

		fu_struct_lenovo_ldc_dock_read_with_address_req_set_size(st_req, 256);
		fu_struct_lenovo_ldc_dock_read_with_address_req_set_addr(st_req, addr + i);
		buf = fu_lenovo_ldc_device_txfer2(self, st_req->buf, error);
		if (buf == NULL)
			return NULL;
		st_res = fu_struct_lenovo_ldc_dock_read_with_address_res_parse(buf->data,
									       buf->len,
									       0x0,
									       error);
		if (st_res == NULL)
			return NULL;
		datatmp =
		    fu_struct_lenovo_ldc_dock_read_with_address_res_get_data(st_res, &datatmpsz);
		g_byte_array_append(data, datatmp, datatmpsz);
	}

	/* success */
	return g_steal_pointer(&data);
}

static gboolean
fu_lenovo_ldc_device_setup(FuDevice *device, GError **error)
{
	FuLenovoLdcDevice *self = FU_LENOVO_LDC_DEVICE(device);

	/* FuhidrawdeviceDevice->setup */
	if (!FU_DEVICE_CLASS(fu_lenovo_ldc_device_parent_class)->setup(device, error))
		return FALSE;

	/* get version */
	if (!fu_lenovo_ldc_device_ensure_version(self, error)) {
		g_prefix_error_literal(error, "failed to ensure version: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_ldc_device_prepare(FuDevice *device,
			     FuProgress *progress,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuLenovoLdcDevice *self = FU_LENOVO_LDC_DEVICE(device);
	/* TODO: anything the device has to do before the update starts */
	g_assert(self != NULL);
	return TRUE;
}

static gboolean
fu_lenovo_ldc_device_cleanup(FuDevice *device,
			     FuProgress *progress,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuLenovoLdcDevice *self = FU_LENOVO_LDC_DEVICE(device);
	/* TODO: anything the device has to do when the update has completed */
	g_assert(self != NULL);
	return TRUE;
}

static gboolean
fu_lenovo_ldc_device_write_blocks(FuLenovoLdcDevice *self,
				  FuChunkArray *chunks,
				  FuProgress *progress,
				  GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		/* TODO: send to hardware */

		/* update progress */
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_ldc_device_verify_usage_info(FuLenovoLdcDevice *self, gboolean *valid, GError **error)
{
	guint32 crc_actual;
	guint32 crc_caclulated;
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_lenovo_ldc_device_flash_read_memory(self,
						     FU_LENOVO_LDC_DEVICE_USAGE_INFO_START,
						     FU_LENOVO_LDC_DEVICE_USAGE_INFO_SIZE,
						     error);
	if (buf == NULL) {
		g_prefix_error_literal(error, "failed to get usage info: ");
		return FALSE;
	}
	if (buf->len < FU_LENOVO_LDC_DEVICE_USAGE_INFO_SIZE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "not enough usage info");
		return FALSE;
	}
	crc_caclulated = fu_crc32(FU_CRC_KIND_B32_STANDARD, buf->data, buf->len - 4);
	crc_actual = fu_memread_uint32(buf->data - 4, G_LITTLE_ENDIAN);
	g_debug("usage info CRC got 0x%4x, expected 0x%4x", crc_actual, crc_caclulated);

	/* success */
	if (valid != NULL)
		*valid = crc_caclulated == crc_actual;
	return TRUE;
}

static gboolean
fu_lenovo_ldc_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuLenovoLdcDevice *self = FU_LENOVO_LDC_DEVICE(device);
	gboolean usage_info_valid = FALSE;
	guint8 flash_id_total = 0;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 44, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 35, NULL);

	if (!fu_lenovo_ldc_device_set_flash_memory_access(
		self,
		FU_LENOVO_LDC_FLASH_MEMORY_ACCESS_CTRL_REQUEST,
		error)) {
		g_prefix_error_literal(error, "failed to request flash memory access: ");
		return FALSE;
	}

	/* get the flash ID list */
	if (!fu_lenovo_ldc_device_get_flash_id_list(self, &flash_id_total, error)) {
		g_prefix_error_literal(error, "failed to get flash ID list: ");
		return FALSE;
	}
	g_debug("flash ID total: 0x%x", flash_id_total);

	/* verify existing CRC */
	if (!fu_lenovo_ldc_device_verify_usage_info(self, &usage_info_valid, error)) {
		g_prefix_error_literal(error, "failed to validate usage info: ");
		return FALSE;
	}

	/* get default image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	/* write each block */
	chunks = fu_chunk_array_new_from_stream(stream,
						self->start_addr,
						FU_CHUNK_PAGESZ_NONE,
						64 /* block_size */,
						error);
	if (chunks == NULL)
		return FALSE;
	if (!fu_lenovo_ldc_device_write_blocks(self,
					       chunks,
					       fu_progress_get_child(progress),
					       error))
		return FALSE;
	fu_progress_step_done(progress);

	/* TODO: verify each block */
	fu_progress_step_done(progress);

	/* release */
	if (!fu_lenovo_ldc_device_set_flash_memory_access(
		self,
		FU_LENOVO_LDC_FLASH_MEMORY_ACCESS_CTRL_RELEASE,
		error)) {
		g_prefix_error_literal(error, "failed to release flash memory access: ");
		return FALSE;
	}
	if (!fu_lenovo_ldc_device_trigger_phase2(self, error)) {
		g_prefix_error_literal(error, "failed to trigger phase2: ");
		return FALSE;
	}

	/* success! */
	return TRUE;
}

static gboolean
fu_lenovo_ldc_device_set_quirk_kv(FuDevice *device,
				  const gchar *key,
				  const gchar *value,
				  GError **error)
{
	FuLenovoLdcDevice *self = FU_LENOVO_LDC_DEVICE(device);

	/* TODO: parse value from quirk file */
	if (g_strcmp0(key, "LenovoLdcStartAddr") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->start_addr = tmp;
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
fu_lenovo_ldc_device_set_progress(FuDevice *self, FuProgress *progress)
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
fu_lenovo_ldc_device_init(FuLenovoLdcDevice *self)
{
	self->start_addr = 0x5000;
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_protocol(FU_DEVICE(self), "com.lenovo.ldc");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_icon(FU_DEVICE(self), "icon-name");
	fu_device_set_firmware_size(FU_DEVICE(self), 0xFFF000);
	fu_device_register_private_flag(FU_DEVICE(self), FU_LENOVO_LDC_DEVICE_FLAG_EXAMPLE);
}

static void
fu_lenovo_ldc_device_finalize(GObject *object)
{
	FuLenovoLdcDevice *self = FU_LENOVO_LDC_DEVICE(object);

	/* TODO: free any allocated instance state here */
	g_assert(self != NULL);

	G_OBJECT_CLASS(fu_lenovo_ldc_device_parent_class)->finalize(object);
}

static void
fu_lenovo_ldc_device_class_init(FuLenovoLdcDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_lenovo_ldc_device_finalize;
	device_class->to_string = fu_lenovo_ldc_device_to_string;
	device_class->setup = fu_lenovo_ldc_device_setup;
	device_class->prepare = fu_lenovo_ldc_device_prepare;
	device_class->cleanup = fu_lenovo_ldc_device_cleanup;
	device_class->attach = fu_lenovo_ldc_device_attach;
	device_class->detach = fu_lenovo_ldc_device_detach;
	device_class->write_firmware = fu_lenovo_ldc_device_write_firmware;
	device_class->set_quirk_kv = fu_lenovo_ldc_device_set_quirk_kv;
	device_class->set_progress = fu_lenovo_ldc_device_set_progress;
}
