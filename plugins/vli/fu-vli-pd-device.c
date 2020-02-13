/*
 * Copyright (C) 2015-2019 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-firmware.h"

#include "fu-vli-pd-device.h"
#include "fu-vli-pd-firmware.h"
#include "fu-vli-pd-parade-device.h"

struct _FuVliPdDevice
{
	FuVliDevice		 parent_instance;
};

G_DEFINE_TYPE (FuVliPdDevice, fu_vli_pd_device, FU_TYPE_VLI_DEVICE)

static gboolean
fu_vli_pd_device_read_regs (FuVliPdDevice *self, guint16 addr,
			    guint8 *buf, gsize bufsz, GError **error)
{
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xe0,
					    ((addr & 0xff) << 8) | 0x01,
					    addr >> 8,
					    buf, bufsz, NULL,
					    1000, NULL, error)) {
		g_prefix_error (error, "failed to write register @0x%x: ", addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_pd_device_read_reg (FuVliPdDevice *self, guint16 addr, guint8 *value, GError **error)
{
	return fu_vli_pd_device_read_regs (self, addr, value, 0x1, error);
}

static gboolean
fu_vli_pd_device_write_reg (FuVliPdDevice *self, guint16 addr, guint8 value, GError **error)
{
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xe0,
					    ((addr & 0xff) << 8) | 0x02,
					    addr >> 8,
					    &value, sizeof(value), NULL,
					    1000, NULL, error)) {
		g_prefix_error (error, "failed to write register @0x%x: ", addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_pd_device_spi_read_status (FuVliDevice *self, guint8 *status, GError **error)
{
	guint8 spi_cmd = 0x0;
	if (!fu_vli_device_get_spi_cmd (self, FU_VLI_DEVICE_SPI_REQ_READ_STATUS,
					&spi_cmd, error))
		return FALSE;
	return g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					      G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					      G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					      G_USB_DEVICE_RECIPIENT_DEVICE,
					      0xc5, spi_cmd, 0x0000,
					      status, 0x1, NULL,
					      FU_VLI_DEVICE_TIMEOUT,
					      NULL, error);
}

static gboolean
fu_vli_pd_device_spi_read_data (FuVliDevice *self, guint32 addr, guint8 *buf, gsize bufsz, GError **error)
{
	guint8 spi_cmd = 0x0;
	guint16 value;
	guint16 index;
	if (!fu_vli_device_get_spi_cmd (self, FU_VLI_DEVICE_SPI_REQ_READ_DATA,
					&spi_cmd, error))
		return FALSE;
	value = ((addr << 8) & 0xff00) | spi_cmd;
	index = addr >> 8;
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
fu_vli_pd_device_spi_write_status (FuVliDevice *self, guint8 status, GError **error)
{
	guint8 spi_cmd = 0x0;
	guint16 value;
	if (!fu_vli_device_get_spi_cmd (self, FU_VLI_DEVICE_SPI_REQ_WRITE_STATUS,
					&spi_cmd, error))
		return FALSE;
	value = ((guint16) status << 8) | spi_cmd;
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xd8, value, 0x0,
					    NULL, 0, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		return FALSE;
	}

	/* Fix_For_GD_&_EN_SPI_Flash */
	g_usleep (100 * 1000);
	return TRUE;
}

static gboolean
fu_vli_pd_device_spi_write_enable (FuVliDevice *self, GError **error)
{
	guint8 spi_cmd = 0x0;
	if (!fu_vli_device_get_spi_cmd (self, FU_VLI_DEVICE_SPI_REQ_WRITE_EN,
					&spi_cmd, error))
		return FALSE;
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xd4, spi_cmd, 0x0000,
					    NULL, 0x0, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to write enable SPI: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_pd_device_spi_chip_erase (FuVliDevice *self, GError **error)
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
fu_vli_pd_device_spi_sector_erase (FuVliDevice *self, guint32 addr, GError **error)
{
	guint8 spi_cmd = 0x0;
	guint16 value;
	guint16 index;
	if (!fu_vli_device_get_spi_cmd (self, FU_VLI_DEVICE_SPI_REQ_SECTOR_ERASE,
					&spi_cmd, error))
		return FALSE;
	value = ((addr << 8) & 0xff00) | spi_cmd;
	index = addr >> 8;
	return g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					      G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					      G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					      G_USB_DEVICE_RECIPIENT_DEVICE,
					      0xd2, value, index,
					      NULL, 0x0, NULL,
					      FU_VLI_DEVICE_TIMEOUT,
					      NULL, error);
}

static gboolean
fu_vli_pd_device_spi_write_data (FuVliDevice *self,
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
	value = ((addr << 8) & 0xff00) | spi_cmd;
	index = addr >> 8;
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xdc, value, index,
					    (guint8 *) buf, bufsz, NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL, error)) {
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_pd_device_reset (FuVliDevice *device, GError **error)
{
	return g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (device)),
					      G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					      G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					      G_USB_DEVICE_RECIPIENT_DEVICE,
					      0xb0, 0x0000, 0x0000,
					      NULL, 0x0, NULL,
					      FU_VLI_DEVICE_TIMEOUT,
					      NULL, error);
}

static gboolean
fu_vli_pd_device_reset_vl103 (FuVliDevice *device, GError **error)
{
	return g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (device)),
					      G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					      G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					      G_USB_DEVICE_RECIPIENT_DEVICE,
					      0xc0, 0x0000, 0x0000,
					      NULL, 0x0, NULL,
					      FU_VLI_DEVICE_TIMEOUT,
					      NULL, error);
}

static gboolean
fu_vli_pd_device_parade_setup (FuVliPdDevice *self, GError **error)
{
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(GError) error_local = NULL;

	/* add child */
	dev = fu_vli_pd_parade_device_new (FU_VLI_DEVICE (self));
	if (!fu_device_probe (dev, &error_local)) {
		if (g_error_matches (error_local,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND)) {
			g_debug ("%s", error_local->message);
		} else {
			g_warning ("cannot create I²C parade device: %s",
				   error_local->message);
		}
		return TRUE;
	}
	if (!fu_device_setup (dev, error)) {
		g_prefix_error (error, "failed to set up parade device: ");
		return FALSE;
	}
	fu_device_add_child (FU_DEVICE (self), dev);
	return TRUE;
}

static gboolean
fu_vli_pd_device_setup (FuVliDevice *device, GError **error)
{
	FuVliPdDevice *self = FU_VLI_PD_DEVICE (device);
	guint32 version_raw;
	guint8 verbuf[4] = { 0x0 };
	guint8 tmp = 0;
	g_autofree gchar *version_str = NULL;
	FuVliDeviceClass *klass = FU_VLI_DEVICE_GET_CLASS (device);

	/* get version */
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xe2, 0x0001, 0x0000,
					    verbuf, sizeof(verbuf), NULL,
					    1000, NULL, error)) {
		g_prefix_error (error, "failed to get version: ");
		return FALSE;
	}
	version_raw = fu_common_read_uint32 (verbuf, G_BIG_ENDIAN);
	fu_device_set_version_raw (FU_DEVICE (self), version_raw);
	version_str = fu_common_version_from_uint32 (version_raw, FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_version (FU_DEVICE (self), version_str, FWUPD_VERSION_FORMAT_QUAD);

	/* get device kind if not already in ROM mode */
	if (fu_vli_device_get_kind (device) == FU_VLI_DEVICE_KIND_UNKNOWN) {
		if (!fu_vli_pd_device_read_reg (self, 0x0018, &tmp, error))
			return FALSE;
		switch (tmp) {
		case 0x00:
			fu_vli_device_set_kind (device, FU_VLI_DEVICE_KIND_VL100);
			break;
		case 0x10:
			/* this is also the code for VL101, but VL102 is more likely */
			fu_vli_device_set_kind (device, FU_VLI_DEVICE_KIND_VL102);
			break;
		case 0x80:
			fu_vli_device_set_kind (device, FU_VLI_DEVICE_KIND_VL103);
			break;
		case 0x90:
			fu_vli_device_set_kind (device, FU_VLI_DEVICE_KIND_VL104);
			break;
		default:
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "unable to map 0x0018=%0x02x to device kind",
				     tmp);
			return FALSE;
		}
	}

	/* handle this in a different way */
	if (fu_vli_device_get_kind (device) == FU_VLI_DEVICE_KIND_VL103)
		klass->reset = fu_vli_pd_device_reset_vl103;

	/* get bootloader mode */
	if (!fu_vli_pd_device_read_reg (self, 0x00F7, &tmp, error))
		return FALSE;
	if ((tmp & 0x80) == 0x00)
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	else
		fu_device_remove_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);

	/* detect any I²C child, e.g. parade device */
	if (fu_device_has_custom_flag (FU_DEVICE (self), "has-i2c-ps186")) {
		if (!fu_vli_pd_device_parade_setup (self, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static FuFirmware *
fu_vli_pd_device_prepare_firmware (FuDevice *device,
				   GBytes *fw,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuVliPdDevice *self = FU_VLI_PD_DEVICE (device);
	FuVliDeviceKind device_kind;
	g_autoptr(FuFirmware) firmware = fu_vli_pd_firmware_new ();

	/* check size */
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
	device_kind = fu_vli_pd_firmware_get_kind (FU_VLI_PD_FIRMWARE (firmware));
	if (fu_vli_device_get_kind (FU_VLI_DEVICE (self)) != device_kind) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "firmware incompatible, got %s, expected %s",
			     fu_vli_common_device_kind_to_string (device_kind),
			     fu_vli_common_device_kind_to_string (fu_vli_device_get_kind (FU_VLI_DEVICE (self))));
		return NULL;
	}

	/* we could check this against flags */
	g_debug ("parsed version: %s", fu_firmware_get_version (firmware));
	return g_steal_pointer (&firmware);
}

static FuFirmware *
fu_vli_pd_device_read_firmware (FuDevice *device, GError **error)
{
	FuVliPdDevice *self = FU_VLI_PD_DEVICE (device);
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
fu_vli_pd_device_write_gpios (FuVliPdDevice *self, GError **error)
{
	/* write GPIO 4&5, 7 then 3 */
	if (!fu_vli_pd_device_write_reg (self, 0x0015, 0x7F, error))
		return FALSE;
	if (!fu_vli_pd_device_write_reg (self, 0x0019, 0x00, error))
		return FALSE;
	if (!fu_vli_pd_device_write_reg (self, 0x001C, 0x02, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_vli_pd_device_write_firmware (FuDevice *device,
				 FuFirmware *firmware,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuVliPdDevice *self = FU_VLI_PD_DEVICE (device);
	gsize bufsz = 0;
	guint8 tmp = 0;
	const guint8 *buf = NULL;
	g_autoptr(GBytes) fw = NULL;

	/* binary blob */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* write GPIOs in new mode */
	if (!fu_vli_pd_device_write_gpios (self, error))
		return FALSE;

	/* disable write protect in GPIO_3 */
	if (!fu_vli_pd_device_read_reg (self, 0x0003, &tmp, error))
		return FALSE;
	if (!fu_vli_pd_device_write_reg (self, 0x0003, tmp | 0x44, error))
		return FALSE;

	/* erase */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_ERASE);
	if (!fu_vli_device_spi_erase_all (FU_VLI_DEVICE (self), error))
		return FALSE;

	/* write in chunks */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_WRITE);
	buf = g_bytes_get_data (fw, &bufsz);
	if (!fu_vli_device_spi_write (FU_VLI_DEVICE (self),
				      fu_vli_device_get_offset (FU_VLI_DEVICE (self)),
				      buf, bufsz, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_vli_pd_device_detach (FuDevice *device, GError **error)
{
	FuVliPdDevice *self = FU_VLI_PD_DEVICE (device);
	guint8 tmp = 0;
	g_autoptr(GError) error_local = NULL;

	/* write GPIOs */
	if (!fu_vli_pd_device_write_gpios (self, error))
		return FALSE;

	/* patch APP5 FW bug (2AF2 -> 2AE2) */
	if (!fu_vli_pd_device_read_reg (self, 0x0018, &tmp, error))
		return FALSE;
	if (tmp != 0x80) {
		if (!fu_vli_pd_device_write_reg (self, 0x2AE2, 0x1E, error))
			return FALSE;
		if (!fu_vli_pd_device_write_reg (self, 0x2AE3, 0xC3, error))
			return FALSE;
		if (!fu_vli_pd_device_write_reg (self, 0x2AE4, 0x5A, error))
			return FALSE;
		if (!fu_vli_pd_device_write_reg (self, 0x2AE5, 0x87, error))
			return FALSE;
		/* set ROM sig */
		if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (device)),
						    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
						    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
						    G_USB_DEVICE_RECIPIENT_DEVICE,
						    0xa0, 0x0000, 0x0000,
						    NULL, 0x0, NULL,
						    FU_VLI_DEVICE_TIMEOUT,
						    NULL, error))
			return FALSE;
	}

	/* reset from SPI_Code into ROM_Code */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	if (!fu_vli_device_reset (FU_VLI_DEVICE (device), &error_local)) {
		if (g_error_matches (error_local,
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
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static void
fu_vli_pd_device_init (FuVliPdDevice *self)
{
	fu_device_add_icon (FU_DEVICE (self), "audio-card");
	fu_device_set_protocol (FU_DEVICE (self), "com.vli.pd");
	fu_device_set_summary (FU_DEVICE (self), "USB PD");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_set_remove_delay (FU_DEVICE (self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_vli_device_set_spi_auto_detect (FU_VLI_DEVICE (self), FALSE);
}

static void
fu_vli_pd_device_class_init (FuVliPdDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuVliDeviceClass *klass_vli_device = FU_VLI_DEVICE_CLASS (klass);
	klass_device->read_firmware = fu_vli_pd_device_read_firmware;
	klass_device->write_firmware = fu_vli_pd_device_write_firmware;
	klass_device->prepare_firmware = fu_vli_pd_device_prepare_firmware;
	klass_device->detach = fu_vli_pd_device_detach;
	klass_vli_device->setup = fu_vli_pd_device_setup;
	klass_vli_device->reset = fu_vli_pd_device_reset;
	klass_vli_device->spi_chip_erase = fu_vli_pd_device_spi_chip_erase;
	klass_vli_device->spi_sector_erase = fu_vli_pd_device_spi_sector_erase;
	klass_vli_device->spi_read_data = fu_vli_pd_device_spi_read_data;
	klass_vli_device->spi_read_status = fu_vli_pd_device_spi_read_status;
	klass_vli_device->spi_write_data = fu_vli_pd_device_spi_write_data;
	klass_vli_device->spi_write_enable = fu_vli_pd_device_spi_write_enable;
	klass_vli_device->spi_write_status = fu_vli_pd_device_spi_write_status;
}
