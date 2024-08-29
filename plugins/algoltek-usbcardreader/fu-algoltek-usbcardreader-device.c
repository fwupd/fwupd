/*
 * Copyright 2024 Algoltek, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#define HAVE_SCSI_SG_H
#ifdef HAVE_SCSI_SG_H
#include <scsi/sg.h>
#endif

#include "fu-algoltek-usbcardreader-common.h"
#include "fu-algoltek-usbcardreader-device.h"
#include "fu-algoltek-usbcardreader-firmware.h"
#include "fu-algoltek-usbcardreader-struct.h"

struct _FuAlgoltekUsbcardreaderDevice {
	FuUdevDevice parent_instance;
	guint16 app_ver;
	guint16 boot_ver;
};

G_DEFINE_TYPE(FuAlgoltekUsbcardreaderDevice, fu_algoltek_usbcardreader_device, FU_TYPE_UDEV_DEVICE)

typedef struct {
	guint16 reg;
	guint8 val;
} FuAgUsbcardreaderRegSetup;

static gboolean
fu_algoltek_usbcardreader_device_cmd_in(FuAlgoltekUsbcardreaderDevice *self,
				const guint8 *cdb,
				gsize cdbsz,
				guint8 *buf,
				gsize bufsz,
				GError **error)
{
#ifdef HAVE_SCSI_SG_H
	guint8 sense_buffer[FU_AG_USBCARDREADER_SENSE_BUFFER_SIZE] = {0};
	struct sg_io_hdr io_hdr = {
	    .interface_id = 'S',
	    .cmd_len = cdbsz,
	    .mx_sb_len = sizeof(sense_buffer),
	    .dxfer_direction = SG_DXFER_FROM_DEV,
	    .dxfer_len = bufsz,
	    .dxferp = buf,
	    .cmdp = (guint8 *)cdb,
	    .sbp = sense_buffer,
	    .timeout = FU_AG_USBCARDREADER_IOCTL_TIMEOUT_MS,
	    .flags = SG_FLAG_DIRECT_IO,
	};
	gint rc = 0;

	fu_dump_raw(G_LOG_DOMAIN, "cmd", cdb, cdbsz);
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  SG_IO,
				  (guint8 *)&io_hdr,
				  sizeof(io_hdr),
				  &rc,
				  5 * FU_AG_USBCARDREADER_IOCTL_TIMEOUT_MS,
				  FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
				  error))
		return FALSE;
	if (io_hdr.status) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "command fail with status %x, senseKey 0x%02x, asc 0x%02x, ascq 0x%02x",
			    io_hdr.status,
			    sense_buffer[2],
			    sense_buffer[12],
			    sense_buffer[13]);
		return FALSE;
	}

	if (bufsz > 0)
		fu_dump_raw(G_LOG_DOMAIN, "cmd data", buf, bufsz);

	/* success */
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unsupported as scsi/sg.h not found");
	return FALSE;
#endif
}

static gboolean
fu_algoltek_usbcardreader_device_cmd_out(FuAlgoltekUsbcardreaderDevice *self,
				 const guint8 *cdb,
				 gsize cdbsz,
				 const guint8 *buf,
				 gsize bufsz,
				 GError **error)
{
#ifdef HAVE_SCSI_SG_H
	guint8 sense_buffer[FU_AG_USBCARDREADER_SENSE_BUFFER_SIZE] = {0};
	struct sg_io_hdr io_hdr = {
	    .interface_id = 'S',
	    .cmd_len = cdbsz,
	    .mx_sb_len = sizeof(sense_buffer),
	    .dxfer_direction = SG_DXFER_TO_DEV,
	    .dxfer_len = bufsz,
	    .dxferp = (guint8 *)buf,
	    .cmdp = (guint8 *)cdb,
	    .sbp = sense_buffer,
	    .timeout = FU_AG_USBCARDREADER_IOCTL_TIMEOUT_MS,
	    .flags = SG_FLAG_DIRECT_IO,
	};
	gint rc = 0;

	fu_dump_raw(G_LOG_DOMAIN, "cmd", cdb, cdbsz);
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  SG_IO,
				  (guint8 *)&io_hdr,
				  sizeof(io_hdr),
				  &rc,
				  5 * FU_AG_USBCARDREADER_IOCTL_TIMEOUT_MS,
				  FU_UDEV_DEVICE_IOCTL_FLAG_RETRY,
				  error))
		return FALSE;
	if (io_hdr.status) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "command fail with status %x, senseKey 0x%02x, asc 0x%02x, ascq 0x%02x",
			    io_hdr.status,
			    sense_buffer[2],
			    sense_buffer[12],
			    sense_buffer[13]);
		return FALSE;
	}

	if (bufsz > 0)
		fu_dump_raw(G_LOG_DOMAIN, "cmd data", buf, bufsz);

	/* success */
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unsupported as scsi/sg.h not found");
	return FALSE;
#endif
}

static GByteArray *
fu_algoltek_usbcardreader_device_cmd_get_ver(FuAlgoltekUsbcardreaderDevice *self, GError **error)
{
	guint8 cdb[FU_AG_USBCARDREADER_MAX_CDB_LEN] = {0};
	g_autoptr(GByteArray) buf = g_byte_array_new();

	cdb[0] = FU_AG_USBCARDREADER_SCSIOP_VENDOR_FIRMWARE_REVISION;
	fu_byte_array_set_size(buf, FU_AG_USBCARDREADER_MAX_BUFFER_SIZE, 0x0);
	if (!fu_algoltek_usbcardreader_device_cmd_in(self, cdb, sizeof(cdb), buf->data, buf->len, error))
		return NULL;

	return g_steal_pointer(&buf);
}

static gboolean
fu_algoltek_usbcardreader_device_write_reg(FuAlgoltekUsbcardreaderDevice *self, guint16 addr, guint8 value, guint8 ram_dest, GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_ag_usbcardreader_reg_cdb_new();

	fu_struct_ag_usbcardreader_reg_cdb_set_cmd(st, FU_AG_USBCARDREADER_SCSIOP_VENDOR_GENERIC_CMD);
	fu_struct_ag_usbcardreader_reg_cdb_set_subcmd(st, FU_AG_USBCARDREADER_RD_WR_RAM);
	fu_struct_ag_usbcardreader_reg_cdb_set_ramdest(st, ram_dest);
	fu_struct_ag_usbcardreader_reg_cdb_set_addr(st, addr);
	fu_struct_ag_usbcardreader_reg_cdb_set_val(st, value);

	if (!fu_algoltek_usbcardreader_device_cmd_out(self, st->data, st->len, NULL, 0, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_algoltek_usbcardreader_device_read_reg(FuAlgoltekUsbcardreaderDevice *self,
					  guint16 addr,
					  guint16 len,
					  guint8 *buf,
					  guint8 ram_dest,
					  GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_ag_usbcardreader_reg_cdb_new();

	fu_struct_ag_usbcardreader_reg_cdb_set_cmd(st, FU_AG_USBCARDREADER_SCSIOP_VENDOR_GENERIC_CMD);
	fu_struct_ag_usbcardreader_reg_cdb_set_subcmd(st, FU_AG_USBCARDREADER_RD_WR_RAM);
	fu_struct_ag_usbcardreader_reg_cdb_set_ramdest(st, ram_dest);
	fu_struct_ag_usbcardreader_reg_cdb_set_addr(st, addr);

	if (!fu_algoltek_usbcardreader_device_cmd_in(self, st->data, st->len, buf, len, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_algoltek_usbcardreader_device_send_spi_cmd(FuAlgoltekUsbcardreaderDevice *self, guint8 cmd, GError **error)
{
	guint8 dummybuf[8] = {0};
	g_autoptr(GByteArray) st = fu_struct_ag_usbcardreader_spi_cdb_new();

	fu_struct_ag_usbcardreader_spi_cdb_set_cmd(st, FU_AG_USBCARDREADER_SCSIOP_VENDOR_EEPROM_WR);
	fu_struct_ag_usbcardreader_spi_cdb_set_addr(st, 0xFFFF);
	fu_struct_ag_usbcardreader_spi_cdb_set_bufsz(st, sizeof(dummybuf) & 0xFF);
	fu_struct_ag_usbcardreader_spi_cdb_set_tag(st, FU_AG_SPECIFY_EEPROM_TYPE_TAG);
	fu_struct_ag_usbcardreader_spi_cdb_set_valid(st, FU_AG_SPIFLASH_VALID);
	fu_struct_ag_usbcardreader_spi_cdb_set_spisig1(st, FU_AG_SPECIFY_SPI_CMD_SIG_1);
	fu_struct_ag_usbcardreader_spi_cdb_set_spisig2(st, FU_AG_SPECIFY_SPI_CMD_SIG_2);
	fu_struct_ag_usbcardreader_spi_cdb_set_spicmd(st, cmd);

	if (!fu_algoltek_usbcardreader_device_cmd_out(self, st->data, st->len, dummybuf, sizeof(dummybuf), error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_algoltek_usbcardreader_device_do_write_spi(FuAlgoltekUsbcardreaderDevice *self, guint16 addr, guint8 wr_len, guint8* buf ,guint8 access_sz, GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_ag_usbcardreader_spi_cdb_new();

	if (!fu_algoltek_usbcardreader_device_send_spi_cmd(self, FU_AG_USBCARDREADER_WREN, error))
	 	return FALSE;

	fu_struct_ag_usbcardreader_spi_cdb_set_cmd(st, FU_AG_USBCARDREADER_SCSIOP_VENDOR_EEPROM_WR);
	fu_struct_ag_usbcardreader_spi_cdb_set_addr(st, addr);
	fu_struct_ag_usbcardreader_spi_cdb_set_bufsz(st, access_sz);
	fu_struct_ag_usbcardreader_spi_cdb_set_tag(st, FU_AG_SPECIFY_EEPROM_TYPE_TAG);
	fu_struct_ag_usbcardreader_spi_cdb_set_valid(st, FU_AG_SPIFLASH_VALID);

	if (!fu_algoltek_usbcardreader_device_cmd_out(self, st->data, st->len, buf, wr_len, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_algoltek_usbcardreader_device_do_read_spi(FuAlgoltekUsbcardreaderDevice *self,
					     guint16 addr,
					     guint8 wr_len,
					     guint8 *buf,
					     GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_ag_usbcardreader_spi_cdb_new();

	fu_struct_ag_usbcardreader_spi_cdb_set_cmd(st, FU_AG_USBCARDREADER_SCSIOP_VENDOR_EEPROM_RD);
	fu_struct_ag_usbcardreader_spi_cdb_set_addr(st, addr);
	fu_struct_ag_usbcardreader_spi_cdb_set_bufsz(st, wr_len);
	fu_struct_ag_usbcardreader_spi_cdb_set_tag(st, FU_AG_SPECIFY_EEPROM_TYPE_TAG);
	fu_struct_ag_usbcardreader_spi_cdb_set_valid(st, FU_AG_SPIFLASH_VALID);

	if (!fu_algoltek_usbcardreader_device_cmd_in(self, st->data, st->len, buf, wr_len, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_algoltek_usbcardreader_device_spi_flash_block_mode_cb(FuAlgoltekUsbcardreaderDevice *self, gpointer user_data, GError **error)
{
	guint8 buf[8] = {0};
	guint8 en = GPOINTER_TO_INT(user_data);
	/* set command wren */
	FuAgUsbcardreaderRegSetup wr_en_reg_set[5]={
		{0xC8,0x04},
		{0xCA,0x01},
		{0x400,FU_AG_USBCARDREADER_WREN},
		{0xC9,0x01},
		{0xC8,0x05},
	};
	for(int i = 0; i< (sizeof(wr_en_reg_set)/sizeof(wr_en_reg_set[0]) & 0xFF);i++){
		if (!fu_algoltek_usbcardreader_device_write_reg(self, wr_en_reg_set[i].reg, wr_en_reg_set[i].val, FU_AG_USBCARDREADER_RD_WR_XDATA, error))
			return FALSE;
	}
	buf[0] = 0;
	do {
		if (!fu_algoltek_usbcardreader_device_read_reg(self, 0xC8, 1, buf, FU_AG_USBCARDREADER_RD_WR_XDATA, error))
			return FALSE;
	}while(buf[0] & 0x01);

	/* set command wrsr */
	FuAgUsbcardreaderRegSetup wr_sr_reg_set[6]={
		{0xC8,0x04},
		{0xCA,0x01},
		{0x400,FU_AG_USBCARDREADER_WRSR},
		{0x401,0x00},
		{0xC9,0x02},
		{0xC8,0x05},
	};
	if(en == FU_AG_BLOCK_MODE_EN){
		wr_sr_reg_set[3].val = 0x0C;
	}
	for(int i = 0; i< (sizeof(wr_sr_reg_set)/sizeof(wr_sr_reg_set[0]) & 0xFF);i++){
		if (!fu_algoltek_usbcardreader_device_write_reg(self, wr_sr_reg_set[i].reg, wr_sr_reg_set[i].val, FU_AG_USBCARDREADER_RD_WR_XDATA, error))
			return FALSE;
	}

	buf[0] = 0;
	do {
		if (!fu_algoltek_usbcardreader_device_read_reg(self, 0xC8, 1, buf, FU_AG_USBCARDREADER_RD_WR_XDATA, error))
			return FALSE;
	}while(buf[0]&0x01);
	/* set command rdsr */
	FuAgUsbcardreaderRegSetup rd_sr_reg_set[7]={
		{0xC8,0x04},
		{0xCA,0x01},
		{0x400,FU_AG_USBCARDREADER_RDSR},
		{0xC9,0x01},
		{0xC4,0x01},
		{0xC7,0x00},
		{0xC8,0x07},
	};
	for(int i = 0; i< (sizeof(rd_sr_reg_set)/sizeof(rd_sr_reg_set[0]) & 0xFF);i++){
		if (!fu_algoltek_usbcardreader_device_write_reg(self, rd_sr_reg_set[i].reg, rd_sr_reg_set[i].val, FU_AG_USBCARDREADER_RD_WR_XDATA, error))
			return FALSE;
	}

	buf[0] = 0;
	do {
		if (!fu_algoltek_usbcardreader_device_read_reg(self, 0xC8, 1, buf, FU_AG_USBCARDREADER_RD_WR_XDATA, error))
			return FALSE;
	}while(buf[0]&0x01);

	/* read data */
	buf[0] = 0;
	if (!fu_algoltek_usbcardreader_device_read_reg(self, 0x400, 2, buf, FU_AG_USBCARDREADER_RD_WR_XDATA, error))
		return FALSE;
	if (en == FU_AG_BLOCK_MODE_DISEN) {
		if ((buf[0] & 0xC) != 0x0)
			return FALSE;
	} else {
		if ((buf[0] & 0xC) != 0xC)
			return FALSE;
	}

	return TRUE;
}

static gboolean
fu_algoltek_usbcardreader_device_set_clear_soft_reset_flag(FuAlgoltekUsbcardreaderDevice *self,
							   guint8 val,
							   GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_ag_usbcardreader_reset_cdb_new();

	fu_struct_ag_usbcardreader_reset_cdb_set_cmd(st, FU_AG_USBCARDREADER_SCSIOP_VENDOR_GENERIC_CMD);
	fu_struct_ag_usbcardreader_reset_cdb_set_subcmd(st, 0x96);
	fu_struct_ag_usbcardreader_reset_cdb_set_val(st, 0x78);
	fu_struct_ag_usbcardreader_reset_cdb_set_val2(st, val);

	if (!fu_algoltek_usbcardreader_device_cmd_out(self, st->data, st->len, NULL, 0, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_algoltek_usbcardreader_device_reset_chip(FuAlgoltekUsbcardreaderDevice *self, GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_ag_usbcardreader_reset_cdb_new();

	fu_struct_ag_usbcardreader_reset_cdb_set_cmd(st, FU_AG_USBCARDREADER_SCSIOP_VENDOR_GENERIC_CMD);
	fu_struct_ag_usbcardreader_reset_cdb_set_subcmd(st, 0x95);
	fu_struct_ag_usbcardreader_reset_cdb_set_val(st, 0x23);

	if (!fu_algoltek_usbcardreader_device_cmd_out(self, st->data, st->len, NULL, 0, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_algoltek_usbcardreader_device_ensure_version(FuAlgoltekUsbcardreaderDevice *self, GError **error){
	g_autoptr(GByteArray) ver_array = NULL;

	ver_array = fu_algoltek_usbcardreader_device_cmd_get_ver(self,error);

	if(ver_array == NULL){
	        g_prefix_error(error, "failed to read version: ");
		return FALSE;
	}
	else{
		if (!fu_memread_uint16_safe(ver_array->data,
					    ver_array->len,
					    130,
					    &self->app_ver,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		if (!fu_memread_uint16_safe(ver_array->data,
					    ver_array->len,
					    132,
					    &self->boot_ver,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
	}
	return TRUE;
}

static void
fu_algoltek_usbcardreader_device_to_string(FuAlgoltekUsbcardreaderDevice *device, guint idt, GString *str)
{
	FuAlgoltekUsbcardreaderDevice *self = FU_ALGOLTEK_USBCARDREADER_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "AppVer", self->app_ver);
	fwupd_codec_string_append_hex(str, idt, "BootVer", self->boot_ver);
}

static gboolean
fu_algoltek_usbcardreader_device_set_quirk_kv(FuDevice *device,
				      const gchar *key,
				      const gchar *value,
				      GError **error)
{
	FuAlgoltekUsbcardreaderDevice *self = FU_ALGOLTEK_USBCARDREADER_DEVICE(device);
	guint64 tmp;

	/* load from quirks */
	if (g_strcmp0(key, "AlgoltekUsbcardreaderCompatibleModel") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static gboolean
fu_algoltek_usbcardreader_device_probe(FuDevice *device, GError **error)
{
	FuAlgoltekUsbcardreaderDevice *self = FU_ALGOLTEK_USBCARDREADER_DEVICE(device);

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_algoltek_usbcardreader_device_parent_class)->probe(device, error))
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
fu_algoltek_usbcardreader_device_setup(FuDevice *device, GError **error)
{
	FuAlgoltekUsbcardreaderDevice *self = FU_ALGOLTEK_USBCARDREADER_DEVICE(device);
	g_autofree gchar *version_str = NULL;
	g_autofree gchar *vendor_id = NULL;

	if (!fu_algoltek_usbcardreader_device_ensure_version(self, error))
		return FALSE;

	version_str = g_strdup_printf("%x",self->app_ver);
	fu_device_set_version(FU_DEVICE(self), version_str);

	vendor_id = g_strdup_printf("Algoltek:0x%04X", fu_udev_device_get_vendor(FU_UDEV_DEVICE(self)));
	fu_device_add_vendor_id(device, vendor_id);
	fu_device_set_vendor(FU_DEVICE(self), "Algoltek");
	/* success */
	return TRUE;
}

static FuFirmware *
fu_algoltek_usbcardreader_device_prepare_firmware(FuDevice *device,
					  GInputStream *stream,
					  FuProgress *progress,
					  FwupdInstallFlags flags,
					  GError **error)
{
	FuAlgoltekUsbcardreaderDevice *self = FU_ALGOLTEK_USBCARDREADER_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_algoltek_usbcardreader_firmware_new();

	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	/* validate compatibility */
	if (fu_algoltek_usbcardreader_firmware_get_boot_ver(firmware) != self->boot_ver) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware boot version is 0x%X while expecting value is 0x%X",
			    fu_algoltek_usbcardreader_firmware_get_boot_ver(firmware),
			    self->boot_ver);
		return NULL;
	}

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_algoltek_usbcardreader_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuAlgoltekUsbcardreaderDevice *self = FU_ALGOLTEK_USBCARDREADER_DEVICE(device);
	gint cur_pos = 0;
	guint8 buf[32] = {0};
	FuAgUsbcardreaderRegSetup rd_sr_reg_set[5]={
		{0x400,FU_AG_USBCARDREADER_RDSR},
		{0xC9,0x01},
		{0xC4,0x01},
		{0xC7,0x00},
		{0xC8,0x07},
	};
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GByteArray) fw_array = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(FuChunk) chk = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 4, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 48, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 48, NULL);

	FuProgress *progress_child_write = fu_progress_get_child(progress);
	FuProgress *progress_child_verify = fu_progress_get_child(progress);

	if (!fu_device_retry_full(FU_DEVICE(self),
				fu_algoltek_usbcardreader_device_spi_flash_block_mode_cb,
				5,
				0,
				GINT_TO_POINTER(FU_AG_BLOCK_MODE_DISEN),
				error))
	 	return FALSE;

	if (!fu_algoltek_usbcardreader_device_send_spi_cmd(self, FU_AG_USBCARDREADER_WREN, error))
	 	return FALSE;

	if (!fu_algoltek_usbcardreader_device_send_spi_cmd(self, FU_AG_USBCARDREADER_ERASE, error))
	 	return FALSE;
	fu_progress_step_done(progress);


	/* get default image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	chunks = fu_chunk_array_new_from_stream(stream, 0, 32, error);
	if (chunks == NULL)
		return FALSE;

	cur_pos = fu_firmware_get_size(firmware);
	fu_progress_set_id(progress_child_write, G_STRLOC);
	fu_progress_set_steps(progress_child_write, fu_chunk_array_length(chunks));
	for (gint i = fu_chunk_array_length(chunks) - 1; i >= 0; i--) {
		guint8 back_data[8] = {0};

		chk = fu_chunk_array_index(chunks, i, error);
		cur_pos -= fu_chunk_get_data_sz(chk);
		if (chk == NULL)
			return FALSE;

		if (!fu_algoltek_usbcardreader_device_do_write_spi(self, cur_pos, fu_chunk_get_data_sz(chk), fu_chunk_get_data(chk),fu_chunk_get_data_sz(chk), error))
			return FALSE;

		do {
			for(int i = 0; i< (sizeof(rd_sr_reg_set)/sizeof(rd_sr_reg_set[0]) & 0xFF);i++){
				if (!fu_algoltek_usbcardreader_device_write_reg(self, rd_sr_reg_set[i].reg, rd_sr_reg_set[i].val, FU_AG_USBCARDREADER_RD_WR_XDATA, error))
					return FALSE;
			}

			back_data[0] = 0;
			do {
				if (!fu_algoltek_usbcardreader_device_read_reg(self, 0xC8, 1, back_data, FU_AG_USBCARDREADER_RD_WR_XDATA, error))
					return FALSE;
			}while(back_data[0]&0x01);

			back_data[0] = 0;
			if (!fu_algoltek_usbcardreader_device_read_reg(self, 0x400, 2, back_data, FU_AG_USBCARDREADER_RD_WR_XDATA, error))
				return FALSE;
		}while(back_data[0]&0x01);
		fu_progress_step_done(progress_child_write);
	}
	fu_progress_step_done(progress);

	cur_pos = 0;
	fu_progress_set_id(progress_child_verify, G_STRLOC);
	fu_progress_set_steps(progress_child_verify, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		chk = fu_chunk_array_index(chunks, i, error);

		if (!fu_algoltek_usbcardreader_device_do_read_spi(self, cur_pos, fu_chunk_get_data_sz(chk), &buf[0], error))
	 		return FALSE;

		if (!fu_memcmp_safe(fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk), 0, buf, sizeof(buf), 0x0, fu_chunk_get_data_sz(chk), error))
			return FALSE;

		cur_pos += fu_chunk_get_data_sz(chk);
		fu_progress_step_done(progress_child_verify);
	}
	fu_progress_step_done(progress);

	if (!fu_device_retry_full(FU_DEVICE(self),
				fu_algoltek_usbcardreader_device_spi_flash_block_mode_cb,
				5,
				0,
				GINT_TO_POINTER(FU_AG_BLOCK_MODE_EN),
				error))
	 	return FALSE;


	/* reset */
	if (!fu_algoltek_usbcardreader_device_set_clear_soft_reset_flag(self, 0xAF, error))
	 	return FALSE;
	if (!fu_algoltek_usbcardreader_device_reset_chip(self, error))
	 	return FALSE;

	/* success! */
	return TRUE;
}


static void
fu_algoltek_usbcardreader_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_algoltek_usbcardreader_device_init(FuAlgoltekUsbcardreaderDevice *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_add_protocol(FU_DEVICE(self), "com.algoltek.usbcardreader");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);

	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_SYNC);
}

static void
fu_algoltek_usbcardreader_device_class_init(FuAlgoltekUsbcardreaderDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_algoltek_usbcardreader_device_probe;
	device_class->to_string = fu_algoltek_usbcardreader_device_to_string;
	device_class->setup = fu_algoltek_usbcardreader_device_setup;
	device_class->prepare_firmware = fu_algoltek_usbcardreader_device_prepare_firmware;
	device_class->write_firmware = fu_algoltek_usbcardreader_device_write_firmware;
	device_class->set_progress = fu_algoltek_usbcardreader_device_set_progress;
	device_class->set_quirk_kv = fu_algoltek_usbcardreader_device_set_quirk_kv;
}
