/*
 * Copyright (C) 2021 Xiaotian Cui <xtcui@analogixsemi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#include "config.h"

#include "fu-analogix-common.h"
#include "fu-analogix-device.h"
#include "fu-analogix-firmware.h"
#include "fu-analogix-struct.h"

struct _FuAnalogixDevice {
	FuUsbDevice parent_instance;
	guint16 ocm_version;
	guint16 custom_version;
};

G_DEFINE_TYPE(FuAnalogixDevice, fu_analogix_device, FU_TYPE_USB_DEVICE)

static void
fu_analogix_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuAnalogixDevice *self = FU_ANALOGIX_DEVICE(device);
	fu_string_append_kx(str, idt, "OcmVersion", self->ocm_version);
	fu_string_append_kx(str, idt, "CustomVersion", self->custom_version);
}

static gboolean
fu_analogix_device_send(FuAnalogixDevice *self,
			AnxBbRqtCode reqcode,
			guint16 val0code,
			guint16 index,
			const guint8 *buf,
			gsize bufsz,
			GError **error)
{
	gsize actual_len = 0;
	g_autofree guint8 *buf_tmp = NULL;

	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(bufsz <= 64, FALSE);

	/* make mutable */
	buf_tmp = fu_memdup_safe(buf, bufsz, error);
	if (buf_tmp == NULL)
		return FALSE;

	/* send data to device */
	if (!g_usb_device_control_transfer(fu_usb_device_get_dev(FU_USB_DEVICE(self)),
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   reqcode,	/* request */
					   val0code,	/* value */
					   index,	/* index */
					   buf_tmp,	/* data */
					   bufsz,	/* length */
					   &actual_len, /* actual length */
					   (guint)ANX_BB_TRANSACTION_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "send data error: ");
		return FALSE;
	}
	if (actual_len != bufsz) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "send data length is incorrect");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_analogix_device_receive(FuAnalogixDevice *self,
			   AnxBbRqtCode reqcode,
			   guint16 val0code,
			   guint16 index,
			   guint8 *buf,
			   gsize bufsz,
			   GError **error)
{
	gsize actual_len = 0;

	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(bufsz <= 64, FALSE);

	/* get data from device */
	if (!g_usb_device_control_transfer(fu_usb_device_get_dev(FU_USB_DEVICE(self)),
					   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   reqcode,  /* request */
					   val0code, /* value */
					   index,
					   buf,		/* data */
					   bufsz,	/* length */
					   &actual_len, /* actual length */
					   (guint)ANX_BB_TRANSACTION_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "receive data error: ");
		return FALSE;
	}
	if (actual_len != bufsz) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "receive data length is incorrect");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_analogix_device_get_update_status(FuAnalogixDevice *self,
				     FuAnalogixUpdateStatus *status,
				     GError **error)
{
	for (guint i = 0; i < 3000; i++) {
		guint8 status_tmp = FU_ANALOGIX_UPDATE_STATUS_INVALID;
		if (!fu_analogix_device_receive(self,
						ANX_BB_RQT_GET_UPDATE_STATUS,
						0,
						0,
						&status_tmp,
						sizeof(status_tmp),
						error))
			return FALSE;
		g_debug("status now: %s [0x%x]",
			fu_analogix_update_status_to_string(status_tmp),
			status_tmp);
		if ((status_tmp != FU_ANALOGIX_UPDATE_STATUS_ERROR) &&
		    (status_tmp != FU_ANALOGIX_UPDATE_STATUS_INVALID)) {
			if (status != NULL)
				*status = status_tmp;
			return TRUE;
		}
		fu_device_sleep(FU_DEVICE(self), 1); /* ms */
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "timed out: status was invalid");
	return FALSE;
}

static gboolean
fu_analogix_device_setup(FuDevice *device, GError **error)
{
	FuAnalogixDevice *self = FU_ANALOGIX_DEVICE(device);
	guint8 buf_fw[2] = {0x0};
	guint8 buf_custom[2] = {0x0};
	g_autofree gchar *version = NULL;

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_analogix_device_parent_class)->setup(device, error))
		return FALSE;

	/* get OCM version */
	if (!fu_analogix_device_receive(self, ANX_BB_RQT_READ_FW_VER, 0, 0, &buf_fw[1], 1, error))
		return FALSE;
	if (!fu_analogix_device_receive(self, ANX_BB_RQT_READ_FW_RVER, 0, 0, &buf_fw[0], 1, error))
		return FALSE;
	self->ocm_version = fu_memread_uint16(buf_fw, G_LITTLE_ENDIAN);

	/*  get custom version */
	if (!fu_analogix_device_receive(self,
					ANX_BB_RQT_READ_CUS_VER,
					0,
					0,
					&buf_custom[1],
					1,
					error))
		return FALSE;
	if (!fu_analogix_device_receive(self,
					ANX_BB_RQT_READ_CUS_RVER,
					0,
					0,
					&buf_custom[0],
					1,
					error))
		return FALSE;
	self->custom_version = fu_memread_uint16(buf_custom, G_LITTLE_ENDIAN);

	/* device version is both versions as a pair */
	version = g_strdup_printf("%04x.%04x", self->custom_version, self->ocm_version);
	fu_device_set_version(FU_DEVICE(device), version);
	return TRUE;
}

static gboolean
fu_analogix_device_find_interface(FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(device);
	FuAnalogixDevice *self = FU_ANALOGIX_DEVICE(device);
	g_autoptr(GPtrArray) intfs = NULL;

	intfs = g_usb_device_get_interfaces(usb_device, error);
	if (intfs == NULL)
		return FALSE;
	for (guint i = 0; i < intfs->len; i++) {
		GUsbInterface *intf = g_ptr_array_index(intfs, i);
		if (g_usb_interface_get_class(intf) == BILLBOARD_CLASS &&
		    g_usb_interface_get_subclass(intf) == BILLBOARD_SUBCLASS &&
		    g_usb_interface_get_protocol(intf) == BILLBOARD_PROTOCOL) {
			fu_usb_device_add_interface(FU_USB_DEVICE(self),
						    g_usb_interface_get_number(intf));
			return TRUE;
		}
	}
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no update interface found");
	return FALSE;
}

static gboolean
fu_analogix_device_probe(FuDevice *device, GError **error)
{
	if (!fu_analogix_device_find_interface(FU_USB_DEVICE(device), error)) {
		g_prefix_error(error, "failed to find update interface: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_analogix_device_write_chunks(FuAnalogixDevice *self,
				GPtrArray *chunks,
				guint16 req_val,
				FuProgress *progress,
				GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuAnalogixUpdateStatus status = FU_ANALOGIX_UPDATE_STATUS_INVALID;
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_analogix_device_send(self,
					     ANX_BB_RQT_SEND_UPDATE_DATA,
					     req_val,
					     i + 1,
					     fu_chunk_get_data(chk),
					     fu_chunk_get_data_sz(chk),
					     error)) {
			g_prefix_error(error, "failed send on chk %u: ", i);
			return FALSE;
		}
		if (!fu_analogix_device_get_update_status(self, &status, error)) {
			g_prefix_error(error, "failed status on chk %u: ", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_analogix_device_write_image(FuAnalogixDevice *self,
			       FuFirmware *image,
			       guint16 req_val,
			       FuProgress *progress,
			       GError **error)
{
	FuAnalogixUpdateStatus status = FU_ANALOGIX_UPDATE_STATUS_INVALID;
	guint8 buf_init[4] = {0x0};
	g_autoptr(GBytes) block_bytes = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "initialization");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, NULL);

	/* offset into firmware */
	block_bytes = fu_firmware_get_bytes(image, error);
	if (block_bytes == NULL)
		return FALSE;

	/* initialization */
	fu_memwrite_uint32(buf_init, g_bytes_get_size(block_bytes), G_LITTLE_ENDIAN);
	if (!fu_analogix_device_send(self,
				     ANX_BB_RQT_SEND_UPDATE_DATA,
				     req_val,
				     0,
				     buf_init,
				     3,
				     error)) {
		g_prefix_error(error, "program initialized failed: ");
		return FALSE;
	}
	if (!fu_analogix_device_get_update_status(self, &status, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write data */
	chunks = fu_chunk_array_new_from_bytes(block_bytes, 0x00, 0x00, BILLBOARD_MAX_PACKET_SIZE);
	if (!fu_analogix_device_write_chunks(self,
					     chunks,
					     req_val,
					     fu_progress_get_child(progress),
					     error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_analogix_device_write_firmware(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuAnalogixDevice *self = FU_ANALOGIX_DEVICE(device);
	gsize totalsz = 0;
	g_autoptr(FuFirmware) fw_cus = NULL;
	g_autoptr(FuFirmware) fw_ocm = NULL;
	g_autoptr(FuFirmware) fw_srx = NULL;
	g_autoptr(FuFirmware) fw_stx = NULL;

	/* these are all optional */
	fw_cus = fu_firmware_get_image_by_id(firmware, "custom", NULL);
	fw_stx = fu_firmware_get_image_by_id(firmware, "stx", NULL);
	fw_srx = fu_firmware_get_image_by_id(firmware, "srx", NULL);
	fw_ocm = fu_firmware_get_image_by_id(firmware, "ocm", NULL);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	if (fw_cus != NULL)
		totalsz += fu_firmware_get_size(fw_cus);
	if (fw_stx != NULL)
		totalsz += fu_firmware_get_size(fw_stx);
	if (fw_srx != NULL)
		totalsz += fu_firmware_get_size(fw_srx);
	if (fw_ocm != NULL)
		totalsz += fu_firmware_get_size(fw_ocm);
	if (totalsz == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "no firmware sections to update");
		return FALSE;
	}
	if (fw_cus != NULL) {
		fu_progress_add_step(progress,
				     FWUPD_STATUS_DEVICE_WRITE,
				     (100 * fu_firmware_get_size(fw_cus) / totalsz),
				     "cus");
	}
	if (fw_stx != NULL) {
		fu_progress_add_step(progress,
				     FWUPD_STATUS_DEVICE_WRITE,
				     (100 * fu_firmware_get_size(fw_stx) / totalsz),
				     "stx");
	}
	if (fw_srx != NULL) {
		fu_progress_add_step(progress,
				     FWUPD_STATUS_DEVICE_WRITE,
				     (100 * fu_firmware_get_size(fw_srx) / totalsz),
				     "srx");
	}
	if (fw_ocm != NULL) {
		fu_progress_add_step(progress,
				     FWUPD_STATUS_DEVICE_WRITE,
				     (100 * fu_firmware_get_size(fw_ocm) / totalsz),
				     "ocm");
	}

	/* CUSTOM_DEF */
	if (fw_cus != NULL) {
		if (!fu_analogix_device_write_image(self,
						    fw_cus,
						    ANX_BB_WVAL_UPDATE_CUSTOM_DEF,
						    fu_progress_get_child(progress),
						    error)) {
			g_prefix_error(error, "program custom define failed: ");
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* SECURE_TX */
	if (fw_stx != NULL) {
		if (!fu_analogix_device_write_image(self,
						    fw_stx,
						    ANX_BB_WVAL_UPDATE_SECURE_TX,
						    fu_progress_get_child(progress),
						    error)) {
			g_prefix_error(error, "program secure TX failed: ");
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* SECURE_RX */
	if (fw_srx != NULL) {
		if (!fu_analogix_device_write_image(self,
						    fw_srx,
						    ANX_BB_WVAL_UPDATE_SECURE_RX,
						    fu_progress_get_child(progress),
						    error)) {
			g_prefix_error(error, "program secure RX failed: ");
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* OCM */
	if (fw_ocm != NULL) {
		if (!fu_analogix_device_write_image(self,
						    fw_ocm,
						    ANX_BB_WVAL_UPDATE_OCM,
						    fu_progress_get_child(progress),
						    error)) {
			g_prefix_error(error, "program OCM failed: ");
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_analogix_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(FwupdRequest) request = fwupd_request_new();

	/* the user has to do something */
	fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
	fwupd_request_set_id(request, FWUPD_REQUEST_ID_REMOVE_REPLUG);
	fwupd_request_add_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	fwupd_request_set_message(request,
				  "The update will continue when the device USB cable has been "
				  "unplugged and then re-inserted.");
	fu_device_emit_request(device, request);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success */
	return TRUE;
}

static void
fu_analogix_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static void
fu_analogix_device_init(FuAnalogixDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.analogix.bb");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_AABB_CCDD);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_ANALOGIX_FIRMWARE);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_USER_REPLUG); /* 40 s */
}

static void
fu_analogix_device_class_init(FuAnalogixDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_analogix_device_to_string;
	klass_device->write_firmware = fu_analogix_device_write_firmware;
	klass_device->attach = fu_analogix_device_attach;
	klass_device->setup = fu_analogix_device_setup;
	klass_device->probe = fu_analogix_device_probe;
	klass_device->set_progress = fu_analogix_device_set_progress;
}
