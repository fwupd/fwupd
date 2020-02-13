/*
 * Copyright (C) 2017-2019 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-chunk.h"

#include "fu-vli-device.h"

typedef struct {
	FuUsbDevice		 parent_instance;
	FuVliDeviceKind		 kind;
	gboolean		 spi_auto_detect;
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

gboolean
fu_vli_device_reset (FuVliDevice *self, GError **error)
{
	FuVliDeviceClass *klass = FU_VLI_DEVICE_GET_CLASS (self);
	if (klass->reset != NULL) {
		if (!klass->reset (self, error)) {
			g_prefix_error (error, "failed to reset device: ");
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_vli_device_spi_write_enable (FuVliDevice *self, GError **error)
{
	FuVliDeviceClass *klass = FU_VLI_DEVICE_GET_CLASS (self);
	if (klass->spi_write_enable != NULL) {
		if (!klass->spi_write_enable (self, error)) {
			g_prefix_error (error, "failed to write enable SPI: ");
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_vli_device_spi_chip_erase (FuVliDevice *self, GError **error)
{
	FuVliDeviceClass *klass = FU_VLI_DEVICE_GET_CLASS (self);
	if (klass->spi_chip_erase != NULL) {
		if (!klass->spi_chip_erase (self, error)) {
			g_prefix_error (error, "failed to erase SPI data: ");
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_vli_device_spi_write_status (FuVliDevice *self, guint8 status, GError **error)
{
	FuVliDeviceClass *klass = FU_VLI_DEVICE_GET_CLASS (self);
	if (klass->spi_write_status != NULL) {
		if (!klass->spi_write_status (self, status, error)) {
			g_prefix_error (error, "failed to write SPI status 0x%x: ", status);
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_vli_device_spi_read_status (FuVliDevice *self, guint8 *status, GError **error)
{
	FuVliDeviceClass *klass = FU_VLI_DEVICE_GET_CLASS (self);
	if (klass->spi_read_status != NULL) {
		if (!klass->spi_read_status (self, status, error)) {
			g_prefix_error (error, "failed to read status: ");
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_vli_device_spi_sector_erase (FuVliDevice *self, guint32 addr, GError **error)
{
	FuVliDeviceClass *klass = FU_VLI_DEVICE_GET_CLASS (self);
	if (klass->spi_sector_erase != NULL) {
		if (!klass->spi_sector_erase (self, addr, error)) {
			g_prefix_error (error, "failed to erase SPI data @0x%x: ", addr);
			return FALSE;
		}
	}
	return TRUE;
}

gboolean
fu_vli_device_spi_read_block (FuVliDevice *self, guint32 addr,
			      guint8 *buf, gsize bufsz, GError **error)
{
	FuVliDeviceClass *klass = FU_VLI_DEVICE_GET_CLASS (self);
	if (klass->spi_read_data != NULL) {
		if (!klass->spi_read_data (self, addr, buf, bufsz, error)) {
			g_prefix_error (error, "failed to read SPI data @0x%x: ", addr);
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_vli_device_spi_write_data (FuVliDevice *self, guint32 addr,
			      const guint8 *buf, gsize bufsz, GError **error)
{
	FuVliDeviceClass *klass = FU_VLI_DEVICE_GET_CLASS (self);
	if (klass->spi_write_data != NULL) {
		if (!klass->spi_write_data (self, addr, buf, bufsz, error)) {
			g_prefix_error (error, "failed to write SPI data @0x%x: ", addr);
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_vli_device_spi_wait_finish (FuVliDevice *self, GError **error)
{
	const guint32 rdy_cnt = 2;
	guint32 cnt = 0;

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
fu_vli_device_spi_erase_sector (FuVliDevice *self, guint32 addr, GError **error)
{
	const guint32 bufsz = 0x1000;

	/* erase sector */
	if (!fu_vli_device_spi_write_enable (self, error)) {
		g_prefix_error (error, "->spi_write_enable failed: ");
		return FALSE;
	}
	if (!fu_vli_device_spi_write_status (self, 0x00, error)) {
		g_prefix_error (error, "->spi_write_status failed: ");
		return FALSE;
	}
	if (!fu_vli_device_spi_write_enable (self, error)) {
		g_prefix_error (error, "->spi_write_enable failed: ");
		return FALSE;
	}
	if (!fu_vli_device_spi_sector_erase (self, addr, error)) {
		g_prefix_error (error, "->spi_sector_erase failed");
		return FALSE;
	}
	if (!fu_vli_device_spi_wait_finish (self, error)) {
		g_prefix_error (error, "->spi_wait_finish failed");
		return FALSE;
	}

	/* verify it really was blanked */
	for (guint32 offset = 0; offset < bufsz; offset += FU_VLI_DEVICE_TXSIZE) {
		guint8 buf[FU_VLI_DEVICE_TXSIZE] = { 0x0 };
		if (!fu_vli_device_spi_read_block (self,
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

GBytes *
fu_vli_device_spi_read (FuVliDevice *self, guint32 address, gsize bufsz, GError **error)
{
	g_autofree guint8 *buf = g_malloc0 (bufsz);
	g_autoptr(GPtrArray) chunks = NULL;

	/* get data from hardware */
	chunks = fu_chunk_array_new (buf, bufsz, address, 0x0, FU_VLI_DEVICE_TXSIZE);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		if (!fu_vli_device_spi_read_block (self,
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
fu_vli_device_spi_write_block (FuVliDevice *self,
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
	if (g_getenv ("FWUPD_VLI_USBHUB_VERBOSE") != NULL)
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
	if (!fu_vli_device_spi_read_block (self, address, buf_tmp, bufsz, error)) {
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
			if (!fu_vli_device_spi_write_block (self,
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
	if (!fu_vli_device_spi_write_block (self,
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

gboolean
fu_vli_device_spi_erase_all (FuVliDevice *self, GError **error)
{
	fu_device_set_progress (FU_DEVICE (self), 0);
	if (!fu_vli_device_spi_write_enable (self, error))
		return FALSE;
	if (!fu_vli_device_spi_write_status (self, 0x00, error))
		return FALSE;
	if (!fu_vli_device_spi_write_enable (self, error))
		return FALSE;
	if (!fu_vli_device_spi_chip_erase (self, error))
		return FALSE;
	g_usleep (4 * G_USEC_PER_SEC);

	/* verify chip was erased */
	for (guint addr = 0; addr < 0x10000; addr += 0x1000) {
		guint8 buf[FU_VLI_DEVICE_TXSIZE] = { 0x0 };
		if (!fu_vli_device_spi_read_block (self, addr, buf, sizeof(buf), error)) {
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
		fu_device_set_progress_full (FU_DEVICE (self),
					     (gsize) addr, (gsize) 0x10000);
	}
	return TRUE;
}

gboolean
fu_vli_device_spi_erase (FuVliDevice *self, guint32 addr, gsize sz, GError **error)
{
	g_autoptr(GPtrArray) chunks = fu_chunk_array_new (NULL, sz, addr, 0x0, 0x1000);
	g_debug ("erasing 0x%x bytes @0x%x", (guint) sz, addr);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chunk = g_ptr_array_index (chunks, i);
		if (g_getenv ("FWUPD_VLI_USBHUB_VERBOSE") != NULL)
			g_debug ("erasing @0x%x", chunk->address);
		if (!fu_vli_device_spi_erase_sector (FU_VLI_DEVICE (self), chunk->address, error)) {
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
	guint32 sz;

	priv->kind = device_kind;

	/* set maximum firmware size */
	sz = fu_vli_common_device_kind_get_size (device_kind);
	if (sz > 0x0)
		fu_device_set_firmware_size_max (FU_DEVICE (self), sz);
}

void
fu_vli_device_set_spi_auto_detect (FuVliDevice *self, gboolean spi_auto_detect)
{
	FuVliDevicePrivate *priv = GET_PRIVATE (self);
	priv->spi_auto_detect = spi_auto_detect;
}

FuVliDeviceKind
fu_vli_device_get_kind (FuVliDevice *self)
{
	FuVliDevicePrivate *priv = GET_PRIVATE (self);
	return priv->kind;
}

guint32
fu_vli_device_get_offset (FuVliDevice *self)
{
	FuVliDevicePrivate *priv = GET_PRIVATE (self);
	return fu_vli_common_device_kind_get_offset (priv->kind);
}

static void
fu_vli_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuVliDeviceClass *klass = FU_VLI_DEVICE_GET_CLASS (device);
	FuVliDevice *self = FU_VLI_DEVICE (device);
	FuVliDevicePrivate *priv = GET_PRIVATE (self);
	fu_common_string_append_kv (str, idt, "DeviceKind",
				    fu_vli_common_device_kind_to_string (priv->kind));
	fu_common_string_append_kb (str, idt, "SpiAutoDetect", priv->spi_auto_detect);
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

	/* get the flash chip attached */
	if (priv->spi_auto_detect) {
		GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
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
	}

	/* subclassed further */
	if (klass->setup != NULL) {
		if (!klass->setup (self, error))
			return FALSE;
	}

	/* add extra DEV GUID too */
	if (priv->kind != FU_VLI_DEVICE_KIND_UNKNOWN) {
		GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
		g_autofree gchar *devid1 = NULL;
		devid1 = g_strdup_printf ("USB\\VID_%04X&PID_%04X&DEV_%s",
					  g_usb_device_get_vid (usb_device),
					  g_usb_device_get_pid (usb_device),
					  fu_vli_common_device_kind_to_string (priv->kind));
		fu_device_add_instance_id (device, devid1);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_vli_device_attach (FuDevice *device, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	/* replug, and ignore the device going away */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	if (!fu_vli_device_reset (FU_VLI_DEVICE (device), &error_local)) {
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
	if (g_strcmp0 (key, "SpiAutoDetect") == 0) {
		priv->spi_auto_detect = fu_common_strtoull (value) > 0;
		return TRUE;
	}
	if (g_strcmp0 (key, "DeviceKind") == 0) {
		FuVliDeviceKind device_kind;
		device_kind = fu_vli_common_device_kind_from_string (value);
		if (device_kind == FU_VLI_DEVICE_KIND_UNKNOWN) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "DeviceKind %s is not supported",
				     value);
			return FALSE;
		}
		fu_vli_device_set_kind (self, device_kind);
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
	priv->spi_auto_detect = TRUE;
}

static void
fu_vli_device_class_init (FuVliDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->to_string = fu_vli_device_to_string;
	klass_device->set_quirk_kv = fu_vli_device_set_quirk_kv;
	klass_device->setup = fu_vli_device_setup;
	klass_device->attach = fu_vli_device_attach;
}
