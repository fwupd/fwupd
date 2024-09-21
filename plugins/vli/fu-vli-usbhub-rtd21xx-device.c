/*
 * Copyright 2017 VIA Corporation
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-vli-struct.h"
#include "fu-vli-usbhub-rtd21xx-device.h"

struct _FuVliUsbhubRtd21xxDevice {
	FuDevice parent_instance;
};

G_DEFINE_TYPE(FuVliUsbhubRtd21xxDevice, fu_vli_usbhub_rtd21xx_device, FU_TYPE_DEVICE)

#define I2C_WRITE_REQUEST 0xB2
#define I2C_READ_REQUEST  0xA5

#define I2C_DELAY_AFTER_SEND 5 /* ms */

#define UC_FOREGROUND_TARGET_ADDR     0x3A
#define UC_FOREGROUND_STATUS	      0x31
#define UC_FOREGROUND_OPCODE	      0x33
#define UC_FOREGROUND_ISP_DATA_OPCODE 0x34

#define ISP_DATA_BLOCKSIZE 30
#define ISP_PACKET_SIZE	   32

typedef enum {
	ISP_STATUS_BUSY = 0xBB,		/* host must wait for device */
	ISP_STATUS_IDLE_SUCCESS = 0x11, /* previous command was OK */
	ISP_STATUS_IDLE_FAILURE = 0x12, /* previous command failed */
} IspStatus;

typedef enum {
	ISP_CMD_ENTER_FW_UPDATE = 0x01,
	ISP_CMD_GET_PROJECT_ID_ADDR = 0x02,
	ISP_CMD_SYNC_IDENTIFY_CODE = 0x03,
	ISP_CMD_GET_FW_INFO = 0x04,
	ISP_CMD_FW_UPDATE_START = 0x05,
	ISP_CMD_FW_UPDATE_ISP_DONE = 0x06,
	ISP_CMD_FW_UPDATE_EXIT = 0x07,
	ISP_CMD_FW_UPDATE_RESET = 0x08,
} IspCmd;

static gboolean
fu_vli_usbhub_rtd21xx_device_i2c_write(FuVliUsbhubDevice *self,
				       guint8 target_addr,
				       guint8 sub_addr,
				       guint8 *data,
				       gsize datasz,
				       GError **error)
{
	gsize bufsz = datasz + 2;
	g_autofree guint8 *buf = g_malloc0(bufsz);

	buf[0] = target_addr;
	buf[1] = sub_addr;
	if (!fu_memcpy_safe(buf,
			    bufsz,
			    0x2, /* dst */
			    data,
			    datasz,
			    0x0, /* src */
			    datasz,
			    error))
		return FALSE;
	fu_dump_raw(G_LOG_DOMAIN, "I2cWriteData", buf, datasz + 2);
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    I2C_WRITE_REQUEST,
					    0x0000,
					    0x0000,
					    buf,
					    datasz + 2,
					    NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL,
					    error)) {
		g_prefix_error(error, "failed to write I2C @0x%02x:%02x: ", target_addr, sub_addr);
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), I2C_DELAY_AFTER_SEND);
	return TRUE;
}

static gboolean
fu_vli_usbhub_rtd21xx_device_i2c_read(FuVliUsbhubDevice *self,
				      guint8 target_addr,
				      guint8 sub_addr,
				      guint8 *data,
				      gsize datasz,
				      GError **error)
{
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_DEVICE_TO_HOST,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    I2C_READ_REQUEST,
					    0x0000,
					    ((guint16)sub_addr << 8) + target_addr,
					    data,
					    datasz,
					    NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL,
					    error)) {
		g_prefix_error(error, "failed to read I2C: ");
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "I2cReadData", data, datasz);
	return TRUE;
}

static gboolean
fu_vli_usbhub_rtd21xx_device_read_status_raw(FuVliUsbhubRtd21xxDevice *self,
					     guint8 *status,
					     GError **error)
{
	FuVliUsbhubDevice *parent = FU_VLI_USBHUB_DEVICE(fu_device_get_parent(FU_DEVICE(self)));
	guint8 buf[] = {0x00};
	if (!fu_vli_usbhub_rtd21xx_device_i2c_read(parent,
						   UC_FOREGROUND_TARGET_ADDR,
						   UC_FOREGROUND_STATUS,
						   buf,
						   sizeof(buf),
						   error))
		return FALSE;
	if (status != NULL)
		*status = buf[0];
	return TRUE;
}

static gboolean
fu_vli_usbhub_rtd21xx_device_read_status_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuVliUsbhubRtd21xxDevice *self = FU_VLI_USBHUB_RTD21XX_DEVICE(device);
	guint8 status = 0xfd;
	if (!fu_vli_usbhub_rtd21xx_device_read_status_raw(self, &status, error))
		return FALSE;
	if (status == ISP_STATUS_BUSY) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "status was 0x%02x", status);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_rtd21xx_device_read_status(FuVliUsbhubRtd21xxDevice *self,
					 guint8 *status,
					 GError **error)
{
	return fu_device_retry(FU_DEVICE(self),
			       fu_vli_usbhub_rtd21xx_device_read_status_cb,
			       4200,
			       status,
			       error);
}

static gboolean
fu_vli_usbhub_rtd21xx_device_ensure_version_unlocked(FuVliUsbhubRtd21xxDevice *self, GError **error)
{
	FuVliUsbhubDevice *parent = FU_VLI_USBHUB_DEVICE(fu_device_get_parent(FU_DEVICE(self)));
	guint8 buf_rep[7] = {0x00};
	guint8 buf_req[] = {ISP_CMD_GET_FW_INFO};
	g_autofree gchar *version = NULL;

	if (!fu_vli_usbhub_rtd21xx_device_i2c_write(parent,
						    UC_FOREGROUND_TARGET_ADDR,
						    UC_FOREGROUND_OPCODE,
						    buf_req,
						    sizeof(buf_req),
						    error)) {
		g_prefix_error(error, "failed to get version number: ");
		return FALSE;
	}

	/* wait for device ready */
	fu_device_sleep(FU_DEVICE(self), 300);
	if (!fu_vli_usbhub_rtd21xx_device_i2c_read(parent,
						   UC_FOREGROUND_TARGET_ADDR,
						   0x00,
						   buf_rep,
						   sizeof(buf_rep),
						   error)) {
		g_prefix_error(error, "failed to get version number: ");
		return FALSE;
	}

	/* set version */
	version = g_strdup_printf("%u.%u", buf_rep[1], buf_rep[2]);
	fu_device_set_version(FU_DEVICE(self), version);
	return TRUE;
}

static gboolean
fu_vli_usbhub_rtd21xx_device_setup(FuDevice *device, GError **error)
{
	FuVliUsbhubRtd21xxDevice *self = FU_VLI_USBHUB_RTD21XX_DEVICE(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* get version */
	locker = fu_device_locker_new_full(device,
					   (FuDeviceLockerFunc)fu_device_detach,
					   (FuDeviceLockerFunc)fu_device_attach,
					   error);
	if (locker == NULL)
		return FALSE;
	if (!fu_vli_usbhub_rtd21xx_device_ensure_version_unlocked(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_vli_usbhub_rtd21xx_device_detach_raw(FuVliUsbhubRtd21xxDevice *self, GError **error)
{
	FuVliUsbhubDevice *parent = FU_VLI_USBHUB_DEVICE(fu_device_get_parent(FU_DEVICE(self)));
	guint8 buf[] = {0x03};
	if (!fu_vli_usbhub_rtd21xx_device_i2c_write(parent, 0x6A, 0x31, buf, sizeof(buf), error)) {
		g_prefix_error(error, "failed to detach: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_rtd21xx_device_detach_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuVliUsbhubRtd21xxDevice *self = FU_VLI_USBHUB_RTD21XX_DEVICE(device);
	guint8 status = 0xfe;
	if (!fu_vli_usbhub_rtd21xx_device_detach_raw(self, error))
		return FALSE;
	if (!fu_vli_usbhub_rtd21xx_device_read_status_raw(self, &status, error))
		return FALSE;
	if (status != ISP_STATUS_IDLE_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "detach status was 0x%02x",
			    status);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_rtd21xx_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuVliUsbhubDevice *parent = FU_VLI_USBHUB_DEVICE(fu_device_get_parent(device));
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open device */
	locker = fu_device_locker_new(parent, error);
	if (locker == NULL)
		return FALSE;
	if (!fu_device_retry(device, fu_vli_usbhub_rtd21xx_device_detach_cb, 100, NULL, error))
		return FALSE;

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static gboolean
fu_vli_usbhub_rtd21xx_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuVliUsbhubDevice *parent = FU_VLI_USBHUB_DEVICE(fu_device_get_parent(device));
	guint8 buf[] = {ISP_CMD_FW_UPDATE_RESET};
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open device */
	locker = fu_device_locker_new(parent, error);
	if (locker == NULL)
		return FALSE;
	if (!fu_vli_usbhub_rtd21xx_device_i2c_write(parent,
						    UC_FOREGROUND_TARGET_ADDR,
						    UC_FOREGROUND_OPCODE,
						    buf,
						    sizeof(buf),
						    error)) {
		g_prefix_error(error, "failed to attach: ");
		return FALSE;
	}

	/* success */
	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static gboolean
fu_vli_usbhub_rtd21xx_device_write_firmware(FuDevice *device,
					    FuFirmware *firmware,
					    FuProgress *progress,
					    FwupdInstallFlags flags,
					    GError **error)
{
	FuVliUsbhubDevice *parent = FU_VLI_USBHUB_DEVICE(fu_device_get_parent(device));
	FuVliUsbhubRtd21xxDevice *self = FU_VLI_USBHUB_RTD21XX_DEVICE(device);
	guint32 project_addr;
	guint8 project_id_count;
	guint8 read_buf[10] = {0};
	guint8 write_buf[ISP_PACKET_SIZE] = {0};
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 10, "enable-isp");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 50, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 10, NULL);

	/* open device */
	locker = fu_device_locker_new(parent, error);
	if (locker == NULL)
		return FALSE;

	/* simple image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	/* enable ISP high priority */
	write_buf[0] = ISP_CMD_ENTER_FW_UPDATE;
	write_buf[1] = 0x01;
	if (!fu_vli_usbhub_rtd21xx_device_i2c_write(parent,
						    UC_FOREGROUND_TARGET_ADDR,
						    UC_FOREGROUND_OPCODE,
						    write_buf,
						    2,
						    error)) {
		g_prefix_error(error, "failed to enable ISP: ");
		return FALSE;
	}
	if (!fu_vli_usbhub_rtd21xx_device_read_status(self, NULL, error))
		return FALSE;

	/* get project ID address */
	write_buf[0] = ISP_CMD_GET_PROJECT_ID_ADDR;
	if (!fu_vli_usbhub_rtd21xx_device_i2c_write(parent,
						    UC_FOREGROUND_TARGET_ADDR,
						    UC_FOREGROUND_OPCODE,
						    write_buf,
						    1,
						    error)) {
		g_prefix_error(error, "failed to get project ID address: ");
		return FALSE;
	}

	/* read back 6 bytes data */
	fu_device_sleep(device, I2C_DELAY_AFTER_SEND * 40);
	if (!fu_vli_usbhub_rtd21xx_device_i2c_read(parent,
						   UC_FOREGROUND_TARGET_ADDR,
						   UC_FOREGROUND_STATUS,
						   read_buf,
						   6,
						   error)) {
		g_prefix_error(error, "failed to read project ID: ");
		return FALSE;
	}
	if (read_buf[0] != ISP_STATUS_IDLE_SUCCESS) {
		g_prefix_error(error, "failed project ID with error 0x%02x: ", read_buf[0]);
		return FALSE;
	}

	/* verify project ID */
	if (!fu_memread_uint32_safe(read_buf,
				    sizeof(read_buf),
				    0x1,
				    &project_addr,
				    G_BIG_ENDIAN,
				    error))
		return FALSE;
	project_id_count = read_buf[5];
	write_buf[0] = ISP_CMD_SYNC_IDENTIFY_CODE;
	if (!fu_input_stream_read_safe(stream,
				       write_buf,
				       sizeof(write_buf),
				       0x1,	     /* dst */
				       project_addr, /* src */
				       project_id_count,
				       error)) {
		g_prefix_error(error, "failed to write project ID from 0x%04x: ", project_addr);
		return FALSE;
	}
	if (!fu_vli_usbhub_rtd21xx_device_i2c_write(parent,
						    UC_FOREGROUND_TARGET_ADDR,
						    UC_FOREGROUND_OPCODE,
						    write_buf,
						    project_id_count + 1,
						    error)) {
		g_prefix_error(error, "failed to send fw update start cmd: ");
		return FALSE;
	}
	if (!fu_vli_usbhub_rtd21xx_device_read_status(self, NULL, error))
		return FALSE;

	/* background FW update start command */
	write_buf[0] = ISP_CMD_FW_UPDATE_START;
	fu_memwrite_uint16(write_buf + 1, ISP_DATA_BLOCKSIZE, G_BIG_ENDIAN);
	if (!fu_vli_usbhub_rtd21xx_device_i2c_write(parent,
						    UC_FOREGROUND_TARGET_ADDR,
						    UC_FOREGROUND_OPCODE,
						    write_buf,
						    3,
						    error)) {
		g_prefix_error(error, "failed to send fw update start cmd: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* send data */
	chunks = fu_chunk_array_new_from_stream(stream, 0x00, ISP_DATA_BLOCKSIZE, error);
	if (chunks == NULL)
		return FALSE;
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_vli_usbhub_rtd21xx_device_read_status(self, NULL, error))
			return FALSE;
		if (!fu_vli_usbhub_rtd21xx_device_i2c_write(parent,
							    UC_FOREGROUND_TARGET_ADDR,
							    UC_FOREGROUND_ISP_DATA_OPCODE,
							    fu_chunk_get_data_out(chk),
							    fu_chunk_get_data_sz(chk),
							    error)) {
			g_prefix_error(error,
				       "failed to write @0x%04x: ",
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}

		/* update progress */
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)i + 1,
						(gsize)fu_chunk_array_length(chunks));
	}
	fu_progress_step_done(progress);

	/* update finish command */
	if (!fu_vli_usbhub_rtd21xx_device_read_status(self, NULL, error))
		return FALSE;
	write_buf[0] = ISP_CMD_FW_UPDATE_ISP_DONE;
	if (!fu_vli_usbhub_rtd21xx_device_i2c_write(parent,
						    UC_FOREGROUND_TARGET_ADDR,
						    UC_FOREGROUND_OPCODE,
						    write_buf,
						    1,
						    error)) {
		g_prefix_error(error, "failed update finish cmd: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* exit background-fw mode */
	if (!fu_vli_usbhub_rtd21xx_device_read_status(self, NULL, error))
		return FALSE;
	write_buf[0] = ISP_CMD_FW_UPDATE_EXIT;
	if (!fu_vli_usbhub_rtd21xx_device_i2c_write(parent,
						    UC_FOREGROUND_TARGET_ADDR,
						    UC_FOREGROUND_OPCODE,
						    write_buf,
						    1,
						    error)) {
		g_prefix_error(error, "FwUpdate exit: ");
		return FALSE;
	}

	/* the device needs some time to restart with the new firmware before
	 * it can be queried again */
	fu_device_sleep_full(device, 20000, progress); /* ms */

	/* success */
	fu_progress_step_done(progress);
	return TRUE;
}

static gboolean
fu_vli_usbhub_rtd21xx_device_reload(FuDevice *device, GError **error)
{
	FuVliUsbhubDevice *parent = FU_VLI_USBHUB_DEVICE(fu_device_get_parent(device));
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open parent device */
	locker = fu_device_locker_new(parent, error);
	if (locker == NULL)
		return FALSE;
	return fu_vli_usbhub_rtd21xx_device_setup(device, error);
}

static gboolean
fu_vli_usbhub_rtd21xx_device_probe(FuDevice *device, GError **error)
{
	FuVliDeviceKind device_kind = FU_VLI_DEVICE_KIND_RTD21XX;
	FuVliUsbhubDevice *parent = FU_VLI_USBHUB_DEVICE(fu_device_get_parent(device));

	fu_device_set_name(device, fu_vli_device_kind_to_string(device_kind));
	fu_device_incorporate(device, FU_DEVICE(parent), FU_DEVICE_INCORPORATE_FLAG_PHYSICAL_ID);

	/* add instance ID */
	fu_device_add_instance_str(device, "I2C", fu_vli_device_kind_to_string(device_kind));
	return fu_device_build_instance_id(device, error, "USB", "VID", "PID", "I2C", NULL);
}

static void
fu_vli_usbhub_rtd21xx_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static void
fu_vli_usbhub_rtd21xx_device_init(FuVliUsbhubRtd21xxDevice *self)
{
	fu_device_add_icon(FU_DEVICE(self), "video-display");
	fu_device_add_protocol(FU_DEVICE(self), "com.vli.i2c");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_set_install_duration(FU_DEVICE(self), 100); /* seconds */
	fu_device_set_logical_id(FU_DEVICE(self), "I2C");
	fu_device_retry_set_delay(FU_DEVICE(self), 30); /* ms */
}

static void
fu_vli_usbhub_rtd21xx_device_class_init(FuVliUsbhubRtd21xxDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_vli_usbhub_rtd21xx_device_probe;
	device_class->setup = fu_vli_usbhub_rtd21xx_device_setup;
	device_class->reload = fu_vli_usbhub_rtd21xx_device_reload;
	device_class->attach = fu_vli_usbhub_rtd21xx_device_attach;
	device_class->detach = fu_vli_usbhub_rtd21xx_device_detach;
	device_class->write_firmware = fu_vli_usbhub_rtd21xx_device_write_firmware;
	device_class->set_progress = fu_vli_usbhub_rtd21xx_device_set_progress;
}

FuDevice *
fu_vli_usbhub_rtd21xx_device_new(FuVliUsbhubDevice *parent)
{
	FuVliUsbhubRtd21xxDevice *self =
	    g_object_new(FU_TYPE_VLI_USBHUB_RTD21XX_DEVICE, "parent", parent, NULL);
	return FU_DEVICE(self);
}
