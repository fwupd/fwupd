/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "lu-common.h"
#include "lu-device-bootloader-nordic.h"

struct _LuDeviceBootloaderNordic
{
	LuDeviceBootloader	 parent_instance;
};

G_DEFINE_TYPE (LuDeviceBootloaderNordic, lu_device_bootloader_nordic, LU_TYPE_DEVICE_BOOTLOADER)

static gchar *
lu_device_bootloader_nordic_get_hw_platform_id (LuDevice *device, GError **error)
{
	g_autoptr(LuDeviceBootloaderRequest) req = lu_device_bootloader_request_new ();
	req->cmd = LU_DEVICE_BOOTLOADER_CMD_GET_HW_PLATFORM_ID;
	if (!lu_device_bootloader_request (device, req, error)) {
		g_prefix_error (error, "failed to get HW ID: ");
		return NULL;
	}
	return g_strndup ((const gchar *) req->data, req->len);
}

static gchar *
lu_device_bootloader_nordic_get_fw_version (LuDevice *device, GError **error)
{
	guint16 micro;

	g_autoptr(LuDeviceBootloaderRequest) req = lu_device_bootloader_request_new ();
	req->cmd = LU_DEVICE_BOOTLOADER_CMD_GET_FW_VERSION;
	if (!lu_device_bootloader_request (device, req, error)) {
		g_prefix_error (error, "failed to get firmware version: ");
		return NULL;
	}

	/* RRRxx.yy_Bzzzz
	 * 012345678901234*/
	micro = (guint16) lu_buffer_read_uint8 ((const gchar *) req->data + 10) << 8;
	micro += lu_buffer_read_uint8 ((const gchar *) req->data + 12);
	return lu_format_version ("RQR",
				  lu_buffer_read_uint8 ((const gchar *) req->data + 3),
				  lu_buffer_read_uint8 ((const gchar *) req->data + 6),
				  micro);
}

static gboolean
lu_device_bootloader_nordic_probe (LuDevice *device, GError **error)
{
	g_autofree gchar *hw_platform_id = NULL;
	g_autofree gchar *version_fw = NULL;
	g_autoptr(GError) error_local = NULL;

	/* get MCU */
	hw_platform_id = lu_device_bootloader_nordic_get_hw_platform_id (device, error);
	if (hw_platform_id == NULL)
		return FALSE;
	g_debug ("hw-platform-id=%s", hw_platform_id);

	/* get firmware version, which is not fatal */
	version_fw = lu_device_bootloader_nordic_get_fw_version (device, &error_local);
	if (version_fw == NULL) {
		g_warning ("failed to get firmware version: %s",
			   error_local->message);
		fu_device_set_version (FU_DEVICE (device), "RQR12.xx_Bxxxx");
	} else {
		fu_device_set_version (FU_DEVICE (device), version_fw);
	}

	return TRUE;
}

static gboolean
lu_device_bootloader_nordic_write_signature (LuDevice *device,
					     guint16 addr, guint8 len, const guint8 *data,
					     GError **error)
{
	g_autoptr(LuDeviceBootloaderRequest) req = lu_device_bootloader_request_new();
	req->cmd = 0xC0;
	req->addr = addr;
	req->len = len;
	memcpy (req->data, data, req->len);
	if (!lu_device_bootloader_request (device, req, error)) {
		g_prefix_error (error, "failed to write sig @0x%02x: ", addr);
		return FALSE;
	}
	if (req->cmd == LU_DEVICE_BOOTLOADER_CMD_WRITE_RAM_BUFFER_INVALID_ADDR) {
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
lu_device_bootloader_nordic_write (LuDevice *device,
				   guint16 addr, guint8 len, const guint8 *data,
				   GError **error)
{
	g_autoptr(LuDeviceBootloaderRequest) req = lu_device_bootloader_request_new ();
	req->cmd = LU_DEVICE_BOOTLOADER_CMD_WRITE;
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
	if (!lu_device_bootloader_request (device, req, error)) {
		g_prefix_error (error, "failed to transfer fw @0x%02x: ", addr);
		return FALSE;
	}
	if (req->cmd == LU_DEVICE_BOOTLOADER_CMD_WRITE_INVALID_ADDR) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to write @%04x: invalid address",
			     addr);
		return FALSE;
	}
	if (req->cmd == LU_DEVICE_BOOTLOADER_CMD_WRITE_VERIFY_FAIL) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to write @%04x: failed to verify flash content",
			     addr);
		return FALSE;
	}
	if (req->cmd == LU_DEVICE_BOOTLOADER_CMD_WRITE_NONZERO_START) {
		g_debug ("wrote %d bytes at address %04x, value %02x", req->len,
			 req->addr, req->data[0]);
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to write @%04x: only 1 byte write of 0xff supported",
			     addr);
		return FALSE;
	}
	if (req->cmd == LU_DEVICE_BOOTLOADER_CMD_WRITE_INVALID_CRC) {
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
lu_device_bootloader_nordic_erase (LuDevice *device, guint16 addr, GError **error)
{
	g_autoptr(LuDeviceBootloaderRequest) req = lu_device_bootloader_request_new ();
	req->cmd = LU_DEVICE_BOOTLOADER_CMD_ERASE_PAGE;
	req->addr = addr;
	req->len = 0x01;
	if (!lu_device_bootloader_request (device, req, error)) {
		g_prefix_error (error, "failed to erase fw @0x%02x: ", addr);
		return FALSE;
	}
	if (req->cmd == LU_DEVICE_BOOTLOADER_CMD_ERASE_PAGE_INVALID_ADDR) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to erase @%04x: invalid page",
			     addr);
		return FALSE;
	}
	if (req->cmd == LU_DEVICE_BOOTLOADER_CMD_ERASE_PAGE_NONZERO_START) {
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
lu_device_bootloader_nordic_write_firmware (LuDevice *device, GBytes *fw, GError **error)
{
	const LuDeviceBootloaderRequest *payload;
	guint16 addr;
	g_autoptr(GPtrArray) reqs = NULL;

	/* erase firmware pages up to the bootloader */
	for (addr = lu_device_bootloader_get_addr_lo (device);
	     addr < lu_device_bootloader_get_addr_hi (device);
	     addr += lu_device_bootloader_get_blocksize (device)) {
		if (!lu_device_bootloader_nordic_erase (device, addr, error))
			return FALSE;
	}

	/* transfer payload */
	reqs = lu_device_bootloader_parse_requests (device, fw, error);
	if (reqs == NULL)
		return FALSE;

	for (guint i = 1; i < reqs->len; i++) {
		gboolean res;
		payload = g_ptr_array_index (reqs, i);

		if (payload->cmd == LU_DEVICE_BOOTLOADER_CMD_WRITE_SIGNATURE) {
			res = lu_device_bootloader_nordic_write_signature(device,
									  payload->addr,
									  payload->len,
									  payload->data,
									  error);
		} else {
			res = lu_device_bootloader_nordic_write (device,
								 payload->addr,
								 payload->len,
								 payload->data,
								 error);
		}

		if (!res)
			return FALSE;
		fu_device_set_progress_full (FU_DEVICE (device), i * 32, reqs->len * 32);
	}

	/* send the first managed packet last, excluding the reset vector */
	payload = g_ptr_array_index (reqs, 0);
	if (!lu_device_bootloader_nordic_write (device,
						payload->addr + 1,
						payload->len - 1,
						payload->data + 1,
						error))
		return FALSE;

	if (!lu_device_bootloader_nordic_write (device,
						0x0000,
						0x01,
						payload->data,
						error))
		return FALSE;

	/* mark as complete */
	fu_device_set_progress_full (FU_DEVICE (device), reqs->len * 32, reqs->len * 32);

	/* success! */
	return TRUE;
}

static void
lu_device_bootloader_nordic_class_init (LuDeviceBootloaderNordicClass *klass)
{
	LuDeviceClass *klass_device = LU_DEVICE_CLASS (klass);
	LuDeviceBootloaderClass *klass_device_bootloader = LU_DEVICE_BOOTLOADER_CLASS (klass);
	klass_device->write_firmware = lu_device_bootloader_nordic_write_firmware;
	klass_device_bootloader->probe = lu_device_bootloader_nordic_probe;
}

static void
lu_device_bootloader_nordic_init (LuDeviceBootloaderNordic *device)
{
}
