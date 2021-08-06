/*
 * Copyright (C) 2021 Realtek Corporation
 * Copyright (C) 2021 Ricky Wu <ricky_wu@realtek.com> <spring1527@gmail.com>
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-rts54hub-rtd21xx-background.h"
#include "fu-rts54hub-device.h"

struct _FuRts54hubRtd21xxBackground
{
	FuRts54hubRtd21xxDevice	 parent_instance;
};

G_DEFINE_TYPE (FuRts54hubRtd21xxBackground, fu_rts54hub_rtd21xx_background, FU_TYPE_RTS54HUB_RTD21XX_DEVICE)

#define ISP_DATA_BLOCKSIZE		32
#define ISP_PACKET_SIZE			257

typedef enum {
	ISP_CMD_FW_UPDATE_START		= 0x01,
	ISP_CMD_FW_UPDATE_ISP_DONE	= 0x02,
	ISP_CMD_GET_FW_INFO		= 0x03,
	ISP_CMD_FW_UPDATE_EXIT		= 0x04,
	ISP_CMD_GET_PROJECT_ID_ADDR	= 0x05,
	ISP_CMD_SYNC_IDENTIFY_CODE	= 0x06,
} IspCmd;

static gboolean
fu_rts54hub_rtd21xx_ensure_version_unlocked (FuRts54hubRtd21xxBackground *self,
					     GError **error)
{
	guint8 buf_rep[7] = { 0x00 };
	guint8 buf_req[] = { ISP_CMD_GET_FW_INFO };
	g_autofree gchar *version = NULL;

	if (!fu_rts54hub_rtd21xx_device_i2c_write (FU_RTS54HUB_RTD21XX_DEVICE (self),
						   UC_ISP_SLAVE_ADDR,
						   UC_BACKGROUND_OPCODE,
						   buf_req, sizeof(buf_req),
						   error)) {
		g_prefix_error (error, "failed to get version number: ");
		return FALSE;
	}

	/* wait for device ready */
	g_usleep(300000);
	if (!fu_rts54hub_rtd21xx_device_i2c_read (FU_RTS54HUB_RTD21XX_DEVICE (self),
						  UC_ISP_SLAVE_ADDR,
						  0x00,
						  buf_rep, sizeof(buf_rep),
						  error)) {
		g_prefix_error (error, "failed to get version number: ");
		return FALSE;
	}

	/* set version */
	version = g_strdup_printf ("%u.%u", buf_rep[1], buf_rep[2]);
	fu_device_set_version (FU_DEVICE (self), version);

	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_background_detach_raw (FuRts54hubRtd21xxBackground *self, GError **error)
{
	guint8 buf = 0x02;
	if (!fu_rts54hub_rtd21xx_device_i2c_write (FU_RTS54HUB_RTD21XX_DEVICE (self), 0x6A, 0x31,
						   &buf, sizeof(buf), error)) {
		g_prefix_error (error, "failed to detach: ");
		return FALSE;
	}

	/* wait for device ready */
	g_usleep (300000);
	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_background_detach_cb (FuDevice *device,
					  gpointer user_data,
					  GError **error)
{
	FuRts54hubRtd21xxBackground *self = FU_RTS54HUB_RTD21XX_BACKGROUND (device);
	guint8 status = 0xfe;

	if (!fu_rts54hub_rtd21xx_background_detach_raw (self, error))
		return FALSE;
	if (!fu_rts54hub_rtd21xx_device_read_status_raw (FU_RTS54HUB_RTD21XX_DEVICE (self),
							 &status, error))
		return FALSE;
	if (status != ISP_STATUS_IDLE_SUCCESS) {
		g_set_error (error,
				FWUPD_ERROR,
				FWUPD_ERROR_INTERNAL,
				"detach status was 0x%02x", status);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_background_detach (FuDevice *device, GError **error)
{
	FuRts54HubDevice *parent = FU_RTS54HUB_DEVICE (fu_device_get_parent (device));
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open device */
	locker = fu_device_locker_new (parent, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_retry (device,
				fu_rts54hub_rtd21xx_background_detach_cb,
				100, NULL, error);
}

static gboolean
fu_rts54hub_rtd21xx_background_attach (FuDevice *device, GError **error)
{
	FuRts54HubDevice *parent = FU_RTS54HUB_DEVICE (fu_device_get_parent (device));
	FuRts54hubRtd21xxDevice *self = FU_RTS54HUB_RTD21XX_DEVICE (device);
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new();
	guint8 buf[] = { ISP_CMD_FW_UPDATE_EXIT };

	/* open device */
	locker = fu_device_locker_new (parent, error);
	if (locker == NULL)
		return FALSE;
	if (!fu_rts54hub_rtd21xx_device_i2c_write (self,
						   UC_ISP_SLAVE_ADDR,
						   UC_BACKGROUND_OPCODE,
						   buf, sizeof (buf),
						   error)) {
		g_prefix_error (error, "failed to attach: ");
		return FALSE;
	}
	fu_progress_sleep(progress, 1);

	/* success */
	fu_device_remove_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_background_setup (FuDevice *device, GError **error)
{
	FuRts54hubRtd21xxBackground *self = FU_RTS54HUB_RTD21XX_BACKGROUND (device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* get version */
	locker = fu_device_locker_new_full (device,
					    (FuDeviceLockerFunc) fu_device_detach,
					    (FuDeviceLockerFunc) fu_device_attach,
					    error);
	if (locker == NULL)
		return FALSE;
	if (!fu_rts54hub_rtd21xx_ensure_version_unlocked (self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_background_reload (FuDevice *device, GError **error)
{
	FuRts54HubDevice *parent = FU_RTS54HUB_DEVICE (fu_device_get_parent (device));
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open parent device */
	locker = fu_device_locker_new (parent, error);
	if (locker == NULL)
		return FALSE;
	return fu_rts54hub_rtd21xx_background_setup (device, error);
}

static gboolean
fu_rts54hub_rtd21xx_background_write_firmware(FuDevice *device,
					      FuFirmware *firmware,
					      FuProgress *progress,
					      FwupdInstallFlags flags,
					      GError **error)
{
	FuRts54hubRtd21xxBackground *self = FU_RTS54HUB_RTD21XX_BACKGROUND (device);
	const guint8 *fwbuf;
	gsize fwbufsz = 0;
	guint32 project_addr;
	guint8 project_id_count;
	guint8 read_buf[10] = { 0x0 };
	guint8 write_buf[ISP_PACKET_SIZE] = { 0x0 };
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* open device */
	locker = fu_device_locker_new (self, error);
	if (locker == NULL)
		return FALSE;

	/* simple image */
	fw = fu_firmware_get_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;
	fwbuf = g_bytes_get_data (fw, &fwbufsz);

	/* get project ID address */
	write_buf[0] = ISP_CMD_GET_PROJECT_ID_ADDR;
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_READ);
	if (!fu_rts54hub_rtd21xx_device_i2c_write (FU_RTS54HUB_RTD21XX_DEVICE (self),
						   UC_ISP_SLAVE_ADDR,
						   UC_BACKGROUND_OPCODE,
						   write_buf, 1,
						   error)) {
		g_prefix_error (error, "failed to get project ID address: ");
		return FALSE;
	}

	/* read back 6 bytes data */
	g_usleep (I2C_DELAY_AFTER_SEND * 40);
	if (!fu_rts54hub_rtd21xx_device_i2c_read (FU_RTS54HUB_RTD21XX_DEVICE (self),
						  UC_ISP_SLAVE_ADDR,
						  0x00,
						  read_buf, 6,
						  error)) {
		g_prefix_error (error, "failed to read project ID: ");
		return FALSE;
	}
	if (read_buf[0] != ISP_STATUS_IDLE_SUCCESS) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			     "failed project ID with error 0x%02x: ",
			     read_buf[0]);
		return FALSE;
	}

	/* verify project ID */
	project_addr = fu_common_read_uint32 (read_buf + 1, G_BIG_ENDIAN);
	project_id_count = read_buf[5];
	write_buf[0] = ISP_CMD_SYNC_IDENTIFY_CODE;
	if (!fu_memcpy_safe (write_buf, sizeof(write_buf), 0x1, /* dst */
			     fwbuf, fwbufsz, project_addr,	/* src */
			     project_id_count, error)) {
		g_prefix_error (error,
				"failed to write project ID from 0x%04x: ",
				project_addr);
		return FALSE;
	}
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_BUSY);
	if (!fu_rts54hub_rtd21xx_device_i2c_write (FU_RTS54HUB_RTD21XX_DEVICE (self),
						   UC_ISP_SLAVE_ADDR,
						   UC_BACKGROUND_OPCODE,
						   write_buf,
						   project_id_count + 1,
						   error)) {
		g_prefix_error (error, "failed to send fw update start cmd: ");
		return FALSE;
	}
	if (!fu_rts54hub_rtd21xx_device_read_status (FU_RTS54HUB_RTD21XX_DEVICE (self),
						     NULL, error))
		return FALSE;

	/* background FW update start command */
	write_buf[0] = ISP_CMD_FW_UPDATE_START;
	fu_common_write_uint16 (write_buf + 1, ISP_DATA_BLOCKSIZE, G_BIG_ENDIAN);
	if (!fu_rts54hub_rtd21xx_device_i2c_write (FU_RTS54HUB_RTD21XX_DEVICE (self),
						   UC_ISP_SLAVE_ADDR,
						   UC_BACKGROUND_OPCODE,
						   write_buf, 3,
						   error)) {
		g_prefix_error (error, "failed to send fw update start cmd: ");
		return FALSE;
	}

	/* send data */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	chunks = fu_chunk_array_new_from_bytes (fw,
						0x00,	/* start addr */
						0x00,	/* page_sz */
						ISP_DATA_BLOCKSIZE);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		if (!fu_rts54hub_rtd21xx_device_read_status (FU_RTS54HUB_RTD21XX_DEVICE (self),
							     NULL, error))
			return FALSE;
		if (!fu_rts54hub_rtd21xx_device_i2c_write (FU_RTS54HUB_RTD21XX_DEVICE (self),
							   UC_ISP_SLAVE_ADDR,
							   UC_BACKGROUND_ISP_DATA_OPCODE,
							   fu_chunk_get_data (chk),
							   fu_chunk_get_data_sz (chk),
							   error)) {
			g_prefix_error (error,
					"failed to write @0x%04x: ",
					fu_chunk_get_address (chk));
			return FALSE;
		}

		/* update progress */
		fu_progress_set_percentage_full(progress, (gsize)i, (gsize)chunks->len);
	}

	/* update finish command */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_BUSY);
	if (!fu_rts54hub_rtd21xx_device_read_status (FU_RTS54HUB_RTD21XX_DEVICE (self),
						     NULL, error))
		return FALSE;
	write_buf[0] = ISP_CMD_FW_UPDATE_ISP_DONE;
	if (!fu_rts54hub_rtd21xx_device_i2c_write (FU_RTS54HUB_RTD21XX_DEVICE (self),
						   UC_ISP_SLAVE_ADDR,
						   UC_BACKGROUND_OPCODE,
						   write_buf, 1,
						   error)) {
		g_prefix_error (error, "failed update finish cmd: ");
		return FALSE;
	}

	/* exit fw mode */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	fu_progress_set_percentage(progress, 0);
	if (!fu_rts54hub_rtd21xx_device_read_status (FU_RTS54HUB_RTD21XX_DEVICE (self),
						     NULL, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_rts54hub_rtd21xx_background_init (FuRts54hubRtd21xxBackground *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
}

static void
fu_rts54hub_rtd21xx_background_class_init (FuRts54hubRtd21xxBackgroundClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->setup = fu_rts54hub_rtd21xx_background_setup;
	klass_device->reload = fu_rts54hub_rtd21xx_background_reload;
	klass_device->attach = fu_rts54hub_rtd21xx_background_attach;
	klass_device->detach = fu_rts54hub_rtd21xx_background_detach;
	klass_device->write_firmware = fu_rts54hub_rtd21xx_background_write_firmware;
}
