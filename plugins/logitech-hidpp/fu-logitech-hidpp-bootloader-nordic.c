/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-logitech-hidpp-bootloader-nordic.h"
#include "fu-logitech-hidpp-common.h"
#include "fu-logitech-hidpp-struct.h"

struct _FuLogitechHidppBootloaderNordic {
	FuLogitechHidppBootloader parent_instance;
};

G_DEFINE_TYPE(FuLogitechHidppBootloaderNordic,
	      fu_logitech_hidpp_bootloader_nordic,
	      FU_TYPE_LOGITECH_HIDPP_BOOTLOADER)

static gchar *
fu_logitech_hidpp_bootloader_nordic_get_hw_platform_id(FuLogitechHidppBootloader *self,
						       GError **error)
{
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_rsp = NULL;
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_req =
	    fu_struct_logitech_hidpp_bootloader_pkt_new();
	fu_struct_logitech_hidpp_bootloader_pkt_set_cmd(
	    st_req,
	    FU_LOGITECH_HIDPP_BOOTLOADER_CMD_GET_HW_PLATFORM_ID);
	st_rsp = fu_logitech_hidpp_bootloader_request(self, st_req, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "failed to get HW ID: ");
		return NULL;
	}
	return fu_strsafe(
	    (const gchar *)fu_struct_logitech_hidpp_bootloader_pkt_get_data(st_rsp, NULL),
	    MIN(fu_struct_logitech_hidpp_bootloader_pkt_get_len(st_rsp),
		FU_STRUCT_LOGITECH_HIDPP_BOOTLOADER_PKT_N_ELEMENTS_DATA));
}

static gchar *
fu_logitech_hidpp_bootloader_nordic_get_fw_version(FuLogitechHidppBootloader *self, GError **error)
{
	const guint8 *buf;
	gsize bufsz = 0;
	guint16 micro = 0;
	guint8 major = 0;
	guint8 minor = 0;
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_rsp = NULL;
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_req =
	    fu_struct_logitech_hidpp_bootloader_pkt_new();

	fu_struct_logitech_hidpp_bootloader_pkt_set_cmd(
	    st_req,
	    FU_LOGITECH_HIDPP_BOOTLOADER_CMD_GET_FW_VERSION);
	st_rsp = fu_logitech_hidpp_bootloader_request(self, st_req, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "failed to get firmware version: ");
		return NULL;
	}

	/* RRRxx.yy_Bzzzz */
	buf = fu_struct_logitech_hidpp_bootloader_pkt_get_data(st_rsp, &bufsz);
	if (!fu_firmware_strparse_uint8_safe((const gchar *)buf, bufsz, 3, &major, error))
		return NULL;
	if (!fu_firmware_strparse_uint8_safe((const gchar *)buf, bufsz, 6, &minor, error))
		return NULL;
	if (!fu_firmware_strparse_uint16_safe((const gchar *)buf, bufsz, 10, &micro, error))
		return NULL;
	return fu_logitech_hidpp_format_version("RQR", major, minor, micro);
}

static gboolean
fu_logitech_hidpp_bootloader_nordic_setup(FuDevice *device, GError **error)
{
	FuLogitechHidppBootloader *self = FU_LOGITECH_HIDPP_BOOTLOADER(device);
	g_autofree gchar *hw_platform_id = NULL;
	g_autofree gchar *version_fw = NULL;
	g_autoptr(GError) error_local = NULL;

	/* FuLogitechHidppBootloader->setup */
	if (!FU_DEVICE_CLASS(fu_logitech_hidpp_bootloader_nordic_parent_class)
		 ->setup(device, error))
		return FALSE;

	/* get MCU */
	hw_platform_id = fu_logitech_hidpp_bootloader_nordic_get_hw_platform_id(self, error);
	if (hw_platform_id == NULL)
		return FALSE;
	g_debug("hw-platform-id=%s", hw_platform_id);

	/* get firmware version, which is not fatal */
	version_fw = fu_logitech_hidpp_bootloader_nordic_get_fw_version(self, &error_local);
	if (version_fw == NULL) {
		g_warning("failed to get firmware version: %s", error_local->message);
		fu_device_set_version(device, "RQR12.00_B0000");
	} else {
		fu_device_set_version(device, version_fw);
	}

	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_nordic_write_signature(FuLogitechHidppBootloader *self,
						    guint16 addr,
						    guint8 len,
						    const guint8 *data,
						    GError **error)
{
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_rsp = NULL;
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_req =
	    fu_struct_logitech_hidpp_bootloader_pkt_new();

	fu_struct_logitech_hidpp_bootloader_pkt_set_cmd(
	    st_req,
	    FU_LOGITECH_HIDPP_BOOTLOADER_CMD_WRITE_RAM_BUFFER);
	fu_struct_logitech_hidpp_bootloader_pkt_set_addr(st_req, addr);
	fu_struct_logitech_hidpp_bootloader_pkt_set_len(st_req, len);
	if (!fu_struct_logitech_hidpp_bootloader_pkt_set_data(st_req, data, len, error))
		return FALSE;
	st_rsp = fu_logitech_hidpp_bootloader_request(self, st_req, error);
	if (st_rsp == NULL) {
		g_prefix_error(error, "failed to write sig @0x%02x: ", addr);
		return FALSE;
	}
	if (fu_struct_logitech_hidpp_bootloader_pkt_get_cmd(st_rsp) ==
	    FU_LOGITECH_HIDPP_BOOTLOADER_CMD_WRITE_RAM_BUFFER_INVALID_ADDR) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to write @%04x: signature is too big",
			    addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_nordic_write(FuLogitechHidppBootloader *self,
					  guint16 addr,
					  guint8 len,
					  const guint8 *data,
					  GError **error)
{
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_rsp = NULL;
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_req =
	    fu_struct_logitech_hidpp_bootloader_pkt_new();

	fu_struct_logitech_hidpp_bootloader_pkt_set_cmd(st_req,
							FU_LOGITECH_HIDPP_BOOTLOADER_CMD_WRITE);
	fu_struct_logitech_hidpp_bootloader_pkt_set_addr(st_req, addr);
	fu_struct_logitech_hidpp_bootloader_pkt_set_len(st_req, len);
	if (!fu_struct_logitech_hidpp_bootloader_pkt_set_data(st_req, data, len, error))
		return FALSE;
	st_rsp = fu_logitech_hidpp_bootloader_request(self, st_req, error);
	if (st_rsp == NULL) {
		g_prefix_error(error, "failed to transfer fw @0x%02x: ", addr);
		return FALSE;
	}
	if (fu_struct_logitech_hidpp_bootloader_pkt_get_cmd(st_rsp) ==
	    FU_LOGITECH_HIDPP_BOOTLOADER_CMD_WRITE_INVALID_ADDR) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to write @%04x: invalid address",
			    addr);
		return FALSE;
	}
	if (fu_struct_logitech_hidpp_bootloader_pkt_get_cmd(st_rsp) ==
	    FU_LOGITECH_HIDPP_BOOTLOADER_CMD_WRITE_VERIFY_FAIL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to write @%04x: failed to verify flash content",
			    addr);
		return FALSE;
	}
	if (fu_struct_logitech_hidpp_bootloader_pkt_get_cmd(st_rsp) ==
	    FU_LOGITECH_HIDPP_BOOTLOADER_CMD_WRITE_NONZERO_START) {
		g_debug("wrote %d bytes at address %04x",
			fu_struct_logitech_hidpp_bootloader_pkt_get_len(st_rsp),
			fu_struct_logitech_hidpp_bootloader_pkt_get_addr(st_rsp));
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to write @%04x: only 1 byte write of 0xff supported",
			    addr);
		return FALSE;
	}
	if (fu_struct_logitech_hidpp_bootloader_pkt_get_cmd(st_rsp) ==
	    FU_LOGITECH_HIDPP_BOOTLOADER_CMD_WRITE_INVALID_CRC) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to write @%04x: invalid CRC",
			    addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_nordic_erase(FuLogitechHidppBootloader *self,
					  guint16 addr,
					  GError **error)
{
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_rsp = NULL;
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_req =
	    fu_struct_logitech_hidpp_bootloader_pkt_new();

	fu_struct_logitech_hidpp_bootloader_pkt_set_cmd(
	    st_req,
	    FU_LOGITECH_HIDPP_BOOTLOADER_CMD_ERASE_PAGE);
	fu_struct_logitech_hidpp_bootloader_pkt_set_addr(st_req, addr);
	fu_struct_logitech_hidpp_bootloader_pkt_set_len(st_req, 0x01);
	st_rsp = fu_logitech_hidpp_bootloader_request(self, st_req, error);
	if (st_rsp == NULL) {
		g_prefix_error(error, "failed to erase fw @0x%02x: ", addr);
		return FALSE;
	}
	if (fu_struct_logitech_hidpp_bootloader_pkt_get_cmd(st_rsp) ==
	    FU_LOGITECH_HIDPP_BOOTLOADER_CMD_ERASE_PAGE_INVALID_ADDR) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to erase @%04x: invalid page",
			    addr);
		return FALSE;
	}
	if (fu_struct_logitech_hidpp_bootloader_pkt_get_cmd(st_rsp) ==
	    FU_LOGITECH_HIDPP_BOOTLOADER_CMD_ERASE_PAGE_NONZERO_START) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to erase @%04x: byte 0x00 is not 0xff",
			    addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_nordic_write_pkt(FuLogitechHidppBootloader *self,
					      FuStructLogitechHidppBootloaderPkt *st_req,
					      GError **error)
{
	if (fu_struct_logitech_hidpp_bootloader_pkt_get_cmd(st_req) ==
	    FU_LOGITECH_HIDPP_BOOTLOADER_CMD_WRITE_SIGNATURE) {
		return fu_logitech_hidpp_bootloader_nordic_write_signature(
		    self,
		    fu_struct_logitech_hidpp_bootloader_pkt_get_addr(st_req),
		    fu_struct_logitech_hidpp_bootloader_pkt_get_len(st_req),
		    fu_struct_logitech_hidpp_bootloader_pkt_get_data(st_req, NULL),
		    error);
	}
	return fu_logitech_hidpp_bootloader_nordic_write(
	    self,
	    fu_struct_logitech_hidpp_bootloader_pkt_get_addr(st_req),
	    fu_struct_logitech_hidpp_bootloader_pkt_get_len(st_req),
	    fu_struct_logitech_hidpp_bootloader_pkt_get_data(st_req, NULL),
	    error);
}

static gboolean
fu_logitech_hidpp_bootloader_nordic_write_pkts(FuLogitechHidppBootloader *self,
					       GPtrArray *pkts,
					       FuProgress *progress,
					       GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, pkts->len - 1);
	for (guint i = 1; i < pkts->len; i++) {
		FuStructLogitechHidppBootloaderPkt *st_req = g_ptr_array_index(pkts, i);
		if (!fu_logitech_hidpp_bootloader_nordic_write_pkt(self, st_req, error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_nordic_write_firmware(FuDevice *device,
						   FuFirmware *firmware,
						   FuProgress *progress,
						   FwupdInstallFlags flags,
						   GError **error)
{
	FuLogitechHidppBootloader *self = FU_LOGITECH_HIDPP_BOOTLOADER(device);
	GPtrArray *records;
	const FuStructLogitechHidppBootloaderPkt *st_req;
	guint16 addr;
	g_autoptr(GPtrArray) pkts = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	if (fu_device_has_private_flag(device, FU_LOGITECH_HIDPP_BOOTLOADER_FLAG_IS_SIGNED)) {
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 4, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 13, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "device-write0");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 82, "reset vector");
	} else {
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 22, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 72, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "device-write0");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 6, "reset-vector");
	}

	/* erase firmware pages up to the bootloader */
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_ERASE);
	for (addr = fu_logitech_hidpp_bootloader_get_addr_lo(self);
	     addr < fu_logitech_hidpp_bootloader_get_addr_hi(self);
	     addr += fu_logitech_hidpp_bootloader_get_blocksize(self)) {
		if (!fu_logitech_hidpp_bootloader_nordic_erase(self, addr, error))
			return FALSE;
	}
	fu_progress_step_done(progress);

	/* transfer payload */
	records = fu_ihex_firmware_get_records(FU_IHEX_FIRMWARE(firmware));
	pkts = fu_logitech_hidpp_bootloader_parse_pkts(self, records, error);
	if (pkts == NULL)
		return FALSE;
	if (!fu_logitech_hidpp_bootloader_nordic_write_pkts(self,
							    pkts,
							    fu_progress_get_child(progress),
							    error))
		return FALSE;
	fu_progress_step_done(progress);

	/* send the first managed packet last, excluding the reset vector */
	st_req = g_ptr_array_index(pkts, 0);
	if (!fu_logitech_hidpp_bootloader_nordic_write(
		self,
		fu_struct_logitech_hidpp_bootloader_pkt_get_addr(st_req) + 1,
		fu_struct_logitech_hidpp_bootloader_pkt_get_len(st_req) - 1,
		fu_struct_logitech_hidpp_bootloader_pkt_get_data(st_req, NULL) + 1,
		error))
		return FALSE;
	fu_progress_step_done(progress);

	/* reset vector */
	if (!fu_logitech_hidpp_bootloader_nordic_write(
		self,
		0x0000,
		0x01,
		fu_struct_logitech_hidpp_bootloader_pkt_get_data(st_req, NULL),
		error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static void
fu_logitech_hidpp_bootloader_nordic_class_init(FuLogitechHidppBootloaderNordicClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_logitech_hidpp_bootloader_nordic_write_firmware;
	device_class->setup = fu_logitech_hidpp_bootloader_nordic_setup;
}

static void
fu_logitech_hidpp_bootloader_nordic_init(FuLogitechHidppBootloaderNordic *self)
{
}
