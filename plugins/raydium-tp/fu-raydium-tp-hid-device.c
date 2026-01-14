/*
 * Copyright 2025 Raydium.inc <Maker.Tsai@rad-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "config.h"
#include "fu-raydium-tp-common.h"
#include "fu-raydium-tp-firmware.h"
#include "fu-raydium-tp-hid-device.h"
#include "fu-raydium-tp-struct.h"

struct _FuRaydiumtpHidDevice {
	FuHidrawDevice parent_instance;
};

G_DEFINE_TYPE(FuRaydiumtpHidDevice, fu_raydium_tp_hid_device, FU_TYPE_HIDRAW_DEVICE)

static void
fu_raydium_tp_hid_device_to_string(FuDevice *device, guint idt, GString *str)
{
}

static gboolean
fu_raydium_tp_hid_device_check_pid(FuRaydiumtpHidDevice *self, FuRaydiumtpFirmware *fw)
{
	return fu_device_get_pid(FU_DEVICE(self)) == fu_raydium_tp_firmware_get_product_id(fw);
}

static gboolean
fu_raydium_tp_hid_device_check_vid(FuRaydiumtpHidDevice *self, FuRaydiumtpFirmware *fw)
{
	return fu_device_get_vid(FU_DEVICE(self)) == fu_raydium_tp_firmware_get_vendor_id(fw);
}

static gboolean
fu_raydium_tp_hid_device_get_report(FuRaydiumtpHidDevice *self,
				    guint8 *rx,
				    gsize rxsz,
				    GError **error)
{
	g_autofree guint8 *rcv_buf = NULL;
	gsize bufsz = rxsz + 1;

	rcv_buf = g_malloc0(bufsz);

	rcv_buf[0] = FU_RAYDIUM_TP_CMD2_RID;
	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					  rcv_buf,
					  bufsz,
					  FU_IOCTL_FLAG_NONE,
					  error))
		return FALSE;

	return fu_memcpy_safe(rx, rxsz, 0, rcv_buf, bufsz, 0, rxsz, error);
}

static gboolean
fu_raydium_tp_hid_device_set_report(FuRaydiumtpHidDevice *self,
				    guint8 *tx,
				    gsize txsz,
				    GError **error)
{
	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					    tx,
					    txsz,
					    FU_IOCTL_FLAG_NONE,
					    error);
}

typedef struct {
	guint8 *outbuf;
	guint8 *inbuf;
} FuRaydiumTpHidCmdHelper;

static gboolean
fu_raydium_tp_hid_device_write_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuRaydiumtpHidDevice *self = FU_RAYDIUM_TP_HID_DEVICE(device);
	FuRaydiumTpHidCmdHelper *args = (FuRaydiumTpHidCmdHelper *)user_data;

	return fu_raydium_tp_hid_device_set_report(self, args->outbuf, I2C_BUF_SIZE, error);
}

static gboolean
fu_raydium_tp_hid_device_read_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuRaydiumtpHidDevice *self = FU_RAYDIUM_TP_HID_DEVICE(device);
	FuRaydiumTpHidCmdHelper *args = (FuRaydiumTpHidCmdHelper *)user_data;

	if (!fu_raydium_tp_hid_device_set_report(self, args->outbuf, I2C_BUF_SIZE, error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_get_report(self, args->inbuf, I2C_BUF_SIZE, error))
		return FALSE;

	return (args->inbuf[HIDI2C_CHK_IDX] == 0xFF || args->inbuf[0] == 0xFF);
}

static gboolean
fu_raydium_tp_hid_device_bl_write(FuRaydiumtpHidDevice *self,
				  guint8 cmd,
				  guint8 wbuf[],
				  guint length,
				  GError **error)
{
	g_autoptr(FuRaydiumTpHidPacket) pkt = NULL;
	FuRaydiumTpHidCmdHelper args = {
	    .outbuf = NULL,
	    .inbuf = NULL,
	};

	pkt = fu_raydium_tp_hid_packet_new();
	fu_raydium_tp_hid_packet_set_header3(pkt, FU_RAYDIUM_TP_HID_DATA_HEADER3_WR);
	fu_raydium_tp_hid_packet_set_header4(pkt, FU_RAYDIUM_TP_HID_DATA_HEADER4_WR);
	fu_raydium_tp_hid_packet_set_data0(pkt, FU_RAYDIUM_TP_CMD2_WRT);
	fu_raydium_tp_hid_packet_set_data2(pkt, cmd);
	fu_raydium_tp_hid_packet_set_data3(pkt, wbuf[3]);
	fu_raydium_tp_hid_packet_set_data4(pkt, wbuf[4]);
	fu_raydium_tp_hid_packet_set_data5(pkt, wbuf[5]);
	fu_raydium_tp_hid_packet_set_length(pkt, length);

	if (!fu_memcpy_safe(pkt->buf->data, I2C_BUF_SIZE, 15, wbuf, I2C_BUF_SIZE, 6, length, error))
		return FALSE;

	args.outbuf = pkt->buf->data;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_raydium_tp_hid_device_write_cb,
				  RETRY_NUM,
				  1,
				  &args,
				  error)) {
		g_prefix_error_literal(error, "wait bl write status failed: ");
		return FALSE;
	}

	pkt = fu_raydium_tp_hid_packet_new();
	fu_raydium_tp_hid_packet_set_header3(pkt, FU_RAYDIUM_TP_HID_DATA_HEADER3_WR);
	fu_raydium_tp_hid_packet_set_header4(pkt, FU_RAYDIUM_TP_HID_DATA_HEADER4_WR);
	fu_raydium_tp_hid_packet_set_data0(pkt, FU_RAYDIUM_TP_CMD2_ACK);
	fu_raydium_tp_hid_packet_set_length(pkt, length);

	args.outbuf = pkt->buf->data;
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_raydium_tp_hid_device_write_cb,
				    RETRY_NUM,
				    1,
				    &args,
				    error);
}

static gboolean
fu_raydium_tp_hid_device_bl_read(FuRaydiumtpHidDevice *self,
				 guint8 rcv_buf[],
				 guint length,
				 GError **error)
{
	guint8 wait_idle_flag = 0;
	g_autofree guint8 *inbuf = g_malloc0(I2C_BUF_MAXSIZE);
	g_autoptr(FuRaydiumTpHidPacket) pkt = NULL;
	FuRaydiumTpHidCmdHelper args = {
	    .outbuf = NULL,
	    .inbuf = inbuf,
	};

	if (rcv_buf[1] == 0xFF) {
		wait_idle_flag = 1;
		rcv_buf[1] = 0x00;
	}

	pkt = fu_raydium_tp_hid_packet_new();
	fu_raydium_tp_hid_packet_set_header3(pkt, FU_RAYDIUM_TP_HID_DATA_HEADER3_RD);
	fu_raydium_tp_hid_packet_set_header4(pkt, FU_RAYDIUM_TP_HID_DATA_HEADER4_RD);
	fu_raydium_tp_hid_packet_set_data0(pkt, rcv_buf[0]);
	fu_raydium_tp_hid_packet_set_data1(pkt, rcv_buf[1]);
	fu_raydium_tp_hid_packet_set_data2(pkt, rcv_buf[2]);
	fu_raydium_tp_hid_packet_set_data3(pkt, rcv_buf[3]);
	fu_raydium_tp_hid_packet_set_data4(pkt, rcv_buf[4]);
	fu_raydium_tp_hid_packet_set_data5(pkt, rcv_buf[5]);
	fu_raydium_tp_hid_packet_set_length(pkt, length);

	args.outbuf = pkt->buf->data;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_raydium_tp_hid_device_read_cb,
				  RETRY_NUM,
				  1,
				  &args,
				  error)) {
		g_prefix_error_literal(error, "wait bl read status failed: ");
		return FALSE;
	}

	if (wait_idle_flag == 1) {
		return fu_memcpy_safe(rcv_buf, I2C_BUF_SIZE, 0, inbuf, 256, 0, I2C_BUF_SIZE, error);
	} else {
		return fu_memcpy_safe(rcv_buf,
				      I2C_BUF_SIZE,
				      0,
				      inbuf,
				      I2C_BUF_MAXSIZE,
				      1,
				      I2C_BUF_SIZE - 1,
				      error);
	}
}

static gboolean
fu_raydium_tp_hid_device_tp_write(FuRaydiumtpHidDevice *self,
				  guint8 cmd,
				  guint8 wbuf[],
				  guint length,
				  GError **error)
{
	g_autoptr(FuRaydiumTpHidPacket) pkt = NULL;
	FuRaydiumTpHidCmdHelper args = {
	    .outbuf = NULL,
	    .inbuf = NULL,
	};

	pkt = fu_raydium_tp_hid_packet_new();
	fu_raydium_tp_hid_packet_set_header3(pkt, FU_RAYDIUM_TP_HID_DATA_HEADER3_WR);
	fu_raydium_tp_hid_packet_set_header4(pkt, FU_RAYDIUM_TP_HID_DATA_HEADER4_WR);
	fu_raydium_tp_hid_packet_set_data0(pkt, FU_RAYDIUM_TP_CMD2_WRT);
	fu_raydium_tp_hid_packet_set_data2(pkt, (guint8)(length + 1));
	fu_raydium_tp_hid_packet_set_data3(pkt, cmd);

	if (!fu_memcpy_safe(pkt->buf->data, I2C_BUF_SIZE, 11, wbuf, I2C_BUF_SIZE, 0, length, error))
		return FALSE;

	args.outbuf = pkt->buf->data;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_raydium_tp_hid_device_write_cb,
				  RETRY_NUM_MAX,
				  1,
				  &args,
				  error)) {
		g_prefix_error_literal(error, "wait tp write status failed: ");
		return FALSE;
	}

	pkt = fu_raydium_tp_hid_packet_new();
	fu_raydium_tp_hid_packet_set_header3(pkt, FU_RAYDIUM_TP_HID_DATA_HEADER3_WR);
	fu_raydium_tp_hid_packet_set_header4(pkt, FU_RAYDIUM_TP_HID_DATA_HEADER4_WR);
	fu_raydium_tp_hid_packet_set_data0(pkt, FU_RAYDIUM_TP_CMD2_ACK);

	args.outbuf = pkt->buf->data;
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_raydium_tp_hid_device_write_cb,
				    RETRY_NUM_MAX,
				    1,
				    &args,
				    error);
}

static gboolean
fu_raydium_tp_hid_device_tp_read(FuRaydiumtpHidDevice *self,
				 guint8 cmd,
				 guint8 rcv_buf[],
				 GError **error)
{
	g_autofree guint8 *inbuf = g_malloc0(I2C_BUF_MAXSIZE);
	g_autoptr(FuRaydiumTpHidPacket) pkt = NULL;
	FuRaydiumTpHidCmdHelper args = {
	    .outbuf = NULL,
	    .inbuf = inbuf,
	};

	pkt = fu_raydium_tp_hid_packet_new();
	fu_raydium_tp_hid_packet_set_header3(pkt, FU_RAYDIUM_TP_HID_DATA_HEADER3_RD);
	fu_raydium_tp_hid_packet_set_header4(pkt, FU_RAYDIUM_TP_HID_DATA_HEADER4_RD);
	fu_raydium_tp_hid_packet_set_data0(pkt, FU_RAYDIUM_TP_CMD2_READ);
	fu_raydium_tp_hid_packet_set_data3(pkt, FU_RAYDIUM_TP_HID_DATA_HEADER10);
	fu_raydium_tp_hid_packet_set_data4(pkt, cmd);

	args.outbuf = pkt->buf->data;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_raydium_tp_hid_device_read_cb,
				  RETRY_NUM_MAX,
				  1,
				  &args,
				  error)) {
		g_prefix_error_literal(error, "wait tp read status failed: ");
		return FALSE;
	}

	return fu_memcpy_safe(rcv_buf,
			      I2C_BUF_SIZE,
			      0,
			      inbuf,
			      I2C_BUF_SIZE,
			      1,
			      I2C_BUF_SIZE - 1,
			      error);
}

static gboolean
fu_raydium_tp_hid_device_set_bl_mem(FuRaydiumtpHidDevice *self,
				    guint32 addr,
				    guint32 value,
				    guint size,
				    GError **error)
{
	guint8 wdata[I2C_BUF_SIZE] = {0};

	fu_memwrite_uint32(wdata + 6, addr, G_LITTLE_ENDIAN);
	fu_memwrite_uint32(wdata + 10, value, G_LITTLE_ENDIAN);

	return fu_raydium_tp_hid_device_bl_write(self,
						 FU_RAYDIUM_TP_CMD_BL_CMD_WRITE_REGISTER,
						 wdata,
						 HIDI2C_WRITE_MAX_LENGTH,
						 error);
}

static gboolean
fu_raydium_tp_hid_device_get_bl_mem(FuRaydiumtpHidDevice *self,
				    guint32 addr,
				    guint16 length,
				    guint8 outbuf[],
				    GError **error)
{
	guint8 wdata[I2C_BUF_SIZE] = {0};

	fu_memwrite_uint32(wdata + 6, addr, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(wdata + 10, length, G_LITTLE_ENDIAN);

	if (!fu_raydium_tp_hid_device_bl_write(self,
					       FU_RAYDIUM_TP_CMD_BL_CMD_READ_ADDRESS_MEMORY,
					       wdata,
					       HIDI2C_WRITE_MAX_LENGTH,
					       error))
		return FALSE;

	outbuf[0] = FU_RAYDIUM_TP_CMD2_READ;

	return fu_raydium_tp_hid_device_bl_read(self, outbuf, length, error);
}

static gboolean
fu_raydium_tp_hid_device_jump_to_boot(FuRaydiumtpHidDevice *self, GError **error)
{
	guint8 wdata[I2C_BUF_SIZE] = {0};

	return fu_raydium_tp_hid_device_tp_write(self,
						 FU_RAYDIUM_TP_CMD_ADDR_JUMP_TO_BOOTLOADER,
						 wdata,
						 1,
						 error);
}

static guint8
fu_raydium_tp_hid_device_read_status(FuRaydiumtpHidDevice *self, GError **error)
{
	guint8 i = 0;
	guint8 data[I2C_BUF_SIZE] = {0};

	for (i = 0; i < RETRY_NUM; i++) {
		memset(data, 0, sizeof(data));
		data[0] = FU_RAYDIUM_TP_CMD2_CHK;
		if (fu_raydium_tp_hid_device_bl_read(self, data, 7, error)) {
			if (fu_memcmp_safe(data,
					   sizeof(data),
					   0,
					   (const guint8 *)"firm",
					   4,
					   0,
					   4,
					   NULL))
				return FU_RAYDIUM_TP_BOOT_MODE_TS_MAIN;
			else if (fu_memcmp_safe(data,
						sizeof(data),
						0,
						(const guint8 *)"boot",
						4,
						0,
						4,
						NULL))
				return FU_RAYDIUM_TP_BOOT_MODE_TS_BLDR;
		}
	}
	return FU_RAYDIUM_TP_BOOT_MODE_TS_NONE;
}

static gboolean
fu_raydium_tp_hid_device_wait_main_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuRaydiumtpHidDevice *self = FU_RAYDIUM_TP_HID_DEVICE(device);

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_KEY_RESET_REG,
						 FU_RAYDIUM_TP_KEY_RESET_VALUE,
						 8,
						 error))
		g_clear_error(error);

	fu_device_sleep(device, 10);
	return fu_raydium_tp_hid_device_read_status(self, error) == FU_RAYDIUM_TP_BOOT_MODE_TS_MAIN;
}

static gboolean
fu_raydium_tp_hid_device_wait_boot_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuRaydiumtpHidDevice *self = FU_RAYDIUM_TP_HID_DEVICE(device);

	if (!fu_raydium_tp_hid_device_jump_to_boot(self, error))
		g_clear_error(error);

	fu_device_sleep(device, 10);
	return fu_raydium_tp_hid_device_read_status(self, error) == FU_RAYDIUM_TP_BOOT_MODE_TS_BLDR;
}

static gboolean
fu_raydium_tp_hid_device_wait_dma_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuRaydiumtpHidDevice *self = FU_RAYDIUM_TP_HID_DEVICE(device);
	FuRaydiumTpHidCmdHelper *args = (FuRaydiumTpHidCmdHelper *)user_data;

	if (!fu_raydium_tp_hid_device_get_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_DMA_IER,
						 5,
						 args->outbuf,
						 error))
		return FALSE;

	return !(args->outbuf[2] & TP_BIT(7));
}

static gboolean
fu_raydium_tp_hid_device_wait_idle_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuRaydiumtpHidDevice *self = FU_RAYDIUM_TP_HID_DEVICE(device);
	FuRaydiumTpHidCmdHelper *args = (FuRaydiumTpHidCmdHelper *)user_data;

	memset(args->outbuf, 0, I2C_BUF_SIZE);
	args->outbuf[0] = FU_RAYDIUM_TP_CMD2_CHK;
	args->outbuf[1] = 0xFF;

	if (!fu_raydium_tp_hid_device_bl_read(self, args->outbuf, 6, error))
		return FALSE;

	*(args->inbuf) = args->outbuf[HIDI2C_CHK_IDX];
	return *(args->inbuf) == FU_RAYDIUM_TP_CMD_BL_CMD_IDLE;
}

static gboolean
fu_raydium_tp_hid_device_wait_for_idle_boot(FuRaydiumtpHidDevice *self, GError **error)
{
	guint8 rcv_buf[I2C_BUF_SIZE] = {0};
	guint8 boot_main_state = 0x00;
	FuRaydiumTpHidCmdHelper args = {
	    .outbuf = rcv_buf,
	    .inbuf = &boot_main_state,
	};

	return fu_device_retry_full(FU_DEVICE(self),
				    fu_raydium_tp_hid_device_wait_idle_cb,
				    RETRY_NUM_MAX,
				    10,
				    &args,
				    error);
}

static gboolean
fu_raydium_tp_hid_device_bl_set_wdt(FuRaydiumtpHidDevice *self, guint8 enable, GError **error)
{
	guint8 wbuf[I2C_BUF_SIZE] = {0};

	if (enable == 1)
		wbuf[3] = FU_RAYDIUM_TP_CMD_BL_WATCHDOG_ENABLE;
	else
		wbuf[3] = FU_RAYDIUM_TP_CMD_BL_WATCHDOG_DISABLE;

	return fu_raydium_tp_hid_device_bl_write(self,
						 FU_RAYDIUM_TP_CMD_BL_CMD_WATCHDOG_FUNCTION_SET,
						 wbuf,
						 HIDI2C_WRITE_MAX_LENGTH,
						 error);
}

static gboolean
fu_raydium_tp_hid_device_bl_dis_wdt_and_unlock_flash(FuRaydiumtpHidDevice *self, GError **error)
{
	if (!fu_raydium_tp_hid_device_bl_set_wdt(self, 0, error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_KEY_DISABLE_FLASH_PROTECTION,
						 FU_RAYDIUM_TP_KEY_DISABLE,
						 8,
						 error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_KEY_UNLOCK_PRAM,
						 FU_RAYDIUM_TP_KEY_DISABLE,
						 8,
						 error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_KEY_FLASH_FLKEY2,
						 FU_RAYDIUM_TP_KEY_FLKEY3_KEY,
						 8,
						 error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_KEY_FLASH_FLKEY1,
						 FU_RAYDIUM_TP_KEY_FLKEY1_KEY,
						 8,
						 error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_KEY_FLASH_FLKEY1,
						 FU_RAYDIUM_TP_KEY_DISABLE,
						 8,
						 error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_KEY_FLASH_FLKEY1,
						 FU_RAYDIUM_TP_KEY_FLKEY1_KEY,
						 8,
						 error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_KEY_FLASH_FLKEY2,
						 FU_RAYDIUM_TP_KEY_DISABLE,
						 8,
						 error))
		return FALSE;

	return fu_raydium_tp_hid_device_wait_for_idle_boot(self, error);
}

static gboolean
fu_raydium_tp_hid_device_bl_erase_fw_flash(FuRaydiumtpHidDevice *self, GError **error)
{
	guint8 wbuf[I2C_BUF_SIZE] = {0};

	wbuf[3] = FU_RAYDIUM_TP_CMD_BL_ERASE_FLASH_MODE1;
	if (!fu_raydium_tp_hid_device_bl_write(self,
					       FU_RAYDIUM_TP_CMD_BL_CMD_ERASE_FLASH,
					       wbuf,
					       HIDI2C_WRITE_MAX_LENGTH,
					       error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 100);
	return fu_raydium_tp_hid_device_wait_for_idle_boot(self, error);
}

static gboolean
fu_raydium_tp_hid_device_bl_erase_flash_sector(FuRaydiumtpHidDevice *self,
					       guint32 addr,
					       guint8 loop,
					       GError **error)
{
	guint8 wbuf[I2C_BUF_SIZE] = {0};

	wbuf[3] = FU_RAYDIUM_TP_CMD_BL_ERASE_FLASH_MODE4;
	fu_memwrite_uint32(wbuf + 7, addr, G_LITTLE_ENDIAN);
	wbuf[11] = loop;

	if (!fu_raydium_tp_hid_device_bl_write(self,
					       FU_RAYDIUM_TP_CMD_BL_CMD_ERASE_FLASH,
					       wbuf,
					       HIDI2C_WRITE_MAX_LENGTH,
					       error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 1);
	return fu_raydium_tp_hid_device_wait_for_idle_boot(self, error);
}

static gboolean
fu_raydium_tp_hid_device_bl_write_flash_chunk(FuRaydiumtpHidDevice *self,
					      FuChunk *chunk,
					      guint cur_write_page_no,
					      guint8 sub_page_no,
					      GError **error)
{
	guint8 wbuf[I2C_BUF_SIZE] = {0};

	fu_memwrite_uint16(wbuf + 3, cur_write_page_no, G_LITTLE_ENDIAN);
	wbuf[5] = sub_page_no;

	if (!fu_memcpy_safe(wbuf,
			    I2C_BUF_SIZE,
			    6,
			    fu_chunk_get_data(chunk),
			    fu_chunk_get_data_sz(chunk),
			    0,
			    fu_chunk_get_data_sz(chunk),
			    error))
		return FALSE;

	return fu_raydium_tp_hid_device_bl_write(self,
						 FU_RAYDIUM_TP_CMD_BL_CMD_WRITE_HID_I2C_FLASH,
						 wbuf,
						 HIDI2C_WRITE_SIZE,
						 error);
}

static gboolean
fu_raydium_tp_hid_device_bl_write_flash(FuRaydiumtpHidDevice *self,
					guint8 inbuf[],
					guint imgsz,
					GError **error)
{
	g_autoptr(GBytes) fw_bytes = g_bytes_new(inbuf, imgsz);
	g_autoptr(FuChunkArray) chunks = NULL;
	guint page_no = 0;
	guint8 sub_page_no = 0;
	g_autoptr(GError) error_local = NULL;

	/* progress */
	chunks = fu_chunk_array_new_from_bytes(fw_bytes,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       HIDI2C_WRITE_SIZE);

	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks, i, &error_local);

		if (chk == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "failed to get chunk at index %u",
				    i);
			return FALSE;
		}

		if (!fu_raydium_tp_hid_device_bl_write_flash_chunk(self,
								   chk,
								   page_no,
								   sub_page_no,
								   &error_local))
			return FALSE;

		sub_page_no++;
		if (sub_page_no == 4) {
			sub_page_no = 0;
			page_no++;
			if (!fu_raydium_tp_hid_device_wait_for_idle_boot(self, &error_local))
				return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_bl_dma_crc(FuRaydiumtpHidDevice *self,
				    guint32 base_addr,
				    guint32 img_length,
				    guint32 image_crc,
				    GError **error)
{
	guint8 rbuf[I2C_BUF_SIZE] = {0};
	guint32 value = 0;
	guint32 calculated_crc = 0;
	FuRaydiumTpHidCmdHelper args = {
	    .outbuf = rbuf,
	    .inbuf = NULL,
	};

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_DMA_SADDR,
						 base_addr,
						 8,
						 error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_DMA_EADDR,
						 base_addr + img_length - CRC_LEN,
						 8,
						 error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_get_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_DMA_IER,
						 5,
						 rbuf,
						 error))
		return FALSE;

	value = fu_memread_uint32(rbuf, G_LITTLE_ENDIAN);
	value &= ~TP_BIT(16);

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_DMA_IER,
						 value,
						 8,
						 error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_PRAM_LOCK,
						 0,
						 8,
						 error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_get_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_DMA_IER,
						 5,
						 rbuf,
						 error))
		return FALSE;

	value = fu_memread_uint32(rbuf, G_LITTLE_ENDIAN);
	value = (value & ~TP_BIT(17)) | TP_BIT(17);

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_DMA_IER,
						 value,
						 8,
						 error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_get_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_DMA_CR,
						 5,
						 rbuf,
						 error))
		return FALSE;

	value = fu_memread_uint32(rbuf, G_LITTLE_ENDIAN);
	value = (value & ~TP_BIT(23)) | TP_BIT(23);

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_DMA_CR,
						 value,
						 8,
						 error))
		return FALSE;

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_raydium_tp_hid_device_wait_dma_cb,
				  RETRY_NUM_MAX,
				  1,
				  &args,
				  error)) {
		g_prefix_error_literal(error, "wait dma status failed: ");
		return FALSE;
	}

	if (!fu_raydium_tp_hid_device_get_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_DMA_RES,
						 5,
						 rbuf,
						 error))
		return FALSE;

	calculated_crc = fu_memread_uint32(rbuf, G_LITTLE_ENDIAN);

	return image_crc == calculated_crc;
}

static gboolean
fu_raydium_tp_hid_device_bl_trig_desc_to_flash(FuRaydiumtpHidDevice *self,
					       guint32 pram_addr,
					       guint32 flash_addr,
					       guint16 length,
					       GError **error)
{
	guint8 wbuf[I2C_BUF_SIZE] = {0};
	g_autoptr(GError) error_local = NULL;

	wbuf[3] = FU_RAYDIUM_TP_CMD_BL_CMD_WRITE_RAM_FLASH;
	wbuf[4] = FU_RAYDIUM_TP_HID_DATA_HEADER5;
	fu_memwrite_uint32(wbuf + 8, pram_addr, G_LITTLE_ENDIAN);
	fu_memwrite_uint32(wbuf + 12, flash_addr, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(wbuf + 16, length, G_LITTLE_ENDIAN);

	if (!fu_raydium_tp_hid_device_bl_write(self,
					       FU_RAYDIUM_TP_CMD_BL_CMD_WRITE_RAM_FLASH,
					       wbuf,
					       HIDI2C_WRITE_MAX_LENGTH,
					       &error_local))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 100);
	return fu_raydium_tp_hid_device_wait_for_idle_boot(self, &error_local);
}

static gboolean
fu_raydium_tp_hid_device_bl_trig_pram_to_flash(FuRaydiumtpHidDevice *self, GError **error)
{
	guint8 wbuf[I2C_BUF_SIZE] = {0};
	g_autoptr(GError) error_local = NULL;

	wbuf[0] = FU_RAYDIUM_TP_CMD2_WRT;
	wbuf[2] = FU_RAYDIUM_TP_CMD_BL_CMD_TRIGGER_WRITE_FLASH;

	if (!fu_raydium_tp_hid_device_bl_write(self,
					       FU_RAYDIUM_TP_CMD_BL_CMD_TRIGGER_WRITE_FLASH,
					       wbuf,
					       HIDI2C_WRITE_MAX_LENGTH,
					       &error_local))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 100);
	return fu_raydium_tp_hid_device_wait_for_idle_boot(self, &error_local);
}

static gboolean
fu_raydium_tp_hid_device_bl_software_reset(FuRaydiumtpHidDevice *self, GError **error)
{
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_raydium_tp_hid_device_wait_main_cb,
				  RETRY_NUM,
				  1000,
				  NULL,
				  error)) {
		g_prefix_error_literal(error, "wait for main failed: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_set_mem_addr(FuRaydiumtpHidDevice *self,
				      guint32 addr,
				      guint8 type,
				      GError **error)
{
	guint8 wdata[I2C_BUF_SIZE] = {0};

	fu_memwrite_uint32(wdata, addr, G_LITTLE_ENDIAN);
	wdata[4] = type;

	return fu_raydium_tp_hid_device_tp_write(self,
						 FU_RAYDIUM_TP_CMD_ADDR_MEM_ADDRESS_SET,
						 wdata,
						 5,
						 error);
}

static gboolean
fu_raydium_tp_hid_device_set_mem_write(FuRaydiumtpHidDevice *self, guint32 value, GError **error)
{
	guint8 wdata[I2C_BUF_SIZE] = {0};

	fu_memwrite_uint32(wdata, value, G_LITTLE_ENDIAN);

	return fu_raydium_tp_hid_device_tp_write(self,
						 FU_RAYDIUM_TP_CMD_ADDR_MEM_WRITE,
						 wdata,
						 4,
						 error);
}

static gboolean
fu_raydium_tp_hid_device_get_mem_read(FuRaydiumtpHidDevice *self, guint8 ram[], GError **error)
{
	guint8 rbuf[I2C_BUF_SIZE] = {0};

	if (!fu_raydium_tp_hid_device_tp_read(self, FU_RAYDIUM_TP_CMD_ADDR_MEM_READ, rbuf, error))
		return FALSE;

	return fu_memcpy_safe(ram, 4, 0, rbuf, 4, 0, 4, error);
}

static guint8
fu_raydium_tp_hid_device_read_flash_protect_status(FuRaydiumtpHidDevice *self, GError **error)
{
	guint8 rbuf[I2C_BUF_SIZE] = {0};
	guint32 value = 0;

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_LENGTH,
						 FU_RAYDIUM_TP_KEY_FLREAD_STATUS,
						 8,
						 error))
		return 0xFF;

	if (!fu_raydium_tp_hid_device_wait_for_idle_boot(self, error))
		return 0xFF;

	if (!fu_raydium_tp_hid_device_get_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_ISPCTL,
						 5,
						 rbuf,
						 error))
		return 0xFF;

	value = fu_memread_uint32(rbuf, G_LITTLE_ENDIAN);
	value = (value & ~TP_BIT(11)) | TP_BIT(11);

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_ISPCTL,
						 value,
						 8,
						 error))
		return 0xFF;

	if (!fu_raydium_tp_hid_device_wait_for_idle_boot(self, error))
		return 0xFF;

	if (!fu_raydium_tp_hid_device_get_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_DATA,
						 5,
						 rbuf,
						 error))
		return 0xFF;

	return rbuf[0];
}

static gboolean
fu_raydium_tp_hid_device_write_flash_protect_status(FuRaydiumtpHidDevice *self,
						    guint8 status,
						    GError **error)
{
	guint8 rbuf[I2C_BUF_SIZE] = {0};
	guint32 value = 0;

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_LENGTH,
						 FU_RAYDIUM_TP_KEY_FLWRITE_EN,
						 8,
						 error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_wait_for_idle_boot(self, error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_get_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_ISPCTL,
						 5,
						 rbuf,
						 error))
		return FALSE;

	value = fu_memread_uint32(rbuf, G_LITTLE_ENDIAN);
	value = (value & ~TP_BIT(11)) | TP_BIT(11);

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_ISPCTL,
						 value,
						 8,
						 error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_wait_for_idle_boot(self, error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_LENGTH,
						 FU_RAYDIUM_TP_KEY_FLWRITE_STATUS,
						 8,
						 error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_wait_for_idle_boot(self, error))
		return FALSE;

	value = (guint32)(status << 16);

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_ADDR,
						 value,
						 8,
						 error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_wait_for_idle_boot(self, error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_get_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_ISPCTL,
						 5,
						 rbuf,
						 error))
		return FALSE;

	value = fu_memread_uint32(rbuf, G_LITTLE_ENDIAN);
	value = (value & ~TP_BIT(11)) | TP_BIT(11);

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_ISPCTL,
						 value,
						 8,
						 error))
		return FALSE;

	return fu_raydium_tp_hid_device_wait_for_idle_boot(self, error);
}

static gboolean
fu_raydium_tp_hid_device_read_firmware_info(FuRaydiumtpHidDevice *self, GError **error)
{
	guint32 addr = 0;
	guint16 length = HIDI2C_WRITE_SIZE;
	guint8 wbuf[I2C_BUF_SIZE] = {0};
	guint8 rbuf[I2C_BUF_SIZE] = {0};
	guint8 rbuf_desc[I2C_BUF_SIZE] = {0};
	guint8 rbuf_ft[I2C_BUF_SIZE] = {0};
	guint16 vid;
	guint8 mode;

	mode = fu_raydium_tp_hid_device_read_status(self, error);

	if (mode == FU_RAYDIUM_TP_BOOT_MODE_TS_NONE) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "wrong boot mode");
		return FALSE;
	}

	if (mode == FU_RAYDIUM_TP_BOOT_MODE_TS_BLDR) {
		addr = FU_RAYDIUM_TP_FLASH_DESC_RECORD_ADDR;
		length = HIDI2C_WRITE_SIZE;
		fu_memwrite_uint32(wbuf + 6, addr, G_LITTLE_ENDIAN);
		fu_memwrite_uint16(wbuf + 10, length, G_LITTLE_ENDIAN);

		if (!fu_raydium_tp_hid_device_bl_write(self,
						       FU_RAYDIUM_TP_CMD_BL_CMD_READ_FLASH_ADDR,
						       wbuf,
						       HIDI2C_WRITE_MAX_LENGTH,
						       error))
			return FALSE;

		rbuf_desc[0] = FU_RAYDIUM_TP_CMD2_READ;

		if (!fu_raydium_tp_hid_device_bl_read(self, rbuf_desc, 40, error))
			return FALSE;

		addr = FU_RAYDIUM_TP_FLASH_FT_RECORD_ADDR;
		length = 16;
		fu_memwrite_uint32(wbuf + 6, addr, G_LITTLE_ENDIAN);
		fu_memwrite_uint16(wbuf + 10, length, G_LITTLE_ENDIAN);

		if (!fu_raydium_tp_hid_device_bl_write(self,
						       FU_RAYDIUM_TP_CMD_BL_CMD_READ_FLASH_ADDR,
						       wbuf,
						       HIDI2C_WRITE_MAX_LENGTH,
						       error))
			return FALSE;

		rbuf_ft[0] = FU_RAYDIUM_TP_CMD2_READ;

		if (!fu_raydium_tp_hid_device_bl_read(self, rbuf_ft, 20, error))
			return FALSE;

		vid = fu_memread_uint16(rbuf_desc + FU_RAYDIUM_TP_DESC_RECORD_INFO_VID_L,
					G_LITTLE_ENDIAN);

		if ((vid == VENDOR_ID) &&
		    ((rbuf_desc[FU_RAYDIUM_TP_DESC_RECORD_INFO_PID_H] != 0xFF) ||
		     (rbuf_desc[FU_RAYDIUM_TP_DESC_RECORD_INFO_PID_L] != 0xFF))) {
			rbuf[9] = rbuf_desc[FU_RAYDIUM_TP_DESC_RECORD_INFO_PID_H];
			rbuf[10] = rbuf_desc[FU_RAYDIUM_TP_DESC_RECORD_INFO_PID_L];
			rbuf[16] = rbuf_desc[FU_RAYDIUM_TP_DESC_RECORD_INFO_VID_L];
			rbuf[17] = rbuf_desc[FU_RAYDIUM_TP_DESC_RECORD_INFO_VID_H];
		} else if ((rbuf_ft[FU_RAYDIUM_TP_FT_RECORD_INFO_PID_H] != 0xFF) ||
			   (rbuf_ft[FU_RAYDIUM_TP_FT_RECORD_INFO_PID_L] != 0xFF)) {
			rbuf[9] = rbuf_ft[FU_RAYDIUM_TP_FT_RECORD_INFO_PID_H];
			rbuf[10] = rbuf_ft[FU_RAYDIUM_TP_FT_RECORD_INFO_PID_L];
			rbuf[16] = rbuf_ft[FU_RAYDIUM_TP_FT_RECORD_INFO_VID_L];
			rbuf[17] = rbuf_ft[FU_RAYDIUM_TP_FT_RECORD_INFO_VID_H];
		}
	} else if (mode == FU_RAYDIUM_TP_BOOT_MODE_TS_MAIN) {
		wbuf[0] = GET_SYS_FW_VERSION_NUM;
		if (!fu_raydium_tp_hid_device_tp_write(
			self,
			FU_RAYDIUM_TP_CMD_ADDR_SYSTEM_INFO_MODE_WRITE,
			wbuf,
			1,
			error))
			return FALSE;

		if (!fu_raydium_tp_hid_device_tp_read(self,
						      FU_RAYDIUM_TP_CMD_ADDR_SYSTEM_INFO_MODE_READ,
						      rbuf,
						      error))
			return FALSE;
	}

	vid = fu_memread_uint16(rbuf + 16, G_LITTLE_ENDIAN);

	return vid == VENDOR_ID;
}

static gboolean
fu_raydium_tp_hid_device_update_prepare(FuRaydiumtpHidDevice *self, GError **error)
{
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_raydium_tp_hid_device_wait_boot_cb,
				  RETRY_NUM,
				  10,
				  NULL,
				  error)) {
		g_prefix_error_literal(error, "wait for boot failed: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_write_fwimage(FuRaydiumtpHidDevice *self,
				       guint8 *img,
				       guint32 base_addr,
				       guint32 img_length,
				       guint32 image_crc,
				       FuProgress *progress,
				       GError **error)
{
	g_autoptr(GError) error_local = NULL;

	if (!fu_raydium_tp_hid_device_bl_write_flash(self, img, img_length, &error_local))
		return FALSE;

	if (!fu_raydium_tp_hid_device_bl_dma_crc(self,
						 FU_RAYDIUM_TP_RAM_FIRM_BASE,
						 img_length - CRC_LEN,
						 image_crc,
						 &error_local))
		return FALSE;

	if (!fu_raydium_tp_hid_device_bl_erase_fw_flash(self, &error_local))
		return FALSE;

	return fu_raydium_tp_hid_device_bl_trig_pram_to_flash(self, &error_local);
}

static gboolean
fu_raydium_tp_hid_device_write_descimage(FuRaydiumtpHidDevice *self,
					 guint8 *img,
					 guint32 base_addr,
					 guint32 img_length,
					 guint32 image_crc,
					 FuProgress *progress,
					 GError **error)
{
	guint8 sector = (guint8)(img_length / FLASH_SECTOR_SIZE);
	g_autoptr(GError) error_local = NULL;

	if (!fu_raydium_tp_hid_device_bl_write_flash(self, img, img_length, &error_local))
		return FALSE;

	if (!fu_raydium_tp_hid_device_bl_dma_crc(self,
						 FU_RAYDIUM_TP_RAM_FIRM_BASE,
						 img_length - CRC_LEN,
						 image_crc,
						 &error_local))
		return FALSE;

	if (!fu_raydium_tp_hid_device_bl_erase_flash_sector(self, base_addr, sector, &error_local))
		return FALSE;

	return fu_raydium_tp_hid_device_bl_trig_desc_to_flash(self,
							      FU_RAYDIUM_TP_RAM_FIRM_BASE,
							      base_addr,
							      img_length,
							      &error_local);
}

static gboolean
fu_raydium_tp_hid_device_read_flash_crc(FuRaydiumtpHidDevice *self,
					guint32 base_addr,
					guint32 length,
					guint8 *out_crc,
					GError **error)
{
	guint8 rdata[5] = {0};
	guint8 wbuf[I2C_BUF_SIZE] = {0};
	guint32 addr = base_addr + length - CRC_LEN;
	guint16 crc_length = CRC_LEN;

	if (length < CRC_LEN) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "component length %u smaller than crc %u",
			    length,
			    (guint32)CRC_LEN);
		return FALSE;
	}

	fu_memwrite_uint32(wbuf + 6, addr, G_LITTLE_ENDIAN);
	fu_memwrite_uint32(wbuf + 10, crc_length, G_LITTLE_ENDIAN);

	if (!fu_raydium_tp_hid_device_bl_write(self,
					       FU_RAYDIUM_TP_CMD_BL_CMD_READ_FLASH_ADDR,
					       wbuf,
					       HIDI2C_WRITE_MAX_LENGTH,
					       error))
		return FALSE;

	rdata[0] = FU_RAYDIUM_TP_CMD2_READ;

	if (!fu_raydium_tp_hid_device_bl_read(self, rdata, sizeof(rdata), error))
		return FALSE;

	return fu_memcpy_safe(out_crc, CRC_LEN, 0, rdata, CRC_LEN, 0, CRC_LEN, error);
}

static gboolean
fu_raydium_tp_hid_device_extract_components(GInputStream *stream,
					    guint32 image_start,
					    guint32 image_length,
					    guint8 *out_buf,
					    GError **error)
{
	gssize nread;
	g_autoptr(GError) error_local = NULL;

	if (!g_seekable_seek(G_SEEKABLE(stream),
			     (goffset)image_start,
			     G_SEEK_SET,
			     NULL,
			     &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "failed to seek firmware stream: %s",
			    error_local->message);
		return FALSE;
	}

	nread = g_input_stream_read(stream, out_buf, image_length, NULL, &error_local);

	if (nread < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "failed to read firmware stream: %s",
			    error_local->message);
		return FALSE;
	}

	if ((guint32)nread != image_length) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "read %zd bytes, expected %u",
			    nread,
			    image_length);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_compare_crc(guint8 *flash_crc, guint8 *image_crc, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	return fu_memcmp_safe(flash_crc, CRC_LEN, 0, image_crc, CRC_LEN, 0, CRC_LEN, &error_local);
}

static gboolean
fu_raydium_tp_hid_device_verify_status(FuRaydiumtpHidDevice *self,
				       FuFirmware *firmware,
				       guint32 fw_start,
				       guint32 fw_length,
				       GError **error)
{
	guint8 rdata[CRC_LEN] = {0};
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GError) error_local = NULL;
	guint32 pram_lock_val = 0;
	guint32 image_fw_crc = 0;
	guint32 device_fw_crc = 0;
	guint8 crc_buf[CRC_LEN] = {0};
	gssize nread;

	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	if (fw_length < CRC_LEN) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid firmware length: %u",
			    fw_length);
		return FALSE;
	}

	if (!g_seekable_seek(G_SEEKABLE(stream),
			     (goffset)(fw_start + fw_length - CRC_LEN),
			     G_SEEK_SET,
			     NULL,
			     &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "failed to seek firmware stream: %s",
			    error_local->message);
		return FALSE;
	}

	nread = g_input_stream_read(stream, crc_buf, CRC_LEN, NULL, &error_local);
	if (nread < 0 || nread != CRC_LEN) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "failed to read firmware CRC: %s",
			    error_local->message);
		return FALSE;
	}

	image_fw_crc = fu_memread_uint32(crc_buf, G_LITTLE_ENDIAN);
	if (!fu_raydium_tp_hid_device_set_mem_addr(self,
						   FU_RAYDIUM_TP_FLASH_CTRL_PRAM_LOCK,
						   MCU_MEM,
						   error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_get_mem_read(self, rdata, error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_set_mem_addr(self,
						   FU_RAYDIUM_TP_FLASH_CTRL_PRAM_LOCK,
						   MCU_MEM,
						   error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_set_mem_write(self, pram_lock_val, error))
		return FALSE;

	pram_lock_val = fu_memread_uint32(rdata, G_LITTLE_ENDIAN);
	pram_lock_val &= ~TP_BIT(2);

	if (!fu_raydium_tp_hid_device_set_mem_addr(self,
						   FU_RAYDIUM_TP_FLASH_FIRM_CRC_ADDR,
						   MCU_MEM,
						   error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_get_mem_read(self, rdata, error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_set_mem_addr(self,
						   FU_RAYDIUM_TP_FLASH_CTRL_PRAM_LOCK,
						   MCU_MEM,
						   error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_set_mem_write(self, pram_lock_val, error))
		return FALSE;

	device_fw_crc = fu_memread_uint32(rdata, G_LITTLE_ENDIAN);

	if (device_fw_crc != image_fw_crc) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "crc check failed: device=0x%08x image=0x%08x",
			    device_fw_crc,
			    image_fw_crc);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_write_images(FuRaydiumtpHidDevice *self,
				      FuFirmware *firmware,
				      guint32 fw_base,
				      guint32 desc_base,
				      guint32 fw_start,
				      guint32 fw_length,
				      guint32 desc_start,
				      guint32 desc_length,
				      FuProgress *progress,
				      GError **error)
{
	gboolean update_fw = TRUE;
	gboolean update_desc = TRUE;
	guint8 flash_fw_crc[CRC_LEN] = {0};
	guint8 flash_desc_crc[CRC_LEN] = {0};
	guint8 image_fw_crc[CRC_LEN] = {0};
	guint8 image_desc_crc[CRC_LEN] = {0};
	guint32 target_crc = 0;
	g_autofree guint8 *pram = NULL;
	g_autofree guint8 *desc = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GError) error_local = NULL;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 5, "prepare-write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 5, "erase");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, "writing");

	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	if (fw_length > CRC_LEN) {
		pram = g_malloc(fw_length);
		if (!fu_raydium_tp_hid_device_extract_components(stream,
								 fw_start,
								 fw_length,
								 pram,
								 &error_local))
			update_fw = FALSE;
		else {
			if (!fu_memcpy_safe(image_fw_crc,
					    sizeof(image_fw_crc),
					    0,
					    pram,
					    fw_length,
					    fw_length - CRC_LEN,
					    CRC_LEN,
					    &error_local))
				return FALSE;
		}
	}

	if (desc_length > CRC_LEN) {
		desc = g_malloc(desc_length);
		if (!fu_raydium_tp_hid_device_extract_components(stream,
								 desc_start,
								 desc_length,
								 desc,
								 &error_local))
			update_desc = FALSE;
		else {
			if (!fu_memcpy_safe(image_desc_crc,
					    sizeof(image_desc_crc),
					    0,
					    desc,
					    desc_length,
					    desc_length - CRC_LEN,
					    CRC_LEN,
					    &error_local))
				return FALSE;
		}
	}

	fu_progress_step_done(progress);

	if (!fu_raydium_tp_hid_device_bl_dis_wdt_and_unlock_flash(self, &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to unlock flash: %s",
			    error_local->message);
		return FALSE;
	}

	if (fu_raydium_tp_hid_device_read_flash_protect_status(self, &error_local) !=
	    FU_RAYDIUM_TP_PROTECT_FW_UNLOCK) {
		if (!fu_raydium_tp_hid_device_write_flash_protect_status(
			self,
			FU_RAYDIUM_TP_PROTECT_FW_UNLOCK,
			&error_local)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to unlock flash protect: %s",
				    error_local->message);
			return FALSE;
		}

		if (fu_raydium_tp_hid_device_read_flash_protect_status(self, &error_local) !=
		    FU_RAYDIUM_TP_PROTECT_FW_UNLOCK) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to check flash unlock: %s",
				    error_local->message);
			return FALSE;
		}
	}

	if (update_fw) {
		if (!fu_raydium_tp_hid_device_read_flash_crc(self,
							     fw_base,
							     fw_length,
							     flash_fw_crc,
							     &error_local))
			update_fw = FALSE;
		else
			update_fw = !fu_raydium_tp_hid_device_compare_crc(flash_fw_crc,
									  image_fw_crc,
									  &error_local);
	}

	if (update_desc) {
		if (!fu_raydium_tp_hid_device_read_flash_crc(self,
							     desc_base,
							     desc_length,
							     flash_desc_crc,
							     &error_local))
			update_desc = FALSE;
		else
			update_desc = !fu_raydium_tp_hid_device_compare_crc(flash_desc_crc,
									    image_desc_crc,
									    &error_local);
	}

	fu_progress_step_done(progress);

	if (update_desc) {
		target_crc = fu_memread_uint32(image_desc_crc, G_LITTLE_ENDIAN);
		if (!fu_raydium_tp_hid_device_write_descimage(self,
							      desc,
							      desc_base,
							      desc_length,
							      target_crc,
							      fu_progress_get_child(progress),
							      &error_local)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to update desc: %s",
				    error_local->message);
			return FALSE;
		}
	}

	if (update_fw) {
		target_crc = fu_memread_uint32(image_fw_crc, G_LITTLE_ENDIAN);
		if (!fu_raydium_tp_hid_device_write_fwimage(self,
							    pram,
							    fw_base,
							    fw_length,
							    target_crc,
							    fu_progress_get_child(progress),
							    &error_local)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to update firmware: %s",
				    error_local->message);

			return FALSE;
		}
	}

	if (fu_raydium_tp_hid_device_read_flash_protect_status(self, &error_local) !=
	    FU_RAYDIUM_TP_PROTECT_ALL_LOCK) {
		if (!fu_raydium_tp_hid_device_write_flash_protect_status(
			self,
			FU_RAYDIUM_TP_PROTECT_ALL_LOCK,
			&error_local)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to lock flash protect: %s",
				    error_local->message);
			return FALSE;
		}

		if (fu_raydium_tp_hid_device_read_flash_protect_status(self, &error_local) !=
		    FU_RAYDIUM_TP_PROTECT_ALL_LOCK) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to check flash lock: %s",
				    error_local->message);
			return FALSE;
		}
	}
	fu_progress_step_done(progress);

	return TRUE;
}

static FuFirmware *
fu_raydium_tp_hid_device_prepare_firmware(FuDevice *device,
					  GInputStream *stream,
					  FuProgress *progress,
					  FuFirmwareParseFlags flags,
					  GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_raydium_tp_firmware_new();
	guint16 vid;
	guint16 pid;

	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	vid = fu_raydium_tp_firmware_get_vendor_id(FU_RAYDIUM_TP_FIRMWARE(firmware));

	if (vid != VENDOR_ID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "unexpected vendor id (expected %u)",
			    vid);
		return NULL;
	}

	pid = fu_raydium_tp_firmware_get_product_id(FU_RAYDIUM_TP_FIRMWARE(firmware));

	if (pid != fu_device_get_pid(device)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "product id mismatch (expected %u)",
			    pid);
		return NULL;
	}

	return g_steal_pointer(&firmware);
}

static gboolean
fu_raydium_tp_hid_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuRaydiumtpHidDevice *self = FU_RAYDIUM_TP_HID_DEVICE(device);
	FuRaydiumtpFirmware *ray_fw = FU_RAYDIUM_TP_FIRMWARE(firmware);
	guint32 fw_base = fu_raydium_tp_firmware_get_fw_base(ray_fw);
	guint32 desc_base = fu_raydium_tp_firmware_get_desc_base(ray_fw);
	guint32 fw_start = fu_raydium_tp_firmware_get_fw_start(ray_fw);
	guint32 fw_len = fu_raydium_tp_firmware_get_fw_len(ray_fw);
	guint32 desc_start = fu_raydium_tp_firmware_get_desc_start(ray_fw);
	guint32 desc_len = fu_raydium_tp_firmware_get_desc_len(ray_fw);

	g_autoptr(GError) error_local = NULL;
	if (!fu_raydium_tp_hid_device_check_pid(self, ray_fw) ||
	    !fu_raydium_tp_hid_device_check_vid(self, ray_fw)) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "firmware mismatch");
		return FALSE;
	}

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 3, "prepare");
	fu_progress_add_step(progress, FWUPD_STATUS_DOWNLOADING, 90, "download");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "reload");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 2, "verify");

	if (!fu_raydium_tp_hid_device_update_prepare(self, &error_local))
		return FALSE;

	fu_progress_step_done(progress);

	if (!fu_raydium_tp_hid_device_write_images(self,
						   firmware,
						   fw_base,
						   desc_base,
						   fw_start,
						   fw_len,
						   desc_start,
						   desc_len,
						   fu_progress_get_child(progress),
						   error))
		return FALSE;

	fu_progress_step_done(progress);

	if (!fu_raydium_tp_hid_device_bl_software_reset(self, &error_local))
		return FALSE;

	fu_progress_step_done(progress);

	if (!fu_raydium_tp_hid_device_verify_status(self,
						    firmware,
						    fw_start,
						    fw_len,
						    &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "update firmware unsuccessful: %s",
			    error_local->message);
		return FALSE;
	}

	fu_progress_step_done(progress);

	return TRUE;
}

static void
fu_raydium_tp_hid_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static gboolean
fu_raydium_tp_hid_device_probe(FuDevice *device, GError **error)
{
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "hidraw") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "incorrect subsystem=%s, expected hidraw",
			    fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)));
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_setup(FuDevice *device, GError **error)
{
	FuRaydiumtpHidDevice *self = FU_RAYDIUM_TP_HID_DEVICE(device);

	if (!fu_raydium_tp_hid_device_read_firmware_info(self, error)) {
		g_prefix_error_literal(error, "read firmware information failed: ");
		return FALSE;
	}

	return TRUE;
}

static gchar *
fu_raydium_tp_hid_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static void
fu_raydium_tp_hid_device_init(FuRaydiumtpHidDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_summary(FU_DEVICE(self), "Touchpad");
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_INPUT_TOUCHPAD);
	fu_device_add_protocol(FU_DEVICE(self), "com.raydium.raydiumtp");
	fu_device_set_name(FU_DEVICE(self), "Touch Controller Sensor");
	fu_device_set_vendor(FU_DEVICE(self), "Raydium");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_priority(FU_DEVICE(self), 1);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_NONBLOCK);
}

static void
fu_raydium_tp_hid_device_class_init(FuRaydiumtpHidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	device_class->to_string = fu_raydium_tp_hid_device_to_string;

	device_class->probe = fu_raydium_tp_hid_device_probe;

	device_class->setup = fu_raydium_tp_hid_device_setup;
	device_class->reload = fu_raydium_tp_hid_device_setup;

	device_class->prepare_firmware = fu_raydium_tp_hid_device_prepare_firmware;
	device_class->write_firmware = fu_raydium_tp_hid_device_write_firmware;

	device_class->set_progress = fu_raydium_tp_hid_device_set_progress;
	device_class->convert_version = fu_raydium_tp_hid_device_convert_version;
}
