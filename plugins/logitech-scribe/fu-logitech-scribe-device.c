/*
 * Copyright (c) 1999-2022 Logitech, Inc.
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <linux/types.h>
#include <linux/usb/video.h>
#include <linux/uvcvideo.h>
#ifdef HAVE_IOCTL_H
#include <sys/ioctl.h>
#endif

#include <string.h>

#include "fu-logitech-scribe-device.h"

/* UPD interface follows TLV (Type, Length, Value) protocol */
/* Payload size limited to 8k for UPD interfaces            */
#define UPD_PACKET_HEADER_SIZE		     (2 * sizeof(guint32))
#define HASH_TIMEOUT			     1500
#define MAX_DATA_SIZE			     8192 /* 8k */
#define PAYLOAD_SIZE			     MAX_DATA_SIZE - UPD_PACKET_HEADER_SIZE
#define UPD_INTERFACE_SUBPROTOCOL_ID	     101
#define BULK_TRANSFER_TIMEOUT		     1000
#define HASH_VALUE_SIZE			     16
#define LENGTH_OFFSET			     0x4
#define COMMAND_OFFSET			     0x0
#define MAX_RETRIES			     5
#define MAX_HANDSHAKE_RETRIES		     3
#define MAX_WAIT_COUNT			     150
#define SESSION_TIMEOUT			     1000
#define FU_LOGITECH_SCRIBE_CHECKSUM_KIND_MD5 2
#define FU_LOGITECH_SCRIBE_VERSION_SIZE	     1024 /* max size of version data returned */
#define FU_LOGITECH_SCRIBE_PROTOCOL_ID	     0x1

enum { EP_OUT, EP_IN, EP_LAST };

enum { BULK_INTERFACE_UPD };

typedef enum {
	CMD_CHECK_BUFFERSIZE = 0xCC00,
	CMD_INIT = 0xCC01,
	CMD_START_TRANSFER = 0xCC02,
	CMD_DATA_TRANSFER = 0xCC03,
	CMD_END_TRANSFER = 0xCC04,
	CMD_UNINIT = 0xCC05,
	CMD_BUFFER_READ = 0xCC06,
	CMD_BUFFER_WRITE = 0xCC07,
	CMD_UNINIT_BUFFER = 0xCC08,
	CMD_ACK = 0xFF01,
	CMD_TIMEOUT = 0xFF02,
	CMD_NACK = 0xFF03
} UsbCommands;

#define FU_LOGITECH_SCRIBE_DEVICE_IOCTL_TIMEOUT 5000 /* ms */
/* 2 byte for get len query */
#define kDefaultUvcGetLenQueryControlSize 2

const guchar kLogiCameraVersionSelector = 1;
const guchar kLogiUvcXuDevInfoCsEepromVersion = 3;
const guint kLogiVideoImageVersionMaxSize = 32;
const guchar kLogiVideoAitInitiateSetMMPData = 1;
const guchar kLogiVideoAitFinalizeSetMMPData = 1;
const guchar kLogiUnitIdAccessMmp = 6;
const guchar kLogiUvcXuAitCustomCsSetMmp = 4;
const guchar kLogiUvcXuAitCustomCsGetMmpResult = 5;
const guchar kLogiUnitIdPeripheralControl = 11;

const guchar kLogiUnitIdCameraVersion = 8;
const guchar kLogiAitSetMmpCmdFwBurning = 1;

struct _FuLogitechScribeDevice {
	FuUdevDevice parent_instance;
	guint update_ep[EP_LAST];
	guint update_iface;
};

G_DEFINE_TYPE(FuLogitechScribeDevice, fu_logitech_scribe_device, FU_TYPE_UDEV_DEVICE)

static gboolean
fu_logitech_scribe_device_send(FuLogitechScribeDevice *self,
			       FuUsbDevice *usb_device,
			       GByteArray *buf,
			       gint interface_id,
			       GError **error)
{
	gsize transferred = 0;
	gint ep;
	GCancellable *cancellable = NULL;
	g_return_val_if_fail(buf != NULL, FALSE);

	if (interface_id == BULK_INTERFACE_UPD) {
		ep = self->update_ep[EP_OUT];
	} else {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "interface is invalid");
		return FALSE;
	}
	if (!g_usb_device_bulk_transfer(fu_usb_device_get_dev(FU_USB_DEVICE(usb_device)),
					ep,
					(guint8 *)buf->data,
					buf->len,
					&transferred,
					BULK_TRANSFER_TIMEOUT,
					cancellable,
					error)) {
		g_prefix_error(error, "failed to send using bulk transfer: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logitech_scribe_device_recv(FuLogitechScribeDevice *self,
			       FuUsbDevice *usb_device,
			       GByteArray *buf,
			       gint interface_id,
			       guint timeout,
			       GError **error)
{
	gsize received_length = 0;
	gint ep;
	g_return_val_if_fail(buf != NULL, FALSE);

	if (interface_id == BULK_INTERFACE_UPD) {
		ep = self->update_ep[EP_IN];
	} else {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "interface is invalid");
		return FALSE;
	}
	if (!g_usb_device_bulk_transfer(fu_usb_device_get_dev(FU_USB_DEVICE(usb_device)),
					ep,
					buf->data,
					buf->len,
					&received_length,
					timeout,
					NULL,
					error)) {
		g_prefix_error(error, "failed to receive using bulk transfer: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logitech_scribe_device_send_upd_cmd(FuLogitechScribeDevice *self,
				       FuUsbDevice *usb_device,
				       guint32 cmd,
				       GByteArray *buf,
				       GError **error)
{
	guint32 cmd_tmp = 0x0;
	guint timeout = BULK_TRANSFER_TIMEOUT;
	g_autoptr(GByteArray) buf_pkt = g_byte_array_new();
	g_autoptr(GByteArray) buf_ack = g_byte_array_new();

	fu_byte_array_append_uint32(buf_pkt, cmd, G_LITTLE_ENDIAN); /* Type(T) : Command type */
	fu_byte_array_append_uint32(buf_pkt,
				    buf != NULL ? buf->len : 0,
				    G_LITTLE_ENDIAN); /*Length(L) : Length of payload */
	if (buf != NULL) {
		g_byte_array_append(buf_pkt,
				    buf->data,
				    buf->len); /* Value(V) : Actual payload data */
	}
	if (!fu_logitech_scribe_device_send(self, usb_device, buf_pkt, BULK_INTERFACE_UPD, error))
		return FALSE;

	/* receiving INIT ACK */
	fu_byte_array_set_size(buf_ack, MAX_DATA_SIZE, 0x00);

	/* extending the bulk transfer timeout value, as android device takes some time to
	   calculate Hash and respond */
	if (CMD_END_TRANSFER == cmd)
		timeout = HASH_TIMEOUT;

	if (!fu_logitech_scribe_device_recv(self,
					    usb_device,
					    buf_ack,
					    BULK_INTERFACE_UPD,
					    timeout,
					    error))
		return FALSE;

	if (!fu_memread_uint32_safe(buf_ack->data,
				    buf_ack->len,
				    COMMAND_OFFSET,
				    &cmd_tmp,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (cmd_tmp != CMD_ACK) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "not CMD_ACK, got %x", cmd);
		return FALSE;
	}
	if (!fu_memread_uint32_safe(buf_ack->data,
				    buf_ack->len,
				    UPD_PACKET_HEADER_SIZE,
				    &cmd_tmp,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (cmd_tmp != cmd) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "invalid upd message received, expected %x, got %x",
			    cmd,
			    cmd_tmp);
		return FALSE;
	}
	return TRUE;
}

static gchar *
fu_logitech_scribe_device_compute_hash(GBytes *data)
{
	guint8 md5buf[HASH_VALUE_SIZE] = {0};
	gsize data_len = sizeof(md5buf);
	GChecksum *checksum = g_checksum_new(G_CHECKSUM_MD5);
	g_checksum_update(checksum, g_bytes_get_data(data, NULL), g_bytes_get_size(data));
	g_checksum_get_digest(checksum, (guint8 *)&md5buf, &data_len);
	return g_base64_encode(md5buf, sizeof(md5buf));
}

static gboolean
fu_logitech_scribe_device_query_data_size(FuLogitechScribeDevice *self,
					  guchar unit_id,
					  guchar control_selector,
					  guint16 *data_size,
					  GError **error)
{
	guint8 size_data[kDefaultUvcGetLenQueryControlSize] = {0x0};
	struct uvc_xu_control_query size_query;
	size_query.unit = unit_id;
	size_query.selector = control_selector;
	size_query.query = UVC_GET_LEN;
	size_query.size = kDefaultUvcGetLenQueryControlSize;
	size_query.data = size_data;

	g_debug("data size query request, unit: 0x%x selector: 0x%x",
		(guchar)unit_id,
		(guchar)control_selector);

	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  UVCIOC_CTRL_QUERY,
				  (guint8 *)&size_query,
				  NULL,
				  FU_LOGITECH_SCRIBE_DEVICE_IOCTL_TIMEOUT,
				  error))
		return FALSE;
	/* convert the data byte to int */
	*data_size = size_data[1] << 8 | size_data[0];
	g_debug("data size query response, size: %u unit: 0x%x selector: 0x%x",
		*data_size,
		(guchar)unit_id,
		(guchar)control_selector);
	fu_dump_raw(G_LOG_DOMAIN, "UVC_GET_LEN", size_data, kDefaultUvcGetLenQueryControlSize);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_scribe_device_get_xu_control(FuLogitechScribeDevice *self,
					 guchar unit_id,
					 guchar control_selector,
					 guint16 data_size,
					 guchar *data,
					 GError **error)
{
	struct uvc_xu_control_query control_query;

	g_debug("get xu control request, size: %" G_GUINT16_FORMAT " unit: 0x%x selector: 0x%x",
		data_size,
		(guchar)unit_id,
		(guchar)control_selector);
	control_query.unit = unit_id;
	control_query.selector = control_selector;
	control_query.query = UVC_GET_CUR;
	control_query.size = data_size;
	control_query.data = data;
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  UVCIOC_CTRL_QUERY,
				  (guint8 *)&control_query,
				  NULL,
				  FU_LOGITECH_SCRIBE_DEVICE_IOCTL_TIMEOUT,
				  error))
		return FALSE;
	g_debug("received get xu control response, size: %u unit: 0x%x selector: 0x%x",
		data_size,
		(guchar)unit_id,
		(guchar)control_selector);
	fu_dump_raw(G_LOG_DOMAIN, "UVC_GET_CUR", data, data_size);

	/* success */
	return TRUE;
}

static void
fu_logitech_scribe_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuLogitechScribeDevice *self = FU_LOGITECH_SCRIBE_DEVICE(device);
	fu_string_append_kx(str, idt, "UpdateIface", self->update_iface);
	fu_string_append_kx(str, idt, "UpdateEpOut", self->update_ep[EP_OUT]);
	fu_string_append_kx(str, idt, "UpdateEpIn", self->update_ep[EP_IN]);
}

static gboolean
fu_logitech_scribe_device_probe(FuDevice *device, GError **error)
{
	const gchar *id_v4l_capabilities;
	const gchar *index;
	GUdevDevice *udev_device = fu_udev_device_get_dev(FU_UDEV_DEVICE(device));

	/* check is valid */
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "video4linux") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "is not correct subsystem=%s, expected video4linux",
			    fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)));
		return FALSE;
	}

	/* only interested in video capture device */
	id_v4l_capabilities = g_udev_device_get_property(udev_device, "ID_V4L_CAPABILITIES");
	if (g_strcmp0(id_v4l_capabilities, ":capture:") != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "only video capture device are supported");
		return FALSE;
	}

	/* interested in lowest index only e,g, video0, ignore low format siblings like
	 * video1/video2/video3 etc */
	index = g_udev_device_get_sysfs_attr(udev_device, "index");
	if (g_strcmp0(index, "0") != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "only device with lower index supported");
		return FALSE;
	};

	/* set the physical ID */
	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "video4linux", error);
}

static gboolean
fu_logitech_scribe_device_send_upd_init_cmd_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuLogitechScribeDevice *self = FU_LOGITECH_SCRIBE_DEVICE(device);
	return fu_logitech_scribe_device_send_upd_cmd(self, user_data, CMD_INIT, NULL, error);
}

static gboolean
fu_logitech_scribe_device_write_fw(FuLogitechScribeDevice *self,
				   FuUsbDevice *usb_device,
				   GBytes *fw,
				   FuProgress *progress,
				   GError **error)
{
	g_autoptr(GPtrArray) chunks = NULL;

	chunks = fu_chunk_array_new_from_bytes(fw, 0x0, 0x0, PAYLOAD_SIZE);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		g_autoptr(GByteArray) data_pkt = g_byte_array_new();
		g_byte_array_append(data_pkt, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
		if (!fu_logitech_scribe_device_send_upd_cmd(self,
							    usb_device,
							    CMD_DATA_TRANSFER,
							    data_pkt,
							    error)) {
			g_prefix_error(error, "failed to send data packet 0x%x: ", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}
	return TRUE;
}

static gboolean
fu_logitech_scribe_device_write_firmware(FuDevice *device,
					 FuFirmware *firmware,
					 FuProgress *progress,
					 FwupdInstallFlags flags,
					 GError **error)
{
	FuLogitechScribeDevice *self = FU_LOGITECH_SCRIBE_DEVICE(device);
	g_autofree gchar *base64hash = NULL;
	g_autoptr(GByteArray) end_pkt = g_byte_array_new();
	g_autoptr(GByteArray) start_pkt = g_byte_array_new();
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GUsbInterface) intf = NULL;
	g_autoptr(GPtrArray) endpoints = NULL;
	g_autoptr(GUsbDevice) g_usb_device = NULL;
	g_autoptr(FuUsbDevice) usb_device = NULL;

	/* convert GUdevDevice to GUsbDevice */
	g_usb_device = fu_udev_device_find_usb_device(FU_UDEV_DEVICE(device), error);
	if (g_usb_device == NULL) {
		return FALSE;
	}

	usb_device = fu_usb_device_new(fu_device_get_context(device), g_usb_device);
	if (usb_device == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "failed to create usb device instance");
		return FALSE;
	}

	/* re-open with new device set */
	fu_usb_device_set_dev(usb_device, g_usb_device);
	if (!fu_device_open(FU_DEVICE(usb_device), error))
		return FALSE;

	/* find the correct interface */
	intf = g_usb_device_get_interface(fu_usb_device_get_dev(FU_USB_DEVICE(usb_device)),
					  G_USB_DEVICE_CLASS_VENDOR_SPECIFIC,
					  UPD_INTERFACE_SUBPROTOCOL_ID,
					  FU_LOGITECH_SCRIBE_PROTOCOL_ID,
					  error);
	if (intf == NULL)
		return FALSE;

	endpoints = g_usb_interface_get_endpoints(intf);
	if (endpoints == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "failed to get usb device endpoints");
		return FALSE;
	}

	self->update_iface = g_usb_interface_get_number(intf);
	for (guint j = 0; j < endpoints->len; j++) {
		GUsbEndpoint *ep = g_ptr_array_index(endpoints, j);
		if (j == EP_OUT)
			self->update_ep[EP_OUT] = g_usb_endpoint_get_address(ep);
		else
			self->update_ep[EP_IN] = g_usb_endpoint_get_address(ep);
	}
	fu_usb_device_add_interface(usb_device, self->update_iface);
	g_debug("usb data, iface: %u ep_out: %u ep_in: %u",
		self->update_iface,
		self->update_ep[EP_OUT],
		self->update_ep[EP_IN]);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "init");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "start-transfer");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "device-write-blocks");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "end-transfer");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "uninit");

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* sending INIT. Retry if device is not in IDLE state to receive the file */
	if (!fu_device_retry(device,
			     fu_logitech_scribe_device_send_upd_init_cmd_cb,
			     MAX_RETRIES,
			     usb_device,
			     error)) {
		g_prefix_error(error,
			       "failed to write init transfer packet: please reboot the device: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* start transfer */
	fu_byte_array_append_uint64(start_pkt, g_bytes_get_size(fw), G_LITTLE_ENDIAN);
	if (!fu_logitech_scribe_device_send_upd_cmd(self,
						    usb_device,
						    CMD_START_TRANSFER,
						    start_pkt,
						    error)) {
		g_prefix_error(error, "failed to write start transfer packet: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* push each block to device */
	if (!fu_logitech_scribe_device_write_fw(self,
						usb_device,
						fw,
						fu_progress_get_child(progress),
						error))
		return FALSE;
	fu_progress_step_done(progress);

	/* end transfer */
	base64hash = fu_logitech_scribe_device_compute_hash(fw);
	fu_byte_array_append_uint32(end_pkt, 1, G_LITTLE_ENDIAN); /* update */
	fu_byte_array_append_uint32(end_pkt, 0, G_LITTLE_ENDIAN); /* force */
	fu_byte_array_append_uint32(end_pkt,
				    FU_LOGITECH_SCRIBE_CHECKSUM_KIND_MD5,
				    G_LITTLE_ENDIAN); /* checksum type */
	g_byte_array_append(end_pkt, (const guint8 *)base64hash, strlen(base64hash));
	if (!fu_logitech_scribe_device_send_upd_cmd(self,
						    usb_device,
						    CMD_END_TRANSFER,
						    end_pkt,
						    error)) {
		g_prefix_error(error, "failed to write end transfer packet: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* uninitialize */
	/* no need to wait for ACK message, perhaps device reboot already in progress, ignore */
	if (!fu_logitech_scribe_device_send_upd_cmd(self,
						    usb_device,
						    CMD_UNINIT,
						    NULL,
						    &error_local)) {
		g_debug(
		    "failed to receive acknowledgment for uninitialize request, ignoring it: %s",
		    error_local->message);
	}
	fu_progress_step_done(progress);

	/*
	 * image file pushed. Device validates and uploads new image on inactive partition. Reboots
	 * wait for RemoveDelay duration
	 */

	/* success! */
	return TRUE;
}

static gboolean
fu_logitech_scribe_device_set_version(FuDevice *device, GError **error)
{
	FuLogitechScribeDevice *self = FU_LOGITECH_SCRIBE_DEVICE(device);
	guint32 fwversion = 0;
	guint16 data_len = 0;
	g_autofree guint8 *query_data = NULL;

	/* query current device version */
	if (!fu_logitech_scribe_device_query_data_size(self,
						       kLogiUnitIdCameraVersion,
						       kLogiCameraVersionSelector,
						       &data_len,
						       error))
		return FALSE;
	if (data_len > FU_LOGITECH_SCRIBE_VERSION_SIZE) {
		g_prefix_error(error, "version packet was too large at 0x%x bytes: ", data_len);
		return FALSE;
	}
	query_data = g_malloc0(data_len);
	if (!fu_logitech_scribe_device_get_xu_control(self,
						      kLogiUnitIdCameraVersion,
						      kLogiCameraVersionSelector,
						      data_len,
						      (guchar *)query_data,
						      error))
		return FALSE;

	/*  little-endian data. MinorVersion byte 0, MajorVersion byte 1, BuildVersion byte 3 & 2 */
	fwversion =
	    (query_data[1] << 24) + (query_data[0] << 16) + (query_data[3] << 8) + query_data[2];
	fu_device_set_version_from_uint32(device, fwversion);

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_scribe_device_setup(FuDevice *device, GError **error)
{
	return fu_logitech_scribe_device_set_version(device, error);
}

static void
fu_logitech_scribe_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_logitech_scribe_device_init(FuLogitechScribeDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.logitech.vc.scribe");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_AA_BB_CCDD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_retry_set_delay(FU_DEVICE(self), 1000);
}

static void
fu_logitech_scribe_device_finalize(GObject *object)
{
	G_OBJECT_CLASS(fu_logitech_scribe_device_parent_class)->finalize(object);
}

static void
fu_logitech_scribe_device_class_init(FuLogitechScribeDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_logitech_scribe_device_finalize;
	klass_device->to_string = fu_logitech_scribe_device_to_string;
	klass_device->write_firmware = fu_logitech_scribe_device_write_firmware;
	klass_device->probe = fu_logitech_scribe_device_probe;
	klass_device->setup = fu_logitech_scribe_device_setup;
	klass_device->set_progress = fu_logitech_scribe_device_set_progress;
}
