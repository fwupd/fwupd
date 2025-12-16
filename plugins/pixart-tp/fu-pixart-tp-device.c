/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-pixart-tp-device.h"
#include "fu-pixart-tp-firmware.h"
#include "fu-pixart-tp-haptic-device.h"
#include "fu-pixart-tp-section.h"

struct _FuPixartTpDevice {
	FuHidrawDevice parent_instance;
	guint8 sram_select;
	guint8 ver_bank;
	guint16 ver_addr;
	gboolean has_tf_child;
};

G_DEFINE_TYPE(FuPixartTpDevice, fu_pixart_tp_device, FU_TYPE_HIDRAW_DEVICE)

#define FU_PIXART_TP_DEVICE_SECTOR_SIZE 4096
#define FU_PIXART_TP_DEVICE_PAGE_SIZE	256

static void
fu_pixart_tp_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuPixartTpDevice *self = FU_PIXART_TP_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "SramSelect", self->sram_select);
	fwupd_codec_string_append_hex(str, idt, "VerBank", self->ver_bank);
	fwupd_codec_string_append_hex(str, idt, "VerAddr", self->ver_addr);
	fwupd_codec_string_append_bool(str, idt, "HasTfChild", self->has_tf_child);
}

#define REPORT_ID_SINGLE 0x42
#define REPORT_ID_BURST	 0x41
#define REPORT_ID_USER	 0x43

#define OP_READ 0x10

static gboolean
fu_pixart_tp_device_register_write(FuPixartTpDevice *self,
				   FuPixartTpSystemBank bank,
				   guint8 addr,
				   guint8 val,
				   GError **error)
{
	guint8 buf[4] = {REPORT_ID_SINGLE, addr, bank, val};

	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  buf,
					  sizeof(buf),
					  FU_IOCTL_FLAG_NONE,
					  error)) {
		g_prefix_error(error,
			       "register write failed: bank=0x%02x addr=0x%02x val=0x%02x: ",
			       bank,
			       addr,
			       val);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_device_register_read(FuPixartTpDevice *self,
				  FuPixartTpSystemBank bank,
				  guint8 addr,
				  guint8 *out_val,
				  GError **error)
{
	guint8 cmd[4] = {REPORT_ID_SINGLE, addr, (guint8)(bank | OP_READ), 0x00};
	guint8 resp[4] = {REPORT_ID_SINGLE};

	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  cmd,
					  sizeof(cmd),
					  FU_IOCTL_FLAG_NONE,
					  error)) {
		g_prefix_error(error,
			       "register read command failed: bank=0x%02x addr=0x%02x: ",
			       bank,
			       addr);
		return FALSE;
	}

	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					  resp,
					  sizeof(resp),
					  FU_IOCTL_FLAG_NONE,
					  error)) {
		g_prefix_error(error,
			       "register read response failed: bank=0x%02x addr=0x%02x: ",
			       bank,
			       addr);
		return FALSE;
	}

	/* success */
	*out_val = resp[3];
	return TRUE;
}

gboolean
fu_pixart_tp_device_register_user_write(FuPixartTpDevice *self,
					FuPixartTpUserBank bank,
					guint8 addr,
					guint8 val,
					GError **error)
{
	guint8 buf[4] = {REPORT_ID_USER, addr, bank, val};

	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  buf,
					  sizeof(buf),
					  FU_IOCTL_FLAG_NONE,
					  error)) {
		g_prefix_error(error,
			       "user register write failed: bank=0x%02x addr=0x%02x val=0x%02x: ",
			       bank,
			       addr,
			       val);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_device_register_user_read(FuPixartTpDevice *self,
				       FuPixartTpUserBank bank,
				       guint8 addr,
				       guint8 *out_val,
				       GError **error)
{
	guint8 cmd[4] = {REPORT_ID_USER, addr, (guint8)(bank | OP_READ), 0x00};
	guint8 resp[4] = {REPORT_ID_USER};

	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  cmd,
					  sizeof(cmd),
					  FU_IOCTL_FLAG_NONE,
					  error)) {
		g_prefix_error(error,
			       "user register read command failed: bank=0x%02x addr=0x%02x: ",
			       bank,
			       addr);
		return FALSE;
	}
	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					  resp,
					  sizeof(resp),
					  FU_IOCTL_FLAG_NONE,
					  error)) {
		g_prefix_error(error,
			       "user register read response failed: bank=0x%02x addr=0x%02x: ",
			       bank,
			       addr);
		return FALSE;
	}

	/* success */
	*out_val = resp[3];
	return TRUE;
}

static gboolean
fu_pixart_tp_device_register_burst_write(FuPixartTpDevice *self,
					 const guint8 *buf,
					 gsize bufsz,
					 GError **error)
{
	guint8 payload[257] = {REPORT_ID_BURST};

	if (!fu_memcpy_safe(payload,
			    sizeof(payload),
			    1, /* dst offset: payload[1..] */
			    buf,
			    bufsz,
			    0, /* src offset: buf[0..] */
			    bufsz,
			    error)) {
		g_prefix_error_literal(error, "burst write memcpy failed: ");
		return FALSE;
	}
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  payload,
					  sizeof(payload),
					  FU_IOCTL_FLAG_NONE,
					  error)) {
		g_prefix_error_literal(error, "burst write feature report failed: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_device_reset(FuPixartTpDevice *self, FuPixartTpResetMode mode, GError **error)
{
	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK1,
						FU_PIXART_TP_REG_SYS1_RESET_KEY1,
						FU_PIXART_TP_RESET_KEY1_SUSPEND,
						error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), 30);

	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK1,
						FU_PIXART_TP_REG_SYS1_RESET_KEY2,
						mode == FU_PIXART_TP_RESET_MODE_APPLICATION
						    ? FU_PIXART_TP_RESET_KEY2_REGULAR
						    : FU_PIXART_TP_RESET_KEY2_BOOTLOADER,
						error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), mode == FU_PIXART_TP_RESET_MODE_APPLICATION ? 500 : 10);

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_device_flash_execute_wait_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuPixartTpDevice *self = FU_PIXART_TP_DEVICE(device);
	guint8 *out_val = user_data;

	if (!fu_pixart_tp_device_register_read(self,
					       FU_PIXART_TP_SYSTEM_BANK_BANK4,
					       FU_PIXART_TP_REG_SYS4_FLASH_EXECUTE,
					       out_val,
					       error))
		return FALSE;

	/* not ready yet, ask fu_device_retry_full() to try again */
	if (*out_val != FU_PIXART_TP_FLASH_EXEC_STATE_SUCCESS) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "flash execute still in progress");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_device_flash_execute(FuPixartTpDevice *self,
				  guint8 inst_cmd,
				  guint32 ccr_cmd,
				  guint16 data_cnt,
				  GError **error)
{
	guint8 out_val = 0;

	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK4,
						FU_PIXART_TP_REG_SYS4_FLASH_INST_CMD,
						inst_cmd,
						error))
		return FALSE;
	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK4,
						FU_PIXART_TP_REG_SYS4_FLASH_CCR0,
						(guint8)((ccr_cmd >> 0) & 0xff),
						error))
		return FALSE;
	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK4,
						FU_PIXART_TP_REG_SYS4_FLASH_CCR1,
						(guint8)((ccr_cmd >> 8) & 0xff),
						error))
		return FALSE;
	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK4,
						FU_PIXART_TP_REG_SYS4_FLASH_CCR2,
						(guint8)((ccr_cmd >> 16) & 0xff),
						error))
		return FALSE;
	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK4,
						FU_PIXART_TP_REG_SYS4_FLASH_CCR3,
						(guint8)((ccr_cmd >> 24) & 0xff),
						error))
		return FALSE;

	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK4,
						FU_PIXART_TP_REG_SYS4_FLASH_DATA_CNT0,
						(guint8)((data_cnt >> 0) & 0xff),
						error))
		return FALSE;
	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK4,
						FU_PIXART_TP_REG_SYS4_FLASH_DATA_CNT1,
						(guint8)((data_cnt >> 8) & 0xff),
						error))
		return FALSE;

	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK4,
						FU_PIXART_TP_REG_SYS4_FLASH_EXECUTE,
						0x01, /* flash_execute_start */
						error))
		return FALSE;

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_pixart_tp_device_flash_execute_wait_cb,
				  10,
				  1, /* ms */
				  &out_val,
				  error)) {
		g_prefix_error_literal(error, "flash executes failure: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_device_flash_write_enable_wait_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuPixartTpDevice *self = FU_PIXART_TP_DEVICE(device);
	guint8 *out_val = user_data;

	/* send READ_STATUS command */
	if (!fu_pixart_tp_device_flash_execute(self,
					       FU_PIXART_TP_FLASH_INST_RD2_REG_BANK,
					       FU_PIXART_TP_FLASH_CCR_READ_STATUS,
					       1,
					       error))
		return FALSE;

	/* small delay between command and status read */
	fu_device_sleep(device, 1);

	/* read FLASH_STATUS register */
	if (!fu_pixart_tp_device_register_read(self,
					       FU_PIXART_TP_SYSTEM_BANK_BANK4,
					       FU_PIXART_TP_REG_SYS4_FLASH_STATUS,
					       out_val,
					       error))
		return FALSE;

	/* check WEL bit */
	if ((*out_val & FU_PIXART_TP_FLASH_WRITE_ENABLE_SUCCESS) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "flash write enable still not set");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_device_flash_write_enable(FuPixartTpDevice *self, GError **error)
{
	guint8 out_val = 0;

	/* send WRITE_ENABLE once */
	if (!fu_pixart_tp_device_flash_execute(self,
					       FU_PIXART_TP_FLASH_INST_NONE,
					       FU_PIXART_TP_FLASH_CCR_WRITE_ENABLE,
					       0,
					       error))
		return FALSE;

	/* poll WEL bit using fu_device_retry_full() */
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_pixart_tp_device_flash_write_enable_wait_cb,
				  10,
				  0, /* ms */
				  &out_val,
				  error)) {
		g_prefix_error_literal(error, "flash write enable failure: ");
		g_debug("flash write enable failure");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_device_flash_wait_busy_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuPixartTpDevice *self = FU_PIXART_TP_DEVICE(device);
	guint8 *out_val = user_data;

	/* send READ_STATUS command */
	if (!fu_pixart_tp_device_flash_execute(self,
					       FU_PIXART_TP_FLASH_INST_RD2_REG_BANK,
					       FU_PIXART_TP_FLASH_CCR_READ_STATUS,
					       1,
					       error))
		return FALSE;

	/* small delay before reading status */
	fu_device_sleep(device, 1);

	/* read FLASH_STATUS register */
	if (!fu_pixart_tp_device_register_read(self,
					       FU_PIXART_TP_SYSTEM_BANK_BANK4,
					       FU_PIXART_TP_REG_SYS4_FLASH_STATUS,
					       out_val,
					       error))
		return FALSE;

	/* busy bit cleared? */
	if ((*out_val & FU_PIXART_TP_FLASH_STATUS_BUSY) != 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "flash still busy");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_device_flash_wait_busy(FuPixartTpDevice *self, GError **error)
{
	guint8 out_val = 0;

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_pixart_tp_device_flash_wait_busy_cb,
				  1000,
				  0, /* ms */
				  &out_val,
				  error)) {
		g_prefix_error_literal(error, "flash wait busy failure: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_device_flash_erase_sector(FuPixartTpDevice *self, guint8 sector, GError **error)
{
	guint32 flash_address = (guint32)sector * FU_PIXART_TP_DEVICE_SECTOR_SIZE;

	if (!fu_pixart_tp_device_flash_wait_busy(self, error))
		return FALSE;
	if (!fu_pixart_tp_device_flash_write_enable(self, error))
		return FALSE;
	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK4,
						FU_PIXART_TP_REG_SYS4_FLASH_ADDR0,
						(guint8)((flash_address >> 0) & 0xff),
						error))
		return FALSE;
	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK4,
						FU_PIXART_TP_REG_SYS4_FLASH_ADDR1,
						(guint8)((flash_address >> 8) & 0xff),
						error))
		return FALSE;
	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK4,
						FU_PIXART_TP_REG_SYS4_FLASH_ADDR2,
						(guint8)((flash_address >> 16) & 0xff),
						error))
		return FALSE;
	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK4,
						FU_PIXART_TP_REG_SYS4_FLASH_ADDR3,
						(guint8)((flash_address >> 24) & 0xff),
						error))
		return FALSE;

	g_debug("erase sector %u (addr=0x%08x)", (guint)sector, flash_address);

	if (!fu_pixart_tp_device_flash_execute(self,
					       FU_PIXART_TP_FLASH_INST_NONE,
					       FU_PIXART_TP_FLASH_CCR_ERASE_SECTOR,
					       0,
					       error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_device_flash_program_256b_to_flash(FuPixartTpDevice *self,
						guint8 sector,
						guint8 page,
						GError **error)
{
	guint32 flash_address = (guint32)sector * FU_PIXART_TP_DEVICE_SECTOR_SIZE +
				(guint32)page * FU_PIXART_TP_DEVICE_PAGE_SIZE;

	if (!fu_pixart_tp_device_flash_wait_busy(self, error))
		return FALSE;
	if (!fu_pixart_tp_device_flash_write_enable(self, error))
		return FALSE;
	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK4,
						FU_PIXART_TP_REG_SYS4_FLASH_BUF_ADDR0,
						0x00,
						error))
		return FALSE;
	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK4,
						FU_PIXART_TP_REG_SYS4_FLASH_BUF_ADDR1,
						0x00,
						error))
		return FALSE;

	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK4,
						FU_PIXART_TP_REG_SYS4_FLASH_ADDR0,
						(guint8)((flash_address >> 0) & 0xff),
						error))
		return FALSE;
	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK4,
						FU_PIXART_TP_REG_SYS4_FLASH_ADDR1,
						(guint8)((flash_address >> 8) & 0xff),
						error))
		return FALSE;
	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK4,
						FU_PIXART_TP_REG_SYS4_FLASH_ADDR2,
						(guint8)((flash_address >> 16) & 0xff),
						error))
		return FALSE;
	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK4,
						FU_PIXART_TP_REG_SYS4_FLASH_ADDR3,
						(guint8)((flash_address >> 24) & 0xff),
						error))
		return FALSE;

	if (!fu_pixart_tp_device_flash_execute(self,
					       FU_PIXART_TP_FLASH_INST_PROGRAM |
						   FU_PIXART_TP_FLASH_INST_INTERNAL_SRAM_ACCESS,
					       FU_PIXART_TP_FLASH_CCR_PROGRAM_PAGE,
					       FU_PIXART_TP_DEVICE_PAGE_SIZE,
					       error))
		return FALSE;

	/* success */
	return TRUE;
}

/* SRAM write (256 bytes) */

static gboolean
fu_pixart_tp_device_write_sram_256b(FuPixartTpDevice *self, const guint8 *data, GError **error)
{
	enum {
		/*
		 * SRAM_TRIGGER (bank6)
		 * 0x00: enable NCS move, start transferring data to target SRAM address
		 * 0x01: disable NCS move
		 */
		PIXART_TP_SRAM_TRIGGER_NCS_ENABLE = 0x00,
		PIXART_TP_SRAM_TRIGGER_NCS_DISABLE = 0x01,
	};

	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK6,
						FU_PIXART_TP_REG_SYS6_SRAM_ADDR0,
						0x00,
						error))
		return FALSE;

	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK6,
						FU_PIXART_TP_REG_SYS6_SRAM_ADDR1,
						0x00,
						error))
		return FALSE;

	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK6,
						FU_PIXART_TP_REG_SYS6_SRAM_SELECT,
						self->sram_select,
						error))
		return FALSE;

	/* enable NCS so that the following burst goes to SRAM buffer */
	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK6,
						FU_PIXART_TP_REG_SYS6_SRAM_TRIGGER,
						PIXART_TP_SRAM_TRIGGER_NCS_ENABLE,
						error))
		return FALSE;

	if (!fu_pixart_tp_device_register_burst_write(self,
						      data,
						      FU_PIXART_TP_DEVICE_PAGE_SIZE,
						      error)) {
		g_prefix_error_literal(error, "burst write buffer failure: ");
		return FALSE;
	}

	/* disable NCS and commit SRAM buffer to target address */
	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK6,
						FU_PIXART_TP_REG_SYS6_SRAM_TRIGGER,
						PIXART_TP_SRAM_TRIGGER_NCS_DISABLE,
						error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_device_firmware_clear(FuPixartTpDevice *self,
				   FuPixartTpFirmware *firmware,
				   GError **error)
{
	FuPixartTpSection *section;
	guint32 start_address = 0;

	section = fu_pixart_tp_firmware_find_section_by_type(firmware,
							     FU_PIXART_TP_UPDATE_TYPE_FW_SECTION,
							     error);
	if (section == NULL)
		return FALSE;
	start_address = fu_pixart_tp_section_get_target_flash_start(section);
	g_debug("clear firmware at start address 0x%08x", start_address);
	if (!fu_pixart_tp_device_flash_erase_sector(
		self,
		(guint8)(start_address / FU_PIXART_TP_DEVICE_SECTOR_SIZE),
		error)) {
		g_prefix_error_literal(error, "clear firmware failure: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_device_crc_firmware_wait_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuPixartTpDevice *self = FU_PIXART_TP_DEVICE(device);
	guint8 *out_val = user_data;

	if (!fu_pixart_tp_device_register_user_read(self,
						    FU_PIXART_TP_USER_BANK_BANK0,
						    FU_PIXART_TP_REG_USER0_CRC_CTRL,
						    out_val,
						    error))
		return FALSE;

	/* busy bit cleared? */
	if ((*out_val & FU_PIXART_TP_CRC_CTRL_BUSY) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "firmware CRC still busy");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_device_crc_firmware(FuPixartTpDevice *self, guint32 *crc, GError **error)
{
	guint8 out_val = 0;
	guint8 swap_flag = 0;
	guint16 part_id = 0;
	guint32 return_value = 0;

	g_return_val_if_fail(crc != NULL, FALSE);
	*crc = 0;

	/* read swap_flag from system bank4 */
	if (!fu_pixart_tp_device_register_read(self,
					       FU_PIXART_TP_SYSTEM_BANK_BANK4,
					       FU_PIXART_TP_REG_SYS4_SWAP_FLAG,
					       &out_val,
					       error))
		return FALSE;
	swap_flag = out_val;

	/* read part_id from user bank0 (little-endian) */
	if (!fu_pixart_tp_device_register_user_read(self,
						    FU_PIXART_TP_USER_BANK_BANK0,
						    FU_PIXART_TP_REG_USER0_PART_ID0,
						    &out_val,
						    error))
		return FALSE;
	part_id = out_val;

	if (!fu_pixart_tp_device_register_user_read(self,
						    FU_PIXART_TP_USER_BANK_BANK0,
						    FU_PIXART_TP_REG_USER0_PART_ID1,
						    &out_val,
						    error))
		return FALSE;
	part_id |= (guint16)out_val << 8;

	switch (part_id) {
	case FU_PIXART_TP_PART_ID_PJP274:
		if (swap_flag != 0) {
			/* PJP274 + swap enabled → firmware on bank1 */
			if (!fu_pixart_tp_device_register_user_write(
				self,
				FU_PIXART_TP_USER_BANK_BANK0,
				FU_PIXART_TP_REG_USER0_CRC_CTRL,
				FU_PIXART_TP_CRC_CTRL_FW_BANK1,
				error))
				return FALSE;
		} else {
			/* PJP274 normal boot → firmware on bank0 */
			if (!fu_pixart_tp_device_register_user_write(
				self,
				FU_PIXART_TP_USER_BANK_BANK0,
				FU_PIXART_TP_REG_USER0_CRC_CTRL,
				FU_PIXART_TP_CRC_CTRL_FW_BANK0,
				error))
				return FALSE;
		}
		break;

	default:
		/* other part_id: always use bank0 firmware CRC */
		if (!fu_pixart_tp_device_register_user_write(self,
							     FU_PIXART_TP_USER_BANK_BANK0,
							     FU_PIXART_TP_REG_USER0_CRC_CTRL,
							     FU_PIXART_TP_CRC_CTRL_FW_BANK0,
							     error))
			return FALSE;
		break;
	}

	/* wait CRC calculation completed */
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_pixart_tp_device_crc_firmware_wait_cb,
				  1000,
				  10, /* ms */
				  &out_val,
				  error)) {
		g_prefix_error_literal(error, "firmware CRC wait busy failure: ");
		return FALSE;
	}

	/* read CRC result (32-bit, little-endian) */
	if (!fu_pixart_tp_device_register_user_read(self,
						    FU_PIXART_TP_USER_BANK_BANK0,
						    FU_PIXART_TP_REG_USER0_CRC_RESULT0,
						    &out_val,
						    error))
		return FALSE;
	return_value |= (guint32)out_val;

	if (!fu_pixart_tp_device_register_user_read(self,
						    FU_PIXART_TP_USER_BANK_BANK0,
						    FU_PIXART_TP_REG_USER0_CRC_RESULT1,
						    &out_val,
						    error))
		return FALSE;
	return_value |= (guint32)out_val << 8;

	if (!fu_pixart_tp_device_register_user_read(self,
						    FU_PIXART_TP_USER_BANK_BANK0,
						    FU_PIXART_TP_REG_USER0_CRC_RESULT2,
						    &out_val,
						    error))
		return FALSE;
	return_value |= (guint32)out_val << 16;

	if (!fu_pixart_tp_device_register_user_read(self,
						    FU_PIXART_TP_USER_BANK_BANK0,
						    FU_PIXART_TP_REG_USER0_CRC_RESULT3,
						    &out_val,
						    error))
		return FALSE;
	return_value |= (guint32)out_val << 24;

	*crc = return_value;
	g_debug("firmware CRC: 0x%08x", (guint)*crc);

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_device_crc_parameter_wait_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuPixartTpDevice *self = FU_PIXART_TP_DEVICE(device);
	guint8 *out_val = user_data;

	if (!fu_pixart_tp_device_register_user_read(self,
						    FU_PIXART_TP_USER_BANK_BANK0,
						    FU_PIXART_TP_REG_USER0_CRC_CTRL,
						    out_val,
						    error))
		return FALSE;

	/* busy bit cleared? */
	if ((*out_val & FU_PIXART_TP_CRC_CTRL_BUSY) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "parameter CRC still busy");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_device_crc_parameter(FuPixartTpDevice *self, guint32 *crc, GError **error)
{
	guint16 part_id = 0;
	guint32 result = 0;
	guint8 out_val = 0;
	guint8 swap_flag;

	g_return_val_if_fail(crc != NULL, FALSE);
	*crc = 0;

	/* read swap_flag from system bank4 */
	if (!fu_pixart_tp_device_register_read(self,
					       FU_PIXART_TP_SYSTEM_BANK_BANK4,
					       FU_PIXART_TP_REG_SYS4_SWAP_FLAG,
					       &out_val,
					       error))
		return FALSE;
	swap_flag = out_val;

	/* read part_id from user bank0 (little-endian) */
	if (!fu_pixart_tp_device_register_user_read(self,
						    FU_PIXART_TP_USER_BANK_BANK0,
						    FU_PIXART_TP_REG_USER0_PART_ID0,
						    &out_val,
						    error))
		return FALSE;
	part_id = out_val;

	if (!fu_pixart_tp_device_register_user_read(self,
						    FU_PIXART_TP_USER_BANK_BANK0,
						    FU_PIXART_TP_REG_USER0_PART_ID1,
						    &out_val,
						    error))
		return FALSE;
	part_id |= (guint16)out_val << 8;

	/* select CRC source */
	switch (part_id) {
	case FU_PIXART_TP_PART_ID_PJP274:
		if (swap_flag != 0) {
			if (!fu_pixart_tp_device_register_user_write(
				self,
				FU_PIXART_TP_USER_BANK_BANK0,
				FU_PIXART_TP_REG_USER0_CRC_CTRL,
				FU_PIXART_TP_CRC_CTRL_PARAM_BANK1,
				error))
				return FALSE;
		} else {
			if (!fu_pixart_tp_device_register_user_write(
				self,
				FU_PIXART_TP_USER_BANK_BANK0,
				FU_PIXART_TP_REG_USER0_CRC_CTRL,
				FU_PIXART_TP_CRC_CTRL_PARAM_BANK0,
				error))
				return FALSE;
		}
		break;

	default:
		if (!fu_pixart_tp_device_register_user_write(self,
							     FU_PIXART_TP_USER_BANK_BANK0,
							     FU_PIXART_TP_REG_USER0_CRC_CTRL,
							     FU_PIXART_TP_CRC_CTRL_PARAM_BANK0,
							     error))
			return FALSE;
		break;
	}

	/* wait CRC calculation completed */
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_pixart_tp_device_crc_parameter_wait_cb,
				  1000,
				  10, /* ms */
				  &out_val,
				  error)) {
		g_prefix_error_literal(error, "parameter CRC wait busy failure: ");
		return FALSE;
	}

	/* read CRC result (32-bit LE) */
	if (!fu_pixart_tp_device_register_user_read(self,
						    FU_PIXART_TP_USER_BANK_BANK0,
						    FU_PIXART_TP_REG_USER0_CRC_RESULT0,
						    &out_val,
						    error))
		return FALSE;
	result |= (guint32)out_val;

	if (!fu_pixart_tp_device_register_user_read(self,
						    FU_PIXART_TP_USER_BANK_BANK0,
						    FU_PIXART_TP_REG_USER0_CRC_RESULT1,
						    &out_val,
						    error))
		return FALSE;
	result |= (guint32)out_val << 8;

	if (!fu_pixart_tp_device_register_user_read(self,
						    FU_PIXART_TP_USER_BANK_BANK0,
						    FU_PIXART_TP_REG_USER0_CRC_RESULT2,
						    &out_val,
						    error))
		return FALSE;
	result |= (guint32)out_val << 16;

	if (!fu_pixart_tp_device_register_user_read(self,
						    FU_PIXART_TP_USER_BANK_BANK0,
						    FU_PIXART_TP_REG_USER0_CRC_RESULT3,
						    &out_val,
						    error))
		return FALSE;
	result |= (guint32)out_val << 24;

	*crc = result;
	g_debug("parameter CRC: 0x%08x", result);

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_device_write_page(FuPixartTpDevice *self,
			       guint8 sector,
			       guint8 page,
			       FuChunk *chk,
			       GError **error)
{
	g_autoptr(GByteArray) page_buf = g_byte_array_new();
	g_autoptr(GBytes) blob = fu_chunk_get_bytes(chk);

	/* initialize all extra bytes to 0xFF */
	fu_byte_array_append_bytes(page_buf, blob);
	fu_byte_array_set_size(page_buf, FU_PIXART_TP_DEVICE_PAGE_SIZE, 0xFF);

	/* write to SRAM using the 256-byte buffer */
	if (!fu_pixart_tp_device_write_sram_256b(self, page_buf->data, error))
		return FALSE;
	if (!fu_pixart_tp_device_flash_program_256b_to_flash(self, sector, page, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_device_write_sector(FuPixartTpDevice *self,
				 guint8 start_sector,
				 FuChunk *chk_sector,
				 GError **error)
{
	g_autoptr(FuChunk) chk0 = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GBytes) blob = fu_chunk_get_bytes(chk_sector);

	/* pages 1..15 */
	chunks = fu_chunk_array_new_from_bytes(blob, 0x0, 0x0, FU_PIXART_TP_DEVICE_PAGE_SIZE);
	for (guint8 i = 1; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_pixart_tp_device_write_page(
			self,
			(guint8)(start_sector + fu_chunk_get_idx(chk_sector)),
			i,
			chk,
			error))
			return FALSE;
	}

	/* page 0 last */
	chk0 = fu_chunk_array_index(chunks, 0, error);
	if (chk0 == NULL)
		return FALSE;
	if (!fu_pixart_tp_device_write_page(self,
					    start_sector + fu_chunk_get_idx(chk_sector),
					    0,
					    chk0,
					    error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_device_write_section(FuPixartTpDevice *self,
				  FuPixartTpSection *section,
				  FuProgress *progress,
				  GError **error)
{
	FuPixartTpUpdateType update_type = FU_PIXART_TP_UPDATE_TYPE_GENERAL;
	guint32 target_flash_start = fu_pixart_tp_section_get_target_flash_start(section);
	guint8 start_sector;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* nothing to do */
	if (fu_firmware_get_size(FU_FIRMWARE(section)) == 0)
		return TRUE;

	/* TF_FORCE is now handled by the haptic child-device */
	update_type = fu_pixart_tp_section_get_update_type(section);
	if (update_type != FU_PIXART_TP_UPDATE_TYPE_GENERAL &&
	    update_type != FU_PIXART_TP_UPDATE_TYPE_FW_SECTION &&
	    update_type != FU_PIXART_TP_UPDATE_TYPE_PARAM) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "unsupported update type %u for TP section",
			    (guint)update_type);
		return FALSE;
	}

	/* chunk section data into sectors */
	stream = fu_firmware_get_stream(FU_FIRMWARE(section), error);
	if (stream == NULL)
		return FALSE;
	chunks = fu_chunk_array_new_from_stream(stream,
						0x0,
						0x0,
						FU_PIXART_TP_DEVICE_SECTOR_SIZE,
						error);
	if (chunks == NULL)
		return FALSE;

	/* nothing to do */
	if (fu_chunk_array_length(chunks) == 0)
		return TRUE;

	/* progress: 2 steps per sector (erase + program) */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks) * 2);

	/* cpu clocks power up */
	if (!fu_pixart_tp_device_register_write(self,
						FU_PIXART_TP_SYSTEM_BANK_BANK1,
						FU_PIXART_TP_REG_SYS1_CLOCKS_POWER_UP,
						FU_PIXART_TP_CLOCKS_POWER_UP_CPU,
						error))
		return FALSE;

	/* erase phase */
	start_sector = (guint8)(target_flash_start / FU_PIXART_TP_DEVICE_SECTOR_SIZE);
	for (guint8 i = 0; i < fu_chunk_array_length(chunks); i++) {
		if (!fu_pixart_tp_device_flash_erase_sector(self,
							    (guint8)(start_sector + i),
							    error)) {
			g_prefix_error(error, "failed to erase sector 0x%x: ", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* Keep the original order: write pages 1..15 first, then page 0.
	 * Each write is 256 bytes; last chunk in blob is padded with 0xFF. */
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_pixart_tp_device_write_sector(self, start_sector, chk, error)) {
			g_prefix_error(error, "failed to write sector 0x%x: ", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_device_write_sections(FuPixartTpDevice *self,
				   GPtrArray *sections,
				   FuProgress *progress,
				   GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, sections->len);

	for (guint i = 0; i < sections->len; i++) {
		FuPixartTpSection *section = g_ptr_array_index(sections, i);
		FuPixartTpUpdateType update_type = fu_pixart_tp_section_get_update_type(section);

		/* skip non-updatable sections */
		if (!fu_pixart_tp_section_has_flag(section, FU_PIXART_TP_FIRMWARE_FLAG_VALID) ||
		    fu_pixart_tp_section_has_flag(section,
						  FU_PIXART_TP_FIRMWARE_FLAG_IS_EXTERNAL)) {
			fu_progress_step_done(progress);
			continue;
		}

		/* skip TF_FORCE sections:
		 *   - handled by TF/haptic child device using its own image
		 *   - parent TP only handles TP firmware/parameter sections */
		if (update_type == FU_PIXART_TP_UPDATE_TYPE_TF_FORCE) {
			g_debug("skip TF_FORCE section %u for TP parent device", i);
			fu_progress_step_done(progress);
			continue;
		}

		if (fu_firmware_get_size(FU_FIRMWARE(section)) == 0) {
			fu_progress_step_done(progress);
			continue;
		}
		if (!fu_pixart_tp_device_write_section(self,
						       section,
						       fu_progress_get_child(progress),
						       error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_device_verify_crc(FuPixartTpDevice *self,
			       FuPixartTpFirmware *firmware,
			       FuProgress *progress,
			       GError **error)
{
	guint32 crc_value;
	FuPixartTpSection *section;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 92, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 8, NULL);

	/* reset to bootloader before CRC check */
	if (!fu_pixart_tp_device_reset(self, FU_PIXART_TP_RESET_MODE_BOOTLOADER, error))
		return FALSE;

	/* firmware CRC */
	if (!fu_pixart_tp_device_crc_firmware(self, &crc_value, error))
		return FALSE;
	section = fu_pixart_tp_firmware_find_section_by_type(firmware,
							     FU_PIXART_TP_UPDATE_TYPE_FW_SECTION,
							     error);
	if (section == NULL)
		return FALSE;
	if (crc_value != fu_pixart_tp_section_get_crc(section)) {
		g_autoptr(GError) error_local = NULL;
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware CRC compare failed");
		if (!fu_pixart_tp_device_firmware_clear(self, firmware, &error_local)) {
			g_warning("failed to clear firmware after CRC error: %s",
				  error_local->message);
		}
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* parameter CRC */
	if (!fu_pixart_tp_device_crc_parameter(self, &crc_value, error))
		return FALSE;
	section = fu_pixart_tp_firmware_find_section_by_type(firmware,
							     FU_PIXART_TP_UPDATE_TYPE_PARAM,
							     error);
	if (section == NULL)
		return FALSE;
	if (crc_value != fu_pixart_tp_section_get_crc(section)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "parameter CRC compare failed");
		(void)fu_pixart_tp_device_firmware_clear(self, firmware, error);
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_device_setup(FuDevice *device, GError **error)
{
	FuPixartTpDevice *self = FU_PIXART_TP_DEVICE(device);
	guint8 buf[2] = {0};

	/* read TP boot status*/
	if (!fu_pixart_tp_device_register_user_read(self,
						    FU_PIXART_TP_USER_BANK_BANK0,
						    FU_PIXART_TP_REG_USER0_BOOT_STAUS,
						    &buf[0],
						    error))
		return FALSE;

	/* avoid tp stuck in bootloader */
	if (buf[0] == FU_PIXART_TP_BOOT_STATUS_ROM) {
		if (!fu_pixart_tp_device_reset(self, FU_PIXART_TP_RESET_MODE_APPLICATION, error))
			return FALSE;
	}

	/* read low byte */
	if (!fu_pixart_tp_device_register_user_read(self,
						    self->ver_bank,
						    (guint8)(self->ver_addr + 0),
						    &buf[0],
						    error))
		return FALSE;

	/* read high byte */
	if (!fu_pixart_tp_device_register_user_read(self,
						    self->ver_bank,
						    (guint8)(self->ver_addr + 1),
						    &buf[1],
						    error))
		return FALSE;

	/* success */
	fu_device_set_version_raw(device, fu_memread_uint16(buf, G_LITTLE_ENDIAN));
	return TRUE;
}

static gboolean
fu_pixart_tp_device_write_firmware(FuDevice *device,
				   FuFirmware *firmware,
				   FuProgress *progress,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuPixartTpDevice *self = FU_PIXART_TP_DEVICE(device);
	guint64 total_update_bytes = 0;
	g_autoptr(GPtrArray) sections = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 2, NULL);

	/* sanity check */
	sections = fu_firmware_get_images(firmware);
	if (sections->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no sections to write");
		return FALSE;
	}

	/* calculate total bytes for valid internal TP sections
	 * NOTE:
	 *   - TF_FORCE sections are skipped here; they are handled by the
	 *     TF/haptic child-device using its own firmware image.
	 */
	for (guint i = 0; i < sections->len; i++) {
		FuPixartTpSection *section = g_ptr_array_index(sections, i);
		guint32 section_length = 0;
		FuPixartTpUpdateType update_type;

		if (!fu_pixart_tp_section_has_flag(section, FU_PIXART_TP_FIRMWARE_FLAG_VALID) ||
		    fu_pixart_tp_section_has_flag(section, FU_PIXART_TP_FIRMWARE_FLAG_IS_EXTERNAL))
			continue;

		update_type = fu_pixart_tp_section_get_update_type(section);
		if (update_type == FU_PIXART_TP_UPDATE_TYPE_TF_FORCE)
			continue;

		section_length = fu_firmware_get_size(FU_FIRMWARE(section));
		if (section_length > 0)
			total_update_bytes += (guint64)section_length;
	}

	/* sanity check */
	if (total_update_bytes == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no internal/valid TP sections to write");
		return FALSE;
	}
	g_debug("total TP update bytes=%" G_GUINT64_FORMAT, total_update_bytes);

	/* erase old firmware */
	if (!fu_pixart_tp_device_firmware_clear(self, FU_PIXART_TP_FIRMWARE(firmware), error))
		return FALSE;

	/* program all TP sections (TF_FORCE handled by child device) */
	if (!fu_pixart_tp_device_write_sections(self,
						sections,
						fu_progress_get_child(progress),
						error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify CRC (firmware + parameter) */
	if (!fu_pixart_tp_device_verify_crc(self,
					    FU_PIXART_TP_FIRMWARE(firmware),
					    fu_progress_get_child(progress),
					    error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_device_set_quirk_kv(FuDevice *device,
				 const gchar *key,
				 const gchar *value,
				 GError **error)
{
	FuPixartTpDevice *self = FU_PIXART_TP_DEVICE(device);
	guint64 tmp = 0;

	if (g_strcmp0(key, "PixartTpHidVersionBank") == 0) {
		if (!fu_strtoull(value, &tmp, 0, 0xff, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->ver_bank = (guint8)tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "PixartTpHidVersionAddr") == 0) {
		if (!fu_strtoull(value, &tmp, 0, 0xffff, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->ver_addr = (guint16)tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "PixartTpSramSelect") == 0) {
		if (!fu_strtoull(value, &tmp, 0, 0xff, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->sram_select = (guint8)tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "PixartTpHasHaptic") == 0)
		return fu_strtobool(value, &self->has_tf_child, error);

	/* unknown quirk */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "quirk key not supported: %s",
		    key);
	return FALSE;
}

static gboolean
fu_pixart_tp_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuPixartTpDevice *self = FU_PIXART_TP_DEVICE(device);

	/* nothing to do if already in application mode */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	if (!fu_pixart_tp_device_reset(self, FU_PIXART_TP_RESET_MODE_APPLICATION, error))
		return FALSE;

	/* success */
	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static gboolean
fu_pixart_tp_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuPixartTpDevice *self = FU_PIXART_TP_DEVICE(device);

	/* already in bootloader, nothing to do */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	if (!fu_pixart_tp_device_reset(self, FU_PIXART_TP_RESET_MODE_BOOTLOADER, error))
		return FALSE;

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static gboolean
fu_pixart_tp_device_reload(FuDevice *device, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	/* best-effort: do not fail the whole update just because reload failed */
	if (!fu_pixart_tp_device_setup(device, &error_local))
		g_debug("failed to refresh firmware version: %s", error_local->message);

	/* success */
	return TRUE;
}

static gboolean
fu_pixart_tp_device_cleanup(FuDevice *device,
			    FuProgress *progress,
			    FwupdInstallFlags flags,
			    GError **error)
{
	FuPixartTpDevice *self = FU_PIXART_TP_DEVICE(device);

	/* ensure we are not stuck in bootloader */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		if (!fu_pixart_tp_device_reset(self, FU_PIXART_TP_RESET_MODE_APPLICATION, error))
			return FALSE;
		fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}

	/* success */
	return TRUE;
}

static void
fu_pixart_tp_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 93, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 6, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gboolean
fu_pixart_tp_device_probe(FuDevice *device, GError **error)
{
	FuPixartTpDevice *self = FU_PIXART_TP_DEVICE(device);

	if (self->has_tf_child) {
		g_autoptr(FuPixartTpHapticDevice) child = fu_pixart_tp_haptic_device_new(device);
		fu_device_add_child(device, FU_DEVICE(child));
	}

	/* success */
	return TRUE;
}

static gchar *
fu_pixart_tp_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return g_strdup_printf("0x%04x", (guint16)version_raw);
}

static void
fu_pixart_tp_device_init(FuPixartTpDevice *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_protocol(FU_DEVICE(self), "com.pixart.tp");
	fu_device_set_summary(FU_DEVICE(self), "Touchpad");
	fu_device_add_icon(FU_DEVICE(self), "input-touchpad");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_PIXART_TP_FIRMWARE);
	self->sram_select = 0x0F;
	self->ver_bank = 0x00;
	self->ver_addr = 0xB2;
}

static void
fu_pixart_tp_device_class_init(FuPixartTpDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_pixart_tp_device_to_string;
	device_class->probe = fu_pixart_tp_device_probe;
	device_class->setup = fu_pixart_tp_device_setup;
	device_class->write_firmware = fu_pixart_tp_device_write_firmware;
	device_class->attach = fu_pixart_tp_device_attach;
	device_class->detach = fu_pixart_tp_device_detach;
	device_class->cleanup = fu_pixart_tp_device_cleanup;
	device_class->set_progress = fu_pixart_tp_device_set_progress;
	device_class->set_quirk_kv = fu_pixart_tp_device_set_quirk_kv;
	device_class->convert_version = fu_pixart_tp_device_convert_version;
	device_class->reload = fu_pixart_tp_device_reload;
}
