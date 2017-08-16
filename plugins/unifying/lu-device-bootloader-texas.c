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
#include "lu-device-bootloader-texas.h"

struct _LuDeviceBootloaderTexas
{
	LuDeviceBootloader	 parent_instance;
};

G_DEFINE_TYPE (LuDeviceBootloaderTexas, lu_device_bootloader_texas, LU_TYPE_DEVICE_BOOTLOADER)

static gboolean
lu_device_bootloader_texas_erase_all (LuDevice *device, GError **error)
{
	g_autoptr(LuDeviceBootloaderRequest) req = lu_device_bootloader_request_new ();
	req->cmd = LU_DEVICE_BOOTLOADER_CMD_FLASH_RAM;
	req->len = 0x01;	/* magic number */
	req->data[0] = 0x00;	/* magic number */
	if (!lu_device_bootloader_request (device, req, error)) {
		g_prefix_error (error, "failed to erase all pages: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
lu_device_bootloader_texas_compute_and_test_crc (LuDevice *device, GError **error)
{
	g_autoptr(LuDeviceBootloaderRequest) req = lu_device_bootloader_request_new ();
	req->cmd = LU_DEVICE_BOOTLOADER_CMD_FLASH_RAM;
	req->len = 0x01;	/* magic number */
	req->data[0] = 0x03;	/* magic number */
	if (!lu_device_bootloader_request (device, req, error)) {
		g_prefix_error (error, "failed to compute and test CRC: ");
		return FALSE;
	}
	if (req->cmd == LU_DEVICE_BOOTLOADER_CMD_FLASH_RAM_WRONG_CRC) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "CRC is incorrect");
		return FALSE;
	}
	return TRUE;
}

static gboolean
lu_device_bootloader_texas_flash_ram_buffer (LuDevice *device, guint16 addr, GError **error)
{
	g_autoptr(LuDeviceBootloaderRequest) req = lu_device_bootloader_request_new ();
	req->cmd = LU_DEVICE_BOOTLOADER_CMD_FLASH_RAM;
	req->addr = addr;
	req->len = 0x01;	/* magic number */
	req->data[0] = 0x01;	/* magic number */
	if (!lu_device_bootloader_request (device, req, error)) {
		g_prefix_error (error, "failed to flash ram buffer @%04x: ", addr);
		return FALSE;
	}
	if (req->cmd == LU_DEVICE_BOOTLOADER_CMD_FLASH_RAM_INVALID_ADDR) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to flash ram buffer @%04x: invalid flash page",
			     addr);
		return FALSE;
	}
	if (req->cmd == LU_DEVICE_BOOTLOADER_CMD_FLASH_RAM_PAGE0_INVALID) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to flash ram buffer @%04x: invalid App JMP vector",
			     addr);
		return FALSE;
	}
	if (req->cmd == LU_DEVICE_BOOTLOADER_CMD_FLASH_RAM_INVALID_ORDER) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to flash ram buffer @%04x: page flashed before page 0",
			     addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
lu_device_bootloader_texas_clear_ram_buffer (LuDevice *device, guint16 addr, GError **error)
{
	g_autoptr(LuDeviceBootloaderRequest) req = lu_device_bootloader_request_new ();
	req->cmd = LU_DEVICE_BOOTLOADER_CMD_FLASH_RAM;
	req->addr = addr;
	req->len = 0x01;	/* magic number */
	req->data[0] = 0x02;	/* magic number */
	if (!lu_device_bootloader_request (device, req, error)) {
		g_prefix_error (error, "failed to clear ram buffer @%04x: ", addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
lu_device_bootloader_texas_write_firmware (LuDevice *device,
					   GBytes *fw,
					   GFileProgressCallback progress_cb,
					   gpointer progress_data,
					   GError **error)
{
	const LuDeviceBootloaderRequest *payload;
	g_autoptr(GPtrArray) reqs = NULL;
	g_autoptr(LuDeviceBootloaderRequest) req = lu_device_bootloader_request_new ();

	/* transfer payload */
	reqs = lu_device_bootloader_parse_requests (device, fw, error);
	if (reqs == NULL)
		return FALSE;

	/* erase all flash pages */
	if (!lu_device_bootloader_texas_erase_all (device, error))
		return FALSE;

	/* set existing RAM buffer to 0xff's */
	if (!lu_device_bootloader_texas_clear_ram_buffer (device, 0x0000, error))
		return FALSE;

	/* transfer payload */
	for (guint i = 0; i < reqs->len; i++) {
		payload = g_ptr_array_index (reqs, i);

		/* check size */
		if (payload->len != 16) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "payload size invalid @%04x: got 0x%02x",
				     payload->addr, payload->len);
			return FALSE;
		}

		/* build packet */
		req->cmd = LU_DEVICE_BOOTLOADER_CMD_WRITE_RAM_BUFFER;
		req->addr = payload->addr % 0x80;
		req->len = payload->len;
		memcpy (req->data, payload->data, payload->len);
		if (!lu_device_bootloader_request (device, req, error)) {
			g_prefix_error (error,
					"failed to write ram bufer @0x%02x: ",
					req->addr);
			return FALSE;
		}
		if (req->cmd == LU_DEVICE_BOOTLOADER_CMD_WRITE_RAM_BUFFER_INVALID_ADDR) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "failed to write ram buffer @%04x: invalid location",
				     req->addr);
			return FALSE;
		}
		if (req->cmd == LU_DEVICE_BOOTLOADER_CMD_WRITE_RAM_BUFFER_OVERFLOW) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "failed to write ram buffer @%04x: invalid size 0x%02x",
				     req->addr, req->len);
			return FALSE;
		}

		/* flush RAM buffer to EEPROM */
		if ((payload->addr + 0x10) % 0x80 == 0) {
			guint16 addr_start = payload->addr - (7 * 0x10);
			g_debug ("addr flush @ 0x%04x for 0x%04x",
				 payload->addr, addr_start);
			if (!lu_device_bootloader_texas_flash_ram_buffer (device,
									  addr_start,
									  error)) {
				g_prefix_error (error,
						"failed to flash ram buffer @0x%04x: ",
						addr_start);
				return FALSE;
			}
		}

		/* update progress */
		if (progress_cb != NULL) {
			progress_cb ((goffset) i * 32,
				     (goffset) reqs->len * 32,
				     progress_data);
		}
	}

	/* check CRC */
	if (!lu_device_bootloader_texas_compute_and_test_crc (device, error))
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

static gchar *
lu_device_bootloader_texas_get_bl_version (LuDevice *device, GError **error)
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
lu_device_bootloader_texas_probe (LuDevice *device, GError **error)
{
	g_autofree gchar *version_bl = NULL;
	version_bl = lu_device_bootloader_texas_get_bl_version (device, error);
	if (version_bl == NULL)
		return FALSE;
	lu_device_set_version_bl (device, version_bl);
	lu_device_set_version_fw (device, "RQR24.xx_Bxxxx");
	return TRUE;
}

static void
lu_device_bootloader_texas_class_init (LuDeviceBootloaderTexasClass *klass)
{
	LuDeviceClass *klass_device = LU_DEVICE_CLASS (klass);
	klass_device->write_firmware = lu_device_bootloader_texas_write_firmware;
	klass_device->probe = lu_device_bootloader_texas_probe;
}

static void
lu_device_bootloader_texas_init (LuDeviceBootloaderTexas *device)
{
}
