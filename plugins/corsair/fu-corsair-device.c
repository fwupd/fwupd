/*
 * Copyright (C) 2022 Andrii Dushko <andrii.dushko@developex.net>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-corsair-common.h"
#include "fu-corsair-device.h"

#define CORSAIR_DEFAULT_VENDOR_INTERFACE_ID 1
#define CORSAIR_ACTIVATION_TIMEOUT	    30000
#define CORSAIR_MODE_BOOTLOADER		    3
#define CORSAIR_FIRST_CHUNK_HEADER_SIZE	    7
#define CORSAIR_NEXT_CHUNKS_HEADER_SIZE	    3
#define CORSAIR_MAX_CMD_SIZE		    1024
#define CORSAIR_TRANSACTION_TIMEOUT	    2000

#define CORSAIR_OFFSET_CMD_VERSION	 0x03
#define CORSAIR_OFFSET_CMD_CRC		 0x08
#define CORSAIR_OFFSET_CMD_MODE		 0x03
#define CORSAIR_OFFSET_CMD_STATUS	 0x02
#define CORSAIR_OFFSET_CMD_FIRMWARE_SIZE 0x03
#define CORSAIR_OFFSET_CMD_SET_MODE	 0x04

struct _FuCorsairDevice {
	FuUsbDevice parent_instance;
	FuCorsairDeviceKind device_kind;
	guint8 vendor_interface;
	guint8 epin;
	guint8 epout;
	guint16 cmd_write_size;
	guint16 cmd_read_size;
};
G_DEFINE_TYPE(FuCorsairDevice, fu_corsair_device, FU_TYPE_USB_DEVICE)

typedef enum {
	FU_CORSAIR_DEVICE_MODE_APPLICATION = 1,
	FU_CORSAIR_DEVICE_MODE_BOOTLOADER = 3
} FuCorsairDeviceMode;

static gboolean
fu_corsair_device_command(FuDevice *device, guint8 *data, guint timeout, GError **error)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));
	gsize actual_len = 0;
	gboolean ret;

	ret = g_usb_device_interrupt_transfer(usb_device,
					      self->epout,
					      data,
					      self->cmd_write_size,
					      &actual_len,
					      timeout,
					      NULL,
					      error);
	if (!ret) {
		g_prefix_error(error, "failed to write command: ");
		return FALSE;
	}
	if (actual_len != self->cmd_write_size) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "wrong size written: %" G_GSIZE_FORMAT,
			    actual_len);
		return FALSE;
	}

	memset(data, 0, CORSAIR_MAX_CMD_SIZE);

	ret = g_usb_device_interrupt_transfer(usb_device,
					      self->epin,
					      data,
					      self->cmd_read_size,
					      &actual_len,
					      timeout,
					      NULL,
					      error);
	if (!ret) {
		g_prefix_error(error, "failed to get command response: ");
		return FALSE;
	}
	if (actual_len != self->cmd_read_size) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "wrong size read: %" G_GSIZE_FORMAT,
			    actual_len);
		return FALSE;
	}

	if (data[CORSAIR_OFFSET_CMD_STATUS] != 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "device replied with error: 0x%02x",
			    data[CORSAIR_OFFSET_CMD_STATUS]);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_corsair_device_probe(FuDevice *device, GError **error)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));
	GUsbInterface *iface = NULL;
	GUsbEndpoint *ep1 = NULL;
	GUsbEndpoint *ep2 = NULL;
	g_autoptr(GPtrArray) ifaces = NULL;
	g_autoptr(GPtrArray) endpoints = NULL;

	if (!FU_DEVICE_CLASS(fu_corsair_device_parent_class)->probe(device, error))
		return FALSE;

	ifaces = g_usb_device_get_interfaces(usb_device, error);
	if (ifaces == NULL || (ifaces->len < (self->vendor_interface + 1u))) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "update interface not found");
		return FALSE;
	}

	iface = g_ptr_array_index(ifaces, self->vendor_interface);
	endpoints = g_usb_interface_get_endpoints(iface);
	/* expecting to have two endpoints for communication */
	if (endpoints == NULL || endpoints->len != 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "update interface endpoints not found");
		return FALSE;
	}

	ep1 = g_ptr_array_index(endpoints, 0);
	ep2 = g_ptr_array_index(endpoints, 1);
	if (g_usb_endpoint_get_direction(ep1) == G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST) {
		self->epin = g_usb_endpoint_get_address(ep1);
		self->epout = g_usb_endpoint_get_address(ep2);
		self->cmd_read_size = g_usb_endpoint_get_maximum_packet_size(ep1);
		self->cmd_write_size = g_usb_endpoint_get_maximum_packet_size(ep2);
	} else {
		self->epin = g_usb_endpoint_get_address(ep2);
		self->epout = g_usb_endpoint_get_address(ep1);
		self->cmd_read_size = g_usb_endpoint_get_maximum_packet_size(ep2);
		self->cmd_write_size = g_usb_endpoint_get_maximum_packet_size(ep1);
	}

	if (self->cmd_write_size > CORSAIR_MAX_CMD_SIZE ||
	    self->cmd_read_size > CORSAIR_MAX_CMD_SIZE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "endpoint size is bigger than allowed command size");
		return FALSE;
	}

	fu_usb_device_add_interface(FU_USB_DEVICE(self), self->vendor_interface);
	return TRUE;
}

static gchar *
fu_corsair_get_version(FuDevice *device, GError **error)
{
	guint32 version_raw;
	guint8 data[CORSAIR_MAX_CMD_SIZE] = {0x08, 0x02, 0x13};

	if (!fu_corsair_device_command(device, data, CORSAIR_TRANSACTION_TIMEOUT, error))
		return NULL;

	if (!fu_common_read_uint32_safe(data,
					sizeof(data),
					CORSAIR_OFFSET_CMD_VERSION,
					&version_raw,
					G_LITTLE_ENDIAN,
					error)) {
		g_prefix_error(error, "parse fail: ");
		return NULL;
	}
	return fu_corsair_version_from_uint32(version_raw);
}

static gchar *
fu_corsair_get_bootloader_version(FuDevice *device, GError **error)
{
	guint32 version_raw;
	guint8 data[CORSAIR_MAX_CMD_SIZE] = {0x08, 0x02, 0x14};

	if (!fu_corsair_device_command(device, data, CORSAIR_TRANSACTION_TIMEOUT, error))
		return NULL;

	if (!fu_common_read_uint32_safe(data,
					sizeof(data),
					CORSAIR_OFFSET_CMD_VERSION,
					&version_raw,
					G_LITTLE_ENDIAN,
					error)) {
		g_prefix_error(error, "parse fail: ");
		return NULL;
	}
	return fu_corsair_version_from_uint32(version_raw);
}

static gboolean
fu_corsair_ensure_mode(FuDevice *device, FuCorsairDeviceMode mode, GError **error)
{
	FuCorsairDeviceMode current_mode;
	guint8 set_mode_cmd[CORSAIR_MAX_CMD_SIZE] = {0x08, 0x01, 0x03};

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		current_mode = FU_CORSAIR_DEVICE_MODE_BOOTLOADER;
	} else {
		current_mode = FU_CORSAIR_DEVICE_MODE_APPLICATION;
	}

	if (mode == current_mode)
		return TRUE;

	set_mode_cmd[CORSAIR_OFFSET_CMD_SET_MODE] = mode;
	if (!fu_corsair_device_command(device, set_mode_cmd, CORSAIR_TRANSACTION_TIMEOUT, error)) {
		g_prefix_error(error, "set mode command fail: ");
		return FALSE;
	}
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	return TRUE;
}

static gboolean
fu_corsair_device_setup(FuDevice *device, GError **error)
{
	guint32 mode;
	g_autofree gchar *bootloader_version = NULL;
	g_autofree gchar *version = NULL;
	guint8 get_mode_cmd[CORSAIR_MAX_CMD_SIZE] = {0x08, 0x02, 0x03};

	version = fu_corsair_get_version(device, error);
	if (version == NULL) {
		g_prefix_error(error, "cannot get version: ");
		return FALSE;
	}
	fu_device_set_version(FU_DEVICE(device), version);

	bootloader_version = fu_corsair_get_bootloader_version(device, error);
	if (bootloader_version == NULL) {
		g_prefix_error(error, "cannot get bootloader version: ");
		return FALSE;
	}
	fu_device_set_version_bootloader(device, bootloader_version);

	if (!fu_corsair_device_command(device, get_mode_cmd, CORSAIR_TRANSACTION_TIMEOUT, error))
		return FALSE;

	if (!fu_common_read_uint32_safe(get_mode_cmd,
					sizeof(get_mode_cmd),
					CORSAIR_OFFSET_CMD_MODE,
					&mode,
					G_LITTLE_ENDIAN,
					error)) {
		g_prefix_error(error, "cannot parse device mode: ");
		return FALSE;
	}

	if (mode == FU_CORSAIR_DEVICE_MODE_BOOTLOADER)
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);

	return TRUE;
}

static gboolean
fu_corsair_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	return fu_corsair_ensure_mode(device, FU_CORSAIR_DEVICE_MODE_APPLICATION, error);
}

static gboolean
fu_corsair_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	return fu_corsair_ensure_mode(device, FU_CORSAIR_DEVICE_MODE_BOOTLOADER, error);
}

static gboolean
fu_corsair_activate_firmware(FuDevice *device, guint32 crc, GError **error)
{
	guint8 commit_cmd[CORSAIR_MAX_CMD_SIZE] = {0x08, 0x05, 0x01, 0x00};
	guint8 activate_cmd[CORSAIR_MAX_CMD_SIZE] =
	    {0x08, 0x16, 0x00, 0x01, 0x03, 0x00, 0x01, 0x01};
	if (!fu_corsair_device_command(device, commit_cmd, CORSAIR_TRANSACTION_TIMEOUT, error)) {
		g_prefix_error(error, "firmware commit fail: ");
		return FALSE;
	}

	if (!fu_common_write_uint32_safe(activate_cmd,
					 sizeof(activate_cmd),
					 CORSAIR_OFFSET_CMD_CRC,
					 crc,
					 G_LITTLE_ENDIAN,
					 error)) {
		g_prefix_error(error, "cannot serialize CRC: ");
		return FALSE;
	}
	return fu_corsair_device_command(device, activate_cmd, CORSAIR_ACTIVATION_TIMEOUT, error);
}

static gboolean
fu_corsair_write_first_chunk(FuDevice *device,
			     FuChunk *chunk,
			     guint32 firmware_size,
			     GError **error)
{
	guint8 init_cmd[CORSAIR_MAX_CMD_SIZE] = {0x08, 0x0d, 0x00, 0x03};
	guint8 write_cmd[CORSAIR_MAX_CMD_SIZE] = {0x08, 0x06, 0x00};
	if (!fu_corsair_device_command(device, init_cmd, CORSAIR_TRANSACTION_TIMEOUT, error)) {
		g_prefix_error(error, "firmware init fail: ");
		return FALSE;
	}

	if (!fu_common_write_uint32_safe(write_cmd,
					 sizeof(write_cmd),
					 CORSAIR_OFFSET_CMD_FIRMWARE_SIZE,
					 firmware_size,
					 G_LITTLE_ENDIAN,
					 error)) {
		g_prefix_error(error, "cannot serialize firmware size: ");
		return FALSE;
	}
	if (!fu_memcpy_safe(write_cmd,
			    sizeof(write_cmd),
			    CORSAIR_FIRST_CHUNK_HEADER_SIZE,
			    fu_chunk_get_data(chunk),
			    fu_chunk_get_data_sz(chunk),
			    0,
			    fu_chunk_get_data_sz(chunk),
			    error)) {
		g_prefix_error(error, "cannot set data: ");
		return FALSE;
	}
	if (!fu_corsair_device_command(device, write_cmd, CORSAIR_TRANSACTION_TIMEOUT, error)) {
		g_prefix_error(error, "write command fail: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_corsair_write_chunk(FuDevice *device, FuChunk *chunk, GError **error)
{
	guint8 cmd[CORSAIR_MAX_CMD_SIZE] = {0x08, 0x07};
	if (!fu_memcpy_safe(cmd,
			    sizeof(cmd),
			    CORSAIR_NEXT_CHUNKS_HEADER_SIZE,
			    fu_chunk_get_data(chunk),
			    fu_chunk_get_data_sz(chunk),
			    0,
			    fu_chunk_get_data_sz(chunk),
			    error)) {
		g_prefix_error(error, "cannot set data: ");
		return FALSE;
	}
	if (!fu_corsair_device_command(device, cmd, CORSAIR_TRANSACTION_TIMEOUT, error)) {
		g_prefix_error(error, "write command fail: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_corsair_device_write_firmware_chunks(FuDevice *device,
					FuChunk *first_chunk,
					GPtrArray *chunks,
					FuProgress *progress,
					guint32 firmware_size,
					GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len + 1);

	if (!fu_corsair_write_first_chunk(device, first_chunk, firmware_size, error)) {
		g_prefix_error(error, "cannot write first chunk: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	for (guint id = 0; id < chunks->len; id++) {
		FuChunk *chunk = g_ptr_array_index(chunks, id);
		if (!fu_corsair_write_chunk(device, chunk, error)) {
			g_prefix_error(error, "cannot write chunk %u", id);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	return TRUE;
}

static gboolean
fu_corsair_device_write_firmware(FuDevice *device,
				 FuFirmware *firmware,
				 FuProgress *progress,
				 FwupdInstallFlags flags,
				 GError **error)
{
	const guint8 *firmware_raw;
	guint32 crc;
	gsize firmware_size;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(FuChunk) firstChunk = NULL;
	g_autoptr(GBytes) rest_of_firmware = NULL;
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);
	guint32 first_chunk_size = self->cmd_write_size - CORSAIR_FIRST_CHUNK_HEADER_SIZE;

	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL) {
		g_prefix_error(error, "cannot get firmware data");
		return FALSE;
	}
	firmware_raw = fu_bytes_get_data_safe(blob, &firmware_size, error);
	if (firmware_raw == NULL) {
		g_prefix_error(error, "cannot get firmware data: ");
		return FALSE;
	}

	/* the firmware size should be greater than 1 chunk */
	if (firmware_size <= first_chunk_size) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "update file should be bigger");
		return FALSE;
	}

	firstChunk = fu_chunk_new(0, 0, 0, g_bytes_get_data(blob, NULL), first_chunk_size);
	rest_of_firmware = fu_common_bytes_new_offset(blob,
						      first_chunk_size,
						      firmware_size - first_chunk_size,
						      error);
	if (rest_of_firmware == NULL) {
		g_prefix_error(error, "cannot get firmware past first chunk: ");
		return FALSE;
	}
	chunks =
	    fu_chunk_array_new_from_bytes(rest_of_firmware,
					  first_chunk_size,
					  0,
					  self->cmd_write_size - CORSAIR_NEXT_CHUNKS_HEADER_SIZE);

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 95);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5);

	crc = fu_corsair_calculate_crc(firmware_raw, firmware_size);
	if (!fu_corsair_device_write_firmware_chunks(device,
						     firstChunk,
						     chunks,
						     fu_progress_get_child(progress),
						     g_bytes_get_size(blob),
						     error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_corsair_activate_firmware(device, crc, error)) {
		g_prefix_error(error, "firmware activation fail: ");
		return FALSE;
	}
	fu_progress_step_done(progress);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	return TRUE;
}

static void
fu_corsair_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);

	FU_DEVICE_CLASS(fu_corsair_device_parent_class)->to_string(device, idt, str);

	fu_common_string_append_kv(str,
				   idt,
				   "DeviceKind",
				   fu_corsair_device_type_to_string(self->device_kind));
	fu_common_string_append_kx(str, idt, "InEndpoint", self->epin);
	fu_common_string_append_kx(str, idt, "OutEndpoint", self->epout);
}

static void
fu_corsair_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 4); /* detach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 92);	/* write */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 4); /* attach */
}

static gboolean
fu_corsair_set_quirk_kv(FuDevice *device, const gchar *key, const gchar *value, GError **error)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);
	guint64 vendor_interface;

	if (g_strcmp0(key, "CorsairDeviceKind") == 0) {
		self->device_kind = fu_corsair_device_type_from_string(value);
		if (self->device_kind != FU_CORSAIR_DEVICE_UNKNOWN)
			return TRUE;

		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "unsupported device in quirk");
		return FALSE;
	} else if (g_strcmp0(key, "CorsairVendorInterfaceId") == 0) {
		/* clapped to uint8 because bNumInterfaces is 8 bits long */
		if (!fu_common_strtoull_full(value, &vendor_interface, 0, 255, error)) {
			g_prefix_error(error, "cannot parse CorsairVendorInterface: ");
			return FALSE;
		}
		self->vendor_interface = vendor_interface;
		return TRUE;
	}

	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "quirk key not supported");
	return FALSE;
}

static void
fu_corsair_device_class_init(FuCorsairDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);

	klass_device->probe = fu_corsair_device_probe;
	klass_device->set_quirk_kv = fu_corsair_set_quirk_kv;
	klass_device->setup = fu_corsair_device_setup;
	klass_device->attach = fu_corsair_device_attach;
	klass_device->detach = fu_corsair_device_detach;
	klass_device->write_firmware = fu_corsair_device_write_firmware;
	klass_device->to_string = fu_corsair_device_to_string;
	klass_device->set_progress = fu_corsair_device_set_progress;
}

static void
fu_corsair_device_init(FuCorsairDevice *device)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);

	self->device_kind = FU_CORSAIR_DEVICE_MOUSE;
	self->vendor_interface = CORSAIR_DEFAULT_VENDOR_INTERFACE_ID;

	fu_device_set_remove_delay(FU_DEVICE(device), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_version_format(FU_DEVICE(device), FWUPD_VERSION_FORMAT_TRIPLET);

	fu_device_add_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);

	fu_device_add_internal_flag(FU_DEVICE(device), FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_protocol(FU_DEVICE(device), "com.corsair.bp");
}
