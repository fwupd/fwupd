/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-rts54hid-common.h"
#include "fu-rts54hid-device.h"
#include "fu-rts54hid-struct.h"

struct _FuRts54HidDevice {
	FuHidDevice parent_instance;
	gboolean fw_auth;
	gboolean dual_bank;
};

G_DEFINE_TYPE(FuRts54HidDevice, fu_rts54hid_device, FU_TYPE_HID_DEVICE)

static void
fu_rts54hid_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuRts54HidDevice *self = FU_RTS54HID_DEVICE(device);
	fwupd_codec_string_append_bool(str, idt, "FwAuth", self->fw_auth);
	fwupd_codec_string_append_bool(str, idt, "DualBank", self->dual_bank);
}

static gboolean
fu_rts54hid_device_set_clock_mode(FuRts54HidDevice *self, gboolean enable, GError **error)
{
	g_autoptr(FuRts54HidCmdBuffer) st = fu_rts54_hid_cmd_buffer_new();

	fu_rts54_hid_cmd_buffer_set_cmd(st, FU_RTS54HID_CMD_WRITE_DATA);
	fu_rts54_hid_cmd_buffer_set_ext(st, FU_RTS54HID_EXT_MCUMODIFYCLOCK);
	fu_rts54_hid_cmd_buffer_set_dwregaddr(st, (guint8)enable);
	fu_byte_array_set_size(st, FU_RTS54FU_HID_REPORT_LENGTH, 0x0);

	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      0x0,
				      st->data,
				      st->len,
				      FU_RTS54HID_DEVICE_TIMEOUT * 2,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "failed to set clock-mode=%i: ", enable);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_rts54hid_device_reset_to_flash(FuRts54HidDevice *self, GError **error)
{
	g_autoptr(FuRts54HidCmdBuffer) st = fu_rts54_hid_cmd_buffer_new();

	fu_rts54_hid_cmd_buffer_set_cmd(st, FU_RTS54HID_CMD_WRITE_DATA);
	fu_rts54_hid_cmd_buffer_set_ext(st, FU_RTS54HID_EXT_RESET2FLASH);
	fu_byte_array_set_size(st, FU_RTS54FU_HID_REPORT_LENGTH, 0x0);

	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      0x0,
				      st->data,
				      st->len,
				      FU_RTS54HID_DEVICE_TIMEOUT * 2,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "failed to soft reset: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_rts54hid_device_write_flash(FuRts54HidDevice *self,
			       guint32 addr,
			       const guint8 *data,
			       guint16 data_sz,
			       GError **error)
{
	g_autoptr(FuRts54HidCmdBuffer) st = fu_rts54_hid_cmd_buffer_new();

	g_return_val_if_fail(data_sz <= 128, FALSE);
	g_return_val_if_fail(data != NULL, FALSE);
	g_return_val_if_fail(data_sz != 0, FALSE);

	fu_rts54_hid_cmd_buffer_set_cmd(st, FU_RTS54HID_CMD_WRITE_DATA);
	fu_rts54_hid_cmd_buffer_set_ext(st, FU_RTS54HID_EXT_WRITEFLASH);
	fu_rts54_hid_cmd_buffer_set_dwregaddr(st, addr);
	fu_rts54_hid_cmd_buffer_set_bufferlen(st, data_sz);
	fu_byte_array_set_size(st, FU_RTS54FU_HID_REPORT_LENGTH, 0x0);

	if (!fu_memcpy_safe(st->data,
			    st->len,
			    FU_RTS54HID_CMD_BUFFER_OFFSET_DATA, /* dst */
			    data,
			    data_sz,
			    0x0, /* src */
			    data_sz,
			    error))
		return FALSE;

	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      0x0,
				      st->data,
				      st->len,
				      FU_RTS54HID_DEVICE_TIMEOUT * 2,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "failed to write flash @%08x: ", (guint)addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_rts54hid_device_verify_update_fw(FuRts54HidDevice *self, FuProgress *progress, GError **error)
{
	g_autoptr(FuRts54HidCmdBuffer) st = fu_rts54_hid_cmd_buffer_new();

	fu_rts54_hid_cmd_buffer_set_cmd(st, FU_RTS54HID_CMD_WRITE_DATA);
	fu_rts54_hid_cmd_buffer_set_ext(st, FU_RTS54HID_EXT_VERIFYUPDATE);
	fu_rts54_hid_cmd_buffer_set_dwregaddr(st, 1);
	fu_rts54_hid_cmd_buffer_set_bufferlen(st, 1);
	fu_byte_array_set_size(st, FU_RTS54FU_HID_REPORT_LENGTH, 0x0);

	/* set then get */
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      0x0,
				      st->data,
				      st->len,
				      FU_RTS54HID_DEVICE_TIMEOUT * 2,
				      FU_HID_DEVICE_FLAG_NONE,
				      error))
		return FALSE;
	fu_device_sleep_full(FU_DEVICE(self), 4000, progress); /* ms */
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      0x0,
				      st->data,
				      st->len,
				      FU_RTS54HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error))
		return FALSE;

	/* check device status */
	if (st->data[0] != 0x01) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "firmware flash failed");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_rts54hid_device_erase_spare_bank(FuRts54HidDevice *self, GError **error)
{
	g_autoptr(FuRts54HidCmdBuffer) st = fu_rts54_hid_cmd_buffer_new();

	fu_rts54_hid_cmd_buffer_set_cmd(st, FU_RTS54HID_CMD_WRITE_DATA);
	fu_rts54_hid_cmd_buffer_set_ext(st, FU_RTS54HID_EXT_ERASEBANK);
	fu_rts54_hid_cmd_buffer_set_dwregaddr(st, 0x100);
	fu_byte_array_set_size(st, FU_RTS54FU_HID_REPORT_LENGTH, 0x0);

	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      0x0,
				      st->data,
				      st->len,
				      FU_RTS54HID_DEVICE_TIMEOUT * 2,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "failed to erase spare bank: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_rts54hid_device_ensure_status(FuRts54HidDevice *self, GError **error)
{
	g_autofree gchar *version = NULL;
	g_autoptr(FuRts54HidCmdBuffer) st = fu_rts54_hid_cmd_buffer_new();

	fu_rts54_hid_cmd_buffer_set_cmd(st, FU_RTS54HID_CMD_READ_DATA);
	fu_rts54_hid_cmd_buffer_set_ext(st, FU_RTS54HID_EXT_READ_STATUS);
	fu_rts54_hid_cmd_buffer_set_bufferlen(st, 32);
	fu_byte_array_set_size(st, FU_RTS54FU_HID_REPORT_LENGTH, 0x0);

	/* set then get */
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      0x0,
				      st->data,
				      st->len,
				      FU_RTS54HID_DEVICE_TIMEOUT * 2,
				      FU_HID_DEVICE_FLAG_NONE,
				      error))
		return FALSE;
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      0x0,
				      st->data,
				      st->len,
				      FU_RTS54HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error))
		return FALSE;

	/* check the hardware capabilities */
	self->dual_bank = (st->data[7] & 0xf0) == 0x80;
	self->fw_auth = (st->data[13] & 0x02) > 0;

	/* hub version is more accurate than bcdVersion */
	version = g_strdup_printf("%x.%x", st->data[10], st->data[11]);
	fu_device_set_version(FU_DEVICE(self), version);
	return TRUE;
}

static gboolean
fu_rts54hid_device_setup(FuDevice *device, GError **error)
{
	FuRts54HidDevice *self = FU_RTS54HID_DEVICE(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_rts54hid_device_parent_class)->setup(device, error))
		return FALSE;

	/* check this device is correct */
	if (!fu_rts54hid_device_ensure_status(self, error))
		return FALSE;

	/* both conditions must be set */
	if (!self->fw_auth) {
		fu_device_set_update_error(device, "device does not support authentication");
	} else if (!self->dual_bank) {
		fu_device_set_update_error(device, "device does not support dual-bank updating");
	} else {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_rts54hid_device_close(FuDevice *device, GError **error)
{
	FuRts54HidDevice *self = FU_RTS54HID_DEVICE(device);

	/* set MCU to normal clock rate */
	if (!fu_rts54hid_device_set_clock_mode(self, FALSE, error))
		return FALSE;

	/* FuHidDevice->close */
	return FU_DEVICE_CLASS(fu_rts54hid_device_parent_class)->close(device, error);
}

static gboolean
fu_rts54hid_device_write_firmware(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuRts54HidDevice *self = FU_RTS54HID_DEVICE(device);
	g_autoptr(GInputStream) stream = NULL;
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
	if (!fu_rts54hid_device_set_clock_mode(self, TRUE, error))
		return FALSE;

	/* erase spare flash bank only if it is not empty */
	if (!fu_rts54hid_device_erase_spare_bank(self, error))
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
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		/* write chunk */
		if (!fu_rts54hid_device_write_flash(self,
						    fu_chunk_get_address(chk),
						    fu_chunk_get_data(chk),
						    fu_chunk_get_data_sz(chk),
						    error))
			return FALSE;

		/* update progress */
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)i + 1,
						(gsize)fu_chunk_array_length(chunks));
	}
	fu_progress_step_done(progress);

	/* get device to authenticate the firmware */
	if (!fu_rts54hid_device_verify_update_fw(self, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* send software reset to run available flash code */
	if (!fu_rts54hid_device_reset_to_flash(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static void
fu_rts54hid_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 62, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 38, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_rts54hid_device_init(FuRts54HidDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.realtek.rts54");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
}

static void
fu_rts54hid_device_class_init(FuRts54HidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_rts54hid_device_write_firmware;
	device_class->to_string = fu_rts54hid_device_to_string;
	device_class->setup = fu_rts54hid_device_setup;
	device_class->close = fu_rts54hid_device_close;
	device_class->set_progress = fu_rts54hid_device_set_progress;
}
