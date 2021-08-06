/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>
#include <string.h>

#include "fu-rts54hub-device.h"

struct _FuRts54HubDevice {
	FuUsbDevice			 parent_instance;
	gboolean			 fw_auth;
	gboolean			 dual_bank;
	gboolean			 running_on_flash;
	guint8				 vendor_cmd;
};

G_DEFINE_TYPE (FuRts54HubDevice, fu_rts54hub_device, FU_TYPE_USB_DEVICE)

#define FU_RTS54HUB_DEVICE_TIMEOUT			1000	/* ms */
#define FU_RTS54HUB_DEVICE_TIMEOUT_RW			1000	/* ms */
#define FU_RTS54HUB_DEVICE_TIMEOUT_ERASE		5000	/* ms */
#define FU_RTS54HUB_DEVICE_TIMEOUT_AUTH			10000	/* ms */
#define FU_RTS54HUB_DEVICE_BLOCK_SIZE			4096
#define FU_RTS54HUB_DEVICE_STATUS_LEN			25

#define FU_RTS54HUB_I2C_CONFIG_REQUEST 			0xF6
#define FU_RTS54HUB_I2C_WRITE_REQUEST 			0xC6
#define FU_RTS54HUB_I2C_READ_REQUEST 			0xD6

typedef enum {
	FU_RTS54HUB_VENDOR_CMD_NONE			= 0x00,
	FU_RTS54HUB_VENDOR_CMD_STATUS			= 1 << 0,
	FU_RTS54HUB_VENDOR_CMD_FLASH			= 1 << 1,
} FuRts54HubVendorCmd;

static void
fu_rts54hub_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuRts54HubDevice *self = FU_RTS54HUB_DEVICE (device);
	fu_common_string_append_kb (str, idt, "FwAuth", self->fw_auth);
	fu_common_string_append_kb (str, idt, "DualBank", self->dual_bank);
	fu_common_string_append_kb (str, idt, "RunningOnFlash", self->running_on_flash);
}

gboolean
fu_rts54hub_device_i2c_config (FuRts54HubDevice *self,
			       guint8 target_addr,
			       guint8 sub_length,
			       FuRts54HubI2cSpeed speed,
			       GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	guint16 value = 0;
	guint16 index = 0x8080;

	value = ((guint16)target_addr << 8) | sub_length;
	index += speed;
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    FU_RTS54HUB_I2C_CONFIG_REQUEST,
					    value,		/* value */
					    index,		/* idx */
					    NULL, 0,		/* data */
					    NULL,		/* actual */
					    FU_RTS54HUB_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to issue i2c Conf cmd 0x%02x: ", target_addr);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_rts54hub_device_i2c_write (FuRts54HubDevice *self,
			      guint32 sub_addr, const guint8 *data, gsize datasz,
			      GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	g_autofree guint8 *datarw = fu_memdup_safe (data, datasz, error);
	if (datarw == NULL)
		return FALSE;
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    FU_RTS54HUB_I2C_WRITE_REQUEST,
					    sub_addr,			/* value */
					    0x0000,			/* idx */
					    datarw, datasz,		/* data */
					    NULL,
					    FU_RTS54HUB_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to write I2C");
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_rts54hub_device_i2c_read (FuRts54HubDevice *self,
			     guint32 sub_addr, guint8 *data,
			     gsize datasz, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    FU_RTS54HUB_I2C_READ_REQUEST, 0x0000,
					    sub_addr,
					    data, datasz, NULL,
					    FU_RTS54HUB_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to read I2C: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_rts54hub_device_highclockmode (FuRts54HubDevice *self, guint16 value, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0x06,		/* request */
					    value,		/* value */
					    0,			/* idx */
					    NULL, 0,		/* data */
					    NULL,		/* actual */
					    FU_RTS54HUB_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to set highclockmode: ");
		return FALSE;
	}
	return TRUE;
}


static gboolean
fu_rts54hub_device_reset_flash (FuRts54HubDevice *self, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xC0 + 0x29,	/* request */
					    0x0,		/* value */
					    0x0,		/* idx */
					    NULL, 0,		/* data */
					    NULL,		/* actual */
					    FU_RTS54HUB_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to reset flash: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_rts54hub_device_write_flash (FuRts54HubDevice *self,
				guint32 addr,
				const guint8 *data,
				gsize datasz,
				GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	gsize actual_len = 0;
	g_autofree guint8 *datarw = NULL;

	/* make mutable */
	datarw = fu_memdup_safe (data, datasz, error);
	if (datarw == NULL)
		return FALSE;

	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xC0 + 0x08,	/* request */
					    addr % (1 << 16),	/* value */
					    addr / (1 << 16),	/* idx */
					    datarw, datasz,	/* data */
					    &actual_len,
					    FU_RTS54HUB_DEVICE_TIMEOUT_RW,
					    NULL, error)) {
		g_prefix_error (error, "failed to write flash: ");
		return FALSE;
	}
	if (actual_len != datasz) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			     "only wrote %" G_GSIZE_FORMAT "bytes", actual_len);
		return FALSE;
	}
	return TRUE;
}

#if 0
static gboolean
fu_rts54hub_device_read_flash (FuRts54HubDevice *self,
			       guint32 addr,
			       guint8 *data,
			       gsize datasz,
			       GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	gsize actual_len = 0;
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xC0 + 0x18,	/* request */
					    addr % (1 << 16),	/* value */
					    addr / (1 << 16),	/* idx */
					    data, datasz,	/* data */
					    &actual_len,
					    FU_RTS54HUB_DEVICE_TIMEOUT_RW,
					    NULL, error)) {
		g_prefix_error (error, "failed to read flash: ");
		return FALSE;
	}
	if (actual_len != datasz) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			     "only read %" G_GSIZE_FORMAT "bytes", actual_len);
		return FALSE;
	}
	return TRUE;
}
#endif

static gboolean
fu_rts54hub_device_flash_authentication (FuRts54HubDevice *self, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xC0 + 0x19,	/* request */
					    0x01,		/* value */
					    0x0,		/* idx */
					    NULL, 0,		/* data */
					    NULL,		/* actual */
					    FU_RTS54HUB_DEVICE_TIMEOUT_AUTH,
					    NULL, error)) {
		g_prefix_error (error, "failed to authenticate: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_rts54hub_device_erase_flash (FuRts54HubDevice *self,
				guint8 erase_type,
				GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0xC0 + 0x28,	/* request */
					    erase_type * 256,	/* value */
					    0x0,		/* idx */
					    NULL, 0,		/* data */
					    NULL,		/* actual */
					    FU_RTS54HUB_DEVICE_TIMEOUT_ERASE,
					    NULL, error)) {
		g_prefix_error (error, "failed to erase flash: ");
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_rts54hub_device_vendor_cmd (FuRts54HubDevice *self, guint8 value, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));

	/* don't set something that's already set */
	if (self->vendor_cmd == value) {
		g_debug ("skipping vendor command 0x%02x as already set", value);
		return TRUE;
	}
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0x02,		/* request */
					    value,		/* value */
					    0x0bda,		/* idx */
					    NULL, 0,		/* data */
					    NULL,		/* actual */
					    FU_RTS54HUB_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to issue vendor cmd 0x%02x: ", value);
		return FALSE;
	}
	self->vendor_cmd = value;
	return TRUE;
}

static gboolean
fu_rts54hub_device_ensure_status (FuRts54HubDevice *self, GError **error)
{
	guint8 data[FU_RTS54HUB_DEVICE_STATUS_LEN] = { 0 };
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	gsize actual_len = 0;

	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0x09,		/* request */
					    0x0,		/* value */
					    0x0,		/* idx */
					    data, sizeof(data),
					    &actual_len,	/* actual */
					    FU_RTS54HUB_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to get status: ");
		return FALSE;
	}
	if (actual_len != FU_RTS54HUB_DEVICE_STATUS_LEN) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			     "only read %" G_GSIZE_FORMAT "bytes", actual_len);
		return FALSE;
	}

	/* check the hardware capabilities */
	self->dual_bank = (data[7] & 0x80) == 0x80;
	self->fw_auth = (data[13] & 0x02) > 0;
	self->running_on_flash = (data[15] & 0x02) > 0;

	return TRUE;
}

static gboolean
fu_rts54hub_device_setup (FuDevice *device, GError **error)
{
	FuRts54HubDevice *self = FU_RTS54HUB_DEVICE (device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS (fu_rts54hub_device_parent_class)->setup (device, error))
		return FALSE;

	/* check this device is correct */
	if (!fu_rts54hub_device_vendor_cmd (self, FU_RTS54HUB_VENDOR_CMD_STATUS, error)) {
		g_prefix_error (error, "failed to vendor enable: ");
		return FALSE;
	}
	if (!fu_rts54hub_device_ensure_status (self, error))
		return FALSE;

	/* all three conditions must be set */
	if (!self->running_on_flash) {
		fu_device_set_update_error (device,
					    "device is abnormally running from ROM");
	} else if (!self->fw_auth) {
		fu_device_set_update_error (device,
					    "device does not support authentication");
	} else if (!self->dual_bank) {
		fu_device_set_update_error (device,
					    "device does not support dual-bank updating");
	} else {
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_rts54hub_device_close (FuDevice *device, GError **error)
{
	FuRts54HubDevice *self = FU_RTS54HUB_DEVICE (device);

	/* disable vendor commands */
	if (self->vendor_cmd != FU_RTS54HUB_VENDOR_CMD_NONE) {
		if (!fu_rts54hub_device_vendor_cmd (self, FU_RTS54HUB_VENDOR_CMD_NONE, error)) {
			g_prefix_error (error, "failed to disable vendor command: ");
			return FALSE;
		}
	}

	/* FuUsbDevice->close */
	return FU_DEVICE_CLASS (fu_rts54hub_device_parent_class)->close (device, error);
}

static gboolean
fu_rts54hub_device_write_firmware(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuRts54HubDevice *self = FU_RTS54HUB_DEVICE (device);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* get default image */
	fw = fu_firmware_get_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* enable vendor commands */
	if (!fu_rts54hub_device_vendor_cmd (self,
					    FU_RTS54HUB_VENDOR_CMD_STATUS |
					    FU_RTS54HUB_VENDOR_CMD_FLASH,
					    error)) {
		g_prefix_error (error, "failed to cmd enable: ");
		return FALSE;
	}

	/* erase spare flash bank only if it is not empty */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_ERASE);
	if (!fu_rts54hub_device_erase_flash (self, 1, error))
		return FALSE;

	/* set MCU clock to high clock mode */
	if (!fu_rts54hub_device_highclockmode (self, 0x0001, error)) {
		g_prefix_error (error, "failed to enable MCU clock: ");
		return FALSE;
	}

	/* set SPI controller clock to high clock mode */
	if (!fu_rts54hub_device_highclockmode (self, 0x0101, error)) {
		g_prefix_error (error, "failed to enable SPI clock: ");
		return FALSE;
	}

	/* build packets */
	chunks = fu_chunk_array_new_from_bytes (fw,
						0x00,	/* start addr */
						0x00,	/* page_sz */
						FU_RTS54HUB_DEVICE_BLOCK_SIZE);

	/* write each block */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);

		/* write chunk */
		if (!fu_rts54hub_device_write_flash (self,
						     fu_chunk_get_address (chk),
						     fu_chunk_get_data (chk),
						     fu_chunk_get_data_sz (chk),
						     error))
			return FALSE;

		/* update progress */
		fu_progress_set_percentage_full(progress, (gsize)i, (gsize)chunks->len - 1);
	}

	/* get device to authenticate the firmware */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_VERIFY);
	if (!fu_rts54hub_device_flash_authentication (self, error))
		return FALSE;

	/* send software reset to run available flash code */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	if (!fu_rts54hub_device_reset_flash (self, error))
		return FALSE;

	/* don't reset the vendor command enable, the device will be rebooted */
	self->vendor_cmd = FU_RTS54HUB_VENDOR_CMD_NONE;

	/* success! */
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static FuFirmware *
fu_rts54hub_device_prepare_firmware (FuDevice *device,
				     GBytes *fw,
				     FwupdInstallFlags flags,
				     GError **error)
{
	gsize bufsz = 0;
	guint8 tmp = 0;
	const guint8 *buf = g_bytes_get_data (fw, &bufsz);

	if (!fu_common_read_uint8_safe (buf, bufsz, 0x7ef3, &tmp, error))
		return NULL;
	if ((tmp & 0xf0) != 0x80) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "firmware needs to be dual bank");
		return NULL;
	}
	return fu_firmware_new_from_bytes (fw);
}

static void
fu_rts54hub_device_init (FuRts54HubDevice *self)
{
	fu_device_add_protocol (FU_DEVICE (self), "com.realtek.rts54");
	fu_device_set_remove_delay (FU_DEVICE (self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
}

static void
fu_rts54hub_device_class_init (FuRts54HubDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->write_firmware = fu_rts54hub_device_write_firmware;
	klass_device->setup = fu_rts54hub_device_setup;
	klass_device->to_string = fu_rts54hub_device_to_string;
	klass_device->prepare_firmware = fu_rts54hub_device_prepare_firmware;
	klass_device->close = fu_rts54hub_device_close;
}
