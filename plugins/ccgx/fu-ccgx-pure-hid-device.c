/*
 * Copyright (C) 2023 Framework Computer Inc
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-ccgx-common.h"
#include "fu-ccgx-firmware.h"
#include "fu-ccgx-hpi-common.h"
#include "fu-ccgx-pure-hid-device.h"

#define ROW_SIZE	  128
#define FW1_START	  0x0030
#define FW2_START	  0x0200
#define FW1_METADATA	  0x03FF
#define FW2_METADATA	  0x03FE
#define WRITE_HEADER_SIZE 4

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
	// guint8 bootloader_info;
	// guint8 bootmode_reason;
	guint32 silicon_id;
	// guint32 image_1_row;
	// guint32 image_2_row;
	// device_uid: // [u8; 6],
	guint32 flash_row_size;
	guint32 flash_size;
};
G_DEFINE_TYPE(FuCcgxPureHidDevice, fu_ccgx_pure_hid_device, FU_TYPE_HID_DEVICE)

typedef struct __attribute__((packed)) {
	guint8 report_id;
	guint8 reserved_1;
	guint16 signature;
	guint8 operating_mode;
	guint8 bootloader_info;
	guint8 bootmode_reason;
	guint8 reserved_2;
	guint32 silicon_id;
	guint32 bl_version;
	guint8 bl_version_reserved[4];
	guint32 image_1_ver;
	guint8 image_1_ver_reserved[4];
	guint32 image_2_ver;
	guint8 image_2_ver_reserved[4];
	guint32 image_1_row;
	guint32 image_2_row;
	guint8 device_uid[6];
	guint8 reserved_3[10];
} HidFwInfo;

#define FU_CCGX_PURE_HID_DEVICE_TIMEOUT	    5000 /* ms */
#define FU_CCGX_PURE_HID_DEVICE_RETRY_DELAY 30	 /* ms */
#define FU_CCGX_PURE_HID_DEVICE_RETRY_CNT   5

static gboolean
fu_ccgx_pure_hid_command(FuDevice *device, guint8 param1, guint8 param2, GError **error)
{
	guint8 buf[8] = {CCGX_HID_COMMAND_E1, param1, param2, 0x00, 0xCC, 0xCC, 0xCC, 0xCC};

	if (!fu_hid_device_set_report(FU_HID_DEVICE(device),
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
fu_ccgx_pure_hid_flashing_mode(FuDevice *device, gpointer user_data, GError **error)
{
	g_debug("before flashing mode");
	if (!fu_ccgx_pure_hid_command(device,
				      CCGX_HID_CMD_FLASH,
				      CY_PD_ENTER_FLASHING_MODE_CMD_SIG,
				      error)) {
		g_prefix_error(error, "flashing enable command error: ");
		return FALSE;
	}
	g_debug("After flashing mode");
	return TRUE;
}

static gboolean
fu_ccgx_pure_hid_magic_unlock(FuDevice *device, gpointer user_data, GError **error)
{
	guint8 magic_buf[8] = {CCGX_HID_CUSTOM, 0x42, 0x43, 0x59, 0x00, 0x00, 0x00, 0x0B};
	g_autoptr(GError) error_local = NULL;
	g_debug("magic unlock");

	if (!fu_hid_device_set_report(FU_HID_DEVICE(device),
				      magic_buf[0],
				      magic_buf,
				      sizeof(magic_buf),
				      FU_CCGX_PURE_HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error)) {
		g_prefix_error(error, "magic enable command error: ");
		return FALSE;
	}

	g_debug("bridge mode");
	if (!fu_ccgx_pure_hid_command(device,
				      CCGX_HID_CMD_MODE,
				      CY_PD_BRIDGE_MODE_CMD_SIG,
				      &error_local)) {
		// Ignoring error. This always fails but has the correct behavior
		g_debug("Set HID report bridge mode failed, which is expected. Error: %s",
			error_local->message);
	}

	return TRUE;
}

static gboolean
fu_ccgx_pure_hid_get_fw_info(FuDevice *device, gpointer user_data, GError **error)
{
	FuCcgxPureHidDevice *self = FU_CCGX_PURE_HID_DEVICE(device);
	guint8 buf[0x40] = {0};
	HidFwInfo *info = NULL;
	g_autofree gchar *bl_ver = NULL;
	g_autofree gchar *ver1 = NULL;
	g_autofree gchar *ver2 = NULL;
	g_debug("get fw info");

	buf[0] = CCGX_HID_INFO_E0;
	if (!fu_hid_device_get_report(FU_HID_DEVICE(device),
				      buf[0],
				      buf,
				      sizeof(buf),
				      FU_CCGX_PURE_HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error))
		return FALSE;

	fu_ccgx_pure_hid_flashing_mode(device, user_data, error);

	info = (HidFwInfo *)&buf[0];

	self->operating_mode = info->operating_mode;
	self->silicon_id = info->silicon_id;
	self->versions[FU_CCGX_FW_MODE_BOOT] = info->bl_version;
	self->versions[FU_CCGX_FW_MODE_FW1] = info->image_1_ver;
	self->versions[FU_CCGX_FW_MODE_FW2] = info->image_2_ver;
	g_debug("Report ID:       0x%02x", info->report_id);
	g_debug("Signature:       0x%02x", info->signature);
	g_debug("Operating Mode:  %s (0x%02x)",
		fu_ccgx_fw_mode_to_string(info->operating_mode),
		info->operating_mode);
	g_debug("Bootloader Info: 0x%02x", info->bootloader_info);
	g_debug("Bootmode Reason: 0x%02x", info->bootmode_reason);
	g_debug("Silicon ID:      0x%08x", info->silicon_id);
	bl_ver = fu_version_from_uint32(info->bl_version, fu_device_get_version_format(self));
	g_debug("BL Version:      %s (0x%08x)", bl_ver, info->bl_version);
	ver1 = fu_version_from_uint32(info->image_1_ver, fu_device_get_version_format(self));
	g_debug("Image 1 Ver:     %s (0x%08x)", ver1, info->image_1_ver);
	ver2 = fu_version_from_uint32(info->image_2_ver, fu_device_get_version_format(self));
	g_debug("Image 2 Ver:     %s (0x%08x)", ver2, info->image_2_ver);
	g_debug("Image 1 Row:     0x%08x", info->image_1_row);
	g_debug("Image 2 Row:     0x%08x", info->image_2_row);
	g_debug("Device UID:      %02X%02X%02X%02X%02X%02X",
		info->device_uid[0],
		info->device_uid[1],
		info->device_uid[2],
		info->device_uid[3],
		info->device_uid[4],
		info->device_uid[5]);

	return TRUE;
}

static gboolean
fu_ccgx_pure_hid_get_fw_info_wrapper(FuDevice *device, gpointer user_data, GError **error)
{
	fu_ccgx_pure_hid_magic_unlock(device, user_data, error);
	return fu_ccgx_pure_hid_get_fw_info(device, user_data, error);
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
	g_debug("Setup Start");

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_ccgx_pure_hid_device_parent_class)->setup(device, error))
		return FALSE;

	if (!fu_ccgx_pure_hid_get_fw_info_wrapper(device, NULL, error)) {
		g_prefix_error(error, "get fw info error: ");
		return FALSE;
	}

	/* set summary */
	// summary = g_strdup_printf("CX%u USB audio device", self->chip_id);
	// fu_device_set_summary(device, summary);

	// TODO: Check if this is set properly
	fu_device_set_logical_id(device, fu_ccgx_fw_mode_to_string(self->operating_mode));
	fu_device_add_instance_str(device, "MODE", fu_device_get_logical_id(device));

	// TODO: Check if this is set properly
	fu_device_add_instance_u16(FU_DEVICE(self), "SID", self->silicon_id);
	fu_device_build_instance_id_quirk(FU_DEVICE(self), NULL, "CCGX", "SID", NULL);

	if (self->operating_mode == FU_CCGX_FW_MODE_BOOT) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	} else {
		fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}

	/* if running in bootloader force an upgrade to any version */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		fu_device_set_version_from_uint32(FU_DEVICE(self), 0x0);
	} else {
		fu_device_set_version_from_uint32(FU_DEVICE(self),
						  self->versions[self->operating_mode]);
	}

	fu_device_set_version_bootloader_raw(FU_DEVICE(self), self->versions[FU_CCGX_FW_MODE_BOOT]);
	fu_device_set_version_bootloader(
	    FU_DEVICE(device),
	    fu_version_from_uint32(self->versions[FU_CCGX_FW_MODE_BOOT],
				   fu_device_get_version_format(self)));

	/* ensure the remove delay is set */
	if (fu_device_get_remove_delay(FU_DEVICE(self)) == 0) {
		fu_device_set_remove_delay(FU_DEVICE(self), 5000);
	}

	g_debug("Setup End");

	/* success */
	return TRUE;
}

static gboolean
fu_ccgx_pure_hid_write_row(FuDevice *device, guint16 row_no, const guint8 *row, GError **error)
{
	guint8 buf[ROW_SIZE + WRITE_HEADER_SIZE];
	buf[0] = CCGX_HID_WRITE_E2;
	buf[1] = CY_PD_FLASH_READ_WRITE_CMD_SIG;
	buf[2] = row_no & 0xFF;
	buf[3] = (row_no >> 8) & 0xFF;
	if (!fu_memcpy_safe(buf, sizeof(buf), WRITE_HEADER_SIZE, row, ROW_SIZE, 0, ROW_SIZE, error))
		return FALSE;
	g_debug("before write row");
	if (!fu_hid_device_set_report(FU_HID_DEVICE(device),
				      buf[0],
				      buf,
				      sizeof(buf),
				      FU_CCGX_PURE_HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "write row command error: ");
		return FALSE;
	}
	g_debug("After write row");

	return TRUE;
}

static gboolean
fu_ccgx_pure_hid_flash_firmware_image(FuDevice *device,
				      const guint8 *fw_bytes,
				      gsize fw_len,
				      guint16 start_row,
				      guint16 metadata_row,
				      guint8 fw_img_no,
				      GError **error)
{
	gsize rows;

	// TODO: Probably not required
	if (!fu_ccgx_pure_hid_get_fw_info_wrapper(device, NULL, error)) {
		g_prefix_error(error, "get fw info error: ");
		return FALSE;
	}

	g_assert(fw_len % ROW_SIZE == 0);
	rows = fw_len / ROW_SIZE;

	for (gsize row_no = 0; row_no < rows; row_no++) {
		const guint8 *row = fw_bytes + row_no * ROW_SIZE;
		if (row_no == 0) {
			g_debug("Writing first row");
		}
		if (row_no == rows - 1) {
			g_debug("Writing metadata row");
			// Last row is the metadata row. It's at a different offset
			if (!fu_ccgx_pure_hid_write_row(device, metadata_row, row, error))
				return FALSE;
		} else {
			g_debug("Writing row #%lu 0x%04lx", row_no, start_row + row_no);
			if (!fu_ccgx_pure_hid_write_row(device, start_row + row_no, row, error))
				return FALSE;
		}
	}

	// TODO: Doing this and reset by themselves (with magic unlock) doesn't
	// switch to the alternative image. Seems we always need to flash in
	// order to switch.
	g_debug("before bootswitch");
	if (!fu_ccgx_pure_hid_command(device, 0x04, fw_img_no, error)) {
		g_prefix_error(error, "bootswitch command error: ");
		return FALSE;
	}
	g_debug("After bootswitch");

	g_debug("before reset");
	if (!fu_ccgx_pure_hid_command(device,
				      CCGX_HID_CMD_JUMP,
				      CY_PD_DEVICE_RESET_CMD_SIG,
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
	g_debug("write firmware");

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

	fu_ccgx_pure_hid_magic_unlock(device, NULL, error);
	if (!fu_ccgx_pure_hid_get_fw_info_wrapper(device, NULL, error)) {
		g_prefix_error(error, "get fw info error: ");
		return FALSE;
	}

	g_debug("Operating Mode:  0x%02x", self->operating_mode);
	switch (self->operating_mode) {
	case FU_CCGX_FW_MODE_BOOT:
	case FU_CCGX_FW_MODE_FW2:
		// Update Image 1
		g_debug("Flashing Image 1");
		if (!fu_ccgx_pure_hid_flash_firmware_image(device,
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
		if (!fu_ccgx_pure_hid_flash_firmware_image(device,
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
		// Invalid value
		// TODO: error = "";
		// g_assert(self->operating_mode < FU_CCGX_FW_MODE_LAST);
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
	fu_device_add_protocol(FU_DEVICE(self), "com.cypress.ccgx");
	fu_device_add_protocol(FU_DEVICE(self), "com.infineon.ccgx");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_INTEL_ME2);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_CCGX_PURE_HID_DEVICE_IS_IN_RESTART,
					"is-in-restart");
}

static void
fu_ccgx_pure_hid_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuCcgxPureHidDevice *self = FU_CCGX_PURE_HID_DEVICE(device);
	fu_string_append_kx(str, idt, "SiliconId", self->silicon_id);
	fu_string_append(str, idt, "FuCcgxFwMode", fu_ccgx_fw_mode_to_string(self->operating_mode));
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
