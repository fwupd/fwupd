/*
 * Copyright (C) 2017-2019 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-vli-device.h"

typedef struct {
	FuUsbDevice		 parent_instance;
	FuVliDeviceKind		 kind;
	FuVliDeviceSpiReq	 spi_cmds[FU_VLI_DEVICE_SPI_REQ_LAST];
	guint8			 spi_cmd_read_id_sz;
	guint32			 flash_id;
} FuVliDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuVliDevice, fu_vli_device, FU_TYPE_USB_DEVICE)

#define GET_PRIVATE(o) (fu_vli_device_get_instance_private (o))

static const gchar *
fu_vli_device_spi_req_to_string (FuVliDeviceSpiReq req)
{
	if (req == FU_VLI_DEVICE_SPI_REQ_READ_ID)
		return "SpiCmdReadId";
	if (req == FU_VLI_DEVICE_SPI_REQ_PAGE_PROG)
		return "SpiCmdPageProg";
	if (req == FU_VLI_DEVICE_SPI_REQ_CHIP_ERASE)
		return "SpiCmdChipErase";
	if (req == FU_VLI_DEVICE_SPI_REQ_READ_DATA)
		return "SpiCmdReadData";
	if (req == FU_VLI_DEVICE_SPI_REQ_READ_STATUS)
		return "SpiCmdReadStatus";
	if (req == FU_VLI_DEVICE_SPI_REQ_SECTOR_ERASE)
		return "SpiCmdSectorErase";
	if (req == FU_VLI_DEVICE_SPI_REQ_WRITE_EN)
		return "SpiCmdWriteEn";
	if (req == FU_VLI_DEVICE_SPI_REQ_WRITE_STATUS)
		return "SpiCmdWriteStatus";
	return NULL;
}

gboolean
fu_vli_device_get_spi_cmd (FuVliDevice *self,
			   FuVliDeviceSpiReq req,
			   guint8 *cmd,
			   GError **error)
{
	FuVliDevicePrivate *priv = GET_PRIVATE (self);
	if (req >= FU_VLI_DEVICE_SPI_REQ_LAST) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "SPI req invalid");
		return FALSE;
	}
	if (priv->spi_cmds[req] == 0x0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "No defined SPI cmd for %s",
			     fu_vli_device_spi_req_to_string (req));
		return FALSE;
	}
	if (cmd != NULL)
		*cmd = priv->spi_cmds[req];
	return TRUE;
}

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
	for (guint i = 0; i < FU_VLI_DEVICE_SPI_REQ_LAST; i++) {
		fu_common_string_append_kx (str, idt,
					    fu_vli_device_spi_req_to_string (i),
					    priv->spi_cmds[i]);
	}

	/* subclassed further */
	if (klass->to_string != NULL)
		return klass->to_string (self, idt, str);
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
					    priv->spi_cmds[FU_VLI_DEVICE_SPI_REQ_READ_ID],
					    0x0000, buf, sizeof(buf), NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to read chip ID: ");
		return FALSE;
	}
	if (g_getenv ("FWUPD_VLI_USBHUB_VERBOSE") != NULL)
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
		priv->spi_cmds[FU_VLI_DEVICE_SPI_REQ_READ_ID] = fu_common_strtoull (value);
		return TRUE;
	}
	if (g_strcmp0 (key, "SpiCmdReadIdSz") == 0) {
		priv->spi_cmd_read_id_sz = fu_common_strtoull (value);
		return TRUE;
	}
	if (g_strcmp0 (key, "SpiCmdChipErase") == 0) {
		priv->spi_cmds[FU_VLI_DEVICE_SPI_REQ_CHIP_ERASE] = fu_common_strtoull (value);
		return TRUE;
	}
	if (g_strcmp0 (key, "SpiCmdSectorErase") == 0) {
		priv->spi_cmds[FU_VLI_DEVICE_SPI_REQ_SECTOR_ERASE] = fu_common_strtoull (value);
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
	priv->spi_cmds[FU_VLI_DEVICE_SPI_REQ_WRITE_STATUS]	= 0x01;
	priv->spi_cmds[FU_VLI_DEVICE_SPI_REQ_PAGE_PROG]		= 0x02;
	priv->spi_cmds[FU_VLI_DEVICE_SPI_REQ_READ_DATA]		= 0x03;
	priv->spi_cmds[FU_VLI_DEVICE_SPI_REQ_READ_STATUS]	= 0x05;
	priv->spi_cmds[FU_VLI_DEVICE_SPI_REQ_WRITE_EN]		= 0x06;
	priv->spi_cmds[FU_VLI_DEVICE_SPI_REQ_SECTOR_ERASE]	= 0x20;
	priv->spi_cmds[FU_VLI_DEVICE_SPI_REQ_CHIP_ERASE]	= 0x60;
	priv->spi_cmds[FU_VLI_DEVICE_SPI_REQ_READ_ID]		= 0x9f;
	priv->spi_cmd_read_id_sz = 2;
}

static void
fu_vli_device_class_init (FuVliDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->to_string = fu_vli_device_to_string;
	klass_device->set_quirk_kv = fu_vli_device_set_quirk_kv;
	klass_device->setup = fu_vli_device_setup;
}
