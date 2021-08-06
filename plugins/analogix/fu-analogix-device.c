/*
 * Copyright (C) 2021 Xiaotian Cui <xtcui@analogixsemi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#include "config.h"

#include <fwupdplugin.h>

#include "fu-analogix-common.h"
#include "fu-analogix-device.h"
#include "fu-analogix-firmware.h"

struct _FuAnalogixDevice {
	FuUsbDevice	 parent_instance;
	guint8		 iface_idx;		/* bInterfaceNumber */
	guint16		 ocm_version;
	guint16		 custom_version;
};

G_DEFINE_TYPE (FuAnalogixDevice, fu_analogix_device, FU_TYPE_USB_DEVICE)

static void
fu_analogix_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuAnalogixDevice *self = FU_ANALOGIX_DEVICE (device);
	fu_common_string_append_kx (str, idt, "IfaceIdx", self->iface_idx);
	fu_common_string_append_kx (str, idt, "OcmVersion", self->ocm_version);
	fu_common_string_append_kx (str, idt, "CustomVersion", self->custom_version);
}

static gboolean
fu_analogix_device_send (FuAnalogixDevice *self,
			 AnxBbRqtCode reqcode,
			 guint16 val0code,
			 guint16 index,
			 const guint8 *buf,
			 gsize bufsz,
			 GError **error)
{
	gsize actual_len = 0;
	g_autofree guint8 *buf_tmp = NULL;

	g_return_val_if_fail (buf != NULL, FALSE);
	g_return_val_if_fail (bufsz <= 64, FALSE);

	/* make mutable */
	buf_tmp = fu_memdup_safe (buf, bufsz, error);
	if (buf_tmp == NULL)
		return FALSE;

	/* send data to device */
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    reqcode, /* request */
					    val0code, /* value */
					    index, /* index */
					    buf_tmp, /* data */
					    bufsz, /* length */
					    &actual_len, /* actual length */
					    (guint) ANX_BB_TRANSACTION_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "send data error: ");
		return FALSE;
	}
	if (actual_len != bufsz) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "send data length is incorrect");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_analogix_device_receive (FuAnalogixDevice *self,
			    AnxBbRqtCode reqcode,
			    guint16 val0code,
			    guint16 index,
			    guint8 *buf,
			    gsize bufsz,
			    GError **error)
{
	gsize actual_len = 0;

	g_return_val_if_fail (buf != NULL, FALSE);
	g_return_val_if_fail (bufsz <= 64, FALSE);

	/* get data from device */
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    reqcode, /* request */
					    val0code, /* value */
					    index,
					    buf, /* data */
					    bufsz,  /* length */
					    &actual_len, /* actual length */
					    (guint) ANX_BB_TRANSACTION_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "receive data error: ");
		return FALSE;
	}
	if (actual_len != bufsz) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "receive data length is incorrect");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_analogix_device_get_update_status (FuAnalogixDevice *self,
				      AnxUpdateStatus *status,
				      GError **error)
{
	for (guint i = 0; i < 3000; i++) {
		guint8 status_tmp = UPDATE_STATUS_INVALID;
		if (!fu_analogix_device_receive (self,
						 ANX_BB_RQT_GET_UPDATE_STATUS,
						 0, 0,
						 &status_tmp, sizeof(status_tmp),
						 error))
			return FALSE;
		if ((status_tmp != UPDATE_STATUS_ERROR) &&
			(status_tmp != UPDATE_STATUS_INVALID)) {
			if (status != NULL)
				*status = status_tmp;
			return TRUE;
		}
		/* wait 1ms */
		g_usleep (1000);
	}
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "timed out: status was invalid");
	return FALSE;
}

static gboolean
fu_analogix_device_open (FuDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	FuAnalogixDevice *self = FU_ANALOGIX_DEVICE (device);

	/* FuUsbDevice->open */
	if (!FU_DEVICE_CLASS (fu_analogix_device_parent_class)->open (device, error))
		return FALSE;
	if (!g_usb_device_claim_interface (usb_device, self->iface_idx,
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   error)) {
		g_prefix_error (error, "failed to claim interface: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_analogix_device_setup (FuDevice *device, GError **error)
{
	FuAnalogixDevice *self = FU_ANALOGIX_DEVICE (device);
	guint8 buf_fw[2] = { 0x0 };
	guint8 buf_custom[2] = { 0x0 };
	g_autofree gchar *version = NULL;

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS (fu_analogix_device_parent_class)->setup (device, error))
		return FALSE;

	/* get OCM version */
	if (!fu_analogix_device_receive (self, ANX_BB_RQT_READ_FW_VER, 0, 0,
					 &buf_fw[1], 1, error))
		return FALSE;
	if (!fu_analogix_device_receive (self, ANX_BB_RQT_READ_FW_RVER, 0, 0,
				         &buf_fw[0], 1, error))
		return FALSE;
	self->ocm_version = fu_common_read_uint16 (buf_fw, G_LITTLE_ENDIAN);

	/*  get custom version */
	if (!fu_analogix_device_receive (self, ANX_BB_RQT_READ_CUS_VER, 0, 0,
					 &buf_custom[1], 1, error))
		return FALSE;
	if (!fu_analogix_device_receive (self, ANX_BB_RQT_READ_CUS_RVER, 0, 0,
				         &buf_custom[0], 1, error))
		return FALSE;
	self->custom_version = fu_common_read_uint16 (buf_custom, G_LITTLE_ENDIAN);

	/* device version is both versions as a pair */
	version = g_strdup_printf ("%04x.%04x", self->custom_version, self->ocm_version);
	fu_device_set_version (FU_DEVICE (device), version);
	return TRUE;
}

static gboolean
fu_analogix_device_find_interface (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);
	FuAnalogixDevice *self = FU_ANALOGIX_DEVICE (device);
	g_autoptr(GPtrArray) intfs = NULL;

	intfs = g_usb_device_get_interfaces (usb_device, error);
	if (intfs == NULL)
		return FALSE;
	for (guint i = 0; i < intfs->len; i++) {
		GUsbInterface *intf = g_ptr_array_index (intfs, i);
		if (g_usb_interface_get_class (intf) == BILLBOARD_CLASS &&
		    g_usb_interface_get_subclass (intf) == BILLBOARD_SUBCLASS &&
		    g_usb_interface_get_protocol (intf) == BILLBOARD_PROTOCOL) {
			g_autoptr(GPtrArray) endpoints = NULL;

			endpoints = g_usb_interface_get_endpoints (intf);
			if (endpoints == NULL)
				continue;
			self->iface_idx = g_usb_interface_get_number (intf);
			return TRUE;
		}
	}
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "no update interface found");
	return FALSE;
}

static gboolean
fu_analogix_device_probe (FuDevice *device, GError **error)
{
	/* FuUsbDevice->probe */
	if (!FU_DEVICE_CLASS (fu_analogix_device_parent_class)->probe (device, error))
		return FALSE;
	if (!fu_analogix_device_find_interface (FU_USB_DEVICE (device), error)) {
		g_prefix_error (error, "failed to find update interface: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static FuFirmware *
fu_analogix_device_prepare_firmware (FuDevice *device,
				     GBytes *fw,
				     FwupdInstallFlags flags,
				     GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_analogix_firmware_new ();
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;
	return g_steal_pointer (&firmware);
}

static gboolean
fu_analogix_device_write_image(FuAnalogixDevice *self,
			       FuFirmware *image,
			       guint16 req_val,
			       FuProgress *progress,
			       GError **error)
{
	AnxUpdateStatus status = UPDATE_STATUS_INVALID;
	guint8 buf_init[4] = { 0x0 };
	g_autoptr(GBytes) block_bytes = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* offset into firmware */
	block_bytes = fu_firmware_get_bytes (image, error);
	if (block_bytes == NULL)
		return FALSE;

	/* initialization */
	fu_common_write_uint32 (buf_init, g_bytes_get_size (block_bytes), G_LITTLE_ENDIAN);
	if (!fu_analogix_device_send (self,
				      ANX_BB_RQT_SEND_UPDATE_DATA,
				      req_val,
				      0,
				      buf_init,
				      3,
				      error)) {
		g_prefix_error (error, "program initialized failed: ");
		return FALSE;
	}
	if (!fu_analogix_device_get_update_status (self, &status, error))
		return FALSE;

	/* write data */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_WRITE);
	chunks = fu_chunk_array_new_from_bytes (block_bytes, 0x00, 0x00,
						BILLBOARD_MAX_PACKET_SIZE);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		if (!fu_analogix_device_send (self,
					      ANX_BB_RQT_SEND_UPDATE_DATA,
					      req_val,
					      i + 1,
					      fu_chunk_get_data (chk),
					      fu_chunk_get_data_sz (chk),
					      error)) {
			g_prefix_error (error, "failed send on chk %u: ", i);
			return FALSE;
		}
		if (!fu_analogix_device_get_update_status (self, &status, error)) {
			g_prefix_error (error, "failed status on chk %u: ", i);
			return FALSE;
		}
		fu_progress_set_percentage_full(progress, i, chunks->len - 1);
	}

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
	FuAnalogixDevice *self = FU_ANALOGIX_DEVICE (device);
	g_autoptr(FuFirmware) fw_cus = NULL;
	g_autoptr(FuFirmware) fw_ocm = NULL;
	g_autoptr(FuFirmware) fw_srx = NULL;
	g_autoptr(FuFirmware) fw_stx = NULL;

	/* progress */
	fu_progress_set_custom_steps(progress,
				     20 /* cus */,
				     20 /* stx */,
				     20 /* srx */,
				     20 /* ocm */,
				     -1);

	/* CUSTOM_DEF */
	fw_cus = fu_firmware_get_image_by_id (firmware, "custom", NULL);
	if (fw_cus != NULL) {
		if (!fu_analogix_device_write_image(self,
						    fw_cus,
						    ANX_BB_WVAL_UPDATE_CUSTOM_DEF,
						    fu_progress_get_division(progress),
						    error)) {
			g_prefix_error (error, "program custom define failed: ");
			return FALSE;
		}
	}
	fu_progress_step_done(progress);

	/* SECURE_TX */
	fw_stx = fu_firmware_get_image_by_id (firmware, "stx", NULL);
	if (fw_stx != NULL) {
		if (!fu_analogix_device_write_image(self,
						    fw_stx,
						    ANX_BB_WVAL_UPDATE_SECURE_TX,
						    fu_progress_get_division(progress),
						    error)) {
			g_prefix_error (error, "program secure TX failed: ");
			return FALSE;
		}
	}
	fu_progress_step_done(progress);

	/* SECURE_RX */
	fw_srx = fu_firmware_get_image_by_id (firmware, "srx", NULL);
	if (fw_srx != NULL) {
		if (!fu_analogix_device_write_image(self,
						    fw_srx,
						    ANX_BB_WVAL_UPDATE_SECURE_RX,
						    fu_progress_get_division(progress),
						    error)) {
			g_prefix_error (error, "program secure RX failed: ");
			return FALSE;
		}
	}
	fu_progress_step_done(progress);

	/* OCM */
	fw_ocm = fu_firmware_get_image_by_id (firmware, "ocm", NULL);
	if (fw_ocm != NULL) {
		if (!fu_analogix_device_write_image(self,
						    fw_ocm,
						    ANX_BB_WVAL_UPDATE_OCM,
						    fu_progress_get_division(progress),
						    error)) {
			g_prefix_error (error, "program OCM failed: ");
			return FALSE;
		}
	}

	/* success */
	fu_progress_step_done(progress);
	return TRUE;
}

static gboolean
fu_analogix_device_close (FuDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	FuAnalogixDevice *self = FU_ANALOGIX_DEVICE (device);

	/* release interface */
	if (!g_usb_device_release_interface (usb_device, self->iface_idx,
					     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					     error)) {
		g_prefix_error (error, "failed to release interface: ");
		return FALSE;
	}

	/* FuUsbDevice->close */
	return FU_DEVICE_CLASS (fu_analogix_device_parent_class)->close (device, error);
}

static void
fu_analogix_device_init (FuAnalogixDevice *self)
{
	fu_device_add_protocol (FU_DEVICE (self), "com.analogix.bb");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_set_vendor (FU_DEVICE (self), "Analogix Semiconductor Inc.");
}

static void
fu_analogix_device_class_init (FuAnalogixDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->to_string = fu_analogix_device_to_string;
	klass_device->write_firmware = fu_analogix_device_write_firmware;
	klass_device->setup = fu_analogix_device_setup;
	klass_device->open = fu_analogix_device_open;
	klass_device->probe = fu_analogix_device_probe;
	klass_device->prepare_firmware = fu_analogix_device_prepare_firmware;
	klass_device->close = fu_analogix_device_close;
}
