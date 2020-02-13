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
#include "fu-vli-usbhub-i2c-device.h"
#include "fu-vli-usbhub-pd-common.h"
#include "fu-vli-usbhub-pd-device.h"

struct _FuVliUsbhubDevice
{
	FuVliDevice		 parent_instance;
	gboolean		 disable_powersave;
	guint8			 update_protocol;
	FuVliUsbhubHeader	 hd1_hdr;	/* factory */
	FuVliUsbhubHeader	 hd2_hdr;	/* update */
};

G_DEFINE_TYPE (FuVliUsbhubDevice, fu_vli_usbhub_device, FU_TYPE_VLI_DEVICE)

static void
fu_vli_usbhub_device_to_string (FuVliDevice *device, guint idt, GString *str)
{
	FuVliUsbhubDevice *self = FU_VLI_USBHUB_DEVICE (device);
	fu_common_string_append_kb (str, idt, "DisablePowersave", self->disable_powersave);
	fu_common_string_append_kx (str, idt, "UpdateProtocol", self->update_protocol);
	if (self->update_protocol >= 0x2) {
		fu_common_string_append_kv (str, idt, "H1Hdr@0x0", NULL);
		fu_vli_usbhub_header_to_string (&self->hd1_hdr, idt + 1, str);
		fu_common_string_append_kv (str, idt, "H2Hdr@0x1000", NULL);
		fu_vli_usbhub_header_to_string (&self->hd2_hdr, idt + 1, str);
	}
}

gboolean
fu_vli_usbhub_device_i2c_read (FuVliUsbhubDevice *self,
			       guint8 cmd, guint8 *buf, gsize bufsz,
			       GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	guint16 value = ((guint16) FU_VLI_USBHUB_I2C_ADDR_WRITE << 8) | cmd;
	guint16 index = (guint16) FU_VLI_USBHUB_I2C_ADDR_READ << 8;
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    FU_VLI_USBHUB_I2C_R_VDR, value, index,
					    buf, bufsz, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to read I2C: ");
		return FALSE;
	}
	if (g_getenv ("FWUPD_VLI_USBHUB_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "I2cReadData", buf, 0x1);
	return TRUE;
}

gboolean
fu_vli_usbhub_device_i2c_read_status (FuVliUsbhubDevice *self,
				      FuVliUsbhubI2cStatus *status,
				      GError **error)
{
	guint8 buf[1] = { 0xff };
	if (!fu_vli_usbhub_device_i2c_read (self,
					    FU_VLI_USBHUB_I2C_CMD_READ_STATUS,
					    buf, sizeof(buf),
					    error))
		return FALSE;
	if (status != NULL)
		*status = buf[0];
	return TRUE;
}

gboolean
fu_vli_usbhub_device_i2c_write_data (FuVliUsbhubDevice *self,
				     guint8 skip_s,
				     guint8 skip_p,
				     const guint8 *buf,
				     gsize bufsz,
				     GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	guint16 value = (((guint16) skip_s) << 8) | skip_p;
	if (g_getenv ("FWUPD_VLI_USBHUB_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "I2cWriteData", buf, bufsz);
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    FU_VLI_USBHUB_I2C_W_VDR, value, 0x0,
					    (guint8 *) buf, bufsz, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to write I2C @0x%x: ", value);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_vli_usbhub_device_i2c_write (FuVliUsbhubDevice *self, guint8 cmd,
				const guint8 *buf, gsize bufsz, GError **error)
{
	guint8 buf2[10] = { FU_VLI_USBHUB_I2C_ADDR_WRITE, cmd, 0x0 };
	if (!fu_memcpy_safe (buf2, sizeof(buf2), 0x2,
			     buf, bufsz, 0x0, bufsz, error))
		return FALSE;
	return fu_vli_usbhub_device_i2c_write_data (self, 0x0, 0x0, buf2, bufsz + 2, error);
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
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to UnLock_VL813: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_read_reg (FuVliUsbhubDevice *self, guint16 addr, guint8 *buf, GError **error)
{
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    addr >> 8, addr & 0xff, 0x0,
					    buf, 0x1, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to read register 0x%x: ", addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_write_reg (FuVliUsbhubDevice *self, guint16 addr, guint8 value, GError **error)
{
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    addr >> 8, addr & 0xff, (guint16) value,
					    NULL, 0x0, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to write register 0x%x: ", addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_spi_read_status (FuVliDevice *self, guint8 *status, GError **error)
{
	guint8 spi_cmd = 0x0;
	if (!fu_vli_device_get_spi_cmd (self, FU_VLI_DEVICE_SPI_REQ_READ_STATUS,
					&spi_cmd, error))
		return FALSE;
	return g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					      G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					      G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					      G_USB_DEVICE_RECIPIENT_DEVICE,
					      0xc1, spi_cmd, 0x0000,
					      status, 0x1, NULL,
					      FU_VLI_DEVICE_TIMEOUT,
					      NULL, error);
}

static gboolean
fu_vli_usbhub_device_spi_read_data (FuVliDevice *self, guint32 addr, guint8 *buf, gsize bufsz, GError **error)
{
	guint8 spi_cmd = 0x0;
	guint16 value;
	guint16 index;
	if (!fu_vli_device_get_spi_cmd (self, FU_VLI_DEVICE_SPI_REQ_READ_DATA,
					&spi_cmd, error))
		return FALSE;
	value = ((addr >> 8) & 0xff00) | spi_cmd;
	index = ((addr << 8) & 0xff00) | ((addr >> 8) & 0x00ff);
	return g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					      G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					      G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					      G_USB_DEVICE_RECIPIENT_DEVICE,
					      0xc4, value, index,
					      buf, bufsz, NULL,
					      FU_VLI_DEVICE_TIMEOUT,
					      NULL, error);
}

static gboolean
fu_vli_usbhub_device_spi_write_status (FuVliDevice *self, guint8 status, GError **error)
{
	guint8 spi_cmd = 0x0;
	if (!fu_vli_device_get_spi_cmd (self, FU_VLI_DEVICE_SPI_REQ_WRITE_STATUS,
					&spi_cmd, error))
		return FALSE;
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xd1, spi_cmd, 0x0000,
					    &status, 0x1, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		return FALSE;
	}

	/* Fix_For_GD_&_EN_SPI_Flash */
	g_usleep (100 * 1000);
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_spi_write_enable (FuVliDevice *self, GError **error)
{
	guint8 spi_cmd = 0x0;
	if (!fu_vli_device_get_spi_cmd (self, FU_VLI_DEVICE_SPI_REQ_WRITE_EN,
					&spi_cmd, error))
		return FALSE;
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xd1, spi_cmd, 0x0000,
					    NULL, 0x0, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to write enable SPI: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_spi_chip_erase (FuVliDevice *self, GError **error)
{
	guint8 spi_cmd = 0x0;
	if (!fu_vli_device_get_spi_cmd (self, FU_VLI_DEVICE_SPI_REQ_CHIP_ERASE,
					&spi_cmd, error))
		return FALSE;
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xd1, spi_cmd, 0x0000,
					    NULL, 0x0, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_spi_sector_erase (FuVliDevice *self, guint32 addr, GError **error)
{
	guint8 spi_cmd = 0x0;
	guint16 value;
	guint16 index;
	if (!fu_vli_device_get_spi_cmd (self, FU_VLI_DEVICE_SPI_REQ_SECTOR_ERASE,
					&spi_cmd, error))
		return FALSE;
	value = ((addr >> 8) & 0xff00) | spi_cmd;
	index = ((addr << 8) & 0xff00) | ((addr >> 8) & 0x00ff);
	return g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					      G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					      G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					      G_USB_DEVICE_RECIPIENT_DEVICE,
					      0xd4, value, index,
					      NULL, 0x0, NULL,
					      FU_VLI_DEVICE_TIMEOUT,
					      NULL, error);
}

static gboolean
fu_vli_usbhub_device_spi_write_data (FuVliDevice *self,
				     guint32 addr,
				     const guint8 *buf,
				     gsize bufsz,
				     GError **error)
{
	guint8 spi_cmd = 0x0;
	guint16 value;
	guint16 index;
	if (!fu_vli_device_get_spi_cmd (self, FU_VLI_DEVICE_SPI_REQ_PAGE_PROG,
					&spi_cmd, error))
		return FALSE;
	value = ((addr >> 8) & 0xff00) | spi_cmd;
	index = ((addr << 8) & 0xff00) | ((addr >> 8) & 0x00ff);
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xd4, value, index,
					    (guint8 *) buf, bufsz, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_reset (FuVliDevice *device, GError **error)
{
	return g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (device)),
					      G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					      G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					      G_USB_DEVICE_RECIPIENT_DEVICE,
					      0xf6, 0x0040, 0x0002,
					      NULL, 0x0, NULL,
					      FU_VLI_DEVICE_TIMEOUT,
					      NULL, error);
}

/* disable hub sleep states -- not really required by 815~ hubs */
static gboolean
fu_vli_usbhub_device_disable_u1u2 (FuVliUsbhubDevice *self, GError **error)
{
	guint8 buf = 0x0;

	/* clear Reg[0xF8A2] bit_3 & bit_7 -- also
	 * clear Total Switch / Flag To Disable FW Auto-Reload Function */
	if (!fu_vli_usbhub_device_read_reg (self, 0xf8a2, &buf, error))
		return FALSE;
	buf &= 0x77;
	if (!fu_vli_usbhub_device_write_reg (self, 0xf8a2, buf, error))
		return FALSE;

	/* clear Reg[0xF832] bit_0 & bit_1 */
	if (!fu_vli_usbhub_device_read_reg (self, 0xf832, &buf, error))
		return FALSE;
	buf &= 0xfc;
	if (!fu_vli_usbhub_device_write_reg (self, 0xf832, buf, error))
		return FALSE;

	/* clear Reg[0xF920] bit_1 & bit_2 */
	if (!fu_vli_usbhub_device_read_reg (self, 0xf920, &buf, error))
		return FALSE;
	buf &= 0xf9;
	if (!fu_vli_usbhub_device_write_reg (self, 0xf920, buf, error))
		return FALSE;

	/* set Reg[0xF836] bit_3 */
	if (!fu_vli_usbhub_device_read_reg (self, 0xf836, &buf, error))
		return FALSE;
	buf |= 0x08;
	if (!fu_vli_usbhub_device_write_reg (self, 0xf836, buf, error))
		return FALSE;
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

	if (!fu_vli_usbhub_device_read_reg (self, 0xf88c, &chipver, error)) {
		g_prefix_error (error, "Read_ChipVer failed: ");
		return FALSE;
	}
	if (!fu_vli_usbhub_device_read_reg (self, 0xf63f, &chipver2, error)) {
		g_prefix_error (error, "Read_ChipVer2 failed: ");
		return FALSE;
	}
	if (!fu_vli_usbhub_device_read_reg (self, 0xf800, &b811P812, error)) {
		g_prefix_error (error, "Read_811P812 failed: ");
		return FALSE;
	}
	if (!fu_vli_usbhub_device_read_reg (self, 0xf88e, &chipid1, error)) {
		g_prefix_error (error, "Read_ChipID1 failed: ");
		return FALSE;
	}
	if (!fu_vli_usbhub_device_read_reg (self, 0xf88f, &chipid2, error)) {
		g_prefix_error (error, "Read_ChipID2 failed: ");
		return FALSE;
	}
	if (!fu_vli_usbhub_device_read_reg (self, 0xf64e, &chipid12, error)) {
		g_prefix_error (error, "Read_ChipID12 failed: ");
		return FALSE;
	}
	if (!fu_vli_usbhub_device_read_reg (self, 0xf64f, &chipid22, error)) {
		g_prefix_error (error, "Read_ChipID22 failed: ");
		return FALSE;
	}
	if (!fu_vli_usbhub_device_read_reg (self, 0xf651, &b820Q7Q8, error)) {
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
		fu_vli_device_set_kind (FU_VLI_DEVICE (self), FU_VLI_DEVICE_KIND_VL210);
	} else if (chipid2 == 0x35 && chipid1 == 0x18) {
		if (b820Q7Q8 & (1 << 2))
			fu_vli_device_set_kind (FU_VLI_DEVICE (self), FU_VLI_DEVICE_KIND_VL820Q8);
		else
			fu_vli_device_set_kind (FU_VLI_DEVICE (self), FU_VLI_DEVICE_KIND_VL820Q7);
	} else if (chipid2 == 0x35 && chipid1 == 0x31) {
		fu_vli_device_set_kind (FU_VLI_DEVICE (self), FU_VLI_DEVICE_KIND_VL815);
	} else if (chipid2 == 0x35 && chipid1 == 0x38) {
		fu_vli_device_set_kind (FU_VLI_DEVICE (self), FU_VLI_DEVICE_KIND_VL817);
	} else if (chipid2 == 0x35 && chipid1 == 0x45) {
		fu_vli_device_set_kind (FU_VLI_DEVICE (self), FU_VLI_DEVICE_KIND_VL211);
	} else if (chipid22 == 0x35 && chipid12 == 0x53) {
		fu_vli_device_set_kind (FU_VLI_DEVICE (self), FU_VLI_DEVICE_KIND_VL120);
	} else if (chipid2 == 0x35 && chipid1 == 0x57) {
		fu_vli_device_set_kind (FU_VLI_DEVICE (self), FU_VLI_DEVICE_KIND_VL819);
	} else if (tPid == 0x810) {
		fu_vli_device_set_kind (FU_VLI_DEVICE (self), FU_VLI_DEVICE_KIND_VL810);
	} else if (tPid == 0x811) {
		fu_vli_device_set_kind (FU_VLI_DEVICE (self), FU_VLI_DEVICE_KIND_VL811);
	} else if ((b811P812 & ((1 << 5) | (1 << 4))) == 0) {
		if (chipver == 0x10)
			fu_vli_device_set_kind (FU_VLI_DEVICE (self), FU_VLI_DEVICE_KIND_VL811PB0);
		else
			fu_vli_device_set_kind (FU_VLI_DEVICE (self), FU_VLI_DEVICE_KIND_VL811PB3);
	} else if ((b811P812 & ((1 << 5) | (1 << 4))) == (1 << 4)) {
		fu_vli_device_set_kind (FU_VLI_DEVICE (self), FU_VLI_DEVICE_KIND_VL812Q4S);
	} else if ((b811P812 & ((1 << 5) | (1 << 4))) == ((1 << 5) | (1 << 4))) {
		if (chipver == 0x10)
			fu_vli_device_set_kind (FU_VLI_DEVICE (self), FU_VLI_DEVICE_KIND_VL812B0);
		else
			fu_vli_device_set_kind (FU_VLI_DEVICE (self), FU_VLI_DEVICE_KIND_VL812B3);
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


static gboolean
fu_vli_usbhub_device_probe (FuDevice *device, GError **error)
{
	guint16 usbver = fu_usb_device_get_spec (FU_USB_DEVICE (device));

	/* quirks now applied... */
	if (usbver > 0x0300 || fu_device_has_custom_flag (device, "usb3")) {
		fu_device_set_summary (device, "USB 3.x Hub");
	} else if (usbver > 0x0200 || fu_device_has_custom_flag (device, "usb2")) {
		fu_device_set_summary (device, "USB 2.x Hub");
	} else {
		fu_device_set_summary (device, "USB Hub");
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_pd_setup (FuVliUsbhubDevice *self, GError **error)
{
	FuVliPdHdr hdr = { 0x0 };
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(GError) error_local = NULL;

	/* legacy location */
	if (!fu_vli_device_spi_read_block (FU_VLI_DEVICE (self),
					   VLI_USBHUB_FLASHMAP_ADDR_PD_LEGACY +
					   VLI_USBHUB_PD_FLASHMAP_ADDR_LEGACY,
					   (guint8 *) &hdr, sizeof(hdr), error)) {
		g_prefix_error (error, "failed to read legacy PD header");
		return FALSE;
	}

	/* new location */
	if (GUINT16_FROM_LE (hdr.vid) != 0x2109) {
		g_debug ("PD VID was 0x%04x trying new location",
			 GUINT16_FROM_LE (hdr.vid));
		if (!fu_vli_device_spi_read_block (FU_VLI_DEVICE (self),
						   VLI_USBHUB_FLASHMAP_ADDR_PD +
						   VLI_USBHUB_PD_FLASHMAP_ADDR,
						   (guint8 *) &hdr, sizeof(hdr), error)) {
			g_prefix_error (error, "failed to read PD header");
			return FALSE;
		}
	}

	/* just empty space */
	if (hdr.fwver == G_MAXUINT32) {
		g_debug ("no PD device header found");
		return TRUE;
	}

	/* add child */
	dev = fu_vli_usbhub_pd_device_new (&hdr);
	if (!fu_device_probe (dev, &error_local)) {
		g_warning ("cannot create PD device: %s", error_local->message);
		return TRUE;
	}
	fu_device_add_child (FU_DEVICE (self), dev);
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_i2c_setup (FuVliUsbhubDevice *self, GError **error)
{
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(GError) error_local = NULL;

	/* add child */
	dev = fu_vli_usbhub_i2c_device_new (self);
	if (!fu_device_probe (dev, error))
		return FALSE;
	if (!fu_device_setup (dev, &error_local)) {
		if (g_error_matches (error_local,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND)) {
			g_debug ("%s", error_local->message);
		} else {
			g_warning ("cannot create I²C device: %s",
				   error_local->message);
		}
		return TRUE;
	}
	fu_device_add_child (FU_DEVICE (self), dev);
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_setup (FuVliDevice *device, GError **error)
{
	FuVliUsbhubDevice *self = FU_VLI_USBHUB_DEVICE (device);
	g_autoptr(GError) error_tmp = NULL;

	/* try to read a block of data which will fail for 813-type devices */
	if (fu_device_has_custom_flag (FU_DEVICE (self), "needs-unlock-legacy813") &&
	    !fu_vli_device_spi_read_block (FU_VLI_DEVICE (self), 0x0, (guint8 *) &self->hd1_hdr,
					   sizeof(self->hd1_hdr), &error_tmp)) {
		g_warning ("failed to read, trying to unlock 813: %s", error_tmp->message);
		if (!fu_vli_usbhub_device_vdr_unlock_813 (self, error))
			return FALSE;
		if (!fu_vli_device_spi_read_block (FU_VLI_DEVICE (self), 0x0, (guint8 *) &self->hd1_hdr,
						   sizeof(self->hd1_hdr), error)) {
			g_prefix_error (error, "813 unlock fail: ");
			return FALSE;
		}
		g_debug ("813 unlock OK");
		/* VL813 & VL210 have same PID (0x0813), and only VL813 can reply */
		fu_vli_device_set_kind (FU_VLI_DEVICE (self), FU_VLI_DEVICE_KIND_VL813);
	} else {
		if (!fu_vli_usbhub_device_guess_kind (self, error))
			return FALSE;
	}

	/* read HD1 (factory) header */
	if (!fu_vli_device_spi_read_block (FU_VLI_DEVICE (self), VLI_USBHUB_FLASHMAP_ADDR_HD1,
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
		if (!fu_vli_device_spi_read_block (FU_VLI_DEVICE (self), VLI_USBHUB_FLASHMAP_ADDR_HD2,
						   (guint8 *) &self->hd2_hdr,
						   sizeof(self->hd2_hdr), error)) {
			g_prefix_error (error, "failed to read HD2 header");
			return FALSE;
		}
	}

	/* detect the PD child */
	if (fu_device_has_custom_flag (FU_DEVICE (self), "has-shared-spi-pd")) {
		if (!fu_vli_usbhub_device_pd_setup (self, error))
			return FALSE;
	}

	/* detect the I²C child */
	if (fu_device_has_custom_flag (FU_DEVICE (self), "has-shared-spi-i2c")) {
		if (!fu_vli_usbhub_device_i2c_setup (self, error))
			return FALSE;
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
	FuVliDeviceKind device_kind;
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
	if (fu_vli_device_get_kind (FU_VLI_DEVICE (self)) != device_kind) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "firmware incompatible, got %s, expected %s",
			     fu_vli_common_device_kind_to_string (device_kind),
			     fu_vli_common_device_kind_to_string (fu_vli_device_get_kind (FU_VLI_DEVICE (self))));
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
	if (!fu_vli_device_spi_erase_all (FU_VLI_DEVICE (self), error)) {
		g_prefix_error (error, "failed to erase chip: ");
		return FALSE;
	}

	/* write in chunks */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_WRITE);
	buf = g_bytes_get_data (fw, &bufsz);
	if (!fu_vli_device_spi_write (FU_VLI_DEVICE (self), 0x0, buf, bufsz, error))
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
		if (!fu_vli_device_spi_erase_sector (FU_VLI_DEVICE (self), addr, error)) {
			g_prefix_error (error, "failed to erase sector @0x%x", addr);
			return FALSE;
		}
		fu_device_set_progress_full (FU_DEVICE (self), (gsize) addr, bufsz);
	}

	/* write in chunks */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_vli_device_spi_write (FU_VLI_DEVICE (self), VLI_USBHUB_FLASHMAP_ADDR_HD1,
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
	if (!fu_vli_device_spi_erase_sector (FU_VLI_DEVICE (self), VLI_USBHUB_FLASHMAP_ADDR_HD1, error)) {
		g_prefix_error (error,
				"failed to erase header1 sector at 0x%x: ",
				(guint) VLI_USBHUB_FLASHMAP_ADDR_HD1);
		return FALSE;
	}
	if (!fu_vli_device_spi_write_block (FU_VLI_DEVICE (self), VLI_USBHUB_FLASHMAP_ADDR_HD1,
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
			if (!fu_vli_device_spi_erase_sector (FU_VLI_DEVICE (self), VLI_USBHUB_FLASHMAP_ADDR_HD2, error)) {
				g_prefix_error (error, "failed to erase sector at header 1: ");
				return FALSE;
			}
			if (!fu_vli_device_spi_write_block (FU_VLI_DEVICE (self), VLI_USBHUB_FLASHMAP_ADDR_HD1_BACKUP,
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
		if (!fu_vli_device_spi_read_block (FU_VLI_DEVICE (self), VLI_USBHUB_FLASHMAP_ADDR_HD1_BACKUP,
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
	if (!fu_vli_device_spi_erase (FU_VLI_DEVICE (self), hd2_fw_addr, hd2_fw_sz, error))
		return FALSE;

	/* perform the actual write */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_vli_device_spi_write (FU_VLI_DEVICE (self),
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
	if (!fu_vli_device_spi_erase_sector (FU_VLI_DEVICE (self), VLI_USBHUB_FLASHMAP_ADDR_HD2, error)) {
		g_prefix_error (error, "failed to erase sectors for HD2: ");
		return FALSE;
	}
	if (!fu_vli_device_spi_write_block (FU_VLI_DEVICE (self),
					    VLI_USBHUB_FLASHMAP_ADDR_HD2,
					    (const guint8 *) &self->hd2_hdr,
					    sizeof(self->hd2_hdr),
					    error)) {
		g_prefix_error (error, "failed to write HD2: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static FuFirmware *
fu_vli_usbhub_device_read_firmware (FuDevice *device, GError **error)
{
	FuVliUsbhubDevice *self = FU_VLI_USBHUB_DEVICE (device);
	g_autoptr(GBytes) fw = NULL;
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_VERIFY);
	fw = fu_vli_device_spi_read (FU_VLI_DEVICE (self), 0x0,
				     fu_device_get_firmware_size_max (device),
				     error);
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

static void
fu_vli_usbhub_device_init (FuVliUsbhubDevice *self)
{
	fu_device_add_icon (FU_DEVICE (self), "audio-card");
	fu_device_set_protocol (FU_DEVICE (self), "com.vli.usbhub");
	fu_device_set_remove_delay (FU_DEVICE (self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
}

static void
fu_vli_usbhub_device_class_init (FuVliUsbhubDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuVliDeviceClass *klass_vli_device = FU_VLI_DEVICE_CLASS (klass);
	klass_device->probe = fu_vli_usbhub_device_probe;
	klass_device->read_firmware = fu_vli_usbhub_device_read_firmware;
	klass_device->write_firmware = fu_vli_usbhub_device_write_firmware;
	klass_device->prepare_firmware = fu_vli_usbhub_device_prepare_firmware;
	klass_vli_device->to_string = fu_vli_usbhub_device_to_string;
	klass_vli_device->setup = fu_vli_usbhub_device_setup;
	klass_vli_device->reset = fu_vli_usbhub_device_reset;
	klass_vli_device->spi_chip_erase = fu_vli_usbhub_device_spi_chip_erase;
	klass_vli_device->spi_sector_erase = fu_vli_usbhub_device_spi_sector_erase;
	klass_vli_device->spi_read_data = fu_vli_usbhub_device_spi_read_data;
	klass_vli_device->spi_read_status = fu_vli_usbhub_device_spi_read_status;
	klass_vli_device->spi_write_data = fu_vli_usbhub_device_spi_write_data;
	klass_vli_device->spi_write_enable = fu_vli_usbhub_device_spi_write_enable;
	klass_vli_device->spi_write_status = fu_vli_usbhub_device_spi_write_status;
}
