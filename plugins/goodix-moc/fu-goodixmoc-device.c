/*
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 boger wang <boger@goodix.com>
 * 
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"
#include <string.h>
#include "fu-chunk.h"
#include "fu-goodixmoc-common.h"
#include "fu-goodixmoc-device.h"

struct _FuGoodixMocDevice {
	FuUsbDevice	parent_instance;
};

G_DEFINE_TYPE (FuGoodixMocDevice, fu_goodixmoc_device, FU_TYPE_USB_DEVICE)

#define GX_USB_BULK_EP_IN		(3 | 0x80)
#define GX_USB_BULK_EP_OUT 		(1 | 0x00)
#define GX_USB_INTERFACE		0 

#define GX_USB_DATAIN_TIMEOUT		2000  /* ms */
#define GX_USB_DATAOUT_TIMEOUT		200   /* ms */
#define GX_FLASH_TRANSFER_BLOCK_SIZE	1000  /* 1000 */

static gboolean
goodixmoc_device_cmd_send (GUsbDevice *usbdevice,
			   guint8      cmd0,
			   guint8      cmd1,
			   GxPkgType   type,
			   GByteArray *request,
			   GError    **error)
{
	gboolean ret = FALSE;
	GxfpPkgHeader header = { 0, };
	guint32 crc_actual = 0;
	gsize actual_len = 0;
	g_autoptr(GByteArray) buf = g_byte_array_new ();
	fu_goodixmoc_build_header (&header, request->len, cmd0, cmd1, type);
	g_byte_array_append (buf, (guint8 *)&header, sizeof(header));
	g_byte_array_append (buf, request->data, request->len);
	crc_actual = fu_common_crc32 (buf->data, sizeof(header) + request->len);
	fu_byte_array_append_uint32 (buf, crc_actual, G_LITTLE_ENDIAN);

	/* send zero length package */
	ret = g_usb_device_bulk_transfer (usbdevice,
					  GX_USB_BULK_EP_OUT,
					  NULL,
					  0,
					  NULL,
					  GX_USB_DATAOUT_TIMEOUT, NULL, error);
	if (!ret) {
		g_prefix_error (error, "failed to request: ");
		return FALSE;
	}
	if (g_getenv ("FWUPD_GOODIXFP_VERBOSE") != NULL) {
		fu_common_dump_full (G_LOG_DOMAIN, "REQST",
				     buf->data, buf->len, 16,
				     FU_DUMP_FLAGS_SHOW_ADDRESSES);
	}

	/* send data */
	ret = g_usb_device_bulk_transfer (usbdevice,
					  GX_USB_BULK_EP_OUT,
					  buf->data,
					  buf->len,
					  &actual_len,
					  GX_USB_DATAOUT_TIMEOUT, NULL, error);
	if (!ret) {
		g_prefix_error (error, "failed to request: ");
		return FALSE;
	}
	if (actual_len != buf->len) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid length");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
goodixmoc_device_cmd_recv (GUsbDevice  *usbdevice,
			   GxfpCmdResp *presponse,
			   gboolean     data_reply,
			   GError     **error)
{ 
	gboolean ret = FALSE;
	GxfpPkgHeader header = { 0, };
	guint32 crc_actual = 0;
	gsize actual_len = 0;
	gsize offset = 0;
	g_assert (presponse != NULL);

	/*
	* package format
	* | zlp | ack | zlp | data |
	*/
	while (1) {
		g_autoptr(GByteArray) reply = g_byte_array_new ();
		g_byte_array_set_size (reply, GX_FLASH_TRANSFER_BLOCK_SIZE);
		ret = g_usb_device_bulk_transfer (usbdevice,
						  GX_USB_BULK_EP_IN,
						  reply->data,
						  reply->len,
						  &actual_len, /* allowed to return short read */
						  GX_USB_DATAIN_TIMEOUT, NULL, error);
		if (!ret) {
			g_prefix_error (error, "failed to reply: ");
			return FALSE;
		}

		/* receive zero length package */
		if (ret && actual_len == 0)
			continue;
		if (g_getenv ("FWUPD_GOODIXFP_VERBOSE") != NULL) {
			fu_common_dump_full (G_LOG_DOMAIN, "REPLY",
					     reply->data, actual_len, 16,
					     FU_DUMP_FLAGS_SHOW_ADDRESSES);
		}

		/* parse package header */
		ret = fu_goodixmoc_parse_header (reply->data, actual_len, &header);
		if (!ret) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "Invalid value");
			return FALSE;
		}
		offset = sizeof(header) + header.len;
		crc_actual = fu_common_crc32 (reply->data, offset);
		if (crc_actual != GUINT32_FROM_LE(*(guint32 *)(reply->data + offset))) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "Invalid checksum");
			return FALSE;
		}

		/* parse package data */
		ret = fu_goodixmoc_parse_body (header.cmd0, reply->data + sizeof(header), header.len, presponse);
		if (!ret) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "Invalid value");
			return FALSE;
		}
		
		/* continue after ack received */
		if (header.cmd0 == GX_CMD_ACK && data_reply)
			continue;
		break;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_goodixmoc_device_cmd_xfer (FuGoodixMocDevice	*device,
			      guint8		 cmd0,
			      guint8 	         cmd1,
			      GxPkgType		 type,
			      GByteArray	*request,
			      GxfpCmdResp       *presponse,
			      gboolean           data_reply,
			      GError           **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE(device));
	if (!goodixmoc_device_cmd_send (usb_device, cmd0, cmd1, type, request, error))
		return FALSE;
	return goodixmoc_device_cmd_recv (usb_device, presponse, data_reply, error);
}

static gchar *
fu_goodixmoc_device_get_version (FuGoodixMocDevice *self, GError **error)
{
	GxfpCmdResp reponse = { 0, };
	guint8 dummy = 0;
	gchar ver[9] = { 0 };

	g_autoptr(GByteArray) request = g_byte_array_new ();
	fu_byte_array_append_uint8 (request, dummy);
	if (!fu_goodixmoc_device_cmd_xfer (self, GX_CMD_VERSION, GX_CMD1_DEFAULT,
					  GX_PKG_TYPE_EOP,
					  request,
					  &reponse,
					  TRUE,
					  error))
		return NULL;
	if (!fu_memcpy_safe ((guint8*)ver, sizeof(ver), 0x0, 
			      reponse.version_info.fwversion, 
			      sizeof(reponse.version_info.fwversion),
			      0x0,
			      sizeof(reponse.version_info.fwversion),		
			      error))
		return NULL;
	return g_strdup (ver);
}

static gboolean
fu_goodixmoc_device_update_init(FuGoodixMocDevice *self, GError **error)
{
	GxfpCmdResp reponse = { 0, };
	g_autoptr(GByteArray) request = g_byte_array_new ();

	/* update initial */
	if (!fu_goodixmoc_device_cmd_xfer (self, GX_CMD_UPGRADE, GX_CMD_UPGRADE_INIT,
					   GX_PKG_TYPE_EOP,
					   request,
					   &reponse,
					   TRUE,
					   error)) {
		g_prefix_error (error, "failed to send initial update: ");
		return FALSE;
	}

	/* check result */
	if (reponse.result != 0) {
		g_prefix_error (error, "initial update failed: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_goodixmoc_device_attach (FuDevice *device, GError **error)
{
	FuGoodixMocDevice *self = FU_GOODIXMOC_DEVICE(device);
	GxfpCmdResp reponse = { 0, };
	g_autoptr(GByteArray) request = g_byte_array_new ();
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);

	/* reset device */
	if (!fu_goodixmoc_device_cmd_xfer (self, GX_CMD_RESET, 0x03,
					   GX_PKG_TYPE_EOP,
					   request,
					   &reponse,
					   FALSE,
					   error)) {
		g_prefix_error (error, "failed to send reset device: ");
		return FALSE;
	}

	/* check result */
	if (reponse.result != 0) {
		g_prefix_error (error, "failed to reset device: ");
		return FALSE;
	}
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_goodixmoc_device_open (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);
	if (!g_usb_device_claim_interface (usb_device, GX_USB_INTERFACE,
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_goodixmoc_device_setup (FuDevice *device, GError **error)
{
	FuGoodixMocDevice *self = FU_GOODIXMOC_DEVICE(device);
	g_autofree gchar *version = NULL;
	version = fu_goodixmoc_device_get_version (self, error);
	if (version == NULL) {
		g_prefix_error (error, "failed to get firmware version: ");
		return FALSE;
	}
	g_debug ("obtained fwver using API '%s'", version);
	fu_device_set_version (device, version);

	/* success */
	return TRUE;
}

static gboolean
fu_goodixmoc_device_write_firmware (FuDevice	     *device,
				    FuFirmware 	     *firmware,
				    FwupdInstallFlags flags,
				    GError 	    **error)
{
	FuGoodixMocDevice *self = FU_GOODIXMOC_DEVICE(device);
	gboolean wait_data_reply = FALSE;
	GxPkgType pkg_eop = GX_PKG_TYPE_NORMAL;
	GxfpCmdResp reponse = { 0, };
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(GError) error_local = NULL;

	/* get default image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* build packets */
	chunks = fu_chunk_array_new_from_bytes (fw,
					        0x00,
					        0x00, /* page_sz */
					        GX_FLASH_TRANSFER_BLOCK_SIZE);

	/* don't auto-boot firmware */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_goodixmoc_device_update_init (self, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to initial update: %s",
			     error_local->message);
		return FALSE;
	}

	/* write each block */
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		g_autoptr(GByteArray) request = g_byte_array_new ();
		g_byte_array_append (request, chk->data, chk->data_sz);

		/* the last chunk */
		if (i == chunks->len - 1) {
			wait_data_reply = TRUE;
			pkg_eop = GX_PKG_TYPE_EOP;
		}
		if (!fu_goodixmoc_device_cmd_xfer (self, GX_CMD_UPGRADE, GX_CMD_UPGRADE_DATA,
						   pkg_eop,
						   request,
						   &reponse,
						   wait_data_reply,
						   &error_local)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_WRITE,
				     "failed to write: %s",
				     error_local->message);
			return FALSE;
		}

		/* check update status */
		if (wait_data_reply && reponse.result != 0) {
			g_prefix_error (error, "failed to verify firmware: ");
			return FALSE;
		}

		/* update progress */
		fu_device_set_progress_full (device, (gsize)i, (gsize)chunks->len);
	}

	/* success! */
	return TRUE;
}

static void
fu_goodixmoc_device_init (FuGoodixMocDevice *self)
{
	fu_device_add_flag (FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_flag (FU_DEVICE(self), FWUPD_DEVICE_FLAG_USE_RUNTIME_VERSION);
	fu_device_set_version_format (FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_remove_delay (FU_DEVICE(self), 5000);
	fu_device_set_protocol (FU_DEVICE (self), "com.goodix.goodixmoc");
	fu_device_set_name (FU_DEVICE(self), "Fingerprint Sensor");
	fu_device_set_summary (FU_DEVICE(self), "Match-On-Chip Fingerprint Sensor");
	fu_device_set_vendor (FU_DEVICE(self), "Goodix");
	fu_device_set_install_duration (FU_DEVICE(self), 10);
	fu_device_set_firmware_size_min (FU_DEVICE(self), 0x20000);
	fu_device_set_firmware_size_max (FU_DEVICE(self), 0x30000);
}

static void
fu_goodixmoc_device_class_init(FuGoodixMocDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS(klass);
	klass_device->write_firmware = fu_goodixmoc_device_write_firmware;
	klass_device->setup = fu_goodixmoc_device_setup;
	klass_device->attach = fu_goodixmoc_device_attach;
	klass_usb_device->open = fu_goodixmoc_device_open;
}
