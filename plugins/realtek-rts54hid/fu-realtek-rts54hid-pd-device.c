/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-realtek-rts54hid-common.h"
#include "fu-realtek-rts54hid-pd-device.h"
#include "fu-realtek-rts54hid-struct.h"

struct _FuRealtekRts54hidPdDevice {
	FuRealtekRts54hidDevice parent_instance;
};

G_DEFINE_TYPE(FuRealtekRts54hidPdDevice,
	      fu_realtek_rts54hid_pd_device,
	      FU_TYPE_REALTEK_RTS54HID_DEVICE)

#define FU_TYPE_REALTEK_RTS54HID_PD_DEVICE_FLAG_RESET_AFTER_UPDATE "reset-after-update"

static gboolean
fu_realtek_rts54hid_pd_device_reset_to_flash(FuRealtekRts54hidPdDevice *self, GError **error)
{
	g_autoptr(FuRealtekRts54hidCmdBuffer) st = fu_realtek_rts54hid_cmd_buffer_new();

	fu_realtek_rts54hid_cmd_buffer_set_cmd(st, FU_REALTEK_RTS54HID_CMD_WRITE_DATA);
	fu_realtek_rts54hid_cmd_buffer_set_ext(st, FU_REALTEK_RTS54HID_EXT_RESET2FLASH);
	fu_byte_array_set_size(st->buf, FU_RTS54FU_HID_REPORT_LENGTH, 0x0);

	if (!fu_realtek_rts54hid_device_set_report(FU_REALTEK_RTS54HID_DEVICE(self),
						   st->buf,
						   error)) {
		g_prefix_error_literal(error, "failed to soft reset: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_realtek_rts54hid_pd_device_verify_update_fw(FuRealtekRts54hidPdDevice *self, GError **error)
{
	g_autoptr(FuRealtekRts54hidCmdBuffer) st = fu_realtek_rts54hid_cmd_buffer_new();

	fu_realtek_rts54hid_cmd_buffer_set_cmd(st, FU_REALTEK_RTS54HID_CMD_WRITE_DATA);
	fu_realtek_rts54hid_cmd_buffer_set_ext(st, FU_REALTEK_RTS54HID_EXT_VERIFYUPDATE);
	fu_realtek_rts54hid_cmd_buffer_set_dwregaddr(st, 1);
	fu_realtek_rts54hid_cmd_buffer_set_bufferlen(st, 1);
	fu_byte_array_set_size(st->buf, FU_RTS54FU_HID_REPORT_LENGTH, 0x0);

	if (!fu_realtek_rts54hid_device_set_report(FU_REALTEK_RTS54HID_DEVICE(self),
						   st->buf,
						   error)) {
		g_prefix_error_literal(error, "failed to verify: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_realtek_rts54hid_pd_device_ensure_status(FuRealtekRts54hidPdDevice *self, GError **error)
{
	g_autofree gchar *version = NULL;
	g_autoptr(FuRealtekRts54hidCmdBuffer) st = fu_realtek_rts54hid_cmd_buffer_new();

	fu_realtek_rts54hid_cmd_buffer_set_cmd(st, FU_REALTEK_RTS54HID_CMD_READ_DATA);
	fu_realtek_rts54hid_cmd_buffer_set_ext(st, FU_REALTEK_RTS54HID_EXT_READ_STATUS);
	fu_realtek_rts54hid_cmd_buffer_set_bufferlen(st, 32);
	fu_byte_array_set_size(st->buf, FU_RTS54FU_HID_REPORT_LENGTH, 0x0);

	/* set then get */
	if (!fu_realtek_rts54hid_device_set_report(FU_REALTEK_RTS54HID_DEVICE(self),
						   st->buf,
						   error))
		return FALSE;
	if (!fu_realtek_rts54hid_device_get_report(FU_REALTEK_RTS54HID_DEVICE(self),
						   st->buf,
						   error))
		return FALSE;

	/* check the hardware capabilities */
	if ((st->buf->data[13] & FU_REALTEK_RTS54HID_STATUS_ISP_VALIDATION_CAPABILITY) > 0) {
		fu_device_add_private_flag(FU_DEVICE(self),
					   FU_REALTEK_RTS54HID_DEVICE_FLAG_FW_AUTH);
	}
	version = g_strdup_printf("%x.%x", st->buf->data[3], st->buf->data[4]);

	fu_device_set_version(FU_DEVICE(self), version);
	return TRUE;
}

static gboolean
fu_realtek_rts54hid_pd_device_vendor_cmd_enable(FuRealtekRts54hidPdDevice *self,
						FuRealtekRts54hidVendorCmdEnable value,
						GError **error)
{
	g_autoptr(FuRealtekRts54hidCmdBuffer) st = fu_realtek_rts54hid_cmd_buffer_new();

	fu_realtek_rts54hid_cmd_buffer_set_cmd(st, FU_REALTEK_RTS54HID_CMD_WRITE_DATA);
	fu_realtek_rts54hid_cmd_buffer_set_ext(st, FU_REALTEK_RTS54HID_EXT_VDCMDENABLE);
	fu_realtek_rts54hid_cmd_buffer_set_dwregaddr(st, 0x0bda0000 | value);
	fu_byte_array_set_size(st->buf, FU_RTS54FU_HID_REPORT_LENGTH, 0x0);

	if (!fu_realtek_rts54hid_device_set_report(FU_REALTEK_RTS54HID_DEVICE(self),
						   st->buf,
						   error)) {
		g_prefix_error_literal(error, "failed to enable vdcmd: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_realtek_rts54hid_pd_device_setup(FuDevice *device, GError **error)
{
	FuRealtekRts54hidPdDevice *self = FU_REALTEK_RTS54HID_PD_DEVICE(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_realtek_rts54hid_pd_device_parent_class)->setup(device, error))
		return FALSE;

	/* check this device is correct */
	if (!fu_realtek_rts54hid_pd_device_ensure_status(self, error))
		return FALSE;

	/* both conditions must be set */
	if (!fu_device_has_private_flag(FU_DEVICE(self), FU_REALTEK_RTS54HID_DEVICE_FLAG_FW_AUTH)) {
		fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_INSECURE_PLATFORM);
	} else {
		fu_device_remove_problem(device, FWUPD_DEVICE_PROBLEM_INSECURE_PLATFORM);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_realtek_rts54hid_pd_device_write_chunks(FuRealtekRts54hidPdDevice *self,
					   FuChunkArray *chunks,
					   FuProgress *progress,
					   GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_realtek_rts54hid_device_write_chunk(FU_REALTEK_RTS54HID_DEVICE(self),
							    chk,
							    error))
			return FALSE;
		fu_progress_step_done(progress);
	}
	return TRUE;
}

static gboolean
fu_realtek_rts54hid_pd_device_write_firmware(FuDevice *device,
					     FuFirmware *firmware,
					     FuProgress *progress,
					     FwupdInstallFlags flags,
					     GError **error)
{
	FuRealtekRts54hidPdDevice *self = FU_REALTEK_RTS54HID_PD_DEVICE(device);
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);

	/* set MCU vendor cmd enable */
	if (!fu_realtek_rts54hid_pd_device_vendor_cmd_enable(
		self,
		FU_REALTEK_RTS54HID_VENDOR_CMD_ENABLE_VENDOR_DEFINED,
		error))
		return FALSE;
	if (!fu_realtek_rts54hid_pd_device_vendor_cmd_enable(
		self,
		FU_REALTEK_RTS54HID_VENDOR_CMD_ENABLE_VENDOR_DEFINED |
		    FU_REALTEK_RTS54HID_VENDOR_CMD_ENABLE_SPI_FLASH_ACCESS,
		error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write each block */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						FU_REALTEK_RTS54HID_TRANSFER_BLOCK_SIZE,
						error);
	if (chunks == NULL)
		return FALSE;
	if (!fu_realtek_rts54hid_pd_device_write_chunks(self,
							chunks,
							fu_progress_get_child(progress),
							error))
		return FALSE;
	fu_progress_step_done(progress);

	/* send verify cmd to authenticate the firmware*/
	if (!fu_realtek_rts54hid_pd_device_verify_update_fw(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	if (fu_device_has_private_flag(
		FU_DEVICE(self),
		FU_TYPE_REALTEK_RTS54HID_PD_DEVICE_FLAG_RESET_AFTER_UPDATE)) {
		if (!fu_realtek_rts54hid_pd_device_reset_to_flash(self, error))
			return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_realtek_rts54hid_pd_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 62, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 38, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_realtek_rts54hid_pd_device_init(FuRealtekRts54hidPdDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_private_flag(FU_DEVICE(self),
				   FU_TYPE_REALTEK_RTS54HID_PD_DEVICE_FLAG_RESET_AFTER_UPDATE);
}

static void
fu_realtek_rts54hid_pd_device_class_init(FuRealtekRts54hidPdDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_realtek_rts54hid_pd_device_write_firmware;
	device_class->setup = fu_realtek_rts54hid_pd_device_setup;
	device_class->set_progress = fu_realtek_rts54hid_pd_device_set_progress;
	fu_device_register_private_flag(device_class,
					FU_TYPE_REALTEK_RTS54HID_PD_DEVICE_FLAG_RESET_AFTER_UPDATE);
}
