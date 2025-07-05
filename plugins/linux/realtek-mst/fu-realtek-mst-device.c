/*
 * Copyright 2021 Peter Marheine <pmarheine@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fcntl.h>
#include <glib/gstdio.h>

#include "fu-realtek-mst-device.h"
#include "fu-realtek-mst-struct.h"

/* firmware debug address */
#define I2C_ADDR_DEBUG 0x35
/* programming address */
#define I2C_ADDR_ISP 0x4a

/* write set to begin executing, cleared when done */
#define CMD_ERASE_BUSY 0x01

/* register for erase commands */
#define CMD_OPCODE_ERASE_SECTOR 0x20
#define CMD_OPCODE_ERASE_BLOCK	0xD8

/* register for read commands */
#define CMD_OPCODE_READ 0x03

/* register for write commands */
#define CMD_OPCODE_WRITE 0x02

/* when bit is set in mode register, ISP mode is active */
#define MCU_MODE_ISP (1 << 7)
/* write set to begin write, reset by device when complete */
#define MCU_MODE_WRITE_BUSY (1 << 5)
/* when bit is clear, write buffer contains data */
#define MCU_MODE_WRITE_BUF (1 << 4)

/* GPIO configuration/access registers */
#define REG_GPIO88_CONFIG 0x104F
#define REG_GPIO88_VALUE  0xFE3F

/* flash chip properties */
#define FLASH_SIZE	  0x100000
#define FLASH_SECTOR_SIZE 4096
#define FLASH_BLOCK_SIZE  65536

/* MST flash layout */
#define FLASH_USER1_ADDR 0x10000
#define FLASH_FLAG1_ADDR 0xfe304
#define FLASH_USER2_ADDR 0x80000
#define FLASH_FLAG2_ADDR 0xff304
#define FLASH_USER_SIZE	 0x70000

struct _FuRealtekMstDevice {
	FuI2cDevice parent_instance;
	FuRealtekMstDeviceFlashBank active_bank;
	FuRealtekMstDeviceDualBankMode mode;
};

G_DEFINE_TYPE(FuRealtekMstDevice, fu_realtek_mst_device, FU_TYPE_I2C_DEVICE)

#define FU_REALTEK_MST_DEVICE_IOCTL_TIMEOUT 5000 /* ms */

static void
fu_realtek_mst_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuRealtekMstDevice *self = FU_REALTEK_MST_DEVICE(device);
	fwupd_codec_string_append(str,
				  idt,
				  "ActiveBank",
				  fu_realtek_mst_device_flash_bank_to_string(self->active_bank));
	fwupd_codec_string_append(str,
				  idt,
				  "Mode",
				  fu_realtek_mst_device_dual_bank_mode_to_string(self->mode));
}

static gboolean
fu_realtek_mst_device_write_register(FuRealtekMstDevice *self,
				     guint8 address,
				     guint8 value,
				     GError **error)
{
	const guint8 command[] = {address, value};
	return fu_i2c_device_write(FU_I2C_DEVICE(self), command, sizeof(command), error);
}

static gboolean
fu_realtek_mst_device_write_register_multi(FuRealtekMstDevice *self,
					   guint8 address,
					   const guint8 *data,
					   gsize count,
					   GError **error)
{
	g_autofree guint8 *command = g_malloc0(count + 1);
	memcpy(command + 1, data, count); /* nocheck:blocked */
	command[0] = address;
	return fu_i2c_device_write(FU_I2C_DEVICE(self), command, count + 1, error);
}

static gboolean
fu_realtek_mst_device_read_register(FuRealtekMstDevice *self,
				    guint8 address,
				    guint8 *value,
				    GError **error)
{
	if (!fu_i2c_device_write(FU_I2C_DEVICE(self), &address, 0x1, error))
		return FALSE;
	return fu_i2c_device_read(FU_I2C_DEVICE(self), value, 0x1, error);
}

static gboolean
fu_realtek_mst_device_set_indirect_address(FuRealtekMstDevice *self,
					   guint16 address,
					   GError **error)
{
	if (!fu_realtek_mst_device_write_register(self,
						  FU_REALTEK_MST_REG_INDIRECT_LO,
						  0x9F,
						  error))
		return FALSE;
	if (!fu_realtek_mst_device_write_register(self,
						  FU_REALTEK_MST_REG_INDIRECT_HI,
						  address >> 8,
						  error))
		return FALSE;
	return fu_realtek_mst_device_write_register(self,
						    FU_REALTEK_MST_REG_INDIRECT_LO,
						    address,
						    error);
}

static gboolean
fu_realtek_mst_device_read_register_indirect(FuRealtekMstDevice *self,
					     guint16 address,
					     guint8 *value,
					     GError **error)
{
	if (!fu_realtek_mst_device_set_indirect_address(self, address, error))
		return FALSE;
	return fu_realtek_mst_device_read_register(self,
						   FU_REALTEK_MST_REG_INDIRECT_HI,
						   value,
						   error);
}

static gboolean
fu_realtek_mst_device_write_register_indirect(FuRealtekMstDevice *self,
					      guint16 address,
					      guint8 value,
					      GError **error)
{
	if (!fu_realtek_mst_device_set_indirect_address(self, address, error))
		return FALSE;
	return fu_realtek_mst_device_write_register(self,
						    FU_REALTEK_MST_REG_INDIRECT_HI,
						    value,
						    error);
}

/**
 * Wait until a device register reads an expected value.
 *
 * Waiting up to @timeout_seconds, poll the given @address for the read value
 * bitwise-ANDed with @mask to be equal to @expected.
 *
 * Returns an error if the timeout expires or in case of an I/O error.
 */
static gboolean
fu_realtek_mst_device_poll_register(FuRealtekMstDevice *self,
				    guint8 address,
				    guint8 mask,
				    guint8 expected,
				    guint timeout_seconds,
				    GError **error)
{
	guint8 value;
	g_autoptr(GTimer) timer = g_timer_new();

	if (!fu_realtek_mst_device_read_register(self, address, &value, error))
		return FALSE;
	while ((value & mask) != expected && g_timer_elapsed(timer, NULL) <= timeout_seconds) {
		fu_device_sleep(FU_DEVICE(self), 1); /* ms */
		if (!fu_realtek_mst_device_read_register(self, address, &value, error))
			return FALSE;
	}
	if ((value & mask) == expected)
		return TRUE;

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_TIMED_OUT,
		    "register %x still reads %x after %us, wanted %x (mask %x)",
		    address,
		    value,
		    timeout_seconds,
		    expected,
		    mask);
	return FALSE;
}

static gboolean
fu_realtek_mst_device_set_gpio88(FuRealtekMstDevice *self, gboolean level, GError **error)
{
	guint8 value;

	/* ensure pin is configured as push-pull GPIO */
	if (!fu_realtek_mst_device_read_register_indirect(self, REG_GPIO88_CONFIG, &value, error))
		return FALSE;
	if (!fu_realtek_mst_device_write_register_indirect(self,
							   REG_GPIO88_CONFIG,
							   (value & 0xF0) | 1,
							   error))
		return FALSE;

	/* set output level */
	g_debug("set pin 88 = %d", level);
	if (!fu_realtek_mst_device_read_register_indirect(self, REG_GPIO88_VALUE, &value, error))
		return FALSE;
	return fu_realtek_mst_device_write_register_indirect(self,
							     REG_GPIO88_VALUE,
							     (value & 0xFE) | (level != FALSE),
							     error);
}

static gboolean
fu_realtek_mst_device_setup(FuDevice *device, GError **error)
{
	FuRealtekMstDevice *self = FU_REALTEK_MST_DEVICE(device);
	const guint8 request[] = {0x01};
	guint8 response[11] = {0x0};
	g_autofree gchar *version = NULL;

	if (!fu_i2c_device_set_address(FU_I2C_DEVICE(self), I2C_ADDR_DEBUG, FALSE, error)) {
		g_prefix_error(error, "failed to ensure address: ");
		return FALSE;
	}

	/* switch to DDCCI mode */
	if (!fu_realtek_mst_device_write_register(self, 0xca, 0x09, error))
		return FALSE;

	/* wait for mode switch to complete */
	fu_device_sleep(FU_DEVICE(self), 200); /* ms */

	/* request dual bank state and read back */
	if (!fu_i2c_device_write(FU_I2C_DEVICE(self), request, sizeof(request), error))
		return FALSE;
	if (!fu_i2c_device_read(FU_I2C_DEVICE(self), response, sizeof(response), error))
		return FALSE;

	if (response[0] != 0xCA || response[1] != 9) {
		/* unexpected response code or length usually means the current
		 * firmware doesn't support dual-bank mode at all */
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unexpected response code %#x, length %d",
			    response[0],
			    response[1]);
		return FALSE;
	}

	/* enable flag, assume anything other than 1 is unsupported */
	if (response[2] != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "dual-bank mode is not enabled");
		return FALSE;
	}
	self->mode = response[3];
	if (self->mode != FU_REALTEK_MST_DEVICE_DUAL_BANK_MODE_DIFF) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unexpected dual bank mode value %#x",
			    self->mode);
		return FALSE;
	}
	self->active_bank = response[4];
	if (self->active_bank >= FU_REALTEK_MST_DEVICE_FLASH_BANK_LAST) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unexpected active flash bank value %#x",
			    self->active_bank);
		return FALSE;
	}
	if (self->active_bank == FU_REALTEK_MST_DEVICE_FLASH_BANK_USER1) {
		version = g_strdup_printf("%u.%u", response[5], response[6]);
	} else if (self->active_bank == FU_REALTEK_MST_DEVICE_FLASH_BANK_USER2) {
		version = g_strdup_printf("%u.%u", response[7], response[8]);
	}
	fu_device_set_version(device, version);

	/* last two bytes of response are reserved */
	return TRUE;
}

static gboolean
fu_realtek_mst_device_flash_iface_read(FuRealtekMstDevice *self,
				       guint32 address,
				       guint8 *buf,
				       const gsize buf_size,
				       FuProgress *progress,
				       GError **error)
{
	gsize bytes_read = 0;
	guint8 byte;
	const guint8 req[] = {0x70};

	g_return_val_if_fail(address < FLASH_SIZE, FALSE);
	g_return_val_if_fail(buf_size <= FLASH_SIZE, FALSE);

	g_debug("read %#" G_GSIZE_MODIFIER "x bytes from %#08x", buf_size, address);

	/* read must start one byte prior to the desired address and ignore the
	 * first byte of data, since the first read value is unpredictable */
	address = (address - 1) & 0xFFFFFF;
	if (!fu_realtek_mst_device_write_register(self,
						  FU_REALTEK_MST_REG_CMD_ADDR_HI,
						  address >> 16,
						  error))
		return FALSE;
	if (!fu_realtek_mst_device_write_register(self,
						  FU_REALTEK_MST_REG_CMD_ADDR_MID,
						  address >> 8,
						  error))
		return FALSE;
	if (!fu_realtek_mst_device_write_register(self,
						  FU_REALTEK_MST_REG_CMD_ADDR_LO,
						  address,
						  error))
		return FALSE;
	if (!fu_realtek_mst_device_write_register(self,
						  FU_REALTEK_MST_REG_READ_OPCODE,
						  CMD_OPCODE_READ,
						  error))
		return FALSE;

	/* ignore first byte of data */
	if (!fu_i2c_device_write(FU_I2C_DEVICE(self), req, sizeof(req), error))
		return FALSE;
	if (!fu_i2c_device_read(FU_I2C_DEVICE(self), &byte, 0x1, error))
		return FALSE;

	while (bytes_read < buf_size) {
		/* read up to 256 bytes in one transaction */
		gsize read_len = buf_size - bytes_read;
		if (read_len > 256)
			read_len = 256;

		if (!fu_i2c_device_read(FU_I2C_DEVICE(self), buf + bytes_read, read_len, error))
			return FALSE;

		bytes_read += read_len;
		fu_progress_set_percentage_full(progress, bytes_read, buf_size);
	}
	return TRUE;
}

static gboolean
fu_realtek_mst_device_flash_iface_erase_sector(FuRealtekMstDevice *self,
					       guint32 address,
					       GError **error)
{
	/* address must be 4k-aligned */
	g_return_val_if_fail((address & 0xFFF) == 0, FALSE);
	g_debug("sector erase %#08x-%#08x", address, address + FLASH_SECTOR_SIZE);

	/* sector address */
	if (!fu_realtek_mst_device_write_register(self,
						  FU_REALTEK_MST_REG_CMD_ADDR_HI,
						  address >> 16,
						  error))
		return FALSE;
	if (!fu_realtek_mst_device_write_register(self,
						  FU_REALTEK_MST_REG_CMD_ADDR_MID,
						  address >> 8,
						  error))
		return FALSE;
	if (!fu_realtek_mst_device_write_register(self,
						  FU_REALTEK_MST_REG_CMD_ADDR_LO,
						  address,
						  error))
		return FALSE;
	/* command type + WREN */
	if (!fu_realtek_mst_device_write_register(self, FU_REALTEK_MST_REG_CMD_ATTR, 0xB8, error))
		return FALSE;
	if (!fu_realtek_mst_device_write_register(self,
						  FU_REALTEK_MST_REG_ERASE_OPCODE,
						  CMD_OPCODE_ERASE_SECTOR,
						  error))
		return FALSE;
	/* begin operation and wait for completion */
	if (!fu_realtek_mst_device_write_register(self,
						  FU_REALTEK_MST_REG_CMD_ATTR,
						  0xB8 | CMD_ERASE_BUSY,
						  error))
		return FALSE;
	return fu_realtek_mst_device_poll_register(self,
						   FU_REALTEK_MST_REG_CMD_ATTR,
						   CMD_ERASE_BUSY,
						   0,
						   10,
						   error);
}

static gboolean
fu_realtek_mst_device_flash_iface_erase_block(FuRealtekMstDevice *self,
					      guint32 address,
					      GError **error)
{
	/* address must be 64k-aligned */
	g_return_val_if_fail((address & 0xFFFF) == 0, FALSE);
	g_debug("block erase %#08x-%#08x", address, address + FLASH_BLOCK_SIZE);

	/* block address */
	if (!fu_realtek_mst_device_write_register(self,
						  FU_REALTEK_MST_REG_CMD_ADDR_HI,
						  address >> 16,
						  error))
		return FALSE;
	if (!fu_realtek_mst_device_write_register(self, FU_REALTEK_MST_REG_CMD_ADDR_MID, 0, error))
		return FALSE;
	if (!fu_realtek_mst_device_write_register(self, FU_REALTEK_MST_REG_CMD_ADDR_LO, 0, error))
		return FALSE;
	/* command type + WREN */
	if (!fu_realtek_mst_device_write_register(self, FU_REALTEK_MST_REG_CMD_ATTR, 0xB8, error))
		return FALSE;
	if (!fu_realtek_mst_device_write_register(self,
						  FU_REALTEK_MST_REG_ERASE_OPCODE,
						  CMD_OPCODE_ERASE_BLOCK,
						  error))
		return FALSE;
	/* begin operation and wait for completion */
	if (!fu_realtek_mst_device_write_register(self,
						  FU_REALTEK_MST_REG_CMD_ATTR,
						  0xB8 | CMD_ERASE_BUSY,
						  error))
		return FALSE;
	return fu_realtek_mst_device_poll_register(self,
						   FU_REALTEK_MST_REG_CMD_ATTR,
						   CMD_ERASE_BUSY,
						   0,
						   10,
						   error);
}

static gboolean
fu_realtek_mst_device_flash_iface_write(FuRealtekMstDevice *self,
					guint32 address,
					GBytes *data,
					FuProgress *progress,
					GError **error)
{
	gsize total_size = g_bytes_get_size(data);
	g_autoptr(FuChunkArray) chunks =
	    fu_chunk_array_new_from_bytes(data, address, FU_CHUNK_PAGESZ_NONE, 256);

	g_debug("write %#" G_GSIZE_MODIFIER "x bytes at %#08x", total_size, address);
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chunk = NULL;
		guint32 chunk_address;
		guint32 chunk_size;

		/* prepare chunk */
		chunk = fu_chunk_array_index(chunks, i, error);
		if (chunk == NULL)
			return FALSE;
		chunk_address = fu_chunk_get_address(chunk);
		chunk_size = fu_chunk_get_data_sz(chunk);

		/* write opcode */
		if (!fu_realtek_mst_device_write_register(self,
							  FU_REALTEK_MST_REG_WRITE_OPCODE,
							  CMD_OPCODE_WRITE,
							  error))
			return FALSE;
		/* write length */
		if (!fu_realtek_mst_device_write_register(self,
							  FU_REALTEK_MST_REG_WRITE_LEN,
							  chunk_size - 1,
							  error))
			return FALSE;
		/* target address */
		if (!fu_realtek_mst_device_write_register(self,
							  FU_REALTEK_MST_REG_CMD_ADDR_HI,
							  chunk_address >> 16,
							  error))
			return FALSE;
		if (!fu_realtek_mst_device_write_register(self,
							  FU_REALTEK_MST_REG_CMD_ADDR_MID,
							  chunk_address >> 8,
							  error))
			return FALSE;
		if (!fu_realtek_mst_device_write_register(self,
							  FU_REALTEK_MST_REG_CMD_ADDR_LO,
							  chunk_address,
							  error))
			return FALSE;
		/* ensure write buffer is empty */
		if (!fu_realtek_mst_device_poll_register(self,
							 FU_REALTEK_MST_REG_MCU_MODE,
							 MCU_MODE_WRITE_BUF,
							 MCU_MODE_WRITE_BUF,
							 10,
							 error)) {
			g_prefix_error(error, "failed waiting for write buffer to clear: ");
			return FALSE;
		}
		/* write data into FIFO */
		if (!fu_realtek_mst_device_write_register_multi(self,
								FU_REALTEK_MST_REG_WRITE_FIFO,
								fu_chunk_get_data(chunk),
								chunk_size,
								error))
			return FALSE;
		/* begin operation and wait for completion */
		if (!fu_realtek_mst_device_write_register(self,
							  FU_REALTEK_MST_REG_MCU_MODE,
							  MCU_MODE_ISP | MCU_MODE_WRITE_BUSY,
							  error))
			return FALSE;
		if (!fu_realtek_mst_device_poll_register(self,
							 FU_REALTEK_MST_REG_MCU_MODE,
							 MCU_MODE_WRITE_BUSY,
							 0,
							 10,
							 error)) {
			g_prefix_error(error,
				       "timed out waiting for write at %#x to complete: ",
				       address);
			return FALSE;
		}
		fu_progress_set_percentage_full(progress, i + 1, fu_chunk_array_length(chunks));
	}

	return TRUE;
}

static gboolean
fu_realtek_mst_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuRealtekMstDevice *self = FU_REALTEK_MST_DEVICE(device);

	if (!fu_i2c_device_set_address(FU_I2C_DEVICE(self), I2C_ADDR_ISP, FALSE, error))
		return FALSE;

	/* Switch to programming mode (stops regular operation) */
	if (!fu_realtek_mst_device_write_register(self,
						  FU_REALTEK_MST_REG_MCU_MODE,
						  MCU_MODE_ISP,
						  error))
		return FALSE;
	g_debug("wait for ISP mode ready");
	if (!fu_realtek_mst_device_poll_register(self,
						 FU_REALTEK_MST_REG_MCU_MODE,
						 MCU_MODE_ISP,
						 MCU_MODE_ISP,
						 60,
						 error))
		return FALSE;

	/* magic value makes the MCU clock run faster than normal; this both
	 * helps programming performance and fixes flakiness where register
	 * writes sometimes get nacked for no apparent reason */
	if (!fu_realtek_mst_device_write_register_indirect(self, 0x06A0, 0x74, error))
		return FALSE;

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);

	/* Disable hardware write protect, assuming Flash ~WP is connected to
	 * device pin 88, a GPIO. */
	return fu_realtek_mst_device_set_gpio88(self, 1, error);
}

static gboolean
fu_realtek_mst_device_write_firmware(FuDevice *device,
				     FuFirmware *firmware,
				     FuProgress *progress,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuRealtekMstDevice *self = FU_REALTEK_MST_DEVICE(device);
	/* write an inactive bank: USER2 if USER1 is active, otherwise USER1
	 * (including if the boot bank is active) */
	guint32 base_addr = self->active_bank == FU_REALTEK_MST_DEVICE_FLASH_BANK_USER1
				? FLASH_USER2_ADDR
				: FLASH_USER1_ADDR;
	guint32 flag_addr = self->active_bank == FU_REALTEK_MST_DEVICE_FLASH_BANK_USER1
				? FLASH_FLAG2_ADDR
				: FLASH_FLAG1_ADDR;
	const guint8 flag_data[] = {0xaa, 0xaa, 0xaa, 0xff, 0xff};
	g_autofree guint8 *readback_buf = g_malloc0(FLASH_USER_SIZE);
	g_autoptr(GBytes) firmware_bytes = NULL;

	/* sanity check */
	firmware_bytes = fu_firmware_get_bytes(firmware, error);
	if (firmware_bytes == NULL)
		return FALSE;
	if (g_bytes_get_size(firmware_bytes) != FLASH_USER_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid size, got 0x%x, expected 0x%x",
			    (guint)g_bytes_get_size(firmware_bytes),
			    (guint)FLASH_USER_SIZE);
		return FALSE;
	}

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 20, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 70, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 9, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "flag");

	if (!fu_i2c_device_set_address(FU_I2C_DEVICE(self), I2C_ADDR_ISP, FALSE, error))
		return FALSE;

	/* erase old image */
	g_debug("erase old image from %#x", base_addr);
	for (guint32 offset = 0; offset < FLASH_USER_SIZE; offset += FLASH_BLOCK_SIZE) {
		if (!fu_realtek_mst_device_flash_iface_erase_block(self, base_addr + offset, error))
			return FALSE;
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						offset + FLASH_BLOCK_SIZE,
						FLASH_USER_SIZE);
	}
	fu_progress_step_done(progress);

	/* write new image */
	g_debug("write new image to %#x", base_addr);
	if (!fu_realtek_mst_device_flash_iface_write(self,
						     base_addr,
						     firmware_bytes,
						     fu_progress_get_child(progress),
						     error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify */
	if (!fu_realtek_mst_device_flash_iface_read(self,
						    base_addr,
						    readback_buf,
						    FLASH_USER_SIZE,
						    fu_progress_get_child(progress),
						    error))
		return FALSE;
	if (memcmp(g_bytes_get_data(firmware_bytes, NULL), readback_buf, FLASH_USER_SIZE) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "flash contents after write do not match firmware image");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* Erase old flag and write new one. The MST appears to modify the
	 * flag value once booted, so we always write the same value here and
	 * it picks up what we've updated. */
	if (!fu_realtek_mst_device_flash_iface_erase_sector(self,
							    flag_addr & ~(FLASH_SECTOR_SIZE - 1),
							    error))
		return FALSE;
	if (!fu_realtek_mst_device_flash_iface_write(
		self,
		flag_addr,
		g_bytes_new_static(flag_data, sizeof(flag_data)),
		fu_progress_get_child(progress),
		error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_realtek_mst_device_read_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuRealtekMstDevice *self = FU_REALTEK_MST_DEVICE(device);
	guint32 bank_address;
	g_autofree guint8 *image_bytes = NULL;
	g_autoptr(GBytes) firmware_bytes = NULL;

	if (self->active_bank == FU_REALTEK_MST_DEVICE_FLASH_BANK_USER1)
		bank_address = FLASH_USER1_ADDR;
	else if (self->active_bank == FU_REALTEK_MST_DEVICE_FLASH_BANK_USER2)
		bank_address = FLASH_USER2_ADDR;
	else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot read firmware from bank %u",
			    self->active_bank);
		return NULL;
	}

	image_bytes = g_malloc0(FLASH_USER_SIZE);
	if (!fu_i2c_device_set_address(FU_I2C_DEVICE(self), I2C_ADDR_ISP, FALSE, error))
		return NULL;
	if (!fu_realtek_mst_device_flash_iface_read(self,
						    bank_address,
						    image_bytes,
						    FLASH_USER_SIZE,
						    progress,
						    error))
		return NULL;
	firmware_bytes = g_bytes_new_take(g_steal_pointer(&image_bytes), FLASH_USER_SIZE);

	return fu_firmware_new_from_bytes(firmware_bytes);
}

static GBytes *
fu_realtek_mst_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuRealtekMstDevice *self = FU_REALTEK_MST_DEVICE(device);
	g_autofree guint8 *flash_contents = g_malloc0(FLASH_SIZE);

	if (!fu_i2c_device_set_address(FU_I2C_DEVICE(self), I2C_ADDR_ISP, FALSE, error))
		return NULL;
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);
	if (!fu_realtek_mst_device_flash_iface_read(self,
						    0,
						    flash_contents,
						    FLASH_SIZE,
						    progress,
						    error))
		return NULL;
	fu_progress_set_status(progress, FWUPD_STATUS_IDLE);

	return g_bytes_new_take(g_steal_pointer(&flash_contents), FLASH_SIZE);
}

static gboolean
fu_realtek_mst_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuRealtekMstDevice *self = FU_REALTEK_MST_DEVICE(device);
	guint8 value;

	if (!fu_i2c_device_set_address(FU_I2C_DEVICE(self), I2C_ADDR_ISP, FALSE, error))
		return FALSE;

	/* re-enable hardware write protect via GPIO */
	if (!fu_realtek_mst_device_set_gpio88(self, 0, error))
		return FALSE;

	if (!fu_realtek_mst_device_read_register(self, FU_REALTEK_MST_REG_MCU_MODE, &value, error))
		return FALSE;
	if ((value & MCU_MODE_ISP) != 0) {
		g_autoptr(GError) error_local = NULL;

		g_debug("resetting device to exit ISP mode");

		/* Set register EE bit 2 to request reset. This write can fail
		 * spuriously, so we ignore the write result and verify the device is
		 * no longer in programming mode after giving it time to reset. */
		if (!fu_realtek_mst_device_read_register(self, 0xEE, &value, error))
			return FALSE;
		if (!fu_realtek_mst_device_write_register(self, 0xEE, value | 2, &error_local)) {
			g_debug("write spuriously failed, ignoring: %s", error_local->message);
		}

		/* allow device some time to reset */
		fu_device_sleep(device, 1000); /* ms */

		/* verify device has exited programming mode and actually reset */
		if (!fu_realtek_mst_device_read_register(self,
							 FU_REALTEK_MST_REG_MCU_MODE,
							 &value,
							 error))
			return FALSE;
		if ((value & MCU_MODE_ISP) == MCU_MODE_ISP) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NEEDS_USER_ACTION,
					    "device failed to reset when requested");
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN);
			return FALSE;
		}
	} else {
		g_debug("device is already in normal mode");
	}

	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static void
fu_realtek_mst_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static void
fu_realtek_mst_device_init(FuRealtekMstDevice *self)
{
	self->active_bank = FU_REALTEK_MST_DEVICE_FLASH_BANK_LAST;
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_NO_GENERIC_GUIDS);
	fu_device_add_protocol(FU_DEVICE(self), "com.realtek.rtd2142");
	fu_device_set_vendor(FU_DEVICE(self), "Realtek");
	fu_device_build_vendor_id_u16(FU_DEVICE(self), "PCI", 0x10EC);
	fu_device_set_summary(FU_DEVICE(self), "DisplayPort MST hub");
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_VIDEO_DISPLAY);
	fu_device_set_firmware_size(FU_DEVICE(self), FLASH_USER_SIZE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
}

static void
fu_realtek_mst_device_class_init(FuRealtekMstDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_realtek_mst_device_to_string;
	device_class->setup = fu_realtek_mst_device_setup;
	device_class->detach = fu_realtek_mst_device_detach;
	device_class->attach = fu_realtek_mst_device_attach;
	device_class->write_firmware = fu_realtek_mst_device_write_firmware;
	device_class->reload = fu_realtek_mst_device_setup;
	device_class->read_firmware = fu_realtek_mst_device_read_firmware;
	device_class->dump_firmware = fu_realtek_mst_device_dump_firmware;
	device_class->set_progress = fu_realtek_mst_device_set_progress;
}
