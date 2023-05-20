/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-logitech-hidpp-bootloader-texas.h"
#include "fu-logitech-hidpp-common.h"
#include "fu-logitech-hidpp-struct.h"

struct _FuLogitechHidppBootloaderTexas {
	FuLogitechHidppBootloader parent_instance;
};

G_DEFINE_TYPE(FuLogitechHidppBootloaderTexas,
	      fu_logitech_hidpp_bootloader_texas,
	      FU_TYPE_LOGITECH_HIDPP_BOOTLOADER)

static gboolean
fu_logitech_hidpp_bootloader_texas_erase_all(FuLogitechHidppBootloader *self, GError **error)
{
	g_autoptr(FuLogitechHidppBootloaderRequest) req =
	    fu_logitech_hidpp_bootloader_request_new();
	req->cmd = FU_LOGITECH_HIDPP_BOOTLOADER_CMD_FLASH_RAM;
	req->len = 0x01;     /* magic number */
	req->data[0] = 0x00; /* magic number */
	if (!fu_logitech_hidpp_bootloader_request(self, req, error)) {
		g_prefix_error(error, "failed to erase all pages: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_texas_compute_and_test_crc(FuLogitechHidppBootloader *self,
							GError **error)
{
	g_autoptr(FuLogitechHidppBootloaderRequest) req =
	    fu_logitech_hidpp_bootloader_request_new();
	req->cmd = FU_LOGITECH_HIDPP_BOOTLOADER_CMD_FLASH_RAM;
	req->len = 0x01;     /* magic number */
	req->data[0] = 0x03; /* magic number */
	if (!fu_logitech_hidpp_bootloader_request(self, req, error)) {
		g_prefix_error(error, "failed to compute and test CRC: ");
		return FALSE;
	}
	if (req->cmd == FU_LOGITECH_HIDPP_BOOTLOADER_CMD_FLASH_RAM_WRONG_CRC) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "CRC is incorrect");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_texas_flash_ram_buffer(FuLogitechHidppBootloader *self,
						    guint16 addr,
						    GError **error)
{
	g_autoptr(FuLogitechHidppBootloaderRequest) req =
	    fu_logitech_hidpp_bootloader_request_new();
	req->cmd = FU_LOGITECH_HIDPP_BOOTLOADER_CMD_FLASH_RAM;
	req->addr = addr;
	req->len = 0x01;     /* magic number */
	req->data[0] = 0x01; /* magic number */
	if (!fu_logitech_hidpp_bootloader_request(self, req, error)) {
		g_prefix_error(error, "failed to flash ram buffer @%04x: ", addr);
		return FALSE;
	}
	if (req->cmd == FU_LOGITECH_HIDPP_BOOTLOADER_CMD_FLASH_RAM_INVALID_ADDR) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "failed to flash ram buffer @%04x: invalid flash page",
			    addr);
		return FALSE;
	}
	if (req->cmd == FU_LOGITECH_HIDPP_BOOTLOADER_CMD_FLASH_RAM_PAGE0_INVALID) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "failed to flash ram buffer @%04x: invalid App JMP vector",
			    addr);
		return FALSE;
	}
	if (req->cmd == FU_LOGITECH_HIDPP_BOOTLOADER_CMD_FLASH_RAM_INVALID_ORDER) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "failed to flash ram buffer @%04x: page flashed before page 0",
			    addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_texas_clear_ram_buffer(FuLogitechHidppBootloader *self, GError **error)
{
	g_autoptr(FuLogitechHidppBootloaderRequest) req =
	    fu_logitech_hidpp_bootloader_request_new();
	req->cmd = FU_LOGITECH_HIDPP_BOOTLOADER_CMD_FLASH_RAM;
	req->addr = 0x0000;
	req->len = 0x01;     /* magic number */
	req->data[0] = 0x02; /* magic number */
	if (!fu_logitech_hidpp_bootloader_request(self, req, error)) {
		g_prefix_error(error, "failed to clear ram buffer @%04x: ", req->addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_texas_write_firmware(FuDevice *device,
						  FuFirmware *firmware,
						  FuProgress *progress,
						  FwupdInstallFlags flags,
						  GError **error)
{
	FuLogitechHidppBootloader *self = FU_LOGITECH_HIDPP_BOOTLOADER(device);
	const FuLogitechHidppBootloaderRequest *payload;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) reqs = NULL;
	g_autoptr(FuLogitechHidppBootloaderRequest) req =
	    fu_logitech_hidpp_bootloader_request_new();

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	if (fu_device_has_private_flag(device, FU_LOGITECH_HIDPP_BOOTLOADER_FLAG_IS_SIGNED)) {
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 3, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 1, "clear");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 18, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 79, NULL);
	} else {
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 11, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 1, "clear");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 75, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 12, NULL);
	}

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* transfer payload */
	reqs = fu_logitech_hidpp_bootloader_parse_requests(self, fw, error);
	if (reqs == NULL)
		return FALSE;

	/* erase all flash pages */
	if (!fu_logitech_hidpp_bootloader_texas_erase_all(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* set existing RAM buffer to 0xff's */
	if (!fu_logitech_hidpp_bootloader_texas_clear_ram_buffer(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write to RAM buffer */
	for (guint i = 0; i < reqs->len; i++) {
		payload = g_ptr_array_index(reqs, i);

		/* check size */
		if (payload->len != 16) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "payload size invalid @%04x: got 0x%02x",
				    payload->addr,
				    payload->len);
			return FALSE;
		}

		/* build packet */
		req->cmd = payload->cmd;

		/* signature addresses do not need to fit inside 128 bytes */
		if (req->cmd == FU_LOGITECH_HIDPP_BOOTLOADER_CMD_WRITE_SIGNATURE)
			req->addr = payload->addr;
		else
			req->addr = payload->addr % 0x80;

		req->len = payload->len;
		if (!fu_memcpy_safe(req->data,
				    req->len,
				    0x0, /* dst */
				    payload->data,
				    payload->len,
				    0x0, /* src */
				    payload->len,
				    error))
			return FALSE;
		if (!fu_logitech_hidpp_bootloader_request(self, req, error)) {
			g_prefix_error(error, "failed to write ram buffer @0x%02x: ", req->addr);
			return FALSE;
		}
		if (req->cmd == FU_LOGITECH_HIDPP_BOOTLOADER_CMD_WRITE_RAM_BUFFER_INVALID_ADDR) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "failed to write ram buffer @%04x: invalid location",
				    req->addr);
			return FALSE;
		}
		if (req->cmd == FU_LOGITECH_HIDPP_BOOTLOADER_CMD_WRITE_RAM_BUFFER_OVERFLOW) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "failed to write ram buffer @%04x: invalid size 0x%02x",
				    req->addr,
				    req->len);
			return FALSE;
		}

		/* flush RAM buffer to EEPROM */
		if ((payload->addr + 0x10) % 0x80 == 0 &&
		    req->cmd != FU_LOGITECH_HIDPP_BOOTLOADER_CMD_WRITE_SIGNATURE) {
			guint16 addr_start = payload->addr - (7 * 0x10);
			g_debug("addr flush @ 0x%04x for 0x%04x", payload->addr, addr_start);
			if (!fu_logitech_hidpp_bootloader_texas_flash_ram_buffer(self,
										 addr_start,
										 error)) {
				g_prefix_error(error,
					       "failed to flash ram buffer @0x%04x: ",
					       addr_start);
				return FALSE;
			}
		}

		/* update progress */
		fu_progress_set_percentage_full(fu_progress_get_child(progress), i + 1, reqs->len);
	}
	fu_progress_step_done(progress);

	/* check CRC */
	if (!fu_logitech_hidpp_bootloader_texas_compute_and_test_crc(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_texas_setup(FuDevice *device, GError **error)
{
	/* FuLogitechHidppBootloader->setup */
	if (!FU_DEVICE_CLASS(fu_logitech_hidpp_bootloader_texas_parent_class)->setup(device, error))
		return FALSE;
	fu_device_set_version(device, "RQR24.00_B0000");
	return TRUE;
}

static void
fu_logitech_hidpp_bootloader_texas_class_init(FuLogitechHidppBootloaderTexasClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->write_firmware = fu_logitech_hidpp_bootloader_texas_write_firmware;
	klass_device->setup = fu_logitech_hidpp_bootloader_texas_setup;
}

static void
fu_logitech_hidpp_bootloader_texas_init(FuLogitechHidppBootloaderTexas *self)
{
}
