/*
 * Copyright 2026 Yuchao Li <liyc44@lenovo.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lenovo-accessory-hid-child-device.h"
#include "fu-lenovo-accessory-hid-common.h"

/* peripheral commands go through BT relay and need more time */
#define FU_LENOVO_ACCESSORY_CHILD_RETRY_COUNT	 500
#define FU_LENOVO_ACCESSORY_CHILD_RETRY_DELAY_MS 10

/* maximum number of times a stale response frame is flushed by re-issuing the
 * request before giving up */
#define FU_LENOVO_ACCESSORY_CHILD_STALE_REWRITE_MAX 2

/* the dongle pushes asynchronous notifications (e.g. a peripheral coming back
 * online after a reboot) on this interrupt-IN interface */
#define FU_LENOVO_ACCESSORY_IFACE_NOTIFY 0x02

/* how long to wait for a rebooted peripheral to re-announce itself; treated as
 * a hard failure if no online notification arrives within the window */
#define FU_LENOVO_ACCESSORY_CHILD_REBOOT_TIMEOUT_MS 5000

/* after the peripheral re-announces itself it still needs a moment to finish
 * its own init before it can answer commands, so settle before setup */
#define FU_LENOVO_ACCESSORY_CHILD_REBOOT_SETTLE_MS 1000

struct _FuLenovoAccessoryHidChildDevice {
	FuDevice parent_instance;
	guint target_slot;
	guint16 pid;
};

static void
fu_lenovo_accessory_hid_child_device_impl_iface_init(FuLenovoAccessoryImplInterface *iface);

G_DEFINE_TYPE_WITH_CODE(FuLenovoAccessoryHidChildDevice,
			fu_lenovo_accessory_hid_child_device,
			FU_TYPE_DEVICE,
			G_IMPLEMENT_INTERFACE(FU_TYPE_LENOVO_ACCESSORY_IMPL,
					      fu_lenovo_accessory_hid_child_device_impl_iface_init))

guint16
fu_lenovo_accessory_hid_child_device_get_pid(FuLenovoAccessoryHidChildDevice *self)
{
	g_return_val_if_fail(FU_IS_LENOVO_ACCESSORY_HID_CHILD_DEVICE(self), 0);
	return self->pid;
}

void
fu_lenovo_accessory_hid_child_device_set_pid(FuLenovoAccessoryHidChildDevice *self, guint16 pid)
{
	g_return_if_fail(FU_IS_LENOVO_ACCESSORY_HID_CHILD_DEVICE(self));
	self->pid = pid;
}

void
fu_lenovo_accessory_hid_child_device_set_target_slot(FuLenovoAccessoryHidChildDevice *self,
						     guint8 target_slot)
{
	g_return_if_fail(FU_IS_LENOVO_ACCESSORY_HID_CHILD_DEVICE(self));
	self->target_slot = target_slot;
}

static void
fu_lenovo_accessory_hid_child_device_set_target(FuLenovoAccessoryHidChildDevice *self,
						GByteArray *buf)
{
	g_return_if_fail(FU_IS_LENOVO_ACCESSORY_HID_CHILD_DEVICE(self));
	g_return_if_fail(buf != NULL);
	/*
	 * Commands to a paired peripheral are relayed through the dongle. The
	 * high nibble of the first command byte selects the target pairing slot
	 * for the relay; the low nibble (status) is unused in the request.
	 */
	if (buf->len > 0)
		buf->data[0] = self->target_slot << 4;
}

static GByteArray *
fu_lenovo_accessory_hid_child_device_read(FuLenovoAccessoryImpl *impl, GError **error)
{
	FuDevice *proxy;

	proxy = fu_device_get_proxy(FU_DEVICE(impl), error);
	if (proxy == NULL)
		return NULL;
	return fu_lenovo_accessory_hid_read(FU_LENOVO_ACCESSORY_IMPL(proxy), error);
}

static gboolean
fu_lenovo_accessory_hid_child_device_write(FuLenovoAccessoryImpl *impl,
					   GByteArray *buf,
					   GError **error)
{
	FuDevice *proxy;

	fu_lenovo_accessory_hid_child_device_set_target(FU_LENOVO_ACCESSORY_HID_CHILD_DEVICE(impl),
							buf);
	proxy = fu_device_get_proxy(FU_DEVICE(impl), error);
	if (proxy == NULL)
		return FALSE;
	return fu_lenovo_accessory_hid_write(FU_LENOVO_ACCESSORY_IMPL(proxy), buf, error);
}

typedef struct {
	guint8 req_cmd_class;
	guint8 req_cmd_id;
	guint stale_rewrites;
	GByteArray *buf_req;
	GByteArray *buf_rsp;
} FuLenovoAccessoryChildPollHelper;

static gboolean
fu_lenovo_accessory_hid_child_device_poll_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuLenovoAccessoryHidChildDevice *self = FU_LENOVO_ACCESSORY_HID_CHILD_DEVICE(device);
	FuLenovoAccessoryChildPollHelper *helper = (FuLenovoAccessoryChildPollHelper *)user_data;
	GByteArray *buf_rsp = helper->buf_rsp;
	FuDevice *proxy;
	guint8 target_status;
	guint8 rsp_cmd_class;
	guint8 rsp_cmd_id;
	FuLenovoAccessoryStatus status;
	guint8 rsp_slot;
	gsize offset = 0x0;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = NULL;

	proxy = fu_device_get_proxy(device, error);
	if (proxy == NULL)
		return FALSE;
	buf = fu_lenovo_accessory_hid_read(FU_LENOVO_ACCESSORY_IMPL(proxy), error);
	if (buf == NULL) {
		g_prefix_error_literal(error, "failed to read cmd: ");
		return FALSE;
	}

	/* control-transfer frame has no report-id prefix */
	st_cmd = fu_struct_lenovo_accessory_cmd_parse(buf->data, buf->len, offset, error);
	if (st_cmd == NULL)
		return FALSE;
	target_status = fu_struct_lenovo_accessory_cmd_get_target_status(st_cmd);

	/* the relayed response must come from the slot we addressed,
	 * otherwise a frame for a different peripheral leaked onto the channel;
	 * retry the read */
	rsp_slot = target_status >> 4;
	if (rsp_slot != self->target_slot) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
			    "slot mismatch: requested 0x%x, got 0x%x",
			    self->target_slot,
			    rsp_slot);
		return FALSE;
	}

	/* the relay can leave a frame from a previous command in the report
	 * buffer; the command_id carries the direction bit (e.g. GET 0x03 -> 0x83)
	 * so the answer must echo our class/id exactly. Detect this before the
	 * status check, as a stale frame may carry an unrelated status (e.g. a
	 * timeout from the previous command) that would otherwise be misread as a
	 * failure of the current command. Re-issue the write to flush it */
	rsp_cmd_class = fu_struct_lenovo_accessory_cmd_get_command_class(st_cmd);
	rsp_cmd_id = fu_struct_lenovo_accessory_cmd_get_command_id(st_cmd);
	if (rsp_cmd_class != helper->req_cmd_class || rsp_cmd_id != helper->req_cmd_id) {
		if (helper->stale_rewrites >= FU_LENOVO_ACCESSORY_CHILD_STALE_REWRITE_MAX) {
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
		helper->stale_rewrites++;
		g_debug("stale frame (rewrite %u): requested class 0x%02x id 0x%02x, "
			"got class 0x%02x id 0x%02x",
			helper->stale_rewrites,
			helper->req_cmd_class,
			helper->req_cmd_id,
			rsp_cmd_class,
			rsp_cmd_id);
		if (!fu_lenovo_accessory_hid_child_device_write(FU_LENOVO_ACCESSORY_IMPL(device),
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

static GByteArray *
fu_lenovo_accessory_hid_child_device_process(FuLenovoAccessoryImpl *impl,
					     GByteArray *buf,
					     GError **error)
{
	g_autoptr(GByteArray) buf_rsp = g_byte_array_new();
	g_autoptr(FuStructLenovoAccessoryCmd) st_req = NULL;
	FuLenovoAccessoryChildPollHelper helper = {0};

	/* the command_class/command_id that the response must echo back; the
	 * command_id already carries the direction bit (e.g. GET 0x03 -> 0x83) */
	st_req = fu_struct_lenovo_accessory_cmd_parse(buf->data, buf->len, 0x0, error);
	if (st_req == NULL)
		return NULL;
	helper.req_cmd_class = fu_struct_lenovo_accessory_cmd_get_command_class(st_req);
	helper.req_cmd_id = fu_struct_lenovo_accessory_cmd_get_command_id(st_req);
	helper.buf_req = buf;
	helper.buf_rsp = buf_rsp;

	if (!fu_lenovo_accessory_hid_child_device_write(impl, buf, error)) {
		g_prefix_error_literal(error, "failed to write cmd: ");
		return NULL;
	}
	if (!fu_device_retry_full(FU_DEVICE(impl),
				  fu_lenovo_accessory_hid_child_device_poll_cb,
				  FU_LENOVO_ACCESSORY_CHILD_RETRY_COUNT,
				  FU_LENOVO_ACCESSORY_CHILD_RETRY_DELAY_MS,
				  &helper,
				  error))
		return NULL;
	return g_steal_pointer(&buf_rsp);
}

static gboolean
fu_lenovo_accessory_hid_child_device_setup(FuDevice *device, GError **error)
{
	guint8 major = 0;
	guint8 minor = 0;
	guint8 micro = 0;
	g_autofree gchar *version = NULL;
	if (!fu_lenovo_accessory_impl_get_fwversion(FU_LENOVO_ACCESSORY_IMPL(device),
						    &major,
						    &minor,
						    &micro,
						    error))
		return FALSE;
	version = g_strdup_printf("%u.%u.%u", major, minor, micro);
	fu_device_set_version(device, version);
	return TRUE;
}

static gboolean
fu_lenovo_accessory_hid_child_device_write_files(FuLenovoAccessoryHidChildDevice *self,
						 FuLenovoAccessoryDfuFileType file_type,
						 GInputStream *stream,
						 FuProgress *progress,
						 GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;

	chunks = fu_chunk_array_new_from_stream(stream, 0, 0, 32, error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint32 i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_lenovo_accessory_impl_dfu_file(FU_LENOVO_ACCESSORY_IMPL(self),
						       file_type,
						       fu_chunk_get_address(chk),
						       fu_chunk_get_data(chk),
						       fu_chunk_get_data_sz(chk),
						       error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_accessory_hid_child_device_write_firmware(FuDevice *device,
						    FuFirmware *firmware,
						    FuProgress *progress,
						    FwupdInstallFlags flags,
						    GError **error)
{
	FuLenovoAccessoryHidChildDevice *self = FU_LENOVO_ACCESSORY_HID_CHILD_DEVICE(device);
	gsize fw_size = 0;
	guint32 file_crc = 0xFFFFFFFF;
	guint32 device_crc = 0;
	FuLenovoAccessoryDeviceMode mode;
	g_autoptr(GInputStream) stream = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 5, "prepare");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 95, "write");

	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	if (!fu_input_stream_size(stream, &fw_size, error))
		return FALSE;
	if (!fu_input_stream_compute_crc32(stream, FU_CRC_KIND_B32_STANDARD, &file_crc, error))
		return FALSE;

	/* only enter DFU mode if not already there */
	if (!fu_lenovo_accessory_impl_get_mode(FU_LENOVO_ACCESSORY_IMPL(device), &mode, error))
		return FALSE;
	if (mode != FU_LENOVO_ACCESSORY_DEVICE_MODE_DFU_MODE) {
		if (!fu_lenovo_accessory_impl_dfu_entry(FU_LENOVO_ACCESSORY_IMPL(device), error))
			return FALSE;
	}
	if (!fu_lenovo_accessory_impl_dfu_attribute(FU_LENOVO_ACCESSORY_IMPL(device),
						    NULL,
						    NULL,
						    NULL,
						    NULL,
						    NULL,
						    NULL,
						    error))
		return FALSE;
	if (!fu_lenovo_accessory_impl_dfu_prepare(FU_LENOVO_ACCESSORY_IMPL(device),
						  FU_LENOVO_ACCESSORY_DFU_FILE_TYPE_BIN_FILE,
						  0x0,
						  (guint32)fw_size,
						  file_crc,
						  error))
		return FALSE;
	fu_progress_step_done(progress);
	if (!fu_lenovo_accessory_hid_child_device_write_files(
		self,
		FU_LENOVO_ACCESSORY_DFU_FILE_TYPE_BIN_FILE,
		stream,
		fu_progress_get_child(progress),
		error))
		return FALSE;

	/* give the device time to finalize the flash before reading back CRC */
	fu_device_sleep(FU_DEVICE(self), 2000);
	if (!fu_lenovo_accessory_impl_dfu_crc(FU_LENOVO_ACCESSORY_IMPL(device),
					      &device_crc,
					      error)) {
		g_prefix_error_literal(error, "failed to read device CRC: ");
		return FALSE;
	}
	if (device_crc != file_crc) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "CRC mismatch: device 0x%08x != file 0x%08x",
			    device_crc,
			    file_crc);
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static guint8
fu_lenovo_accessory_hid_child_device_find_notify_ep(FuUsbDevice *device, GError **error)
{
	g_autoptr(GPtrArray) ifaces = NULL;

	/* find the notify interface by number: it is a standard HID collection
	 * that cannot be told apart from the mouse interface by descriptor
	 * (both advertise the same vendor usage-page), so match the number */
	ifaces = fu_usb_device_get_interfaces(device, error);
	if (ifaces == NULL)
		return 0x0;
	for (guint i = 0; i < ifaces->len; i++) {
		FuUsbInterface *iface = g_ptr_array_index(ifaces, i);
		g_autoptr(GPtrArray) endpoints = NULL;
		if (fu_usb_interface_get_number(iface) != FU_LENOVO_ACCESSORY_IFACE_NOTIFY)
			continue;
		endpoints = fu_usb_interface_get_endpoints(iface);
		for (guint j = 0; endpoints != NULL && j < endpoints->len; j++) {
			FuUsbEndpoint *ep = g_ptr_array_index(endpoints, j);
			if (fu_usb_endpoint_get_direction(ep) == FU_USB_DIRECTION_DEVICE_TO_HOST)
				return fu_usb_endpoint_get_address(ep);
		}
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "no interrupt-IN endpoint on the notify interface");
	return 0x0;
}

static gboolean
fu_lenovo_accessory_hid_child_device_notify_open(FuUsbDevice *device, GError **error)
{
	/* claim the notify interface, detaching the kernel HID driver that
	 * normally owns it; best-effort callers may ignore failure on a
	 * receiver that does not expose this interface */
	return fu_usb_device_claim_interface(device,
					     FU_LENOVO_ACCESSORY_IFACE_NOTIFY,
					     FU_USB_DEVICE_CLAIM_FLAG_KERNEL_DRIVER,
					     error);
}

static void
fu_lenovo_accessory_hid_child_device_notify_close(FuUsbDevice *device)
{
	g_autoptr(GError) error_local = NULL;
	if (!fu_usb_device_release_interface(device,
					     FU_LENOVO_ACCESSORY_IFACE_NOTIFY,
					     FU_USB_DEVICE_CLAIM_FLAG_KERNEL_DRIVER,
					     &error_local))
		g_debug("failed to release notify interface: %s", error_local->message);
}

typedef struct {
	guint8 ep;
} FuLenovoAccessoryNotifyHelper;

static gboolean
fu_lenovo_accessory_hid_child_device_notify_poll_cb(FuDevice *device,
						    gpointer user_data,
						    GError **error)
{
	FuLenovoAccessoryNotifyHelper *helper = (FuLenovoAccessoryNotifyHelper *)user_data;
	guint8 buf[FU_LENOVO_ACCESSORY_HID_BUFSZ] = {0x0};
	gsize actual_len = 0;
	g_autoptr(FuStructLenovoAccessoryNotify) st = NULL;

	/* a rebooted peripheral re-announces itself over the notify endpoint; a
	 * read timeout or an unrelated input report (e.g. a keypress, rejected by
	 * the struct validation) is reported as an error so the retry loop keeps
	 * polling until the connect event arrives or the window elapses */
	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(device),
					      helper->ep,
					      buf,
					      sizeof(buf),
					      &actual_len,
					      500, /* ms */
					      NULL,
					      error))
		return FALSE;
	st = fu_struct_lenovo_accessory_notify_parse(buf, actual_len, 0x0, error);
	if (st == NULL)
		return FALSE;
	if (fu_struct_lenovo_accessory_notify_get_event(st) !=
		FU_LENOVO_ACCESSORY_NOTIFY_EVENT_WIRELESS_CONNECT_STATUS_CHANGE ||
	    fu_struct_lenovo_accessory_notify_get_connect_status(st) !=
		FU_LENOVO_ACCESSORY_NOTIFY_CONNECT_STATUS_CONNECTED) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_BUSY,
				    "no online notification yet");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_lenovo_accessory_hid_child_device_notify_wait_online(FuUsbDevice *self, GError **error)
{
	FuLenovoAccessoryNotifyHelper helper = {0};

	helper.ep = fu_lenovo_accessory_hid_child_device_find_notify_ep(self, error);
	if (helper.ep == 0x0)
		return FALSE;

	/* poll the notify endpoint until the connect event arrives; using
	 * fu_device_retry means the wait aborts if the proxy is unplugged rather
	 * than blocking for the whole window */
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_lenovo_accessory_hid_child_device_notify_poll_cb,
				    FU_LENOVO_ACCESSORY_CHILD_REBOOT_TIMEOUT_MS / 500,
				    0, /* ms */
				    &helper,
				    error);
}

static gboolean
fu_lenovo_accessory_hid_child_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *proxy;
	gboolean notify_open = FALSE;
	g_autoptr(GError) error_notify = NULL;

	/* the reboot announcement arrives on the dongle's notify interface, so
	 * start listening before triggering the reboot; best-effort, as a
	 * receiver without the notify interface falls back to a fixed wait */
	proxy = fu_device_get_proxy(device, error);
	if (proxy == NULL)
		return FALSE;
	if (fu_lenovo_accessory_hid_child_device_notify_open(FU_USB_DEVICE(proxy), &error_notify)) {
		notify_open = TRUE;
	} else {
		g_debug("notify channel unavailable, using fixed wait: %s", error_notify->message);
	}

	if (!fu_lenovo_accessory_impl_dfu_exit(FU_LENOVO_ACCESSORY_IMPL(device),
					       FU_LENOVO_ACCESSORY_DFU_EXIT_CODE_DFU_SUCCESS,
					       error)) {
		g_prefix_error_literal(error, "failed to exit: ");
		if (notify_open)
			fu_lenovo_accessory_hid_child_device_notify_close(FU_USB_DEVICE(proxy));
		return FALSE;
	}

	if (notify_open) {
		/* the reboot must be observed within the window or it failed */
		gboolean online =
		    fu_lenovo_accessory_hid_child_device_notify_wait_online(FU_USB_DEVICE(proxy),
									    error);
		fu_lenovo_accessory_hid_child_device_notify_close(FU_USB_DEVICE(proxy));
		if (!online) {
			g_prefix_error_literal(error, "peripheral did not come back online: ");
			return FALSE;
		}
		/* let the peripheral finish initializing before talking to it */
		fu_device_sleep(device, FU_LENOVO_ACCESSORY_CHILD_REBOOT_SETTLE_MS);
	} else {
		fu_device_sleep(device, FU_LENOVO_ACCESSORY_CHILD_REBOOT_TIMEOUT_MS);
	}

	if (!fu_lenovo_accessory_hid_child_device_setup(device, error))
		return FALSE;
	return TRUE;
}

static void
fu_lenovo_accessory_hid_child_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_lenovo_accessory_hid_child_device_impl_iface_init(FuLenovoAccessoryImplInterface *iface)
{
	iface->read = fu_lenovo_accessory_hid_child_device_read;
	iface->write = fu_lenovo_accessory_hid_child_device_write;
	iface->process = fu_lenovo_accessory_hid_child_device_process;
}

static void
fu_lenovo_accessory_hid_child_device_class_init(FuLenovoAccessoryHidChildDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_lenovo_accessory_hid_child_device_setup;
	device_class->write_firmware = fu_lenovo_accessory_hid_child_device_write_firmware;
	device_class->set_progress = fu_lenovo_accessory_hid_child_device_set_progress;
	device_class->attach = fu_lenovo_accessory_hid_child_device_attach;
}

static void
fu_lenovo_accessory_hid_child_device_init(FuLenovoAccessoryHidChildDevice *self)
{
	fu_device_set_remove_delay(FU_DEVICE(self), 10000); /* ms */
	fu_device_add_protocol(FU_DEVICE(self), "com.lenovo.accessory");
	fu_device_set_install_duration(FU_DEVICE(self), 60);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
	fu_device_set_proxy_gtype(FU_DEVICE(self), FU_TYPE_HID_DEVICE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FOR_OPEN);
}

FuLenovoAccessoryHidChildDevice *
fu_lenovo_accessory_hid_child_device_new(FuDevice *proxy)
{
	return g_object_new(FU_TYPE_LENOVO_ACCESSORY_HID_CHILD_DEVICE, "proxy", proxy, NULL);
}
