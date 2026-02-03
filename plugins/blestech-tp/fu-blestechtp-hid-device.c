/*
 * Copyright 2026 SHENZHEN Betterlife Electronic CO.LTD <xiaomaoting@blestech.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-blestechtp-firmware.h"
#include "fu-blestechtp-hid-device.h"
#include "fu-blestechtp-struct.h"

struct _FuBlestechtpHidDevice {
	FuHidrawDevice parent_instance;
};

G_DEFINE_TYPE(FuBlestechtpHidDevice, fu_blestechtp_hid_device, FU_TYPE_HIDRAW_DEVICE)

static guint8
fu_blestechtp_calc_checksum(const guint8 *buf, 
			gsize bufsz)
{
	guint8 checksum = 0;
    /* invalid size */
	if (bufsz > FW_PAGE_SIZE)
        return 0;

	for (guint16 i = 0; i < bufsz; i++) {
		checksum ^= buf[i];
	}
	return checksum+1;
}

static gboolean
fu_blestechtp_hid_device_write_read(FuBlestechtpHidDevice *self,
			 guint8 *wbuf,
			 gsize wbufsz,
			 guint8 *rbuf,
			 gsize rbufsz,
			 guint16 delay,
			 GError **error)
{
	/* SetReport */
	if (wbuf != NULL && wbufsz > 0) {
		guint8 write_buf[33];
		memset(write_buf, 0x00, sizeof(write_buf));
		/* report id */
		write_buf[0] = REPORT_ID;
		/* pack len */
		write_buf[1] = wbufsz + SET_REPORT_PACK_FIX_SIZE;
		/* write len */
		write_buf[4] = (wbufsz >> 8) & 0xff;
		write_buf[5] = wbufsz & 0xff;
		/* read len */
		write_buf[6] = (rbufsz >> 8) & 0xff;
		write_buf[7] = rbufsz & 0xff;
		if (!fu_memcpy_safe(write_buf, sizeof(write_buf), 0x08, 
				wbuf, wbufsz, 0x00, wbufsz, error))
			return FALSE;
		
		/* checksum */
		write_buf[2] = fu_blestechtp_calc_checksum(
					write_buf + 3, write_buf[1] -1);

		if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
						  write_buf,
						  sizeof(write_buf),
						  FU_IOCTL_FLAG_NONE,
						  error)) {
			return FALSE;
		}
	}

	if (delay > 0)
		fu_device_sleep(FU_DEVICE(self), delay);

	/* GetReport */
	if (rbuf != NULL && rbufsz > 0) {
		guint8 read_buf[34];
		memset(read_buf, 0, sizeof(read_buf));
        read_buf[0] = REPORT_ID;
		if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
						 read_buf,
						  sizeof(read_buf),
						  FU_IOCTL_FLAG_NONE,
						  error)) {
			return FALSE;
		}

		if (!fu_memcpy_safe(rbuf, rbufsz, 0x0, read_buf, sizeof(read_buf), 
					0x04, rbufsz, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_blestechtp_hid_device_get_ver(FuBlestechtpHidDevice *self,
					guint8* fw_ver,
					GError **error) 
{
	guint8 fw_cmd = FU_BLESTECHTP_CMD_GET_FW_VER;
	if (!fu_blestechtp_hid_device_write_read(self,
			&fw_cmd, 1,
			fw_ver, FW_VER_LEN, 
			0, error)) {
		g_prefix_error_literal(error, "read version fail: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean 
fu_blestechtp_hid_device_update_start(FuBlestechtpHidDevice *self, 
					GError **error) 
{
	guint8 start_cmd[] = {FU_BLESTECHTP_CMD_UPDATE_START, 
			0x75, 0x65, 0x55, 0x45, 0x63, 0x75, 0x69, 0x33};
	guint8 ret_buff[2];
	if (!fu_blestechtp_hid_device_write_read(self,
				start_cmd, sizeof(start_cmd),
				ret_buff, sizeof(ret_buff), 
				10, error)) 
		return FALSE;
	/* success */
	return TRUE;
}

static gboolean
fu_blestechtp_hid_device_switch_boot(FuBlestechtpHidDevice *self, 
					GError **error) 
{
	guint8 cmd_switch_boot[] = {0xFF, 0xFF, 0x5A, 0xA5};
	guint8 fw_ver[FW_VER_LEN];

	if (!fu_blestechtp_hid_device_write_read(self,
				cmd_switch_boot, sizeof(cmd_switch_boot),
				NULL, 0, 
				0, error)) 
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 50);
	if(!fu_blestechtp_hid_device_get_ver(self, fw_ver, error)) {
		return FALSE;
	}
	/* check whether switch boot success */
	if(fw_ver[1] >= 0xC0 && fw_ver[1] <= 0xD0) {
		return TRUE;
	}
	else {
		g_set_error(error,
			FWUPD_ERROR,
			FWUPD_ERROR_NOT_SUPPORTED,
			"Not expected boot ver:%d", 
			fw_ver[1]
		);
		return FALSE;
	}
}

static gboolean
fu_blestechtp_hid_device_program_page_cb(FuDevice *device, 
					gpointer user_data, 
					GError **error)
{
	FuBlestechtpHidDevice *self = FU_BLESTECHTP_HID_DEVICE(device);	
	guint8 cmd_buf[3] = {FU_BLESTECHTP_CMD_PROGRAM_PAGE_END, 0x00, 0x00};
	guint8 read_buf[3] = { 0 };
	guint8 checksum = 0;
	guint16 page = 0;
	FuChunk *chk = (FuChunk *)user_data;
	gsize chk_sz = fu_chunk_get_data_sz(chk);
	const guint8* chk_data = fu_chunk_get_data(chk);
	guint64 addr = fu_chunk_get_address(chk);
	/* program 512 bytes */
	for (guint16 i = 0; i < chk_sz; i+= PROGRAM_PACK_LEN) {
		guint8 pack_data[PROGRAM_PACK_LEN+1];
		gsize pack_len;
		pack_data[0] = FU_BLESTECHTP_CMD_PROGRAM_PAGE;
		pack_len = MIN(PROGRAM_PACK_LEN, chk_sz-i); 
		if(!fu_memcpy_safe(pack_data, sizeof(pack_data), 1, 
					   chk_data, chk_sz, i, pack_len, error))
			return FALSE;

		if (!fu_blestechtp_hid_device_write_read(self, 
					pack_data, pack_len+1,
					NULL, 0, 
					0, error)) {
			g_prefix_error(error, "addr:%08X program failed: ", (guint16)(addr + i * chk_sz));
			return FALSE;
		}

		fu_device_sleep(FU_DEVICE(self), 1);
	}

	checksum = fu_blestechtp_calc_checksum(chk_data, chk_sz);
	page = addr / chk_sz;
	cmd_buf[1] = page;
	cmd_buf[2] = (page >> 8) & 0xff;
	if (!fu_blestechtp_hid_device_write_read(self,
				 cmd_buf, sizeof(cmd_buf),
				 read_buf, sizeof(read_buf), 
				 30, error)) {
		return FALSE;
	}
	/* check whether checksum is matched */
	if(read_buf[1] == checksum) {
		return TRUE;
	}
	else {
		g_set_error(error,
			FWUPD_ERROR,
			FWUPD_ERROR_WRITE,
			"checksum error: act:%04X, exp:%04X", 
			read_buf[1], checksum
		);
		return FALSE;
	}
}

static gboolean
fu_blestechtp_hid_device_program_chunk(FuBlestechtpHidDevice *self,
					FuChunkArray *chunks,
					guint chk_idx,
					GError **error)
{					
	g_autoptr(FuChunk) chk = NULL;
	chk = fu_chunk_array_index(chunks, chk_idx, error);
	if (chk == NULL)
		return FALSE;
    if (!fu_device_retry_full(FU_DEVICE(self),
	        	fu_blestechtp_hid_device_program_page_cb,
				5,
				30,
				chk,
				error)) {
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_blestechtp_hid_device_program(FuBlestechtpHidDevice *self,
					 FuFirmware *firmware,
					 FuProgress *progress,
					 GError **error)
{
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	guint boot_pack_nums = 0;
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	chunks = fu_chunk_array_new_from_stream(stream,
					fu_firmware_get_addr(firmware),
					FU_CHUNK_PAGESZ_NONE,
					FW_PAGE_SIZE,
					error);
	if (chunks == NULL)
		return FALSE;

	boot_pack_nums = BOOT_SIZE/ FW_PAGE_SIZE;
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks) - boot_pack_nums);
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		/* skip boot parts and config part*/
		if (i < boot_pack_nums || i == APP_CONFIG_PAGE) continue;
		if (!fu_blestechtp_hid_device_program_chunk(self,
					chunks,
					i,
					error)) {
			g_prefix_error(error, 
				"Program Page %u Failed: ", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	if (!fu_blestechtp_hid_device_program_chunk(self,
			chunks,
			APP_CONFIG_PAGE,
			error)) {
		g_prefix_error(error, 
			"Program Page %d Failed: ", APP_CONFIG_PAGE);
		return FALSE;
	}
	fu_progress_step_done(progress);

	return TRUE;
}


static gboolean
fu_blestechtp_hid_device_program_finish(FuBlestechtpHidDevice *self,
	                FuFirmware *firmware,
					GError **error)
{
	FuBlestechtpFirmware *blestechtpFirmware = FU_BLESTECHTP_FIRMWARE(firmware);
	guint16 calc_checksum;
	guint16 bin_checksum = fu_blestechtp_firmware_get_checksum(blestechtpFirmware);
	guint8 cmd_buf[4] = {FU_BLESTECHTP_CMD_PROGRAM_CHECKSUM, 
				(guint8)bin_checksum, (guint8)(bin_checksum >> 8), 0};
	guint8 read_buf[4] = { 0 };

	if (!fu_blestechtp_hid_device_write_read(self,
			cmd_buf, 3,
			read_buf, sizeof(read_buf), 
			60, error)) {
		return FALSE;
	}

	calc_checksum = (guint16)read_buf[1] | (guint16)(read_buf[2] << 8);
	if (calc_checksum != bin_checksum){
		g_set_error(error,
			FWUPD_ERROR,
			FWUPD_ERROR_NOT_SUPPORTED,
			"checksum failed, exp:%04X, act:%04X",
			bin_checksum, calc_checksum);
		return FALSE;
	}

	cmd_buf[0] = FU_BLESTECHTP_CMD_PROGRAM_END;
	if (!fu_blestechtp_hid_device_write_read(self,
			cmd_buf, 1,
			NULL, 0, 
			0, error)) {
		g_prefix_error_literal(error, "program end failed: ");
		return FALSE;
	}
	/* need about 80ms to start */
	fu_device_sleep(FU_DEVICE(self), 80);
	/* success*/
	return TRUE;
}


static gboolean
fu_blestechtp_hid_device_write_firmware(FuDevice *device,
				     FuFirmware *firmware,
				     FuProgress *progress,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuBlestechtpHidDevice *self = FU_BLESTECHTP_HID_DEVICE(device);
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "switch");
	fu_progress_add_step(progress, FWUPD_STATUS_DOWNLOADING, 5, "start");
	fu_progress_add_step(progress, FWUPD_STATUS_DOWNLOADING, 80, "program");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 10, "reset");

	/* switch to boot */
	if (!fu_blestechtp_hid_device_switch_boot(self, error)) {
		g_prefix_error_literal(error,
				"switch boot failed: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* update start */
	if (!fu_blestechtp_hid_device_update_start(self, error)) {
		g_prefix_error_literal(error,
				"update start failed: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write image */
	if (!fu_blestechtp_hid_device_program(self, firmware, 
				fu_progress_get_child(progress), error)) {
		g_prefix_error_literal(error,
				"write image failed");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* finish */
	if (!fu_blestechtp_hid_device_program_finish(self, firmware, error)) {
		g_prefix_error_literal(error,
			"program end failed: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_blestechtp_hid_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 3, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 3, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 4, "reload");
}

static gchar *
fu_blestechtp_hid_device_convert_version(FuDevice *device, guint64 version_raw)
{
	gchar* ch;
	ch =  fu_version_from_uint16(version_raw, fu_device_get_version_format(device));
	return ch;
}

static gboolean
fu_blestechtp_hid_device_setup(FuDevice *device, GError **error)
{
	FuBlestechtpHidDevice *self = FU_BLESTECHTP_HID_DEVICE(device);
	guint8 fw_ver[FW_VER_LEN];
	if(!fu_blestechtp_hid_device_get_ver(self, fw_ver, error)) {
		return FALSE;
	}
	fu_device_set_version_raw(device, (fw_ver[0] << 8) | fw_ver[1]);

	/* success */
	return TRUE;
}

static void
fu_blestechtp_hid_device_init(FuBlestechtpHidDevice *self)
{
    fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_firmware_size(FU_DEVICE(self), FW_SIZE);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_BLESTECHTP_FIRMWARE);
	fu_device_set_summary(FU_DEVICE(self), "Touchpad");
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_INPUT_TOUCHPAD);
	fu_device_add_protocol(FU_DEVICE(self), "com.blestech.tp");
	fu_device_set_vendor(FU_DEVICE(self), "Blestech");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_BCD);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_NONBLOCK);
}

static void
fu_blestechtp_hid_device_class_init(FuBlestechtpHidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	device_class->setup = fu_blestechtp_hid_device_setup;
	device_class->reload = fu_blestechtp_hid_device_setup;
	device_class->set_progress = fu_blestechtp_hid_device_set_progress;
	device_class->convert_version = fu_blestechtp_hid_device_convert_version;
	device_class->write_firmware = fu_blestechtp_hid_device_write_firmware;
}
