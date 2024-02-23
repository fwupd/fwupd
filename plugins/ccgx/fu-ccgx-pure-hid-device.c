/*
 * Copyright (C) 2023 Framework Computer Inc
 * Copyright (C) 2024 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-ccgx-firmware.h"
#include "fu-ccgx-hid-device.h"
#include "fu-ccgx-hpi-common.h"
#include "fu-ccgx-pure-hid-device.h"
#include "fu-ccgx-pure-hid-struct.h"

#define DEFAULT_ROW_SIZE 0x80

struct _FuCcgxPureHidDevice {
	FuHidDevice parent_instance;
	FuCcgxPureHidFwMode operating_mode;
	guint32 silicon_id;
	gsize flash_row_size;
};
G_DEFINE_TYPE(FuCcgxPureHidDevice, fu_ccgx_pure_hid_device, FU_TYPE_HID_DEVICE)

#define FU_CCGX_PURE_HID_DEVICE_TIMEOUT 5000 /* ms */

static gboolean
fu_ccgx_pure_hid_command(FuCcgxPureHidDevice *self, guint8 param1, guint8 param2, GError **error)
{
	g_autoptr(GByteArray) cmd = fu_struct_ccgx_pure_hid_command_new();
	fu_struct_ccgx_pure_hid_command_set_cmd(cmd, param1);
	fu_struct_ccgx_pure_hid_command_set_opt(cmd, param2);
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      FU_CCGX_PURE_HID_REPORT_ID_COMMAND,
				      cmd->data,
				      cmd->len,
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
				      FU_CCGX_PURE_HID_COMMAND_FLASH,
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
				      FU_CCGX_PURE_HID_COMMAND_MODE,
				      FU_CCGX_PD_RESP_BRIDGE_MODE_CMD_SIG,
				      &error_local)) {
		g_debug("expected HID report bridge mode failure: %s", error_local->message);
	}

	return TRUE;
}

static gboolean
fu_ccgx_pure_hid_ensure_fw_info(FuCcgxPureHidDevice *self, GError **error)
{
	FuDevice *device = FU_DEVICE(self);
	guint8 buf[0x40] = {FU_CCGX_PURE_HID_REPORT_ID_INFO, 0};
	guint version = 0;
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

	st_info = fu_struct_ccgx_pure_hid_fw_info_parse(buf, sizeof(buf), 0x0, error);
	if (st_info == NULL)
		return FALSE;
	self->silicon_id = fu_struct_ccgx_pure_hid_fw_info_get_silicon_id(st_info);
	self->operating_mode = fu_struct_ccgx_pure_hid_fw_info_get_operating_mode(st_info);

	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	/* set current version */
	switch (self->operating_mode) {
	case FU_CCGX_PURE_HID_FW_MODE_FW1:
		version = fu_struct_ccgx_pure_hid_fw_info_get_image1_version(st_info);
		break;
	case FU_CCGX_PURE_HID_FW_MODE_FW2:
		version = fu_struct_ccgx_pure_hid_fw_info_get_image2_version(st_info);
		break;
	case FU_CCGX_PURE_HID_FW_MODE_BOOT:
		/* force an upgrade to any version */
		version = 0x0;
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		break;
	default:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "unsupported mode");
		return FALSE;
	}
	fu_device_set_version_raw(device, version);

	/* set bootloader version */
	fu_device_set_version_bootloader_raw(
	    device,
	    fu_struct_ccgx_pure_hid_fw_info_get_bl_version(st_info));
	bl_ver = fu_version_from_uint32(fu_struct_ccgx_pure_hid_fw_info_get_bl_version(st_info),
					fu_device_get_version_format(self));
	fu_device_set_version_bootloader(FU_DEVICE(self), bl_ver);

	/* FIXME: d4s: do we need that for querying??? */
	if (!fu_ccgx_pure_hid_enter_flashing_mode(self, error))
		return FALSE;

	/* TODO: d4s: from wireshark for ReadVersion.exe:
	 * > e1 04 01 00 cc cc cc cc
	 * > e4 42 41 41 00 00 00 18
	 */

	return TRUE;
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

	fu_device_add_instance_strup(device,
				     "MODE",
				     fu_ccgx_pure_hid_fw_mode_to_string(self->operating_mode));
	if (!fu_device_build_instance_id(FU_DEVICE(self), error, "USB", "VID", "PID", "MODE", NULL))
		return FALSE;

	fu_device_add_instance_u16(FU_DEVICE(self), "SID", self->silicon_id);
	if (!fu_device_build_instance_id_full(FU_DEVICE(self),
					      FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					      error,
					      "CCGX",
					      "SID",
					      NULL))
		return FALSE;
	g_debug("got silicon ID: 0x%04x", self->silicon_id);

	/* ensure the remove delay is set, even if no quirk matched */
	if (fu_device_get_remove_delay(FU_DEVICE(self)) == 0)
		fu_device_set_remove_delay(FU_DEVICE(self), 5000);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_ccgx_pure_hid_device_prepare_firmware(FuDevice *device,
					 GInputStream *stream,
					 FwupdInstallFlags flags,
					 GError **error)
{
	FuCcgxPureHidDevice *self = FU_CCGX_PURE_HID_DEVICE(device);
	FuCcgxFwMode fw_mode;
	GPtrArray *records = NULL;
	guint16 fw_silicon_id;
	g_autoptr(FuFirmware) firmware = fu_ccgx_firmware_new();
	gsize fw_size = 0;

	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	/* check the silicon ID */
	fw_silicon_id = fu_ccgx_firmware_get_silicon_id(FU_CCGX_FIRMWARE(firmware));
	if (fw_silicon_id != self->silicon_id) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "silicon id mismatch, expected 0x%x, got 0x%x",
			    self->silicon_id,
			    fw_silicon_id);
		return NULL;
	}

	fw_mode = fu_ccgx_firmware_get_fw_mode(FU_CCGX_FIRMWARE(firmware));
	if (fw_mode != fu_ccgx_fw_mode_get_alternate((FuCcgxFwMode)self->operating_mode)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "FuCcgxFwMode mismatch, expected %s, got %s",
			    fu_ccgx_fw_mode_to_string(
				fu_ccgx_fw_mode_get_alternate((FuCcgxFwMode)self->operating_mode)),
			    fu_ccgx_fw_mode_to_string(fw_mode));
		return NULL;
	}

	/* validate all records has proper size */
	records = fu_ccgx_firmware_get_records(FU_CCGX_FIRMWARE(firmware));
	g_debug("records found: %u", records->len);
	for (guint i = 0; i < records->len; i++) {
		FuCcgxFirmwareRecord *record = g_ptr_array_index(records, i);
		gsize record_size = g_bytes_get_size(record->data);
		if (record_size != self->flash_row_size) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "expected block length %lu, got %lu: array id=0x%02x, "
				    "row=0x%04x (:%02x%04x%04lx)",
				    self->flash_row_size,
				    record_size,
				    record->array_id,
				    record->row_number,
				    record->array_id,
				    record->row_number,
				    record_size);
			return NULL;
		}
		fw_size += record_size;
	}
	g_debug("firmware size: %lu", fw_size);

	return g_steal_pointer(&firmware);
}

static gboolean
fu_ccgx_pure_hid_write_row(FuCcgxPureHidDevice *self,
			   guint16 address,
			   const guint8 *row,
			   gsize row_len,
			   GError **error)
{
	g_autoptr(GByteArray) st_hdr = fu_struct_ccgx_pure_hid_write_hdr_new();

	fu_struct_ccgx_pure_hid_write_hdr_set_pd_resp(st_hdr,
						      FU_CCGX_PD_RESP_FLASH_READ_WRITE_CMD_SIG);
	fu_struct_ccgx_pure_hid_write_hdr_set_addr(st_hdr, address);
	if (!fu_memcpy_safe(st_hdr->data,
			    st_hdr->len,
			    FU_STRUCT_CCGX_PURE_HID_WRITE_HDR_OFFSET_DATA,
			    row,
			    row_len,
			    0,
			    self->flash_row_size,
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
fu_ccgx_pure_hid_device_write_firmware(FuDevice *device,
				       FuFirmware *firmware,
				       FuProgress *progress,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuCcgxPureHidDevice *self = FU_CCGX_PURE_HID_DEVICE(device);
	guint8 fw_mode = 1;
	GPtrArray *records = fu_ccgx_firmware_get_records(FU_CCGX_FIRMWARE(firmware));

	g_debug("operating mode: 0x%02x", self->operating_mode);

	if (self->operating_mode != FU_CCGX_PURE_HID_FW_MODE_FW2) {
		fw_mode = 2;
	}
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, records->len);

	for (guint i = 0; i < records->len; i++) {
		FuCcgxFirmwareRecord *record = g_ptr_array_index(records, i);
		g_debug("writing row 0x%x 0x%04lx",
			record->row_number,
			g_bytes_get_size(record->data));
		if (!fu_ccgx_pure_hid_write_row(self,
						record->row_number,
						g_bytes_get_data(record->data, NULL),
						g_bytes_get_size(record->data),
						error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	g_debug("bootswitch");
	if (!fu_ccgx_pure_hid_command(self, FU_CCGX_PURE_HID_COMMAND_SET_BOOT, fw_mode, error)) {
		g_prefix_error(error, "bootswitch command error: ");
		return FALSE;
	}

	g_debug("reset");
	if (!fu_ccgx_pure_hid_command(self,
				      FU_CCGX_PURE_HID_COMMAND_JUMP,
				      FU_CCGX_PD_RESP_DEVICE_RESET_CMD_SIG,
				      error)) {
		g_prefix_error(error, "reset command error: ");
		return FALSE;
	}

	g_debug("wait for replug");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	return TRUE;
}

static void
fu_ccgx_pure_hid_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_ccgx_pure_hid_device_init(FuCcgxPureHidDevice *self)
{
	self->flash_row_size = DEFAULT_ROW_SIZE;
	fu_device_add_protocol(FU_DEVICE(self), "com.infineon.ccgx");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_INTEL_ME2);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_ONLY_WAIT_FOR_REPLUG);
}

static void
fu_ccgx_pure_hid_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuCcgxPureHidDevice *self = FU_CCGX_PURE_HID_DEVICE(device);
	fu_string_append_kx(str, idt, "SiliconId", self->silicon_id);
	fu_string_append(str,
			 idt,
			 "FwMode",
			 fu_ccgx_pure_hid_fw_mode_to_string(self->operating_mode));
	if (self->flash_row_size > 0)
		fu_string_append_kx(str, idt, "CcgxFlashRowSize", self->flash_row_size);
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

	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "not supported");
	return FALSE;
}

static gchar *
fu_ccgx_pure_hid_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32((guint32)version_raw, fu_device_get_version_format(device));
}

static void
fu_ccgx_pure_hid_device_class_init(FuCcgxPureHidDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_ccgx_pure_hid_device_to_string;
	klass_device->setup = fu_ccgx_pure_hid_device_setup;
	klass_device->write_firmware = fu_ccgx_pure_hid_device_write_firmware;
	klass_device->set_progress = fu_ccgx_pure_hid_device_set_progress;
	klass_device->set_quirk_kv = fu_ccgx_pure_hid_device_set_quirk_kv;
	klass_device->convert_version = fu_ccgx_pure_hid_device_convert_version;
	klass_device->prepare_firmware = fu_ccgx_pure_hid_device_prepare_firmware;
}
