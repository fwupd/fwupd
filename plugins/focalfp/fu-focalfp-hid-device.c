/*
 * Copyright 2022 Shihwei Huang <shihwei.huang@focaltech-electronics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-focalfp-firmware.h"
#include "fu-focalfp-hid-device.h"

struct _FuFocalfpHidDevice {
	FuHidrawDevice parent_instance;
};

G_DEFINE_TYPE(FuFocalfpHidDevice, fu_focalfp_hid_device, FU_TYPE_HIDRAW_DEVICE)

#define CMD_ENTER_UPGRADE_MODE	       0x40
#define CMD_CHECK_CURRENT_STATE	       0x41
#define CMD_READY_FOR_UPGRADE	       0x42
#define CMD_SEND_DATA		       0x43
#define CMD_UPGRADE_CHECKSUM	       0x44
#define CMD_EXIT_UPGRADE_MODE	       0x45
#define CMD_USB_READ_UPGRADE_ID	       0x46
#define CMD_USB_ERASE_FLASH	       0x47
#define CMD_USB_BOOT_READ	       0x48
#define CMD_USB_BOOT_BOOTLOADERVERSION 0x49
#define CMD_READ_REGISTER	       0x50
#define CMD_WRITE_REGISTER	       0x51
#define CMD_ACK			       0xf0
#define CMD_NACK		       0xff

#define FIRST_PACKET	    0x00
#define MID_PACKET	    0x01
#define END_PACKET	    0x02
#define REPORT_SIZE	    64
#define MAX_USB_PACKET_SIZE 56

static gboolean
fu_focalfp_hid_device_detach(FuDevice *device, FuProgress *progress, GError **error);

static gboolean
fu_focalfp_hid_device_probe(FuDevice *device, GError **error)
{
	/* check is valid */
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "hidraw") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "is not correct subsystem=%s, expected hidraw",
			    fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)));
		return FALSE;
	}

	/* i2c-hid */
	if (fu_device_get_pid(device) != 0x0106) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "not i2c-hid touchpad");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static guint8
fu_focalfp_hid_device_generate_checksum(const guint8 *buf, gsize bufsz)
{
	guint8 checksum = 0;
	for (gsize i = 0; i < bufsz; i++)
		checksum ^= buf[i];
	checksum++;
	return checksum;
}

static gboolean
fu_focalfp_hid_device_io(FuFocalfpHidDevice *self,
			 guint8 *wbuf,
			 gsize wbufsz,
			 guint8 *rbuf,
			 gsize rbufsz,
			 GError **error)
{
	/* SetReport */
	if (wbuf != NULL && wbufsz > 0) {
		guint8 buf[64] = {0x06, 0xff, 0xff};
		guint8 cmdlen = 4 + wbufsz;
		buf[3] = cmdlen;
		if (!fu_memcpy_safe(buf, sizeof(buf), 0x04, wbuf, wbufsz, 0x00, wbufsz, error))
			return FALSE;
		buf[cmdlen] = fu_focalfp_hid_device_generate_checksum(&buf[1], cmdlen - 1);
		if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
						  buf,
						  sizeof(buf),
						  FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
						  error)) {
			return FALSE;
		}
	}

	/* GetReport */
	if (rbuf != NULL && rbufsz > 0) {
		guint8 buf[64] = {0x06};
		if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
						  buf,
						  sizeof(buf),
						  FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
						  error)) {
			return FALSE;
		}
		if (!fu_memcpy_safe(rbuf, rbufsz, 0x0, buf, sizeof(buf), 0x00, rbufsz, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_focalfp_hid_device_check_cmd_crc(const guint8 *buf, gsize bufsz, guint8 cmd, GError **error)
{
	guint8 csum = 0;
	guint8 csum_actual;

	/* check was correct response */
	if (buf[4] != cmd) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "got cmd 0x%02x, expected 0x%02x",
			    buf[4],
			    cmd);
		return FALSE;
	}

	/* check crc */
	if (!fu_memread_uint8_safe(buf, bufsz, buf[3], &csum, error))
		return FALSE;
	csum_actual = fu_focalfp_hid_device_generate_checksum(buf + 1, buf[3] - 1);
	if (csum != csum_actual) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "got checksum 0x%02x, expected 0x%02x",
			    csum,
			    csum_actual);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_focalfp_hid_device_read_reg_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuFocalfpHidDevice *self = FU_FOCALFP_HID_DEVICE(device);
	guint8 buf[64] = {0x0};
	guint8 *val = (guint8 *)user_data;

	if (!fu_focalfp_hid_device_io(self, NULL, 0, buf, 8, error))
		return FALSE;

	/* check was correct response */
	if (!fu_focalfp_hid_device_check_cmd_crc(buf, sizeof(buf), CMD_READ_REGISTER, error))
		return FALSE;

	/* success */
	*val = buf[6];
	return TRUE;
}

static gboolean
fu_focalfp_hid_device_read_reg(FuFocalfpHidDevice *self,
			       guint8 reg_address,
			       guint8 *val, /* out */
			       GError **error)
{
	guint8 buf[64] = {CMD_READ_REGISTER, reg_address};

	/* write */
	if (!fu_focalfp_hid_device_io(self, buf, 2, NULL, 0, error))
		return FALSE;

	/* read */
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_focalfp_hid_device_read_reg_cb,
				    5,
				    1 /* ms */,
				    val,
				    error);
}

/* enter upgrade mode */
static gboolean
fu_focalfp_hid_device_enter_upgrade_mode(FuFocalfpHidDevice *self, GError **error)
{
	guint8 wbuf[64] = {CMD_ENTER_UPGRADE_MODE};
	guint8 rbuf[64] = {0x0};

	if (!fu_focalfp_hid_device_io(self, wbuf, 1, rbuf, 6, error)) {
		g_prefix_error(error, "failed to CMD_ENTER_UPGRADE_MODE: ");
		return FALSE;
	}

	/* check was correct response */
	return fu_focalfp_hid_device_check_cmd_crc(rbuf, sizeof(rbuf), CMD_ACK, error);
}

/* get bootloader current state */
static gboolean
fu_focalfp_hid_device_check_current_state(FuFocalfpHidDevice *self, guint8 *val, GError **error)
{
	guint8 wbuf[64] = {CMD_CHECK_CURRENT_STATE};
	guint8 rbuf[64] = {0x0};

	if (!fu_focalfp_hid_device_io(self, wbuf, 1, rbuf, 7, error))
		return FALSE;

	/* check was correct response */
	if (!fu_focalfp_hid_device_check_cmd_crc(rbuf,
						 sizeof(rbuf),
						 CMD_CHECK_CURRENT_STATE,
						 error))
		return FALSE;

	/* success */
	*val = rbuf[5];
	return TRUE;
}

static gboolean
fu_focalfp_hid_device_wait_for_upgrade_ready_cb(FuDevice *device,
						gpointer user_data,
						GError **error)
{
	FuFocalfpHidDevice *self = FU_FOCALFP_HID_DEVICE(device);
	guint8 wbuf[64] = {CMD_READY_FOR_UPGRADE};
	guint8 rbuf[64] = {0x0};

	if (!fu_focalfp_hid_device_io(self, wbuf, 1, rbuf, 7, error))
		return FALSE;

	/* check was correct response */
	return fu_focalfp_hid_device_check_cmd_crc(rbuf,
						   sizeof(rbuf),
						   CMD_READY_FOR_UPGRADE,
						   error);
}

/* wait for ready */
static gboolean
fu_focalfp_hid_device_wait_for_upgrade_ready(FuFocalfpHidDevice *self,
					     guint retries,
					     GError **error)
{
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_focalfp_hid_device_wait_for_upgrade_ready_cb,
				    retries,
				    500,
				    NULL,
				    error);
}

static gboolean
fu_focalfp_hid_device_read_update_id_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuFocalfpHidDevice *self = FU_FOCALFP_HID_DEVICE(device);
	guint16 *us_ic_id = (guint16 *)user_data;
	guint8 wbuf[64] = {CMD_USB_READ_UPGRADE_ID};
	guint8 rbuf[64] = {0x0};

	if (!fu_focalfp_hid_device_io(self, wbuf, 1, rbuf, 8, error))
		return FALSE;

	/* check was correct response */
	if (!fu_focalfp_hid_device_check_cmd_crc(rbuf,
						 sizeof(rbuf),
						 CMD_USB_READ_UPGRADE_ID,
						 error))
		return FALSE;

	/* success */
	*us_ic_id = fu_memread_uint16(rbuf + 5, G_BIG_ENDIAN);
	return TRUE;
}

/* get bootload id */
static gboolean
fu_focalfp_hid_device_read_update_id(FuFocalfpHidDevice *self, guint16 *us_ic_id, GError **error)
{
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_focalfp_hid_device_read_update_id_cb,
				    10,
				    1 /* ms */,
				    us_ic_id,
				    error);
}

/* erase flash */
static gboolean
fu_focalfp_hid_device_erase_flash(FuFocalfpHidDevice *self, GError **error)
{
	guint8 wbuf[64] = {CMD_USB_ERASE_FLASH};
	guint8 rbuf[64] = {0x0};

	if (!fu_focalfp_hid_device_io(self, wbuf, 1, rbuf, 6, error))
		return FALSE;

	/* check was correct response */
	return fu_focalfp_hid_device_check_cmd_crc(rbuf, sizeof(rbuf), CMD_ACK, error);
}

static gboolean
fu_focalfp_hid_device_send_data_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuFocalfpHidDevice *self = FU_FOCALFP_HID_DEVICE(device);
	guint8 rbuf[64] = {0x0};

	if (!fu_focalfp_hid_device_io(self, NULL, 0, rbuf, 7, error))
		return FALSE;

	/* check was correct response */
	return fu_focalfp_hid_device_check_cmd_crc(rbuf, sizeof(rbuf), CMD_ACK, error);
}

/* send write data */
static gboolean
fu_focalfp_hid_device_send_data(FuFocalfpHidDevice *self,
				guint8 packet_type,
				const guint8 *buf,
				guint8 bufsz,
				GError **error)
{
	guint8 wbuf[64] = {CMD_SEND_DATA, packet_type};

	/* sanity check */
	if (bufsz > REPORT_SIZE - 8) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "data length 0x%x invalid",
			    bufsz);
		return FALSE;
	}

	if (!fu_memcpy_safe(wbuf, sizeof(wbuf), 0x02, buf, bufsz, 0x00, bufsz, error))
		return FALSE;
	if (!fu_focalfp_hid_device_io(self, wbuf, bufsz + 2, NULL, 0, error))
		return FALSE;

	return fu_device_retry_full(FU_DEVICE(self),
				    fu_focalfp_hid_device_send_data_cb,
				    4,
				    1 /* ms */,
				    NULL,
				    error);
}

/* get checksum for write done */
static gboolean
fu_focalfp_hid_device_checksum_upgrade(FuFocalfpHidDevice *self, guint32 *val, GError **error)
{
	guint8 wbuf[64] = {CMD_UPGRADE_CHECKSUM};
	guint8 rbuf[64] = {0x0};

	if (!fu_focalfp_hid_device_io(self, wbuf, 1, rbuf, 7 + 3, error))
		return FALSE;

	/* check was correct response */
	if (!fu_focalfp_hid_device_check_cmd_crc(rbuf, sizeof(rbuf), CMD_UPGRADE_CHECKSUM, error))
		return FALSE;

	/* success */
	return fu_memread_uint32_safe(rbuf, sizeof(rbuf), 0x05, val, G_LITTLE_ENDIAN, error);
}

static gboolean
fu_focalfp_hid_device_setup(FuDevice *device, GError **error)
{
	FuFocalfpHidDevice *self = FU_FOCALFP_HID_DEVICE(device);
	guint8 buf[2] = {0x0};

	/* get current firmware version */
	if (!fu_focalfp_hid_device_read_reg(self, 0xA6, buf, error)) {
		g_prefix_error(error, "failed to read version1: ");
		return FALSE;
	}
	if (!fu_focalfp_hid_device_read_reg(self, 0xAD, buf + 1, error)) {
		g_prefix_error(error, "failed to read version2: ");
		return FALSE;
	}
	fu_device_set_version_raw(device, fu_memread_uint16(buf, G_BIG_ENDIAN));

	/* success */
	return TRUE;
}

static gboolean
fu_focalfp_hid_device_write_chunks(FuFocalfpHidDevice *self,
				   FuChunkArray *chunks,
				   FuProgress *progress,
				   GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		guint8 uc_packet_type = MID_PACKET;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (i == 0)
			uc_packet_type = FIRST_PACKET;
		else if (i == fu_chunk_array_length(chunks) - 1)
			uc_packet_type = END_PACKET;

		if (!fu_focalfp_hid_device_send_data(self,
						     uc_packet_type,
						     fu_chunk_get_data(chk),
						     fu_chunk_get_data_sz(chk),
						     error)) {
			g_prefix_error(error, "failed to write chunk %u: ", i);
			return FALSE;
		}
		if (!fu_focalfp_hid_device_wait_for_upgrade_ready(self, 100, error)) {
			g_prefix_error(error, "failed to wait for chunk %u: ", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_focalfp_hid_device_write_firmware(FuDevice *device,
				     FuFirmware *firmware,
				     FuProgress *progress,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuFocalfpHidDevice *self = FU_FOCALFP_HID_DEVICE(device);
	const guint32 UPGRADE_ID = 0x582E;
	guint16 us_ic_id = 0;
	guint32 checksum = 0;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 89, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 89, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 10, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "reset");

	/* simple image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	/* check chip id and erase flash */
	if (!fu_focalfp_hid_device_wait_for_upgrade_ready(self, 6, error))
		return FALSE;
	if (!fu_focalfp_hid_device_read_update_id(self, &us_ic_id, error))
		return FALSE;
	if (us_ic_id != UPGRADE_ID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "got us_ic_id 0x%02x, expected 0x%02x",
			    us_ic_id,
			    (guint)UPGRADE_ID);
		return FALSE;
	}
	if (!fu_focalfp_hid_device_erase_flash(self, error))
		return FALSE;
	fu_device_sleep(device, 1000);
	if (!fu_focalfp_hid_device_wait_for_upgrade_ready(self, 20, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* send packet data */
	chunks = fu_chunk_array_new_from_stream(stream, 0x0, MAX_USB_PACKET_SIZE, error);
	if (chunks == NULL)
		return FALSE;
	if (!fu_focalfp_hid_device_write_chunks(self,
						chunks,
						fu_progress_get_child(progress),
						error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write flash end and check ready (fw calculate checksum) */
	fu_device_sleep(device, 50);
	if (!fu_focalfp_hid_device_wait_for_upgrade_ready(self, 5, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify checksum */
	if (!fu_focalfp_hid_device_checksum_upgrade(self, &checksum, error))
		return FALSE;
	if (checksum != fu_focalfp_firmware_get_checksum(FU_FOCALFP_FIRMWARE(firmware))) {
		fu_device_sleep(device, 500);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "device checksum invalid, got 0x%02x, expected 0x%02x",
			    checksum,
			    fu_focalfp_firmware_get_checksum(FU_FOCALFP_FIRMWARE(firmware)));
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

/* called after attach, but only when the firmware has been updated */
static gboolean
fu_focalfp_hid_device_reload(FuDevice *device, GError **error)
{
	FuFocalfpHidDevice *self = FU_FOCALFP_HID_DEVICE(device);
	guint8 idbuf[2] = {0x0};

	fu_device_sleep(device, 500);
	if (!fu_focalfp_hid_device_read_reg(self, 0x9F, &idbuf[0], error))
		return FALSE;
	if (!fu_focalfp_hid_device_read_reg(self, 0xA3, &idbuf[1], error))
		return FALSE;
	g_debug("id1=%x, id2=%x", idbuf[1], idbuf[0]);
	if (idbuf[1] != 0x58 && idbuf[0] != 0x22) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "firmware id invalid, got 0x%02x:0x%02x, expected 0x%02x:0x%02x",
			    idbuf[1],
			    idbuf[0],
			    (guint)0x58,
			    (guint)0x22);
		return FALSE;
	}

	return fu_focalfp_hid_device_setup(device, error);
}

static gboolean
fu_focalfp_hid_device_detach_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuFocalfpHidDevice *self = FU_FOCALFP_HID_DEVICE(device);
	guint8 uc_mode = 0;

	if (!fu_focalfp_hid_device_enter_upgrade_mode(self, error)) {
		g_prefix_error(error, "failed to enter upgrade mode: ");
		return FALSE;
	}

	/* get current state */
	if (!fu_focalfp_hid_device_check_current_state(self, &uc_mode, error))
		return FALSE;

	/* 1: upgrade mode; 2: fw mode */
	if (uc_mode != 1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "got uc_mode 0x%02x, expected 0x%02x",
			    uc_mode,
			    (guint)1);
		return FALSE;
	}

	/* success */
	return TRUE;
}

/* enter upgrade mode */
static gboolean
fu_focalfp_hid_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuFocalfpHidDevice *self = FU_FOCALFP_HID_DEVICE(device);
	guint8 wbuf[64] = {CMD_ENTER_UPGRADE_MODE};
	guint8 rbuf[64] = {0x0};

	/* command to go from APP --> Bootloader -- but we do not check crc */
	if (!fu_focalfp_hid_device_io(self, wbuf, 1, rbuf, 6, error)) {
		g_prefix_error(error, "failed to CMD_ENTER_UPGRADE_MODE: ");
		return FALSE;
	}
	fu_device_sleep(device, 200);

	/* second command : bootloader normal mode --> bootloader upgrade mode */
	if (!fu_device_retry_full(device,
				  fu_focalfp_hid_device_detach_cb,
				  3,
				  200 /* ms */,
				  progress,
				  error))
		return FALSE;

	/* success */
	fu_device_sleep(device, 200);
	return TRUE;
}

/* exit upgrade mode */
static gboolean
fu_focalfp_hid_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuFocalfpHidDevice *self = FU_FOCALFP_HID_DEVICE(device);
	guint8 wbuf[64] = {CMD_EXIT_UPGRADE_MODE};
	guint8 rbuf[64] = {0x0};

	if (!fu_focalfp_hid_device_io(self, wbuf, 1, rbuf, 6, error))
		return FALSE;

	/* check was correct response */
	if (!fu_focalfp_hid_device_check_cmd_crc(rbuf, sizeof(rbuf), CMD_ACK, error))
		return FALSE;

	/* success */
	fu_device_sleep(device, 500);
	return TRUE;
}

static void
fu_focalfp_hid_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gchar *
fu_focalfp_hid_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint16(version_raw, fu_device_get_version_format(device));
}

static void
fu_focalfp_hid_device_init(FuFocalfpHidDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_firmware_size(FU_DEVICE(self), 0x1E000);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_FOCALFP_FIRMWARE);
	fu_device_set_summary(FU_DEVICE(self), "Forcepad");
	fu_device_add_icon(FU_DEVICE(self), "input-touchpad");
	fu_device_add_protocol(FU_DEVICE(self), "tw.com.focalfp");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_NONBLOCK);
}

static void
fu_focalfp_hid_device_class_init(FuFocalfpHidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->attach = fu_focalfp_hid_device_attach;
	device_class->detach = fu_focalfp_hid_device_detach;
	device_class->setup = fu_focalfp_hid_device_setup;
	device_class->reload = fu_focalfp_hid_device_reload;
	device_class->write_firmware = fu_focalfp_hid_device_write_firmware;
	device_class->probe = fu_focalfp_hid_device_probe;
	device_class->set_progress = fu_focalfp_hid_device_set_progress;
	device_class->convert_version = fu_focalfp_hid_device_convert_version;
}
