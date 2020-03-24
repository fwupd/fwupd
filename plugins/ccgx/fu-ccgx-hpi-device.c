/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ccgx-common.h"
#include "fu-ccgx-hpi-common.h"
#include "fu-ccgx-hpi-device.h"
#include "fu-ccgx-cyacd-firmware.h"

struct _FuCcgxHpiDevice
{
	FuUsbDevice		 parent_instance;
	guint8			 inf_num;	/* USB interface number */
	guint8			 scb_index;
	guint16			 silicon_id;
	guint16			 fw_app_type;
	guint8			 hpi_addrsz;	/* hpiv1: 1 byte, hpiv2: 2 byte	*/
	guint8			 num_ports;	/* max number of ports	*/
	FWMode			 fw_mode;
	guint8			 slave_address;
	guint8			 ep_bulk_in;
	guint8			 ep_bulk_out;
	guint8			 ep_intr_in;
	guint32			 flash_row_size;
	guint32			 flash_size;
};

G_DEFINE_TYPE (FuCcgxHpiDevice, fu_ccgx_hpi_device, FU_TYPE_USB_DEVICE)

static void
fu_ccgx_hpi_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);
	fu_common_string_append_kx (str, idt, "InfNum", self->inf_num);
	fu_common_string_append_kx (str, idt, "ScbIndex", self->scb_index);
	fu_common_string_append_kx (str, idt, "SiliconId", self->silicon_id);
	fu_common_string_append_kx (str, idt, "FwAppType", self->fw_app_type);
	fu_common_string_append_kx (str, idt, "HpiAddrsz", self->hpi_addrsz);
	fu_common_string_append_kx (str, idt, "NumPorts", self->num_ports);
	fu_common_string_append_kv (str, idt, "FWMode",
				    fu_ccgx_fw_mode_to_string (self->fw_mode));
	fu_common_string_append_kx (str, idt, "EpBulkIn", self->ep_bulk_in);
	fu_common_string_append_kx (str, idt, "EpBulkOut", self->ep_bulk_out);
	fu_common_string_append_kx (str, idt, "EpIntrIn", self->ep_intr_in);
	fu_common_string_append_kx (str, idt, "FlashRowSize", self->flash_row_size);
	fu_common_string_append_kx (str, idt, "FlashSize", self->flash_size);
}

static gboolean
fu_ccgx_hpi_device_get_i2c_status (FuCcgxHpiDevice *self,
				   guint8 mode,
				   guint8 *i2c_status, /* out */
				   GError **error)
{
	g_autoptr(GError) error_local =	NULL;
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    CY_I2C_GET_STATUS_CMD,
					    (((guint16) self->scb_index) << CY_SCB_INDEX_POS) | mode,
					    0x0,
					    (guint8 *) &i2c_status,
					    CY_I2C_GET_STATUS_LEN,
					    NULL,
					    FU_CCGX_HPI_WAIT_TIMEOUT,
					    NULL,
					    &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to get i2c status: %s",
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_hpi_device_get_i2c_config (FuCcgxHpiDevice *self,
				   CyI2CConfig *i2c_config,
				   GError **error)
{
	g_autoptr(GError) error_local = NULL;
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    CY_I2C_GET_CONFIG_CMD,
					    ((guint16) self->scb_index) << CY_SCB_INDEX_POS,
					    0x0,
					    (guint8 *) i2c_config,
					    sizeof(*i2c_config),
					    NULL,
					    FU_CCGX_HPI_WAIT_TIMEOUT,
					    NULL,
					    &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "i2c get config error: control xfer: %s",
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_hpi_device_set_i2c_config (FuCcgxHpiDevice *self,
				   CyI2CConfig *i2c_config,
				   GError **error)
{
	g_autoptr(GError) error_local = NULL;
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    CY_I2C_SET_CONFIG_CMD,
					    ((guint16) self->scb_index) << CY_SCB_INDEX_POS,
					    0x0,
					    (guint8 *) i2c_config,
					    sizeof(*i2c_config),
					    NULL,
					    FU_CCGX_HPI_WAIT_TIMEOUT,
					    NULL,
					    &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "i2c set config error: control xfer: %s",
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_hpi_device_wait_for_notify (FuCcgxHpiDevice *self,
				    guint16 *bytes_pending,
				    GError **error)
{
	guint8 buf[CY_I2C_EVENT_NOTIFICATION_LEN] = { 0x0 };
	g_autoptr(GError) error_local = NULL;

	if (!g_usb_device_interrupt_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					      self->ep_intr_in,
					      buf, sizeof(buf), NULL,
					      FU_CCGX_HPI_WAIT_TIMEOUT,
					      NULL, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to get i2c event: %s",
			     error_local->message);
		return FALSE;
	}

	/* @bytes_pending available on failure */
	if (buf[0] & CY_I2C_ERROR_BIT) {
		if (bytes_pending != NULL) {
			if (!fu_common_read_uint16_safe (buf, sizeof(buf), 0x01,
							 bytes_pending, G_LITTLE_ENDIAN,
							 error))
				return FALSE;
		}
		/* write */
		if (buf[0] & 0x80) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "i2c status error in i2c write [0x%x] event: %s",
				     (guint8) buf[0], error_local->message);
		/* read */
		} else {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "i2c status error in i2c read [0x%x] event: %s",
				     (guint8) buf[0], error_local->message);
		}
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_hpi_device_i2c_read (FuCcgxHpiDevice *self,
			     guint8 *buf, gsize bufsz,
			     CyI2CDataConfigBits cfg_bits,
			     GError **error)
{
	guint8 i2c_status = 0x0;
	guint8 slave_address = 0;

	if (!fu_ccgx_hpi_device_get_i2c_status (self, CY_I2C_MODE_READ, &i2c_status, error)) {
		g_prefix_error (error, "i2c read error: ");
		return FALSE;
	}
	slave_address = (self->slave_address & 0x7F) | (self->scb_index << 7);
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    CY_I2C_READ_CMD,
					    (((guint16) slave_address) << 8) | cfg_bits,
					    bufsz, NULL, 0x0, NULL,
					    FU_CCGX_HPI_WAIT_TIMEOUT, NULL,
					    error)) {
		g_prefix_error (error, "i2c read error: control xfer: ");
		return FALSE;
	}
	if (!g_usb_device_bulk_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					 self->ep_bulk_in,
					 buf, bufsz, NULL,
					 FU_CCGX_HPI_WAIT_TIMEOUT,
					 NULL, error)) {
		g_prefix_error (error, "i2c read error: bulk xfer: ");
		return FALSE;
	}

	/* 10 msec delay */
	g_usleep (I2C_READ_WRITE_DELAY_US);
	if (!fu_ccgx_hpi_device_wait_for_notify (self, NULL, error)) {
		g_prefix_error (error, "i2c read error: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_hpi_device_i2c_write (FuCcgxHpiDevice *self,
			      guint8 *buf, gsize bufsz,
			      CyI2CDataConfigBits cfg_bits,
			      GError **error)
{
	guint8 i2c_status = 0x0;
	guint8 slave_address;
	g_autoptr(GError) error_local = NULL;

	if (!fu_ccgx_hpi_device_get_i2c_status (self,
						CY_I2C_MODE_WRITE,
						&i2c_status,
						error)) {
		g_prefix_error (error, "i2c get status error: ");
		return FALSE;
	}
	slave_address = (self->slave_address & 0x7F) | (self->scb_index << 7);
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    CY_I2C_WRITE_CMD,
					    ((guint16) slave_address << 8) | (cfg_bits & CY_I2C_DATA_CONFIG_STOP),
					    bufsz, /* idx */
					    NULL, 0x0, NULL,
					    FU_CCGX_HPI_WAIT_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "i2c write error: control xfer: ");
		return FALSE;
	}
	if (!g_usb_device_bulk_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					self->ep_bulk_out, buf, bufsz, NULL,
					FU_CCGX_HPI_WAIT_TIMEOUT,
					NULL, error)) {
		g_prefix_error (error, "i2c write error: bulk xfer: ");
		return FALSE;
	}

	/* 10 msec delay */
	g_usleep (I2C_READ_WRITE_DELAY_US);
	if (!fu_ccgx_hpi_device_wait_for_notify (self, NULL, error)) {
		g_prefix_error (error, "i2c wait for notification error: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_hpi_device_reg_read (FuCcgxHpiDevice *self,
			     guint16 addr,
			     guint8 *buf,
			     guint16 bufsz,
			     GError **error)
{
	g_autofree guint8 *bufhw = g_malloc0 (self->hpi_addrsz + 1);
	for (guint32 i = 0; i < self->hpi_addrsz; i++)
		bufhw[i] = (guint8) (addr >> (8 * i));
	if (!fu_ccgx_hpi_device_i2c_write (self, bufhw, self->hpi_addrsz,
					   CY_I2C_DATA_CONFIG_NAK, error)) {
		g_prefix_error (error, "write error: ");
		return FALSE;
	}
	if (!fu_ccgx_hpi_device_i2c_read (self, buf, bufsz,
					  CY_I2C_DATA_CONFIG_STOP | CY_I2C_DATA_CONFIG_NAK,
					  error)) {
		g_prefix_error (error, "read error: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_hpi_device_attach (FuDevice *device, GError **error)
{
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "not supported");
	return FALSE;
}

static FuFirmware *
fu_ccgx_hpi_device_prepare_firmware (FuDevice *device,
				     GBytes *fw,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);
	g_autoptr(FuFirmware) firmware = fu_ccgx_cyacd_firmware_new ();
	g_autoptr(GPtrArray) images = NULL;

	/* parse all images */
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;

	/* check the silicon ID of all images */
	images = fu_firmware_get_images (firmware);
	for (guint i = 0; i < images->len; i++) {
		FuFirmwareImage *img = g_ptr_array_index (images, i);
		if (fu_firmware_image_get_addr (img) != self->silicon_id) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "silicon id mismatch on image %u, "
				     "expected 0x%x, got 0x%x",
				     i, self->silicon_id,
				     (guint) fu_firmware_image_get_addr (img));
			return NULL;
		}
	}

	return g_steal_pointer (&firmware);
}

static gboolean
fu_ccgx_hpi_write_firmware (FuDevice *device,
			    FuFirmware *firmware,
			    FwupdInstallFlags flags,
			    GError **error)
{
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "not supported");
	return FALSE;
}

static gboolean
fu_ccgx_hpi_device_ensure_silicon_id (FuCcgxHpiDevice *self, GError **error)
{
	guint8 buf[2] = { 0x0 };
	g_autofree gchar *instance_id = NULL;

	if (!fu_ccgx_hpi_device_reg_read (self, CY_PD_SILICON_ID,
					  buf, sizeof(buf), error)) {
		g_prefix_error (error, "get silicon id error: ");
		return FALSE;
	}
	if (!fu_common_read_uint16_safe (buf, sizeof(buf),
					0x0, &self->silicon_id,
					G_LITTLE_ENDIAN, error))
		return FALSE;

	/* add quirks */
	instance_id = g_strdup_printf ("CCGX\\SID_%X", self->silicon_id);
	fu_device_add_instance_id_full (FU_DEVICE (self),
					instance_id,
					FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS);

	/* sanity check */
	if (self->flash_row_size == 0x0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Invalid row size for Instance ID: %s",
			     instance_id);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_ccgx_hpi_device_setup (FuDevice *device, GError **error)
{
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);
	CyI2CConfig i2c_config = { 0x0 };
	guint8 mode = 0;
	g_autofree gchar *instance_id = NULL;

	/* set the new config */
	if (!fu_ccgx_hpi_device_get_i2c_config (self, &i2c_config, error)) {
		g_prefix_error (error, "get config error: ");
		return FALSE;
	}
	i2c_config.frequency = FU_CCGX_HPI_FREQ;
	i2c_config.is_master = TRUE;
	i2c_config.is_msb_first = TRUE;
	if (!fu_ccgx_hpi_device_set_i2c_config (self, &i2c_config, error)) {
		g_prefix_error (error, "set config error: ");
		return FALSE;
	}
	if (!fu_ccgx_hpi_device_reg_read (self, CY_PD_REG_DEVICE_MODE_ADDR,
					  &mode, 1, error)) {
		g_prefix_error (error, "get device mode error: ");
		return FALSE;
	}
	self->hpi_addrsz = mode & 0x80 ? 2 : 1;
	self->num_ports = (mode >> 2) & 0x03 ? 2 : 1;
	self->fw_mode = (FWMode) (mode & 0x03);

	/* add extra instance ID */
	instance_id = g_strdup_printf ("USB\\VID_%04X&PID_%04X&MODE_%s",
				       fu_usb_device_get_vid (FU_USB_DEVICE (device)),
				       fu_usb_device_get_pid (FU_USB_DEVICE (device)),
				       fu_ccgx_fw_mode_to_string (self->fw_mode));
	fu_device_add_instance_id (device, instance_id);

	/* get silicon ID */
	if (!fu_ccgx_hpi_device_ensure_silicon_id (self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_ccgx_hpi_device_set_quirk_kv (FuDevice *device,
				 const gchar *key,
				 const gchar *value,
				 GError **error)
{
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);
	if (g_strcmp0 (key, "SiliconId") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp < G_MAXUINT16) {
			self->silicon_id = tmp;
			return TRUE;
		}
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid SiliconId");
		return FALSE;
	}
	if (g_strcmp0 (key, "FwAppType") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp < G_MAXUINT16) {
			self->fw_app_type = tmp;
			return TRUE;
		}
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid FwAppType");
		return FALSE;
	}
	if (g_strcmp0 (key, "FlashRowSize") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp < G_MAXUINT32) {
			self->flash_row_size = tmp;
			return TRUE;
		}
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid FlashRowSize");
		return FALSE;
	}
	if (g_strcmp0 (key, "FlashSize") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp < G_MAXUINT32) {
			self->flash_size = tmp;
			return TRUE;
		}
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid FlashSize");
		return FALSE;
	}
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "no supported");
	return FALSE;
}

static gboolean
fu_ccgx_hpi_device_open (FuUsbDevice *device, GError **error)
{
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);
	g_autoptr(GError) error_local = NULL;
	if (!g_usb_device_claim_interface (fu_usb_device_get_dev (device),
					   self->inf_num,
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "cannot claim interface: %s",
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_hpi_device_close (FuUsbDevice *device, GError **error)
{
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);
	g_autoptr(GError) error_local = NULL;
	if (!g_usb_device_release_interface (fu_usb_device_get_dev (device),
					     self->inf_num,
					     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					     &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "cannot release interface: %s",
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

static void
fu_ccgx_hpi_device_init (FuCcgxHpiDevice *self)
{
	self->inf_num = 0x0;
	self->hpi_addrsz = 1;
	self->num_ports = 1;
	self->slave_address = PD_I2C_SLAVE_ADDRESS;
	self->ep_bulk_out = PD_I2C_USB_EP_BULK_OUT;
	self->ep_bulk_in = PD_I2C_USB_EP_BULK_IN;
	self->ep_intr_in = PD_I2C_USB_EP_INTR_IN;
	fu_device_set_protocol (FU_DEVICE (self), "com.cypress.ccgx");
	fu_device_set_install_duration (FU_DEVICE (self), 60);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);

	/* this might not be true for future hardware */
	if (self->inf_num > 0)
		self->scb_index = 1;
}

static void
fu_ccgx_hpi_device_class_init (FuCcgxHpiDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);
	klass_device->to_string = fu_ccgx_hpi_device_to_string;
	klass_device->write_firmware = fu_ccgx_hpi_write_firmware;
	klass_device->prepare_firmware = fu_ccgx_hpi_device_prepare_firmware;
	klass_device->attach = fu_ccgx_hpi_device_attach;
	klass_device->setup = fu_ccgx_hpi_device_setup;
	klass_device->set_quirk_kv = fu_ccgx_hpi_device_set_quirk_kv;
	klass_usb_device->open = fu_ccgx_hpi_device_open;
	klass_usb_device->close = fu_ccgx_hpi_device_close;
}
