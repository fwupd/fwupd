/*
 * Copyright (C) 2015-2019 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-chunk.h"

#include "fu-vli-pd-device.h"
#include "fu-vli-pd-parade-device.h"

struct _FuVliPdParadeDevice
{
	FuDevice		 parent_instance;
	FuVliDeviceKind		 device_kind;
	guint8			 page2;		/* base address */
	guint8			 page7;		/* base address */
};

G_DEFINE_TYPE (FuVliPdParadeDevice, fu_vli_pd_parade_device, FU_TYPE_DEVICE)

#define FU_VLI_PD_PARADE_I2C_CMD_WRITE		0xa6
#define FU_VLI_PD_PARADE_I2C_CMD_READ		0xa5

static void
fu_vli_pd_parade_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuVliPdParadeDevice *self = FU_VLI_PD_PARADE_DEVICE (device);
	fu_common_string_append_kv (str, idt, "DeviceKind",
				    fu_vli_common_device_kind_to_string (self->device_kind));
	fu_common_string_append_kx (str, idt, "Page2", self->page2);
	fu_common_string_append_kx (str, idt, "Page7", self->page7);
}

static gboolean
fu_vli_pd_parade_device_i2c_read (FuVliPdParadeDevice *self,
				  guint8 page2,
				  guint8 reg_offset,		/* customers addr offset */
				  guint8 *buf,
				  gsize bufsz,
				  GError **error)
{
	guint16 value;

	/* sanity check */
	if (bufsz > 0x40) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "request too large");
		return FALSE;
	}

	/* VL103 FW only Use bits[7:1], so divide by 2 */
	value = ((guint16) reg_offset << 8)| (page2 >> 1);
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    FU_VLI_PD_PARADE_I2C_CMD_READ, value, 0x0,
					    buf, bufsz, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to read 0x%x:0x%x: ", page2, reg_offset);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_pd_parade_device_i2c_write (FuVliPdParadeDevice *self,
				   guint8 page2,
				   guint8 reg_offset,	/* customers addr offset */
				   guint8 val,		/* only one byte supported */
				   GError **error)
{
	guint16 value;
	guint16 index;
	guint8 buf[2] = { 0x0 };	/* apparently unused... */

	/* VL103 FW only Use bits[7:1], so divide by 2 */
	value = ((guint16) reg_offset << 8) | (page2 >> 1);
	index = (guint16) val << 8;
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    FU_VLI_PD_PARADE_I2C_CMD_WRITE,
					    value, index,
					    buf, 0x0, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to write 0x%x:0x%x: ", page2, reg_offset);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_pd_parade_device_start_mcu (FuVliPdParadeDevice *self, GError **error)
{
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0xBC, 0x00, error)) {
		g_prefix_error (error, "failed to start MCU: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_pd_parade_device_stop_mcu (FuVliPdParadeDevice *self, GError **error)
{
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0xBC, 0xC0, error)) {
		g_prefix_error (error, "failed to stop MCU: ");
		return FALSE;
	}
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0xBC, 0x40, error)) {
		g_prefix_error (error, "failed to stop MCU 2nd: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_pd_parade_device_set_offset (FuVliPdParadeDevice *self, guint16 addr, GError **error)
{
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0x8E, addr >> 8, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0x8F, addr & 0xff, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_vli_pd_parade_device_read_fw_ver (FuVliPdParadeDevice *self, GError **error)
{
	guint8 buf[0x20] = { 0x0 };
	g_autofree gchar *version_str = NULL;

	/* stop MCU */
	if (!fu_vli_pd_parade_device_stop_mcu (self, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_set_offset (self, 0x0, error))
		return FALSE;
	g_usleep (1000 * 10);
	if (!fu_vli_pd_parade_device_i2c_read (self, self->page7, 0x02, buf, 0x1, error))
		return FALSE;
	if (buf[0] != 0x01 && buf[0] != 0x02) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "not supported");
		return FALSE;
	}

	g_debug ("getting FW%X version", buf[0]);
	if (!fu_vli_pd_parade_device_set_offset (self, 0x5000 | buf[0], error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_read (self, self->page7, 0x00, buf, sizeof(buf), error))
		return FALSE;

	/* start MCU */
	if (!fu_vli_pd_parade_device_start_mcu (self, error))
		return FALSE;

	/* format version triplet */
	version_str = g_strdup_printf ("%u.%u.%u", buf[0], buf[1], buf[2]);
	fu_device_set_version (FU_DEVICE (self), version_str, FWUPD_VERSION_FORMAT_TRIPLET);
	return TRUE;
}

static gboolean
fu_vli_pd_parade_device_set_wp (FuVliPdParadeDevice *self, gboolean val, GError **error)
{
	return fu_vli_pd_parade_device_i2c_write (self, self->page2, 0xB3,
						  val ? 0x10 : 0x00, error);
}

static gboolean
fu_vli_pd_parade_device_write_enable (FuVliPdParadeDevice *self, GError **error)
{
	/* Set_WP_High, SPI_WEN_06, Len_00, Trigger_Write, Set_WP_Low */
	if (!fu_vli_pd_parade_device_set_wp (self, TRUE, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0x90, 0x06, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0x92, 0x00, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0x93, 0x05, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_set_wp (self, FALSE, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_vli_pd_parade_device_write_disable (FuVliPdParadeDevice *self, GError **error)
{
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0xDA, 0x00, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_vli_pd_parade_device_write_status (FuVliPdParadeDevice *self, guint8 target_status, GError **error)
{
	/* Set_WP_High, SPI_WSTS_01, Target_Status, Len_01, Trigger_Write, Set_WP_Low */
	if (!fu_vli_pd_parade_device_set_wp (self, TRUE, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0x90, 0x01, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0x90, target_status, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0x92, 0x01, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0x93, 0x05, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_set_wp (self, FALSE, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_vli_pd_parade_device_wait_ready (FuVliPdParadeDevice *self, GError **error)
{
	gboolean ret = FALSE;
	guint limit = 100;
	guint8 buf = 0x0;

	/* wait for SPI ROM */
	for (guint wait_cnt1 = 0; wait_cnt1 < limit; wait_cnt1++) {
		buf = 0xFF;
		if (!fu_vli_pd_parade_device_i2c_read (self, self->page2,
						       0x9E, &buf, sizeof(buf),
						       error))
			return FALSE;
		/* busy status:
		 * bit[1,0]:Byte_Program
		 * bit[3,2]:Sector Erase
		 * bit[5,4]:Chip Erase */
		if ((buf & 0x0C) == 0) {
			ret = TRUE;
			break;
		}
	}
	if (!ret) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to wait for SPI not BUSY");
		return FALSE;
	}

	/* wait for SPI ROM status clear */
	ret = FALSE;
	for (guint wait_cnt1 = 0; wait_cnt1 < limit; wait_cnt1++) {
		gboolean ret2 = FALSE;

		/* SPI_RSTS_05, Len_01, Trigger_Read */
		if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0x90, 0x05, error))
			return FALSE;
		if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0x92, 0x00, error))
			return FALSE;
		if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0x93, 0x01, error))
			return FALSE;

		/* wait for cmd done */
		for (guint wait_cnt2 = 0; wait_cnt2 < limit; wait_cnt2++) {
			buf = 0xFF;
			if (!fu_vli_pd_parade_device_i2c_read (self, self->page2,
							       0x93, &buf, sizeof(buf),
							       error))
				return FALSE;
			if ((buf & 0x01) == 0) {
				ret2 = TRUE;
				break;
			}
		}
		if (!ret2) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "failed to wait for SPI CMD done");
			return FALSE;
		}

		/* Wait_SPI_STS_00 */
		buf = 0xFF;
		if (!fu_vli_pd_parade_device_i2c_read (self, self->page2,
						       0x91, &buf, sizeof(buf),
						       error))
			return FALSE;
		if ((buf & 0x01) == 0) {
			ret = TRUE;
			break;
		}
	}
	if (!ret) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to wait for SPI status clear");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_vli_pd_parade_device_sector_erase (FuVliPdParadeDevice *self, guint16 addr, GError **error)
{
	/* SPI_SE_20, SPI_Adr_H, SPI_Adr_M, SPI_Adr_L, Len_03, Trigger_Write */
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0x90, 0x20, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0x90, addr >> 8, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0x90, addr & 0xff, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0x90, 0x00, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0x92, 0x03, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0x93, 0x05, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_vli_pd_parade_device_enable_mapping (FuVliPdParadeDevice *self, GError **error)
{
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0xDA, 0xAA, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0xDA, 0x55, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0xDA, 0x50, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0xDA, 0x41, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0xDA, 0x52, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0xDA, 0x44, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_vli_pd_parade_device_block_erase (FuVliPdParadeDevice *self, guint8 block_idx, GError **error)
{
	/* erase */
	for (guint idx = 0x00; idx < 0x100; idx += 0x10){
		if (!fu_vli_pd_parade_device_write_enable (self, error))
			return FALSE;
		if (!fu_vli_pd_parade_device_set_wp (self, TRUE, error))
			return FALSE;
		if (!fu_vli_pd_parade_device_sector_erase (self, ((guint16) block_idx << 8) | idx, error))
			return FALSE;
		if (!fu_vli_pd_parade_device_wait_ready (self, error))
			return FALSE;
		if (!fu_vli_pd_parade_device_set_wp (self, FALSE, error))
			return FALSE;
	}

	/* verify */
	for (guint idx = 0; idx < 0x100; idx += 0x10){
		guint8 buf[0x20] = { 0xff };
		if (!fu_vli_pd_parade_device_set_offset (self, (block_idx << 8) | idx, error))
			return FALSE;
		if (!fu_vli_pd_parade_device_i2c_read (self, self->page7, 0, buf, 0x20, error))
			return FALSE;
		for (guint idx2 = 0; idx2 < 0x20; idx2++){
			if (buf[idx2] != 0xFF) {
				guint32 addr = (block_idx << 16) + (idx << 8);
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "Erase failed @0x%x", addr);
				return FALSE;
			}
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_vli_pd_parade_device_block_write (FuVliPdParadeDevice *self,
				     guint8 block_idx,
				     const guint8 *txbuf,
				     GError **error)
{
	for (guint idx = 0; idx < 0x100; idx++) {
		if (!fu_vli_pd_parade_device_set_offset (self, (block_idx << 8) | idx, error))
			return FALSE;
		for (guint idx2 = 0; idx2 < 0x100; idx2++){
			guint32 buf_offset = (idx << 8) + idx2;
			if (!fu_vli_pd_parade_device_i2c_write (self,
								self->page7,
								(guint8)idx2,
								txbuf[buf_offset],
								error))
				return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static GBytes *
_g_bytes_new_sized (gsize sz)
{
	guint8 *buf = g_malloc0 (sz);
	return g_bytes_new_take (buf, sz);
}

static gboolean
fu_vli_pd_parade_device_block_read (FuVliPdParadeDevice *self,
				    guint8 block_idx,
				    guint8 *buf,
				    gsize bufsz,
				    GError **error)
{
	for (guint idx = 0; idx < 0x100; idx++){
		if (!fu_vli_pd_parade_device_set_offset (self, (block_idx << 8) | idx, error))
			return FALSE;
		for (guint idx2 = 0; idx2 < 0x100; idx2 += 0x20){
			guint buf_offset = (idx << 8) + idx2;
			if (!fu_vli_pd_parade_device_i2c_read (self, self->page7, idx2, buf + buf_offset, 0x20, error))
				return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_vli_pd_parade_device_write_firmware (FuDevice *device,
					FuFirmware *firmware,
					FwupdInstallFlags flags,
					GError **error)
{
	FuVliPdParadeDevice *self = FU_VLI_PD_PARADE_DEVICE (device);
	FuVliPdDevice *parent = FU_VLI_PD_DEVICE (fu_device_get_parent (device));
	guint8 buf[0x20];
	guint block_idx_tmp;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) fw_verify = NULL;
	g_autoptr(GPtrArray) blocks = NULL;
	g_autoptr(GPtrArray) blocks_verify = NULL;

	/* simple image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* open device */
	locker = fu_device_locker_new (parent, error);
	if (locker == NULL)
		return FALSE;

	/*  stop MPU and reset SPI */
	if (!fu_vli_pd_parade_device_stop_mcu (self, error))
		return FALSE;

	/*  64K block erase */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_ERASE);
	if (!fu_vli_pd_parade_device_write_enable (self, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_write_status (self, 0x00, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_wait_ready (self, error))
		return FALSE;
	blocks = fu_chunk_array_new_from_bytes (fw, 0x0, 0x0, 0x10000);
	for (guint i = 1; i < blocks->len; i++) {
		FuChunk *block = g_ptr_array_index (blocks, i);
		if (!fu_vli_pd_parade_device_block_erase (self, block->idx, error))
			return FALSE;
		fu_device_set_progress_full (FU_DEVICE (self), i, blocks->len);
	}

	/*  load F/W to SPI ROM */
	if (!fu_vli_pd_parade_device_enable_mapping (self, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0x82, 0x20, error))
		return FALSE; /* Reset_CLT2SPI_Interface */
	g_usleep (1000 * 100);
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page2, 0x82, 0x00, error))
		return FALSE;

	/* write blocks */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 1; i < blocks->len; i++) {
		FuChunk *block = g_ptr_array_index (blocks, i);
		if (!fu_vli_pd_parade_device_block_write (self, block->idx, block->data, error))
			return FALSE;
		fu_device_set_progress_full (FU_DEVICE (self), i, blocks->len);
	}
	if (!fu_vli_pd_parade_device_write_disable (self, error))
		return FALSE;

	/*  verify SPI ROM */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_VERIFY);
	fw_verify = _g_bytes_new_sized (g_bytes_get_size (fw));
	blocks_verify = fu_chunk_array_new_from_bytes (fw_verify, 0x0, 0x0, 0x10000);
	for (guint i = 1; i < blocks_verify->len; i++) {
		FuChunk *block = g_ptr_array_index (blocks_verify, i);
		if (!fu_vli_pd_parade_device_block_read (self,
							 block->idx,
							 (guint8 *) block->data,
							 block->data_sz,
							 error))
			return FALSE;
		fu_device_set_progress_full (FU_DEVICE (self), i, blocks->len);
	}
	if (!fu_common_bytes_compare (fw, fw_verify, error))
		return FALSE;

	/*  save boot config into Block_0 */
	if (!fu_vli_pd_parade_device_write_enable (self, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_set_wp (self, TRUE, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_sector_erase (self, 0x0, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_wait_ready (self, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_set_wp (self, FALSE, error))
		return FALSE;

	/* Page_HW_Write_Disable */
	if (!fu_vli_pd_parade_device_enable_mapping (self, error))
		return FALSE;

	block_idx_tmp = 1;
	if (!fu_vli_pd_parade_device_set_offset (self, 0x0, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page7, 0x00, 0x55, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page7, 0x01, 0xAA, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page7, 0x02, (guint8) block_idx_tmp, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_write (self, self->page7, 0x03, (guint8) (0x01 - block_idx_tmp), error))
		return FALSE;
	if (!fu_vli_pd_parade_device_write_disable (self, error))
		return FALSE;

	/*  check boot config data */
	if (!fu_vli_pd_parade_device_set_offset (self, 0x0, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_i2c_read (self, self->page7, 0, buf, sizeof(buf), error))
		return FALSE;
	if (buf[0] != 0x55 ||
	    buf[1] != 0xAA ||
	    buf[2] != block_idx_tmp ||
	    buf[3] != 0x01 - block_idx_tmp) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "boot config data error");
		return FALSE;
	}

	/*  enable write protection */
	if (!fu_vli_pd_parade_device_write_enable (self, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_write_status (self, 0x8C, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_wait_ready (self, error))
		return FALSE;
	if (!fu_vli_pd_parade_device_write_disable (self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static FuFirmware *
fu_vli_pd_parade_device_read_firmware (FuDevice *device, GError **error)
{
	FuVliPdDevice *parent = FU_VLI_PD_DEVICE (fu_device_get_parent (device));
	FuVliPdParadeDevice *self = FU_VLI_PD_PARADE_DEVICE (device);
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) blocks = NULL;

	/* open device */
	locker = fu_device_locker_new (parent, error);
	if (locker == NULL)
		return NULL;

	/*  stop MPU and reset SPI */
	if (!fu_vli_pd_parade_device_stop_mcu (self, error))
		return NULL;

	/* read */
	fu_device_set_status (FU_DEVICE (device), FWUPD_STATUS_DEVICE_VERIFY);
	fw = _g_bytes_new_sized (fu_device_get_firmware_size_max (device));
	blocks = fu_chunk_array_new_from_bytes (fw, 0x0, 0x0, 0x10000);
	for (guint i = 0; i < blocks->len; i++) {
		FuChunk *block = g_ptr_array_index (blocks, i);
		if (!fu_vli_pd_parade_device_block_read (self,
							 block->idx,
							 (guint8 *) block->data,
							 block->data_sz,
							 error))
			return NULL;
	}
	return fu_firmware_new_from_bytes (fw);
}


static FuFirmware *
fu_vli_pd_parade_device_prepare_firmware (FuDevice *device,
					  GBytes *fw,
					  FwupdInstallFlags flags,
					  GError **error)
{
	/* check size */
	if (g_bytes_get_size (fw) < fu_device_get_firmware_size_min (device)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "firmware too small, got 0x%x, expected >= 0x%x",
			     (guint) g_bytes_get_size (fw),
			     (guint) fu_device_get_firmware_size_min (device));
		return NULL;
	}
	return fu_firmware_new_from_bytes (fw);
}

static gboolean
fu_vli_pd_parade_device_probe (FuDevice *device, GError **error)
{
	FuVliPdDevice *parent = FU_VLI_PD_DEVICE (fu_device_get_parent (device));
	FuVliPdParadeDevice *self = FU_VLI_PD_PARADE_DEVICE (device);
	g_autofree gchar *instance_id1 = NULL;

	/* get version */
	if (!fu_vli_pd_parade_device_read_fw_ver (self, error))
		return FALSE;

	/* use header to populate device info */
	instance_id1 = g_strdup_printf ("USB\\VID_%04X&PID_%04X&I2C_%s",
					fu_usb_device_get_vid (FU_USB_DEVICE (parent)),
					fu_usb_device_get_pid (FU_USB_DEVICE (parent)),
					fu_vli_common_device_kind_to_string (self->device_kind));
	fu_device_add_instance_id (device, instance_id1);

	/* success */
	return TRUE;
}

static void
fu_vli_pd_parade_device_init (FuVliPdParadeDevice *self)
{
	self->device_kind = FU_VLI_DEVICE_KIND_PS186;
	self->page2 = 0x14;
	self->page7 = 0x1E;
	fu_device_add_icon (FU_DEVICE (self), "video-display");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_protocol (FU_DEVICE (self), "com.vli.i2c");
	fu_device_set_install_duration (FU_DEVICE (self), 15); /* seconds */
	fu_device_set_logical_id (FU_DEVICE (self), "PS186");
	fu_device_set_summary (FU_DEVICE (self), "DisplayPort 1.4a to HDMI 2.0b Protocol Converter");
	fu_device_set_firmware_size (FU_DEVICE (self), 0x40000);
}

static void
fu_vli_pd_parade_device_class_init (FuVliPdParadeDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->to_string = fu_vli_pd_parade_device_to_string;
	klass_device->probe = fu_vli_pd_parade_device_probe;
	klass_device->read_firmware = fu_vli_pd_parade_device_read_firmware;
	klass_device->prepare_firmware = fu_vli_pd_parade_device_prepare_firmware;
	klass_device->write_firmware = fu_vli_pd_parade_device_write_firmware;
}

FuDevice *
fu_vli_pd_parade_device_new (FuVliDevice *parent)
{
	FuVliPdParadeDevice *self = g_object_new (FU_TYPE_VLI_PD_PARADE_DEVICE,
						  "parent", parent,
						  NULL);
	return FU_DEVICE (self);
}
