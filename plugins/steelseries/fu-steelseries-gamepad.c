/*
 * Copyright (C) 2021 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-steelseries-common.h"
#include "fu-steelseries-gamepad.h"

#define STEELSERIES_TRANSACTION_TIMEOUT	 1000
#define STEELSERIES_BUFFER_CONTROL_SIZE	 64
#define STEELSERIES_BUFFER_TRANSFER_SIZE 32

struct _FuSteelseriesGamepad {
	FuUsbDevice parent_instance;
	FuSteelseriesDeviceKind device_kind;
	guint8 iface_idx;
	guint8 ep;
	gsize in_size;
};

G_DEFINE_TYPE(FuSteelseriesGamepad, fu_steelseries_gamepad, FU_TYPE_USB_DEVICE)

static gboolean
fu_steelseries_gamepad_command(FuDevice *device, guint8 *data, gboolean answer, GError **error)
{
	FuSteelseriesGamepad *self = FU_STEELSERIES_GAMEPAD(device);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));
	gsize actual_len = 0;
	gboolean ret;

	ret = g_usb_device_control_transfer(usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    0x09,
					    0x0200,
					    self->iface_idx,
					    data,
					    STEELSERIES_BUFFER_CONTROL_SIZE,
					    &actual_len,
					    STEELSERIES_TRANSACTION_TIMEOUT,
					    NULL,
					    error);
	if (!ret) {
		g_prefix_error(error, "failed to do control transfer: ");
		return FALSE;
	}
	if (actual_len != STEELSERIES_BUFFER_CONTROL_SIZE) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "only wrote %" G_GSIZE_FORMAT "bytes",
			    actual_len);
		return FALSE;
	}

	/* cleanup the buffer before receiving any data */
	memset(data, 0x00, STEELSERIES_BUFFER_CONTROL_SIZE);

	/* do not expect the answer from device */
	if (answer != TRUE)
		return TRUE;

	ret = g_usb_device_interrupt_transfer(usb_device,
					      self->ep,
					      data,
					      self->in_size,
					      &actual_len,
					      STEELSERIES_TRANSACTION_TIMEOUT,
					      NULL,
					      error);

	if (!ret) {
		g_prefix_error(error, "failed to do EP transfer: ");
		return FALSE;
	}
	if (actual_len != self->in_size) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "only read %" G_GSIZE_FORMAT "bytes",
			    actual_len);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_steelseries_gamepad_cmd_erase(FuDevice *device, GError **error)
{
	FuSteelseriesGamepad *self = FU_STEELSERIES_GAMEPAD(device);

	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0xA1, 0xAA, 0x55};

	/* dongle for gamepad is using different options */
	if (self->device_kind == FU_STEELSERIES_DEVICE_GAMEPAD_DONGLE) {
		/* dongle */
		data[8] = 0xD0;
		data[9] = 0x01;
	} else {
		/* gamepad */
		data[9] = 0x02;
		/* magic is needed for newer gamepad */
		data[13] = 0x02;
	}

	if (!fu_steelseries_gamepad_command(device, data, FALSE, error)) {
		g_prefix_error(error, "unable erase flash block: ");
		return FALSE;
	}

	/* timeout to give some time to erase */
	g_usleep(20000);

	return TRUE;
}

static gboolean
fu_steelseries_gamepad_probe(FuDevice *device, GError **error)
{
#if G_USB_CHECK_VERSION(0, 3, 3)
	FuSteelseriesGamepad *self = FU_STEELSERIES_GAMEPAD(device);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));
	GUsbInterface *iface = NULL;
	GUsbEndpoint *ep = NULL;
	guint8 iface_id;
	guint8 ep_id;
	guint16 packet_size;
	g_autoptr(GPtrArray) ifaces = NULL;
	g_autoptr(GPtrArray) endpoints = NULL;

	/* FuUsbDevice->probe */
	if (!FU_DEVICE_CLASS(fu_steelseries_gamepad_parent_class)->probe(device, error))
		return FALSE;

	ifaces = g_usb_device_get_interfaces(usb_device, error);
	if (ifaces == NULL || ifaces->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "update interface not found");
		return FALSE;
	}

	/* use the last interface for interrupt transfer */
	iface_id = ifaces->len - 1;

	iface = g_ptr_array_index(ifaces, iface_id);

	endpoints = g_usb_interface_get_endpoints(iface);
	/* expecting to have only one endpoint for communication */
	if (endpoints == NULL || endpoints->len != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "endpoint not found");
		return FALSE;
	}

	ep = g_ptr_array_index(endpoints, 0);
	ep_id = g_usb_endpoint_get_address(ep);
	packet_size = g_usb_endpoint_get_maximum_packet_size(ep);

	self->iface_idx = iface_id;
	self->ep = ep_id;
	self->in_size = packet_size;

	fu_usb_device_add_interface(FU_USB_DEVICE(self), iface_id);
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "this version of GUsb is not supported");
	return FALSE;
#endif
}

static gboolean
fu_steelseries_gamepad_setup(FuDevice *device, GError **error)
{
	g_autofree gchar *bootloader_version = NULL;
	g_autofree gchar *version = NULL;
	guint16 fw_ver;
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0};

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	/* get version of FW and bootloader */
	data[0] = 0x12;
	if (!fu_steelseries_gamepad_command(device, data, TRUE, error))
		return FALSE;

	if (!fu_common_read_uint16_safe(data, sizeof(data), 0x01, &fw_ver, G_LITTLE_ENDIAN, error))
		return FALSE;
	version = fu_common_version_from_uint16(fw_ver, FWUPD_VERSION_FORMAT_BCD);
	fu_device_set_version(FU_DEVICE(device), version);

	if (!fu_common_read_uint16_safe(data, sizeof(data), 0x03, &fw_ver, G_LITTLE_ENDIAN, error))
		return FALSE;
	bootloader_version = fu_common_version_from_uint16(fw_ver, FWUPD_VERSION_FORMAT_BCD);
	fu_device_set_version_bootloader(device, bootloader_version);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_gamepad_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0xA6, 0xAA, 0x55};
	g_autoptr(GError) error_local = NULL;

	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	/* switch to runtime mode */
	if (!fu_steelseries_gamepad_command(device, data, FALSE, &error_local))
		g_debug("ignoring error on reset: %s", error_local->message);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	return TRUE;
}

static gboolean
fu_steelseries_gamepad_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0x02, 0x08};
	g_autoptr(GError) error_local = NULL;

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	/* switch to bootloader mode */
	if (!fu_steelseries_gamepad_command(device, data, FALSE, &error_local))
		g_debug("ignoring error on reset: %s", error_local->message);

	/* controller will be renumbered after switching to bootloader mode */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	return TRUE;
}

static gboolean
fu_steelseries_gamepad_write_firmware_chunks(FuDevice *device,
					     GPtrArray *chunks,
					     FuProgress *progress,
					     guint32 *checksum,
					     GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);

	for (guint id = 0; id < chunks->len; id++) {
		FuChunk *chunk = g_ptr_array_index(chunks, id);
		guint16 chunk_checksum;
		guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0xA3};

		/* block ID */
		if (!fu_common_write_uint16_safe(data,
						 STEELSERIES_BUFFER_CONTROL_SIZE,
						 0x01,
						 (guint16)id,
						 G_LITTLE_ENDIAN,
						 error))
			return FALSE;
		/* 32B of data only */
		if (!fu_memcpy_safe(data,
				    STEELSERIES_BUFFER_CONTROL_SIZE,
				    0x03,
				    fu_chunk_get_data(chunk),
				    STEELSERIES_BUFFER_TRANSFER_SIZE,
				    0,
				    fu_chunk_get_data_sz(chunk),
				    error))
			return FALSE;

		/* block checksum */
		/* probably not necessary */
		chunk_checksum = fu_common_sum16(data + 3, STEELSERIES_BUFFER_TRANSFER_SIZE);
		if (!fu_common_write_uint16_safe(data,
						 STEELSERIES_BUFFER_CONTROL_SIZE,
						 0x03 + STEELSERIES_BUFFER_TRANSFER_SIZE,
						 chunk_checksum,
						 G_LITTLE_ENDIAN,
						 error))
			return FALSE;

		*checksum += (guint32)chunk_checksum;

		if (!fu_steelseries_gamepad_command(device, data, FALSE, error)) {
			g_prefix_error(error, "unable to flash block %u: ", id);
			return FALSE;
		}
		/* timeout to give some time to flash the block on device */
		g_usleep(10000);
		fu_progress_step_done(progress);
	}

	return TRUE;
}

static gboolean
fu_steelseries_gamepad_write_checksum(FuDevice *device, guint32 checksum, GError **error)
{
	/* write checksum */
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0xA5, 0xAA, 0x55};

	if (!fu_common_write_uint32_safe(data,
					 STEELSERIES_BUFFER_CONTROL_SIZE,
					 0x03,
					 checksum,
					 G_LITTLE_ENDIAN,
					 error))
		return FALSE;

	if (!fu_steelseries_gamepad_command(device, data, TRUE, error)) {
		g_prefix_error(error, "unable to write checksum: ");
		return FALSE;
	}

	/* validate checksum */
	if (data[0] != 0xA5 || data[1] != 0xAA || data[2] != 0x55 || data[3] != 0x01) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "Controller is unable to validate checksum");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_steelseries_gamepad_write_firmware(FuDevice *device,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	guint32 checksum = 0;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;

	chunks = fu_chunk_array_new_from_bytes(blob, 0, 0, STEELSERIES_BUFFER_TRANSFER_SIZE);

	if (chunks->len > (G_MAXUINT16 + 1)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "too lot of firmware chunks for the device");
		return FALSE;
	}

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 1);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 0);

	/* erase all first */
	if (!fu_steelseries_gamepad_cmd_erase(device, error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_steelseries_gamepad_write_firmware_chunks(device,
							  chunks,
							  fu_progress_get_child(progress),
							  &checksum,
							  error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_steelseries_gamepad_write_checksum(device, checksum, error))
		return FALSE;
	fu_progress_step_done(progress);

	return TRUE;
}

static gboolean
fu_steelseries_gamepad_set_quirk_kv(FuDevice *device,
				    const gchar *key,
				    const gchar *value,
				    GError **error)
{
	FuSteelseriesGamepad *self = FU_STEELSERIES_GAMEPAD(device);

	if (g_strcmp0(key, "SteelSeriesDeviceKind") == 0) {
		self->device_kind = fu_steelseries_device_type_from_string(value);
		if (self->device_kind != FU_STEELSERIES_DEVICE_UNKNOWN)
			return TRUE;

		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "unsupported SteelSeriesDeviceKind quirk format");
		return FALSE;
	}

	/* failed */
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "quirk key not supported");
	return FALSE;
}

static void
fu_steelseries_gamepad_to_string(FuDevice *device, guint idt, GString *str)
{
	FuSteelseriesGamepad *self = FU_STEELSERIES_GAMEPAD(device);

	FU_DEVICE_CLASS(fu_steelseries_gamepad_parent_class)->to_string(device, idt, str);

	fu_common_string_append_kv(str,
				   idt,
				   "DeviceKind",
				   fu_steelseries_device_type_to_string(self->device_kind));
	fu_common_string_append_kx(str, idt, "Interface", self->iface_idx);
	fu_common_string_append_kx(str, idt, "Endpoint", self->ep);
}

static void
fu_steelseries_gamepad_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1); /* detach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 93);	/* write */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1); /* attach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5);	/* reload */
}

static void
fu_steelseries_gamepad_class_init(FuSteelseriesGamepadClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);

	klass_device->probe = fu_steelseries_gamepad_probe;
	klass_device->setup = fu_steelseries_gamepad_setup;
	klass_device->attach = fu_steelseries_gamepad_attach;
	klass_device->detach = fu_steelseries_gamepad_detach;
	klass_device->write_firmware = fu_steelseries_gamepad_write_firmware;
	klass_device->set_quirk_kv = fu_steelseries_gamepad_set_quirk_kv;
	klass_device->to_string = fu_steelseries_gamepad_to_string;
	klass_device->set_progress = fu_steelseries_gamepad_set_progress;
}

static void
fu_steelseries_gamepad_init(FuSteelseriesGamepad *self)
{
	self->device_kind = FU_STEELSERIES_DEVICE_GAMEPAD;

	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_BCD);

	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_ADD_COUNTERPART_GUIDS);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_protocol(FU_DEVICE(self), "com.steelseries.gamepad");

	fu_device_set_firmware_size_max(FU_DEVICE(self),
					(G_MAXUINT16 + 1) * STEELSERIES_BUFFER_TRANSFER_SIZE);
}
