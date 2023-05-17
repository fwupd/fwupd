/*
 * Copyright (C) 2023 Framework Computer Inc
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

// #include "fu-ccgx-common.h"
#include "fu-ccgx-firmware.h"
#include "fu-ccgx-hpi-common.h"
#include "fu-ccgx-pure-hid-device.h"
#include "fu-ccgx-struct.h"

#define ROW_SIZE	  128
#define FW1_START	  0x0030
#define FW2_START	  0x0200
#define FW1_METADATA	  0x03FF
#define FW2_METADATA	  0x03FE

/**
 * FU_CCGX_PURE_HID_DEVICE_IS_IN_RESTART:
 *
 * Device is in restart and should not be closed manually.
 *
 * Since: 1.9.2
 */
#define FU_CCGX_PURE_HID_DEVICE_IS_IN_RESTART (1 << 0)

struct _FuCcgxPureHidDevice {
	FuHidDevice parent_instance;
	FuCcgxFwMode operating_mode;
	guint32 versions[FU_CCGX_FW_MODE_LAST];
	guint32 silicon_id;
	guint32 flash_row_size; // FIXME: unused?
	guint32 flash_size;	// FIXME: unused?
};
G_DEFINE_TYPE(FuCcgxPureHidDevice, fu_ccgx_pure_hid_device, FU_TYPE_HID_DEVICE)

typedef enum {
	FU_CCGX_PURE_HID_REPORT_ID_INFO = 0xE0,
	FU_CCGX_PURE_HID_REPORT_ID_COMMAND = 0xE1,
	FU_CCGX_PURE_HID_REPORT_ID_WRITE = 0xE2,
	FU_CCGX_PURE_HID_REPORT_ID_READ = 0xE3,
	FU_CCGX_PURE_HID_REPORT_ID_CUSTOM = 0xE4,
} FuCcgxPureHidReportId;

typedef enum {
	CCGX_HID_CMD_JUMP = 0x01,
	CCGX_HID_CMD_FLASH = 0x02,
	CCGX_HID_CMD_SET_BOOT = 0x04,
	CCGX_HID_CMD_MODE = 0x06,
} FuCcgxPureHidDeviceCommand;

#define FU_CCGX_PURE_HID_DEVICE_TIMEOUT	    5000 /* ms */
#define FU_CCGX_PURE_HID_DEVICE_RETRY_DELAY 30	 /* ms */
#define FU_CCGX_PURE_HID_DEVICE_RETRY_CNT   5

static gboolean
fu_ccgx_pure_hid_command(FuCcgxPureHidDevice *self, guint8 param1, guint8 param2, GError **error)
{
	guint8 buf[8] =
	    {FU_CCGX_PURE_HID_REPORT_ID_COMMAND, param1, param2, 0x00, 0xCC, 0xCC, 0xCC, 0xCC};
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      buf[0],
				      buf,
				      sizeof(buf),
				      FU_CCGX_PURE_HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_pure_hid_enter_flashing_mode(FuCcgxPureHidDevice *self, GError **error)
{
	if (!fu_ccgx_pure_hid_command(self,
				      CCGX_HID_CMD_FLASH,
				      FU_CCGX_PD_RESP_ENTER_FLASHING_MODE_CMD_SIG,
				      error)) {
		g_prefix_error(error, "flashing enable command error: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_pure_hid_magic_unlock(FuCcgxPureHidDevice *self, GError **error)
{
	guint8 buf[8] = {FU_CCGX_PURE_HID_REPORT_ID_CUSTOM,
			 FU_CCGX_PD_RESP_BRIDGE_MODE_CMD_SIG,
			 0x43,
			 0x59,
			 0x00,
			 0x00,
			 0x00,
			 0x0B};
	g_autoptr(GError) error_local = NULL;

	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      buf[0],
				      buf,
				      sizeof(buf),
				      FU_CCGX_PURE_HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error)) {
		g_prefix_error(error, "magic enable command error: ");
		return FALSE;
	}

	/* ignore error: this always fails but has the correct behavior */
	if (!fu_ccgx_pure_hid_command(self,
				      CCGX_HID_CMD_MODE,
				      FU_CCGX_PD_RESP_BRIDGE_MODE_CMD_SIG,
				      &error_local)) {
		g_debug("expected HID report bridge mode failure: %s", error_local->message);
	}

	return TRUE;
}

static gboolean
fu_ccgx_pure_hid_ensure_fw_info(FuCcgxPureHidDevice *self, GError **error)
{
	guint8 buf[0x40] = {FU_CCGX_PURE_HID_REPORT_ID_INFO, 0};
	g_autofree gchar *bl_ver = NULL;
	g_autoptr(GByteArray) st_info = NULL;

	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      buf[0],
				      buf,
				      sizeof(buf),
				      FU_CCGX_PURE_HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error))
		return FALSE;
	if (!fu_ccgx_pure_hid_enter_flashing_mode(self, error))
		return FALSE;

	st_info = fu_struct_ccgx_pure_hid_fw_info_parse(buf, sizeof(buf), 0x0, error);
	if (st_info == NULL)
		return FALSE;
	self->silicon_id = fu_struct_ccgx_pure_hid_fw_info_get_silicon_id(st_info);
	self->operating_mode = fu_struct_ccgx_pure_hid_fw_info_get_operating_mode(st_info);

	/* set current version */
	if (self->operating_mode == FU_CCGX_FW_MODE_FW1) {
		guint version = fu_struct_ccgx_pure_hid_fw_info_get_image1_version(st_info);
		fu_device_set_version_from_uint32(FU_DEVICE(self), version);
	} else if (self->operating_mode == FU_CCGX_FW_MODE_FW2) {
		guint version = fu_struct_ccgx_pure_hid_fw_info_get_image2_version(st_info);
		fu_device_set_version_from_uint32(FU_DEVICE(self), version);
	}

	/* set bootloader version */
	fu_device_set_version_bootloader_raw(
	    FU_DEVICE(self),
	    fu_struct_ccgx_pure_hid_fw_info_get_bl_version(st_info));
	bl_ver = fu_version_from_uint32(fu_struct_ccgx_pure_hid_fw_info_get_bl_version(st_info),
					fu_device_get_version_format(self));
	fu_device_set_version_bootloader(FU_DEVICE(self), bl_ver);
	return TRUE;
}

// static gboolean
// fu_ccgx_pure_hid_device_detach(FuDevice *device, FuProgress *progress, GError **error)
//{
//	// Reset already done in fw update function. Takes a few seconds (roughly 4 to 5)
//	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
//	fu_device_add_private_flag(device, FU_CCGX_PURE_HID_DEVICE_IS_IN_RESTART);
//
//	return TRUE;
// }

static gboolean
fu_ccgx_pure_hid_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	fu_device_remove_private_flag(device, FU_CCGX_PURE_HID_DEVICE_IS_IN_RESTART);
	return TRUE;
}

static gboolean
fu_ccgx_pure_hid_device_close(FuDevice *device, GError **error)
{
	/* do not close handle when device restarts */
	if (fu_device_has_private_flag(device, FU_CCGX_PURE_HID_DEVICE_IS_IN_RESTART))
		return TRUE;

	/* FuUsbDevice->close */
	return FU_DEVICE_CLASS(fu_ccgx_pure_hid_device_parent_class)->close(device, error);
}

static gboolean
fu_ccgx_pure_hid_device_setup(FuDevice *device, GError **error)
{
	FuCcgxPureHidDevice *self = FU_CCGX_PURE_HID_DEVICE(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_ccgx_pure_hid_device_parent_class)->setup(device, error))
		return FALSE;

	if (!fu_ccgx_pure_hid_magic_unlock(self, error))
		return FALSE;
	if (!fu_ccgx_pure_hid_ensure_fw_info(self, error))
		return FALSE;

	// TODO: Check if this is set properly
	fu_device_set_logical_id(device, fu_ccgx_fw_mode_to_string(self->operating_mode));
	fu_device_add_instance_strup(device, "MODE", fu_device_get_logical_id(device));
	if (!fu_device_build_instance_id(FU_DEVICE(self), error, "USB", "VID", "PID", "MODE", NULL))
		return FALSE;

	// TODO: Check if this is set properly
	fu_device_add_instance_u16(FU_DEVICE(self), "SID", self->silicon_id);
	if (!fu_device_build_instance_id_quirk(FU_DEVICE(self), error, "CCGX", "SID", NULL))
		return FALSE;

	if (self->operating_mode == FU_CCGX_FW_MODE_BOOT) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		/* force an upgrade to any version */
		fu_device_set_version_from_uint32(FU_DEVICE(self), 0x0);
	} else {
		fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}

	/* ensure the remove delay is set, even if no quirk matched */
	if (fu_device_get_remove_delay(FU_DEVICE(self)) == 0)
		fu_device_set_remove_delay(FU_DEVICE(self), 5000);

	/* success */
	return TRUE;
}

static gboolean
fu_ccgx_pure_hid_write_row(FuCcgxPureHidDevice *self,
			   guint16 address,
			   const guint8 *row,
			   GError **error)
{
	g_autoptr(GByteArray) st_hdr = fu_struct_ccgx_pure_hid_write_hdr_new();

	fu_struct_ccgx_pure_hid_write_hdr_set_pd_resp(st_hdr,
						      FU_CCGX_PD_RESP_FLASH_READ_WRITE_CMD_SIG);
	fu_struct_ccgx_pure_hid_write_hdr_set_addr(st_hdr, address);
	//	buf[0] = FU_CCGX_PURE_HID_REPORT_ID_WRITE;
	if (!fu_memcpy_safe(st_hdr->data,
			    st_hdr->len,
			    FU_STRUCT_CCGX_PURE_HID_WRITE_HDR_OFFSET_DATA,
			    row,
			    ROW_SIZE,
			    0,
			    ROW_SIZE,
			    error))
		return FALSE;
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      st_hdr->data[0],
				      st_hdr->data,
				      st_hdr->len,
				      FU_CCGX_PURE_HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "write row command error: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_pure_hid_flash_firmware_image(FuCcgxPureHidDevice *self,
				      const guint8 *fw_bytes,
				      gsize fw_len,
				      guint16 address_start,
				      guint16 address_metadata,
				      guint8 fw_img_no,
				      GError **error)
{
	gsize rows;

	// TODO: Probably not required
	if (!fu_ccgx_pure_hid_magic_unlock(self, error))
		return FALSE;
	if (!fu_ccgx_pure_hid_ensure_fw_info(self, error))
		return FALSE;

	g_assert(fw_len % ROW_SIZE == 0);
	rows = fw_len / ROW_SIZE;

	for (gsize address = 0; address < rows; address++) {
		const guint8 *row = fw_bytes + address * ROW_SIZE;
		g_debug("writing row %u", (guint)address);
		if (address == rows - 1) {
			g_debug("Writing metadata row");
			// Last row is the metadata row. It's at a different offset
			if (!fu_ccgx_pure_hid_write_row(self, address_metadata, row, error))
				return FALSE;
		} else {
			g_debug("Writing row #%lu 0x%04lx", address, address_start + address);
			if (!fu_ccgx_pure_hid_write_row(self, address_start + address, row, error))
				return FALSE;
		}
	}

	// TODO: Doing this and reset by themselves (with magic unlock) doesn't
	// switch to the alternative image. Seems we always need to flash in
	// order to switch.
	g_debug("before bootswitch");
	if (!fu_ccgx_pure_hid_command(self, CCGX_HID_CMD_SET_BOOT, fw_img_no, error)) {
		g_prefix_error(error, "bootswitch command error: ");
		return FALSE;
	}
	g_debug("After bootswitch");

	g_debug("before reset");
	if (!fu_ccgx_pure_hid_command(self,
				      CCGX_HID_CMD_JUMP,
				      FU_CCGX_PD_RESP_DEVICE_RESET_CMD_SIG,
				      error)) {
		g_prefix_error(error, "reset command error: ");
		return FALSE;
	}
	g_debug("After reset");

	return TRUE;
}

static gboolean
fu_ccgx_pure_hid_device_write_firmware(FuDevice *device,
				       FuFirmware *firmware,
				       FuProgress *progress,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuCcgxPureHidDevice *self = FU_CCGX_PURE_HID_DEVICE(device);
	g_autoptr(GBytes) fw = NULL;
	gsize fw_total_size;
	gsize fw_size;
	const guint8 *fw_bytes;
	const guint8 *fw1_binary;
	const guint8 *fw2_binary;

	/* get blob and read */
	fw = fu_firmware_get_bytes(FU_FIRMWARE(firmware), error);
	if (fw == NULL)
		return FALSE;

	fw_bytes = g_bytes_get_data(fw, NULL);
	fw_total_size = g_bytes_get_size(fw);
	fw_size = fw_total_size / 2;

	// TODO: Do this in prepare_firmware
	// Make sure there are two firmware images composed of full rows
	g_assert(fw_total_size % 2 * ROW_SIZE == 0);
	fw1_binary = fw_bytes;
	fw2_binary = &fw_bytes[fw_size];

	// FIXME: needed?
	if (!fu_ccgx_pure_hid_magic_unlock(self, error))
		return FALSE;
	if (!fu_ccgx_pure_hid_ensure_fw_info(self, error))
		return FALSE;

	g_debug("Operating Mode:  0x%02x", self->operating_mode);
	switch (self->operating_mode) {
	case FU_CCGX_FW_MODE_BOOT:
	case FU_CCGX_FW_MODE_FW2:
		// Update Image 1
		g_debug("Flashing Image 1");
		if (!fu_ccgx_pure_hid_flash_firmware_image(self,
							   fw1_binary,
							   fw_size,
							   FW1_START,
							   FW1_METADATA,
							   1,
							   error))
			return FALSE;

		g_debug("Add wait for replug");
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
		fu_device_add_private_flag(device, FU_CCGX_PURE_HID_DEVICE_IS_IN_RESTART);

		// TODO: Wait 5s
		// TODO: Refresh HID devices
		// fu_ccgx_pure_hid_magic_unlock(device, user_data, error);
		// Update Image 2
		// flash_firmware_image(device, fw2_binary, fw_size, FW2_START, FW2_METADATA, 2);
		break;
	case FU_CCGX_FW_MODE_FW1:
		// Update Image 2
		g_debug("Flashing Image 2");
		if (!fu_ccgx_pure_hid_flash_firmware_image(self,
							   fw2_binary,
							   fw_size,
							   FW2_START,
							   FW2_METADATA,
							   2,
							   error))
			return FALSE;

		g_debug("Add wait for replug");
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
		fu_device_add_private_flag(device, FU_CCGX_PURE_HID_DEVICE_IS_IN_RESTART);

		// TODO: Wait 5s
		// TODO: Refresh HID devices
		// fu_ccgx_pure_hid_magic_unlock(device, user_data, error);
		// Update Image 1
		// flash_firmware_image(device, fw1_binary, fw_size, FW1_START, FW1_METADATA, 1);
		break;
	default:
		return FALSE;
	}

	return TRUE;
}

static void
fu_ccgx_pure_hid_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 25, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 50, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 25, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 50, "reload");
}

static void
fu_ccgx_pure_hid_device_init(FuCcgxPureHidDevice *self)
{
	// FIXME: I don't think this is try anymore
	fu_device_add_protocol(FU_DEVICE(self), "com.infineon.ccgx");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_CCGX_PURE_HID_DEVICE_IS_IN_RESTART,
					"is-in-restart");
}

static void
fu_ccgx_pure_hid_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuCcgxPureHidDevice *self = FU_CCGX_PURE_HID_DEVICE(device);
	fu_string_append_kx(str, idt, "SiliconId", self->silicon_id);
	fu_string_append(str, idt, "FwMode", fu_ccgx_fw_mode_to_string(self->operating_mode));
	if (self->flash_row_size > 0)
		fu_string_append_kx(str, idt, "CcgxFlashRowSize", self->flash_row_size);
	if (self->flash_size > 0)
		fu_string_append_kx(str, idt, "CcgxFlashSize", self->flash_size);
}

static gboolean
fu_ccgx_pure_hid_device_set_quirk_kv(FuDevice *device,
				     const gchar *key,
				     const gchar *value,
				     GError **error)
{
	FuCcgxPureHidDevice *self = FU_CCGX_PURE_HID_DEVICE(device);
	guint64 tmp = 0;

	if (g_strcmp0(key, "SiliconId") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, error))
			return FALSE;
		self->silicon_id = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "CcgxFlashRowSize") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, error))
			return FALSE;
		self->flash_row_size = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "CcgxFlashSize") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, error))
			return FALSE;
		self->flash_size = tmp;
		return TRUE;
	}
	// if (g_strcmp0(key, "CcgxImageKind") == 0) {
	//	self->fw_image_type = fu_ccgx_image_type_from_string(value);
	//	if (self->fw_image_type != FU_CCGX_IMAGE_TYPE_UNKNOWN)
	//		return TRUE;
	//	g_set_error_literal(error,
	//			    G_IO_ERROR,
	//			    G_IO_ERROR_INVALID_DATA,
	//			    "invalid CcgxImageKind");
	//	return FALSE;
	// }
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "no supported");
	return FALSE;
}

static void
fu_ccgx_pure_hid_device_class_init(FuCcgxPureHidDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_ccgx_pure_hid_device_to_string;
	klass_device->attach = fu_ccgx_pure_hid_device_attach;
	// klass_device->detach = fu_ccgx_pure_hid_device_detach;
	klass_device->setup = fu_ccgx_pure_hid_device_setup;
	klass_device->write_firmware = fu_ccgx_pure_hid_device_write_firmware;
	klass_device->set_progress = fu_ccgx_pure_hid_device_set_progress;
	klass_device->set_quirk_kv = fu_ccgx_pure_hid_device_set_quirk_kv;
	klass_device->close = fu_ccgx_pure_hid_device_close;
	// TODO: Check firmware file
	// klass_device->prepare_firmware = fu_ccgx_pure_hid_device_prepare_firmware;
}
