/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-sunwinon-hid-device.h"
#include "fu-sunwinon-hid-struct.h"
#include "fu-sunwinon-util-dfu-master.h"

struct _FuSunwinonHidDevice {
	FuHidrawDevice parent_instance;
};

G_DEFINE_TYPE(FuSunwinonHidDevice, fu_sunwinon_hid_device, FU_TYPE_HIDRAW_DEVICE)

#define DFU_IMAGE_INFO_LEN 48
#define HID_REPORT_DATA_LEN 480

typedef struct {
	FuDevice *device;
	FuProgress *progress;
	const guint8 *fw;
	gsize fw_sz;
	FuDfuMaster *master;
	FuSunwinonDfuImageInfo img_info;
	guint32 fw_save_addr;
	gboolean done;
	gboolean failed;
	char *fail_reason;
} FuSwHidDfuCtx;

static gboolean
fu_sunwinon_hid_device_send(FuSwHidDfuCtx *self, const guint8 *payload, guint16 len, GError **error)
{
	if (len > HID_REPORT_DATA_LEN)
		len = HID_REPORT_DATA_LEN;
	g_autoptr(FuStructSunwinonHidOut) st_out = fu_struct_sunwinon_hid_out_new();
	fu_struct_sunwinon_hid_out_set_data_len(st_out, len);
	if (!fu_struct_sunwinon_hid_out_set_data(st_out, payload, len, error))
		return FALSE;
	if (!fu_hidraw_device_set_report(FU_HIDRAW_DEVICE(self->device),
					 st_out->buf->data,
					 st_out->buf->len,
					 FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
					 error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_sunwinon_hid_device_dfu_send_data(void *user_data, guint8 *data, guint16 len, GError **error)
{
	FuSwHidDfuCtx *self = (FuSwHidDfuCtx *)user_data;
	return fu_sunwinon_hid_device_send(self, data, len, error);
}

static gboolean
fu_sunwinon_hid_device_dfu_get_img_info(void *user_data,
					FuSunwinonDfuImageInfo *img_info,
					GError **error)
{
	FuSwHidDfuCtx *self = (FuSwHidDfuCtx *)user_data;
	return fu_memcpy_safe((guint8 *)img_info,
			      sizeof(FuSunwinonDfuImageInfo),
			      0,
			      (const guint8 *)&self->img_info,
			      sizeof(self->img_info),
			      0,
			      sizeof(FuSunwinonDfuImageInfo),
			      error);
}

static gboolean
fu_sunwinon_hid_device_dfu_get_img_data(void *user_data,
					guint32 addr,
					guint8 *data,
					guint16 len,
					GError **error)
{
	FuSwHidDfuCtx *self = (FuSwHidDfuCtx *)user_data;
	guint32 off = 0;
	if (addr >= self->fw_save_addr)
		off = addr - self->fw_save_addr;
	if ((guint64)off + len > self->fw_sz)
		len = (guint16)MAX((gint)0, (gint)(self->fw_sz > off ? self->fw_sz - off : 0));
	{
		gsize avail = (self->fw_sz > off) ? (self->fw_sz - off) : 0;
		return fu_memcpy_safe(data,
				      len,
				      0,
				      (const guint8 *)(self->fw + off),
				      avail,
				      0,
				      len,
				      error);
	}
}

static guint32
fu_sunwinon_hid_device_dfu_get_time(void *user_data)
{
	return (guint32)(g_get_monotonic_time() / 1000);
}

static void
fu_sunwinon_hid_device_dfu_wait(void *user_data, guint32 ms)
{
	FuSwHidDfuCtx *self = (FuSwHidDfuCtx *)user_data;
	fu_device_sleep(FU_DEVICE(self->device), ms);
}

static void
fu_sunwinon_hid_device_dfu_event_handler(void *user_data, FuSunwinonDfuEvent event, guint8 progress)
{
	FuSwHidDfuCtx *self = (FuSwHidDfuCtx *)user_data;
	switch (event) {
	case FU_SUNWINON_DFU_EVENT_PRO_START_SUCCESS:
		break;
	case FU_SUNWINON_DFU_EVENT_PRO_FLASH_SUCCESS:
	case FU_SUNWINON_DFU_EVENT_FAST_DFU_PRO_FLASH_SUCCESS:
		if (self->progress != NULL)
			fu_progress_set_percentage(self->progress, progress);
		break;
	case FU_SUNWINON_DFU_EVENT_PRO_END_SUCCESS:
		if (self->progress != NULL)
			fu_progress_set_percentage(self->progress, 100);
		self->done = TRUE;
		break;
	case FU_SUNWINON_DFU_EVENT_DFU_ACK_TIMEOUT:
		g_clear_pointer(&self->fail_reason, g_free);
		self->fail_reason = g_strdup("dfu ack timeout");
		self->failed = TRUE;
		self->done = TRUE;
		break;
	case FU_SUNWINON_DFU_EVENT_PRO_END_FAIL:
		g_clear_pointer(&self->fail_reason, g_free);
		self->fail_reason = g_strdup("program end failed");
		self->failed = TRUE;
		self->done = TRUE;
		break;
	default:
		break;
	}
}

static void
fu_sunwinon_hid_device_dfu_setup_callbacks(FuSunwinonDfuCallback *callback)
{
	callback->dfu_m_send_data = fu_sunwinon_hid_device_dfu_send_data;
	callback->dfu_m_get_img_info = fu_sunwinon_hid_device_dfu_get_img_info;
	callback->dfu_m_get_img_data = fu_sunwinon_hid_device_dfu_get_img_data;
	callback->dfu_m_get_time = fu_sunwinon_hid_device_dfu_get_time;
	callback->dfu_m_event_handler = fu_sunwinon_hid_device_dfu_event_handler;
	callback->dfu_m_wait = fu_sunwinon_hid_device_dfu_wait;
}

static void
fu_sunwinon_hid_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-firmware");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gboolean
fu_sunwinon_hid_device_probe(FuDevice *device, GError **error)
{
	fu_device_add_instance_id(device, "SUNWINON_HID");
	return TRUE;
}

static gboolean
fu_sunwinon_hid_device_fetch_fw_version(FuSunwinonHidDevice *device, GError **error)
{
	FuSwHidDfuCtx ctx = {0};
	FuSunwinonDfuCallback cfg = {0};
	ctx.device = FU_DEVICE(device);
	cfg.user_data = &ctx;
	fu_sunwinon_hid_device_dfu_setup_callbacks(&cfg);
	g_autoptr(FuDfuMaster) master = fu_sunwinon_util_dfu_master_new(&cfg, HID_REPORT_DATA_LEN);
	ctx.master = master;
	if (!fu_sunwinon_util_dfu_master_send_fw_info_get(master, error))
		return FALSE;
	/* wait for response */
	g_autoptr(FuStructSunwinonHidIn) st_in = fu_struct_sunwinon_hid_in_new();
	(void)fu_hidraw_device_get_report(FU_HIDRAW_DEVICE(device),
					  st_in->buf->data,
					  st_in->buf->len,
					  FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
					  NULL);
	fu_dump_raw(G_LOG_DOMAIN, "Raw Input Report", st_in->buf->data, st_in->buf->len);
	if (!fu_struct_sunwinon_hid_in_validate(st_in->buf->data, st_in->buf->len, 0x0, error))
		return FALSE;
	FuSunwinonDfuImageInfo fw_info = {0};
	guint16 inlen = fu_struct_sunwinon_hid_in_get_data_len(st_in);
	if (inlen > HID_REPORT_DATA_LEN)
		inlen = HID_REPORT_DATA_LEN;
	const guint8 *payload = fu_struct_sunwinon_hid_in_get_data(st_in, NULL);
	if (!fu_sunwinon_util_dfu_master_parse_fw_info(master, &fw_info, payload, inlen, error))
		return FALSE;
	g_debug("SunwinonHid: Firmware version fetched: %u.%u",
		(guint)((fw_info.version >> 8) & 0xFF),
		(guint)(fw_info.version & 0xFF));
	fu_device_set_version(FU_DEVICE(device),
			      g_strdup_printf("%u.%u",
					      (guint)((fw_info.version >> 8) & 0xFF),
					      (guint)(fw_info.version & 0xFF)));
	return TRUE;
}

static gboolean
fu_sunwinon_hid_device_check_update_channel(FuHidDescriptor *desc, GError **error)
{
	g_return_val_if_fail(desc != NULL, FALSE);
	g_autoptr(FuHidReport) report_out =
	    fu_hid_descriptor_find_report(desc,
					  error,
					  "report-id",
					  FU_SUNWINON_HID_REPORT_CHANNEL_ID,
					  "usage",
					  0x02,
					  "output",
					  0x00,
					  NULL);
	if (report_out == NULL)
		return FALSE;

	g_autoptr(FuHidReport) report_in =
	    fu_hid_descriptor_find_report(desc,
					  error,
					  "report-id",
					  FU_SUNWINON_HID_REPORT_CHANNEL_ID,
					  "usage",
					  0x02,
					  "input",
					  0x00,
					  NULL);
	if (report_in == NULL)
		return FALSE;

	return TRUE;
}

static gboolean
fu_sunwinon_hid_device_setup(FuDevice *device, GError **error)
{
	g_autoptr(FuHidDescriptor) descriptor =
	    fu_hidraw_device_parse_descriptor(FU_HIDRAW_DEVICE(device), error);
	if (descriptor == NULL)
		return FALSE;
	g_debug("SunwinonHid: HID Descriptor parsed successfully");
	if (!fu_sunwinon_hid_device_check_update_channel(descriptor, error))
		return FALSE;
	if (!fu_sunwinon_hid_device_fetch_fw_version(FU_SUNWINON_HID_DEVICE(device), error))
		return FALSE;
	/* HID report descriptor and fw version confirmed, now device is ready to update */
	fu_device_add_instance_id(device, "SUNWINON_HID");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	return TRUE;
}

static void
fu_sunwinon_hid_device_init(FuSunwinonHidDevice *self)
{
	g_debug("SunwinonHid: Initializing Sunwinon BLE HID Device");
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_INPUT_TABLET);
	fu_device_set_id(FU_DEVICE(self), "SunwinonHidTest");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_add_protocol(FU_DEVICE(self), "com.sunwinon.hid");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_RETRY_OPEN);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
}

static gboolean
fu_sunwinon_hid_device_write_firmware(FuDevice *device,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuSwHidDfuCtx ctx = {0};
	FuSunwinonDfuCallback cfg = {0};
	g_autoptr(FuDeviceLocker) locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	g_autoptr(GBytes) blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;
	ctx.fw = g_bytes_get_data(blob, &ctx.fw_sz);
	if (ctx.fw_sz < DFU_IMAGE_INFO_LEN) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware too small");
		return FALSE;
	}

	{
		gsize off = ctx.fw_sz - DFU_IMAGE_INFO_LEN;
		gsize avail = (ctx.fw_sz > off) ? (ctx.fw_sz - off) : 0;
		if (!fu_memcpy_safe((guint8 *)&ctx.img_info,
				    sizeof(ctx.img_info),
				    0,
				    (const guint8 *)(ctx.fw + off),
				    avail,
				    0,
				    sizeof(FuSunwinonDfuImageInfo),
				    error))
			return FALSE;
	}

	ctx.device = device;
	ctx.progress = progress;
	ctx.done = FALSE;
	ctx.failed = FALSE;
	g_clear_pointer(&ctx.fail_reason, g_free);
	ctx.fw_save_addr = ctx.img_info.boot_info.load_addr;

	fu_sunwinon_hid_device_dfu_setup_callbacks(&cfg);
	cfg.user_data = &ctx;
	g_autoptr(FuDfuMaster) master = fu_sunwinon_util_dfu_master_new(&cfg, HID_REPORT_DATA_LEN);
	ctx.master = master;
	fu_sunwinon_util_dfu_master_fast_dfu_mode_set(master, FU_SUNWINON_FAST_DFU_MODE_DISABLE);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_sunwinon_util_dfu_master_start(master, error))
		return FALSE;

	g_autoptr(FuStructSunwinonHidIn) st_in = fu_struct_sunwinon_hid_in_new();
	while (TRUE) {
		if (!fu_sunwinon_util_dfu_master_schedule(master, error))
			return FALSE;
		if (ctx.done || ctx.failed)
			break;
		(void)fu_hidraw_device_get_report(FU_HIDRAW_DEVICE(device),
						  st_in->buf->data,
						  st_in->buf->len,
						  FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
						  NULL);
		fu_dump_raw(G_LOG_DOMAIN, "Raw Input Report", st_in->buf->data, st_in->buf->len);
		if (!fu_struct_sunwinon_hid_in_validate(st_in->buf->data,
							st_in->buf->len,
							0x0,
							error))
			return FALSE;
		guint16 inlen = fu_struct_sunwinon_hid_in_get_data_len(st_in);
		if (inlen > HID_REPORT_DATA_LEN)
			inlen = HID_REPORT_DATA_LEN;
		const guint8 *payload = fu_struct_sunwinon_hid_in_get_data(st_in, NULL);
		fu_sunwinon_util_dfu_master_cmd_parse(master, payload, inlen);
	}

	if (!ctx.done) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "dfu did not complete");
		return FALSE;
	}
	if (ctx.failed) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    ctx.fail_reason != NULL ? ctx.fail_reason : "dfu failed");
		return FALSE;
	}
	return TRUE;
}

static void
fu_sunwinon_hid_device_class_init(FuSunwinonHidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_sunwinon_hid_device_probe;
	device_class->setup = fu_sunwinon_hid_device_setup;
	device_class->write_firmware = fu_sunwinon_hid_device_write_firmware;
	device_class->set_progress = fu_sunwinon_hid_device_set_progress;
}
