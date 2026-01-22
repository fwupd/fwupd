/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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
	guint8 buf[] = {FU_STRUCT_LOGITECH_HIDPP_BOOTLOADER_TEXAS_CMD_ERASE_ALL};
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_rsp = NULL;
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_req =
	    fu_struct_logitech_hidpp_bootloader_pkt_new();

	fu_struct_logitech_hidpp_bootloader_pkt_set_cmd(st_req,
							FU_LOGITECH_HIDPP_BOOTLOADER_CMD_FLASH_RAM);
	fu_struct_logitech_hidpp_bootloader_pkt_set_len(st_req, sizeof(buf));
	if (!fu_struct_logitech_hidpp_bootloader_pkt_set_data(st_req, buf, sizeof(buf), error))
		return FALSE;
	st_rsp = fu_logitech_hidpp_bootloader_request(self, st_req, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "failed to erase all pages: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_texas_compute_and_test_crc(FuLogitechHidppBootloader *self,
							GError **error)
{
	guint8 buf[] = {FU_STRUCT_LOGITECH_HIDPP_BOOTLOADER_TEXAS_CMD_COMPUTE_CRC};
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_rsp = NULL;
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_req =
	    fu_struct_logitech_hidpp_bootloader_pkt_new();

	fu_struct_logitech_hidpp_bootloader_pkt_set_cmd(st_req,
							FU_LOGITECH_HIDPP_BOOTLOADER_CMD_FLASH_RAM);
	fu_struct_logitech_hidpp_bootloader_pkt_set_len(st_req, sizeof(buf));
	if (!fu_struct_logitech_hidpp_bootloader_pkt_set_data(st_req, buf, sizeof(buf), error))
		return FALSE;
	st_rsp = fu_logitech_hidpp_bootloader_request(self, st_req, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "failed to compute and test CRC: ");
		return FALSE;
	}
	if (fu_struct_logitech_hidpp_bootloader_pkt_get_cmd(st_rsp) ==
	    FU_LOGITECH_HIDPP_BOOTLOADER_CMD_FLASH_RAM_WRONG_CRC) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "CRC is incorrect");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_texas_flash_ram_buffer(FuLogitechHidppBootloader *self,
						    guint16 addr,
						    GError **error)
{
	guint8 buf[] = {FU_STRUCT_LOGITECH_HIDPP_BOOTLOADER_TEXAS_CMD_FLASH_RAM_BUFFER};
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_rsp = NULL;
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_req =
	    fu_struct_logitech_hidpp_bootloader_pkt_new();

	fu_struct_logitech_hidpp_bootloader_pkt_set_cmd(st_req,
							FU_LOGITECH_HIDPP_BOOTLOADER_CMD_FLASH_RAM);
	fu_struct_logitech_hidpp_bootloader_pkt_set_addr(st_req, addr);
	fu_struct_logitech_hidpp_bootloader_pkt_set_len(st_req, sizeof(buf));
	if (!fu_struct_logitech_hidpp_bootloader_pkt_set_data(st_req, buf, sizeof(buf), error))
		return FALSE;
	st_rsp = fu_logitech_hidpp_bootloader_request(self, st_req, error);
	if (st_rsp == NULL) {
		g_prefix_error(error, "failed to flash ram buffer @%04x: ", addr);
		return FALSE;
	}
	if (fu_struct_logitech_hidpp_bootloader_pkt_get_cmd(st_rsp) ==
	    FU_LOGITECH_HIDPP_BOOTLOADER_CMD_FLASH_RAM_INVALID_ADDR) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to flash ram buffer @%04x: invalid flash page",
			    addr);
		return FALSE;
	}
	if (fu_struct_logitech_hidpp_bootloader_pkt_get_cmd(st_rsp) ==
	    FU_LOGITECH_HIDPP_BOOTLOADER_CMD_FLASH_RAM_PAGE0_INVALID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to flash ram buffer @%04x: invalid App JMP vector",
			    addr);
		return FALSE;
	}
	if (fu_struct_logitech_hidpp_bootloader_pkt_get_cmd(st_rsp) ==
	    FU_LOGITECH_HIDPP_BOOTLOADER_CMD_FLASH_RAM_INVALID_ORDER) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to flash ram buffer @%04x: page flashed before page 0",
			    addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_texas_clear_ram_buffer(FuLogitechHidppBootloader *self, GError **error)
{
	guint8 buf[] = {FU_STRUCT_LOGITECH_HIDPP_BOOTLOADER_TEXAS_CMD_CLEAR_RAM_BUFFER};
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_rsp = NULL;
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_req =
	    fu_struct_logitech_hidpp_bootloader_pkt_new();

	fu_struct_logitech_hidpp_bootloader_pkt_set_cmd(st_req,
							FU_LOGITECH_HIDPP_BOOTLOADER_CMD_FLASH_RAM);
	fu_struct_logitech_hidpp_bootloader_pkt_set_len(st_req, sizeof(buf));
	if (!fu_struct_logitech_hidpp_bootloader_pkt_set_data(st_req, buf, sizeof(buf), error))
		return FALSE;
	st_rsp = fu_logitech_hidpp_bootloader_request(self, st_req, error);
	if (st_rsp == NULL) {
		g_prefix_error(error,
			       "failed to clear ram buffer @%04x: ",
			       fu_struct_logitech_hidpp_bootloader_pkt_get_addr(st_req));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_texas_write_pkt(FuLogitechHidppBootloader *self,
					     FuStructLogitechHidppBootloaderPkt *st_req,
					     GError **error)
{
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_rsp = NULL;
	guint16 addr_old = fu_struct_logitech_hidpp_bootloader_pkt_get_addr(st_req);

	if (fu_struct_logitech_hidpp_bootloader_pkt_get_cmd(st_req) !=
	    FU_LOGITECH_HIDPP_BOOTLOADER_CMD_WRITE_SIGNATURE) {
		fu_struct_logitech_hidpp_bootloader_pkt_set_addr(st_req, addr_old % 0x80);
	}
	st_rsp = fu_logitech_hidpp_bootloader_request(self, st_req, error);
	if (st_rsp == NULL) {
		g_prefix_error(error, "failed to write ram buffer @0x%02x: ", addr_old);
		return FALSE;
	}
	if (fu_struct_logitech_hidpp_bootloader_pkt_get_cmd(st_rsp) ==
	    FU_LOGITECH_HIDPP_BOOTLOADER_CMD_WRITE_RAM_BUFFER_INVALID_ADDR) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to write ram buffer @%04x: invalid location",
			    fu_struct_logitech_hidpp_bootloader_pkt_get_addr(st_rsp));
		return FALSE;
	}
	if (fu_struct_logitech_hidpp_bootloader_pkt_get_cmd(st_rsp) ==
	    FU_LOGITECH_HIDPP_BOOTLOADER_CMD_WRITE_RAM_BUFFER_OVERFLOW) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to write ram buffer @%04x: invalid size 0x%02x",
			    fu_struct_logitech_hidpp_bootloader_pkt_get_addr(st_rsp),
			    fu_struct_logitech_hidpp_bootloader_pkt_get_len(st_rsp));
		return FALSE;
	}

	/* flush RAM buffer to EEPROM */
	if ((addr_old + 0x10) % 0x80 == 0 &&
	    fu_struct_logitech_hidpp_bootloader_pkt_get_cmd(st_rsp) !=
		FU_LOGITECH_HIDPP_BOOTLOADER_CMD_WRITE_SIGNATURE) {
		guint16 addr_start = addr_old - (7 * 0x10);
		g_debug("addr flush @ 0x%04x for 0x%04x", addr_old, addr_start);
		if (!fu_logitech_hidpp_bootloader_texas_flash_ram_buffer(self, addr_start, error)) {
			g_prefix_error(error, "failed to flash ram buffer @0x%04x: ", addr_start);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_texas_write_pkts(FuLogitechHidppBootloader *self,
					      GPtrArray *pkts,
					      FuProgress *progress,
					      GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, pkts->len);
	for (guint i = 0; i < pkts->len; i++) {
		FuStructLogitechHidppBootloaderPkt *st_req = g_ptr_array_index(pkts, i);
		if (!fu_logitech_hidpp_bootloader_texas_write_pkt(self, st_req, error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
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
	GPtrArray *records;
	g_autoptr(GPtrArray) pkts = NULL;

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

	/* transfer payload */
	records = fu_ihex_firmware_get_records(FU_IHEX_FIRMWARE(firmware));
	pkts = fu_logitech_hidpp_bootloader_parse_pkts(self, records, error);
	if (pkts == NULL)
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
	if (!fu_logitech_hidpp_bootloader_texas_write_pkts(self,
							   pkts,
							   fu_progress_get_child(progress),
							   error))
		return FALSE;
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
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_logitech_hidpp_bootloader_texas_write_firmware;
	device_class->setup = fu_logitech_hidpp_bootloader_texas_setup;
}

static void
fu_logitech_hidpp_bootloader_texas_init(FuLogitechHidppBootloaderTexas *self)
{
}
