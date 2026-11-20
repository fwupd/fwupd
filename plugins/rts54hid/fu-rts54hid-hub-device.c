/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-rts54hid-common.h"
#include "fu-rts54hid-hub-device.h"
#include "fu-rts54hid-struct.h"

struct _FuRts54hidHubDevice {
	FuRts54hidDevice parent_instance;
};

G_DEFINE_TYPE(FuRts54hidHubDevice, fu_rts54hid_hub_device, FU_TYPE_RTS54HID_DEVICE)

static gboolean
fu_rts54hid_hub_device_set_clock_mode(FuRts54hidHubDevice *self, gboolean enable, GError **error)
{
	g_autoptr(FuRts54hidCmdBuffer) st = fu_rts54hid_cmd_buffer_new();

	fu_rts54hid_cmd_buffer_set_cmd(st, FU_RTS54HID_CMD_WRITE_DATA);
	fu_rts54hid_cmd_buffer_set_ext(st, FU_RTS54HID_EXT_MCUMODIFYCLOCK);
	fu_rts54hid_cmd_buffer_set_dwregaddr(st, (guint8)enable);
	fu_byte_array_set_size(st->buf, FU_RTS54FU_HID_REPORT_LENGTH, 0x0);

	if (!fu_rts54hid_device_set_report(FU_RTS54HID_DEVICE(self), st->buf, error)) {
		g_prefix_error(error, "failed to set clock-mode=%i: ", enable);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_rts54hid_hub_device_reset_to_flash(FuRts54hidHubDevice *self, GError **error)
{
	g_autoptr(FuRts54hidCmdBuffer) st = fu_rts54hid_cmd_buffer_new();

	fu_rts54hid_cmd_buffer_set_cmd(st, FU_RTS54HID_CMD_WRITE_DATA);
	fu_rts54hid_cmd_buffer_set_ext(st, FU_RTS54HID_EXT_RESET2FLASH);
	fu_byte_array_set_size(st->buf, FU_RTS54FU_HID_REPORT_LENGTH, 0x0);

	if (!fu_rts54hid_device_set_report(FU_RTS54HID_DEVICE(self), st->buf, error)) {
		g_prefix_error_literal(error, "failed to soft reset: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_rts54hid_hub_device_verify_update_fw(FuRts54hidHubDevice *self,
					FuProgress *progress,
					GError **error)
{
	g_autoptr(FuRts54hidCmdBuffer) st = fu_rts54hid_cmd_buffer_new();

	fu_rts54hid_cmd_buffer_set_cmd(st, FU_RTS54HID_CMD_WRITE_DATA);
	fu_rts54hid_cmd_buffer_set_ext(st, FU_RTS54HID_EXT_VERIFYUPDATE);
	fu_rts54hid_cmd_buffer_set_dwregaddr(st, 1);
	fu_rts54hid_cmd_buffer_set_bufferlen(st, 1);
	fu_byte_array_set_size(st->buf, FU_RTS54FU_HID_REPORT_LENGTH, 0x0);

	/* set then get */
	if (!fu_rts54hid_device_set_report(FU_RTS54HID_DEVICE(self), st->buf, error))
		return FALSE;
	fu_device_sleep_full(FU_DEVICE(self), 4000, progress); /* ms */
	if (!fu_rts54hid_device_get_report(FU_RTS54HID_DEVICE(self), st->buf, error))
		return FALSE;

	/* check device status */
	if (st->buf->data[0] != 0x01) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "firmware flash failed");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_rts54hid_hub_device_erase_spare_bank(FuRts54hidHubDevice *self, GError **error)
{
	g_autoptr(FuRts54hidCmdBuffer) st = fu_rts54hid_cmd_buffer_new();

	fu_rts54hid_cmd_buffer_set_cmd(st, FU_RTS54HID_CMD_WRITE_DATA);
	fu_rts54hid_cmd_buffer_set_ext(st, FU_RTS54HID_EXT_ERASEBANK);
	fu_rts54hid_cmd_buffer_set_dwregaddr(st, 0x100);
	fu_byte_array_set_size(st->buf, FU_RTS54FU_HID_REPORT_LENGTH, 0x0);

	if (!fu_rts54hid_device_set_report(FU_RTS54HID_DEVICE(self), st->buf, error)) {
		g_prefix_error_literal(error, "failed to erase spare bank: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_rts54hid_hub_device_ensure_status(FuRts54hidHubDevice *self, GError **error)
{
	g_autofree gchar *version = NULL;
	g_autoptr(FuRts54hidCmdBuffer) st = fu_rts54hid_cmd_buffer_new();

	fu_rts54hid_cmd_buffer_set_cmd(st, FU_RTS54HID_CMD_READ_DATA);
	fu_rts54hid_cmd_buffer_set_ext(st, FU_RTS54HID_EXT_READ_STATUS);
	fu_rts54hid_cmd_buffer_set_bufferlen(st, 32);
	fu_byte_array_set_size(st->buf, FU_RTS54FU_HID_REPORT_LENGTH, 0x0);

	/* set then get */
	if (!fu_rts54hid_device_set_report(FU_RTS54HID_DEVICE(self), st->buf, error))
		return FALSE;
	if (!fu_rts54hid_device_get_report(FU_RTS54HID_DEVICE(self), st->buf, error))
		return FALSE;

	/* check the hardware capabilities */
	if ((st->buf->data[7] & 0xf0) == 0x80)
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	if ((st->buf->data[13] & 0x02) > 0)
		fu_device_add_private_flag(FU_DEVICE(self), FU_RTS54HID_DEVICE_FLAG_FW_AUTH);

	/* hub version is more accurate than bcdVersion */
	version = g_strdup_printf("%x.%x", st->buf->data[10], st->buf->data[11]);
	fu_device_set_version(FU_DEVICE(self), version);
	return TRUE;
}

static gboolean
fu_rts54hid_hub_device_setup(FuDevice *device, GError **error)
{
	FuRts54hidHubDevice *self = FU_RTS54HID_HUB_DEVICE(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_rts54hid_hub_device_parent_class)->setup(device, error))
		return FALSE;

	/* check this device is correct */
	if (!fu_rts54hid_hub_device_ensure_status(self, error))
		return FALSE;

	/* both conditions must be set */
	if (!fu_device_has_private_flag(FU_DEVICE(self), FU_RTS54HID_DEVICE_FLAG_FW_AUTH)) {
		fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_INSECURE_PLATFORM);
	} else if (!fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE)) {
		fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_FIRMWARE_LOCKED);
	} else {
		fu_device_remove_problem(device, FWUPD_DEVICE_PROBLEM_INSECURE_PLATFORM);
		fu_device_remove_problem(device, FWUPD_DEVICE_PROBLEM_FIRMWARE_LOCKED);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_rts54hid_hub_device_close(FuDevice *device, GError **error)
{
	FuRts54hidHubDevice *self = FU_RTS54HID_HUB_DEVICE(device);

	/* set MCU to normal clock rate */
	if (!fu_rts54hid_hub_device_set_clock_mode(self, FALSE, error))
		return FALSE;

	/* FuHidDevice->close */
	return FU_DEVICE_CLASS(fu_rts54hid_hub_device_parent_class)->close(device, error);
}

static gboolean
fu_rts54hid_hub_device_write_chunks(FuRts54hidHubDevice *self,
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
		if (!fu_rts54hid_device_write_chunk(FU_RTS54HID_DEVICE(self), chk, error))
			return FALSE;
		fu_progress_step_done(progress);
	}
	return TRUE;
}

static gboolean
fu_rts54hid_hub_device_write_firmware(FuDevice *device,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuRts54hidHubDevice *self = FU_RTS54HID_HUB_DEVICE(device);
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 46, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 52, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reset");

	/* get default image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	/* set MCU to high clock rate for better ISP performance */
	if (!fu_rts54hid_hub_device_set_clock_mode(self, TRUE, error))
		return FALSE;

	/* erase spare flash bank only if it is not empty */
	if (!fu_rts54hid_hub_device_erase_spare_bank(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write each block */
	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						FU_RTS54HID_TRANSFER_BLOCK_SIZE,
						error);
	if (chunks == NULL)
		return FALSE;
	if (!fu_rts54hid_hub_device_write_chunks(self,
						 chunks,
						 fu_progress_get_child(progress),
						 error))
		return FALSE;
	fu_progress_step_done(progress);

	/* get device to authenticate the firmware */
	if (!fu_rts54hid_hub_device_verify_update_fw(self, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* send software reset to run available flash code */
	if (!fu_rts54hid_hub_device_reset_to_flash(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_rts54hid_hub_device_set_progress(FuDevice *device, FuProgress *progress)
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
fu_rts54hid_hub_device_init(FuRts54hidHubDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
}

static void
fu_rts54hid_hub_device_class_init(FuRts54hidHubDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_rts54hid_hub_device_write_firmware;
	device_class->setup = fu_rts54hid_hub_device_setup;
	device_class->close = fu_rts54hid_hub_device_close;
	device_class->set_progress = fu_rts54hid_hub_device_set_progress;
}
