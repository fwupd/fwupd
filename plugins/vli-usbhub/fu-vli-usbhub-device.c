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

#include "fu-vli-usbhub-common.h"
#include "fu-vli-usbhub-device.h"
#include "fu-vli-usbhub-firmware.h"

#define FU_VLI_USBHUB_DEVICE_TIMEOUT		3000	/* ms */
#define FU_VLI_USBHUB_TXSIZE			0x20	/* bytes */

struct _FuVliUsbhubDevice
{
	FuUsbDevice		 parent_instance;
	FuVliUsbhubDeviceKind	 kind;
	gboolean		 disable_powersave;
	guint8			 update_protocol;
	FuVliUsbhubHeader	 hd1_hdr;	/* factory */
	FuVliUsbhubHeader	 hd2_hdr;	/* update */
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
};

G_DEFINE_TYPE (FuVliUsbhubDevice, fu_vli_usbhub_device, FU_TYPE_USB_DEVICE)

static gchar *
fu_vli_usbhub_device_get_flash_id_str (FuVliUsbhubDevice *self)
{
	if (self->spi_cmd_read_id_sz == 4)
		return g_strdup_printf ("%08X", self->flash_id);
	if (self->spi_cmd_read_id_sz == 2)
		return g_strdup_printf ("%04X", self->flash_id);
	if (self->spi_cmd_read_id_sz == 1)
		return g_strdup_printf ("%02X", self->flash_id);
	return g_strdup_printf ("%X", self->flash_id);
}

static void
fu_vli_usbhub_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuVliUsbhubDevice *self = FU_VLI_USBHUB_DEVICE (device);
	fu_common_string_append_kv (str, idt, "DeviceKind",
				    fu_vli_usbhub_device_kind_to_string (self->kind));
	fu_common_string_append_kb (str, idt, "DisablePowersave", self->disable_powersave);
	fu_common_string_append_kx (str, idt, "UpdateProtocol", self->update_protocol);
	if (self->flash_id != 0x0) {
		g_autofree gchar *tmp = fu_vli_usbhub_device_get_flash_id_str (self);
		fu_common_string_append_kv (str, idt, "FlashId", tmp);
	}
	fu_common_string_append_kx (str, idt, "SpiCmdReadId", self->spi_cmd_read_id);
	fu_common_string_append_kx (str, idt, "SpiCmdReadIdSz", self->spi_cmd_read_id_sz);
	fu_common_string_append_kx (str, idt, "SpiCmdChipErase", self->spi_cmd_chip_erase);
	fu_common_string_append_kx (str, idt, "SpiCmdPageProg", self->spi_cmd_page_prog);
	fu_common_string_append_kx (str, idt, "SpiCmdReadData", self->spi_cmd_read_data);
	fu_common_string_append_kx (str, idt, "SpiCmdSectorErase", self->spi_cmd_sector_erase);
	fu_common_string_append_kx (str, idt, "SpiCmdWriteEn", self->spi_cmd_write_en);
	fu_common_string_append_kx (str, idt, "SpiCmdWriteStatus", self->spi_cmd_write_status);
	if (self->update_protocol >= 0x2) {
		fu_common_string_append_kv (str, idt, "H1Hdr@0x0", NULL);
		fu_vli_usbhub_header_to_string (&self->hd1_hdr, idt + 1, str);
		fu_common_string_append_kv (str, idt, "H2Hdr@0x1000", NULL);
		fu_vli_usbhub_header_to_string (&self->hd2_hdr, idt + 1, str);
	}
}

static gboolean
fu_vli_usbhub_device_vdr_unlock_813 (FuVliUsbhubDevice *self, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0x85, 0x8786, 0x8988,
					    NULL, 0x0, NULL,
					    FU_VLI_USBHUB_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to UnLock_VL813: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_vdr_read_register (FuVliUsbhubDevice *self,
					guint8 fun_num,
					guint16 offset,
					guint8 *buf,
					GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    fun_num, offset, 0x0,
					    buf, 0x1, NULL,
					    FU_VLI_USBHUB_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error,
				"failed to read VDR register 0x%x offset 0x%x: ",
				fun_num, offset);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_vdr_write_register (FuVliUsbhubDevice *self,
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
					    FU_VLI_USBHUB_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error,
				"failed to write VDR register 0x%x offset 0x%x value 0x%x: ",
				fun_num, offset, value);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_spi_read_flash_id (FuVliUsbhubDevice *self, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	guint8 buf[4] = { 0x0 };
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xc0 | (self->spi_cmd_read_id_sz * 2),
					    self->spi_cmd_read_id, 0x0000,
					    buf, sizeof(buf), NULL,
					    FU_VLI_USBHUB_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to read chip ID: ");
		return FALSE;
	}
	if (g_getenv ("FWUPD_VLI_USBHUB_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "SpiCmdReadId", buf, sizeof(buf));
	if (self->spi_cmd_read_id_sz == 4)
		self->flash_id = fu_common_read_uint32 (buf, G_BIG_ENDIAN);
	else if (self->spi_cmd_read_id_sz == 2)
		self->flash_id = fu_common_read_uint16 (buf, G_BIG_ENDIAN);
	else if (self->spi_cmd_read_id_sz == 1)
		self->flash_id = buf[0];
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_spi_read_status (FuVliUsbhubDevice *self, guint8 *status, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));

	/* sanity check */
	if (self->spi_cmd_read_status == 0x0) {
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
					    0xc1, self->spi_cmd_read_status, 0x0000,
					    status, 0x1, NULL,
					    FU_VLI_USBHUB_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to read status: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_spi_read_data (FuVliUsbhubDevice *self,
				    guint32 data_addr,
				    guint8 *buf,
				    gsize length,
				    GError **error)
{
	guint16 index = ((data_addr << 8) & 0xff00) | ((data_addr >> 8) & 0x00ff);
	guint16 value = ((data_addr >> 8) & 0xff00) | self->spi_cmd_read_data;
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));

	/* sanity check */
	if (self->spi_cmd_read_data == 0x0) {
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
					    FU_VLI_USBHUB_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to read SPI data @0x%x: ", data_addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_spi_write_status (FuVliUsbhubDevice *self, guint8 status, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));

	/* sanity check */
	if (self->spi_cmd_write_status == 0x0) {
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
					    0xd1, self->spi_cmd_write_status, 0x0000,
					    &status, 0x1, NULL,
					    FU_VLI_USBHUB_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to write SPI status 0x%x", status);
		return FALSE;
	}

	/* Fix_For_GD_&_EN_SPI_Flash */
	g_usleep (100 * 1000);
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_spi_write_enable (FuVliUsbhubDevice *self, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));

	/* sanity check */
	if (self->spi_cmd_write_en == 0x0) {
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
					    0xd1, self->spi_cmd_write_en, 0x0000,
					    NULL, 0x0, NULL,
					    FU_VLI_USBHUB_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to write enable SPI: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_spi_erase_chip (FuVliUsbhubDevice *self, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));

	/* sanity check */
	if (self->spi_cmd_chip_erase == 0x0) {
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
					    0xd1, self->spi_cmd_chip_erase, 0x0000,
					    NULL, 0x0, NULL,
					    FU_VLI_USBHUB_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to erase SPI: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_spi_erase_sector (FuVliUsbhubDevice *self, guint32 data_addr, GError **error)
{
	guint16 index = ((data_addr << 8) & 0xff00) | ((data_addr >> 8) & 0x00ff);
	guint16 value = ((data_addr >> 8) & 0xff00) | self->spi_cmd_sector_erase;
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));

	/* sanity check */
	if (self->spi_cmd_sector_erase == 0x0) {
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
					    FU_VLI_USBHUB_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to erase SPI sector @0x%x: ", data_addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_spi_write_data (FuVliUsbhubDevice *self,
				     guint32 data_addr,
				     const guint8 *buf,
				     gsize length,
				     GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	guint16 value = ((data_addr >> 8) & 0xff00) | self->spi_cmd_page_prog;
	guint16 index = ((data_addr << 8) & 0xff00) | ((data_addr >> 8) & 0x00ff);

	/* sanity check */
	if (self->spi_cmd_page_prog == 0x0) {
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
					    FU_VLI_USBHUB_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to write SPI @0x%x: ", data_addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_spi_wait_finish (FuVliUsbhubDevice *self, GError **error)
{
	const guint32 rdy_cnt = 2;
	guint32 cnt = 0;

	/* sanity check */
	if (self->spi_cmd_read_status == 0x0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "No value for SpiCmdReadStatus");
		return FALSE;
	}
	for (guint32 idx = 0; idx < 1000; idx++) {
		guint8 status = 0x7f;

		/* must get bit[1:0] == 0 twice in a row for success */
		if (!fu_vli_usbhub_device_spi_read_status (self, &status, error))
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

static gboolean
fu_vli_usbhub_device_erase_sector (FuVliUsbhubDevice *self, guint32 addr, GError **error)
{
	const guint32 bufsz = 0x1000;

	/* erase sector */
	if (!fu_vli_usbhub_device_spi_write_enable (self, error)) {
		g_prefix_error (error, "fu_vli_usbhub_device_spi_write_enable failed: ");
		return FALSE;
	}
	if (!fu_vli_usbhub_device_spi_write_status (self, 0x00, error)) {
		g_prefix_error (error, "fu_vli_usbhub_device_spi_write_status failed: ");
		return FALSE;
	}
	if (!fu_vli_usbhub_device_spi_write_enable (self, error)) {
		g_prefix_error (error, "fu_vli_usbhub_device_spi_write_enable failed: ");
		return FALSE;
	}
	if (!fu_vli_usbhub_device_spi_erase_sector (self, addr, error)) {
		g_prefix_error (error, "fu_vli_usbhub_device_spi_erase_sector failed");
		return FALSE;
	}
	if (!fu_vli_usbhub_device_spi_wait_finish (self, error)) {
		g_prefix_error (error, "fu_vli_usbhub_device_spi_wait_finish failed");
		return FALSE;
	}

	/* verify it really was blanked */
	for (guint32 offset = 0; offset < bufsz; offset += FU_VLI_USBHUB_TXSIZE) {
		guint8 buf[FU_VLI_USBHUB_TXSIZE] = { 0x0 };
		if (!fu_vli_usbhub_device_spi_read_data (self,
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

static gboolean
fu_vli_usbhub_device_erase_sectors (FuVliUsbhubDevice *self,
				    guint32 addr,
				    gsize sz,
				    GError **error)
{
	g_autoptr(GPtrArray) chunks = fu_chunk_array_new (NULL, sz, addr, 0x0, 0x1000);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chunk = g_ptr_array_index (chunks, i);
		g_debug ("erasing @0x%x", chunk->address);
		if (!fu_vli_usbhub_device_erase_sector (self, chunk->address, error)) {
			g_prefix_error (error,
					"failed to erase FW sector @0x%x",
					chunk->address);
			return FALSE;
		}
		fu_device_set_progress_full (FU_DEVICE (self),
					     (gsize) i, (gsize) chunks->len);
	}
	return TRUE;
}

/* disable hub sleep states -- not really required by 815~ hubs */
static gboolean
fu_vli_usbhub_device_disable_u1u2 (FuVliUsbhubDevice *self, GError **error)
{
	guint8 fun_num, buf;
	guint16 offset;

	/* clear Reg[0xF8A2] bit_3 & bit_7 -- also
	 * clear Total Switch / Flag To Disable FW Auto-Reload Function */
	fun_num = 0xf8;
	offset = 0xa2;
	if (!fu_vli_usbhub_device_vdr_read_register (self, fun_num, offset, &buf, error)) {
		g_prefix_error (error, "reg offset 0x%x: ", offset);
		return FALSE;
	}
	buf &= 0x77;
	if (!fu_vli_usbhub_device_vdr_write_register (self, fun_num, offset, buf, error)) {
		g_prefix_error (error, "reg offset 0x%x: ", offset);
		return FALSE;
	}

	/* clear Reg[0xF832] bit_0 & bit_1 */
	fun_num = 0xf8;
	offset = 0x32;
	if (!fu_vli_usbhub_device_vdr_read_register (self, fun_num, offset, &buf, error)) {
		g_prefix_error (error, "reg offset 0x%x: ", offset);
		return FALSE;
	}
	buf &= 0xfc;
	if (!fu_vli_usbhub_device_vdr_write_register (self, fun_num, offset, buf, error)) {
		g_prefix_error (error, "reg offset 0x%x: ", offset);
		return FALSE;
	}

	/* clear Reg[0xF920] bit_1 & bit_2 */
	fun_num = 0xf9;
	offset = 0x20;
	if (!fu_vli_usbhub_device_vdr_read_register (self, fun_num, offset, &buf, error)) {
		g_prefix_error (error, "reg offset 0x%x: ", offset);
		return FALSE;
	}
	buf &= 0xf9;
	if (!fu_vli_usbhub_device_vdr_write_register (self, fun_num, offset, buf, error)) {
		g_prefix_error (error, "reg offset 0x%x: ", offset);
		return FALSE;
	}

	/* set Reg[0xF836] bit_3 */
	fun_num = 0xf8;
	offset = 0x36;
	if (!fu_vli_usbhub_device_vdr_read_register (self, fun_num, offset, &buf, error)) {
		g_prefix_error (error, "reg offset 0x%x: ", offset);
		return FALSE;
	}
	buf |= 0x08;
	if (!fu_vli_usbhub_device_vdr_write_register (self, fun_num, offset, buf, error)) {
		g_prefix_error (error, "reg offset 0x%x: ", offset);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_guess_kind (FuVliUsbhubDevice *self, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	guint8 b811P812 = 0x0;
	guint8 b820Q7Q8 = 0x0;
	guint8 chipid1 = 0x0;
	guint8 chipid2 = 0x0;
	guint8 chipid12 = 0x0;
	guint8 chipid22 = 0x0;
	guint8 chipver = 0x0;
	guint8 chipver2 = 0x0;
	gint tPid = g_usb_device_get_pid (usb_device) & 0x0fff;

	if (!fu_vli_usbhub_device_vdr_read_register (self, 0xf8, 0x8c, &chipver, error)) {
		g_prefix_error (error, "Read_ChipVer failed: ");
		return FALSE;
	}
	if (!fu_vli_usbhub_device_vdr_read_register (self, 0xf6, 0x3f, &chipver2, error)) {
		g_prefix_error (error, "Read_ChipVer2 failed: ");
		return FALSE;
	}
	if (!fu_vli_usbhub_device_vdr_read_register (self, 0xf8, 0x00, &b811P812, error)) {
		g_prefix_error (error, "Read_811P812 failed: ");
		return FALSE;
	}
	if (!fu_vli_usbhub_device_vdr_read_register (self, 0xf8, 0x8e, &chipid1, error)) {
		g_prefix_error (error, "Read_ChipID1 failed: ");
		return FALSE;
	}
	if (!fu_vli_usbhub_device_vdr_read_register (self, 0xf8, 0x8f, &chipid2, error)) {
		g_prefix_error (error, "Read_ChipID2 failed: ");
		return FALSE;
	}
	if (!fu_vli_usbhub_device_vdr_read_register (self, 0xf6, 0x4e, &chipid12, error)) {
		g_prefix_error (error, "Read_ChipID12 failed: ");
		return FALSE;
	}
	if (!fu_vli_usbhub_device_vdr_read_register (self, 0xf6, 0x4f, &chipid22, error)) {
		g_prefix_error (error, "Read_ChipID22 failed: ");
		return FALSE;
	}
	if (!fu_vli_usbhub_device_vdr_read_register (self, 0xf6, 0x51, &b820Q7Q8, error)) {
		g_prefix_error (error, "Read_820Q7Q8 failed: ");
		return FALSE;
	}
	g_debug ("chipver = 0x%02x", chipver);
	g_debug ("chipver2 = 0x%02x", chipver2);
	g_debug ("b811P812 = 0x%02x", b811P812);
	g_debug ("chipid1 = 0x%02x", chipid1);
	g_debug ("chipid2 = 0x%02x", chipid2);
	g_debug ("chipid12 = 0x%02x", chipid12);
	g_debug ("chipid22 = 0x%02x", chipid22);
	g_debug ("b820Q7Q8 = 0x%02x", b820Q7Q8);

	if (chipid2 == 0x35 && chipid1 == 0x07) {
		self->kind = FU_VLI_USBHUB_DEVICE_KIND_VL210;
	} else if (chipid2 == 0x35 && chipid1 == 0x18) {
		if (b820Q7Q8 & (1 << 2))
			self->kind = FU_VLI_USBHUB_DEVICE_KIND_VL820Q8;
		else
			self->kind = FU_VLI_USBHUB_DEVICE_KIND_VL820Q7;
	} else if (chipid2 == 0x35 && chipid1 == 0x31) {
		self->kind = FU_VLI_USBHUB_DEVICE_KIND_VL815;
	} else if (chipid2 == 0x35 && chipid1 == 0x38) {
		self->kind = FU_VLI_USBHUB_DEVICE_KIND_VL817;
	} else if (chipid2 == 0x35 && chipid1 == 0x45) {
		self->kind = FU_VLI_USBHUB_DEVICE_KIND_VL211;
	} else if (chipid22 == 0x35 && chipid12 == 0x53) {
		self->kind = FU_VLI_USBHUB_DEVICE_KIND_VL120;
	} else if (chipid2 == 0x35 && chipid1 == 0x57) {
		self->kind = FU_VLI_USBHUB_DEVICE_KIND_VL819;
	} else if (tPid == 0x810) {
		self->kind = FU_VLI_USBHUB_DEVICE_KIND_VL810;
	} else if (tPid == 0x811) {
		self->kind = FU_VLI_USBHUB_DEVICE_KIND_VL811;
	} else if ((b811P812 & ((1 << 5) | (1 << 4))) == 0) {
		if (chipver == 0x10)
			self->kind = FU_VLI_USBHUB_DEVICE_KIND_VL811PB0;
		else
			self->kind = FU_VLI_USBHUB_DEVICE_KIND_VL811PB3;
	} else if ((b811P812 & ((1 << 5) | (1 << 4))) == (1 << 4)) {
		self->kind = FU_VLI_USBHUB_DEVICE_KIND_VL812Q4S;
	} else if ((b811P812 & ((1 << 5) | (1 << 4))) == ((1 << 5) | (1 << 4))) {
		if (chipver == 0x10)
			self->kind = FU_VLI_USBHUB_DEVICE_KIND_VL812B0;
		else
			self->kind = FU_VLI_USBHUB_DEVICE_KIND_VL812B3;
	} else {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "hardware is not supported");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_vli_usbhub_device_dump_firmware (FuVliUsbhubDevice *self, gsize bufsz, GError **error)
{
	g_autofree guint8 *buf = g_malloc0 (bufsz);
	g_autoptr(GPtrArray) chunks = NULL;

	/* get data from hardware */
	chunks = fu_chunk_array_new (buf, bufsz, 0x0, 0x0, FU_VLI_USBHUB_TXSIZE);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		if (!fu_vli_usbhub_device_spi_read_data (self,
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

static gboolean
fu_vli_usbhub_device_probe (FuDevice *device, GError **error)
{
	/* quirks now applied... */
	if (fu_device_has_custom_flag (device, "usb3")) {
		fu_device_set_summary (device, "USB 3.x Hub");
	} else if (fu_device_has_custom_flag (device, "usb2")) {
		fu_device_set_summary (device, "USB 2.x Hub");
	} else {
		fu_device_set_summary (device, "USB Hub");
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_setup (FuDevice *device, GError **error)
{
	FuVliUsbhubDevice *self = FU_VLI_USBHUB_DEVICE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	g_autoptr(GError) error_tmp = NULL;

	/* try to read a block of data which will fail for 813-type devices */
	if (fu_device_has_custom_flag (device, "needs-unlock-legacy813") &&
	    !fu_vli_usbhub_device_spi_read_data (self, 0x0, (guint8 *) &self->hd1_hdr,
						 sizeof(self->hd1_hdr), &error_tmp)) {
		g_warning ("failed to read, trying to unlock 813: %s", error_tmp->message);
		if (!fu_vli_usbhub_device_vdr_unlock_813 (self, error))
			return FALSE;
		if (!fu_vli_usbhub_device_spi_read_data (self, 0x0, (guint8 *) &self->hd1_hdr,
							 sizeof(self->hd1_hdr), error)) {
			g_prefix_error (error, "813 unlock fail: ");
			return FALSE;
		}
		g_debug ("813 unlock OK");
		/* VL813 & VL210 have same PID (0x0813), and only VL813 can reply */
		self->kind = FU_VLI_USBHUB_DEVICE_KIND_VL813;
	} else {
		if (!fu_vli_usbhub_device_guess_kind (self, error))
			return FALSE;
	}

	/* get the flash chip attached */
	if (!fu_vli_usbhub_device_spi_read_flash_id (self, error)) {
		g_prefix_error (error, "failed to read SPI chip ID: ");
		return FALSE;
	}
	if (self->flash_id != 0x0) {
		g_autofree gchar *spi_id = NULL;
		g_autofree gchar *devid1 = NULL;
		g_autofree gchar *devid2 = NULL;
		g_autofree gchar *flash_id = fu_vli_usbhub_device_get_flash_id_str (self);
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

	/* read HD1 (factory) header */
	if (!fu_vli_usbhub_device_spi_read_data (self, VLI_USBHUB_FLASHMAP_ADDR_HD1,
						 (guint8 *) &self->hd1_hdr,
						 sizeof(self->hd1_hdr), error)) {
		g_prefix_error (error, "failed to read HD1 header");
		return FALSE;
	}

	/* detect update protocol from the device ID */
	switch (GUINT16_FROM_BE(self->hd1_hdr.dev_id) >> 8) {
	/* VL810~VL813 */
	case 0x0d:
		self->update_protocol = 0x1;
		self->disable_powersave = TRUE;
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
		fu_device_set_install_duration (FU_DEVICE (self), 10); /* seconds */
		break;
	/* VL817~ */
	case 0x05:
		self->update_protocol = 0x2;
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
		fu_device_set_install_duration (FU_DEVICE (self), 15); /* seconds */
		break;
	default:
		g_warning ("unknown update protocol, device_id=0x%x",
			   GUINT16_FROM_BE(self->hd1_hdr.dev_id));
		break;
	}

	/* read HD2 (update) header */
	if (self->update_protocol >= 0x2) {
		if (!fu_vli_usbhub_device_spi_read_data (self, VLI_USBHUB_FLASHMAP_ADDR_HD2,
							 (guint8 *) &self->hd2_hdr,
							 sizeof(self->hd2_hdr), error)) {
			g_prefix_error (error, "failed to read HD2 header");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static FuFirmware *
fu_vli_usbhub_device_prepare_firmware (FuDevice *device,
				       GBytes *fw,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuVliUsbhubDevice *self = FU_VLI_USBHUB_DEVICE (device);
	FuVliUsbhubDeviceKind device_kind;
	guint16 device_id;
	g_autoptr(FuFirmware) firmware = fu_vli_usbhub_firmware_new ();

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
	if (g_bytes_get_size (fw) > fu_device_get_firmware_size_max (device)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "firmware too large, got 0x%x, expected <= 0x%x",
			     (guint) g_bytes_get_size (fw),
			     (guint) fu_device_get_firmware_size_max (device));
		return NULL;
	}

	/* check is compatible with firmware */
	fu_device_set_status (device, FWUPD_STATUS_DECOMPRESSING);
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;
	device_kind = fu_vli_usbhub_firmware_get_device_kind (FU_VLI_USBHUB_FIRMWARE (firmware));
	if (self->kind != device_kind) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "firmware incompatible, got %s, expected %s",
			     fu_vli_usbhub_device_kind_to_string (device_kind),
			     fu_vli_usbhub_device_kind_to_string (self->kind));
		return NULL;
	}
	device_id = fu_vli_usbhub_firmware_get_device_id (FU_VLI_USBHUB_FIRMWARE (firmware));
	if (GUINT16_FROM_BE(self->hd1_hdr.dev_id) != device_id) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "firmware incompatible, got 0x%04x, expected 0x%04x",
			     device_id, GUINT16_FROM_BE(self->hd1_hdr.dev_id));
		return NULL;
	}

	/* we could check this against flags */
	g_debug ("parsed version: %s", fu_firmware_get_version (firmware));
	return g_steal_pointer (&firmware);
}

static gboolean
fu_vli_usbhub_device_erase_all (FuVliUsbhubDevice *self, GError **error)
{
	if (!fu_vli_usbhub_device_spi_write_enable (self, error))
		return FALSE;
	if (!fu_vli_usbhub_device_spi_write_status (self, 0x00, error))
		return FALSE;
	if (!fu_vli_usbhub_device_spi_write_enable (self, error))
		return FALSE;
	if (!fu_vli_usbhub_device_spi_erase_chip (self, error))
		return FALSE;
	g_usleep (4 * G_USEC_PER_SEC);

	/* verify chip was erased */
	for (guint addr = 0; addr < 0x10000; addr += 0x1000) {
		guint8 buf[FU_VLI_USBHUB_TXSIZE] = { 0x0 };
		if (!fu_vli_usbhub_device_spi_read_data (self, addr, buf, sizeof(buf), error)) {
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

static gboolean
fu_vli_usbhub_device_write_block (FuVliUsbhubDevice *self,
				  guint32 address,
				  const guint8 *buf,
				  gsize bufsz,
				  GError **error)
{
	g_autofree guint8 *buf_tmp = g_malloc0 (bufsz);

	/* sanity check */
	if (bufsz > FU_VLI_USBHUB_TXSIZE) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "cannot write 0x%x in one block",
			     (guint) bufsz);
		return FALSE;
	}

	/* write */
	if (!fu_vli_usbhub_device_spi_write_enable (self, error)) {
		g_prefix_error (error, "enabling SPI write failed: ");
		return FALSE;
	}
	if (!fu_vli_usbhub_device_spi_write_data (self, address, buf, bufsz, error)) {
		g_prefix_error (error, "SPI data write failed: ");
		return FALSE;
	}
	g_usleep (800);

	/* verify */
	if (!fu_vli_usbhub_device_spi_read_data (self, address, buf_tmp, bufsz, error)) {
		g_prefix_error (error, "SPI data read failed: ");
		return FALSE;
	}
	return fu_common_bytes_compare_raw (buf, bufsz, buf_tmp, bufsz, error);
}

static gboolean
fu_vli_usbhub_device_write_blocks (FuVliUsbhubDevice *self,
				   guint32 address,
				   const guint8 *buf,
				   gsize bufsz,
				   GError **error)
{
	FuChunk *chk;
	g_autoptr(GPtrArray) chunks = NULL;

	/* write SPI data, then CRC bytes last */
	chunks = fu_chunk_array_new (buf, bufsz, 0x0, 0x0, FU_VLI_USBHUB_TXSIZE);
	if (chunks->len > 1) {
		for (guint i = 1; i < chunks->len; i++) {
			chk = g_ptr_array_index (chunks, i);
			if (!fu_vli_usbhub_device_write_block (self,
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
	if (!fu_vli_usbhub_device_write_block (self,
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
fu_vli_usbhub_device_update_v1 (FuVliUsbhubDevice *self,
				FuFirmware *firmware,
				GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf;
	g_autoptr(GBytes) fw = NULL;

	/* simple image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* erase */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_ERASE);
	if (!fu_vli_usbhub_device_erase_all (self, error)) {
		g_prefix_error (error, "failed to erase chip: ");
		return FALSE;
	}

	/* write in chunks */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_WRITE);
	buf = g_bytes_get_data (fw, &bufsz);
	if (!fu_vli_usbhub_device_write_blocks (self, 0x0, buf, bufsz, error))
		return FALSE;

	/* success */
	return TRUE;
}

/* if no header1 or ROM code update, write data directly */
static gboolean
fu_vli_usbhub_device_update_v2_recovery (FuVliUsbhubDevice *self, GBytes *fw, GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data (fw, &bufsz);

	/* erase */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_ERASE);
	for (guint32 addr = 0; addr < bufsz; addr += 0x1000) {
		if (!fu_vli_usbhub_device_erase_sector (self, addr, error)) {
			g_prefix_error (error, "failed to erase sector @0x%x", addr);
			return FALSE;
		}
	}

	/* write in chunks */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_vli_usbhub_device_write_blocks (self, VLI_USBHUB_FLASHMAP_ADDR_HD1,
						buf, bufsz, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_hd1_is_valid (FuVliUsbhubHeader *hdr)
{
	if (hdr->prev_ptr != VLI_USBHUB_FLASHMAP_IDX_INVALID)
		return FALSE;
	if (hdr->checksum != fu_vli_usbhub_header_crc8 (hdr))
		return FALSE;
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_hd1_recover (FuVliUsbhubDevice *self, FuVliUsbhubHeader *hdr, GError **error)
{
	/* point to HD2, i.e. updated firmware */
	if (hdr->next_ptr != VLI_USBHUB_FLASHMAP_IDX_HD2) {
		hdr->next_ptr = VLI_USBHUB_FLASHMAP_IDX_HD2;
		hdr->checksum = fu_vli_usbhub_header_crc8 (hdr);
	}

	/* write new header block */
	if (!fu_vli_usbhub_device_erase_sector (self, VLI_USBHUB_FLASHMAP_ADDR_HD1, error)) {
		g_prefix_error (error,
				"failed to erase header1 sector at 0x%x: ",
				(guint) VLI_USBHUB_FLASHMAP_ADDR_HD1);
		return FALSE;
	}
	if (!fu_vli_usbhub_device_write_block (self, VLI_USBHUB_FLASHMAP_ADDR_HD1,
					       (const guint8 *) hdr,
					       sizeof(FuVliUsbhubHeader),
					       error)) {
		g_prefix_error (error,
				"failed to write header1 block at 0x%x: ",
				(guint) VLI_USBHUB_FLASHMAP_ADDR_HD1);
		return FALSE;
	}

	/* update the cached copy */
	memcpy (&self->hd1_hdr, hdr, sizeof(self->hd1_hdr));
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_update_v2 (FuVliUsbhubDevice *self, FuFirmware *firmware, GError **error)
{
	gsize buf_fwsz = 0;
	guint32 hd1_fw_sz;
	guint32 hd2_fw_sz;
	guint32 hd2_fw_addr;
	guint32 hd2_fw_offset;
	const guint8 *buf_fw;
	FuVliUsbhubHeader hdr = { 0x0 };
	g_autoptr(GBytes) fw = NULL;

	/* simple image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* root header is valid */
	if (fu_vli_usbhub_device_hd1_is_valid (&self->hd1_hdr)) {

		/* no update has ever been done */
		if (self->hd1_hdr.next_ptr != VLI_USBHUB_FLASHMAP_IDX_HD2) {

			/* backup HD1 before recovering */
			if (!fu_vli_usbhub_device_erase_sector (self, VLI_USBHUB_FLASHMAP_ADDR_HD2, error)) {
				g_prefix_error (error, "failed to erase sector at header 1: ");
				return FALSE;
			}
			if (!fu_vli_usbhub_device_write_block (self, VLI_USBHUB_FLASHMAP_ADDR_HD1_BACKUP,
							       (const guint8 *) &self->hd1_hdr, sizeof(hdr),
							       error)) {
				g_prefix_error (error, "failed to write block at header 1: ");
				return FALSE;
			}
			if (!fu_vli_usbhub_device_hd1_recover (self, &self->hd1_hdr, error)) {
				g_prefix_error (error, "failed to write header: ");
				return FALSE;
			}
		}

	/* copy the header from the backup zone */
	} else {
		g_debug ("HD1 was invalid, reading backup");
		if (!fu_vli_usbhub_device_spi_read_data (self, VLI_USBHUB_FLASHMAP_ADDR_HD1_BACKUP,
							 (guint8 *) &self->hd1_hdr, sizeof(hdr),
							 error)) {
			g_prefix_error (error,
					"failed to read root header from 0x%x",
					(guint) VLI_USBHUB_FLASHMAP_ADDR_HD1_BACKUP);
			return FALSE;
		}
		if (!fu_vli_usbhub_device_hd1_is_valid (&self->hd1_hdr)) {
			g_debug ("backup header is also invalid, starting recovery");
			return fu_vli_usbhub_device_update_v2_recovery (self, fw, error);
		}
		if (!fu_vli_usbhub_device_hd1_recover (self, &self->hd1_hdr, error)) {
			g_prefix_error (error, "failed to get root header in backup zone: ");
			return FALSE;
		}
	}

	/* align the update fw address to the sector after the factory size */
	hd1_fw_sz = GUINT16_FROM_BE(self->hd1_hdr.usb3_fw_sz);
	if (hd1_fw_sz > 0xF000) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "FW1 size abnormal 0x%x",
			     (guint) hd1_fw_sz);
		return FALSE;
	}
	hd2_fw_addr = (hd1_fw_sz + 0xfff) & 0xf000;
	hd2_fw_addr += VLI_USBHUB_FLASHMAP_ADDR_FW;

	/* get the size and offset of the update firmware */
	buf_fw = g_bytes_get_data (fw, &buf_fwsz);
	memcpy (&hdr, buf_fw, sizeof(hdr));
	hd2_fw_sz = GUINT16_FROM_BE(hdr.usb3_fw_sz);
	hd2_fw_offset = GUINT16_FROM_BE(hdr.usb3_fw_addr);
	g_debug ("FW2 @0x%x (length 0x%x, offset 0x%x)",
		 hd2_fw_addr, hd2_fw_sz, hd2_fw_offset);

	/* make space */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_ERASE);
	if (!fu_vli_usbhub_device_erase_sectors (self, hd2_fw_addr, hd2_fw_sz, error))
		return FALSE;

	/* perform the actual write */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_vli_usbhub_device_write_blocks (self,
						hd2_fw_addr,
						buf_fw + hd2_fw_offset,
						hd2_fw_sz,
						error)) {
		g_prefix_error (error, "failed to write payload: ");
		return FALSE;
	}

	/* map into header */
	if (!fu_memcpy_safe ((guint8 *) &self->hd2_hdr, sizeof(hdr), 0x0,
			     buf_fw, buf_fwsz, 0x0, sizeof(hdr), error)) {
		g_prefix_error (error, "failed to read header: ");
		return FALSE;
	}

	/* write new HD2 */
	self->hd2_hdr.usb3_fw_addr = GUINT16_TO_BE(hd2_fw_addr & 0xffff);
	self->hd2_hdr.usb3_fw_addr_high = (guint8) (hd2_fw_addr >> 16);
	self->hd2_hdr.prev_ptr = VLI_USBHUB_FLASHMAP_IDX_HD1;
	self->hd2_hdr.next_ptr = VLI_USBHUB_FLASHMAP_IDX_INVALID;
	self->hd2_hdr.checksum = fu_vli_usbhub_header_crc8 (&self->hd2_hdr);
	if (!fu_vli_usbhub_device_erase_sector (self, VLI_USBHUB_FLASHMAP_ADDR_HD2, error)) {
		g_prefix_error (error, "failed to erase sectors for HD2: ");
		return FALSE;
	}
	if (!fu_vli_usbhub_device_write_block (self,
					       VLI_USBHUB_FLASHMAP_ADDR_HD2,
					       (const guint8 *) &self->hd2_hdr,
					       sizeof(self->hd2_hdr),
					       error)) {
		g_prefix_error (error, "failed to write HD2: ");
		return FALSE;
	}

	/* success */
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static FuFirmware *
fu_vli_usbhub_device_read_firmware (FuDevice *device, GError **error)
{
	FuVliUsbhubDevice *self = FU_VLI_USBHUB_DEVICE (device);
	g_autoptr(GBytes) fw = NULL;
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_VERIFY);
	fw = fu_vli_usbhub_device_dump_firmware (self, fu_device_get_firmware_size_max (device), error);
	if (fw == NULL)
		return NULL;
	return fu_firmware_new_from_bytes (fw);
}

static gboolean
fu_vli_usbhub_device_write_firmware (FuDevice *device,
				     FuFirmware *firmware,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuVliUsbhubDevice *self = FU_VLI_USBHUB_DEVICE (device);

	/* disable powersaving if required */
	if (self->disable_powersave) {
		if (!fu_vli_usbhub_device_disable_u1u2 (self, error)) {
			g_prefix_error (error, "disabling powersave failed: ");
			return FALSE;
		}
	}

	/* use correct method */
	if (self->update_protocol == 0x1)
		return fu_vli_usbhub_device_update_v1 (self, firmware, error);
	if (self->update_protocol == 0x2)
		return fu_vli_usbhub_device_update_v2 (self, firmware, error);

	/* not sure what to do */
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_SUPPORTED,
		     "update protocol 0x%x not supported",
		     self->update_protocol);
	return FALSE;
}

static gboolean
fu_vli_usbhub_device_attach (FuDevice *device, GError **error)
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
					    FU_VLI_USBHUB_DEVICE_TIMEOUT,
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
fu_vli_usbhub_device_set_quirk_kv (FuDevice *device,
				   const gchar *key,
				   const gchar *value,
				   GError **error)
{
	FuVliUsbhubDevice *self = FU_VLI_USBHUB_DEVICE (device);
	if (g_strcmp0 (key, "SpiCmdReadId") == 0) {
		self->spi_cmd_read_id = fu_common_strtoull (value);
		return TRUE;
	}
	if (g_strcmp0 (key, "SpiCmdReadIdSz") == 0) {
		self->spi_cmd_read_id_sz = fu_common_strtoull (value);
		return TRUE;
	}
	if (g_strcmp0 (key, "SpiCmdChipErase") == 0) {
		self->spi_cmd_chip_erase = fu_common_strtoull (value);
		return TRUE;
	}
	if (g_strcmp0 (key, "SpiCmdSectorErase") == 0) {
		self->spi_cmd_sector_erase = fu_common_strtoull (value);
		return TRUE;
	}
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "quirk key not supported");
	return FALSE;
}

static void
fu_vli_usbhub_device_init (FuVliUsbhubDevice *self)
{
	self->spi_cmd_write_status	= 0x01;
	self->spi_cmd_page_prog		= 0x02;
	self->spi_cmd_read_data		= 0x03;
	self->spi_cmd_read_status	= 0x05;
	self->spi_cmd_write_en		= 0x06;
	self->spi_cmd_sector_erase	= 0x20;
	self->spi_cmd_chip_erase	= 0x60;
	self->spi_cmd_read_id		= 0x9f;
	self->spi_cmd_read_id_sz = 2;
	fu_device_add_icon (FU_DEVICE (self), "audio-card");
	fu_device_set_firmware_size_max (FU_DEVICE (self), 0x20000);
	fu_device_set_remove_delay (FU_DEVICE (self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
}

static void
fu_vli_usbhub_device_class_init (FuVliUsbhubDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->to_string = fu_vli_usbhub_device_to_string;
	klass_device->set_quirk_kv = fu_vli_usbhub_device_set_quirk_kv;
	klass_device->probe = fu_vli_usbhub_device_probe;
	klass_device->setup = fu_vli_usbhub_device_setup;
	klass_device->read_firmware = fu_vli_usbhub_device_read_firmware;
	klass_device->write_firmware = fu_vli_usbhub_device_write_firmware;
	klass_device->prepare_firmware = fu_vli_usbhub_device_prepare_firmware;
	klass_device->attach = fu_vli_usbhub_device_attach;
}
