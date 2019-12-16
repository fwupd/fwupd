/*
 * Copyright (C) 2017-2019 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-chunk.h"
#include "fu-firmware.h"

#include "fu-vli-device.h"

#define FU_VLI_DEVICE_TIMEOUT		3000	/* ms */
#define FU_VLI_DEVICE_TXSIZE		0x20	/* bytes */

typedef struct {
	FuUsbDevice		 parent_instance;
	FuVliDeviceKind		 kind;
	guint32			 flash_id;
	guint8			 spi_cmd_read_id;
	guint8			 spi_cmd_read_id_sz;
	guint8			 spi_cmd_page_prog;
	guint8			 spi_cmd_chip_erase;
	guint8			 spi_cmd_read_data;
	guint8			 spi_cmd_read_status;
	guint8			 spi_cmd_sector_erase;
	guint8			 spi_cmd_write_en;
	guint8			 spi_cmd_write_status;
} FuVliDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuVliDevice, fu_vli_device, FU_TYPE_USB_DEVICE)

#define GET_PRIVATE(o) (fu_vli_device_get_instance_private (o))

static gchar *
fu_vli_device_get_flash_id_str (FuVliDevice *self)
{
	FuVliDevicePrivate *priv = GET_PRIVATE (self);
	if (priv->spi_cmd_read_id_sz == 4)
		return g_strdup_printf ("%08X", priv->flash_id);
	if (priv->spi_cmd_read_id_sz == 2)
		return g_strdup_printf ("%04X", priv->flash_id);
	if (priv->spi_cmd_read_id_sz == 1)
		return g_strdup_printf ("%02X", priv->flash_id);
	return g_strdup_printf ("%X", priv->flash_id);
}

void
fu_vli_device_set_kind (FuVliDevice *self, FuVliDeviceKind device_kind)
{
	FuVliDevicePrivate *priv = GET_PRIVATE (self);
	priv->kind = device_kind;
}

FuVliDeviceKind
fu_vli_device_get_kind (FuVliDevice *self)
{
	FuVliDevicePrivate *priv = GET_PRIVATE (self);
	return priv->kind;
}

static void
fu_vli_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuVliDeviceClass *klass = FU_VLI_DEVICE_GET_CLASS (device);
	FuVliDevice *self = FU_VLI_DEVICE (device);
	FuVliDevicePrivate *priv = GET_PRIVATE (self);
	fu_common_string_append_kv (str, idt, "DeviceKind",
				    fu_vli_common_device_kind_to_string (priv->kind));
	if (priv->flash_id != 0x0) {
		g_autofree gchar *tmp = fu_vli_device_get_flash_id_str (self);
		fu_common_string_append_kv (str, idt, "FlashId", tmp);
	}
	fu_common_string_append_kx (str, idt, "SpiCmdReadId", priv->spi_cmd_read_id);
	fu_common_string_append_kx (str, idt, "SpiCmdReadIdSz", priv->spi_cmd_read_id_sz);
	fu_common_string_append_kx (str, idt, "SpiCmdChipErase", priv->spi_cmd_chip_erase);
	fu_common_string_append_kx (str, idt, "SpiCmdPageProg", priv->spi_cmd_page_prog);
	fu_common_string_append_kx (str, idt, "SpiCmdReadData", priv->spi_cmd_read_data);
	fu_common_string_append_kx (str, idt, "SpiCmdSectorErase", priv->spi_cmd_sector_erase);
	fu_common_string_append_kx (str, idt, "SpiCmdWriteEn", priv->spi_cmd_write_en);
	fu_common_string_append_kx (str, idt, "SpiCmdWriteStatus", priv->spi_cmd_write_status);

	/* subclassed further */
	if (klass->to_string != NULL)
		return klass->to_string (self, idt, str);
}

gboolean
fu_vli_device_i2c_read (FuVliDevice *self,
			guint8 cmd, guint8 *buf, gsize bufsz,
			GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	guint16 value = ((guint16) FU_VLI_I2C_ADDR_WRITE << 8) | cmd;
	guint16 index = (guint16) FU_VLI_I2C_ADDR_READ << 8;
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    FU_VLI_I2C_R_VDR, value, index,
					    buf, bufsz, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to read I2C: ");
		return FALSE;
	}
	if (g_getenv ("FWUPD_VLI_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "I2cReadData", buf, 0x1);
	return TRUE;
}

gboolean
fu_vli_device_i2c_read_status (FuVliDevice *self,
			       FuVliDeviceI2cStatus *status,
			       GError **error)
{
	guint8 buf[1] = { 0xff };
	if (!fu_vli_device_i2c_read (self,
				     FU_VLI_I2C_CMD_READ_STATUS,
				     buf, sizeof(buf),
				     error))
		return FALSE;
	if (status != NULL)
		*status = buf[0];
	return TRUE;
}

gboolean
fu_vli_device_i2c_write_data (FuVliDevice *self,
			      guint8 skip_s,
			      guint8 skip_p,
			      const guint8 *buf,
			      gsize bufsz,
			      GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	guint16 value = (((guint16) skip_s) << 8) | skip_p;
	if (g_getenv ("FWUPD_VLI_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "I2cWriteData", buf, bufsz);
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    FU_VLI_I2C_W_VDR, value, 0x0,
					    (guint8 *) buf, bufsz, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to write I2C @0x%x: ", value);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_vli_device_i2c_write (FuVliDevice *self, guint8 cmd,
			 const guint8 *buf, gsize bufsz, GError **error)
{
	guint8 buf2[10] = { FU_VLI_I2C_ADDR_WRITE, cmd, 0x0 };
	if (!fu_memcpy_safe (buf2, sizeof(buf2), 0x2,
			     buf, bufsz, 0x0, bufsz, error))
		return FALSE;
	return fu_vli_device_i2c_write_data (self, 0x0, 0x0, buf2, bufsz + 2, error);
}

gboolean
fu_vli_device_vdr_reg_read (FuVliDevice *self,
			    guint8 fun_num,
			    guint16 offset,
			    guint8 *buf,
			    gsize bufsz,
			    GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    fun_num, offset, 0x0,
					    buf, bufsz, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error,
				"failed to read VDR register 0x%x offset 0x%x: ",
				fun_num, offset);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_vli_device_vdr_reg_write (FuVliDevice *self,
			     guint8 fun_num,
			     guint16 offset,
			     guint8 value,
			     GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    fun_num, offset, (guint16) value,
					    NULL, 0x0, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error,
				"failed to write VDR register 0x%x offset 0x%x value 0x%x: ",
				fun_num, offset, value);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_device_spi_read_flash_id (FuVliDevice *self, GError **error)
{
	FuVliDevicePrivate *priv = GET_PRIVATE (self);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	guint8 buf[4] = { 0x0 };
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xc0 | (priv->spi_cmd_read_id_sz * 2),
					    priv->spi_cmd_read_id, 0x0000,
					    buf, sizeof(buf), NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to read chip ID: ");
		return FALSE;
	}
	if (g_getenv ("FWUPD_VLI_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "SpiCmdReadId", buf, sizeof(buf));
	if (priv->spi_cmd_read_id_sz == 4)
		priv->flash_id = fu_common_read_uint32 (buf, G_BIG_ENDIAN);
	else if (priv->spi_cmd_read_id_sz == 2)
		priv->flash_id = fu_common_read_uint16 (buf, G_BIG_ENDIAN);
	else if (priv->spi_cmd_read_id_sz == 1)
		priv->flash_id = buf[0];
	return TRUE;
}

static gboolean
fu_vli_device_spi_read_status (FuVliDevice *self, guint8 *status, GError **error)
{
	FuVliDevicePrivate *priv = GET_PRIVATE (self);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));

	/* sanity check */
	if (priv->spi_cmd_read_status == 0x0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "No value for SpiCmdReadStatus");
		return FALSE;
	}
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xc1, priv->spi_cmd_read_status, 0x0000,
					    status, 0x1, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to read status: ");
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_vli_device_spi_read (FuVliDevice *self,
			     guint32 data_addr,
			     guint8 *buf,
			     gsize length,
			     GError **error)
{
	FuVliDevicePrivate *priv = GET_PRIVATE (self);
	guint16 index = ((data_addr << 8) & 0xff00) | ((data_addr >> 8) & 0x00ff);
	guint16 value = ((data_addr >> 8) & 0xff00) | priv->spi_cmd_read_data;
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));

	/* sanity check */
	if (priv->spi_cmd_read_data == 0x0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "No value for SpiCmdReadData");
		return FALSE;
	}
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xc4, value, index,
					    buf, length, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to read SPI data @0x%x: ", data_addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_device_spi_write_status (FuVliDevice *self, guint8 status, GError **error)
{
	FuVliDevicePrivate *priv = GET_PRIVATE (self);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));

	/* sanity check */
	if (priv->spi_cmd_write_status == 0x0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "No value for SpiCmdWriteStatus");
		return FALSE;
	}
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xd1, priv->spi_cmd_write_status, 0x0000,
					    &status, 0x1, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to write SPI status 0x%x", status);
		return FALSE;
	}

	/* Fix_For_GD_&_EN_SPI_Flash */
	g_usleep (100 * 1000);
	return TRUE;
}

gboolean
fu_vli_device_spi_write_enable (FuVliDevice *self, GError **error)
{
	FuVliDevicePrivate *priv = GET_PRIVATE (self);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));

	/* sanity check */
	if (priv->spi_cmd_write_en == 0x0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "No value for SpiCmdWriteEn");
		return FALSE;
	}
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xd1, priv->spi_cmd_write_en, 0x0000,
					    NULL, 0x0, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to write enable SPI: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_device_spi_erase_chip (FuVliDevice *self, GError **error)
{
	FuVliDevicePrivate *priv = GET_PRIVATE (self);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));

	/* sanity check */
	if (priv->spi_cmd_chip_erase == 0x0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "No value for SpiCmdChipErase");
		return FALSE;
	}
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xd1, priv->spi_cmd_chip_erase, 0x0000,
					    NULL, 0x0, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to erase SPI: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_device_spi_erase_sector (FuVliDevice *self, guint32 data_addr, GError **error)
{
	FuVliDevicePrivate *priv = GET_PRIVATE (self);
	guint16 index = ((data_addr << 8) & 0xff00) | ((data_addr >> 8) & 0x00ff);
	guint16 value = ((data_addr >> 8) & 0xff00) | priv->spi_cmd_sector_erase;
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));

	/* sanity check */
	if (priv->spi_cmd_sector_erase == 0x0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "No value for SpiCmdSectorErase");
		return FALSE;
	}
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xd4, value, index,
					    NULL, 0x0, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to erase SPI sector @0x%x: ", data_addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_device_spi_write_data (FuVliDevice *self,
			      guint32 data_addr,
			      const guint8 *buf,
			      gsize length,
			      GError **error)
{
	FuVliDevicePrivate *priv = GET_PRIVATE (self);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	guint16 value = ((data_addr >> 8) & 0xff00) | priv->spi_cmd_page_prog;
	guint16 index = ((data_addr << 8) & 0xff00) | ((data_addr >> 8) & 0x00ff);

	/* sanity check */
	if (priv->spi_cmd_page_prog == 0x0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "No value for SpiCmdPageProg");
		return FALSE;
	}
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xd4, value, index,
					    (guint8 *) buf, length, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to write SPI @0x%x: ", data_addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_device_spi_wait_finish (FuVliDevice *self, GError **error)
{
	FuVliDevicePrivate *priv = GET_PRIVATE (self);
	const guint32 rdy_cnt = 2;
	guint32 cnt = 0;

	/* sanity check */
	if (priv->spi_cmd_read_status == 0x0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "No value for SpiCmdReadStatus");
		return FALSE;
	}
	for (guint32 idx = 0; idx < 1000; idx++) {
		guint8 status = 0x7f;

		/* must get bit[1:0] == 0 twice in a row for success */
		if (!fu_vli_device_spi_read_status (self, &status, error))
			return FALSE;
		if ((status & 0x03) == 0x00) {
			if (cnt++ >= rdy_cnt)
				return TRUE;
		} else {
			cnt = 0;
		}
		g_usleep (500 * 1000);
	}
	g_set_error (error,
		     G_IO_ERROR,
		     G_IO_ERROR_FAILED,
		     "failed to wait for SPI");
	return FALSE;
}

gboolean
fu_vli_device_spi_erase_verify (FuVliDevice *self, guint32 addr, GError **error)
{
	const guint32 bufsz = 0x1000;

	/* erase sector */
	if (!fu_vli_device_spi_write_enable (self, error)) {
		g_prefix_error (error, "fu_vli_device_spi_write_enable failed: ");
		return FALSE;
	}
	if (!fu_vli_device_spi_write_status (self, 0x00, error)) {
		g_prefix_error (error, "fu_vli_device_spi_write_status failed: ");
		return FALSE;
	}
	if (!fu_vli_device_spi_write_enable (self, error)) {
		g_prefix_error (error, "fu_vli_device_spi_write_enable failed: ");
		return FALSE;
	}
	if (!fu_vli_device_spi_erase_sector (self, addr, error)) {
		g_prefix_error (error, "fu_vli_device_spi_erase_sector failed");
		return FALSE;
	}
	if (!fu_vli_device_spi_wait_finish (self, error)) {
		g_prefix_error (error, "fu_vli_device_spi_wait_finish failed");
		return FALSE;
	}

	/* verify it really was blanked */
	for (guint32 offset = 0; offset < bufsz; offset += FU_VLI_DEVICE_TXSIZE) {
		guint8 buf[FU_VLI_DEVICE_TXSIZE] = { 0x0 };
		if (!fu_vli_device_spi_read (self,
							 addr + offset,
							 buf, sizeof (buf),
							 error)) {
			g_prefix_error (error, "failed to read back empty: ");
			return FALSE;
		}
		for (guint i = 0; i < sizeof(buf); i++) {
			if (buf[i] != 0xff) {
				g_set_error (error,
					     G_IO_ERROR,
					     G_IO_ERROR_FAILED,
					     "failed to check blank @0x%x",
					     addr + offset + i);
				return FALSE;
			}
		}
	}

	/* success */
	return TRUE;
}

gboolean
fu_vli_device_spi_erase (FuVliDevice *self, guint32 addr, gsize sz, GError **error)
{
	g_autoptr(GPtrArray) chunks = fu_chunk_array_new (NULL, sz, addr, 0x0, 0x1000);
	g_debug ("erasing 0x%x bytes @0x%x", (guint) sz, addr);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chunk = g_ptr_array_index (chunks, i);
		if (g_getenv ("FWUPD_VLI_VERBOSE") != NULL)
			g_debug ("erasing @0x%x", chunk->address);
		if (!fu_vli_device_spi_erase_sector (self, chunk->address, error)) {
			g_prefix_error (error,
					"failed to erase FW sector @0x%x: ",
					chunk->address);
			return FALSE;
		}
		fu_device_set_progress_full (FU_DEVICE (self),
					     (gsize) i, (gsize) chunks->len);
	}
	return TRUE;
}

GBytes *
fu_vli_device_spi_read_all (FuVliDevice *self, guint32 address, gsize bufsz, GError **error)
{
	g_autofree guint8 *buf = g_malloc0 (bufsz);
	g_autoptr(GPtrArray) chunks = NULL;

	/* get data from hardware */
	chunks = fu_chunk_array_new (buf, bufsz, address, 0x0, FU_VLI_DEVICE_TXSIZE);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		if (!fu_vli_device_spi_read (self,
							 chk->address,
							 (guint8 *) chk->data,
							 chk->data_sz,
							 error)) {
			g_prefix_error (error, "SPI data read failed @0x%x: ", chk->address);
			return NULL;
		}
		fu_device_set_progress_full (FU_DEVICE (self),
					     (gsize) i, (gsize) chunks->len);
	}
	return g_bytes_new_take (g_steal_pointer (&buf), bufsz);
}

gboolean
fu_vli_device_spi_erase_all (FuVliDevice *self, GError **error)
{
	if (!fu_vli_device_spi_write_enable (self, error))
		return FALSE;
	if (!fu_vli_device_spi_write_status (self, 0x00, error))
		return FALSE;
	if (!fu_vli_device_spi_write_enable (self, error))
		return FALSE;
	if (!fu_vli_device_spi_erase_chip (self, error))
		return FALSE;
	g_usleep (4 * G_USEC_PER_SEC);

	/* verify chip was erased */
	for (guint addr = 0; addr < 0x10000; addr += 0x1000) {
		guint8 buf[FU_VLI_DEVICE_TXSIZE] = { 0x0 };
		if (!fu_vli_device_spi_read (self, addr, buf, sizeof(buf), error)) {
			g_prefix_error (error, "failed to read @0x%x: ", addr);
			return FALSE;
		}
		for (guint i = 0; i < sizeof(buf); i++) {
			if (buf[i] != 0xff) {
				g_set_error (error,
					     G_IO_ERROR,
					     G_IO_ERROR_FAILED,
					     "failed to verify erase @0x%x: ", addr);
				return FALSE;
			}
		}
	}
	return TRUE;
}

gboolean
fu_vli_device_spi_write_verify (FuVliDevice *self,
				guint32 address,
				const guint8 *buf,
				gsize bufsz,
				GError **error)
{
	g_autofree guint8 *buf_tmp = g_malloc0 (bufsz);

	/* sanity check */
	if (bufsz > FU_VLI_DEVICE_TXSIZE) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "cannot write 0x%x in one block",
			     (guint) bufsz);
		return FALSE;
	}

	/* write */
	if (g_getenv ("FWUPD_VLI_VERBOSE") != NULL)
		g_debug ("writing 0x%x block @0x%x", (guint) bufsz, address);
	if (!fu_vli_device_spi_write_enable (self, error)) {
		g_prefix_error (error, "enabling SPI write failed: ");
		return FALSE;
	}
	if (!fu_vli_device_spi_write_data (self, address, buf, bufsz, error)) {
		g_prefix_error (error, "SPI data write failed: ");
		return FALSE;
	}
	g_usleep (800);

	/* verify */
	if (!fu_vli_device_spi_read (self, address, buf_tmp, bufsz, error)) {
		g_prefix_error (error, "SPI data read failed: ");
		return FALSE;
	}
	return fu_common_bytes_compare_raw (buf, bufsz, buf_tmp, bufsz, error);
}

gboolean
fu_vli_device_spi_write (FuVliDevice *self,
			 guint32 address,
			 const guint8 *buf,
			 gsize bufsz,
			 GError **error)
{
	FuChunk *chk;
	g_autoptr(GPtrArray) chunks = NULL;

	/* write SPI data, then CRC bytes last */
	g_debug ("writing 0x%x bytes @0x%x", (guint) bufsz, address);
	chunks = fu_chunk_array_new (buf, bufsz, 0x0, 0x0, FU_VLI_DEVICE_TXSIZE);
	if (chunks->len > 1) {
		for (guint i = 1; i < chunks->len; i++) {
			chk = g_ptr_array_index (chunks, i);
			if (!fu_vli_device_spi_write_verify (self,
							     chk->address + address,
							     chk->data,
							     chk->data_sz,
							     error)) {
				g_prefix_error (error, "failed to write block 0x%x: ", chk->idx);
				return FALSE;
			}
			fu_device_set_progress_full (FU_DEVICE (self),
						     (gsize) i - 1,
						     (gsize) chunks->len);
		}
	}
	chk = g_ptr_array_index (chunks, 0);
	if (!fu_vli_device_spi_write_verify (self,
					     chk->address + address,
					     chk->data,
					     chk->data_sz,
					     error)) {
		g_prefix_error (error, "failed to write CRC block: ");
		return FALSE;
	}
	fu_device_set_progress_full (FU_DEVICE (self), (gsize) chunks->len, (gsize) chunks->len);
	return TRUE;
}

static gboolean
fu_vli_device_attach (FuDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_autoptr(GError) error_local = NULL;

	/* replug, and ignore the device going away */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xf6, 0x40, 0x02,
					    NULL, 0x0, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, &error_local)) {
		if (g_error_matches (error_local,
				     G_USB_DEVICE_ERROR,
				     G_USB_DEVICE_ERROR_NO_DEVICE) ||
		    g_error_matches (error_local,
				     G_USB_DEVICE_ERROR,
				     G_USB_DEVICE_ERROR_FAILED)) {
			g_debug ("ignoring %s", error_local->message);
		} else {
			g_propagate_prefixed_error (error,
						    g_steal_pointer (&error_local),
						    "failed to restart device: ");
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_vli_device_setup (FuDevice *device, GError **error)
{
	FuVliDevice *self = FU_VLI_DEVICE (device);
	FuVliDevicePrivate *priv = GET_PRIVATE (self);
	FuVliDeviceClass *klass = FU_VLI_DEVICE_GET_CLASS (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));

	/* get the flash chip attached */
	if (!fu_vli_device_spi_read_flash_id (self, error)) {
		g_prefix_error (error, "failed to read SPI chip ID: ");
		return FALSE;
	}
	if (priv->flash_id != 0x0) {
		g_autofree gchar *spi_id = NULL;
		g_autofree gchar *devid1 = NULL;
		g_autofree gchar *devid2 = NULL;
		g_autofree gchar *flash_id = fu_vli_device_get_flash_id_str (self);
		g_debug ("using flash part %s", flash_id);

		/* load the SPI parameters from quirks */
		spi_id = g_strdup_printf ("VLI_USBHUB\\SPI_%s", flash_id);
		fu_device_add_instance_id (FU_DEVICE (self), spi_id);

		/* add extra instance IDs to include the SPI variant */
		devid2 = g_strdup_printf ("USB\\VID_%04X&PID_%04X&SPI_%s&REV_%04X",
					  g_usb_device_get_vid (usb_device),
					  g_usb_device_get_pid (usb_device),
					  flash_id,
					  g_usb_device_get_release (usb_device));
		fu_device_add_instance_id (device, devid2);
		devid1 = g_strdup_printf ("USB\\VID_%04X&PID_%04X&SPI_%s",
					  g_usb_device_get_vid (usb_device),
					  g_usb_device_get_pid (usb_device),
					  flash_id);
		fu_device_add_instance_id (device, devid1);

	}

	/* subclassed further */
	if (klass->setup != NULL)
		return klass->setup (self, error);

	/* success */
	return TRUE;
}

static gboolean
fu_vli_device_set_quirk_kv (FuDevice *device,
			    const gchar *key,
			    const gchar *value,
			    GError **error)
{
	FuVliDevice *self = FU_VLI_DEVICE (device);
	FuVliDevicePrivate *priv = GET_PRIVATE (self);
	if (g_strcmp0 (key, "SpiCmdReadId") == 0) {
		priv->spi_cmd_read_id = fu_common_strtoull (value);
		return TRUE;
	}
	if (g_strcmp0 (key, "SpiCmdReadIdSz") == 0) {
		priv->spi_cmd_read_id_sz = fu_common_strtoull (value);
		return TRUE;
	}
	if (g_strcmp0 (key, "SpiCmdChipErase") == 0) {
		priv->spi_cmd_chip_erase = fu_common_strtoull (value);
		return TRUE;
	}
	if (g_strcmp0 (key, "SpiCmdSectorErase") == 0) {
		priv->spi_cmd_sector_erase = fu_common_strtoull (value);
		return TRUE;
	}
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "quirk key not supported");
	return FALSE;
}

static void
fu_vli_device_init (FuVliDevice *self)
{
	FuVliDevicePrivate *priv = GET_PRIVATE (self);
	priv->spi_cmd_write_status	= 0x01;
	priv->spi_cmd_page_prog		= 0x02;
	priv->spi_cmd_read_data		= 0x03;
	priv->spi_cmd_read_status	= 0x05;
	priv->spi_cmd_write_en		= 0x06;
	priv->spi_cmd_sector_erase	= 0x20;
	priv->spi_cmd_chip_erase	= 0x60;
	priv->spi_cmd_read_id		= 0x9f;
	priv->spi_cmd_read_id_sz = 2;
}

static void
fu_vli_device_class_init (FuVliDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->to_string = fu_vli_device_to_string;
	klass_device->set_quirk_kv = fu_vli_device_set_quirk_kv;
	klass_device->attach = fu_vli_device_attach;
	klass_device->setup = fu_vli_device_setup;
}
