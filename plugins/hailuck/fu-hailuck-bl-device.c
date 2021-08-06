/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-hailuck-common.h"
#include "fu-hailuck-bl-device.h"
#include "fu-hailuck-kbd-firmware.h"

struct _FuHailuckBlDevice {
	FuHidDevice		 parent_instance;
};

G_DEFINE_TYPE (FuHailuckBlDevice, fu_hailuck_bl_device, FU_TYPE_HID_DEVICE)

static gboolean
fu_hailuck_bl_device_attach (FuDevice *device, GError **error)
{
	guint8 buf[6] = {
		FU_HAILUCK_REPORT_ID_SHORT,
		FU_HAILUCK_CMD_ATTACH,
	};
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	if (!fu_hid_device_set_report (FU_HID_DEVICE (device),
				       buf[0], buf, sizeof(buf), 1000,
				       FU_HID_DEVICE_FLAG_IS_FEATURE, error))
		return FALSE;
	if (!g_usb_device_reset (fu_usb_device_get_dev (FU_USB_DEVICE (device)), error))
		return FALSE;
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_hailuck_bl_device_probe (FuDevice *device, GError **error)
{
	g_autofree gchar *devid = NULL;

	/* FuUsbDevice->probe */
	if (!FU_DEVICE_CLASS (fu_hailuck_bl_device_parent_class)->probe (device, error))
		return FALSE;

	/* add extra keyboard-specific GUID */
	devid = g_strdup_printf ("USB\\VID_%04X&PID_%04X&MODE_KBD",
				 fu_usb_device_get_vid (FU_USB_DEVICE (device)),
				 fu_usb_device_get_pid (FU_USB_DEVICE (device)));
	fu_device_add_instance_id (device, devid);

	/* success */
	return TRUE;
}

static gboolean
fu_hailuck_bl_device_read_block_start (FuHailuckBlDevice *self,
					guint32 length,
					GError **error)
{
	guint8 buf[6] = {
		FU_HAILUCK_REPORT_ID_SHORT,
		FU_HAILUCK_CMD_READ_BLOCK_START,
	};
	fu_common_write_uint16 (buf + 4, length, G_LITTLE_ENDIAN);
	return fu_hid_device_set_report (FU_HID_DEVICE (self), buf[0],
					 buf, sizeof(buf), 100,
					 FU_HID_DEVICE_FLAG_IS_FEATURE, error);
}

static gboolean
fu_hailuck_bl_device_read_block (FuHailuckBlDevice *self,
				  guint8 *data, gsize data_sz,
				  GError **error)
{
	gsize bufsz = data_sz + 2;
	g_autofree guint8 *buf = g_malloc0 (bufsz);

	buf[0] = FU_HAILUCK_REPORT_ID_LONG;
	buf[1] = FU_HAILUCK_CMD_READ_BLOCK;
	if (!fu_hid_device_get_report (FU_HID_DEVICE (self), buf[0],
				       buf, bufsz, 2000,
				       FU_HID_DEVICE_FLAG_IS_FEATURE,
				       error))
		return FALSE;
	if (!fu_memcpy_safe (data, data_sz, 0x0,	/* dst */
			     buf, bufsz, 0x02,		/* src */
			     data_sz, error))
		return FALSE;

	/* success */
	g_usleep (10000);
	return TRUE;
}

static GBytes *
fu_hailuck_bl_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuHailuckBlDevice *self = FU_HAILUCK_BL_DEVICE (device);
	gsize fwsz = fu_device_get_firmware_size_max (device);
	g_autoptr(GByteArray) fwbuf = g_byte_array_new ();
	g_autoptr(GPtrArray) chunks = NULL;

	/* tell device amount of data to send */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_READ);
	if (!fu_hailuck_bl_device_read_block_start (self, fwsz, error))
		return NULL;

	/* receive data back */
	fu_byte_array_set_size (fwbuf, fwsz);
	chunks = fu_chunk_array_mutable_new (fwbuf->data, fwbuf->len, 0x0, 0x0, 2048);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		if (!fu_hailuck_bl_device_read_block (self,
						      fu_chunk_get_data_out (chk),
						      fu_chunk_get_data_sz (chk),
						      error))
			return NULL;
		fu_progress_set_percentage_full(progress, i + 1, chunks->len);
	}

	/* success */
	return g_byte_array_free_to_bytes (g_steal_pointer (&fwbuf));
}

static gboolean
fu_hailuck_bl_device_erase(FuHailuckBlDevice *self, FuProgress *progress, GError **error)
{
	guint8 buf[6] = {
		FU_HAILUCK_REPORT_ID_SHORT,
		FU_HAILUCK_CMD_ERASE,
	};
	if (!fu_hid_device_set_report (FU_HID_DEVICE (self), buf[0],
				       buf, sizeof(buf), 100,
				       FU_HID_DEVICE_FLAG_IS_FEATURE, error))
		return FALSE;
	fu_progress_sleep(progress, 2);
	return TRUE;
}

static gboolean
fu_hailuck_bl_device_write_block_start (FuHailuckBlDevice *self,
					 guint32 length, GError **error)
{
	guint8 buf[6] = {
		FU_HAILUCK_REPORT_ID_SHORT,
		FU_HAILUCK_CMD_WRITE_BLOCK_START,
	};
	fu_common_write_uint16 (buf + 4, length, G_LITTLE_ENDIAN);
	return fu_hid_device_set_report (FU_HID_DEVICE (self), buf[0],
					 buf, sizeof(buf), 100,
					 FU_HID_DEVICE_FLAG_IS_FEATURE, error);
}

static gboolean
fu_hailuck_bl_device_write_block (FuHailuckBlDevice *self,
				   const guint8 *data, gsize data_sz,
				   GError **error)
{
	gsize bufsz = data_sz + 2;
	g_autofree guint8 *buf = g_malloc0 (bufsz);

	buf[0] = FU_HAILUCK_REPORT_ID_LONG;
	buf[1] = FU_HAILUCK_CMD_WRITE_BLOCK;
	if (!fu_memcpy_safe (buf, bufsz, 0x02,		/* dst */
			     data, data_sz, 0x0,	/* src */
			     data_sz, error))
		return FALSE;
	if (!fu_hid_device_set_report (FU_HID_DEVICE (self), buf[0],
				       buf, bufsz, 2000,
				       FU_HID_DEVICE_FLAG_IS_FEATURE,
				       error))
		return FALSE;

	/* success */
	g_usleep (10000);
	return TRUE;
}

static FuFirmware *
fu_hailuck_bl_device_prepare_firmware (FuDevice *device,
				     GBytes *fw,
				     FwupdInstallFlags flags,
				     GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_hailuck_kbd_firmware_new ();
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;
	return g_steal_pointer (&firmware);
}

static gboolean
fu_hailuck_bl_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuHailuckBlDevice *self = FU_HAILUCK_BL_DEVICE (device);
	FuChunk *chk0;
	FuProgress *progress_local;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) fw_new = NULL;
	g_autoptr(GPtrArray) chunks = NULL;
	g_autofree guint8 *chk0_data = NULL;

	/* progress */
	fu_progress_set_custom_steps(progress,
				     10 /* erase */,
				     80 /* write */,
				     1 /* write0 */,
				     9 /* verify */,
				     -1);

	/* get default image */
	fw = fu_firmware_get_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* erase all contents */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_ERASE);
	if (!fu_hailuck_bl_device_erase(self, fu_progress_get_division(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* tell device amount of data to expect */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_hailuck_bl_device_write_block_start (self, g_bytes_get_size (fw), error))
		return FALSE;

	/* build packets */
	chunks = fu_chunk_array_new_from_bytes (fw, 0x0, 0x00, 2048);

	/* intentionally corrupt first chunk so that CRC fails */
	chk0 = g_ptr_array_index (chunks, 0);
	chk0_data = fu_memdup_safe (fu_chunk_get_data (chk0),
				    fu_chunk_get_data_sz (chk0),
				    error);
	if (chk0_data == NULL)
		return FALSE;
	chk0_data[0] = 0x00;
	if (!fu_hailuck_bl_device_write_block (self,
					       chk0_data,
					       fu_chunk_get_data_sz (chk0),
					       error))
		return FALSE;

	/* send the rest of the chunks */
	progress_local = fu_progress_get_division(progress);
	for (guint i = 1; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		if (!fu_hailuck_bl_device_write_block (self,
						       fu_chunk_get_data (chk),
						       fu_chunk_get_data_sz (chk),
						       error))
			return FALSE;
		fu_progress_set_percentage_full(progress_local, i, chunks->len);
	}
	fu_progress_step_done(progress);

	/* retry write of first block */
	if (!fu_hailuck_bl_device_write_block_start (self, g_bytes_get_size (fw), error))
		return FALSE;
	if (!fu_hailuck_bl_device_write_block (self,
					       fu_chunk_get_data (chk0),
					       fu_chunk_get_data_sz (chk0),
					       error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify */
	fw_new =
	    fu_hailuck_bl_device_dump_firmware(device, fu_progress_get_division(progress), error);
	fu_progress_step_done(progress);
	return fu_common_bytes_compare (fw, fw_new, error);
}

static void
fu_hailuck_bl_device_init (FuHailuckBlDevice *self)
{
	fu_device_set_firmware_size (FU_DEVICE (self), 0x4000);
	fu_device_add_protocol (FU_DEVICE (self), "com.hailuck.kbd");
	fu_device_set_name (FU_DEVICE (self), "Keyboard [bootloader]");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_internal_flag (FU_DEVICE (self), FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_icon (FU_DEVICE (self), "input-keyboard");
	fu_hid_device_add_flag (FU_HID_DEVICE (self), FU_HID_DEVICE_FLAG_NO_KERNEL_REBIND);
	fu_device_set_remove_delay (FU_DEVICE (self),
				    FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
}

static void
fu_hailuck_bl_device_class_init (FuHailuckBlDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->dump_firmware = fu_hailuck_bl_device_dump_firmware;
	klass_device->prepare_firmware = fu_hailuck_bl_device_prepare_firmware;
	klass_device->write_firmware = fu_hailuck_bl_device_write_firmware;
	klass_device->attach = fu_hailuck_bl_device_attach;
	klass_device->probe = fu_hailuck_bl_device_probe;
}
