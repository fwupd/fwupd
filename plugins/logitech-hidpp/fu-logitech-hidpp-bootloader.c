/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-logitech-hidpp-bootloader.h"
#include "fu-logitech-hidpp-common.h"
#include "fu-logitech-hidpp-hidpp.h"

typedef struct {
	guint16 flash_addr_lo;
	guint16 flash_addr_hi;
	guint16 flash_blocksize;
} FuLogitechHidppBootloaderPrivate;

#define FU_LOGITECH_HIDPP_DEVICE_EP1 0x81
#define FU_LOGITECH_HIDPP_DEVICE_EP3 0x83

G_DEFINE_TYPE_WITH_PRIVATE(FuLogitechHidppBootloader,
			   fu_logitech_hidpp_bootloader,
			   FU_TYPE_HID_DEVICE)

#define GET_PRIVATE(o) (fu_logitech_hidpp_bootloader_get_instance_private(o))

static void
fu_logitech_hidpp_bootloader_to_string(FuDevice *device, guint idt, GString *str)
{
	FuLogitechHidppBootloader *self = FU_LOGITECH_HIDPP_BOOTLOADER(device);
	FuLogitechHidppBootloaderPrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append_hex(str, idt, "FlashAddrHigh", priv->flash_addr_hi);
	fwupd_codec_string_append_hex(str, idt, "FlashAddrLow", priv->flash_addr_lo);
	fwupd_codec_string_append_hex(str, idt, "FlashBlockSize", priv->flash_blocksize);
}

GPtrArray *
fu_logitech_hidpp_bootloader_parse_pkts(FuLogitechHidppBootloader *self,
					GPtrArray *records,
					GError **error)
{
	guint32 last_addr = 0;
	g_autoptr(GPtrArray) pkts = g_ptr_array_new_with_free_func(
	    (GDestroyNotify)fu_struct_logitech_hidpp_bootloader_pkt_unref);

	for (guint i = 0; i < records->len; i++) {
		FuIhexFirmwareRecord *rcd = g_ptr_array_index(records, i);
		g_autoptr(FuStructLogitechHidppBootloaderPkt) st_req =
		    fu_struct_logitech_hidpp_bootloader_pkt_new();

		if (rcd->record_type == FU_IHEX_FIRMWARE_RECORD_TYPE_EOF)
			break;

		if (rcd->record_type == FU_IHEX_FIRMWARE_RECORD_TYPE_SIGNATURE) {
			fu_struct_logitech_hidpp_bootloader_pkt_set_cmd(
			    st_req,
			    FU_LOGITECH_HIDPP_BOOTLOADER_CMD_WRITE_SIGNATURE);
		} else {
			fu_struct_logitech_hidpp_bootloader_pkt_set_cmd(
			    st_req,
			    FU_LOGITECH_HIDPP_BOOTLOADER_CMD_WRITE_RAM_BUFFER);
		}
		fu_struct_logitech_hidpp_bootloader_pkt_set_addr(st_req, rcd->addr);
		fu_struct_logitech_hidpp_bootloader_pkt_set_len(st_req, rcd->byte_cnt);

		/* read the data, but skip the checksum byte */
		if (!fu_struct_logitech_hidpp_bootloader_pkt_set_data(st_req,
								      rcd->data->data,
								      rcd->data->len,
								      error)) {
			g_prefix_error_literal(error, "failed to copy data: ");
			return NULL;
		}

		/* no need to bound check signature addresses */
		if (rcd->record_type == FU_IHEX_FIRMWARE_RECORD_TYPE_SIGNATURE) {
			g_ptr_array_add(pkts, g_steal_pointer(&st_req));
			continue;
		}

		/* skip the bootloader */
		if (rcd->addr > fu_logitech_hidpp_bootloader_get_addr_hi(self)) {
			g_debug("skipping write @ %04x", rcd->addr);
			continue;
		}

		/* skip the header */
		if (rcd->addr < fu_logitech_hidpp_bootloader_get_addr_lo(self)) {
			g_debug("skipping write @ %04x", rcd->addr);
			continue;
		}

		/* make sure firmware addresses only go up */
		if (rcd->addr < last_addr) {
			g_debug("skipping write @ %04x", rcd->addr);
			continue;
		}
		last_addr = rcd->addr;

		/* pending */
		g_ptr_array_add(pkts, g_steal_pointer(&st_req));
	}
	if (pkts->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "firmware data invalid: no payloads found");
		return NULL;
	}
	return g_steal_pointer(&pkts);
}

guint16
fu_logitech_hidpp_bootloader_get_addr_lo(FuLogitechHidppBootloader *self)
{
	FuLogitechHidppBootloaderPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_LOGITECH_HIDPP_BOOTLOADER(self), 0x0000);
	return priv->flash_addr_lo;
}

guint16
fu_logitech_hidpp_bootloader_get_addr_hi(FuLogitechHidppBootloader *self)
{
	FuLogitechHidppBootloaderPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_LOGITECH_HIDPP_BOOTLOADER(self), 0x0000);
	return priv->flash_addr_hi;
}

guint16
fu_logitech_hidpp_bootloader_get_blocksize(FuLogitechHidppBootloader *self)
{
	FuLogitechHidppBootloaderPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_LOGITECH_HIDPP_BOOTLOADER(self), 0x0000);
	return priv->flash_blocksize;
}

static gboolean
fu_logitech_hidpp_bootloader_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuLogitechHidppBootloader *self = FU_LOGITECH_HIDPP_BOOTLOADER(device);
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_rsp = NULL;
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_req =
	    fu_struct_logitech_hidpp_bootloader_pkt_new();

	fu_struct_logitech_hidpp_bootloader_pkt_set_cmd(st_req,
							FU_LOGITECH_HIDPP_BOOTLOADER_CMD_REBOOT);
	st_rsp = fu_logitech_hidpp_bootloader_request(self, st_req, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "failed to attach back to runtime: ");
		return FALSE;
	}
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_ensure_bl_version(FuLogitechHidppBootloader *self, GError **error)
{
	const guint8 *buf;
	gsize bufsz = 0;
	guint16 build = 0;
	guint8 major = 0;
	guint8 minor = 0;
	g_autofree gchar *version = NULL;
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_rsp = NULL;
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_req =
	    fu_struct_logitech_hidpp_bootloader_pkt_new();

	/* call into hardware */
	fu_struct_logitech_hidpp_bootloader_pkt_set_cmd(
	    st_req,
	    FU_LOGITECH_HIDPP_BOOTLOADER_CMD_GET_BL_VERSION);
	st_rsp = fu_logitech_hidpp_bootloader_request(self, st_req, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "failed to get firmware version: ");
		return FALSE;
	}
	buf = fu_struct_logitech_hidpp_bootloader_pkt_get_data(st_rsp, &bufsz);

	/* BOTxx.yy_Bzzzz
	 * 012345678901234 */
	if (!fu_firmware_strparse_uint8_safe((const gchar *)buf, bufsz, 3, &major, error))
		return FALSE;
	if (!fu_firmware_strparse_uint8_safe((const gchar *)buf, bufsz, 6, &minor, error))
		return FALSE;
	if (!fu_firmware_strparse_uint16_safe((const gchar *)buf, bufsz, 10, &build, error))
		return FALSE;
	version = fu_logitech_hidpp_format_version("BOT", major, minor, build);
	if (version == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "failed to format firmware version");
		return FALSE;
	}
	fu_device_set_version_bootloader(FU_DEVICE(self), version);

	if ((major == 0x01 && minor >= 0x04) || (major == 0x03 && minor >= 0x02)) {
		fu_device_add_private_flag(FU_DEVICE(self),
					   FU_LOGITECH_HIDPP_BOOTLOADER_FLAG_IS_SIGNED);
		fu_device_add_protocol(FU_DEVICE(self), "com.logitech.unifyingsigned");
	} else {
		fu_device_add_protocol(FU_DEVICE(self), "com.logitech.unifying");
	}
	return TRUE;
}

static gboolean
fu_logitech_hidpp_bootloader_setup(FuDevice *device, GError **error)
{
	FuLogitechHidppBootloader *self = FU_LOGITECH_HIDPP_BOOTLOADER(device);
	FuLogitechHidppBootloaderPrivate *priv = GET_PRIVATE(self);
	const guint8 *buf;
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_rsp = NULL;
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_req =
	    fu_struct_logitech_hidpp_bootloader_pkt_new();

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_logitech_hidpp_bootloader_parent_class)->setup(device, error))
		return FALSE;

	/* get memory map */
	fu_struct_logitech_hidpp_bootloader_pkt_set_cmd(
	    st_req,
	    FU_LOGITECH_HIDPP_BOOTLOADER_CMD_GET_MEMINFO);
	st_rsp = fu_logitech_hidpp_bootloader_request(self, st_req, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "failed to get meminfo: ");
		return FALSE;
	}
	if (fu_struct_logitech_hidpp_bootloader_pkt_get_len(st_rsp) != 0x06) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to get meminfo: invalid size %02x",
			    fu_struct_logitech_hidpp_bootloader_pkt_get_len(st_rsp));
		return FALSE;
	}

	/* parse values */
	buf = fu_struct_logitech_hidpp_bootloader_pkt_get_data(st_rsp, NULL);
	priv->flash_addr_lo = fu_memread_uint16(buf + 0, G_BIG_ENDIAN);
	priv->flash_addr_hi = fu_memread_uint16(buf + 2, G_BIG_ENDIAN);
	priv->flash_blocksize = fu_memread_uint16(buf + 4, G_BIG_ENDIAN);

	/* get bootloader version */
	return fu_logitech_hidpp_bootloader_ensure_bl_version(self, error);
}

FuStructLogitechHidppBootloaderPkt *
fu_logitech_hidpp_bootloader_request(FuLogitechHidppBootloader *self,
				     FuStructLogitechHidppBootloaderPkt *st_req,
				     GError **error)
{
	gsize actual_length = 0;
	guint8 buf[32] = {0};
	g_autoptr(FuStructLogitechHidppBootloaderPkt) st_rsp = NULL;

	/* send request */
	fu_dump_raw(G_LOG_DOMAIN, "host->device", st_req->buf->data, st_req->buf->len);
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      0x0,
				      st_req->buf->data,
				      st_req->buf->len,
				      FU_LOGITECH_HIDPP_DEVICE_TIMEOUT_MS,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error_literal(error, "failed to send data: ");
		return NULL;
	}

	/* no response required when rebooting */
	if (fu_struct_logitech_hidpp_bootloader_pkt_get_cmd(st_req) ==
	    FU_LOGITECH_HIDPP_BOOTLOADER_CMD_REBOOT) {
		g_autoptr(GError) error_ignore = NULL;
		if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
						      FU_LOGITECH_HIDPP_DEVICE_EP1,
						      buf,
						      sizeof(buf),
						      &actual_length,
						      FU_LOGITECH_HIDPP_DEVICE_TIMEOUT_MS,
						      NULL,
						      &error_ignore)) {
			g_debug("ignoring: %s", error_ignore->message);
		} else {
			fu_dump_raw(G_LOG_DOMAIN, "device->host", buf, actual_length);
		}
		return fu_struct_logitech_hidpp_bootloader_pkt_new();
	}

	/* get response */
	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
					      FU_LOGITECH_HIDPP_DEVICE_EP1,
					      buf,
					      sizeof(buf),
					      &actual_length,
					      FU_LOGITECH_HIDPP_DEVICE_TIMEOUT_MS,
					      NULL,
					      error)) {
		g_prefix_error_literal(error, "failed to get data: ");
		return NULL;
	}
	fu_dump_raw(G_LOG_DOMAIN, "device->host", buf, actual_length);

	/* parse response */
	if ((buf[0x00] & 0xf0) != fu_struct_logitech_hidpp_bootloader_pkt_get_cmd(st_req)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid command response of %02x, expected %02x",
			    buf[0x00],
			    fu_struct_logitech_hidpp_bootloader_pkt_get_cmd(st_req));
		return NULL;
	}
	st_rsp = fu_struct_logitech_hidpp_bootloader_pkt_parse(buf, sizeof(buf), 0x0, error);
	if (st_rsp == NULL)
		return NULL;
	return g_steal_pointer(&st_rsp);
}

static void
fu_logitech_hidpp_bootloader_replace(FuDevice *device, FuDevice *donor)
{
	fu_device_incorporate_flag(device, donor, FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_incorporate_flag(device, donor, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
}

static void
fu_logitech_hidpp_bootloader_init(FuLogitechHidppBootloader *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_USB_RECEIVER);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_name(FU_DEVICE(self), "Unifying Receiver");
	fu_device_set_summary(FU_DEVICE(self), "Miniaturised USB wireless receiver (bootloader)");
	fu_device_set_remove_delay(FU_DEVICE(self), FU_LOGITECH_HIDPP_DEVICE_TIMEOUT_MS);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_LOGITECH_HIDPP_BOOTLOADER_FLAG_IS_SIGNED);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), 0x00);
}

static void
fu_logitech_hidpp_bootloader_class_init(FuLogitechHidppBootloaderClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_logitech_hidpp_bootloader_to_string;
	device_class->attach = fu_logitech_hidpp_bootloader_attach;
	device_class->setup = fu_logitech_hidpp_bootloader_setup;
	device_class->replace = fu_logitech_hidpp_bootloader_replace;
}
