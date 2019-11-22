/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-logitech-hidpp-common.h"
#include "fu-logitech-hidpp-bootloader-nordic.h"

struct _FuLogitechHidPpBootloaderNordic
{
	FuLogitechHidPpBootloader	 parent_instance;
};

G_DEFINE_TYPE (FuLogitechHidPpBootloaderNordic, fu_logitech_hidpp_bootloader_nordic, FU_TYPE_UNIFYING_BOOTLOADER)

static gchar *
fu_logitech_hidpp_bootloader_nordic_get_hw_platform_id (FuLogitechHidPpBootloader *self, GError **error)
{
	g_autoptr(FuLogitechHidPpBootloaderRequest) req = fu_logitech_hidpp_bootloader_request_new ();
	req->cmd = FU_UNIFYING_BOOTLOADER_CMD_GET_HW_PLATFORM_ID;
	if (!fu_logitech_hidpp_bootloader_request (self, req, error)) {
		g_prefix_error (error, "failed to get HW ID: ");
		return NULL;
	}
	return g_strndup ((const gchar *) req->data, req->len);
}

static gchar *
fu_logitech_hidpp_bootloader_nordic_get_fw_version (FuLogitechHidPpBootloader *self, GError **error)
{
	guint16 micro;

	g_autoptr(FuLogitechHidPpBootloaderRequest) req = fu_logitech_hidpp_bootloader_request_new ();
	req->cmd = FU_UNIFYING_BOOTLOADER_CMD_GET_FW_VERSION;
	if (!fu_logitech_hidpp_bootloader_request (self, req, error)) {
		g_prefix_error (error, "failed to get firmware version: ");
		return NULL;
	}

	/* RRRxx.yy_Bzzzz
	 * 012345678901234*/
	micro = (guint16) fu_logitech_hidpp_buffer_read_uint8 ((const gchar *) req->data + 10) << 8;
	micro += fu_logitech_hidpp_buffer_read_uint8 ((const gchar *) req->data + 12);
	return fu_logitech_hidpp_format_version ("RQR",
					       fu_logitech_hidpp_buffer_read_uint8 ((const gchar *) req->data + 3),
					       fu_logitech_hidpp_buffer_read_uint8 ((const gchar *) req->data + 6),
					       micro);
}

static gboolean
fu_logitech_hidpp_bootloader_nordic_setup (FuLogitechHidPpBootloader *self, GError **error)
{
	g_autofree gchar *hw_platform_id = NULL;
	g_autofree gchar *version_fw = NULL;
	g_autoptr(GError) error_local = NULL;

	/* get MCU */
	hw_platform_id = fu_logitech_hidpp_bootloader_nordic_get_hw_platform_id (self, error);
	if (hw_platform_id == NULL)
		return FALSE;
	g_debug ("hw-platform-id=%s", hw_platform_id);

	/* get firmware version, which is not fatal */
	version_fw = fu_logitech_hidpp_bootloader_nordic_get_fw_version (self, &error_local);
	if (version_fw == NULL) {
		g_warning ("failed to get firmware version: %s",
			   error_local->message);
		fu_device_set_version (FU_DEVICE (self), "RQR12.00_B0000",
				       FWUPD_VERSION_FORMAT_PLAIN);
	} else {
		fu_device_set_version (FU_DEVICE (self), version_fw,
				       FWUPD_VERSION_FORMAT_PLAIN);
	}

	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_nordic_write_signature (FuLogitechHidPpBootloader *self,
					       guint16 addr, guint8 len, const guint8 *data,
					       GError **error)
{
	g_autoptr(FuLogitechHidPpBootloaderRequest) req = fu_logitech_hidpp_bootloader_request_new();
	req->cmd = 0xC0;
	req->addr = addr;
	req->len = len;
	memcpy (req->data, data, req->len);
	if (!fu_logitech_hidpp_bootloader_request (self, req, error)) {
		g_prefix_error (error, "failed to write sig @0x%02x: ", addr);
		return FALSE;
	}
	if (req->cmd == FU_UNIFYING_BOOTLOADER_CMD_WRITE_RAM_BUFFER_INVALID_ADDR) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to write @%04x: signature is too big",
			     addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_nordic_write (FuLogitechHidPpBootloader *self,
				     guint16 addr, guint8 len, const guint8 *data,
				     GError **error)
{
	g_autoptr(FuLogitechHidPpBootloaderRequest) req = fu_logitech_hidpp_bootloader_request_new ();
	req->cmd = FU_UNIFYING_BOOTLOADER_CMD_WRITE;
	req->addr = addr;
	req->len = len;
	if (req->len > 28) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to write @%04x: data length too large %02x",
			     addr, req->len);
		return FALSE;
	}
	memcpy (req->data, data, req->len);
	if (!fu_logitech_hidpp_bootloader_request (self, req, error)) {
		g_prefix_error (error, "failed to transfer fw @0x%02x: ", addr);
		return FALSE;
	}
	if (req->cmd == FU_UNIFYING_BOOTLOADER_CMD_WRITE_INVALID_ADDR) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to write @%04x: invalid address",
			     addr);
		return FALSE;
	}
	if (req->cmd == FU_UNIFYING_BOOTLOADER_CMD_WRITE_VERIFY_FAIL) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to write @%04x: failed to verify flash content",
			     addr);
		return FALSE;
	}
	if (req->cmd == FU_UNIFYING_BOOTLOADER_CMD_WRITE_NONZERO_START) {
		g_debug ("wrote %d bytes at address %04x, value %02x", req->len,
			 req->addr, req->data[0]);
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to write @%04x: only 1 byte write of 0xff supported",
			     addr);
		return FALSE;
	}
	if (req->cmd == FU_UNIFYING_BOOTLOADER_CMD_WRITE_INVALID_CRC) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to write @%04x: invalid CRC",
			     addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_nordic_erase (FuLogitechHidPpBootloader *self, guint16 addr, GError **error)
{
	g_autoptr(FuLogitechHidPpBootloaderRequest) req = fu_logitech_hidpp_bootloader_request_new ();
	req->cmd = FU_UNIFYING_BOOTLOADER_CMD_ERASE_PAGE;
	req->addr = addr;
	req->len = 0x01;
	if (!fu_logitech_hidpp_bootloader_request (self, req, error)) {
		g_prefix_error (error, "failed to erase fw @0x%02x: ", addr);
		return FALSE;
	}
	if (req->cmd == FU_UNIFYING_BOOTLOADER_CMD_ERASE_PAGE_INVALID_ADDR) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to erase @%04x: invalid page",
			     addr);
		return FALSE;
	}
	if (req->cmd == FU_UNIFYING_BOOTLOADER_CMD_ERASE_PAGE_NONZERO_START) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to erase @%04x: byte 0x00 is not 0xff",
			     addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_nordic_write_firmware (FuDevice *device,
					      FuFirmware *firmware,
					      FwupdInstallFlags flags,
					      GError **error)
{
	FuLogitechHidPpBootloader *self = FU_UNIFYING_BOOTLOADER (device);
	const FuLogitechHidPpBootloaderRequest *payload;
	guint16 addr;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) reqs = NULL;

	/* get default image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* erase firmware pages up to the bootloader */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_ERASE);
	for (addr = fu_logitech_hidpp_bootloader_get_addr_lo (self);
	     addr < fu_logitech_hidpp_bootloader_get_addr_hi (self);
	     addr += fu_logitech_hidpp_bootloader_get_blocksize (self)) {
		if (!fu_logitech_hidpp_bootloader_nordic_erase (self, addr, error))
			return FALSE;
	}

	/* transfer payload */
	reqs = fu_logitech_hidpp_bootloader_parse_requests (self, fw, error);
	if (reqs == NULL)
		return FALSE;
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 1; i < reqs->len; i++) {
		gboolean res;
		payload = g_ptr_array_index (reqs, i);

		if (payload->cmd == FU_UNIFYING_BOOTLOADER_CMD_WRITE_SIGNATURE) {
			res = fu_logitech_hidpp_bootloader_nordic_write_signature (self,
										   payload->addr,
										   payload->len,
										   payload->data,
										   error);
		} else {
			res = fu_logitech_hidpp_bootloader_nordic_write (self,
									 payload->addr,
									 payload->len,
									 payload->data,
									 error);
		}

		if (!res)
			return FALSE;
		fu_device_set_progress_full (device, i * 32, reqs->len * 32);
	}

	/* send the first managed packet last, excluding the reset vector */
	payload = g_ptr_array_index (reqs, 0);
	if (!fu_logitech_hidpp_bootloader_nordic_write (self,
							payload->addr + 1,
							payload->len - 1,
							payload->data + 1,
							error))
		return FALSE;

	if (!fu_logitech_hidpp_bootloader_nordic_write (self,
						      0x0000,
						      0x01,
						      payload->data,
						      error))
		return FALSE;

	/* mark as complete */
	fu_device_set_progress_full (device, reqs->len * 32, reqs->len * 32);

	/* success! */
	return TRUE;
}

static void
fu_logitech_hidpp_bootloader_nordic_class_init (FuLogitechHidPpBootloaderNordicClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuLogitechHidPpBootloaderClass *klass_device_bootloader = FU_UNIFYING_BOOTLOADER_CLASS (klass);
	klass_device->write_firmware = fu_logitech_hidpp_bootloader_nordic_write_firmware;
	klass_device_bootloader->setup = fu_logitech_hidpp_bootloader_nordic_setup;
}

static void
fu_logitech_hidpp_bootloader_nordic_init (FuLogitechHidPpBootloaderNordic *self)
{
}
