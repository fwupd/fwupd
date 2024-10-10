/*
 * Copyright 2024 Mario Limonciello <superm1@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-asus-hid-device.h"
#include "fu-asus-hid-firmware.h"
#include "fu-asus-hid-struct.h"

struct _FuAsusHidDevice {
	FuHidDevice parent_instance;
	guint8 num_mcu;
};

G_DEFINE_TYPE(FuAsusHidDevice, fu_asus_hid_device, FU_TYPE_HID_DEVICE)

#define FU_ASUS_HID_DEVICE_TIMEOUT 200 /* ms */

static gboolean
fu_asus_hid_device_transfer_feature(FuDevice *device,
				    GByteArray *req,
				    GByteArray *res,
				    guint8 report,
				    GError **error)
{
	FuHidDevice *hid_dev;

	/* can be called from child or proxy */
	if (FU_IS_HID_DEVICE(device))
		hid_dev = FU_HID_DEVICE(device);
	else
		hid_dev = FU_HID_DEVICE(fu_device_get_proxy(device));

	if (req != NULL) {
		if (!fu_hid_device_set_report(hid_dev,
					      report,
					      req->data,
					      req->len,
					      FU_ASUS_HID_DEVICE_TIMEOUT,
					      FU_HID_DEVICE_FLAG_IS_FEATURE,
					      error)) {
			g_prefix_error(error, "failed to send packet: ");
			return FALSE;
		}
	}
	if (res != NULL) {
		if (!fu_hid_device_get_report(hid_dev,
					      report,
					      res->data,
					      res->len,
					      FU_ASUS_HID_DEVICE_TIMEOUT,
					      FU_HID_DEVICE_FLAG_IS_FEATURE,
					      error)) {
			g_prefix_error(error, "failed to receive packet: ");
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_asus_hid_device_init_seq(FuDevice *device, GError **error)
{
	g_autoptr(GByteArray) cmd = fu_struct_asus_hid_command_new();

	fu_struct_asus_hid_command_set_cmd(cmd, FU_ASUS_HID_COMMAND_INIT_SEQUENCE);

	if (!fu_asus_hid_device_transfer_feature(device,
						 cmd,
						 NULL,
						 FU_ASUS_HID_REPORT_ID_INFO,
						 error)) {
		g_prefix_error(error, "failed to initialize device: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_asus_hid_device_ensure_manufacturer(FuDevice *device, GError **error)
{
	const gchar *man;
	g_autoptr(GByteArray) cmd = fu_struct_asus_man_command_new();
	g_autoptr(GByteArray) result = fu_struct_asus_man_result_new();

	if (!fu_asus_hid_device_transfer_feature(device,
						 cmd,
						 result,
						 FU_ASUS_HID_REPORT_ID_INFO,
						 error))
		return FALSE;

	man = fu_struct_asus_man_result_get_data(result);
	if (g_strcmp0(man, "ASUSTech.Inc.") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "manufacturer not supported");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_asus_hid_device_ensure_version(FuDevice *device, guint mcu, GError **error)
{
	g_autoptr(GByteArray) cmd = fu_struct_asus_hid_command_new();
	g_autoptr(GByteArray) result = fu_struct_asus_hid_fw_info_new();
	g_autofree gchar *name = NULL;

	if (mcu == 0)
		fu_struct_asus_hid_command_set_cmd(cmd, FU_ASUS_HID_COMMAND_FW_VERSION);
	else if (mcu == 1)
		fu_struct_asus_hid_command_set_cmd(cmd, FU_ASUS_HID_COMMAND_MAIN_FW_VERSION);
	else {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "mcu not supported");
		return FALSE;
	}

	fu_struct_asus_hid_command_set_length(cmd, FU_STRUCT_ASUS_HID_RESULT_SIZE);

	if (!fu_asus_hid_device_transfer_feature(device,
						 cmd,
						 result,
						 FU_ASUS_HID_REPORT_ID_INFO,
						 error))
		return FALSE;

	fu_device_add_instance_strsafe(device,
				       "PART",
				       fu_struct_asus_hid_desc_get_product(
					   fu_struct_asus_hid_fw_info_get_description(result)));
	fu_device_build_instance_id(device, NULL, "USB", "VID", "PID", "PART", NULL);

	fu_device_set_version(device,
			      fu_struct_asus_hid_desc_get_version(
				  fu_struct_asus_hid_fw_info_get_description(result)));

	name = g_strdup_printf("Microcontroller %u", mcu);
	fu_device_set_name(device, name);

	fu_device_set_logical_id(device,
				 fu_struct_asus_hid_desc_get_product(
				     fu_struct_asus_hid_fw_info_get_description(result)));

	return TRUE;
}

static gboolean
fu_asus_hid_device_probe(FuDevice *device, GError **error)
{
	if (fu_device_has_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		fu_hid_device_set_interface(FU_HID_DEVICE(device), 0);
	else
		fu_hid_device_set_interface(FU_HID_DEVICE(device), 2);

	return TRUE;
}

static gboolean
fu_asus_hid_device_setup(FuDevice *device, GError **error)
{
	FuAsusHidDevice *self = FU_ASUS_HID_DEVICE(device);

	/* HidDevice->setup */
	if (!FU_DEVICE_CLASS(fu_asus_hid_device_parent_class)->setup(device, error))
		return FALSE;

	if (!fu_asus_hid_device_init_seq(device, error))
		return FALSE;

	for (guint i = 0; i < self->num_mcu; i++) {
		g_autoptr(FuDevice) dev_tmp = fu_device_new(NULL);

		fu_device_set_version_format(dev_tmp, FWUPD_VERSION_FORMAT_PLAIN);
		fu_device_set_proxy(dev_tmp, device);
		if (!fu_asus_hid_device_ensure_manufacturer(dev_tmp, error))
			return FALSE;
		if (!fu_asus_hid_device_ensure_version(dev_tmp, i, error))
			return FALSE;
		fu_device_add_flag(dev_tmp, FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_device_add_protocol(dev_tmp, "com.asus.hid");
		fu_device_add_child(device, dev_tmp);
	}

	/* success */
	return TRUE;
}

static FuFirmware *
fu_asus_hid_device_prepare_firmware(FuDevice *device,
				    GInputStream *stream,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_asus_hid_firmware_new();
	const gchar *fw_pn;
	const gchar *logical;

	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	fw_pn = fu_asus_hid_firmware_get_product(firmware);
	logical = fu_device_get_logical_id(device);
	if (g_strcmp0(fw_pn, logical) != 0) {
		if ((flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "firmware for %s does not match %s",
				    fw_pn,
				    logical);
			return NULL;
		}
		g_warning("firmware for %s does not match %s but is being force installed anyway",
			  fw_pn,
			  logical);
	}

	return g_steal_pointer(&firmware);
}

static gboolean
fu_asus_hid_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(GByteArray) cmd = fu_struct_asus_flash_reset_new();

	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	if (!fu_asus_hid_device_transfer_feature(device,
						 cmd,
						 NULL,
						 FU_ASUS_HID_REPORT_ID_FLASHING,
						 error)) {
		g_prefix_error(error, "failed to reset device: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_asus_hid_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(GByteArray) cmd = fu_struct_asus_pre_update_command_new();
	g_autoptr(GByteArray) result = fu_struct_asus_hid_result_new();
	guint32 previous_result;

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	fu_struct_asus_hid_command_set_cmd(cmd, FU_ASUS_HID_COMMAND_PRE_UPDATE);
	fu_struct_asus_hid_command_set_length(cmd, FU_STRUCT_ASUS_HID_RESULT_SIZE);
	if (!fu_asus_hid_device_transfer_feature(device,
						 cmd,
						 result,
						 FU_ASUS_HID_REPORT_ID_INFO,
						 error))
		return FALSE;

	// TODO save some bits from result here for data for next command
	fu_struct_asus_hid_command_set_cmd(cmd, FU_ASUS_HID_COMMAND_PRE_UPDATE2);
	fu_struct_asus_hid_command_set_length(cmd, 1);
	if (!fu_asus_hid_device_transfer_feature(device,
						 cmd,
						 result,
						 FU_ASUS_HID_REPORT_ID_INFO,
						 error))
		return FALSE;

	// TODO save some bits from result here for data for next command
	previous_result = 0x1;
	fu_struct_asus_hid_command_set_cmd(cmd, FU_ASUS_HID_COMMAND_PRE_UPDATE3);
	fu_struct_asus_hid_command_set_length(cmd, 1);
	if (!fu_struct_asus_pre_update_command_set_data(cmd,
							(guint8 *)&previous_result,
							sizeof(previous_result),
							error))
		return FALSE;
	if (!fu_asus_hid_device_transfer_feature(device,
						 cmd,
						 NULL,
						 FU_ASUS_HID_REPORT_ID_INFO,
						 error))
		return FALSE;

	previous_result = 0x0;
	fu_struct_asus_hid_command_set_cmd(cmd, FU_ASUS_HID_COMMAND_PRE_UPDATE4);
	fu_struct_asus_hid_command_set_length(cmd, FU_STRUCT_ASUS_HID_RESULT_SIZE);
	if (!fu_struct_asus_pre_update_command_set_data(cmd,
							(guint8 *)&previous_result,
							sizeof(previous_result),
							error))
		return FALSE;
	if (!fu_asus_hid_device_transfer_feature(device,
						 cmd,
						 result,
						 FU_ASUS_HID_REPORT_ID_INFO,
						 error))
		return FALSE;

	// TODO save some bits from result here for data for next command

	previous_result = 0x2;
	fu_struct_asus_hid_command_set_cmd(cmd, FU_ASUS_HID_COMMAND_PRE_UPDATE5);
	fu_struct_asus_hid_command_set_length(cmd, 0x01);
	if (!fu_struct_asus_pre_update_command_set_data(cmd,
							(guint8 *)&previous_result,
							sizeof(previous_result),
							error))
		return FALSE;
	if (!fu_asus_hid_device_transfer_feature(device,
						 cmd,
						 NULL,
						 FU_ASUS_HID_REPORT_ID_INFO,
						 error))
		return FALSE;

	/* Maybe this command unlocks for flashing mode? */
	previous_result = 0x0;
	fu_struct_asus_hid_command_set_cmd(cmd, FU_ASUS_HID_COMMAND_PRE_UPDATE6);
	fu_struct_asus_hid_command_set_length(cmd, 0x0);
	if (!fu_struct_asus_pre_update_command_set_data(cmd,
							(guint8 *)&previous_result,
							sizeof(previous_result),
							error))
		return FALSE;
	if (!fu_asus_hid_device_transfer_feature(device,
						 cmd,
						 NULL,
						 FU_ASUS_HID_REPORT_ID_INFO,
						 error))
		return FALSE;

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	return TRUE;
}

static GBytes *
fu_asus_hid_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(GByteArray) fw = g_byte_array_new();
	g_autoptr(GPtrArray) blocks = NULL;

	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "device is not in bootloader mode");
		return NULL;
	}

	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);
	fu_byte_array_set_size(fw, fu_device_get_firmware_size_max(device), 0x00);
	blocks = fu_chunk_array_mutable_new(fw->data,
					    fw->len,
					    0x0,
					    0x1000,
					    FU_STRUCT_ASUS_READ_FLASH_COMMAND_SIZE_DATA);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, blocks->len);
	for (guint i = 0, offset = 0; i < blocks->len; i++) {
		FuChunk *chk = g_ptr_array_index(blocks, i);
		g_autoptr(GByteArray) cmd = fu_struct_asus_read_flash_command_new();
		g_autoptr(GByteArray) result = fu_struct_asus_read_flash_command_new();

		fu_struct_asus_read_flash_command_set_offset(cmd, offset);
		fu_struct_asus_read_flash_command_set_datasz(cmd, fu_chunk_get_data_sz(chk));

		if (!fu_asus_hid_device_transfer_feature(device,
							 cmd,
							 result,
							 FU_ASUS_HID_REPORT_ID_FLASHING,
							 error))
			return NULL;

		if (!fu_memcpy_safe(fu_chunk_get_data_out(chk),
				    fu_chunk_get_data_sz(chk),
				    0x0,
				    fu_struct_asus_read_flash_command_get_data(result, NULL),
				    fu_struct_asus_read_flash_command_get_datasz(result),
				    0x0,
				    fu_struct_asus_read_flash_command_get_datasz(result),
				    error))
			return NULL;
		offset += fu_chunk_get_data_sz(chk);
		fu_progress_step_done(progress);
	}
	return g_bytes_new(fw->data, fw->len);
}

static gboolean
fu_asus_hid_device_verify_ite_part(FuDevice *device, GError **error)
{
	g_autoptr(GByteArray) cmd = fu_struct_flash_identify_new();
	g_autoptr(GByteArray) result = fu_struct_flash_identify_response_new();
	guint16 part;

	if (!fu_asus_hid_device_transfer_feature(device,
						 cmd,
						 result,
						 FU_ASUS_HID_REPORT_ID_FLASHING,
						 error))
		return FALSE;

	part = fu_struct_flash_identify_response_get_part(result);
	if (part != 0x8237) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unexpected part 0x%x",
			    part);
		return FALSE;
	}

	return TRUE;
}

/* Now to start flash sequence:
 * C0 command, no arguments
 * 18 C1 commands, 0x3b bytes each except for the last one which is 0x15 bytes
 * D0 command, and look at result to make a judgment
 * C3 command with argument of address and size of page (0x400)
 */
static gboolean
fu_asus_hid_device_write_data_chunks(FuAsusHidDevice *self,
				     FuChunkArray *chunks,
				     FuProgress *progress,
				     GError **error)
{
	FuDevice *device = FU_DEVICE(self);

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GByteArray) c0 = fu_struct_asus_clear_buffer_new();
		g_autoptr(GByteArray) c1 = fu_struct_asus_write_flash_command_new();
		g_autoptr(GByteArray) c3 = fu_struct_asus_flush_page_new();
		g_autoptr(GByteArray) d0 = fu_struct_asus_verify_buffer_new();
		g_autoptr(GByteArray) d0_res = fu_struct_asus_verify_result_new();

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		if (!fu_asus_hid_device_transfer_feature(device,
							 c0,
							 NULL,
							 FU_ASUS_HID_REPORT_ID_FLASHING,
							 error))
			return FALSE;

		if (!fu_struct_asus_write_flash_command_set_data(c1,
								 fu_chunk_get_data(chk),
								 fu_chunk_get_data_sz(chk),
								 error))
			return FALSE;

		if (!fu_asus_hid_device_transfer_feature(device,
							 c1,
							 NULL,
							 FU_ASUS_HID_REPORT_ID_FLASHING,
							 error))
			return FALSE;

		// TODO: add D0 command checking here.
		if (!fu_asus_hid_device_transfer_feature(device,
							 d0,
							 d0_res,
							 FU_ASUS_HID_REPORT_ID_FLASHING,
							 error))

			// TODO: need to add arguments for address and page size!!

			if (!fu_asus_hid_device_transfer_feature(device,
								 c3,
								 NULL,
								 FU_ASUS_HID_REPORT_ID_FLASHING,
								 error))
				return FALSE;

		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_asus_hid_device_write_data(FuAsusHidDevice *self,
			      FuFirmware *firmware,
			      FuProgress *progress,
			      GError **error)
{
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	stream = fu_firmware_get_image_by_id_stream(firmware, FU_FIRMWARE_ID_PAYLOAD, error);
	if (stream == NULL)
		return FALSE;

	chunks = fu_chunk_array_new_from_stream(stream, 0, 0x400, error);
	if (chunks == NULL)
		return FALSE;

	if (!fu_asus_hid_device_write_data_chunks(self, chunks, progress, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_asus_hid_device_write_firmware(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	if (fu_asus_hid_device_verify_ite_part(device, error))
		return FALSE;

	/* automatic backup should happen when FWUPD_DEVICE_FLAG_BACKUP_BEFORE_INSTALL is set */

	/* ASUS tool uses a series of 2 C2 commands that appear to check bootloader integrity:
	 * Start out by dumping the first 8k and seeing if all 8k matches in 1024 byte pages
	 *
	 * Use C2 command to set next offset?  Or to clear region.
	 *
	 * If dump didn't match, next offset will be 0x2000 and size will be right shifted by 10
	 * If dump did match, next offset will be 0 and size will be right shifted and 8 subtracted.
	 *
	 * c2(offset, shifted_size)
	 */

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);

	if (!fu_asus_hid_device_write_data(FU_ASUS_HID_DEVICE(device), firmware, progress, error))
		return FALSE;

	/* sequence of D1 commands, presumably to verify result */

	/* reset using attach */

	return TRUE;
}

static gboolean
fu_asus_hid_device_set_quirk_kv(FuDevice *device,
				const gchar *key,
				const gchar *value,
				GError **error)
{
	FuAsusHidDevice *self = FU_ASUS_HID_DEVICE(device);

	if (g_strcmp0(key, "AsusHidNumMcu") == 0) {
		guint64 tmp;

		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->num_mcu = tmp;
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_asus_hid_device_init(FuAsusHidDevice *self)
{
	// TODO: automatic backup
	// fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_BACKUP_BEFORE_INSTALL);
}

static void
fu_asus_hid_device_class_init(FuAsusHidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_asus_hid_device_setup;
	device_class->probe = fu_asus_hid_device_probe;
	device_class->set_quirk_kv = fu_asus_hid_device_set_quirk_kv;
	device_class->prepare_firmware = fu_asus_hid_device_prepare_firmware;
	device_class->write_firmware = fu_asus_hid_device_write_firmware;
	device_class->detach = fu_asus_hid_device_detach;
	device_class->attach = fu_asus_hid_device_attach;
	device_class->dump_firmware = fu_asus_hid_device_dump_firmware;
}
