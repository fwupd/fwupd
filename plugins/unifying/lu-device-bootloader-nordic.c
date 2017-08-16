/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
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

static gchar *
lu_device_bootloader_nordic_get_bl_version (LuDevice *device, GError **error)
{
	guint16 build;

	g_autoptr(LuDeviceBootloaderRequest) req = lu_device_bootloader_request_new ();
	req->cmd = LU_DEVICE_BOOTLOADER_CMD_GET_BL_VERSION;
	if (!lu_device_bootloader_request (device, req, error)) {
		g_prefix_error (error, "failed to get firmware version: ");
		return NULL;
	}

	/* BOTxx.yy_Bzzzz
	 * 012345678901234 */
	build = (guint16) lu_buffer_read_uint8 ((const gchar *) req->data + 10) << 8;
	build += lu_buffer_read_uint8 ((const gchar *) req->data + 12);
	return lu_format_version ("BOT",
				  lu_buffer_read_uint8 ((const gchar *) req->data + 3),
				  lu_buffer_read_uint8 ((const gchar *) req->data + 6),
				  build);
}

static gboolean
lu_device_bootloader_nordic_probe (LuDevice *device, GError **error)
{
	g_autofree gchar *hw_platform_id = NULL;
	g_autofree gchar *version_bl = NULL;
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
		lu_device_set_version_fw (device, "RQR12.xx_Bxxxx");
	} else {
		lu_device_set_version_fw (device, version_fw);
	}

	/* get bootloader version */
	version_bl = lu_device_bootloader_nordic_get_bl_version (device, error);
	if (version_bl == NULL)
		return FALSE;
	lu_device_set_version_bl (device, version_bl);
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
		if (addr == 0x0000) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "failed to write @%04x: only 1 byte write supported",
				     addr);
			return FALSE;
		}
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to write @%04x: byte 0x00 is not 0xff",
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
lu_device_bootloader_nordic_write_firmware (LuDevice *device,
					    GBytes *fw,
					    GFileProgressCallback progress_cb,
					    gpointer progress_data,
					    GError **error)
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
		payload = g_ptr_array_index (reqs, i);
		if (!lu_device_bootloader_nordic_write (device,
							payload->addr,
							payload->len,
							payload->data,
							error))
			return FALSE;
		if (progress_cb != NULL) {
			progress_cb ((goffset) i * 32,
				     (goffset) reqs->len * 32,
				     progress_data);
		}
	}

	/* send the first managed packet last, excluding the reset vector */
	payload = g_ptr_array_index (reqs, 0);
	if (!lu_device_bootloader_nordic_write (device,
						payload->addr + 1,
						payload->len - 1,
						payload->data + 1,
						error))
		return FALSE;

	/* set the reset vector */
	if (!lu_device_bootloader_nordic_write (device,
						0x0000,
						0x01,
						payload->data,
						error))
		return FALSE;

	/* mark as complete */
	if (progress_cb != NULL) {
		progress_cb ((goffset) reqs->len * 32,
			     (goffset) reqs->len * 32,
			     progress_data);
	}

	/* success! */
	return TRUE;
}

static void
lu_device_bootloader_nordic_class_init (LuDeviceBootloaderNordicClass *klass)
{
	LuDeviceClass *klass_device = LU_DEVICE_CLASS (klass);
	klass_device->write_firmware = lu_device_bootloader_nordic_write_firmware;
	klass_device->probe = lu_device_bootloader_nordic_probe;
}

static void
lu_device_bootloader_nordic_init (LuDeviceBootloaderNordic *device)
{
}
