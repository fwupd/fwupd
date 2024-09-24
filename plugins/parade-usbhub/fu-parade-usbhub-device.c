/*
 * Copyright 2023 Parade Technology, Ltd
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * Based on PS8830_FwUpd_SampleCode.cpp, v1.0.1.0
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-parade-usbhub-common.h"
#include "fu-parade-usbhub-device.h"
#include "fu-parade-usbhub-firmware.h"
#include "fu-parade-usbhub-struct.h"

struct _FuParadeUsbhubDevice {
	FuUsbDevice parent_instance;
	FuCfiDevice *cfi_device;
	guint32 spi_address;
};

G_DEFINE_TYPE(FuParadeUsbhubDevice, fu_parade_usbhub_device, FU_TYPE_USB_DEVICE)

#define FU_PARADE_USBHUB_DEVICE_TIMEOUT 1500 /* ms */

#define FU_PARADE_USBHUB_SPI_ROM_BANK_SIZE 0x10000

#define FU_PARADE_USBHUB_SPI_ROM_ADDRESS_BANK4_HUB_FIRMWARE_1 0x40000
#define FU_PARADE_USBHUB_SPI_ROM_ADDRESS_BANK5_HUB_FIRMWARE_2 0x50000

#define FU_PARADE_USBHUB_SPI_ROM_ERASE_SIZE 4096u

#define FU_PARADE_USBHUB_SPI_ROM_CHECKSUM_BUFFER_SIZE 0xFFFF

#define FU_PARADE_USBHUB_DMA_SRAM_ADDRESS 0xF800
#define FU_PARADE_USBHUB_DMA_SRAM_SIZE	  1024

#define FU_PARADE_USBHUB_DEVICE_SPI_BURST_DBI_MAX    4
#define FU_PARADE_USBHUB_DEVICE_MMIO_BURST_WRITE_MAX 16

#define FU_PARADE_USBHUB_DEVICE_SPI_RETRY_COUNT 100
#define FU_PARADE_USBHUB_DEVICE_SPI_RETRY_DELAY 50 /* ms */

static void
fu_parade_usbhub_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuParadeUsbhubDevice *self = FU_PARADE_USBHUB_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "SpiAddress", self->spi_address);
}

static gboolean
fu_parade_usbhub_device_mmio_read_u8(FuParadeUsbhubDevice *self,
				     guint16 address,
				     guint8 *data,
				     GError **error)
{
	return fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					      FU_USB_DIRECTION_DEVICE_TO_HOST,
					      FU_USB_REQUEST_TYPE_VENDOR,
					      FU_USB_RECIPIENT_DEVICE,
					      FU_PARADE_USBHUB_DEVICE_REQUEST_READ,
					      0x0, /* always 0 */
					      address,
					      data,
					      sizeof(*data),
					      NULL,
					      FU_PARADE_USBHUB_DEVICE_TIMEOUT,
					      NULL,
					      error);
}

static gboolean
fu_parade_usbhub_device_mmio_read(FuParadeUsbhubDevice *self,
				  guint16 address,
				  guint8 *buf,
				  gsize bufsz,
				  GError **error)
{
	for (gsize i = 0; i < bufsz; i++) {
		if (!fu_parade_usbhub_device_mmio_read_u8(self, address + i, buf + i, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_parade_usbhub_device_mmio_write_raw(FuParadeUsbhubDevice *self,
				       guint16 address,
				       guint8 *buf,
				       gsize bufsz,
				       GError **error)
{
	return fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					      FU_USB_DIRECTION_HOST_TO_DEVICE,
					      FU_USB_REQUEST_TYPE_VENDOR,
					      FU_USB_RECIPIENT_DEVICE,
					      FU_PARADE_USBHUB_DEVICE_REQUEST_WRITE,
					      0x0, /* always 0 */
					      address,
					      buf,
					      bufsz,
					      NULL,
					      FU_PARADE_USBHUB_DEVICE_TIMEOUT,
					      NULL,
					      error);
}

static gboolean
fu_parade_usbhub_device_mmio_write(FuParadeUsbhubDevice *self,
				   guint16 address,
				   guint8 *buf,
				   gsize bufsz,
				   GError **error)
{
	for (gsize i = 0; i < bufsz; i++) {
		if (!fu_parade_usbhub_device_mmio_write_raw(self,
							    address + i,
							    buf + i,
							    sizeof(guint8),
							    error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_parade_usbhub_device_mmio_write_u8(FuParadeUsbhubDevice *self,
				      guint16 address,
				      guint8 data,
				      GError **error)
{
	return fu_parade_usbhub_device_mmio_write(self, address, &data, sizeof(data), error);
}

static gboolean
fu_parade_usbhub_device_mmio_write_u16(FuParadeUsbhubDevice *self,
				       guint16 address,
				       guint16 data,
				       GError **error)
{
	guint8 buf[2] = {0};
	fu_memwrite_uint16(buf, data, G_LITTLE_ENDIAN);
	return fu_parade_usbhub_device_mmio_write(self, address, buf, sizeof(buf), error);
}

static gboolean
fu_parade_usbhub_device_mmio_write_u24(FuParadeUsbhubDevice *self,
				       guint16 address,
				       guint32 data,
				       GError **error)
{
	guint8 buf[3] = {0};
	fu_memwrite_uint24(buf, data, G_LITTLE_ENDIAN);
	return fu_parade_usbhub_device_mmio_write(self, address, buf, sizeof(buf), error);
}

static gboolean
fu_parade_usbhub_device_mmio_set_bit(FuParadeUsbhubDevice *self,
				     guint16 address,
				     gsize bit_offset,
				     GError **error)
{
	guint8 val = 0;
	if (!fu_parade_usbhub_device_mmio_read_u8(self, address, &val, error))
		return FALSE;
	FU_BIT_SET(val, bit_offset);
	return fu_parade_usbhub_device_mmio_write_u8(self, address, val, error);
}

static gboolean
fu_parade_usbhub_device_mmio_clear_bit(FuParadeUsbhubDevice *self,
				       guint16 address,
				       gsize bit_offset,
				       GError **error)
{
	guint8 val = 0;
	if (!fu_parade_usbhub_device_mmio_read_u8(self, address, &val, error))
		return FALSE;
	FU_BIT_CLEAR(val, bit_offset);
	return fu_parade_usbhub_device_mmio_write_u8(self, address, val, error);
}

static gboolean
fu_parade_usbhub_device_spi_rom_wait_done_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuParadeUsbhubDevice *self = FU_PARADE_USBHUB_DEVICE(device);
	guint8 val = 0;
	if (!fu_parade_usbhub_device_mmio_read_u8(self,
						  FU_PARADE_USBHUB_DEVICE_ADDR_STATUS,
						  &val,
						  error))
		return FALSE;
	if ((val & FU_PARADE_USBHUB_DEVICE_STATUS_FLAG_SPI_DONE) == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "not done");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_parade_usbhub_device_spi_rom_wait_done(FuParadeUsbhubDevice *self, GError **error)
{
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_parade_usbhub_device_spi_rom_wait_done_cb,
				    FU_PARADE_USBHUB_DEVICE_SPI_RETRY_COUNT,
				    FU_PARADE_USBHUB_DEVICE_SPI_RETRY_DELAY,
				    NULL,
				    error);
}

static gboolean
fu_parade_usbhub_device_spi_read_dma_dbi(FuParadeUsbhubDevice *self,
					 guint8 spi_command,
					 guint32 spi_address,
					 gsize spi_command_size,
					 guint8 *buf,
					 gsize bufsz,
					 GError **error)
{
	if (!fu_parade_usbhub_device_mmio_write_u8(self,
						   FU_PARADE_USBHUB_DEVICE_ADDR_DMA_SIZE,
						   spi_command_size,
						   error))
		return FALSE;
	if (!fu_parade_usbhub_device_mmio_write_u8(self,
						   FU_PARADE_USBHUB_DEVICE_ADDR_READ_SIZE,
						   bufsz,
						   error))
		return FALSE;

	/* SPI command */
	if (spi_command_size > 0) {
		guint8 buf_spi[4] = {0};

		g_return_val_if_fail(spi_command_size <= sizeof(buf_spi), FALSE);

		/* write data to SPI buffer */
		buf_spi[0] = spi_command;
		fu_memwrite_uint24(buf_spi + 1, spi_address, G_BIG_ENDIAN);
		if (!fu_parade_usbhub_device_mmio_write(self,
							FU_PARADE_USBHUB_DEVICE_ADDR_DATA,
							buf_spi,
							spi_command_size,
							error))
			return FALSE;
	}

	/* trigger read */
	if (!fu_parade_usbhub_device_mmio_write_u8(self,
						   FU_PARADE_USBHUB_DEVICE_ADDR_STATUS,
						   FU_PARADE_USBHUB_DEVICE_STATUS_FLAG_TRIGGER_DBI,
						   error))
		return FALSE;

	/* polling status bit */
	if (!fu_parade_usbhub_device_spi_rom_wait_done(self, error))
		return FALSE;

	/* read data buffer */
	return fu_parade_usbhub_device_mmio_read(self,
						 FU_PARADE_USBHUB_DEVICE_ADDR_DATA,
						 buf,
						 bufsz,
						 error);
}

static gboolean
fu_parade_usbhub_device_spi_write_dma_dbi(FuParadeUsbhubDevice *self,
					  guint8 spi_command,
					  guint32 spi_address,
					  gsize spi_command_size,
					  guint8 *buf,
					  gsize bufsz,
					  GError **error)
{
	if (spi_command_size > 0) {
		guint8 buf_spi[4] = {0};

		g_return_val_if_fail(spi_command_size <= sizeof(buf_spi), FALSE);

		if (!fu_parade_usbhub_device_mmio_write_u8(self,
							   FU_PARADE_USBHUB_DEVICE_ADDR_DMA_SIZE,
							   spi_command_size,
							   error))
			return FALSE;
		if (!fu_parade_usbhub_device_mmio_write_u8(self,
							   FU_PARADE_USBHUB_DEVICE_ADDR_READ_SIZE,
							   0,
							   error))
			return FALSE;

		/* write data */
		buf_spi[0] = spi_command;
		fu_memwrite_uint24(buf_spi + 1, spi_address, G_BIG_ENDIAN);
		if (!fu_parade_usbhub_device_mmio_write(self,
							FU_PARADE_USBHUB_DEVICE_ADDR_DATA,
							buf_spi,
							spi_command_size,
							error))
			return FALSE;

		/* trigger write */
		if (!fu_parade_usbhub_device_mmio_write_u8(
			self,
			FU_PARADE_USBHUB_DEVICE_ADDR_STATUS,
			FU_PARADE_USBHUB_DEVICE_STATUS_FLAG_TRIGGER_DBI |
			    FU_PARADE_USBHUB_DEVICE_STATUS_FLAG_WRITE,
			error))
			return FALSE;
		if (!fu_parade_usbhub_device_spi_rom_wait_done(self, error))
			return FALSE;
	}

	if (bufsz > 0) {
		if (!fu_parade_usbhub_device_mmio_write_u8(self,
							   FU_PARADE_USBHUB_DEVICE_ADDR_DMA_SIZE,
							   bufsz,
							   error))
			return FALSE;
		if (!fu_parade_usbhub_device_mmio_write_u8(self,
							   FU_PARADE_USBHUB_DEVICE_ADDR_READ_SIZE,
							   0,
							   error))
			return FALSE;

		/* write data and trigger */
		if (!fu_parade_usbhub_device_mmio_write(self,
							FU_PARADE_USBHUB_DEVICE_ADDR_DATA,
							buf,
							bufsz,
							error))
			return FALSE;
		if (!fu_parade_usbhub_device_mmio_write_u8(
			self,
			FU_PARADE_USBHUB_DEVICE_ADDR_STATUS,
			FU_PARADE_USBHUB_DEVICE_STATUS_FLAG_TRIGGER_DBI |
			    FU_PARADE_USBHUB_DEVICE_STATUS_FLAG_WRITE,
			error))
			return FALSE;
		if (!fu_parade_usbhub_device_spi_rom_wait_done(self, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_parade_usbhub_device_spi_data_read(FuParadeUsbhubDevice *self,
				      guint8 spi_command,
				      guint32 spi_address,
				      gsize spi_command_size,
				      guint8 *buf,
				      gsize bufsz,
				      GError **error)
{
	g_autoptr(GPtrArray) chunks = NULL;

	/* no data */
	if (bufsz == 0) {
		return fu_parade_usbhub_device_spi_read_dma_dbi(self,
								spi_command,
								spi_address,
								spi_command_size,
								NULL,
								0,
								error);
	}

	/* blocks of data */
	chunks = fu_chunk_array_mutable_new(buf,
					    bufsz,
					    spi_address,
					    0x0,
					    FU_PARADE_USBHUB_DEVICE_SPI_BURST_DBI_MAX);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_parade_usbhub_device_spi_read_dma_dbi(self,
							      spi_command,
							      fu_chunk_get_address(chk),
							      spi_command_size,
							      fu_chunk_get_data_out(chk),
							      fu_chunk_get_data_sz(chk),
							      error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_parade_usbhub_device_spi_data_write(FuParadeUsbhubDevice *self,
				       guint8 spi_command,
				       guint32 spi_address,
				       gsize spi_command_size,
				       guint8 *buf,
				       gsize bufsz,
				       GError **error)
{
	g_autoptr(GPtrArray) chunks = NULL;

	/* no data */
	if (bufsz == 0) {
		return fu_parade_usbhub_device_spi_write_dma_dbi(self,
								 spi_command,
								 spi_address,
								 spi_command_size,
								 NULL,
								 0,
								 error);
	}

	/* blocks of data */
	chunks = fu_chunk_array_mutable_new(buf,
					    bufsz,
					    spi_address,
					    0x0,
					    FU_PARADE_USBHUB_DEVICE_SPI_BURST_DBI_MAX);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_parade_usbhub_device_spi_write_dma_dbi(self,
							       spi_command,
							       fu_chunk_get_address(chk),
							       spi_command_size,
							       fu_chunk_get_data_out(chk),
							       fu_chunk_get_data_sz(chk),
							       error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_parade_usbhub_device_spi_data_write_ex(FuParadeUsbhubDevice *self,
					  guint8 *buf,
					  gsize bufsz,
					  GError **error)
{
	if (!fu_parade_usbhub_device_mmio_write_u8(self,
						   FU_PARADE_USBHUB_DEVICE_ADDR_DMA_SIZE,
						   bufsz,
						   error))
		return FALSE;
	if (!fu_parade_usbhub_device_mmio_write_u8(self,
						   FU_PARADE_USBHUB_DEVICE_ADDR_READ_SIZE,
						   0,
						   error))
		return FALSE;

	/* write data and trigger */
	if (!fu_parade_usbhub_device_mmio_write(self,
						FU_PARADE_USBHUB_DEVICE_ADDR_DATA,
						buf,
						bufsz,
						error))
		return FALSE;
	if (!fu_parade_usbhub_device_mmio_write_u8(self,
						   FU_PARADE_USBHUB_DEVICE_ADDR_STATUS,
						   FU_PARADE_USBHUB_DEVICE_STATUS_FLAG_TRIGGER_DBI |
						       FU_PARADE_USBHUB_DEVICE_STATUS_FLAG_WRITE,
						   error))
		return FALSE;
	return fu_parade_usbhub_device_spi_rom_wait_done(self, error);
}

static gboolean
fu_parade_usbhub_device_spi_write_command(FuParadeUsbhubDevice *self,
					  guint8 spi_command,
					  guint32 spi_address,
					  gsize spi_command_size,
					  GError **error)
{
	return fu_parade_usbhub_device_spi_data_write(self,
						      spi_command,
						      spi_address,
						      spi_command_size,
						      NULL,
						      0,
						      error);
}

static gboolean
fu_parade_usbhub_device_enable_spi_master(FuParadeUsbhubDevice *self, GError **error)
{
	return fu_parade_usbhub_device_mmio_set_bit(self,
						    FU_PARADE_USBHUB_DEVICE_ADDR_SPI_MASTER,
						    4,
						    error);
}

static gboolean
fu_parade_usbhub_device_disable_spi_master(FuParadeUsbhubDevice *self, GError **error)
{
	return fu_parade_usbhub_device_mmio_clear_bit(self,
						      FU_PARADE_USBHUB_DEVICE_ADDR_SPI_MASTER,
						      4,
						      error);
}

static gboolean
fu_parade_usbhub_device_spi_wait_status_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuParadeUsbhubDevice *self = FU_PARADE_USBHUB_DEVICE(device);
	guint8 spi_cmd_read_status = 0;
	guint8 val = 0;

	if (!fu_cfi_device_get_cmd(self->cfi_device,
				   FU_CFI_DEVICE_CMD_READ_STATUS,
				   &spi_cmd_read_status,
				   error))
		return FALSE;
	if (!fu_parade_usbhub_device_spi_data_read(self,
						   spi_cmd_read_status,
						   0, /* SPI addr */
						   1,
						   &val,
						   sizeof(val),
						   error))
		return FALSE;
	if (val & 0b1) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "status invalid");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_parade_usbhub_device_spi_wait_status(FuParadeUsbhubDevice *self, GError **error)
{
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_parade_usbhub_device_spi_wait_status_cb,
				    FU_PARADE_USBHUB_DEVICE_SPI_RETRY_COUNT,
				    FU_PARADE_USBHUB_DEVICE_SPI_RETRY_DELAY,
				    NULL,
				    error);
}

static gboolean
fu_parade_usbhub_device_acquire_spi_master(FuParadeUsbhubDevice *self, GError **error)
{
	return fu_parade_usbhub_device_mmio_set_bit(self,
						    FU_PARADE_USBHUB_DEVICE_ADDR_SPI_MASTER_ACQUIRE,
						    7,
						    error);
}

static gboolean
fu_parade_usbhub_device_spi_rom_chip_unprotect(FuParadeUsbhubDevice *self, GError **error)
{
	guint8 status = 0;
	guint8 status_new = 0;
	guint8 buf_spi[2];
	guint8 spi_cmd_write_en = 0;
	guint8 spi_cmd_read_status = 0;
	guint8 spi_cmd_write_status = 0;

	/* read status */
	if (!fu_cfi_device_get_cmd(self->cfi_device,
				   FU_CFI_DEVICE_CMD_READ_STATUS,
				   &spi_cmd_read_status,
				   error))
		return FALSE;
	if (!fu_parade_usbhub_device_spi_data_read(self,
						   spi_cmd_read_status,
						   0, /* SPI addr */
						   1,
						   &status,
						   sizeof(status),
						   error))
		return FALSE;

	if (FU_BIT_IS_CLEAR(status, 2) && FU_BIT_IS_CLEAR(status, 3) && FU_BIT_IS_CLEAR(status, 7))
		return TRUE;

	FU_BIT_CLEAR(status, 2); /* BP0 */
	FU_BIT_CLEAR(status, 3); /* BP1 */
	FU_BIT_CLEAR(status, 7); /* SRWD */

	/* write enable */
	if (!fu_cfi_device_get_cmd(self->cfi_device,
				   FU_CFI_DEVICE_CMD_WRITE_EN,
				   &spi_cmd_write_en,
				   error))
		return FALSE;
	if (!fu_parade_usbhub_device_spi_write_command(self, spi_cmd_write_en, 0, 1, error))
		return FALSE;

	/* write status */
	if (!fu_cfi_device_get_cmd(self->cfi_device,
				   FU_CFI_DEVICE_CMD_WRITE_STATUS,
				   &spi_cmd_write_status,
				   error))
		return FALSE;
	buf_spi[0] = spi_cmd_write_status;
	buf_spi[1] = status;
	if (!fu_parade_usbhub_device_spi_data_write_ex(self, buf_spi, sizeof(buf_spi), error))
		return FALSE;
	if (!fu_parade_usbhub_device_spi_wait_status(self, error))
		return FALSE;

	/* check status */
	if (!fu_parade_usbhub_device_spi_data_read(self,
						   spi_cmd_read_status,
						   0, /* SPI addr */
						   1,
						   &status_new,
						   sizeof(status_new),
						   error))
		return FALSE;
	if (status_new != status) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "status was 0x%x, expected 0x%x",
			    status_new,
			    status);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_parade_usbhub_device_spi_rom_erase_sector(FuParadeUsbhubDevice *self,
					     guint32 spi_address,
					     GError **error)
{
	guint8 spi_cmd_write_en = 0;
	guint8 spi_cmd_sector_erase = 0;

	/* has to be aligned */
	if (spi_address % FU_PARADE_USBHUB_SPI_ROM_ERASE_SIZE != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "SPI address 0x%x not aligned to 0x%x",
			    spi_address,
			    FU_PARADE_USBHUB_SPI_ROM_ERASE_SIZE);
		return FALSE;
	}

	/* write enable */
	if (!fu_cfi_device_get_cmd(self->cfi_device,
				   FU_CFI_DEVICE_CMD_WRITE_EN,
				   &spi_cmd_write_en,
				   error))
		return FALSE;
	if (!fu_parade_usbhub_device_spi_write_command(self, spi_cmd_write_en, 0, 1, error))
		return FALSE;

	/* sector erase */
	if (!fu_cfi_device_get_cmd(self->cfi_device,
				   FU_CFI_DEVICE_CMD_SECTOR_ERASE,
				   &spi_cmd_sector_erase,
				   error))
		return FALSE;
	if (!fu_parade_usbhub_device_spi_write_command(self,
						       spi_cmd_sector_erase,
						       spi_address,
						       4,
						       error))
		return FALSE;

	/* check status */
	return fu_parade_usbhub_device_spi_wait_status(self, error);
}

static gboolean
fu_parade_usbhub_device_spi_rom_erase(FuParadeUsbhubDevice *self,
				      gsize bufsz,
				      FuProgress *progress,
				      GError **error)
{
	g_autoptr(GPtrArray) chunks = NULL;

	chunks = fu_chunk_array_new(NULL,
				    bufsz,
				    self->spi_address,
				    0,
				    FU_PARADE_USBHUB_SPI_ROM_ERASE_SIZE);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_parade_usbhub_device_spi_rom_erase_sector(self,
								  fu_chunk_get_address(chk),
								  error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_parade_usbhub_device_sram_set_page(FuParadeUsbhubDevice *self,
				      guint8 index_of_sram_page,
				      GError **error)
{
	guint8 val = 0;
	if (!fu_parade_usbhub_device_mmio_read_u8(self,
						  FU_PARADE_USBHUB_DEVICE_ADDR_SRAM_PAGE,
						  &val,
						  error))
		return FALSE;
	val &= 0xF0;
	val |= index_of_sram_page;
	return fu_parade_usbhub_device_mmio_write_u8(self,
						     FU_PARADE_USBHUB_DEVICE_ADDR_SRAM_PAGE,
						     val,
						     error);
}

static gboolean
fu_parade_usbhub_device_sram_page_write(FuParadeUsbhubDevice *self,
					guint32 sram_address,
					guint8 *buf,
					gsize bufsz,
					GError **error)
{
	guint page = G_MAXUINT;
	g_autoptr(GPtrArray) chunks = NULL;

	chunks = fu_chunk_array_mutable_new(buf,
					    bufsz,
					    sram_address,
					    0x1000,
					    FU_PARADE_USBHUB_DEVICE_MMIO_BURST_WRITE_MAX);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);

		/* setup sram address */
		if (page != fu_chunk_get_page(chk)) {
			page = fu_chunk_get_page(chk);
			if (!fu_parade_usbhub_device_sram_set_page(self, page, error))
				return FALSE;
		}

		/* write data to sram */
		if (!fu_parade_usbhub_device_mmio_write_raw(self,
							    0x6000 + fu_chunk_get_address(chk),
							    fu_chunk_get_data_out(chk),
							    fu_chunk_get_data_sz(chk),
							    error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_parade_usbhub_device_spi_rom_write_trigger(FuParadeUsbhubDevice *self,
					      guint16 sram_address,
					      guint32 spi_address,
					      gsize dma_size,
					      GError **error)
{
	if (!fu_parade_usbhub_device_mmio_write_u24(self,
						    FU_PARADE_USBHUB_DEVICE_ADDR_SPI_ADDR,
						    spi_address,
						    error))
		return FALSE;
	if (!fu_parade_usbhub_device_mmio_write_u16(self,
						    FU_PARADE_USBHUB_DEVICE_ADDR_SRAM_ADDR,
						    sram_address,
						    error))
		return FALSE;
	if (!fu_parade_usbhub_device_mmio_write_u16(self,
						    FU_PARADE_USBHUB_DEVICE_ADDR_DMA_SIZE,
						    dma_size,
						    error))
		return FALSE;
	return fu_parade_usbhub_device_mmio_write_u8(
	    self,
	    FU_PARADE_USBHUB_DEVICE_ADDR_STATUS,
	    FU_PARADE_USBHUB_DEVICE_STATUS_FLAG_TRIGGER_SPI |
		FU_PARADE_USBHUB_DEVICE_STATUS_FLAG_WRITE,
	    error);
}

static gboolean
fu_parade_usbhub_device_spi_rom_write(FuParadeUsbhubDevice *self,
				      GByteArray *blob,
				      FuProgress *progress,
				      GError **error)
{
	g_autoptr(GPtrArray) chunks = NULL;

	/* disable DBI timeout */
	if (!fu_parade_usbhub_device_mmio_write_u8(self,
						   FU_PARADE_USBHUB_DEVICE_ADDR_DBI_TIMEOUT,
						   0,
						   error))
		return FALSE;

	/* write sram scratch buffer then trigger DMA */
	chunks = fu_chunk_array_mutable_new(blob->data,
					    blob->len,
					    self->spi_address,
					    0, /* page */
					    FU_PARADE_USBHUB_DMA_SRAM_SIZE);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_parade_usbhub_device_sram_page_write(self,
							     FU_PARADE_USBHUB_DMA_SRAM_ADDRESS,
							     fu_chunk_get_data_out(chk),
							     fu_chunk_get_data_sz(chk),
							     error))
			return FALSE;
		if (!fu_parade_usbhub_device_spi_rom_write_trigger(
			self,
			FU_PARADE_USBHUB_DMA_SRAM_ADDRESS,
			fu_chunk_get_address(chk),
			fu_chunk_get_data_sz(chk),
			error))
			return FALSE;
		if (!fu_parade_usbhub_device_spi_rom_wait_done(self, error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* enable DBI timeout */
	return fu_parade_usbhub_device_mmio_write_u8(self,
						     FU_PARADE_USBHUB_DEVICE_ADDR_DBI_TIMEOUT,
						     0x0F,
						     error);
}

static gboolean
fu_parade_usbhub_device_spi_rom_chip_protect(FuParadeUsbhubDevice *self, GError **error)
{
	guint8 status = 0;
	guint8 status_new = 0;
	guint8 buf_spi[2];
	guint8 spi_cmd_write_en = 0;
	guint8 spi_cmd_read_status = 0;
	guint8 spi_cmd_write_status = 0;

	/* read status */
	if (!fu_cfi_device_get_cmd(self->cfi_device,
				   FU_CFI_DEVICE_CMD_READ_STATUS,
				   &spi_cmd_read_status,
				   error))
		return FALSE;
	if (!fu_parade_usbhub_device_spi_data_read(self,
						   spi_cmd_read_status,
						   0,
						   1,
						   &status,
						   sizeof(status),
						   error))
		return FALSE;
	if (FU_BIT_IS_SET(status, 2) && FU_BIT_IS_SET(status, 3) && FU_BIT_IS_CLEAR(status, 7))
		return TRUE;

	FU_BIT_SET(status, 2);	 /* BP0 */
	FU_BIT_SET(status, 3);	 /* BP1 */
	FU_BIT_CLEAR(status, 7); /* SRWD */

	/* write enable */
	if (!fu_cfi_device_get_cmd(self->cfi_device,
				   FU_CFI_DEVICE_CMD_WRITE_EN,
				   &spi_cmd_write_en,
				   error))
		return FALSE;
	if (!fu_parade_usbhub_device_spi_write_command(self, spi_cmd_write_en, 0, 1, error))
		return FALSE;

	/* write status */
	if (!fu_cfi_device_get_cmd(self->cfi_device,
				   FU_CFI_DEVICE_CMD_WRITE_STATUS,
				   &spi_cmd_write_status,
				   error))
		return FALSE;
	buf_spi[0] = spi_cmd_write_status;
	buf_spi[1] = status;
	if (!fu_parade_usbhub_device_spi_data_write_ex(self, buf_spi, sizeof(buf_spi), error))
		return FALSE;

	/* check status */
	if (!fu_parade_usbhub_device_spi_wait_status(self, error))
		return FALSE;
	if (!fu_parade_usbhub_device_spi_data_read(self,
						   spi_cmd_read_status,
						   0,
						   1,
						   &status_new,
						   sizeof(status_new),
						   error))
		return FALSE;
	if (status_new != status) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "status was 0x%x, expected 0x%x",
			    status_new,
			    status);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_parade_usbhub_device_calculate_checksum(FuParadeUsbhubDevice *self,
					   guint32 spi_address,
					   gsize size,
					   GError **error)
{
	if (!fu_parade_usbhub_device_mmio_write_u24(self,
						    FU_PARADE_USBHUB_DEVICE_ADDR_SPI_ADDR,
						    spi_address,
						    error))
		return FALSE;
	if (!fu_parade_usbhub_device_mmio_write_u16(self,
						    FU_PARADE_USBHUB_DEVICE_ADDR_DMA_SIZE,
						    size,
						    error))
		return FALSE;
	if (!fu_parade_usbhub_device_mmio_write_u8(
		self,
		FU_PARADE_USBHUB_DEVICE_ADDR_STATUS,
		FU_PARADE_USBHUB_DEVICE_STATUS_FLAG_CHECKSUM |
		    FU_PARADE_USBHUB_DEVICE_STATUS_FLAG_TRIGGER_SPI,
		error))
		return FALSE;
	return fu_parade_usbhub_device_spi_rom_wait_done(self, error);
}

static gboolean
fu_parade_usbhub_device_spi_rom_checksum(FuParadeUsbhubDevice *self,
					 gsize size,
					 guint32 *checksum,
					 GError **error)
{
	guint8 buf_csum[4] = {0};
	g_autoptr(GPtrArray) chunks = NULL;

	/* acquire and enable SPI master after internal reset */
	if (!fu_parade_usbhub_device_acquire_spi_master(self, error))
		return FALSE;
	if (!fu_parade_usbhub_device_enable_spi_master(self, error))
		return FALSE;

	/* calculate checksum internally */
	chunks = fu_chunk_array_new(NULL,
				    size,
				    self->spi_address,
				    0x0,
				    FU_PARADE_USBHUB_SPI_ROM_CHECKSUM_BUFFER_SIZE);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_parade_usbhub_device_calculate_checksum(self,
								fu_chunk_get_address(chk),
								fu_chunk_get_data_sz(chk),
								error))
			return FALSE;
	}

	/* read calculated checksum */
	if (!fu_parade_usbhub_device_mmio_read(self,
					       FU_PARADE_USBHUB_DEVICE_ADDR_DATA,
					       buf_csum,
					       sizeof(buf_csum),
					       error))
		return FALSE;
	*checksum = fu_memread_uint32(buf_csum, G_LITTLE_ENDIAN);

	/* success */
	return TRUE;
}

static gboolean
fu_parade_usbhub_device_set_ufp_disconnect_flag(FuParadeUsbhubDevice *self, GError **error)
{
	guint8 val = 0;
	if (!fu_parade_usbhub_device_mmio_read_u8(self,
						  FU_PARADE_USBHUB_DEVICE_ADDR_UFP_DISCONNECT,
						  &val,
						  error))
		return FALSE;
	val &= 0x0F;
	val |= 0xB0;
	return fu_parade_usbhub_device_mmio_write_u8(self,
						     FU_PARADE_USBHUB_DEVICE_ADDR_UFP_DISCONNECT,
						     val,
						     error);
}

static gboolean
fu_parade_usbhub_device_ensure_version(FuParadeUsbhubDevice *self, GError **error)
{
	guint8 buf[4] = {0};

	if (!fu_parade_usbhub_device_mmio_read_u8(self,
						  FU_PARADE_USBHUB_DEVICE_ADDR_VERSION_A,
						  buf + 0,
						  error))
		return FALSE;
	if (!fu_parade_usbhub_device_mmio_read_u8(self,
						  FU_PARADE_USBHUB_DEVICE_ADDR_VERSION_B,
						  buf + 1,
						  error))
		return FALSE;
	if (!fu_parade_usbhub_device_mmio_read_u8(self,
						  FU_PARADE_USBHUB_DEVICE_ADDR_VERSION_C,
						  buf + 2,
						  error))
		return FALSE;
	if (!fu_parade_usbhub_device_mmio_read_u8(self,
						  FU_PARADE_USBHUB_DEVICE_ADDR_VERSION_D,
						  buf + 3,
						  error))
		return FALSE;
	fu_device_set_version_raw(FU_DEVICE(self), fu_memread_uint32(buf, G_LITTLE_ENDIAN));

	/* success */
	return TRUE;
}

static gboolean
fu_parade_usbhub_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuParadeUsbhubDevice *self = FU_PARADE_USBHUB_DEVICE(device);

	if (!fu_parade_usbhub_device_acquire_spi_master(self, error)) {
		g_prefix_error(error, "failed to acquire SPI master: ");
		return FALSE;
	}
	if (!fu_parade_usbhub_device_enable_spi_master(self, error)) {
		g_prefix_error(error, "failed to enable SPI master: ");
		return FALSE;
	}
	if (!fu_parade_usbhub_device_spi_rom_chip_unprotect(self, error)) {
		g_prefix_error(error, "failed to unprotect SPI ROM: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_parade_usbhub_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuParadeUsbhubDevice *self = FU_PARADE_USBHUB_DEVICE(device);

	if (!fu_parade_usbhub_device_spi_rom_chip_protect(self, error)) {
		g_prefix_error(error, "failed to protect SPI ROM: ");
		return FALSE;
	}
	if (!fu_parade_usbhub_device_disable_spi_master(self, error)) {
		g_prefix_error(error, "failed to disable SPI master: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_parade_usbhub_device_setup(FuDevice *device, GError **error)
{
	FuParadeUsbhubDevice *self = FU_PARADE_USBHUB_DEVICE(device);

	/* HidDevice->setup */
	if (!FU_DEVICE_CLASS(fu_parade_usbhub_device_parent_class)->setup(device, error))
		return FALSE;

	/* get the version from the hardware */
	if (!fu_parade_usbhub_device_ensure_version(self, error)) {
		g_prefix_error(error, "failed to get version: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_parade_usbhub_device_prepare(FuDevice *device,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	FuParadeUsbhubDevice *self = FU_PARADE_USBHUB_DEVICE(device);

	/* prevent staying in high-power charging mode if UFP is disconnected */
	if (!fu_parade_usbhub_device_set_ufp_disconnect_flag(self, error)) {
		g_prefix_error(error, "failed to set UFP disconnect flag: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_parade_usbhub_device_cleanup(FuDevice *device,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	g_autoptr(FwupdRequest) request = fwupd_request_new();

	fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
	fwupd_request_set_id(request, FWUPD_REQUEST_ID_REMOVE_REPLUG);
	fwupd_request_add_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	if (!fu_device_emit_request(device, request, progress, error))
		return FALSE;
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static FuFirmware *
fu_parade_usbhub_device_prepare_firmware(FuDevice *device,
					 GInputStream *stream,
					 FuProgress *progress,
					 FwupdInstallFlags flags,
					 GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_parade_usbhub_firmware_new();
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	return g_steal_pointer(&firmware);
}

static gboolean
fu_parade_usbhub_device_write_firmware(FuDevice *device,
				       FuFirmware *firmware,
				       FuProgress *progress,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuParadeUsbhubDevice *self = FU_PARADE_USBHUB_DEVICE(device);
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GByteArray) blob = NULL;
	guint32 checksum;
	guint32 checksum_new;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 33, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 66, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 1, NULL);

	/* get default image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	/* get bank 4 slice */
	blob = fu_input_stream_read_byte_array(stream,
					       self->spi_address,
					       FU_PARADE_USBHUB_SPI_ROM_BANK_SIZE,
					       error);
	if (blob == NULL)
		return FALSE;

	/* SPI ROM update */
	if (!fu_parade_usbhub_device_spi_rom_erase(self,
						   blob->len,
						   fu_progress_get_child(progress),
						   error))
		return FALSE;
	fu_progress_step_done(progress);
	if (!fu_parade_usbhub_device_spi_rom_write(self,
						   blob,
						   fu_progress_get_child(progress),
						   error))
		return FALSE;
	fu_progress_step_done(progress);

	/* compare checksum */
	if (!fu_parade_usbhub_device_spi_rom_checksum(self, blob->len, &checksum_new, error)) {
		g_prefix_error(error, "failed to get ROM checksum: ");
		return FALSE;
	}
	checksum = fu_crc32(FU_CRC_KIND_B32_MPEG2, blob->data, blob->len);
	if (checksum != checksum_new) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "checksum was 0x%x, expected 0x%x",
			    checksum_new,
			    checksum);
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static void
fu_parade_usbhub_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 96, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gchar *
fu_parade_usbhub_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static void
fu_parade_usbhub_device_init(FuParadeUsbhubDevice *self)
{
	self->spi_address = FU_PARADE_USBHUB_SPI_ROM_ADDRESS_BANK4_HUB_FIRMWARE_1;
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_USER_REPLUG);
	fu_device_set_firmware_size(FU_DEVICE(self), FU_PARADE_USBHUB_SPI_ROM_SIZE);
	fu_device_add_protocol(FU_DEVICE(self), "com.paradetech.usbhub");
	fu_device_add_icon(FU_DEVICE(self), "usb-hub");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_add_request_flag(FU_DEVICE(self), FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), 0);
}

static void
fu_parade_usbhub_device_constructed(GObject *object)
{
	FuParadeUsbhubDevice *self = FU_PARADE_USBHUB_DEVICE(object);
	self->cfi_device = fu_cfi_device_new(fu_device_get_context(FU_DEVICE(self)), NULL);
}

static void
fu_parade_usbhub_device_finalize(GObject *object)
{
	FuParadeUsbhubDevice *self = FU_PARADE_USBHUB_DEVICE(object);

	if (self->cfi_device != NULL)
		g_object_unref(self->cfi_device);

	G_OBJECT_CLASS(fu_parade_usbhub_device_parent_class)->finalize(object);
}

static void
fu_parade_usbhub_device_class_init(FuParadeUsbhubDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->constructed = fu_parade_usbhub_device_constructed;
	object_class->finalize = fu_parade_usbhub_device_finalize;
	device_class->to_string = fu_parade_usbhub_device_to_string;
	device_class->setup = fu_parade_usbhub_device_setup;
	device_class->prepare = fu_parade_usbhub_device_prepare;
	device_class->cleanup = fu_parade_usbhub_device_cleanup;
	device_class->attach = fu_parade_usbhub_device_attach;
	device_class->detach = fu_parade_usbhub_device_detach;
	device_class->prepare_firmware = fu_parade_usbhub_device_prepare_firmware;
	device_class->write_firmware = fu_parade_usbhub_device_write_firmware;
	device_class->set_progress = fu_parade_usbhub_device_set_progress;
	device_class->convert_version = fu_parade_usbhub_device_convert_version;
}
