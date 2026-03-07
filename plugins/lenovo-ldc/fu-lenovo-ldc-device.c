/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lenovo-ldc-common.h"
#include "fu-lenovo-ldc-device.h"
#include "fu-lenovo-ldc-struct.h"

/* this can be set using Flags=example in the quirk file  */
#define FU_LENOVO_LDC_DEVICE_FLAG_EXAMPLE "example"

#define FU_LENOVO_LDC_DEVICE_IFACE1_LEN 64
#define FU_LENOVO_LDC_DEVICE_IFACE2_LEN 272

#define FU_LENOVO_LDC_DEVICE_DELAY   25 /* ms */
#define FU_LENOVO_LDC_DEVICE_RETRIES 1600

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

/* TODO: this is only required if the device instance state is required elsewhere */
guint16
fu_lenovo_ldc_device_get_start_addr(FuLenovoLdcDevice *self)
{
	g_return_val_if_fail(FU_IS_LENOVO_LDC_DEVICE(self), G_MAXUINT16);
	return self->start_addr;
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
fu_lenovo_ldc_device_reload(FuDevice *device, GError **error)
{
	FuLenovoLdcDevice *self = FU_LENOVO_LDC_DEVICE(device);
	/* TODO: reprobe the hardware, or delete this vfunc to use ->setup() as a fallback */
	g_assert(self != NULL);
	return TRUE;
}

static gboolean
fu_lenovo_ldc_device_probe(FuDevice *device, GError **error)
{
	FuLenovoLdcDevice *self = FU_LENOVO_LDC_DEVICE(device);

	/* FuFuhidrawdeviceDevice->probe */
	if (!FU_DEVICE_CLASS(fu_lenovo_ldc_device_parent_class)->probe(device, error))
		return FALSE;

	/* TODO: probe the device for properties available before it is opened */
	if (fu_device_has_private_flag(device, FU_LENOVO_LDC_DEVICE_FLAG_EXAMPLE))
		self->start_addr = 0x100;
	/* success */
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
fu_lenovo_ldc_device_get_report(FuLenovoLdcDevice *self, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	fu_byte_array_set_size(buf, FU_LENOVO_LDC_DEVICE_IFACE1_LEN, 0x0);
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
fu_lenovo_ldc_device_set_report(FuLenovoLdcDevice *self, GByteArray *buf, GError **error)
{
	fu_byte_array_set_size(buf, FU_LENOVO_LDC_DEVICE_IFACE1_LEN, 0x0);
	return fu_hidraw_device_set_report(FU_HIDRAW_DEVICE(self),
					   buf->data,
					   buf->len,
					   FU_IO_CHANNEL_FLAG_NONE,
					   error);
}

static gboolean
fu_lenovo_ldc_device_ensure_version(FuLenovoLdcDevice *self, GError **error)
{
	g_autofree gchar *version = NULL;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoLdcGetCompositeVersionReq) st_req =
	    fu_struct_lenovo_ldc_get_composite_version_req_new();
	g_autoptr(FuStructLenovoLdcGetCompositeVersionRes) st_res = NULL;

	if (!fu_lenovo_ldc_device_set_report(self, st_req->buf, error))
		return FALSE;
	buf = fu_lenovo_ldc_device_get_report(self, error);
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
fu_lenovo_ldc_device_setup(FuDevice *device, GError **error)
{
	FuLenovoLdcDevice *self = FU_LENOVO_LDC_DEVICE(device);

	/* FuhidrawdeviceDevice->setup */
	if (!FU_DEVICE_CLASS(fu_lenovo_ldc_device_parent_class)->setup(device, error))
		return FALSE;

	/* get version */
	if (!fu_lenovo_ldc_device_ensure_version(self, error))
		return FALSE;

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
fu_lenovo_ldc_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuLenovoLdcDevice *self = FU_LENOVO_LDC_DEVICE(device);
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 44, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 35, NULL);

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
	device_class->probe = fu_lenovo_ldc_device_probe;
	device_class->setup = fu_lenovo_ldc_device_setup;
	device_class->reload = fu_lenovo_ldc_device_reload;
	device_class->prepare = fu_lenovo_ldc_device_prepare;
	device_class->cleanup = fu_lenovo_ldc_device_cleanup;
	device_class->attach = fu_lenovo_ldc_device_attach;
	device_class->detach = fu_lenovo_ldc_device_detach;
	device_class->write_firmware = fu_lenovo_ldc_device_write_firmware;
	device_class->set_quirk_kv = fu_lenovo_ldc_device_set_quirk_kv;
	device_class->set_progress = fu_lenovo_ldc_device_set_progress;
}
