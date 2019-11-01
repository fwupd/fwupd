/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-logitech-hidpp-common.h"
#include "fu-logitech-hidpp-bootloader-texas.h"

struct _FuLogitechHidPpBootloaderTexas
{
	FuLogitechHidPpBootloader	 parent_instance;
};

G_DEFINE_TYPE (FuLogitechHidPpBootloaderTexas, fu_logitech_hidpp_bootloader_texas, FU_TYPE_UNIFYING_BOOTLOADER)

static gboolean
fu_logitech_hidpp_bootloader_texas_erase_all (FuLogitechHidPpBootloader *self, GError **error)
{
	g_autoptr(FuLogitechHidPpBootloaderRequest) req = fu_logitech_hidpp_bootloader_request_new ();
	req->cmd = FU_UNIFYING_BOOTLOADER_CMD_FLASH_RAM;
	req->len = 0x01;	/* magic number */
	req->data[0] = 0x00;	/* magic number */
	if (!fu_logitech_hidpp_bootloader_request (self, req, error)) {
		g_prefix_error (error, "failed to erase all pages: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_texas_compute_and_test_crc (FuLogitechHidPpBootloader *self, GError **error)
{
	g_autoptr(FuLogitechHidPpBootloaderRequest) req = fu_logitech_hidpp_bootloader_request_new ();
	req->cmd = FU_UNIFYING_BOOTLOADER_CMD_FLASH_RAM;
	req->len = 0x01;	/* magic number */
	req->data[0] = 0x03;	/* magic number */
	if (!fu_logitech_hidpp_bootloader_request (self, req, error)) {
		g_prefix_error (error, "failed to compute and test CRC: ");
		return FALSE;
	}
	if (req->cmd == FU_UNIFYING_BOOTLOADER_CMD_FLASH_RAM_WRONG_CRC) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "CRC is incorrect");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_texas_flash_ram_buffer (FuLogitechHidPpBootloader *self, guint16 addr, GError **error)
{
	g_autoptr(FuLogitechHidPpBootloaderRequest) req = fu_logitech_hidpp_bootloader_request_new ();
	req->cmd = FU_UNIFYING_BOOTLOADER_CMD_FLASH_RAM;
	req->addr = addr;
	req->len = 0x01;	/* magic number */
	req->data[0] = 0x01;	/* magic number */
	if (!fu_logitech_hidpp_bootloader_request (self, req, error)) {
		g_prefix_error (error, "failed to flash ram buffer @%04x: ", addr);
		return FALSE;
	}
	if (req->cmd == FU_UNIFYING_BOOTLOADER_CMD_FLASH_RAM_INVALID_ADDR) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to flash ram buffer @%04x: invalid flash page",
			     addr);
		return FALSE;
	}
	if (req->cmd == FU_UNIFYING_BOOTLOADER_CMD_FLASH_RAM_PAGE0_INVALID) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to flash ram buffer @%04x: invalid App JMP vector",
			     addr);
		return FALSE;
	}
	if (req->cmd == FU_UNIFYING_BOOTLOADER_CMD_FLASH_RAM_INVALID_ORDER) {
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
fu_logitech_hidpp_bootloader_texas_clear_ram_buffer (FuLogitechHidPpBootloader *self, GError **error)
{
	g_autoptr(FuLogitechHidPpBootloaderRequest) req = fu_logitech_hidpp_bootloader_request_new ();
	req->cmd = FU_UNIFYING_BOOTLOADER_CMD_FLASH_RAM;
	req->addr = 0x0000;
	req->len = 0x01;	/* magic number */
	req->data[0] = 0x02;	/* magic number */
	if (!fu_logitech_hidpp_bootloader_request (self, req, error)) {
		g_prefix_error (error, "failed to clear ram buffer @%04x: ", req->addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_texas_write_firmware (FuDevice *device,
					     FuFirmware *firmware,
					     FwupdInstallFlags flags,
					     GError **error)
{
	FuLogitechHidPpBootloader *self = FU_UNIFYING_BOOTLOADER (device);
	const FuLogitechHidPpBootloaderRequest *payload;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) reqs = NULL;
	g_autoptr(FuLogitechHidPpBootloaderRequest) req = fu_logitech_hidpp_bootloader_request_new ();

	/* get default image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* transfer payload */
	reqs = fu_logitech_hidpp_bootloader_parse_requests (self, fw, error);
	if (reqs == NULL)
		return FALSE;

	/* erase all flash pages */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_ERASE);
	if (!fu_logitech_hidpp_bootloader_texas_erase_all (self, error))
		return FALSE;

	/* set existing RAM buffer to 0xff's */
	if (!fu_logitech_hidpp_bootloader_texas_clear_ram_buffer (self, error))
		return FALSE;

	/* write to RAM buffer */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
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
		req->cmd = payload->cmd;

		/* signature addresses do not need to fit inside 128 bytes */
		if (req->cmd == FU_UNIFYING_BOOTLOADER_CMD_WRITE_SIGNATURE)
			req->addr = payload->addr;
		else
			req->addr =  payload->addr % 0x80;

		req->len = payload->len;
		memcpy (req->data, payload->data, payload->len);
		if (!fu_logitech_hidpp_bootloader_request (self, req, error)) {
			g_prefix_error (error,
					"failed to write ram buffer @0x%02x: ",
					req->addr);
			return FALSE;
		}
		if (req->cmd == FU_UNIFYING_BOOTLOADER_CMD_WRITE_RAM_BUFFER_INVALID_ADDR) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "failed to write ram buffer @%04x: invalid location",
				     req->addr);
			return FALSE;
		}
		if (req->cmd == FU_UNIFYING_BOOTLOADER_CMD_WRITE_RAM_BUFFER_OVERFLOW) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "failed to write ram buffer @%04x: invalid size 0x%02x",
				     req->addr, req->len);
			return FALSE;
		}

		/* flush RAM buffer to EEPROM */
		if ((payload->addr + 0x10) % 0x80 == 0 &&
		    req->cmd != FU_UNIFYING_BOOTLOADER_CMD_WRITE_SIGNATURE) {
			guint16 addr_start = payload->addr - (7 * 0x10);
			g_debug ("addr flush @ 0x%04x for 0x%04x",
				 payload->addr, addr_start);
			if (!fu_logitech_hidpp_bootloader_texas_flash_ram_buffer (self,
									    addr_start,
									    error)) {
				g_prefix_error (error,
						"failed to flash ram buffer @0x%04x: ",
						addr_start);
				return FALSE;
			}
		}

		/* update progress */
		fu_device_set_progress_full (device, i * 32, reqs->len * 32);
	}

	/* check CRC */
	if (!fu_logitech_hidpp_bootloader_texas_compute_and_test_crc (self, error))
		return FALSE;

	/* mark as complete */
	fu_device_set_progress_full (device, reqs->len * 32, reqs->len * 32);

	/* success! */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_texas_setup (FuLogitechHidPpBootloader *self, GError **error)
{
	fu_device_set_version (FU_DEVICE (self), "RQR24.00_B0000",
			       FWUPD_VERSION_FORMAT_PLAIN);
	return TRUE;
}

static void
fu_logitech_hidpp_bootloader_texas_class_init (FuLogitechHidPpBootloaderTexasClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuLogitechHidPpBootloaderClass *klass_device_bootloader = FU_UNIFYING_BOOTLOADER_CLASS (klass);
	klass_device->write_firmware = fu_logitech_hidpp_bootloader_texas_write_firmware;
	klass_device_bootloader->setup = fu_logitech_hidpp_bootloader_texas_setup;
}

static void
fu_logitech_hidpp_bootloader_texas_init (FuLogitechHidPpBootloaderTexas *self)
{
}
