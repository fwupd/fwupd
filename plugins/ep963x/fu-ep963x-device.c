/*#
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-ep963x-common.h"
#include "fu-ep963x-device.h"
#include "fu-ep963x-firmware.h"

struct _FuEp963xDevice {
	FuHidDevice		 parent_instance;
};

G_DEFINE_TYPE (FuEp963xDevice, fu_ep963x_device, FU_TYPE_HID_DEVICE)

#define FU_EP963_DEVICE_TIMEOUT			5000	/* ms */

static gboolean
fu_ep963x_device_write (FuEp963xDevice *self,
			guint8 ctrl_id, guint8 cmd,
			const guint8 *buf, gsize bufsz,
			GError **error)
{
	guint8 bufhw[FU_EP963_FEATURE_ID1_SIZE] = {
		ctrl_id, cmd, 0x0,
	};
	if (buf != NULL) {
		if (!fu_memcpy_safe (bufhw, sizeof(bufhw), 0x02,	/* dst */
				     buf, bufsz, 0x0,			/* src */
				     bufsz, error))
			return FALSE;
	}
	if (!fu_hid_device_set_report (FU_HID_DEVICE (self), 0x00,
				       bufhw, sizeof(bufhw),
				       FU_EP963_DEVICE_TIMEOUT,
				       FU_HID_DEVICE_FLAG_IS_FEATURE,
				       error))
		return FALSE;

	/* wait for hardware */
	g_usleep (100 * 1000);
	return TRUE;
}

static gboolean
fu_ep963x_device_write_icp (FuEp963xDevice *self, guint8 cmd,
			    const guint8 *buf, gsize bufsz,
			    guint8 *bufout, gsize bufoutsz,
			    GError **error)
{
	/* wait for hardware */
	for (guint i = 0; i < 5; i++) {
		guint8 bufhw[FU_EP963_FEATURE_ID1_SIZE] = {
			FU_EP963_USB_CONTROL_ID,
			cmd,
		};
		if (!fu_ep963x_device_write (self, FU_EP963_USB_CONTROL_ID,
					     cmd, buf, bufsz, error))
			return FALSE;
		if (!fu_hid_device_get_report (FU_HID_DEVICE (self), 0x00,
					       bufhw, sizeof(bufhw),
					       FU_EP963_DEVICE_TIMEOUT,
					       FU_HID_DEVICE_FLAG_IS_FEATURE,
					       error)) {
			return FALSE;
		}
		if (bufhw[2] == FU_EP963_USB_STATE_READY) {
			/* optional data */
			if (bufout != NULL) {
				if (!fu_memcpy_safe (bufout, bufoutsz, 0x0,
						     bufhw, sizeof(bufhw), 0x02,
						     bufoutsz, error))
					return FALSE;
			}
			return TRUE;
		}
		g_usleep (100 * 1000);
	}

	/* failed */
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to wait for icp-done");
	return FALSE;
}

static gboolean
fu_ep963x_device_detach (FuDevice *device, GError **error)
{
	FuEp963xDevice *self = FU_EP963X_DEVICE (device);
	const guint8 buf[] = { 'E', 'P', '9', '6', '3' };
	g_autoptr(GError) error_local = NULL;

	/* sanity check */
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug ("already in bootloader mode, skipping");
		return TRUE;
	}

	if (!fu_ep963x_device_write_icp (self, FU_EP963_ICP_ENTER,
					 buf, sizeof(buf),	/* in */
					 NULL, 0x0,		/* out */
					 &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to detach: %s",
			     error_local->message);
		return FALSE;
	}

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_ep963x_device_attach (FuDevice *device, GError **error)
{
	FuEp963xDevice *self = FU_EP963X_DEVICE (device);
	g_autoptr(GError) error_local = NULL;

	/* sanity check */
	if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug ("already in runtime mode, skipping");
		return TRUE;
	}

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	if (!fu_ep963x_device_write (self,
				     FU_EP963_USB_CONTROL_ID,
				     FU_EP963_OPCODE_SUBMCU_PROGRAM_FINISHED,
				     NULL, 0, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to boot to runtime: %s",
			     error_local->message);
		return FALSE;
	}
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_ep963x_device_setup (FuDevice *device, GError **error)
{
	FuEp963xDevice *self = FU_EP963X_DEVICE (device);
	guint8 buf[] = { 0x0 };
	g_autofree gchar *version = NULL;

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS (fu_ep963x_device_parent_class)->setup (device, error))
		return FALSE;

	/* get version */
	if (!fu_ep963x_device_write_icp (self, FU_EP963_UF_CMD_VERSION,
					 NULL, 0,		/* in */
					 buf, sizeof(buf),	/* out */
					 error)) {
		return FALSE;
	}
	version = g_strdup_printf ("%i", buf[0]);
	fu_device_set_version (device, version);

	/* the VID and PID are unchanged between bootloader modes */
	if (buf[0] == 0x00) {
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	} else {
		fu_device_remove_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}

	/* success */
	return TRUE;
}

static FuFirmware *
fu_ep963x_device_prepare_firmware (FuDevice *device,
				    GBytes *fw,
				    FwupdInstallFlags flags,
				    GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_ep963x_firmware_new ();
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;
	return g_steal_pointer (&firmware);
}

static gboolean
fu_ep963x_device_wait_cb (FuDevice *device, gpointer user_data, GError **error)
{
	guint8 bufhw[FU_EP963_FEATURE_ID1_SIZE] = {
		FU_EP963_USB_CONTROL_ID,
		FU_EP963_OPCODE_SUBMCU_PROGRAM_BLOCK,
		0xFF,
	};
	if (!fu_hid_device_get_report (FU_HID_DEVICE (device), 0x00,
				       bufhw, sizeof(bufhw),
				       FU_EP963_DEVICE_TIMEOUT,
				       FU_HID_DEVICE_FLAG_IS_FEATURE,
				       error)) {
		return FALSE;
	}
	if (bufhw[2] != FU_EP963_USB_STATE_READY) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_BUSY,
				     "hardware is not ready");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ep963x_device_write_firmware(FuDevice *device,
				FuFirmware *firmware,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	FuEp963xDevice *self = FU_EP963X_DEVICE (device);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) blocks = NULL;

	/* get default image */
	fw = fu_firmware_get_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* reset the block index */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_ep963x_device_write (self,
				     FU_EP963_USB_CONTROL_ID,
				     FU_EP963_OPCODE_SUBMCU_ENTER_ICP,
				     NULL, 0, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to reset block index: %s",
			     error_local->message);
		return FALSE;
	}

	/* write each block */
	blocks = fu_chunk_array_new_from_bytes (fw, 0x00, 0x00,
						FU_EP963_TRANSFER_BLOCK_SIZE);
	for (guint i = 0; i < blocks->len; i++) {
		FuChunk *chk2 = g_ptr_array_index (blocks, i);
		guint8 buf[] = { i };
		g_autoptr(GPtrArray) chunks = NULL;

		/* set the block index */
		if (!fu_ep963x_device_write (self,
					     FU_EP963_USB_CONTROL_ID,
					     FU_EP963_OPCODE_SUBMCU_RESET_BLOCK_IDX,
					     buf, sizeof(buf), &error_local)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_WRITE,
				     "failed to reset block index: %s",
				     error_local->message);
			return FALSE;
		}

		/* 4 byte chunks */
		chunks = fu_chunk_array_new (fu_chunk_get_data (chk2),
					     fu_chunk_get_data_sz (chk2),
					     fu_chunk_get_address (chk2), 0x0,
					     FU_EP963_TRANSFER_CHUNK_SIZE);
		for (guint j = 0; j < chunks->len; j++) {
			FuChunk *chk = g_ptr_array_index (chunks, j);
			g_autoptr(GError) error_loop = NULL;

			/* copy data and write */
			if (!fu_ep963x_device_write (self,
						     FU_EP963_USB_CONTROL_ID,
						     FU_EP963_OPCODE_SUBMCU_WRITE_BLOCK_DATA,
						     fu_chunk_get_data (chk),
						     fu_chunk_get_data_sz (chk),
						     &error_loop)) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_WRITE,
					     "failed to write 0x%x: %s",
					     (guint) fu_chunk_get_address (chk),
					     error_loop->message);
				return FALSE;
			}
		}

		/* program block */
		if (!fu_ep963x_device_write (self,
					     FU_EP963_USB_CONTROL_ID,
					     FU_EP963_OPCODE_SUBMCU_PROGRAM_BLOCK,
					     buf, sizeof(buf), &error_local)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_WRITE,
				     "failed to write 0x%x: %s",
				     (guint) fu_chunk_get_address (chk2),
				     error_local->message);
			return FALSE;
		}

		/* wait for program finished */
		if (!fu_device_retry (device, fu_ep963x_device_wait_cb, 5, NULL, error))
			return FALSE;

		/* update progress */
		fu_progress_set_percentage_full(progress, (gsize)i, (gsize)chunks->len);
	}

	/* success! */
	return TRUE;
}

static void
fu_ep963x_device_init (FuEp963xDevice *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_protocol (FU_DEVICE (self), "tw.com.exploretech.ep963x");
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_NUMBER);
	fu_device_set_remove_delay (FU_DEVICE (self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_firmware_size (FU_DEVICE (self), FU_EP963_FIRMWARE_SIZE);
	fu_device_retry_set_delay (FU_DEVICE (self), 100);
}

static void
fu_ep963x_device_class_init (FuEp963xDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->prepare_firmware = fu_ep963x_device_prepare_firmware;
	klass_device->write_firmware = fu_ep963x_device_write_firmware;
	klass_device->attach = fu_ep963x_device_attach;
	klass_device->detach = fu_ep963x_device_detach;
	klass_device->setup = fu_ep963x_device_setup;
}
