/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-chunk.h"
#include "fu-rts54hid-common.h"
#include "fu-rts54hid-device.h"

struct _FuRts54HidDevice {
	FuHidDevice			 parent_instance;
	gboolean			 fw_auth;
	gboolean			 dual_bank;
};

G_DEFINE_TYPE (FuRts54HidDevice, fu_rts54hid_device, FU_TYPE_HID_DEVICE)

static void
fu_rts54hid_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuRts54HidDevice *self = FU_RTS54HID_DEVICE (device);
	fu_common_string_append_kb (str, idt, "FwAuth", self->fw_auth);
	fu_common_string_append_kb (str, idt, "DualBank", self->dual_bank);
}

static gboolean
fu_rts54hid_device_set_clock_mode (FuRts54HidDevice *self, gboolean enable, GError **error)
{
	FuRts54HidCmdBuffer cmd_buffer = {
		.cmd = FU_RTS54HID_CMD_WRITE_DATA,
		.ext = FU_RTS54HID_EXT_MCUMODIFYCLOCK,
		.cmd_data0 = (guint8) enable,
		.cmd_data1 = 0,
		.cmd_data2 = 0,
		.cmd_data3 = 0,
		.bufferlen = 0,
		.parameters = 0,
	};
	guint8 buf[FU_RTS54FU_HID_REPORT_LENGTH] = { 0 };
	memcpy (buf, &cmd_buffer, sizeof(cmd_buffer));
	if (!fu_hid_device_set_report (FU_HID_DEVICE (self), 0x0, buf, sizeof(buf),
				       FU_RTS54HID_DEVICE_TIMEOUT * 2,
				       FU_HID_DEVICE_FLAG_NONE,
				       error)) {
		g_prefix_error (error, "failed to set clock-mode=%i: ", enable);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_rts54hid_device_reset_to_flash (FuRts54HidDevice *self, GError **error)
{
	FuRts54HidCmdBuffer cmd_buffer = {
		.cmd = FU_RTS54HID_CMD_WRITE_DATA,
		.ext = FU_RTS54HID_EXT_RESET2FLASH,
		.dwregaddr = 0,
		.bufferlen = 0,
		.parameters = 0,
	};
	guint8 buf[FU_RTS54FU_HID_REPORT_LENGTH] = { 0 };
	memcpy (buf, &cmd_buffer, sizeof(cmd_buffer));
	if (!fu_hid_device_set_report (FU_HID_DEVICE (self), 0x0, buf, sizeof(buf),
				       FU_RTS54HID_DEVICE_TIMEOUT * 2,
				       FU_HID_DEVICE_FLAG_NONE,
				       error)) {
		g_prefix_error (error, "failed to soft reset: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_rts54hid_device_write_flash (FuRts54HidDevice *self,
				guint32 addr,
				const guint8 *data,
				guint16 data_sz,
				GError **error)
{
	FuRts54HidCmdBuffer cmd_buffer = {
		.cmd = FU_RTS54HID_CMD_WRITE_DATA,
		.ext = FU_RTS54HID_EXT_WRITEFLASH,
		.dwregaddr = GUINT32_TO_LE (addr),
		.bufferlen = GUINT16_TO_LE (data_sz),
		.parameters = 0,
	};
	guint8 buf[FU_RTS54FU_HID_REPORT_LENGTH] = { 0 };

	g_return_val_if_fail (data_sz <= 128, FALSE);
	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (data_sz != 0, FALSE);

	memcpy (buf, &cmd_buffer, sizeof(cmd_buffer));
	if (!fu_memcpy_safe (buf, sizeof(buf), FU_RTS54HID_CMD_BUFFER_OFFSET_DATA,	/* dst */
			     data, data_sz, 0x0,					/* src */
			     data_sz, error))
		return FALSE;
	if (!fu_hid_device_set_report (FU_HID_DEVICE (self), 0x0, buf, sizeof(buf),
				       FU_RTS54HID_DEVICE_TIMEOUT * 2,
				       FU_HID_DEVICE_FLAG_NONE,
				       error)) {
		g_prefix_error (error, "failed to write flash @%08x: ", (guint) addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_rts54hid_device_verify_update_fw (FuRts54HidDevice *self, GError **error)
{
	const FuRts54HidCmdBuffer cmd_buffer = {
		.cmd = FU_RTS54HID_CMD_WRITE_DATA,
		.ext = FU_RTS54HID_EXT_VERIFYUPDATE,
		.cmd_data0 = 1,
		.cmd_data1 = 0,
		.cmd_data2 = 0,
		.cmd_data3 = 0,
		.bufferlen = GUINT16_TO_LE (1),
		.parameters = 0,
	};
	guint8 buf[FU_RTS54FU_HID_REPORT_LENGTH] = { 0 };

	/* set then get */
	memcpy (buf, &cmd_buffer, sizeof(cmd_buffer));
	if (!fu_hid_device_set_report (FU_HID_DEVICE (self), 0x0, buf, sizeof(buf),
				       FU_RTS54HID_DEVICE_TIMEOUT * 2,
				       FU_HID_DEVICE_FLAG_NONE,
				       error))
		return FALSE;
	g_usleep (4 * G_USEC_PER_SEC);
	if (!fu_hid_device_get_report (FU_HID_DEVICE (self), 0x0, buf, sizeof(buf),
				       FU_RTS54HID_DEVICE_TIMEOUT,
				       FU_HID_DEVICE_FLAG_NONE,
				       error))
		return FALSE;

	/* check device status */
	if (buf[0x40] != 0x01) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_WRITE,
				     "firmware flash failed");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_rts54hid_device_erase_spare_bank (FuRts54HidDevice *self, GError **error)
{
	FuRts54HidCmdBuffer cmd_buffer = {
		.cmd = FU_RTS54HID_CMD_WRITE_DATA,
		.ext = FU_RTS54HID_EXT_ERASEBANK,
		.cmd_data0 = 0,
		.cmd_data1 = 1,
		.cmd_data2 = 0,
		.cmd_data3 = 0,
		.bufferlen = 0,
		.parameters = 0,
	};
	guint8 buf[FU_RTS54FU_HID_REPORT_LENGTH] = { 0 };
	memcpy (buf, &cmd_buffer, sizeof(cmd_buffer));
	if (!fu_hid_device_set_report (FU_HID_DEVICE (self), 0x0, buf, sizeof(buf),
				       FU_RTS54HID_DEVICE_TIMEOUT * 2,
				       FU_HID_DEVICE_FLAG_NONE,
				       error)) {
		g_prefix_error (error, "failed to erase spare bank: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_rts54hid_device_ensure_status (FuRts54HidDevice *self, GError **error)
{
	const FuRts54HidCmdBuffer cmd_buffer = {
		.cmd = FU_RTS54HID_CMD_READ_DATA,
		.ext = FU_RTS54HID_EXT_READ_STATUS,
		.cmd_data0 = 0,
		.cmd_data1 = 0,
		.cmd_data2 = 0,
		.cmd_data3 = 0,
		.bufferlen = GUINT16_TO_LE (32),
		.parameters = 0,
	};
	guint8 buf[FU_RTS54FU_HID_REPORT_LENGTH] = { 0 };
	g_autofree gchar *version = NULL;

	/* set then get */
	memcpy (buf, &cmd_buffer, sizeof(cmd_buffer));
	if (!fu_hid_device_set_report (FU_HID_DEVICE (self), 0x0, buf, sizeof(buf),
				       FU_RTS54HID_DEVICE_TIMEOUT * 2,
				       FU_HID_DEVICE_FLAG_NONE,
				       error))
		return FALSE;
	if (!fu_hid_device_get_report (FU_HID_DEVICE (self), 0x0, buf, sizeof(buf),
				       FU_RTS54HID_DEVICE_TIMEOUT,
				       FU_HID_DEVICE_FLAG_NONE,
				       error))
		return FALSE;

	/* check the hardware capabilities */
	self->dual_bank = (buf[0x40 + 7] & 0xf0) == 0x80;
	self->fw_auth = (buf[0x40 + 13] & 0x02) > 0;

	/* hub version is more accurate than bcdVersion */
	version = g_strdup_printf ("%x.%x", buf[0x40 + 10], buf[0x40 + 11]);
	fu_device_set_version (FU_DEVICE (self), version);
	return TRUE;
}

static gboolean
fu_rts54hid_device_setup (FuDevice *device, GError **error)
{
	FuRts54HidDevice *self = FU_RTS54HID_DEVICE (device);

	/* check this device is correct */
	if (!fu_rts54hid_device_ensure_status (self, error))
		return FALSE;

	/* both conditions must be set */
	if (!self->fw_auth) {
		fu_device_set_update_error (device,
					    "device does not support authentication");
	} else if (!self->dual_bank) {
		fu_device_set_update_error (device,
					    "device does not support dual-bank updating");
	} else {
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_rts54hid_device_close (FuHidDevice *device, GError **error)
{
	FuRts54HidDevice *self = FU_RTS54HID_DEVICE (device);

	/* set MCU to normal clock rate */
	return fu_rts54hid_device_set_clock_mode (self, FALSE, error);
}

static gboolean
fu_rts54hid_device_write_firmware (FuDevice *device,
				   FuFirmware *firmware,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuRts54HidDevice *self = FU_RTS54HID_DEVICE (device);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* get default image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* set MCU to high clock rate for better ISP performance */
	if (!fu_rts54hid_device_set_clock_mode (self, TRUE, error))
		return FALSE;

	/* erase spare flash bank only if it is not empty */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_ERASE);
	if (!fu_rts54hid_device_erase_spare_bank (self, error))
		return FALSE;

	/* build packets */
	chunks = fu_chunk_array_new_from_bytes (fw,
						0x00,	/* start addr */
						0x00,	/* page_sz */
						FU_RTS54HID_TRANSFER_BLOCK_SIZE);

	/* write each block */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);

		/* write chunk */
		if (!fu_rts54hid_device_write_flash (self,
						     chk->address,
						     chk->data,
						     chk->data_sz,
						     error))
			return FALSE;

		/* update progress */
		fu_device_set_progress_full (device, (gsize) i, (gsize) chunks->len * 2);
	}

	/* get device to authenticate the firmware */
	if (!fu_rts54hid_device_verify_update_fw (self, error))
		return FALSE;

	/* send software reset to run available flash code */
	if (!fu_rts54hid_device_reset_to_flash (self, error))
		return FALSE;

	/* success! */
	return TRUE;
}

static void
fu_rts54hid_device_init (FuRts54HidDevice *self)
{
	fu_device_set_protocol (FU_DEVICE (self), "com.realtek.rts54");
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_PAIR);
}

static void
fu_rts54hid_device_class_init (FuRts54HidDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuHidDeviceClass *klass_hid_device = FU_HID_DEVICE_CLASS (klass);
	klass_device->write_firmware = fu_rts54hid_device_write_firmware;
	klass_device->to_string = fu_rts54hid_device_to_string;
	klass_device->setup = fu_rts54hid_device_setup;
	klass_hid_device->close = fu_rts54hid_device_close;
}
