/*
 * Copyright 1999-2023 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-logitech-rallysystem-struct.h"
#include "fu-logitech-rallysystem-tablehub-device.h"

enum { EP_OUT, EP_IN, EP_LAST };

#define FU_LOGITECH_RALLYSYSTEM_TABLEHUB_DEVICE_IOCTL_TIMEOUT	       3000  /* 3 sec */
#define FU_LOGITECH_RALLYSYSTEM_TABLEHUB_DEVICE_IOCTL_PROGRESS_TIMEOUT 90000 /* 90 sec */

struct _FuLogitechRallysystemTablehubDevice {
	FuUsbDevice parent_instance;
	guint bulk_ep[EP_LAST];
};

G_DEFINE_TYPE(FuLogitechRallysystemTablehubDevice,
	      fu_logitech_rallysystem_tablehub_device,
	      FU_TYPE_USB_DEVICE)

static void
fu_logitech_rallysystem_tablehub_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuLogitechRallysystemTablehubDevice *self = FU_LOGITECH_RALLYSYSTEM_TABLEHUB_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "EpBulkIn", self->bulk_ep[EP_IN]);
	fwupd_codec_string_append_hex(str, idt, "EpBulkOut", self->bulk_ep[EP_OUT]);
}

static gboolean
fu_logitech_rallysystem_tablehub_device_probe(FuDevice *device, GError **error)
{
	FuLogitechRallysystemTablehubDevice *self = FU_LOGITECH_RALLYSYSTEM_TABLEHUB_DEVICE(device);
	guint8 bulk_iface = G_MAXUINT8;
	g_autoptr(GPtrArray) intfs = NULL;

	intfs = fu_usb_device_get_interfaces(FU_USB_DEVICE(self), error);
	if (intfs == NULL)
		return FALSE;
	for (guint i = 0; i < intfs->len; i++) {
		FuUsbInterface *intf = g_ptr_array_index(intfs, i);
		if (fu_usb_interface_get_class(intf) == FU_USB_CLASS_VENDOR_SPECIFIC) {
			g_autoptr(GPtrArray) endpoints = fu_usb_interface_get_endpoints(intf);
			bulk_iface = fu_usb_interface_get_number(intf);
			if (endpoints == NULL)
				continue;
			for (guint j = 0; j < endpoints->len; j++) {
				FuUsbEndpoint *ep = g_ptr_array_index(endpoints, j);
				if (j == EP_OUT)
					self->bulk_ep[EP_OUT] = fu_usb_endpoint_get_address(ep);
				else
					self->bulk_ep[EP_IN] = fu_usb_endpoint_get_address(ep);
			}
		}
	}
	if (bulk_iface == G_MAXUINT8) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no bulk interface found");
		return FALSE;
	}
	fu_usb_device_add_interface(FU_USB_DEVICE(self), bulk_iface);
	return TRUE;
}

static gboolean
fu_logitech_rallysystem_tablehub_device_send(FuLogitechRallysystemTablehubDevice *self,
					     guint8 *buf,
					     gsize bufsz,
					     GError **error)
{
	gsize actual_length = 0;
	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 self->bulk_ep[EP_OUT],
					 buf,
					 bufsz,
					 &actual_length,
					 FU_LOGITECH_RALLYSYSTEM_TABLEHUB_DEVICE_IOCTL_TIMEOUT,
					 NULL,
					 error)) {
		g_prefix_error(error, "failed to send using bulk transfer: ");
		return FALSE;
	}
	if (bufsz != actual_length) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "failed to send full packet using bulk transfer");
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "RallysystemBulkTx", buf, bufsz);
	return TRUE;
}

static gboolean
fu_logitech_rallysystem_tablehub_device_recv(FuLogitechRallysystemTablehubDevice *self,
					     guint8 *buf,
					     gsize bufsz,
					     guint timeout,
					     GError **error)
{
	gsize actual_length = 0;
	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 self->bulk_ep[EP_IN],
					 buf,
					 bufsz,
					 &actual_length,
					 timeout,
					 NULL,
					 error)) {
		g_prefix_error(error, "failed to receive using bulk transfer: ");
		return FALSE;
	}
	if (bufsz != actual_length) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "failed to receive full packet using bulk transfer");
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "RallysystemBulkRx", buf, bufsz);
	return TRUE;
}

static gboolean
fu_logitech_rallysystem_tablehub_device_write_fw(FuLogitechRallysystemTablehubDevice *self,
						 GInputStream *stream,
						 FuProgress *progress,
						 GError **error)
{
	g_autoptr(FuChunkArray) chunks = fu_chunk_array_new_from_stream(stream,
									FU_CHUNK_ADDR_OFFSET_NONE,
									FU_CHUNK_PAGESZ_NONE,
									0x200,
									error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autofree guint8 *data_mut = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		data_mut = fu_memdup_safe(fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk), error);
		if (data_mut == NULL)
			return FALSE;
		if (!fu_logitech_rallysystem_tablehub_device_send(self,
								  data_mut,
								  fu_chunk_get_data_sz(chk),
								  error)) {
			g_prefix_error(error, "failed to send data packet 0x%x: ", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	return TRUE;
}

static gboolean
fu_logitech_rallysystem_tablehub_device_progress_cb(FuDevice *device,
						    gpointer user_data,
						    GError **error)
{
	FuLogitechRallysystemTablehubDevice *self = FU_LOGITECH_RALLYSYSTEM_TABLEHUB_DEVICE(device);
	guint8 buf[FU_STRUCT_USB_PROGRESS_RESPONSE_SIZE] = {0x0};
	g_autoptr(GByteArray) st_res = NULL;

	if (!fu_logitech_rallysystem_tablehub_device_recv(
		self,
		buf,
		sizeof(buf),
		FU_LOGITECH_RALLYSYSTEM_TABLEHUB_DEVICE_IOCTL_PROGRESS_TIMEOUT,
		error)) {
		g_prefix_error(error, "failed to get progress report: ");
		return FALSE;
	}
	st_res = fu_struct_usb_progress_response_parse(buf, sizeof(buf), 0x0, error);
	if (st_res == NULL)
		return FALSE;
	if (fu_struct_usb_progress_response_get_completed(st_res) != 100) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "percentage only %u%%",
			    fu_struct_usb_progress_response_get_completed(st_res));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_rallysystem_tablehub_device_write_firmware(FuDevice *device,
						       FuFirmware *firmware,
						       FuProgress *progress,
						       FwupdInstallFlags flags,
						       GError **error)
{
	FuLogitechRallysystemTablehubDevice *self = FU_LOGITECH_RALLYSYSTEM_TABLEHUB_DEVICE(device);
	gsize streamsz = 0;
	guint8 buf[FU_STRUCT_USB_FIRMWARE_DOWNLOAD_RESPONSE_SIZE] = {0x0};
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GByteArray) st_req = fu_struct_usb_firmware_download_request_new();
	g_autoptr(GByteArray) st_res = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "init");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 4, "device-write-blocks");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 35, "uninit");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 60, NULL);

	/* get default image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	fu_struct_usb_firmware_download_request_set_len(st_req, streamsz);
	if (!fu_struct_usb_firmware_download_request_set_fw_version(st_req,
								    fu_device_get_version(device),
								    error)) {
		g_prefix_error(error, "failed to copy download mode payload: ");
		return FALSE;
	}
	if (!fu_logitech_rallysystem_tablehub_device_send(self, st_req->data, st_req->len, error)) {
		g_prefix_error(error, "failed to set download mode: ");
		return FALSE;
	}
	if (!fu_logitech_rallysystem_tablehub_device_recv(
		self,
		buf,
		sizeof(buf),
		FU_LOGITECH_RALLYSYSTEM_TABLEHUB_DEVICE_IOCTL_TIMEOUT,
		error)) {
		g_prefix_error(
		    error,
		    "failed to receive set download mode response: please reboot the device: ");
		return FALSE;
	}
	st_res = fu_struct_usb_firmware_download_response_parse(buf, sizeof(buf), 0x0, error);
	if (st_res == NULL)
		return FALSE;
	fu_progress_step_done(progress);

	/* push each block to device */
	if (!fu_logitech_rallysystem_tablehub_device_write_fw(self,
							      stream,
							      fu_progress_get_child(progress),
							      error))
		return FALSE;
	fu_progress_step_done(progress);

	/* image file pushed. Device validates and uploads new image on inactive partition.
	 * After upload is finished, device reboots itself */
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_logitech_rallysystem_tablehub_device_progress_cb,
				  210,
				  1000,
				  NULL,
				  error)) {
		g_prefix_error(error, "failed to wait for 100pc: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* return no error since table hub may not come back right after reboot, it goes straight
	 * to update camera/tv if needed and will be disappear until it finished the tasks */
	fu_device_sleep_full(FU_DEVICE(self), 7 * 60 * 1000, fu_progress_get_child(progress));
	fu_progress_step_done(progress);

	/* success! */
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_logitech_rallysystem_tablehub_device_send_init_cmd_cb(FuDevice *device,
							 gpointer user_data,
							 GError **error)
{
	FuLogitechRallysystemTablehubDevice *self = FU_LOGITECH_RALLYSYSTEM_TABLEHUB_DEVICE(device);
	guint8 buf[FU_STRUCT_USB_INIT_RESPONSE_SIZE] = {0x0};
	g_autoptr(GByteArray) st_req = fu_struct_usb_init_request_new();
	g_autoptr(GByteArray) st_res = NULL;

	if (!fu_logitech_rallysystem_tablehub_device_send(self, st_req->data, st_req->len, error)) {
		g_prefix_error(error, "failed to send init packet: ");
		return FALSE;
	}
	if (!fu_logitech_rallysystem_tablehub_device_recv(
		self,
		buf,
		sizeof(buf),
		FU_LOGITECH_RALLYSYSTEM_TABLEHUB_DEVICE_IOCTL_TIMEOUT,
		error)) {
		g_prefix_error(error, "failed to receive init packet: ");
		return FALSE;
	}
	st_res = fu_struct_usb_init_response_parse(buf, sizeof(buf), 0x0, error);
	if (st_res == NULL) {
		g_prefix_error(error, "failed to get correct init packet: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_rallysystem_tablehub_device_setup(FuDevice *device, GError **error)
{
	FuLogitechRallysystemTablehubDevice *self = FU_LOGITECH_RALLYSYSTEM_TABLEHUB_DEVICE(device);
	guint8 buf[FU_STRUCT_USB_READ_VERSION_RESPONSE_SIZE] = {0x0};
	g_autofree gchar *fw_version = NULL;
	g_autoptr(GByteArray) st_req = fu_struct_usb_read_version_request_new();
	g_autoptr(GByteArray) st_res = NULL;

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_logitech_rallysystem_tablehub_device_parent_class)
		 ->setup(device, error))
		return FALSE;

	/* sending INIT. Retry if device is not in IDLE state to receive the data */
	if (!fu_device_retry(device,
			     fu_logitech_rallysystem_tablehub_device_send_init_cmd_cb,
			     5,
			     NULL,
			     error)) {
		g_prefix_error(error, "failed to write init packet: please reboot the device: ");
		return FALSE;
	}

	/* query tablehub firmware version */
	if (!fu_logitech_rallysystem_tablehub_device_send(self, st_req->data, st_req->len, error)) {
		g_prefix_error(error,
			       "failed to send tablehub firmware version request: "
			       "please reboot the device: ");
		return FALSE;
	}
	if (!fu_logitech_rallysystem_tablehub_device_recv(
		self,
		buf,
		sizeof(buf),
		FU_LOGITECH_RALLYSYSTEM_TABLEHUB_DEVICE_IOCTL_TIMEOUT,
		error)) {
		g_prefix_error(error,
			       "failed to get response for tablehub firmware "
			       "version request: please reboot the device: ");
		return FALSE;
	}
	st_res = fu_struct_usb_read_version_response_parse(buf, sizeof(buf), 0x0, error);
	if (st_res == NULL)
		return FALSE;
	fw_version = fu_struct_usb_read_version_response_get_fw_version(st_res);
	fu_device_set_version(FU_DEVICE(self), fw_version);

	/* success! */
	return TRUE;
}

static void
fu_logitech_rallysystem_tablehub_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 55, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 45, "reload");
}

static void
fu_logitech_rallysystem_tablehub_device_init(FuLogitechRallysystemTablehubDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.logitech.vc.rallysystem");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_set_install_duration(FU_DEVICE(self), 5 * 60);
	fu_device_set_remove_delay(FU_DEVICE(self), 60 * 1000); /* wait for subcomponent */
}

static void
fu_logitech_rallysystem_tablehub_device_class_init(FuLogitechRallysystemTablehubDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_logitech_rallysystem_tablehub_device_to_string;
	device_class->write_firmware = fu_logitech_rallysystem_tablehub_device_write_firmware;
	device_class->probe = fu_logitech_rallysystem_tablehub_device_probe;
	device_class->setup = fu_logitech_rallysystem_tablehub_device_setup;
	device_class->set_progress = fu_logitech_rallysystem_tablehub_device_set_progress;
}
