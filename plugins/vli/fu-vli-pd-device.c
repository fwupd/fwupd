/*
 * Copyright 2015 VIA Corporation
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-vli-pd-device.h"
#include "fu-vli-pd-firmware.h"
#include "fu-vli-pd-parade-device.h"
#include "fu-vli-struct.h"

struct _FuVliPdDevice {
	FuVliDevice parent_instance;
};

#define FU_VLI_PD_DEVICE_FLAG_HAS_I2C_PS186 "has-i2c-ps186"
#define FU_VLI_PD_DEVICE_FLAG_SKIPS_ROM	    "skips-rom"

G_DEFINE_TYPE(FuVliPdDevice, fu_vli_pd_device, FU_TYPE_VLI_DEVICE)

static gboolean
fu_vli_pd_device_read_regs(FuVliPdDevice *self,
			   guint16 addr,
			   guint8 *buf,
			   gsize bufsz,
			   GError **error)
{
	g_autofree gchar *title = g_strdup_printf("ReadRegs@0x%x", addr);
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_DEVICE_TO_HOST,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    0xe0,
					    ((addr & 0xff) << 8) | 0x01,
					    addr >> 8,
					    buf,
					    bufsz,
					    NULL,
					    1000,
					    NULL,
					    error)) {
		g_prefix_error(error, "failed to write register @0x%x: ", addr);
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, title, buf, bufsz);
	return TRUE;
}

static gboolean
fu_vli_pd_device_read_reg(FuVliPdDevice *self, guint16 addr, guint8 *value, GError **error)
{
	return fu_vli_pd_device_read_regs(self, addr, value, 0x1, error);
}

static gboolean
fu_vli_pd_device_write_reg(FuVliPdDevice *self, guint16 addr, guint8 value, GError **error)
{
	g_autofree gchar *title = g_strdup_printf("WriteReg@0x%x", addr);
	fu_dump_raw(G_LOG_DOMAIN, title, &value, sizeof(value));
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    0xe0,
					    ((addr & 0xff) << 8) | 0x02,
					    addr >> 8,
					    &value,
					    sizeof(value),
					    NULL,
					    1000,
					    NULL,
					    error)) {
		g_prefix_error(error, "failed to write register @0x%x: ", addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vli_pd_device_spi_read_status(FuVliDevice *self, guint8 *status, GError **error)
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
					      0xc5,
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
fu_vli_pd_device_spi_read_data(FuVliDevice *self,
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
	value = ((addr << 8) & 0xff00) | spi_cmd;
	index = addr >> 8;
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
fu_vli_pd_device_spi_write_status(FuVliDevice *self, guint8 status, GError **error)
{
	guint8 spi_cmd = 0x0;
	guint16 value;
	if (!fu_cfi_device_get_cmd(fu_vli_device_get_cfi_device(self),
				   FU_CFI_DEVICE_CMD_WRITE_STATUS,
				   &spi_cmd,
				   error))
		return FALSE;
	value = ((guint16)status << 8) | spi_cmd;
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    0xd8,
					    value,
					    0x0,
					    NULL,
					    0,
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
fu_vli_pd_device_spi_write_enable(FuVliDevice *self, GError **error)
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
					    0xd4,
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
fu_vli_pd_device_spi_chip_erase(FuVliDevice *self, GError **error)
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
fu_vli_pd_device_spi_sector_erase(FuVliDevice *self, guint32 addr, GError **error)
{
	guint8 spi_cmd = 0x0;
	guint16 value;
	guint16 index;
	if (!fu_cfi_device_get_cmd(fu_vli_device_get_cfi_device(self),
				   FU_CFI_DEVICE_CMD_SECTOR_ERASE,
				   &spi_cmd,
				   error))
		return FALSE;
	value = ((addr << 8) & 0xff00) | spi_cmd;
	index = addr >> 8;
	return fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					      FU_USB_DIRECTION_HOST_TO_DEVICE,
					      FU_USB_REQUEST_TYPE_VENDOR,
					      FU_USB_RECIPIENT_DEVICE,
					      0xd2,
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
fu_vli_pd_device_spi_write_data(FuVliDevice *self,
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
	value = ((addr << 8) & 0xff00) | spi_cmd;
	index = addr >> 8;

	buf_mut = fu_memdup_safe(buf, bufsz, error);
	if (buf_mut == NULL)
		return FALSE;
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    0xdc,
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
	return TRUE;
}

static gboolean
fu_vli_pd_device_parade_setup(FuVliPdDevice *self, GError **error)
{
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(GError) error_local = NULL;

	/* add child */
	dev = fu_vli_pd_parade_device_new(FU_VLI_DEVICE(self));
	if (!fu_device_probe(dev, &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_debug("%s", error_local->message);
		} else {
			g_warning("cannot create I²C parade device: %s", error_local->message);
		}
		return TRUE;
	}
	if (!fu_device_setup(dev, error)) {
		g_prefix_error(error, "failed to set up parade device: ");
		return FALSE;
	}
	fu_device_add_child(FU_DEVICE(self), dev);
	return TRUE;
}

static gboolean
fu_vli_pd_device_setup(FuDevice *device, GError **error)
{
	FuVliPdDevice *self = FU_VLI_PD_DEVICE(device);
	guint32 version_raw;
	guint8 verbuf[4] = {0x0};
	guint8 value = 0;
	guint8 cmp = 0;

	/* FuVliDevice->setup */
	if (!FU_DEVICE_CLASS(fu_vli_pd_device_parent_class)->setup(device, error))
		return FALSE;

	/* get version */
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_DEVICE_TO_HOST,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    0xe2,
					    0x0001,
					    0x0000,
					    verbuf,
					    sizeof(verbuf),
					    NULL,
					    1000,
					    NULL,
					    error)) {
		g_prefix_error(error, "failed to get version: ");
		return FALSE;
	}
	if (!fu_memread_uint32_safe(verbuf, sizeof(verbuf), 0x0, &version_raw, G_BIG_ENDIAN, error))
		return FALSE;
	fu_device_set_version_raw(FU_DEVICE(self), version_raw);

	/* get device kind if not already in ROM mode */
	if (fu_vli_device_get_kind(FU_VLI_DEVICE(self)) == FU_VLI_DEVICE_KIND_UNKNOWN) {
		if (!fu_vli_pd_device_read_reg(self,
					       FU_VLI_PD_REGISTER_ADDRESS_PROJ_LEGACY,
					       &value,
					       error))
			return FALSE;
		if (value != 0xFF) {
			switch (value & 0xF0) {
			case 0x00:
				fu_vli_device_set_kind(FU_VLI_DEVICE(self),
						       FU_VLI_DEVICE_KIND_VL100);
				break;
			case 0x10:
				/* this is also the code for VL101, but VL102 is more likely */
				fu_vli_device_set_kind(FU_VLI_DEVICE(self),
						       FU_VLI_DEVICE_KIND_VL102);
				break;
			case 0x80:
				fu_vli_device_set_kind(FU_VLI_DEVICE(self),
						       FU_VLI_DEVICE_KIND_VL103);
				break;
			case 0x90:
				fu_vli_device_set_kind(FU_VLI_DEVICE(self),
						       FU_VLI_DEVICE_KIND_VL104);
				break;
			case 0xA0:
				fu_vli_device_set_kind(FU_VLI_DEVICE(self),
						       FU_VLI_DEVICE_KIND_VL105);
				break;
			case 0xB0:
				fu_vli_device_set_kind(FU_VLI_DEVICE(self),
						       FU_VLI_DEVICE_KIND_VL106);
				break;
			case 0xC0:
				fu_vli_device_set_kind(FU_VLI_DEVICE(self),
						       FU_VLI_DEVICE_KIND_VL107);
				break;
			default:
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "unable to map 0x0018=0x%02X to device kind",
					    value);
				return FALSE;
			}
		} else {
			if (!fu_vli_pd_device_read_reg(self,
						       FU_VLI_PD_REGISTER_ADDRESS_PROJ_ID_HIGH,
						       &value,
						       error))
				return FALSE;
			if (!fu_vli_pd_device_read_reg(self,
						       FU_VLI_PD_REGISTER_ADDRESS_PROJ_ID_LOW,
						       &cmp,
						       error))
				return FALSE;
			if ((value == 0x35) && (cmp == 0x96))
				fu_vli_device_set_kind(FU_VLI_DEVICE(self),
						       FU_VLI_DEVICE_KIND_VL108);
			else if ((value == 0x36) && (cmp == 0x01))
				fu_vli_device_set_kind(FU_VLI_DEVICE(self),
						       FU_VLI_DEVICE_KIND_VL109);
			else {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "unable to map 0x9C9D=0x%02X%02X to device kind",
					    value,
					    cmp);
				return FALSE;
			}
		}
	}

	/* get bootloader mode */
	if (!fu_vli_pd_device_read_reg(self, 0x00F7, &value, error))
		return FALSE;
	if ((value & 0x80) == 0x00)
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	else
		fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);

	/* detect any I²C child, e.g. parade device */
	if (fu_device_has_private_flag(device, FU_VLI_PD_DEVICE_FLAG_HAS_I2C_PS186)) {
		if (!fu_vli_pd_device_parade_setup(self, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static FuFirmware *
fu_vli_pd_device_prepare_firmware(FuDevice *device,
				  GInputStream *stream,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuVliPdDevice *self = FU_VLI_PD_DEVICE(device);
	FuVliDeviceKind device_kind;
	g_autoptr(FuFirmware) firmware = fu_vli_pd_firmware_new();

	/* check size */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	if (fu_firmware_get_size(firmware) > fu_device_get_firmware_size_max(device)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware too large, got 0x%x, expected <= 0x%x",
			    (guint)fu_firmware_get_size(firmware),
			    (guint)fu_device_get_firmware_size_max(device));
		return NULL;
	}

	/* check is compatible with firmware */
	device_kind = fu_vli_pd_firmware_get_kind(FU_VLI_PD_FIRMWARE(firmware));
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

	/* we could check this against flags */
	g_debug("parsed version: %s", fu_firmware_get_version(firmware));
	return g_steal_pointer(&firmware);
}

static GBytes *
fu_vli_pd_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuVliPdDevice *self = FU_VLI_PD_DEVICE(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* require detach -> attach */
	locker = fu_device_locker_new_full(device,
					   (FuDeviceLockerFunc)fu_device_detach,
					   (FuDeviceLockerFunc)fu_device_attach,
					   error);
	if (locker == NULL)
		return NULL;
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);
	return fu_vli_device_spi_read(FU_VLI_DEVICE(self),
				      0x0,
				      fu_device_get_firmware_size_max(device),
				      progress,
				      error);
}

static gboolean
fu_vli_pd_device_write_gpios(FuVliPdDevice *self, GError **error)
{
	/* disable UART-Rx mode */
	if (!fu_vli_pd_device_write_reg(self, 0x0015, 0x7F, error))
		return FALSE;
	/* disable 'Watch Mode', chip is not in debug mode */
	if (!fu_vli_pd_device_write_reg(self, 0x0019, 0x00, error))
		return FALSE;
	/* GPIO3 output enable, switch/CMOS/Boost control pin */
	if (!fu_vli_pd_device_write_reg(self, 0x001C, 0x02, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_vli_pd_device_write_dual_firmware(FuVliPdDevice *self,
				     GBytes *fw,
				     FuProgress *progress,
				     GError **error)
{
	const guint8 *buf = NULL;
	const guint8 *sbuf = NULL;
	gsize bufsz = 0;
	gsize sbufsz = 0;
	guint16 crc_actual;
	guint16 crc_file = 0x0;
	guint32 sec_addr = 0x28000;
	g_autoptr(GBytes) spi_fw = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 10, "crc");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 45, "backup");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 45, "primary");

	/* check spi fw1 crc16 */
	spi_fw = fu_vli_device_spi_read(FU_VLI_DEVICE(self),
					fu_vli_device_get_offset(FU_VLI_DEVICE(self)),
					fu_device_get_firmware_size_max(FU_DEVICE(self)),
					fu_progress_get_child(progress),
					error);
	if (spi_fw == NULL)
		return FALSE;
	sbuf = g_bytes_get_data(spi_fw, &sbufsz);
	if (sbufsz != 0x8000)
		sec_addr = 0x30000;
	if (sbufsz < 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "buffer was too small");
		return FALSE;
	}
	if (!fu_memread_uint16_safe(sbuf, sbufsz, sbufsz - 2, &crc_file, G_LITTLE_ENDIAN, error)) {
		g_prefix_error(error, "failed to read file CRC: ");
		return FALSE;
	}
	crc_actual = fu_crc16(FU_CRC_KIND_B16_USB, sbuf, sbufsz - 2);
	fu_progress_step_done(progress);

	/* update fw2 first if fw1 correct */
	buf = g_bytes_get_data(fw, &bufsz);
	if (crc_actual == crc_file) {
		if (!fu_vli_device_spi_write(FU_VLI_DEVICE(self),
					     sec_addr,
					     buf,
					     bufsz,
					     fu_progress_get_child(progress),
					     error))
			return FALSE;
		fu_progress_step_done(progress);
		if (!fu_vli_device_spi_write(FU_VLI_DEVICE(self),
					     fu_vli_device_get_offset(FU_VLI_DEVICE(self)),
					     buf,
					     bufsz,
					     fu_progress_get_child(progress),
					     error))
			return FALSE;
		fu_progress_step_done(progress);

		/* else update fw1 first */
	} else {
		if (!fu_vli_device_spi_write(FU_VLI_DEVICE(self),
					     fu_vli_device_get_offset(FU_VLI_DEVICE(self)),
					     buf,
					     bufsz,
					     fu_progress_get_child(progress),
					     error))
			return FALSE;
		fu_progress_step_done(progress);
		if (!fu_vli_device_spi_write(FU_VLI_DEVICE(self),
					     sec_addr,
					     buf,
					     bufsz,
					     fu_progress_get_child(progress),
					     error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_vli_pd_device_write_firmware(FuDevice *device,
				FuFirmware *firmware,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	FuVliPdDevice *self = FU_VLI_PD_DEVICE(device);
	gsize bufsz = 0;
	guint8 tmp = 0;
	const guint8 *buf = NULL;
	g_autoptr(GBytes) fw = NULL;

	/* binary blob */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* write GPIOs in new mode */
	if (!fu_vli_pd_device_write_gpios(self, error))
		return FALSE;

	/* disable write protect in GPIO_3 */
	if (!fu_vli_pd_device_read_reg(self,
				       FU_VLI_PD_REGISTER_ADDRESS_GPIO_CONTROL_A,
				       &tmp,
				       error))
		return FALSE;
	if (!fu_vli_pd_device_write_reg(self,
					FU_VLI_PD_REGISTER_ADDRESS_GPIO_CONTROL_A,
					tmp | 0x44,
					error))
		return FALSE;

	/* dual image on VL103 */
	if (fu_vli_device_get_kind(FU_VLI_DEVICE(device)) == FU_VLI_DEVICE_KIND_VL103 &&
	    fu_device_has_flag(device, FWUPD_DEVICE_FLAG_DUAL_IMAGE))
		return fu_vli_pd_device_write_dual_firmware(self, fw, progress, error);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 63, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 37, NULL);

	/* erase */
	if (!fu_vli_device_spi_erase_all(FU_VLI_DEVICE(self),
					 fu_progress_get_child(progress),
					 error)) {
		g_prefix_error(error, "failed to erase all: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write in chunks */
	buf = g_bytes_get_data(fw, &bufsz);
	if (!fu_vli_device_spi_write(FU_VLI_DEVICE(self),
				     fu_vli_device_get_offset(FU_VLI_DEVICE(self)),
				     buf,
				     bufsz,
				     fu_progress_get_child(progress),
				     error))
		return FALSE;

	/* success */
	fu_progress_step_done(progress);
	return TRUE;
}

static gboolean
fu_vli_pd_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuVliPdDevice *self = FU_VLI_PD_DEVICE(device);
	g_autoptr(GError) error_local = NULL;
	guint8 gpio_control_a = 0;

	/* sanity check */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in bootloader mode, skipping");
		return TRUE;
	}

	/* VL103 only updates in ROM mode, check other devices for skips-rom flag */
	if (fu_vli_device_get_kind(FU_VLI_DEVICE(device)) != FU_VLI_DEVICE_KIND_VL103 &&
	    fu_device_has_private_flag(device, FU_VLI_PD_DEVICE_FLAG_SKIPS_ROM)) {
		return TRUE;
	}

	/* write GPIOs */
	if (!fu_vli_pd_device_write_gpios(self, error))
		return FALSE;

	/* disable charging */
	if (!fu_vli_pd_device_read_reg(self,
				       FU_VLI_PD_REGISTER_ADDRESS_GPIO_CONTROL_A,
				       &gpio_control_a,
				       error))
		return FALSE;
	gpio_control_a = (gpio_control_a | 0x10) & 0xFE;
	if (!fu_vli_pd_device_write_reg(self,
					FU_VLI_PD_REGISTER_ADDRESS_GPIO_CONTROL_A,
					gpio_control_a,
					error))
		return FALSE;

	/* VL103 set ROM sig does not work, so use alternate function */
	if ((fu_vli_device_get_kind(FU_VLI_DEVICE(device)) != FU_VLI_DEVICE_KIND_VL100) &&
	    (fu_vli_device_get_kind(FU_VLI_DEVICE(device)) != FU_VLI_DEVICE_KIND_VL102)) {
		if (!fu_usb_device_control_transfer(FU_USB_DEVICE(FU_USB_DEVICE(device)),
						    FU_USB_DIRECTION_HOST_TO_DEVICE,
						    FU_USB_REQUEST_TYPE_VENDOR,
						    FU_USB_RECIPIENT_DEVICE,
						    0xc0,
						    0x0000,
						    0x0000,
						    NULL,
						    0x0,
						    NULL,
						    FU_VLI_DEVICE_TIMEOUT,
						    NULL,
						    &error_local)) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_INTERNAL)) {
				g_debug("ignoring %s", error_local->message);
			} else {
				g_propagate_prefixed_error(error,
							   g_steal_pointer(&error_local),
							   "failed to restart device: ");
				return FALSE;
			}
		}
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
		return TRUE;
	}

	/* patch APP5 FW bug (2AF2 -> 2AE2) on VL100-App5 and VL102 */
	if (fu_vli_device_get_kind(FU_VLI_DEVICE(device)) == FU_VLI_DEVICE_KIND_VL100 ||
	    fu_vli_device_get_kind(FU_VLI_DEVICE(device)) == FU_VLI_DEVICE_KIND_VL102) {
		guint8 proj_legacy = 0;
		if (!fu_vli_pd_device_read_reg(self,
					       FU_VLI_PD_REGISTER_ADDRESS_PROJ_LEGACY,
					       &proj_legacy,
					       error))
			return FALSE;
		if (proj_legacy != 0x80) {
			if (!fu_vli_pd_device_write_reg(self, 0x2AE2, 0x1E, error))
				return FALSE;
			if (!fu_vli_pd_device_write_reg(self, 0x2AE3, 0xC3, error))
				return FALSE;
			if (!fu_vli_pd_device_write_reg(self, 0x2AE4, 0x5A, error))
				return FALSE;
			if (!fu_vli_pd_device_write_reg(self, 0x2AE5, 0x87, error))
				return FALSE;
		}
	}

	/* set ROM sig */
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(FU_USB_DEVICE(device)),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    0xa0,
					    0x0000,
					    0x0000,
					    NULL,
					    0x0,
					    NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL,
					    error))
		return FALSE;

	/* reset from SPI_Code into ROM_Code */
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(FU_USB_DEVICE(device)),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    0xb0,
					    0x0000,
					    0x0000,
					    NULL,
					    0x0,
					    NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL,
					    &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_INTERNAL) ||
		    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			g_debug("ignoring %s", error_local->message);
		} else {
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "failed to restart device: ");
			return FALSE;
		}
	}
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_vli_pd_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuVliPdDevice *self = FU_VLI_PD_DEVICE(device);
	g_autoptr(GError) error_local = NULL;

	/* Work around a silicon bug: Once the CC-resistor is removed, the
	 * CC-host thinks the device is un-plugged and turn off VBUS (power).
	 * When VL103 is powered-off, VL103 puts a resistor at CC-pin.
	 * The CC-host will think the device is re-plugged and provides VBUS
	 * again. Then, VL103 will be powered on and runs new FW. */
	if (fu_vli_device_get_kind(FU_VLI_DEVICE(device)) == FU_VLI_DEVICE_KIND_VL103) {
		if (!fu_vli_pd_device_write_reg(self, 0x1201, 0xf6, error))
			return FALSE;
		if (!fu_vli_pd_device_write_reg(self, 0x1001, 0xf6, error))
			return FALSE;
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
		return TRUE;
	}

	/* sanity check */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in runtime mode, skipping");
		return TRUE;
	}

	/* chip reset command works only for non-VL103 */
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(FU_USB_DEVICE(device)),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    0xb0,
					    0x0000,
					    0x0000,
					    NULL,
					    0x0,
					    NULL,
					    FU_VLI_DEVICE_TIMEOUT,
					    NULL,
					    &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND) ||
		    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED) ||
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

	/* replug */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static void
fu_vli_pd_device_kind_changed_cb(FuVliDevice *device, GParamSpec *pspec, gpointer user_data)
{
	if (fu_vli_device_get_kind(device) == FU_VLI_DEVICE_KIND_VL103) {
		/* wait for USB-C timeout */
		fu_device_set_remove_delay(FU_DEVICE(device), 10000);
	}
}

static void
fu_vli_pd_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 72, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 28, "reload");
}

static gchar *
fu_vli_pd_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static void
fu_vli_pd_device_init(FuVliPdDevice *self)
{
	fu_device_add_icon(FU_DEVICE(self), "usb-hub");
	fu_device_add_protocol(FU_DEVICE(self), "com.vli.pd");
	fu_device_set_summary(FU_DEVICE(self), "USB power distribution device");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_vli_device_set_spi_auto_detect(FU_VLI_DEVICE(self), FALSE);
	fu_device_register_private_flag(FU_DEVICE(self), FU_VLI_PD_DEVICE_FLAG_HAS_I2C_PS186);
	fu_device_register_private_flag(FU_DEVICE(self), FU_VLI_PD_DEVICE_FLAG_SKIPS_ROM);

	/* connect up attach or detach vfuncs when kind is known */
	g_signal_connect(FU_VLI_DEVICE(self),
			 "notify::kind",
			 G_CALLBACK(fu_vli_pd_device_kind_changed_cb),
			 NULL);
}

static void
fu_vli_pd_device_class_init(FuVliPdDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	FuVliDeviceClass *vli_device_class = FU_VLI_DEVICE_CLASS(klass);
	device_class->dump_firmware = fu_vli_pd_device_dump_firmware;
	device_class->write_firmware = fu_vli_pd_device_write_firmware;
	device_class->prepare_firmware = fu_vli_pd_device_prepare_firmware;
	device_class->attach = fu_vli_pd_device_attach;
	device_class->detach = fu_vli_pd_device_detach;
	device_class->setup = fu_vli_pd_device_setup;
	device_class->set_progress = fu_vli_pd_device_set_progress;
	device_class->convert_version = fu_vli_pd_device_convert_version;
	vli_device_class->spi_chip_erase = fu_vli_pd_device_spi_chip_erase;
	vli_device_class->spi_sector_erase = fu_vli_pd_device_spi_sector_erase;
	vli_device_class->spi_read_data = fu_vli_pd_device_spi_read_data;
	vli_device_class->spi_read_status = fu_vli_pd_device_spi_read_status;
	vli_device_class->spi_write_data = fu_vli_pd_device_spi_write_data;
	vli_device_class->spi_write_enable = fu_vli_pd_device_spi_write_enable;
	vli_device_class->spi_write_status = fu_vli_pd_device_spi_write_status;
}
