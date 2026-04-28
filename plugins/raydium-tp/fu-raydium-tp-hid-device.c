/*
 * Copyright 2025 Raydium.inc <Maker.Tsai@rad-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-raydium-tp-firmware.h"
#include "fu-raydium-tp-hid-device.h"
#include "fu-raydium-tp-image.h"
#include "fu-raydium-tp-struct.h"

struct _FuRaydiumTpHidDevice {
	FuHidrawDevice parent_instance;
};

G_DEFINE_TYPE(FuRaydiumTpHidDevice, fu_raydium_tp_hid_device, FU_TYPE_HIDRAW_DEVICE)

#define RAYDIUM_VENDOR_ID	       0x2386
#define RAYDIUM_GET_SYS_FW_VERSION_NUM 1
#define RAYDIUM_MCU_MEM		       1

#define RAYDIUM_I2C_BUF_SIZE		64
#define RAYDIUM_I2C_BUF_MAXSIZE		256
#define RAYDIUM_HIDI2C_WRITE_SIZE	32
#define RAYDIUM_CRC_LEN			4
#define RAYDIUM_HIDI2C_CHK_IDX		61
#define RAYDIUM_HIDI2C_WRITE_MAX_LENGTH 49

#define RAYDIUM_RETRY_NUM     10
#define RAYDIUM_RETRY_NUM_MAX 30

#define RAYDIUM_FLASH_SECTOR_SIZE 4096

static GByteArray *
fu_raydium_tp_hid_device_get_report(FuRaydiumTpHidDevice *self, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	fu_byte_array_append_uint8(buf, FU_RAYDIUM_TP_CMD2_RID);
	fu_byte_array_set_size(buf, RAYDIUM_I2C_BUF_SIZE, 0x0);
	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					  buf->data,
					  buf->len,
					  FU_IOCTL_FLAG_NONE,
					  error))
		return NULL;
	return g_steal_pointer(&buf);
}

static gboolean
fu_raydium_tp_hid_device_set_report(FuRaydiumTpHidDevice *self, GByteArray *buf, GError **error)
{
	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					    buf->data,
					    buf->len,
					    FU_IOCTL_FLAG_NONE,
					    error);
}

static gboolean
fu_raydium_tp_hid_device_write_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuRaydiumTpHidDevice *self = FU_RAYDIUM_TP_HID_DEVICE(device);
	GByteArray *buf = (GByteArray *)user_data;
	return fu_raydium_tp_hid_device_set_report(self, buf, error);
}

static gboolean
fu_raydium_tp_hid_device_write(FuRaydiumTpHidDevice *self, GByteArray *buf, GError **error)
{
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_raydium_tp_hid_device_write_cb,
				    RAYDIUM_RETRY_NUM_MAX,
				    1,
				    buf,
				    error);
}

typedef struct {
	GByteArray *outbuf;
	GByteArray *inbuf;
} FuRaydiumTpHidReadHelper;

static gboolean
fu_raydium_tp_hid_device_read_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuRaydiumTpHidDevice *self = FU_RAYDIUM_TP_HID_DEVICE(device);
	FuRaydiumTpHidReadHelper *helper = (FuRaydiumTpHidReadHelper *)user_data;
	guint8 chk_idx = 0;
	g_autoptr(GByteArray) buf = NULL;

	if (!fu_raydium_tp_hid_device_set_report(self, helper->outbuf, error))
		return FALSE;
	buf = fu_raydium_tp_hid_device_get_report(self, error);
	if (buf == NULL)
		return FALSE;
	if (!fu_memread_uint8_safe(buf->data, buf->len, RAYDIUM_HIDI2C_CHK_IDX, &chk_idx, error))
		return FALSE;
	if (chk_idx != 0xFF && buf->data[0] != 0xFF) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_READ, "device not ready");
		return FALSE;
	}

	/* success */
	helper->inbuf = g_steal_pointer(&buf);
	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_bl_write(FuRaydiumTpHidDevice *self,
				  guint8 cmd,
				  guint8 *buf,
				  gsize bufsz,
				  guint length,
				  GError **error)
{
	g_autoptr(FuStructRaydiumTpHidPacket) st1 = fu_struct_raydium_tp_hid_packet_new();
	g_autoptr(FuStructRaydiumTpHidPacket) st2 = fu_struct_raydium_tp_hid_packet_new();

	fu_struct_raydium_tp_hid_packet_set_header3(st1, FU_RAYDIUM_TP_HID_DATA_HEADER3_WR);
	fu_struct_raydium_tp_hid_packet_set_header4(st1, FU_RAYDIUM_TP_HID_DATA_HEADER4_WR);
	fu_struct_raydium_tp_hid_packet_set_data0(st1, FU_RAYDIUM_TP_CMD2_WRT);
	fu_struct_raydium_tp_hid_packet_set_data2(st1, cmd);
	fu_struct_raydium_tp_hid_packet_set_data3(st1, buf[3]);
	fu_struct_raydium_tp_hid_packet_set_data4(st1, buf[4]);
	fu_struct_raydium_tp_hid_packet_set_data5(st1, buf[5]);
	fu_struct_raydium_tp_hid_packet_set_length(st1, length);

	if (!fu_memcpy_safe(st1->buf->data,
			    st1->buf->len,
			    15, /* dst */
			    buf,
			    bufsz,
			    6, /* src */
			    length,
			    error))
		return FALSE;
	if (!fu_raydium_tp_hid_device_write(self, st1->buf, error)) {
		g_prefix_error_literal(error, "wait bl write status failed: ");
		return FALSE;
	}

	fu_struct_raydium_tp_hid_packet_set_header3(st2, FU_RAYDIUM_TP_HID_DATA_HEADER3_WR);
	fu_struct_raydium_tp_hid_packet_set_header4(st2, FU_RAYDIUM_TP_HID_DATA_HEADER4_WR);
	fu_struct_raydium_tp_hid_packet_set_data0(st2, FU_RAYDIUM_TP_CMD2_ACK);
	fu_struct_raydium_tp_hid_packet_set_length(st2, length);
	return fu_raydium_tp_hid_device_write(self, st2->buf, error);
}

static gboolean
fu_raydium_tp_hid_device_bl_read(FuRaydiumTpHidDevice *self,
				 guint8 *rcv_buf,
				 gsize rcv_bufsz,
				 guint length,
				 GError **error)
{
	FuRaydiumTpHidReadHelper helper = {0};
	guint8 wait_idle_flag = 0;
	g_autoptr(GByteArray) inbuf = NULL;
	g_autoptr(FuStructRaydiumTpHidPacket) st = fu_struct_raydium_tp_hid_packet_new();

	if (rcv_buf[1] == 0xFF) {
		wait_idle_flag = 1;
		rcv_buf[1] = 0x00;
	}

	fu_struct_raydium_tp_hid_packet_set_header3(st, FU_RAYDIUM_TP_HID_DATA_HEADER3_RD);
	fu_struct_raydium_tp_hid_packet_set_header4(st, FU_RAYDIUM_TP_HID_DATA_HEADER4_RD);
	fu_struct_raydium_tp_hid_packet_set_data0(st, rcv_buf[0]);
	fu_struct_raydium_tp_hid_packet_set_data1(st, rcv_buf[1]);
	fu_struct_raydium_tp_hid_packet_set_data2(st, rcv_buf[2]);
	fu_struct_raydium_tp_hid_packet_set_data3(st, rcv_buf[3]);
	fu_struct_raydium_tp_hid_packet_set_data4(st, rcv_buf[4]);
	fu_struct_raydium_tp_hid_packet_set_data5(st, rcv_buf[5]);
	fu_struct_raydium_tp_hid_packet_set_length(st, length);

	helper.outbuf = st->buf;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_raydium_tp_hid_device_read_cb,
				  RAYDIUM_RETRY_NUM,
				  1,
				  &helper,
				  error)) {
		g_prefix_error_literal(error, "wait bl read status failed: ");
		return FALSE;
	}
	inbuf = helper.inbuf;

	if (wait_idle_flag == 1) {
		return fu_memcpy_safe(rcv_buf,
				      rcv_bufsz,
				      0, /* dst */
				      inbuf->data,
				      inbuf->len,
				      0, /* src */
				      rcv_bufsz,
				      error);
	} else {
		return fu_memcpy_safe(rcv_buf,
				      rcv_bufsz,
				      0, /* dst */
				      inbuf->data,
				      inbuf->len,
				      1, /* src */
				      rcv_bufsz - 1,
				      error);
	}
}

static gboolean
fu_raydium_tp_hid_device_tp_write(FuRaydiumTpHidDevice *self,
				  guint8 cmd,
				  guint8 *buf,
				  gsize bufsz,
				  guint length,
				  GError **error)
{
	g_autoptr(FuStructRaydiumTpHidPacket) st1 = fu_struct_raydium_tp_hid_packet_new();
	g_autoptr(FuStructRaydiumTpHidPacket) st2 = fu_struct_raydium_tp_hid_packet_new();

	/* sanity check */
	if (length > G_MAXUINT8 - 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "write length too large for packet");
		return FALSE;
	}

	fu_struct_raydium_tp_hid_packet_set_header3(st1, FU_RAYDIUM_TP_HID_DATA_HEADER3_WR);
	fu_struct_raydium_tp_hid_packet_set_header4(st1, FU_RAYDIUM_TP_HID_DATA_HEADER4_WR);
	fu_struct_raydium_tp_hid_packet_set_data0(st1, FU_RAYDIUM_TP_CMD2_WRT);
	fu_struct_raydium_tp_hid_packet_set_data2(st1, (guint8)(length + 1));
	fu_struct_raydium_tp_hid_packet_set_data3(st1, cmd);
	if (!fu_memcpy_safe(st1->buf->data, st1->buf->len, 11, buf, bufsz, 0, length, error))
		return FALSE;
	if (!fu_raydium_tp_hid_device_write(self, st1->buf, error)) {
		g_prefix_error_literal(error, "wait tp write status failed: ");
		return FALSE;
	}

	fu_struct_raydium_tp_hid_packet_set_header3(st2, FU_RAYDIUM_TP_HID_DATA_HEADER3_WR);
	fu_struct_raydium_tp_hid_packet_set_header4(st2, FU_RAYDIUM_TP_HID_DATA_HEADER4_WR);
	fu_struct_raydium_tp_hid_packet_set_data0(st2, FU_RAYDIUM_TP_CMD2_ACK);
	return fu_raydium_tp_hid_device_write(self, st2->buf, error);
}

static gboolean
fu_raydium_tp_hid_device_tp_read(FuRaydiumTpHidDevice *self,
				 guint8 cmd,
				 guint8 *rcv_buf,
				 gsize rcv_bufsz,
				 GError **error)
{
	FuRaydiumTpHidReadHelper helper = {0};
	g_autoptr(FuStructRaydiumTpHidPacket) st = fu_struct_raydium_tp_hid_packet_new();
	g_autoptr(GByteArray) inbuf = NULL;

	fu_struct_raydium_tp_hid_packet_set_header3(st, FU_RAYDIUM_TP_HID_DATA_HEADER3_RD);
	fu_struct_raydium_tp_hid_packet_set_header4(st, FU_RAYDIUM_TP_HID_DATA_HEADER4_RD);
	fu_struct_raydium_tp_hid_packet_set_data0(st, FU_RAYDIUM_TP_CMD2_READ);
	fu_struct_raydium_tp_hid_packet_set_data3(st, FU_RAYDIUM_TP_HID_DATA_HEADER10);
	fu_struct_raydium_tp_hid_packet_set_data4(st, cmd);

	helper.outbuf = st->buf;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_raydium_tp_hid_device_read_cb,
				  RAYDIUM_RETRY_NUM_MAX,
				  1,
				  &helper,
				  error)) {
		g_prefix_error_literal(error, "wait tp read status failed: ");
		return FALSE;
	}
	inbuf = helper.inbuf;

	return fu_memcpy_safe(rcv_buf,
			      rcv_bufsz,
			      0,
			      inbuf->data,
			      inbuf->len,
			      1,
			      RAYDIUM_I2C_BUF_SIZE - 1,
			      error);
}

static gboolean
fu_raydium_tp_hid_device_set_bl_mem(FuRaydiumTpHidDevice *self,
				    guint32 addr,
				    guint32 value,
				    guint size,
				    GError **error)
{
	guint8 buf[RAYDIUM_I2C_BUF_SIZE] = {0};

	fu_memwrite_uint32(buf + 6, addr, G_LITTLE_ENDIAN);
	fu_memwrite_uint32(buf + 10, value, G_LITTLE_ENDIAN);
	return fu_raydium_tp_hid_device_bl_write(self,
						 FU_RAYDIUM_TP_BL_CMD_WRITE_REGISTER,
						 buf,
						 sizeof(buf),
						 RAYDIUM_HIDI2C_WRITE_MAX_LENGTH,
						 error);
}

static gboolean
fu_raydium_tp_hid_device_get_bl_mem(FuRaydiumTpHidDevice *self,
				    guint32 addr,
				    guint16 length,
				    guint8 *outbuf,
				    gsize outbufsz,
				    GError **error)
{
	guint8 buf[RAYDIUM_I2C_BUF_SIZE] = {0};

	fu_memwrite_uint32(buf + 6, addr, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(buf + 10, length, G_LITTLE_ENDIAN);
	if (!fu_raydium_tp_hid_device_bl_write(self,
					       FU_RAYDIUM_TP_BL_CMD_READ_ADDRESS_MEMORY,
					       buf,
					       sizeof(buf),
					       RAYDIUM_HIDI2C_WRITE_MAX_LENGTH,
					       error))
		return FALSE;

	outbuf[0] = FU_RAYDIUM_TP_CMD2_READ;
	return fu_raydium_tp_hid_device_bl_read(self, outbuf, outbufsz, length, error);
}

static gboolean
fu_raydium_tp_hid_device_jump_to_boot(FuRaydiumTpHidDevice *self, GError **error)
{
	guint8 buf[RAYDIUM_I2C_BUF_SIZE] = {0};

	return fu_raydium_tp_hid_device_tp_write(self,
						 FU_RAYDIUM_TP_ADDR_JUMP_TO_BOOTLOADER,
						 buf,
						 sizeof(buf),
						 1,
						 error);
}

static gboolean
fu_raydium_tp_hid_device_read_status_cb(FuDevice *device, gpointer userdata, GError **error)
{
	FuRaydiumTpHidDevice *self = FU_RAYDIUM_TP_HID_DEVICE(device);
	FuRaydiumTpBootMode *status = (FuRaydiumTpBootMode *)userdata;
	guint8 data[RAYDIUM_I2C_BUF_SIZE] = {0};

	data[0] = FU_RAYDIUM_TP_CMD2_CHK;
	if (!fu_raydium_tp_hid_device_bl_read(self, data, sizeof(data), 7, error))
		return FALSE;
	if (fu_memcmp_safe(data, sizeof(data), 0, (const guint8 *)"firm", 4, 0, 4, NULL))
		*status = FU_RAYDIUM_TP_BOOT_MODE_TS_MAIN;
	else if (fu_memcmp_safe(data, sizeof(data), 0, (const guint8 *)"boot", 4, 0, 4, NULL))
		*status = FU_RAYDIUM_TP_BOOT_MODE_TS_BLDR;
	else
		*status = FU_RAYDIUM_TP_BOOT_MODE_TS_NONE;
	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_read_status(FuRaydiumTpHidDevice *self,
				     FuRaydiumTpBootMode *status,
				     GError **error)
{
	return fu_device_retry(FU_DEVICE(self),
			       fu_raydium_tp_hid_device_read_status_cb,
			       RAYDIUM_RETRY_NUM,
			       status,
			       error);
}

static gboolean
fu_raydium_tp_hid_device_wait_main_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuRaydiumTpHidDevice *self = FU_RAYDIUM_TP_HID_DEVICE(device);
	FuRaydiumTpBootMode mode = FU_RAYDIUM_TP_BOOT_MODE_TS_NONE;

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_KEY_RESET_REG,
						 FU_RAYDIUM_TP_KEY_RESET_VALUE,
						 8,
						 error))
		return FALSE;
	fu_device_sleep(device, 10);
	if (!fu_raydium_tp_hid_device_read_status(self, &mode, error))
		return FALSE;
	if (mode != FU_RAYDIUM_TP_BOOT_MODE_TS_MAIN) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "device not in main mode");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_wait_boot_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuRaydiumTpHidDevice *self = FU_RAYDIUM_TP_HID_DEVICE(device);
	FuRaydiumTpBootMode mode = FU_RAYDIUM_TP_BOOT_MODE_TS_NONE;

	if (!fu_raydium_tp_hid_device_jump_to_boot(self, error))
		return FALSE;
	fu_device_sleep(device, 10);
	if (!fu_raydium_tp_hid_device_read_status(self, &mode, error))
		return FALSE;
	if (mode != FU_RAYDIUM_TP_BOOT_MODE_TS_BLDR) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "device not in boot mode");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_wait_dma_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuRaydiumTpHidDevice *self = FU_RAYDIUM_TP_HID_DEVICE(device);
	FuRaydiumTpHidReadHelper *helper = (FuRaydiumTpHidReadHelper *)user_data;

	if (!fu_raydium_tp_hid_device_get_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_DMA_IER,
						 5,
						 helper->outbuf->data,
						 helper->outbuf->len,
						 error))
		return FALSE;
	if (FU_BIT_IS_SET(helper->outbuf->data[2], 7)) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_READ, "dma still ongoing...");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_wait_idle_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuRaydiumTpHidDevice *self = FU_RAYDIUM_TP_HID_DEVICE(device);
	FuRaydiumTpBlCmd boot_main_state;
	guint8 chk_idx = 0;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	fu_byte_array_append_uint8(buf, FU_RAYDIUM_TP_CMD2_CHK);
	fu_byte_array_append_uint8(buf, 0xFF);
	fu_byte_array_set_size(buf, RAYDIUM_I2C_BUF_SIZE, 0x0);

	if (!fu_raydium_tp_hid_device_bl_read(self, buf->data, buf->len, 6, error))
		return FALSE;
	if (!fu_memread_uint8_safe(buf->data, buf->len, RAYDIUM_HIDI2C_CHK_IDX, &chk_idx, error))
		return FALSE;
	boot_main_state = chk_idx;
	if (boot_main_state != FU_RAYDIUM_TP_BL_CMD_IDLE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "not idle; got %s",
			    fu_raydium_tp_bl_cmd_to_string(boot_main_state));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_wait_for_idle_boot(FuRaydiumTpHidDevice *self, GError **error)
{
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_raydium_tp_hid_device_wait_idle_cb,
				    RAYDIUM_RETRY_NUM_MAX,
				    10,
				    NULL,
				    error);
}

static gboolean
fu_raydium_tp_hid_device_bl_set_wdt(FuRaydiumTpHidDevice *self, guint8 enable, GError **error)
{
	guint8 buf[RAYDIUM_I2C_BUF_SIZE] = {0};

	if (enable == 1)
		buf[3] = FU_RAYDIUM_TP_CMD_BL_WATCHDOG_ENABLE;
	else
		buf[3] = FU_RAYDIUM_TP_CMD_BL_WATCHDOG_DISABLE;

	return fu_raydium_tp_hid_device_bl_write(self,
						 FU_RAYDIUM_TP_BL_CMD_WATCHDOG_FUNCTION_SET,
						 buf,
						 sizeof(buf),
						 RAYDIUM_HIDI2C_WRITE_MAX_LENGTH,
						 error);
}

static gboolean
fu_raydium_tp_hid_device_bl_dis_wdt_and_unlock_flash(FuRaydiumTpHidDevice *self, GError **error)
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
fu_raydium_tp_hid_device_bl_erase_fw_flash(FuRaydiumTpHidDevice *self, GError **error)
{
	guint8 buf[RAYDIUM_I2C_BUF_SIZE] = {0};

	buf[3] = FU_RAYDIUM_TP_CMD_BL_ERASE_FLASH_MODE1;
	if (!fu_raydium_tp_hid_device_bl_write(self,
					       FU_RAYDIUM_TP_BL_CMD_ERASE_FLASH,
					       buf,
					       sizeof(buf),
					       RAYDIUM_HIDI2C_WRITE_MAX_LENGTH,
					       error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 100);
	return fu_raydium_tp_hid_device_wait_for_idle_boot(self, error);
}

static gboolean
fu_raydium_tp_hid_device_bl_erase_flash_sector(FuRaydiumTpHidDevice *self,
					       guint32 addr,
					       guint8 loop,
					       GError **error)
{
	guint8 buf[RAYDIUM_I2C_BUF_SIZE] = {0};

	buf[3] = FU_RAYDIUM_TP_CMD_BL_ERASE_FLASH_MODE4;
	fu_memwrite_uint32(buf + 7, addr, G_LITTLE_ENDIAN);
	buf[11] = loop;

	if (!fu_raydium_tp_hid_device_bl_write(self,
					       FU_RAYDIUM_TP_BL_CMD_ERASE_FLASH,
					       buf,
					       sizeof(buf),
					       RAYDIUM_HIDI2C_WRITE_MAX_LENGTH,
					       error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 1);
	return fu_raydium_tp_hid_device_wait_for_idle_boot(self, error);
}

static gboolean
fu_raydium_tp_hid_device_bl_write_flash_chunk(FuRaydiumTpHidDevice *self,
					      FuChunk *chunk,
					      guint cur_write_page_no,
					      guint8 sub_page_no,
					      GError **error)
{
	guint8 buf[RAYDIUM_I2C_BUF_SIZE] = {0};

	fu_memwrite_uint16(buf + 3, cur_write_page_no, G_LITTLE_ENDIAN);
	buf[5] = sub_page_no;

	if (!fu_memcpy_safe(buf,
			    RAYDIUM_I2C_BUF_SIZE,
			    6,
			    fu_chunk_get_data(chunk),
			    fu_chunk_get_data_sz(chunk),
			    0,
			    fu_chunk_get_data_sz(chunk),
			    error))
		return FALSE;

	return fu_raydium_tp_hid_device_bl_write(self,
						 FU_RAYDIUM_TP_BL_CMD_WRITE_HID_I2C_FLASH,
						 buf,
						 sizeof(buf),
						 RAYDIUM_HIDI2C_WRITE_SIZE,
						 error);
}

static gboolean
fu_raydium_tp_hid_device_bl_write_flash(FuRaydiumTpHidDevice *self,
					FuFirmware *img,
					FuProgress *progress,
					GError **error)
{
	guint page_no = 0;
	guint8 sub_page_no = 0;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	stream = fu_firmware_get_stream(img, error);
	if (stream == NULL)
		return FALSE;
	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						RAYDIUM_HIDI2C_WRITE_SIZE,
						error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRFUNC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_raydium_tp_hid_device_bl_write_flash_chunk(self,
								   chk,
								   page_no,
								   sub_page_no,
								   error))
			return FALSE;

		sub_page_no++;
		if (sub_page_no == 4) {
			sub_page_no = 0;
			page_no++;
			if (!fu_raydium_tp_hid_device_wait_for_idle_boot(self, error))
				return FALSE;
		}

		/* done */
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_bl_dma_crc(FuRaydiumTpHidDevice *self,
				    guint32 base_addr,
				    guint32 img_length,
				    guint32 image_crc,
				    GError **error)
{
	guint32 value = 0;
	guint32 calculated_crc = 0;
	g_autoptr(GByteArray) rbuf = g_byte_array_new();
	FuRaydiumTpHidReadHelper helper = {
	    .outbuf = rbuf,
	    .inbuf = NULL,
	};

	fu_byte_array_set_size(rbuf, RAYDIUM_I2C_BUF_SIZE, 0x0);
	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_DMA_SADDR,
						 base_addr,
						 8,
						 error))
		return FALSE;
	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_DMA_EADDR,
						 base_addr + img_length - RAYDIUM_CRC_LEN,
						 8,
						 error))
		return FALSE;
	if (!fu_raydium_tp_hid_device_get_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_DMA_IER,
						 5,
						 rbuf->data,
						 rbuf->len,
						 error))
		return FALSE;

	value = fu_memread_uint32(rbuf->data, G_LITTLE_ENDIAN);
	FU_BIT_CLEAR(value, 16);

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
						 rbuf->data,
						 rbuf->len,
						 error))
		return FALSE;

	value = fu_memread_uint32(rbuf->data, G_LITTLE_ENDIAN);
	FU_BIT_SET(value, 17);

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_DMA_IER,
						 value,
						 8,
						 error))
		return FALSE;
	if (!fu_raydium_tp_hid_device_get_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_DMA_CR,
						 5,
						 rbuf->data,
						 rbuf->len,
						 error))
		return FALSE;

	value = fu_memread_uint32(rbuf->data, G_LITTLE_ENDIAN);
	FU_BIT_SET(value, 23);

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_DMA_CR,
						 value,
						 8,
						 error))
		return FALSE;

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_raydium_tp_hid_device_wait_dma_cb,
				  RAYDIUM_RETRY_NUM_MAX,
				  1,
				  &helper,
				  error)) {
		g_prefix_error_literal(error, "wait dma status failed: ");
		return FALSE;
	}

	if (!fu_raydium_tp_hid_device_get_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_DMA_RES,
						 5,
						 rbuf->data,
						 rbuf->len,
						 error))
		return FALSE;

	calculated_crc = fu_memread_uint32(rbuf->data, G_LITTLE_ENDIAN);
	if (image_crc != calculated_crc) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "crc check failed: expected 0x%08x, got 0x%08x",
			    image_crc,
			    calculated_crc);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_bl_trig_desc_to_flash(FuRaydiumTpHidDevice *self,
					       guint32 pram_addr,
					       guint32 flash_addr,
					       guint16 length,
					       GError **error)
{
	guint8 buf[RAYDIUM_I2C_BUF_SIZE] = {0};

	buf[3] = FU_RAYDIUM_TP_BL_CMD_WRITE_RAM_FLASH;
	buf[4] = FU_RAYDIUM_TP_HID_DATA_HEADER5;
	fu_memwrite_uint32(buf + 8, pram_addr, G_LITTLE_ENDIAN);
	fu_memwrite_uint32(buf + 12, flash_addr, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(buf + 16, length, G_LITTLE_ENDIAN);

	if (!fu_raydium_tp_hid_device_bl_write(self,
					       FU_RAYDIUM_TP_BL_CMD_WRITE_RAM_FLASH,
					       buf,
					       sizeof(buf),
					       RAYDIUM_HIDI2C_WRITE_MAX_LENGTH,
					       error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 100);
	return fu_raydium_tp_hid_device_wait_for_idle_boot(self, error);
}

static gboolean
fu_raydium_tp_hid_device_bl_trig_pram_to_flash(FuRaydiumTpHidDevice *self, GError **error)
{
	guint8 buf[RAYDIUM_I2C_BUF_SIZE] = {0};

	buf[0] = FU_RAYDIUM_TP_CMD2_WRT;
	buf[2] = FU_RAYDIUM_TP_BL_CMD_TRIGGER_WRITE_FLASH;

	if (!fu_raydium_tp_hid_device_bl_write(self,
					       FU_RAYDIUM_TP_BL_CMD_TRIGGER_WRITE_FLASH,
					       buf,
					       sizeof(buf),
					       RAYDIUM_HIDI2C_WRITE_MAX_LENGTH,
					       error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 100);
	return fu_raydium_tp_hid_device_wait_for_idle_boot(self, error);
}

static gboolean
fu_raydium_tp_hid_device_bl_software_reset(FuRaydiumTpHidDevice *self, GError **error)
{
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_raydium_tp_hid_device_wait_main_cb,
				  RAYDIUM_RETRY_NUM,
				  1000,
				  NULL,
				  error)) {
		g_prefix_error_literal(error, "wait for main failed: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_set_mem_addr(FuRaydiumTpHidDevice *self,
				      guint32 addr,
				      guint8 type,
				      GError **error)
{
	guint8 buf[RAYDIUM_I2C_BUF_SIZE] = {0};

	fu_memwrite_uint32(buf, addr, G_LITTLE_ENDIAN);
	buf[4] = type;

	return fu_raydium_tp_hid_device_tp_write(self,
						 FU_RAYDIUM_TP_ADDR_MEM_ADDRESS_SET,
						 buf,
						 sizeof(buf),
						 5,
						 error);
}

static gboolean
fu_raydium_tp_hid_device_set_mem_write(FuRaydiumTpHidDevice *self, guint32 value, GError **error)
{
	guint8 buf[RAYDIUM_I2C_BUF_SIZE] = {0};

	fu_memwrite_uint32(buf, value, G_LITTLE_ENDIAN);
	return fu_raydium_tp_hid_device_tp_write(self,
						 FU_RAYDIUM_TP_ADDR_MEM_WRITE,
						 buf,
						 sizeof(buf),
						 4,
						 error);
}

static gboolean
fu_raydium_tp_hid_device_get_mem_read(FuRaydiumTpHidDevice *self,
				      guint8 *ram,
				      gsize ramsz,
				      GError **error)
{
	guint8 rbuf[RAYDIUM_I2C_BUF_SIZE] = {0};

	if (!fu_raydium_tp_hid_device_tp_read(self,
					      FU_RAYDIUM_TP_ADDR_MEM_READ,
					      rbuf,
					      sizeof(rbuf),
					      error))
		return FALSE;

	return fu_memcpy_safe(ram, ramsz, 0, rbuf, sizeof(rbuf), 0, 4, error);
}

static gboolean
fu_raydium_tp_hid_device_read_flash_protect_status(FuRaydiumTpHidDevice *self,
						   FuRaydiumTpProtect *status,
						   GError **error)
{
	guint8 rbuf[RAYDIUM_I2C_BUF_SIZE] = {0};
	guint32 value = 0;

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_LENGTH,
						 FU_RAYDIUM_TP_KEY_FLREAD_STATUS,
						 8,
						 error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_wait_for_idle_boot(self, error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_get_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_ISPCTL,
						 5,
						 rbuf,
						 sizeof(rbuf),
						 error))
		return FALSE;

	value = fu_memread_uint32(rbuf, G_LITTLE_ENDIAN);
	FU_BIT_SET(value, 11);

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_ISPCTL,
						 value,
						 8,
						 error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_wait_for_idle_boot(self, error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_get_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_DATA,
						 5,
						 rbuf,
						 sizeof(rbuf),
						 error))
		return FALSE;

	*status = rbuf[0];
	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_write_flash_protect_status(FuRaydiumTpHidDevice *self,
						    guint8 status,
						    GError **error)
{
	guint8 rbuf[RAYDIUM_I2C_BUF_SIZE] = {0};
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
						 sizeof(rbuf),
						 error))
		return FALSE;

	value = fu_memread_uint32(rbuf, G_LITTLE_ENDIAN);
	FU_BIT_SET(value, 11);

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
						 sizeof(rbuf),
						 error))
		return FALSE;

	value = fu_memread_uint32(rbuf, G_LITTLE_ENDIAN);
	FU_BIT_SET(value, 11);

	if (!fu_raydium_tp_hid_device_set_bl_mem(self,
						 FU_RAYDIUM_TP_FLASH_CTRL_ISPCTL,
						 value,
						 8,
						 error))
		return FALSE;

	return fu_raydium_tp_hid_device_wait_for_idle_boot(self, error);
}

static gboolean
fu_raydium_tp_hid_device_ensure_version_bldr_fallback(FuRaydiumTpHidDevice *self, GError **error)
{
	guint8 buf[RAYDIUM_I2C_BUF_SIZE] = {0};
	guint8 major_ver;
	guint8 minor_ver;
	g_autoptr(FuStructRaydiumTpFtRecordInfo) st_info = NULL;

	buf[0] = FU_RAYDIUM_TP_CMD2_READ;
	if (!fu_raydium_tp_hid_device_bl_read(self, buf, sizeof(buf), 20, error))
		return FALSE;
	st_info = fu_struct_raydium_tp_ft_record_info_parse(buf, sizeof(buf), 0x0, error);
	if (st_info == NULL)
		return FALSE;
	major_ver = fu_struct_raydium_tp_ft_record_info_get_version_major(st_info);
	minor_ver = fu_struct_raydium_tp_ft_record_info_get_version_minor(st_info);

	/* success */
	fu_device_set_version_raw(FU_DEVICE(self), (((guint32)major_ver) << 24) | minor_ver);
	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_ensure_version_bldr(FuRaydiumTpHidDevice *self, GError **error)
{
	guint8 wbuf[RAYDIUM_I2C_BUF_SIZE] = {0};
	guint8 rbuf[RAYDIUM_I2C_BUF_SIZE] = {0};
	guint16 vid;
	guint8 major_ver;
	guint8 minor_ver;
	g_autoptr(FuStructRaydiumTpDescRecordInfo) st_info = NULL;

	fu_memwrite_uint32(wbuf + 6, FU_RAYDIUM_TP_FLASH_DESC_RECORD_ADDR, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(wbuf + 10, RAYDIUM_HIDI2C_WRITE_SIZE, G_LITTLE_ENDIAN);
	if (!fu_raydium_tp_hid_device_bl_write(self,
					       FU_RAYDIUM_TP_BL_CMD_READ_FLASH_ADDR,
					       wbuf,
					       sizeof(wbuf),
					       RAYDIUM_HIDI2C_WRITE_MAX_LENGTH,
					       error))
		return FALSE;

	rbuf[0] = FU_RAYDIUM_TP_CMD2_READ;
	if (!fu_raydium_tp_hid_device_bl_read(self, rbuf, sizeof(rbuf), 40, error))
		return FALSE;
	st_info = fu_struct_raydium_tp_desc_record_info_parse(rbuf, sizeof(rbuf), 0x0, error);
	if (st_info == NULL)
		return FALSE;
	vid = fu_struct_raydium_tp_desc_record_info_get_vid(st_info);
	if (vid != RAYDIUM_VENDOR_ID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unexpected vendor id 0x%04x",
			    vid);
		return FALSE;
	}
	major_ver = fu_struct_raydium_tp_desc_record_info_get_rev(st_info) >> 8;
	minor_ver = fu_struct_raydium_tp_desc_record_info_get_rev(st_info) & 0xFF;

	fu_memwrite_uint32(wbuf + 6, FU_RAYDIUM_TP_FLASH_FT_RECORD_ADDR, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(wbuf + 10, 16, G_LITTLE_ENDIAN); /* length */
	if (!fu_raydium_tp_hid_device_bl_write(self,
					       FU_RAYDIUM_TP_BL_CMD_READ_FLASH_ADDR,
					       wbuf,
					       sizeof(wbuf),
					       RAYDIUM_HIDI2C_WRITE_MAX_LENGTH,
					       error))
		return FALSE;

	/* try harder */
	if (fu_struct_raydium_tp_desc_record_info_get_pid(st_info) == 0xFFFF)
		return fu_raydium_tp_hid_device_ensure_version_bldr_fallback(self, error);

	/* success */
	fu_device_set_version_raw(FU_DEVICE(self), (((guint32)major_ver) << 24) | minor_ver);
	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_ensure_version_main(FuRaydiumTpHidDevice *self, GError **error)
{
	guint16 vid;
	guint8 major_ver;
	guint8 minor_ver;
	guint8 rbuf[RAYDIUM_I2C_BUF_SIZE] = {0};
	guint8 wbuf[RAYDIUM_I2C_BUF_SIZE] = {0};

	wbuf[0] = RAYDIUM_GET_SYS_FW_VERSION_NUM;
	if (!fu_raydium_tp_hid_device_tp_write(self,
					       FU_RAYDIUM_TP_ADDR_SYSTEM_INFO_MODE_WRITE,
					       wbuf,
					       sizeof(wbuf),
					       1,
					       error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_tp_read(self,
					      FU_RAYDIUM_TP_ADDR_SYSTEM_INFO_MODE_READ,
					      rbuf,
					      sizeof(rbuf),
					      error))
		return FALSE;
	vid = fu_memread_uint16(rbuf + 16, G_LITTLE_ENDIAN);
	if (vid != RAYDIUM_VENDOR_ID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unexpected vendor id 0x%04x",
			    vid);
		return FALSE;
	}
	major_ver = rbuf[5];
	minor_ver = rbuf[6];
	fu_device_set_version_raw(FU_DEVICE(self), (((guint32)major_ver) << 24) | minor_ver);

	/* success */
	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_ensure_version(FuRaydiumTpHidDevice *self, GError **error)
{
	FuRaydiumTpBootMode mode = FU_RAYDIUM_TP_BOOT_MODE_TS_NONE;

	if (!fu_raydium_tp_hid_device_read_status(self, &mode, error))
		return FALSE;
	if (mode == FU_RAYDIUM_TP_BOOT_MODE_TS_BLDR)
		return fu_raydium_tp_hid_device_ensure_version_bldr(self, error);
	if (mode == FU_RAYDIUM_TP_BOOT_MODE_TS_MAIN)
		return fu_raydium_tp_hid_device_ensure_version_main(self, error);

	/* failed */
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "wrong boot mode");
	return FALSE;
}

static gboolean
fu_raydium_tp_hid_device_update_prepare(FuRaydiumTpHidDevice *self, GError **error)
{
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_raydium_tp_hid_device_wait_boot_cb,
				  RAYDIUM_RETRY_NUM,
				  10,
				  NULL,
				  error)) {
		g_prefix_error_literal(error, "wait for boot failed: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_write_fwimage(FuRaydiumTpHidDevice *self,
				       FuFirmware *img,
				       FuProgress *progress,
				       GError **error)
{
	gsize img_length = fu_firmware_get_size(img);
	guint32 image_crc = fu_raydium_tp_image_get_checksum(FU_RAYDIUM_TP_IMAGE(img));

	if (img_length < RAYDIUM_CRC_LEN) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "firmware image too small");
		return FALSE;
	}

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 10, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 45, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 40, "attach");

	/* write */
	if (!fu_raydium_tp_hid_device_bl_write_flash(self,
						     img,
						     fu_progress_get_child(progress),
						     error))
		return FALSE;
	if (!fu_raydium_tp_hid_device_bl_dma_crc(self,
						 FU_RAYDIUM_TP_RAM_FIRM_BASE,
						 img_length - RAYDIUM_CRC_LEN,
						 image_crc,
						 error))
		return FALSE;
	fu_progress_step_done(progress);

	/* erase */
	if (!fu_raydium_tp_hid_device_bl_erase_fw_flash(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* trigger */
	if (!fu_raydium_tp_hid_device_bl_trig_pram_to_flash(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_write_descimage(FuRaydiumTpHidDevice *self,
					 FuFirmware *img,
					 FuProgress *progress,
					 GError **error)
{
	gsize img_length = fu_firmware_get_size(img);
	guint32 image_crc = fu_raydium_tp_image_get_checksum(FU_RAYDIUM_TP_IMAGE(img));
	guint8 sector = (guint8)(img_length / RAYDIUM_FLASH_SECTOR_SIZE);

	if (img_length < RAYDIUM_CRC_LEN || img_length > G_MAXUINT16) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "firmware image size invalid: 0x%x",
			    (guint)img_length);
		return FALSE;
	}

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 10, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 45, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 40, "attach");

	if (!fu_raydium_tp_hid_device_bl_write_flash(self,
						     img,
						     fu_progress_get_child(progress),
						     error))
		return FALSE;
	if (!fu_raydium_tp_hid_device_bl_dma_crc(self,
						 FU_RAYDIUM_TP_RAM_FIRM_BASE,
						 img_length - RAYDIUM_CRC_LEN,
						 image_crc,
						 error))
		return FALSE;
	fu_progress_step_done(progress);

	/* erase */
	if (!fu_raydium_tp_hid_device_bl_erase_flash_sector(self,
							    fu_firmware_get_addr(img),
							    sector,
							    error))
		return FALSE;
	fu_progress_step_done(progress);

	/* trigger */
	if (!fu_raydium_tp_hid_device_bl_trig_desc_to_flash(self,
							    FU_RAYDIUM_TP_RAM_FIRM_BASE,
							    fu_firmware_get_addr(img),
							    img_length,
							    error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_read_flash_crc(FuRaydiumTpHidDevice *self,
					guint32 base_addr,
					guint32 length,
					guint32 *out_crc,
					GError **error)
{
	guint8 rdata[RAYDIUM_I2C_BUF_SIZE] = {0};
	guint8 wbuf[RAYDIUM_I2C_BUF_SIZE] = {0};
	guint32 addr = base_addr + length - RAYDIUM_CRC_LEN;
	guint32 crc_length = RAYDIUM_CRC_LEN;

	if (length < RAYDIUM_CRC_LEN) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "component length %u smaller than crc %u",
			    length,
			    (guint32)RAYDIUM_CRC_LEN);
		return FALSE;
	}

	fu_memwrite_uint32(wbuf + 6, addr, G_LITTLE_ENDIAN);
	fu_memwrite_uint32(wbuf + 10, crc_length, G_LITTLE_ENDIAN);
	if (!fu_raydium_tp_hid_device_bl_write(self,
					       FU_RAYDIUM_TP_BL_CMD_READ_FLASH_ADDR,
					       wbuf,
					       sizeof(wbuf),
					       RAYDIUM_HIDI2C_WRITE_MAX_LENGTH,
					       error))
		return FALSE;

	rdata[0] = FU_RAYDIUM_TP_CMD2_READ;

	if (!fu_raydium_tp_hid_device_bl_read(self, rdata, sizeof(rdata), sizeof(rdata), error))
		return FALSE;
	*out_crc = fu_memread_uint32(rdata, G_LITTLE_ENDIAN);
	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_verify_status(FuRaydiumTpHidDevice *self,
				       FuRaydiumTpFirmware *firmware,
				       GError **error)
{
	guint8 rdata[RAYDIUM_CRC_LEN] = {0};
	guint32 pram_lock_val = 0;
	guint32 image_fw_crc;
	guint32 device_fw_crc = 0;
	g_autoptr(FuFirmware) img_pram = NULL;

	if (!fu_raydium_tp_hid_device_set_mem_addr(self,
						   FU_RAYDIUM_TP_FLASH_CTRL_PRAM_LOCK,
						   RAYDIUM_MCU_MEM,
						   error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_get_mem_read(self, rdata, sizeof(rdata), error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_set_mem_addr(self,
						   FU_RAYDIUM_TP_FLASH_CTRL_PRAM_LOCK,
						   RAYDIUM_MCU_MEM,
						   error))
		return FALSE;

	pram_lock_val = fu_memread_uint32(rdata, G_LITTLE_ENDIAN);
	FU_BIT_CLEAR(pram_lock_val, 2);

	if (!fu_raydium_tp_hid_device_set_mem_addr(self,
						   FU_RAYDIUM_TP_FLASH_FIRM_CRC_ADDR,
						   RAYDIUM_MCU_MEM,
						   error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_get_mem_read(self, rdata, sizeof(rdata), error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_set_mem_addr(self,
						   FU_RAYDIUM_TP_FLASH_CTRL_PRAM_LOCK,
						   RAYDIUM_MCU_MEM,
						   error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_set_mem_write(self, pram_lock_val, error))
		return FALSE;

	img_pram =
	    fu_firmware_get_image_by_id(FU_FIRMWARE(firmware), FU_FIRMWARE_ID_PAYLOAD, error);
	if (img_pram == NULL)
		return FALSE;
	image_fw_crc = fu_raydium_tp_image_get_checksum(FU_RAYDIUM_TP_IMAGE(img_pram));
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

	/* success */
	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_write_images(FuRaydiumTpHidDevice *self,
				      FuRaydiumTpFirmware *firmware,
				      FuProgress *progress,
				      GError **error)
{
	FuRaydiumTpProtect status = 0;
	guint32 flash_fw_crc = 0;
	guint32 flash_desc_crc = 0;
	g_autoptr(FuFirmware) img_pram = NULL;
	g_autoptr(FuFirmware) img_desc = NULL;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 5, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 10, "desc");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 80, "fwimage");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, NULL);

	if (!fu_raydium_tp_hid_device_bl_dis_wdt_and_unlock_flash(self, error))
		return FALSE;
	if (!fu_raydium_tp_hid_device_read_flash_protect_status(self, &status, error))
		return FALSE;
	if (status != FU_RAYDIUM_TP_PROTECT_FW_UNLOCK) {
		if (!fu_raydium_tp_hid_device_write_flash_protect_status(
			self,
			FU_RAYDIUM_TP_PROTECT_FW_UNLOCK,
			error)) {
			g_prefix_error_literal(error, "failed to unlock flash protect: ");
			return FALSE;
		}
		if (!fu_raydium_tp_hid_device_read_flash_protect_status(self, &status, error))
			return FALSE;
		if (status != FU_RAYDIUM_TP_PROTECT_FW_UNLOCK) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "not fw-unlock: %s",
				    fu_raydium_tp_protect_to_string(status));
			return FALSE;
		}
	}

	/* is the update needed? */
	img_pram =
	    fu_firmware_get_image_by_id(FU_FIRMWARE(firmware), FU_FIRMWARE_ID_PAYLOAD, error);
	if (img_pram == NULL)
		return FALSE;
	img_desc = fu_firmware_get_image_by_id(FU_FIRMWARE(firmware), FU_FIRMWARE_ID_HEADER, error);
	if (img_desc == NULL)
		return FALSE;
	if (!fu_raydium_tp_hid_device_read_flash_crc(self,
						     fu_firmware_get_addr(img_pram),
						     fu_firmware_get_size(img_pram),
						     &flash_fw_crc,
						     error)) {
		g_prefix_error_literal(error, "failed to read firmware CRC: ");
		return FALSE;
	}
	if (!fu_raydium_tp_hid_device_read_flash_crc(self,
						     fu_firmware_get_addr(img_desc),
						     fu_firmware_get_size(img_desc),
						     &flash_desc_crc,
						     error)) {
		g_prefix_error_literal(error, "failed to read descriptor CRC: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	if (flash_desc_crc != fu_raydium_tp_image_get_checksum(FU_RAYDIUM_TP_IMAGE(img_desc))) {
		if (!fu_raydium_tp_hid_device_write_descimage(self,
							      img_desc,
							      fu_progress_get_child(progress),
							      error)) {
			g_prefix_error_literal(error, "failed to update desc: ");
			return FALSE;
		}
	}
	fu_progress_step_done(progress);
	if (flash_fw_crc != fu_raydium_tp_image_get_checksum(FU_RAYDIUM_TP_IMAGE(img_pram))) {
		if (!fu_raydium_tp_hid_device_write_fwimage(self,
							    img_pram,
							    fu_progress_get_child(progress),
							    error)) {
			g_prefix_error_literal(error, "failed to update firmware: ");
			return FALSE;
		}
	}
	fu_progress_step_done(progress);

	if (!fu_raydium_tp_hid_device_read_flash_protect_status(self, &status, error))
		return FALSE;
	if (status != FU_RAYDIUM_TP_PROTECT_ALL_LOCK) {
		if (!fu_raydium_tp_hid_device_write_flash_protect_status(
			self,
			FU_RAYDIUM_TP_PROTECT_ALL_LOCK,
			error)) {
			g_prefix_error_literal(error, "failed to lock flash protect: ");
			return FALSE;
		}
		if (!fu_raydium_tp_hid_device_read_flash_protect_status(self, &status, error))
			return FALSE;
		if (status != FU_RAYDIUM_TP_PROTECT_ALL_LOCK) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "not all-lock: %s",
				    fu_raydium_tp_protect_to_string(status));
			return FALSE;
		}
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_check_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuFirmwareParseFlags flags,
					GError **error)
{
	guint16 vid;
	guint16 pid;

	vid = fu_raydium_tp_firmware_get_vendor_id(FU_RAYDIUM_TP_FIRMWARE(firmware));
	if (vid != RAYDIUM_VENDOR_ID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "unexpected vendor id (expected 0x%04x)",
			    (guint)RAYDIUM_VENDOR_ID);
		return FALSE;
	}
	pid = fu_raydium_tp_firmware_get_product_id(FU_RAYDIUM_TP_FIRMWARE(firmware));
	if (pid != fu_device_get_pid(device)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "product id mismatch (got 0x%04x)",
			    pid);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuRaydiumTpHidDevice *self = FU_RAYDIUM_TP_HID_DEVICE(device);

	/* for refactoring */
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_STRICT_EMULATION_ORDER);

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 3, "prepare");
	fu_progress_add_step(progress, FWUPD_STATUS_DOWNLOADING, 90, "download");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "reload");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 2, "verify");

	if (!fu_raydium_tp_hid_device_update_prepare(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_raydium_tp_hid_device_write_images(self,
						   FU_RAYDIUM_TP_FIRMWARE(firmware),
						   fu_progress_get_child(progress),
						   error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_raydium_tp_hid_device_bl_software_reset(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_raydium_tp_hid_device_verify_status(self, FU_RAYDIUM_TP_FIRMWARE(firmware), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
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
	if (!FU_DEVICE_CLASS(fu_raydium_tp_hid_device_parent_class)->probe(device, error))
		return FALSE;
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "hidraw") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "incorrect subsystem=%s, expected hidraw",
			    fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_raydium_tp_hid_device_setup(FuDevice *device, GError **error)
{
	FuRaydiumTpHidDevice *self = FU_RAYDIUM_TP_HID_DEVICE(device);

	if (!FU_DEVICE_CLASS(fu_raydium_tp_hid_device_parent_class)->setup(device, error))
		return FALSE;

	if (!fu_raydium_tp_hid_device_ensure_version(self, error)) {
		g_prefix_error_literal(error, "read firmware information failed: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gchar *
fu_raydium_tp_hid_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static void
fu_raydium_tp_hid_device_init(FuRaydiumTpHidDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_INPUT_TOUCHPAD);
	fu_device_add_protocol(FU_DEVICE(self), "com.raydium.raydiumtp");
	fu_device_set_name(FU_DEVICE(self), "Touch Controller Sensor");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_RAYDIUM_TP_FIRMWARE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_NONBLOCK);
	fu_device_retry_add_recovery(FU_DEVICE(self), FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, NULL);
}

static void
fu_raydium_tp_hid_device_class_init(FuRaydiumTpHidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_raydium_tp_hid_device_probe;
	device_class->setup = fu_raydium_tp_hid_device_setup;
	device_class->reload = fu_raydium_tp_hid_device_setup;
	device_class->check_firmware = fu_raydium_tp_hid_device_check_firmware;
	device_class->write_firmware = fu_raydium_tp_hid_device_write_firmware;
	device_class->set_progress = fu_raydium_tp_hid_device_set_progress;
	device_class->convert_version = fu_raydium_tp_hid_device_convert_version;
}
