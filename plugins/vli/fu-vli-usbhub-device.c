/*
 * Copyright 2017 VIA Corporation
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-vli-struct.h"
#include "fu-vli-usbhub-common.h"
#include "fu-vli-usbhub-device.h"
#include "fu-vli-usbhub-firmware.h"
#include "fu-vli-usbhub-msp430-device.h"
#include "fu-vli-usbhub-pd-device.h"
#include "fu-vli-usbhub-rtd21xx-device.h"

struct _FuVliUsbhubDevice {
	FuVliDevice parent_instance;
	gboolean disable_powersave;
	guint8 update_protocol;
	GByteArray *st_hd1; /* factory */
	GByteArray *st_hd2; /* update */
};

G_DEFINE_TYPE(FuVliUsbhubDevice, fu_vli_usbhub_device, FU_TYPE_VLI_DEVICE)

#define FU_VLI_USBHUB_DEVICE_FLAG_ATTACH_WITH_GPIOB	 "attach-with-gpiob"
#define FU_VLI_USBHUB_DEVICE_FLAG_USB2			 "usb2"
#define FU_VLI_USBHUB_DEVICE_FLAG_USB3			 "usb3"
#define FU_VLI_USBHUB_DEVICE_FLAG_UNLOCK_LEGACY813	 "unlock-legacy813"
#define FU_VLI_USBHUB_DEVICE_FLAG_HAS_SHARED_SPI_PD	 "has-shared-spi-pd"
#define FU_VLI_USBHUB_DEVICE_FLAG_HAS_MSP430		 "has-msp430"
#define FU_VLI_USBHUB_DEVICE_FLAG_HAS_RTD21XX		 "has-rtd21xx"
#define FU_VLI_USBHUB_DEVICE_FLAG_ATTACH_WITH_USB_CABLE	 "attach-with-usb"
#define FU_VLI_USBHUB_DEVICE_FLAG_ATTACH_WITH_POWER_CORD "attach-with-power"

static void
fu_vli_usbhub_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuVliUsbhubDevice *self = FU_VLI_USBHUB_DEVICE(device);
	fwupd_codec_string_append_bool(str, idt, "DisablePowersave", self->disable_powersave);
	fwupd_codec_string_append_hex(str, idt, "UpdateProtocol", self->update_protocol);
	if (self->update_protocol >= 0x2) {
		g_autofree gchar *st_hd1str = fu_struct_vli_usbhub_hdr_to_string(self->st_hd1);
		fwupd_codec_string_append(str, idt, "H1Hdr@0x0", st_hd1str);
		if (fu_struct_vli_usbhub_hdr_get_dev_id(self->st_hd2) != 0xFFFF) {
			g_autofree gchar *st_hd2str =
			    fu_struct_vli_usbhub_hdr_to_string(self->st_hd2);
			fwupd_codec_string_append(str, idt, "H2Hdr@0x1000", st_hd2str);
		}
	}
}

static guint8
fu_vli_usbhub_device_header_crc8(GByteArray *hdr)
{
	return fu_crc8(FU_CRC_KIND_B8_STANDARD, hdr->data, hdr->len - 1);
}

static gboolean
fu_vli_usbhub_device_vdr_unlock_813(FuVliUsbhubDevice *self, GError **error)
{
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    0x85,
					    0x8786,
					    0x8988,
					    NULL,
					    0x0,
					    NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL,
					    error)) {
		g_prefix_error(error, "failed to UnLock_VL813: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_read_reg(FuVliUsbhubDevice *self, guint16 addr, guint8 *buf, GError **error)
{
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_DEVICE_TO_HOST,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    addr >> 8,
					    addr & 0xff,
					    0x0,
					    buf,
					    0x1,
					    NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL,
					    error)) {
		g_prefix_error(error, "failed to read register 0x%x: ", addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_write_reg(FuVliUsbhubDevice *self, guint16 addr, guint8 value, GError **error)
{
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    addr >> 8,
					    addr & 0xff,
					    (guint16)value,
					    NULL,
					    0x0,
					    NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL,
					    error)) {
		g_prefix_error(error, "failed to write register 0x%x: ", addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_spi_read_status(FuVliDevice *self, guint8 *status, GError **error)
{
	guint8 spi_cmd = 0x0;
	if (!fu_cfi_device_get_cmd(fu_vli_device_get_cfi_device(self),
				   FU_CFI_DEVICE_CMD_READ_STATUS,
				   &spi_cmd,
				   error))
		return FALSE;
	return fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					      FU_USB_DIRECTION_DEVICE_TO_HOST,
					      FU_USB_REQUEST_TYPE_VENDOR,
					      FU_USB_RECIPIENT_DEVICE,
					      0xc1,
					      spi_cmd,
					      0x0000,
					      status,
					      0x1,
					      NULL,
					      FU_VLI_DEVICE_TIMEOUT,
					      NULL,
					      error);
}

static gboolean
fu_vli_usbhub_device_spi_read_data(FuVliDevice *self,
				   guint32 addr,
				   guint8 *buf,
				   gsize bufsz,
				   GError **error)
{
	guint8 spi_cmd = 0x0;
	guint16 value;
	guint16 index;
	if (!fu_cfi_device_get_cmd(fu_vli_device_get_cfi_device(self),
				   FU_CFI_DEVICE_CMD_READ_DATA,
				   &spi_cmd,
				   error))
		return FALSE;
	value = ((addr >> 8) & 0xff00) | spi_cmd;
	index = ((addr << 8) & 0xff00) | ((addr >> 8) & 0x00ff);
	return fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					      FU_USB_DIRECTION_DEVICE_TO_HOST,
					      FU_USB_REQUEST_TYPE_VENDOR,
					      FU_USB_RECIPIENT_DEVICE,
					      0xc4,
					      value,
					      index,
					      buf,
					      bufsz,
					      NULL,
					      FU_VLI_DEVICE_TIMEOUT,
					      NULL,
					      error);
}

static gboolean
fu_vli_usbhub_device_spi_write_status(FuVliDevice *self, guint8 status, GError **error)
{
	guint8 spi_cmd = 0x0;
	if (!fu_cfi_device_get_cmd(fu_vli_device_get_cfi_device(self),
				   FU_CFI_DEVICE_CMD_WRITE_STATUS,
				   &spi_cmd,
				   error))
		return FALSE;
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    0xd1,
					    spi_cmd,
					    0x0000,
					    &status,
					    0x1,
					    NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL,
					    error)) {
		return FALSE;
	}

	/* Fix_For_GD_&_EN_SPI_Flash */
	fu_device_sleep(FU_DEVICE(self), 100); /* ms */
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_spi_write_enable(FuVliDevice *self, GError **error)
{
	guint8 spi_cmd = 0x0;
	if (!fu_cfi_device_get_cmd(fu_vli_device_get_cfi_device(self),
				   FU_CFI_DEVICE_CMD_WRITE_EN,
				   &spi_cmd,
				   error))
		return FALSE;
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    0xd1,
					    spi_cmd,
					    0x0000,
					    NULL,
					    0x0,
					    NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL,
					    error)) {
		g_prefix_error(error, "failed to write enable SPI: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_spi_chip_erase(FuVliDevice *self, GError **error)
{
	guint8 spi_cmd = 0x0;
	if (!fu_cfi_device_get_cmd(fu_vli_device_get_cfi_device(self),
				   FU_CFI_DEVICE_CMD_CHIP_ERASE,
				   &spi_cmd,
				   error))
		return FALSE;
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    0xd1,
					    spi_cmd,
					    0x0000,
					    NULL,
					    0x0,
					    NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL,
					    error)) {
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_spi_sector_erase(FuVliDevice *self, guint32 addr, GError **error)
{
	guint8 spi_cmd = 0x0;
	guint16 value;
	guint16 index;
	if (!fu_cfi_device_get_cmd(fu_vli_device_get_cfi_device(self),
				   FU_CFI_DEVICE_CMD_SECTOR_ERASE,
				   &spi_cmd,
				   error))
		return FALSE;
	value = ((addr >> 8) & 0xff00) | spi_cmd;
	index = ((addr << 8) & 0xff00) | ((addr >> 8) & 0x00ff);
	return fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					      FU_USB_DIRECTION_HOST_TO_DEVICE,
					      FU_USB_REQUEST_TYPE_VENDOR,
					      FU_USB_RECIPIENT_DEVICE,
					      0xd4,
					      value,
					      index,
					      NULL,
					      0x0,
					      NULL,
					      FU_VLI_DEVICE_TIMEOUT,
					      NULL,
					      error);
}

static gboolean
fu_vli_usbhub_device_spi_write_data(FuVliDevice *self,
				    guint32 addr,
				    const guint8 *buf,
				    gsize bufsz,
				    GError **error)
{
	guint8 spi_cmd = 0x0;
	guint16 value;
	guint16 index;
	g_autofree guint8 *buf_mut = NULL;

	if (!fu_cfi_device_get_cmd(fu_vli_device_get_cfi_device(self),
				   FU_CFI_DEVICE_CMD_PAGE_PROG,
				   &spi_cmd,
				   error))
		return FALSE;
	value = ((addr >> 8) & 0xff00) | spi_cmd;
	index = ((addr << 8) & 0xff00) | ((addr >> 8) & 0x00ff);
	buf_mut = fu_memdup_safe(buf, bufsz, error);
	if (buf_mut == NULL)
		return FALSE;
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    0xd4,
					    value,
					    index,
					    buf_mut,
					    bufsz,
					    NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL,
					    error)) {
		return FALSE;
	}

	/* patch for PUYA flash write data command */
	fu_device_sleep(FU_DEVICE(self), 1); /* ms */
	return TRUE;
}

#define VL817_ADDR_GPIO_OUTPUT_ENABLE	0xF6A0 /* 0=input, 1=output */
#define VL817_ADDR_GPIO_SET_OUTPUT_DATA 0xF6A1 /* 0=low, 1=high */
#define VL817_ADDR_GPIO_GET_INPUT_DATA	0xF6A2 /* 0=low, 1=high */

static gboolean
fu_vli_usbhub_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy_with_fallback(device);
	g_autoptr(GError) error_local = NULL;

	/* the user has to do something */
	if (fu_device_has_private_flag(device, FU_VLI_USBHUB_DEVICE_FLAG_ATTACH_WITH_USB_CABLE)) {
		g_autoptr(FwupdRequest) request = fwupd_request_new();
		fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
		fwupd_request_set_id(request, FWUPD_REQUEST_ID_REMOVE_REPLUG);
		fwupd_request_add_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
		if (!fu_device_emit_request(device, request, progress, error))
			return FALSE;
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
		return TRUE;
	}
	if (fu_device_has_private_flag(device, FU_VLI_USBHUB_DEVICE_FLAG_ATTACH_WITH_POWER_CORD)) {
		g_autoptr(FwupdRequest) request = fwupd_request_new();
		fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
		fwupd_request_set_id(request, FWUPD_REQUEST_ID_REPLUG_POWER);
		fwupd_request_add_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
		if (!fu_device_emit_request(device, request, progress, error))
			return FALSE;
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
		return TRUE;
	}

	/* some hardware has to toggle a GPIO to reset the entire PCB */
	if (fu_vli_device_get_kind(FU_VLI_DEVICE(proxy)) == FU_VLI_DEVICE_KIND_VL817 &&
	    fu_device_has_private_flag(device, FU_VLI_USBHUB_DEVICE_FLAG_ATTACH_WITH_GPIOB)) {
		guint8 tmp = 0x0;

		/* set GPIOB output enable */
		g_info("using GPIO reset for %s", fu_device_get_id(device));
		if (!fu_vli_usbhub_device_read_reg(FU_VLI_USBHUB_DEVICE(proxy),
						   VL817_ADDR_GPIO_OUTPUT_ENABLE,
						   &tmp,
						   error))
			return FALSE;
		if (!fu_vli_usbhub_device_write_reg(FU_VLI_USBHUB_DEVICE(proxy),
						    VL817_ADDR_GPIO_OUTPUT_ENABLE,
						    tmp | (1 << 1),
						    error))
			return FALSE;

		/* toggle GPIOB to trigger reset */
		if (!fu_vli_usbhub_device_read_reg(FU_VLI_USBHUB_DEVICE(proxy),
						   VL817_ADDR_GPIO_SET_OUTPUT_DATA,
						   &tmp,
						   error))
			return FALSE;
		if (!fu_vli_usbhub_device_write_reg(FU_VLI_USBHUB_DEVICE(proxy),
						    VL817_ADDR_GPIO_SET_OUTPUT_DATA,
						    tmp ^ (1 << 1),
						    error))
			return FALSE;
	} else {
		/* replug, and ignore the device going away */
		if (!fu_usb_device_control_transfer(FU_USB_DEVICE(FU_USB_DEVICE(proxy)),
						    FU_USB_DIRECTION_HOST_TO_DEVICE,
						    FU_USB_REQUEST_TYPE_VENDOR,
						    FU_USB_RECIPIENT_DEVICE,
						    0xf6,
						    0x0040,
						    0x0002,
						    NULL,
						    0x0,
						    NULL,
						    FU_VLI_DEVICE_TIMEOUT,
						    NULL,
						    &error_local)) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND) ||
			    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT) ||
			    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_INTERNAL)) {
				g_debug("ignoring %s", error_local->message);
			} else {
				g_propagate_prefixed_error(error,
							   g_steal_pointer(&error_local),
							   "failed to restart device: ");
				return FALSE;
			}
		}
	}

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

/* disable hub sleep states -- not really required by 815~ hubs */
static gboolean
fu_vli_usbhub_device_disable_u1u2(FuVliUsbhubDevice *self, GError **error)
{
	guint8 buf = 0x0;

	/* clear Reg[0xF8A2] bit_3 & bit_7 -- also
	 * clear Total Switch / Flag To Disable FW Auto-Reload Function */
	if (!fu_vli_usbhub_device_read_reg(self, 0xf8a2, &buf, error))
		return FALSE;
	buf &= 0x77;
	if (!fu_vli_usbhub_device_write_reg(self, 0xf8a2, buf, error))
		return FALSE;

	/* clear Reg[0xF832] bit_0 & bit_1 */
	if (!fu_vli_usbhub_device_read_reg(self, 0xf832, &buf, error))
		return FALSE;
	buf &= 0xfc;
	if (!fu_vli_usbhub_device_write_reg(self, 0xf832, buf, error))
		return FALSE;

	/* clear Reg[0xF920] bit_1 & bit_2 */
	if (!fu_vli_usbhub_device_read_reg(self, 0xf920, &buf, error))
		return FALSE;
	buf &= 0xf9;
	if (!fu_vli_usbhub_device_write_reg(self, 0xf920, buf, error))
		return FALSE;

	/* set Reg[0xF836] bit_3 */
	if (!fu_vli_usbhub_device_read_reg(self, 0xf836, &buf, error))
		return FALSE;
	buf |= 0x08;
	if (!fu_vli_usbhub_device_write_reg(self, 0xf836, buf, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_guess_kind(FuVliUsbhubDevice *self, GError **error)
{
	guint8 b811P812 = 0x0;
	guint8 pkgtype = 0x0;
	guint8 chipid1 = 0x0;
	guint8 chipid2 = 0x0;
	guint8 chipid12 = 0x0;
	guint8 chipid22 = 0x0;
	guint8 chipver = 0x0;
	guint8 chipver2 = 0x0;
	gint tPid = fu_device_get_pid(FU_DEVICE(self)) & 0x0fff;

	if (!fu_vli_usbhub_device_read_reg(self, 0xf88c, &chipver, error)) {
		g_prefix_error(error, "Read_ChipVer failed: ");
		return FALSE;
	}
	g_debug("chipver = 0x%02x", chipver);
	if (!fu_vli_usbhub_device_read_reg(self, 0xf63f, &chipver2, error)) {
		g_prefix_error(error, "Read_ChipVer2 failed: ");
		return FALSE;
	}
	g_debug("chipver2 = 0x%02x", chipver2);
	if (!fu_vli_usbhub_device_read_reg(self, 0xf800, &b811P812, error)) {
		g_prefix_error(error, "Read_811P812 failed: ");
		return FALSE;
	}
	g_debug("b811P812 = 0x%02x", b811P812);
	if (!fu_vli_usbhub_device_read_reg(self, 0xf88e, &chipid1, error)) {
		g_prefix_error(error, "Read_ChipID1 failed: ");
		return FALSE;
	}
	g_debug("chipid1 = 0x%02x", chipid1);
	if (!fu_vli_usbhub_device_read_reg(self, 0xf88f, &chipid2, error)) {
		g_prefix_error(error, "Read_ChipID2 failed: ");
		return FALSE;
	}
	g_debug("chipid2 = 0x%02x", chipid2);
	if (!fu_vli_usbhub_device_read_reg(self, 0xf64e, &chipid12, error)) {
		g_prefix_error(error, "Read_ChipID12 failed: ");
		return FALSE;
	}
	g_debug("chipid12 = 0x%02x", chipid12);
	if (!fu_vli_usbhub_device_read_reg(self, 0xf64f, &chipid22, error)) {
		g_prefix_error(error, "Read_ChipID22 failed: ");
		return FALSE;
	}
	g_debug("chipid22 = 0x%02x", chipid22);
	if (!fu_vli_usbhub_device_read_reg(self, 0xf651, &pkgtype, error)) {
		g_prefix_error(error, "Read_820Q7Q8 failed: ");
		return FALSE;
	}
	g_debug("pkgtype = 0x%02x", pkgtype);

	if (chipid2 == 0x35 && chipid1 == 0x07) {
		fu_vli_device_set_kind(FU_VLI_DEVICE(self), FU_VLI_DEVICE_KIND_VL210);
	} else if (chipid2 == 0x35 && chipid1 == 0x18) {
		if (chipver == 0xF0) {
			/* packet type determines device kind for VL819-VL822, minus VL820 */
			switch ((pkgtype >> 1) & 0x07) {
			/* VL822Q7 */
			case 0x00:
				fu_vli_device_set_kind(FU_VLI_DEVICE(self),
						       FU_VLI_DEVICE_KIND_VL822Q7);
				break;
			/* VL822Q5 */
			case 0x01:
				fu_vli_device_set_kind(FU_VLI_DEVICE(self),
						       FU_VLI_DEVICE_KIND_VL822Q5);
				break;
			/* VL822Q8 */
			case 0x02:
				fu_vli_device_set_kind(FU_VLI_DEVICE(self),
						       FU_VLI_DEVICE_KIND_VL822Q8);
				break;
			/* VL821Q7 */
			case 0x04:
				fu_vli_device_set_kind(FU_VLI_DEVICE(self),
						       FU_VLI_DEVICE_KIND_VL821Q7);
				break;
			/* VL819Q7 */
			case 0x05:
				fu_vli_device_set_kind(FU_VLI_DEVICE(self),
						       FU_VLI_DEVICE_KIND_VL819Q7);
				break;
			/* VL821Q8 */
			case 0x06:
				fu_vli_device_set_kind(FU_VLI_DEVICE(self),
						       FU_VLI_DEVICE_KIND_VL821Q8);
				break;
			/* VL819Q8 */
			case 0x07:
				fu_vli_device_set_kind(FU_VLI_DEVICE(self),
						       FU_VLI_DEVICE_KIND_VL819Q8);
				break;
			default:
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "packet Type match failed: ");
				return FALSE;
			}
		} else {
			if (pkgtype & (1 << 2))
				fu_vli_device_set_kind(FU_VLI_DEVICE(self),
						       FU_VLI_DEVICE_KIND_VL820Q8);
			else
				fu_vli_device_set_kind(FU_VLI_DEVICE(self),
						       FU_VLI_DEVICE_KIND_VL820Q7);
		}
	} else if (chipid2 == 0x35 && chipid1 == 0x31) {
		fu_vli_device_set_kind(FU_VLI_DEVICE(self), FU_VLI_DEVICE_KIND_VL815);
	} else if (chipid2 == 0x35 && chipid1 == 0x38) {
		fu_vli_device_set_kind(FU_VLI_DEVICE(self), FU_VLI_DEVICE_KIND_VL817);
	} else if (chipid2 == 0x35 && chipid1 == 0x90) {
		fu_vli_device_set_kind(FU_VLI_DEVICE(self), FU_VLI_DEVICE_KIND_VL817S);
	} else if (chipid2 == 0x35 && chipid1 == 0x95) {
		fu_vli_device_set_kind(FU_VLI_DEVICE(self), FU_VLI_DEVICE_KIND_VL822T);
	} else if (chipid2 == 0x35 && chipid1 == 0x99) {
		if (chipver == 0xC0 || chipver == 0xC1)
			fu_vli_device_set_kind(FU_VLI_DEVICE(self), FU_VLI_DEVICE_KIND_VL822C0);
		else {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "not supported 99 type");
			return FALSE;
		}
	} else if (chipid2 == 0x35 && chipid1 == 0x66) {
		if (chipver <= 0xC0)
			fu_vli_device_set_kind(FU_VLI_DEVICE(self), FU_VLI_DEVICE_KIND_VL830);
		else
			fu_vli_device_set_kind(FU_VLI_DEVICE(self), FU_VLI_DEVICE_KIND_VL832);
	} else if (chipid2 == 0x35 && chipid1 == 0x45) {
		fu_vli_device_set_kind(FU_VLI_DEVICE(self), FU_VLI_DEVICE_KIND_VL211);
	} else if (chipid22 == 0x35 && chipid12 == 0x53) {
		fu_vli_device_set_kind(FU_VLI_DEVICE(self), FU_VLI_DEVICE_KIND_VL120);
	} else if (chipid22 == 0x35 && chipid12 == 0x92) {
		fu_vli_device_set_kind(FU_VLI_DEVICE(self), FU_VLI_DEVICE_KIND_VL122);
	} else if (tPid == 0x810) {
		fu_vli_device_set_kind(FU_VLI_DEVICE(self), FU_VLI_DEVICE_KIND_VL810);
	} else if (tPid == 0x811) {
		fu_vli_device_set_kind(FU_VLI_DEVICE(self), FU_VLI_DEVICE_KIND_VL811);
	} else if ((b811P812 & ((1 << 5) | (1 << 4))) == 0) {
		if (chipver == 0x10)
			fu_vli_device_set_kind(FU_VLI_DEVICE(self), FU_VLI_DEVICE_KIND_VL811PB0);
		else
			fu_vli_device_set_kind(FU_VLI_DEVICE(self), FU_VLI_DEVICE_KIND_VL811PB3);
	} else if ((b811P812 & ((1 << 5) | (1 << 4))) == (1 << 4)) {
		fu_vli_device_set_kind(FU_VLI_DEVICE(self), FU_VLI_DEVICE_KIND_VL812Q4S);
	} else if ((b811P812 & ((1 << 5) | (1 << 4))) == ((1 << 5) | (1 << 4))) {
		if (chipver == 0x10)
			fu_vli_device_set_kind(FU_VLI_DEVICE(self), FU_VLI_DEVICE_KIND_VL812B0);
		else
			fu_vli_device_set_kind(FU_VLI_DEVICE(self), FU_VLI_DEVICE_KIND_VL812B3);
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "hardware is not supported");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_probe(FuDevice *device, GError **error)
{
	FuVliUsbhubDevice *self = FU_VLI_USBHUB_DEVICE(device);
	guint16 usbver = fu_usb_device_get_spec(FU_USB_DEVICE(device));

	/* quirks now applied... */
	if (usbver > 0x0300 || fu_device_has_private_flag(device, FU_VLI_USBHUB_DEVICE_FLAG_USB3)) {
		fu_device_set_summary(device, "USB 3.x hub");
		/* prefer to show the USB 3 device and only fall back to the
		 * USB 2 version as a recovery */
		fu_device_set_priority(device, 1);
	} else if (usbver > 0x0200 ||
		   fu_device_has_private_flag(device, FU_VLI_USBHUB_DEVICE_FLAG_USB2)) {
		fu_device_set_summary(device, "USB 2.x hub");
	} else {
		fu_device_set_summary(device, "USB hub");
	}

	/* only some required */
	if (fu_device_has_private_flag(device, FU_VLI_USBHUB_DEVICE_FLAG_ATTACH_WITH_USB_CABLE) ||
	    fu_device_has_private_flag(device, FU_VLI_USBHUB_DEVICE_FLAG_ATTACH_WITH_POWER_CORD)) {
		fu_device_add_request_flag(FU_DEVICE(self),
					   FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	}

	return TRUE;
}

static gboolean
fu_vli_usbhub_device_pd_setup(FuVliUsbhubDevice *self, GError **error)
{
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(GError) error_local = NULL;

	/* add child */
	dev = fu_vli_usbhub_pd_device_new(self);
	if (!fu_device_probe(dev, error))
		return FALSE;
	if (!fu_device_setup(dev, &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_debug("%s", error_local->message);
		} else {
			g_warning("cannot create PD device: %s", error_local->message);
		}
		return TRUE;
	}
	fu_device_add_child(FU_DEVICE(self), dev);
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_msp430_setup(FuVliUsbhubDevice *self, GError **error)
{
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(GError) error_local = NULL;

	/* add child */
	dev = fu_vli_usbhub_msp430_device_new(self);
	if (!fu_device_probe(dev, error))
		return FALSE;
	if (!fu_device_setup(dev, &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_debug("%s", error_local->message);
		} else {
			g_warning("cannot create MSP430 I²C device: %s", error_local->message);
		}
		return TRUE;
	}
	fu_device_add_child(FU_DEVICE(self), dev);
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_rtd21xx_setup(FuVliUsbhubDevice *self, GError **error)
{
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(GError) error_local = NULL;

	/* add child */
	dev = fu_vli_usbhub_rtd21xx_device_new(self);
	if (!fu_device_probe(dev, error))
		return FALSE;
	if (!fu_device_setup(dev, &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_debug("%s", error_local->message);
		} else {
			g_warning("cannot create RTD21XX I²C device: %s", error_local->message);
		}
		return TRUE;
	}
	fu_device_add_child(FU_DEVICE(self), dev);
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_ready(FuDevice *device, GError **error)
{
	FuVliUsbhubDevice *self = FU_VLI_USBHUB_DEVICE(device);
	g_autoptr(GError) error_tmp = NULL;

	/* FuUsbDevice->ready */
	if (!FU_DEVICE_CLASS(fu_vli_usbhub_device_parent_class)->ready(device, error))
		return FALSE;

	/* to expose U3 hub, wait until fw is stable before sending VDR */
	fu_device_sleep(FU_DEVICE(self), 100); /* ms */

	/* try to read a block of data which will fail for 813-type devices */
	if (fu_device_has_private_flag(device, FU_VLI_USBHUB_DEVICE_FLAG_UNLOCK_LEGACY813) &&
	    !fu_vli_device_spi_read_block(FU_VLI_DEVICE(self),
					  0x0,
					  self->st_hd1->data,
					  self->st_hd1->len,
					  &error_tmp)) {
		g_warning("failed to read, trying to unlock 813: %s", error_tmp->message);
		if (!fu_vli_usbhub_device_vdr_unlock_813(self, error))
			return FALSE;
		if (!fu_vli_device_spi_read_block(FU_VLI_DEVICE(self),
						  0x0,
						  self->st_hd1->data,
						  self->st_hd1->len,
						  error)) {
			g_prefix_error(error, "813 unlock fail: ");
			return FALSE;
		}
		g_debug("813 unlock OK");
		/* VL813 & VL210 have same PID (0x0813), and only VL813 can reply */
		fu_vli_device_set_kind(FU_VLI_DEVICE(self), FU_VLI_DEVICE_KIND_VL813);
	} else {
		if (!fu_vli_usbhub_device_guess_kind(self, error))
			return FALSE;
	}

	/* read HD1 (factory) header */
	if (!fu_vli_device_spi_read_block(FU_VLI_DEVICE(self),
					  VLI_USBHUB_FLASHMAP_ADDR_HD1,
					  self->st_hd1->data,
					  self->st_hd1->len,
					  error)) {
		g_prefix_error(error, "failed to read HD1 header: ");
		return FALSE;
	}

	/* detect update protocol from the device ID */
	switch (fu_struct_vli_usbhub_hdr_get_dev_id(self->st_hd1)) {
	/* VL810~VL813 */
	case 0x0d12:
		self->update_protocol = 0x1;
		self->disable_powersave = TRUE;
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
		fu_device_set_install_duration(FU_DEVICE(self), 10); /* seconds */
		break;
	/* VL817~ */
	case 0x0507:
	case 0x0518:
	case 0x0538:
	case 0x0545:
	case 0x0553:
	case 0x0590:
	case 0x0592:
	case 0x0595:
		self->update_protocol = 0x2;
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
		fu_device_set_install_duration(FU_DEVICE(self), 15); /* seconds */
		break;
	case 0x0566:
		self->update_protocol = 0x3;
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
		fu_device_set_install_duration(FU_DEVICE(self), 30); /* seconds */
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "hardware is not supported, dev_id=0x%x",
			    (guint)fu_struct_vli_usbhub_hdr_get_dev_id(self->st_hd1));
		return FALSE;
	}

	/* read HD2 (update) header */
	if (self->update_protocol >= 0x2) {
		if (!fu_vli_device_spi_read_block(FU_VLI_DEVICE(self),
						  VLI_USBHUB_FLASHMAP_ADDR_HD2,
						  self->st_hd2->data,
						  self->st_hd2->len,
						  error)) {
			g_prefix_error(error, "failed to read HD2 header: ");
			return FALSE;
		}
	}

	/* detect the PD child */
	if (fu_device_has_private_flag(device, FU_VLI_USBHUB_DEVICE_FLAG_HAS_SHARED_SPI_PD)) {
		if (!fu_vli_usbhub_device_pd_setup(self, error))
			return FALSE;
	}

	/* detect the I²C child */
	if (fu_usb_device_get_spec(FU_USB_DEVICE(self)) >= 0x0300 &&
	    fu_device_has_private_flag(device, FU_VLI_USBHUB_DEVICE_FLAG_HAS_MSP430)) {
		if (!fu_vli_usbhub_device_msp430_setup(self, error))
			return FALSE;
	}
	if (fu_device_has_private_flag(device, FU_VLI_USBHUB_DEVICE_FLAG_HAS_RTD21XX)) {
		if (!fu_vli_usbhub_device_rtd21xx_setup(self, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static FuFirmware *
fu_vli_usbhub_device_prepare_firmware(FuDevice *device,
				      GInputStream *stream,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuVliUsbhubDevice *self = FU_VLI_USBHUB_DEVICE(device);
	FuVliDeviceKind device_kind;
	g_autoptr(FuFirmware) firmware = fu_vli_usbhub_firmware_new();

	/* check is compatible with firmware */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	device_kind = fu_vli_usbhub_firmware_get_device_kind(FU_VLI_USBHUB_FIRMWARE(firmware));
	if (fu_vli_device_get_kind(FU_VLI_DEVICE(self)) != device_kind) {
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_FILE,
		    "firmware incompatible, got %s, expected %s",
		    fu_vli_device_kind_to_string(device_kind),
		    fu_vli_device_kind_to_string(fu_vli_device_get_kind(FU_VLI_DEVICE(self))));
		return NULL;
	}
	if (fu_struct_vli_usbhub_hdr_get_dev_id(self->st_hd1) !=
	    fu_vli_usbhub_firmware_get_device_id(FU_VLI_USBHUB_FIRMWARE(firmware))) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware incompatible, got 0x%04x, expected 0x%04x",
			    fu_vli_usbhub_firmware_get_device_id(FU_VLI_USBHUB_FIRMWARE(firmware)),
			    (guint)fu_struct_vli_usbhub_hdr_get_dev_id(self->st_hd1));
		return NULL;
	}

	/* we could check this against flags */
	g_info("parsed version: %s", fu_firmware_get_version(firmware));
	return g_steal_pointer(&firmware);
}

static gboolean
fu_vli_usbhub_device_update_v1(FuVliUsbhubDevice *self,
			       FuFirmware *firmware,
			       FuProgress *progress,
			       GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf;
	g_autoptr(GBytes) fw = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 20, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 80, NULL);

	/* simple image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* erase */
	if (!fu_vli_device_spi_erase_all(FU_VLI_DEVICE(self),
					 fu_progress_get_child(progress),
					 error)) {
		g_prefix_error(error, "failed to erase chip: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write in chunks */
	buf = g_bytes_get_data(fw, &bufsz);
	if (!fu_vli_device_spi_write(FU_VLI_DEVICE(self),
				     0x0,
				     buf,
				     bufsz,
				     fu_progress_get_child(progress),
				     error))
		return FALSE;

	/* success */
	fu_progress_step_done(progress);
	return TRUE;
}

/* if no header1 or ROM code update, write data directly */
static gboolean
fu_vli_usbhub_device_update_v2_recovery(FuVliUsbhubDevice *self,
					GBytes *fw,
					FuProgress *progress,
					GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 20, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 80, NULL);

	/* erase */
	for (guint32 addr = 0; addr < bufsz; addr += 0x1000) {
		if (!fu_vli_device_spi_erase_sector(FU_VLI_DEVICE(self), addr, error)) {
			g_prefix_error(error, "failed to erase sector @0x%x: ", addr);
			return FALSE;
		}
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)addr,
						bufsz);
	}
	fu_progress_step_done(progress);

	/* write in chunks */
	if (!fu_vli_device_spi_write(FU_VLI_DEVICE(self),
				     VLI_USBHUB_FLASHMAP_ADDR_HD1,
				     buf,
				     bufsz,
				     fu_progress_get_child(progress),
				     error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_hd1_is_valid(GByteArray *hdr)
{
	if (fu_struct_vli_usbhub_hdr_get_prev_ptr(hdr) != VLI_USBHUB_FLASHMAP_IDX_INVALID)
		return FALSE;
	if (fu_struct_vli_usbhub_hdr_get_checksum(hdr) != fu_vli_usbhub_device_header_crc8(hdr))
		return FALSE;
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_hd1_recover(FuVliUsbhubDevice *self,
				 GByteArray *hdr,
				 FuProgress *progress,
				 GError **error)
{
	/* point to HD2, i.e. updated firmware */
	if (fu_struct_vli_usbhub_hdr_get_next_ptr(hdr) != VLI_USBHUB_FLASHMAP_IDX_HD2) {
		fu_struct_vli_usbhub_hdr_set_next_ptr(hdr, VLI_USBHUB_FLASHMAP_IDX_HD2);
		fu_struct_vli_usbhub_hdr_set_checksum(hdr, fu_vli_usbhub_device_header_crc8(hdr));
	}

	/* write new header block */
	if (!fu_vli_device_spi_erase_sector(FU_VLI_DEVICE(self),
					    VLI_USBHUB_FLASHMAP_ADDR_HD1,
					    error)) {
		g_prefix_error(error,
			       "failed to erase header1 sector at 0x%x: ",
			       (guint)VLI_USBHUB_FLASHMAP_ADDR_HD1);
		return FALSE;
	}
	if (!fu_vli_device_spi_write_block(FU_VLI_DEVICE(self),
					   VLI_USBHUB_FLASHMAP_ADDR_HD1,
					   hdr->data,
					   hdr->len,
					   progress,
					   error)) {
		g_prefix_error(error,
			       "failed to write header1 block at 0x%x: ",
			       (guint)VLI_USBHUB_FLASHMAP_ADDR_HD1);
		return FALSE;
	}

	/* update the cached copy */
	g_byte_array_unref(self->st_hd1);
	self->st_hd1 = g_byte_array_ref(hdr);
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_update_v2(FuVliUsbhubDevice *self,
			       FuFirmware *firmware,
			       FuProgress *progress,
			       GError **error)
{
	gsize buf_fwsz = 0;
	guint32 hd1_fw_sz;
	guint32 hd2_fw_sz;
	guint32 hd2_fw_addr;
	guint32 hd2_fw_offset;
	const guint8 *buf_fw;
	g_autoptr(GByteArray) st_hd = NULL;
	g_autoptr(GBytes) fw = NULL;

	/* simple image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* root header is valid */
	if (fu_vli_usbhub_device_hd1_is_valid(self->st_hd1)) {
		/* no update has ever been done */
		if (fu_struct_vli_usbhub_hdr_get_next_ptr(self->st_hd1) !=
		    VLI_USBHUB_FLASHMAP_IDX_HD2) {
			/* backup HD1 before recovering */
			if (!fu_vli_device_spi_erase_sector(FU_VLI_DEVICE(self),
							    VLI_USBHUB_FLASHMAP_ADDR_HD2,
							    error)) {
				g_prefix_error(error, "failed to erase sector at header 1: ");
				return FALSE;
			}
			if (!fu_vli_device_spi_write_block(FU_VLI_DEVICE(self),
							   VLI_USBHUB_FLASHMAP_ADDR_HD1_BACKUP,
							   self->st_hd1->data,
							   self->st_hd1->len,
							   progress,
							   error)) {
				g_prefix_error(error, "failed to write block at header 1: ");
				return FALSE;
			}
			if (!fu_vli_usbhub_device_hd1_recover(self,
							      self->st_hd1,
							      progress,
							      error)) {
				g_prefix_error(error, "failed to write header: ");
				return FALSE;
			}
		}

	} else {
		/* copy the header from the backup zone */
		g_info("HD1 was invalid, reading backup");
		if (!fu_vli_device_spi_read_block(FU_VLI_DEVICE(self),
						  VLI_USBHUB_FLASHMAP_ADDR_HD1_BACKUP,
						  self->st_hd1->data,
						  self->st_hd1->len,
						  error)) {
			g_prefix_error(error,
				       "failed to read root header from 0x%x: ",
				       (guint)VLI_USBHUB_FLASHMAP_ADDR_HD1_BACKUP);
			return FALSE;
		}
		if (!fu_vli_usbhub_device_hd1_is_valid(self->st_hd1)) {
			g_info("backup header is also invalid, starting recovery");
			return fu_vli_usbhub_device_update_v2_recovery(self, fw, progress, error);
		}
		if (!fu_vli_usbhub_device_hd1_recover(self, self->st_hd1, progress, error)) {
			g_prefix_error(error, "failed to get root header in backup zone: ");
			return FALSE;
		}
	}

	/* align the update fw address to the sector after the factory size */
	hd1_fw_sz = fu_struct_vli_usbhub_hdr_get_usb3_fw_sz(self->st_hd1);
	if (hd1_fw_sz > 0xF000) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "FW1 size abnormal 0x%x",
			    (guint)hd1_fw_sz);
		return FALSE;
	}
	hd2_fw_addr = (hd1_fw_sz + 0xfff) & 0xf000;
	hd2_fw_addr += VLI_USBHUB_FLASHMAP_ADDR_FW;

	/* get the size and offset of the update firmware */
	buf_fw = g_bytes_get_data(fw, &buf_fwsz);
	st_hd = fu_struct_vli_usbhub_hdr_parse(buf_fw, buf_fwsz, 0x0, error);
	if (st_hd == NULL)
		return FALSE;
	hd2_fw_sz = fu_struct_vli_usbhub_hdr_get_usb3_fw_sz(st_hd);
	hd2_fw_offset = fu_struct_vli_usbhub_hdr_get_usb3_fw_addr(st_hd);
	g_debug("FW2 @0x%x (length 0x%x, offset 0x%x)", hd2_fw_addr, hd2_fw_sz, hd2_fw_offset);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 72, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 20, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 8, "hd2");

	/* make space */
	if (!fu_vli_device_spi_erase(FU_VLI_DEVICE(self),
				     hd2_fw_addr,
				     hd2_fw_sz,
				     fu_progress_get_child(progress),
				     error))
		return FALSE;
	fu_progress_step_done(progress);

	/* perform the actual write */
	if (!fu_vli_device_spi_write(FU_VLI_DEVICE(self),
				     hd2_fw_addr,
				     buf_fw + hd2_fw_offset,
				     hd2_fw_sz,
				     fu_progress_get_child(progress),
				     error)) {
		g_prefix_error(error, "failed to write payload: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write new HD2 */
	fu_struct_vli_usbhub_hdr_set_usb3_fw_addr(st_hd, hd2_fw_addr & 0xFFFF);
	fu_struct_vli_usbhub_hdr_set_usb3_fw_addr_high(st_hd, hd2_fw_addr >> 16);
	fu_struct_vli_usbhub_hdr_set_prev_ptr(st_hd, VLI_USBHUB_FLASHMAP_IDX_HD1);
	fu_struct_vli_usbhub_hdr_set_next_ptr(st_hd, VLI_USBHUB_FLASHMAP_IDX_INVALID);
	fu_struct_vli_usbhub_hdr_set_checksum(st_hd, fu_vli_usbhub_device_header_crc8(st_hd));
	if (!fu_vli_device_spi_erase_sector(FU_VLI_DEVICE(self),
					    VLI_USBHUB_FLASHMAP_ADDR_HD2,
					    error)) {
		g_prefix_error(error, "failed to erase sectors for HD2: ");
		return FALSE;
	}
	if (!fu_vli_device_spi_write_block(FU_VLI_DEVICE(self),
					   VLI_USBHUB_FLASHMAP_ADDR_HD2,
					   st_hd->data,
					   st_hd->len,
					   fu_progress_get_child(progress),
					   error)) {
		g_prefix_error(error, "failed to write HD2: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	g_byte_array_unref(self->st_hd2);
	self->st_hd2 = g_byte_array_ref(st_hd);
	return TRUE;
}

static gboolean
fu_vli_usbhub_device_update_v3(FuVliUsbhubDevice *self,
			       FuFirmware *firmware,
			       FuProgress *progress,
			       GError **error)
{
	gsize buf_fwsz = 0;
	guint32 hd2_fw_sz;
	guint32 hd2_fw_addr;
	guint32 hd2_fw_offset;
	const guint8 *buf_fw;
	g_autoptr(GByteArray) st_hd = NULL;
	g_autoptr(GBytes) fw = NULL;

	/* simple image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* root header is valid */
	if (fu_vli_usbhub_device_hd1_is_valid(self->st_hd1)) {
		/* no update has ever been done */
		if (fu_struct_vli_usbhub_hdr_get_next_ptr(self->st_hd1) !=
		    VLI_USBHUB_FLASHMAP_IDX_HD2) {
			/* backup HD1 before recovering */
			if (!fu_vli_device_spi_erase_sector(FU_VLI_DEVICE(self),
							    VLI_USBHUB_FLASHMAP_ADDR_HD2,
							    error)) {
				g_prefix_error(error, "failed to erase sector at header 1: ");
				return FALSE;
			}
			if (!fu_vli_device_spi_write_block(FU_VLI_DEVICE(self),
							   VLI_USBHUB_FLASHMAP_ADDR_HD1_BACKUP,
							   self->st_hd1->data,
							   self->st_hd1->len,
							   progress,
							   error)) {
				g_prefix_error(error, "failed to write block at header 1: ");
				return FALSE;
			}
			if (!fu_vli_usbhub_device_hd1_recover(self,
							      self->st_hd1,
							      progress,
							      error)) {
				g_prefix_error(error, "failed to write header: ");
				return FALSE;
			}
		}

	} else {
		/* copy the header from the backup zone */
		g_info("HD1 was invalid, reading backup");
		if (!fu_vli_device_spi_read_block(FU_VLI_DEVICE(self),
						  VLI_USBHUB_FLASHMAP_ADDR_HD1_BACKUP,
						  self->st_hd1->data,
						  self->st_hd1->len,
						  error)) {
			g_prefix_error(error,
				       "failed to read root header from 0x%x: ",
				       (guint)VLI_USBHUB_FLASHMAP_ADDR_HD1_BACKUP);
			return FALSE;
		}
		if (!fu_vli_usbhub_device_hd1_is_valid(self->st_hd1)) {
			g_info("backup header is also invalid, starting recovery");
			return fu_vli_usbhub_device_update_v2_recovery(self, fw, progress, error);
		}
		if (!fu_vli_usbhub_device_hd1_recover(self, self->st_hd1, progress, error)) {
			g_prefix_error(error, "failed to get root header in backup zone: ");
			return FALSE;
		}
	}

	/* use fixed address for update fw */
	if (fu_vli_device_get_kind(FU_VLI_DEVICE(self)) == FU_VLI_DEVICE_KIND_VL830)
		hd2_fw_addr = 0x60000;
	else
		hd2_fw_addr = 0x80000;

	/* get the size and offset of the update firmware */
	buf_fw = g_bytes_get_data(fw, &buf_fwsz);
	st_hd = fu_struct_vli_usbhub_hdr_parse(buf_fw, buf_fwsz, 0x0, error);
	if (st_hd == NULL)
		return FALSE;
	hd2_fw_sz = (fu_struct_vli_usbhub_hdr_get_usb3_fw_sz_high(st_hd) << 16);
	hd2_fw_sz += fu_struct_vli_usbhub_hdr_get_usb3_fw_sz(st_hd);
	hd2_fw_offset = (fu_struct_vli_usbhub_hdr_get_usb3_fw_addr_high(st_hd) << 16);
	hd2_fw_offset += fu_struct_vli_usbhub_hdr_get_usb3_fw_addr(st_hd);
	g_debug("FW2 @0x%x (length 0x%x, offset 0x%x)", hd2_fw_addr, hd2_fw_sz, hd2_fw_offset);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 72, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 20, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 8, "hd2");

	/* make space */
	if (!fu_vli_device_spi_erase(FU_VLI_DEVICE(self),
				     hd2_fw_addr,
				     hd2_fw_sz,
				     fu_progress_get_child(progress),
				     error))
		return FALSE;
	fu_progress_step_done(progress);

	/* perform the actual write */
	if (!fu_vli_device_spi_write(FU_VLI_DEVICE(self),
				     hd2_fw_addr,
				     buf_fw + hd2_fw_offset,
				     hd2_fw_sz,
				     fu_progress_get_child(progress),
				     error)) {
		g_prefix_error(error, "failed to write payload: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write new HD2 */
	fu_struct_vli_usbhub_hdr_set_usb3_fw_addr(st_hd, hd2_fw_addr & 0xFFFF);
	fu_struct_vli_usbhub_hdr_set_usb3_fw_addr_high(st_hd, hd2_fw_addr >> 16);
	fu_struct_vli_usbhub_hdr_set_prev_ptr(st_hd, VLI_USBHUB_FLASHMAP_IDX_HD1);
	fu_struct_vli_usbhub_hdr_set_next_ptr(st_hd, VLI_USBHUB_FLASHMAP_IDX_INVALID);
	fu_struct_vli_usbhub_hdr_set_checksum(st_hd, fu_vli_usbhub_device_header_crc8(st_hd));
	if (!fu_vli_device_spi_erase_sector(FU_VLI_DEVICE(self),
					    VLI_USBHUB_FLASHMAP_ADDR_HD2,
					    error)) {
		g_prefix_error(error, "failed to erase sectors for HD2: ");
		return FALSE;
	}
	if (!fu_vli_device_spi_write_block(FU_VLI_DEVICE(self),
					   VLI_USBHUB_FLASHMAP_ADDR_HD2,
					   st_hd->data,
					   st_hd->len,
					   fu_progress_get_child(progress),
					   error)) {
		g_prefix_error(error, "failed to write HD2: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	g_byte_array_unref(self->st_hd2);
	self->st_hd2 = g_byte_array_ref(st_hd);
	return TRUE;
}

static GBytes *
fu_vli_usbhub_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuVliUsbhubDevice *self = FU_VLI_USBHUB_DEVICE(device);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);
	return fu_vli_device_spi_read(FU_VLI_DEVICE(self),
				      0x0,
				      fu_device_get_firmware_size_max(device),
				      progress,
				      error);
}

static gboolean
fu_vli_usbhub_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuVliUsbhubDevice *self = FU_VLI_USBHUB_DEVICE(device);

	/* disable powersaving if required */
	if (self->disable_powersave) {
		if (!fu_vli_usbhub_device_disable_u1u2(self, error)) {
			g_prefix_error(error, "disabling powersave failed: ");
			return FALSE;
		}
	}

	/* use correct method */
	if (self->update_protocol == 0x1)
		return fu_vli_usbhub_device_update_v1(self, firmware, progress, error);
	if (self->update_protocol == 0x2)
		return fu_vli_usbhub_device_update_v2(self, firmware, progress, error);
	if (self->update_protocol == 0x3)
		return fu_vli_usbhub_device_update_v3(self, firmware, progress, error);

	/* not sure what to do */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "update protocol 0x%x not supported",
		    self->update_protocol);
	return FALSE;
}

static void
fu_vli_usbhub_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 92, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 7, "reload");
}

static void
fu_vli_usbhub_device_init(FuVliUsbhubDevice *self)
{
	self->st_hd1 = fu_struct_vli_usbhub_hdr_new();
	self->st_hd2 = fu_struct_vli_usbhub_hdr_new();
	fu_device_add_icon(FU_DEVICE(self), "usb-hub");
	fu_device_add_protocol(FU_DEVICE(self), "com.vli.usbhub");
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FALLBACK);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_AUTO_PARENT_CHILDREN);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_VLI_USBHUB_DEVICE_FLAG_ATTACH_WITH_GPIOB);
	fu_device_register_private_flag(FU_DEVICE(self), FU_VLI_USBHUB_DEVICE_FLAG_USB2);
	fu_device_register_private_flag(FU_DEVICE(self), FU_VLI_USBHUB_DEVICE_FLAG_USB3);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_VLI_USBHUB_DEVICE_FLAG_UNLOCK_LEGACY813);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_VLI_USBHUB_DEVICE_FLAG_HAS_SHARED_SPI_PD);
	fu_device_register_private_flag(FU_DEVICE(self), FU_VLI_USBHUB_DEVICE_FLAG_HAS_MSP430);
	fu_device_register_private_flag(FU_DEVICE(self), FU_VLI_USBHUB_DEVICE_FLAG_HAS_RTD21XX);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_VLI_USBHUB_DEVICE_FLAG_ATTACH_WITH_USB_CABLE);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_VLI_USBHUB_DEVICE_FLAG_ATTACH_WITH_POWER_CORD);
}

static void
fu_vli_usbhub_device_finalize(GObject *obj)
{
	FuVliUsbhubDevice *self = FU_VLI_USBHUB_DEVICE(obj);
	g_byte_array_unref(self->st_hd1);
	g_byte_array_unref(self->st_hd2);
	G_OBJECT_CLASS(fu_vli_usbhub_device_parent_class)->finalize(obj);
}

static void
fu_vli_usbhub_device_class_init(FuVliUsbhubDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	FuVliDeviceClass *vli_device_class = FU_VLI_DEVICE_CLASS(klass);
	object_class->finalize = fu_vli_usbhub_device_finalize;
	device_class->probe = fu_vli_usbhub_device_probe;
	device_class->dump_firmware = fu_vli_usbhub_device_dump_firmware;
	device_class->write_firmware = fu_vli_usbhub_device_write_firmware;
	device_class->prepare_firmware = fu_vli_usbhub_device_prepare_firmware;
	device_class->attach = fu_vli_usbhub_device_attach;
	device_class->to_string = fu_vli_usbhub_device_to_string;
	device_class->ready = fu_vli_usbhub_device_ready;
	device_class->set_progress = fu_vli_usbhub_device_set_progress;
	vli_device_class->spi_chip_erase = fu_vli_usbhub_device_spi_chip_erase;
	vli_device_class->spi_sector_erase = fu_vli_usbhub_device_spi_sector_erase;
	vli_device_class->spi_read_data = fu_vli_usbhub_device_spi_read_data;
	vli_device_class->spi_read_status = fu_vli_usbhub_device_spi_read_status;
	vli_device_class->spi_write_data = fu_vli_usbhub_device_spi_write_data;
	vli_device_class->spi_write_enable = fu_vli_usbhub_device_spi_write_enable;
	vli_device_class->spi_write_status = fu_vli_usbhub_device_spi_write_status;
}
