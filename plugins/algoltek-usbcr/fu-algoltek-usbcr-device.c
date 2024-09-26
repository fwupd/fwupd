/*
 * Copyright 2024 Algoltek, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-algoltek-usbcr-common.h"
#include "fu-algoltek-usbcr-device.h"
#include "fu-algoltek-usbcr-firmware.h"
#include "fu-algoltek-usbcr-struct.h"

struct _FuAlgoltekUsbcrDevice {
	FuBlockDevice parent_instance;
};

G_DEFINE_TYPE(FuAlgoltekUsbcrDevice, fu_algoltek_usbcr_device, FU_TYPE_BLOCK_DEVICE)

typedef struct {
	guint16 reg;
	guint8 val;
} FuAgUsbcrRegSetup;

static GByteArray *
fu_algoltek_usbcr_device_cmd_get_ver(FuAlgoltekUsbcrDevice *self, GError **error)
{
	guint8 cdb[FU_AG_USBCR_MAX_CDB_LEN] = {0};
	g_autoptr(GByteArray) buf = g_byte_array_new();

	cdb[0] = FU_AG_USBCR_SCSIOP_VENDOR_FIRMWARE_REVISION;
	fu_byte_array_set_size(buf, FU_AG_USBCR_MAX_BUFFER_SIZE, 0x0);
	if (!fu_block_device_sg_io_cmd_read(FU_BLOCK_DEVICE(self),
					    cdb,
					    sizeof(cdb),
					    buf->data,
					    buf->len,
					    error))
		return NULL;

	return g_steal_pointer(&buf);
}

static gboolean
fu_algoltek_usbcr_device_write_reg(FuAlgoltekUsbcrDevice *self,
				   guint16 addr,
				   guint8 value,
				   guint8 ram_dest,
				   GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_ag_usbcr_reg_cdb_new();

	fu_struct_ag_usbcr_reg_cdb_set_cmd(st, FU_AG_USBCR_SCSIOP_VENDOR_GENERIC_CMD);
	fu_struct_ag_usbcr_reg_cdb_set_subcmd(st, FU_AG_USBCR_RD_WR_RAM);
	fu_struct_ag_usbcr_reg_cdb_set_ramdest(st, ram_dest);
	fu_struct_ag_usbcr_reg_cdb_set_addr(st, addr);
	fu_struct_ag_usbcr_reg_cdb_set_val(st, value);

	return fu_block_device_sg_io_cmd_none(FU_BLOCK_DEVICE(self), st->data, st->len, error);
}

static gboolean
fu_algoltek_usbcr_device_read_reg(FuAlgoltekUsbcrDevice *self,
				  guint16 addr,
				  guint8 *buf,
				  guint16 bufsz,
				  guint8 ram_dest,
				  GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_ag_usbcr_reg_cdb_new();

	fu_struct_ag_usbcr_reg_cdb_set_cmd(st, FU_AG_USBCR_SCSIOP_VENDOR_GENERIC_CMD);
	fu_struct_ag_usbcr_reg_cdb_set_subcmd(st, FU_AG_USBCR_RD_WR_RAM);
	fu_struct_ag_usbcr_reg_cdb_set_ramdest(st, ram_dest);
	fu_struct_ag_usbcr_reg_cdb_set_addr(st, addr);

	return fu_block_device_sg_io_cmd_read(FU_BLOCK_DEVICE(self),
					      st->data,
					      st->len,
					      buf,
					      bufsz,
					      error);
}

static gboolean
fu_algoltek_usbcr_device_send_spi_cmd(FuAlgoltekUsbcrDevice *self, guint8 cmd, GError **error)
{
	guint8 buf[8] = {0};
	g_autoptr(GByteArray) st = fu_struct_ag_usbcr_spi_cdb_new();

	fu_struct_ag_usbcr_spi_cdb_set_cmd(st, FU_AG_USBCR_SCSIOP_VENDOR_EEPROM_WR);
	fu_struct_ag_usbcr_spi_cdb_set_addr(st, 0xFFFF);
	fu_struct_ag_usbcr_spi_cdb_set_bufsz(st, sizeof(buf) & 0xFF);
	fu_struct_ag_usbcr_spi_cdb_set_tag(st, FU_AG_SPECIFY_EEPROM_TYPE_TAG);
	fu_struct_ag_usbcr_spi_cdb_set_valid(st, FU_AG_SPIFLASH_VALID);
	fu_struct_ag_usbcr_spi_cdb_set_spisig1(st, FU_AG_SPECIFY_SPI_CMD_SIG_1);
	fu_struct_ag_usbcr_spi_cdb_set_spisig2(st, FU_AG_SPECIFY_SPI_CMD_SIG_2);
	fu_struct_ag_usbcr_spi_cdb_set_spicmd(st, cmd);

	return fu_block_device_sg_io_cmd_write(FU_BLOCK_DEVICE(self),
					       st->data,
					       st->len,
					       buf,
					       sizeof(buf),
					       error);
}

static gboolean
fu_algoltek_usbcr_device_do_write_spi(FuAlgoltekUsbcrDevice *self,
				      guint16 addr,
				      const guint8 *buf,
				      guint8 bufsz,
				      guint8 access_sz,
				      GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_ag_usbcr_spi_cdb_new();

	if (!fu_algoltek_usbcr_device_send_spi_cmd(self, FU_AG_USBCR_WREN, error))
		return FALSE;

	fu_struct_ag_usbcr_spi_cdb_set_cmd(st, FU_AG_USBCR_SCSIOP_VENDOR_EEPROM_WR);
	fu_struct_ag_usbcr_spi_cdb_set_addr(st, addr);
	fu_struct_ag_usbcr_spi_cdb_set_bufsz(st, access_sz);
	fu_struct_ag_usbcr_spi_cdb_set_tag(st, FU_AG_SPECIFY_EEPROM_TYPE_TAG);
	fu_struct_ag_usbcr_spi_cdb_set_valid(st, FU_AG_SPIFLASH_VALID);

	return fu_block_device_sg_io_cmd_write(FU_BLOCK_DEVICE(self),
					       st->data,
					       st->len,
					       buf,
					       bufsz,
					       error);
}

static gboolean
fu_algoltek_usbcr_device_do_read_spi(FuAlgoltekUsbcrDevice *self,
				     guint16 addr,
				     guint8 *buf,
				     guint8 bufsz,
				     GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_ag_usbcr_spi_cdb_new();

	fu_struct_ag_usbcr_spi_cdb_set_cmd(st, FU_AG_USBCR_SCSIOP_VENDOR_EEPROM_RD);
	fu_struct_ag_usbcr_spi_cdb_set_addr(st, addr);
	fu_struct_ag_usbcr_spi_cdb_set_bufsz(st, bufsz);
	fu_struct_ag_usbcr_spi_cdb_set_tag(st, FU_AG_SPECIFY_EEPROM_TYPE_TAG);
	fu_struct_ag_usbcr_spi_cdb_set_valid(st, FU_AG_SPIFLASH_VALID);

	return fu_block_device_sg_io_cmd_read(FU_BLOCK_DEVICE(self),
					      st->data,
					      st->len,
					      buf,
					      bufsz,
					      error);
}

static gboolean
fu_algoltek_usbcr_device_verify_reg_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuAlgoltekUsbcrDevice *self = FU_ALGOLTEK_USBCR_DEVICE(device);
	guint8 *buf = (guint8 *)user_data;

	if (!fu_algoltek_usbcr_device_read_reg(self, 0xC8, buf, 1, FU_AG_USBCR_RD_WR_XDATA, error))
		return FALSE;
	if ((buf[0] & 0x01) != 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "not ready");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_algoltek_usbcr_device_check_status_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuAlgoltekUsbcrDevice *self = FU_ALGOLTEK_USBCR_DEVICE(device);
	guint8 *buf = (guint8 *)user_data;
	FuAgUsbcrRegSetup regs[5] = {
	    {0x400, FU_AG_USBCR_RDSR},
	    {0xC9, 0x01},
	    {0xC4, 0x01},
	    {0xC7, 0x00},
	    {0xC8, 0x07},
	};

	for (guint j = 0; j < G_N_ELEMENTS(regs); j++) {
		if (!fu_algoltek_usbcr_device_write_reg(self,
							regs[j].reg,
							regs[j].val,
							FU_AG_USBCR_RD_WR_XDATA,
							error))
			return FALSE;
	}
	buf[0] = 0;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_algoltek_usbcr_device_verify_reg_cb,
				  5,
				  0,
				  buf,
				  error))
		return FALSE;

	buf[0] = 0;
	if (!fu_algoltek_usbcr_device_read_reg(self, 0x400, buf, 2, FU_AG_USBCR_RD_WR_XDATA, error))
		return FALSE;
	if ((buf[0] & 0x01) != 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "not ready");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_algoltek_usbcr_device_command_wren(FuAlgoltekUsbcrDevice *self, GError **error)
{
	guint8 buf[1] = {0};
	FuAgUsbcrRegSetup regs[] = {
	    {0xC8, 0x04},
	    {0xCA, 0x01},
	    {0x400, FU_AG_USBCR_WREN},
	    {0xC9, 0x01},
	    {0xC8, 0x05},
	};

	for (guint i = 0; i < G_N_ELEMENTS(regs); i++) {
		if (!fu_algoltek_usbcr_device_write_reg(self,
							regs[i].reg,
							regs[i].val,
							FU_AG_USBCR_RD_WR_XDATA,
							error))
			return FALSE;
	}
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_algoltek_usbcr_device_verify_reg_cb,
				  5,
				  0,
				  buf,
				  error))
		return FALSE;

	/* success*/
	return TRUE;
}

static gboolean
fu_algoltek_usbcr_device_command_wrsr(FuAlgoltekUsbcrDevice *self, gboolean en, GError **error)
{
	guint8 buf[1] = {0};
	FuAgUsbcrRegSetup regs[] = {
	    {0xC8, 0x04},
	    {0xCA, 0x01},
	    {0x400, FU_AG_USBCR_WRSR},
	    {0x401, 0x00},
	    {0xC9, 0x02},
	    {0xC8, 0x05},
	};

	if (en == FU_AG_BLOCK_MODE_EN)
		regs[3].val = 0x0C;
	for (guint i = 0; i < G_N_ELEMENTS(regs); i++) {
		if (!fu_algoltek_usbcr_device_write_reg(self,
							regs[i].reg,
							regs[i].val,
							FU_AG_USBCR_RD_WR_XDATA,
							error))
			return FALSE;
	}
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_algoltek_usbcr_device_verify_reg_cb,
				  5,
				  0,
				  buf,
				  error))
		return FALSE;

	/* success*/
	return TRUE;
}

static gboolean
fu_algoltek_usbcr_device_command_rdsr(FuAlgoltekUsbcrDevice *self, GError **error)
{
	guint8 buf[1] = {0};
	FuAgUsbcrRegSetup regs[] = {
	    {0xC8, 0x04},
	    {0xCA, 0x01},
	    {0x400, FU_AG_USBCR_RDSR},
	    {0xC9, 0x01},
	    {0xC4, 0x01},
	    {0xC7, 0x00},
	    {0xC8, 0x07},
	};

	for (guint i = 0; i < G_N_ELEMENTS(regs); i++) {
		if (!fu_algoltek_usbcr_device_write_reg(self,
							regs[i].reg,
							regs[i].val,
							FU_AG_USBCR_RD_WR_XDATA,
							error))
			return FALSE;
	}
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_algoltek_usbcr_device_verify_reg_cb,
				  5,
				  0,
				  buf,
				  error))
		return FALSE;

	/* success*/
	return TRUE;
}

static gboolean
fu_algoltek_usbcr_device_spi_flash_block_mode_cb(FuDevice *device,
						 gpointer user_data,
						 GError **error)
{
	FuAlgoltekUsbcrDevice *self = FU_ALGOLTEK_USBCR_DEVICE(device);
	guint8 buf[2] = {0};
	guint8 en = GPOINTER_TO_INT(user_data);

	/* set command wren */
	if (!fu_algoltek_usbcr_device_command_wren(self, error))
		return FALSE;

	/* set command wrsr */
	if (!fu_algoltek_usbcr_device_command_wrsr(self, en, error))
		return FALSE;

	/* set command rdsr */
	if (!fu_algoltek_usbcr_device_command_rdsr(self, error))
		return FALSE;

	/* read data */
	if (!fu_algoltek_usbcr_device_read_reg(self,
					       0x400,
					       buf,
					       sizeof(buf),
					       FU_AG_USBCR_RD_WR_XDATA,
					       error))
		return FALSE;
	if (en == FU_AG_BLOCK_MODE_DISEN) {
		if ((buf[0] & 0xC) != 0x0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "return value is 0x%x while expecting value is 0x0",
				    (guint)(buf[0] & 0xC));
			return FALSE;
		}
	} else {
		if ((buf[0] & 0xC) != 0xC) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "return value is 0x%x while expecting value is 0xC",
				    (guint)(buf[0] & 0xC));
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_algoltek_usbcr_device_set_clear_soft_reset_flag(FuAlgoltekUsbcrDevice *self,
						   guint8 val,
						   GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_ag_usbcr_reset_cdb_new();

	fu_struct_ag_usbcr_reset_cdb_set_cmd(st, FU_AG_USBCR_SCSIOP_VENDOR_GENERIC_CMD);
	fu_struct_ag_usbcr_reset_cdb_set_subcmd(st, 0x96);
	fu_struct_ag_usbcr_reset_cdb_set_val(st, 0x78);
	fu_struct_ag_usbcr_reset_cdb_set_val2(st, val);

	return fu_block_device_sg_io_cmd_none(FU_BLOCK_DEVICE(self), st->data, st->len, error);
}

static gboolean
fu_algoltek_usbcr_device_reset_chip(FuAlgoltekUsbcrDevice *self, GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_ag_usbcr_reset_cdb_new();

	fu_struct_ag_usbcr_reset_cdb_set_cmd(st, FU_AG_USBCR_SCSIOP_VENDOR_GENERIC_CMD);
	fu_struct_ag_usbcr_reset_cdb_set_subcmd(st, 0x95);
	fu_struct_ag_usbcr_reset_cdb_set_val(st, 0x23);

	return fu_block_device_sg_io_cmd_none(FU_BLOCK_DEVICE(self), st->data, st->len, error);
}

static gboolean
fu_algoltek_usbcr_device_ensure_version(FuAlgoltekUsbcrDevice *self, GError **error)
{
	guint16 app_ver = 0;
	guint16 boot_ver = 0;
	g_autoptr(GByteArray) ver_array = NULL;

	ver_array = fu_algoltek_usbcr_device_cmd_get_ver(self, error);
	if (ver_array == NULL) {
		g_prefix_error(error, "failed to read version: ");
		return FALSE;
	}
	if (!fu_memread_uint16_safe(ver_array->data,
				    ver_array->len,
				    130,
				    &app_ver,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	fu_device_set_version_raw(FU_DEVICE(self), app_ver);
	if (!fu_memread_uint16_safe(ver_array->data,
				    ver_array->len,
				    132,
				    &boot_ver,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	fu_device_set_version_bootloader_raw(FU_DEVICE(self), boot_ver);
	return TRUE;
}

static gboolean
fu_algoltek_usbcr_device_probe(FuDevice *device, GError **error)
{
	FuAlgoltekUsbcrDevice *self = FU_ALGOLTEK_USBCR_DEVICE(device);

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_algoltek_usbcr_device_parent_class)->probe(device, error))
		return FALSE;

	if (g_strcmp0(fu_udev_device_get_devtype(FU_UDEV_DEVICE(self)), "disk") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "is not correct devtype=%s, expected disk",
			    fu_udev_device_get_devtype(FU_UDEV_DEVICE(self)));
		return FALSE;
	}

	/* set the physical ID */
	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "usb", error);
}

static gboolean
fu_algoltek_usbcr_device_setup(FuDevice *device, GError **error)
{
	FuAlgoltekUsbcrDevice *self = FU_ALGOLTEK_USBCR_DEVICE(device);

	if (!fu_algoltek_usbcr_device_ensure_version(self, error))
		return FALSE;
	fu_device_build_vendor_id_u16(device, "BLOCK", fu_device_get_vid(device));

	/* success */
	return TRUE;
}

static FuFirmware *
fu_algoltek_usbcr_device_prepare_firmware(FuDevice *device,
					  GInputStream *stream,
					  FuProgress *progress,
					  FwupdInstallFlags flags,
					  GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_algoltek_usbcr_firmware_new();

	/* validate compatibility */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	if (fu_algoltek_usbcr_firmware_get_boot_ver(FU_ALGOLTEK_USBCR_FIRMWARE(firmware)) !=
	    fu_device_get_version_bootloader_raw(device)) {
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_FILE,
		    "firmware boot version is 0x%x while expecting value is 0x%x",
		    fu_algoltek_usbcr_firmware_get_boot_ver(FU_ALGOLTEK_USBCR_FIRMWARE(firmware)),
		    (guint)fu_device_get_version_bootloader_raw(device));
		return NULL;
	}

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_algoltek_usbcr_device_write_chunk(FuAlgoltekUsbcrDevice *self, FuChunk *chk, GError **error)
{
	guint8 back_data[8] = {0};

	if (!fu_algoltek_usbcr_device_do_write_spi(self,
						   fu_chunk_get_address(chk),
						   fu_chunk_get_data(chk),
						   fu_chunk_get_data_sz(chk),
						   fu_chunk_get_data_sz(chk),
						   error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_algoltek_usbcr_device_check_status_cb,
				  5,
				  0,
				  back_data,
				  error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_algoltek_usbcr_device_write_chunks(FuAlgoltekUsbcrDevice *self,
				      FuChunkArray *chunks,
				      FuProgress *progress,
				      GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	for (gint i = fu_chunk_array_length(chunks) - 1; i >= 0; i--) {
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_algoltek_usbcr_device_write_chunk(self, chk, error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_algoltek_usbcr_device_verify_chunks(FuAlgoltekUsbcrDevice *self,
				       FuChunkArray *chunks,
				       FuProgress *progress,
				       GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autofree guint8 *buf = NULL;
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		buf = g_malloc0(fu_chunk_get_data_sz(chk));
		if (!fu_algoltek_usbcr_device_do_read_spi(self,
							  fu_chunk_get_address(chk),
							  buf,
							  fu_chunk_get_data_sz(chk),
							  error))
			return FALSE;
		if (!fu_memcmp_safe(fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    0,
				    buf,
				    fu_chunk_get_data_sz(chk),
				    0x0,
				    fu_chunk_get_data_sz(chk),
				    error))
			return FALSE;

		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_algoltek_usbcr_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuAlgoltekUsbcrDevice *self = FU_ALGOLTEK_USBCR_DEVICE(device);
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 4, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 48, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 48, NULL);

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_algoltek_usbcr_device_spi_flash_block_mode_cb,
				  5,
				  0,
				  GINT_TO_POINTER(FU_AG_BLOCK_MODE_DISEN),
				  error))
		return FALSE;

	if (!fu_algoltek_usbcr_device_send_spi_cmd(self, FU_AG_USBCR_WREN, error))
		return FALSE;
	if (!fu_algoltek_usbcr_device_send_spi_cmd(self, FU_AG_USBCR_ERASE, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* get default image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	chunks = fu_chunk_array_new_from_stream(stream, 0, 32, error);
	if (chunks == NULL)
		return FALSE;

	/* write */
	if (!fu_algoltek_usbcr_device_write_chunks(self,
						   chunks,
						   fu_progress_get_child(progress),
						   error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify */
	if (!fu_algoltek_usbcr_device_verify_chunks(self,
						    chunks,
						    fu_progress_get_child(progress),
						    error))
		return FALSE;
	fu_progress_step_done(progress);
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_algoltek_usbcr_device_spi_flash_block_mode_cb,
				  5,
				  0,
				  GINT_TO_POINTER(FU_AG_BLOCK_MODE_EN),
				  error))
		return FALSE;

	/* reset */
	if (!fu_algoltek_usbcr_device_set_clear_soft_reset_flag(self, 0xAF, error))
		return FALSE;
	if (!fu_algoltek_usbcr_device_reset_chip(self, error))
		return FALSE;

	/* success! */
	return TRUE;
}

static void
fu_algoltek_usbcr_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gchar *
fu_algoltek_usbcr_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return g_strdup_printf("%x", (guint)fu_device_get_version_raw(device));
}

static void
fu_algoltek_usbcr_device_init(FuAlgoltekUsbcrDevice *self)
{
	fu_device_set_vendor(FU_DEVICE(self), "Algoltek");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_device_add_protocol(FU_DEVICE(self), "com.algoltek.usbcr");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_SYNC);
}

static void
fu_algoltek_usbcr_device_class_init(FuAlgoltekUsbcrDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_algoltek_usbcr_device_probe;
	device_class->setup = fu_algoltek_usbcr_device_setup;
	device_class->prepare_firmware = fu_algoltek_usbcr_device_prepare_firmware;
	device_class->write_firmware = fu_algoltek_usbcr_device_write_firmware;
	device_class->set_progress = fu_algoltek_usbcr_device_set_progress;
	device_class->convert_version = fu_algoltek_usbcr_device_convert_version;
}
