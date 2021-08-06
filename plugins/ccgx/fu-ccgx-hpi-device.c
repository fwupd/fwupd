/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>
#include <string.h>

#include "fu-ccgx-common.h"
#include "fu-ccgx-hpi-common.h"
#include "fu-ccgx-hpi-device.h"
#include "fu-ccgx-firmware.h"
#include "fu-ccgx-firmware.h"

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
	FWImageType		 fw_image_type;
	guint8			 target_address;
	guint8			 ep_bulk_in;
	guint8			 ep_bulk_out;
	guint8			 ep_intr_in;
	guint32			 flash_row_size;
	guint32			 flash_size;
};

G_DEFINE_TYPE (FuCcgxHpiDevice, fu_ccgx_hpi_device, FU_TYPE_USB_DEVICE)

#define HPI_CMD_REG_READ_WRITE_DELAY_US			10000
#define HPI_CMD_ENTER_FLASH_MODE_DELAY_US		20000
#define HPI_CMD_SETUP_EVENT_WAIT_TIME_MS		200
#define HPI_CMD_SETUP_EVENT_CLEAR_TIME_MS		150
#define HPI_CMD_COMMAND_RESPONSE_TIME_MS		500
#define HPI_CMD_COMMAND_CLEAR_EVENT_TIME_MS		30
#define HPI_CMD_RESET_COMPLETE_DELAY_US			150000
#define HPI_CMD_RETRY_DELAY				30 /* ms */
#define HPI_CMD_RESET_RETRY_CNT				3
#define HPI_CMD_ENTER_LEAVE_FLASH_MODE_RETRY_CNT	3
#define HPI_CMD_FLASH_WRITE_RETRY_CNT 			3
#define HPI_CMD_FLASH_READ_RETRY_CNT 			3
#define HPI_CMD_VALIDATE_FW_RETRY_CNT 			3

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
	fu_common_string_append_kv (str, idt, "FwImageType",
				    fu_ccgx_fw_image_type_to_string (self->fw_image_type));
	fu_common_string_append_kx (str, idt, "EpBulkIn", self->ep_bulk_in);
	fu_common_string_append_kx (str, idt, "EpBulkOut", self->ep_bulk_out);
	fu_common_string_append_kx (str, idt, "EpIntrIn", self->ep_intr_in);
	if (self->flash_row_size > 0)
		fu_common_string_append_kx (str, idt, "CcgxFlashRowSize", self->flash_row_size);
	if (self->flash_size > 0)
		fu_common_string_append_kx (str, idt, "CcgxFlashSize", self->flash_size);
}

typedef struct {
	guint8			 mode;
	guint16			 addr;
	guint8			*buf;
	gsize			 bufsz;
} FuCcgxHpiDeviceRetryHelper;

typedef struct {
	guint16			 addr;
	const guint8		*buf;
	gsize			 bufsz;
} FuCcgxHpiFlashWriteRetryHelper;

typedef struct {
	guint16			 addr;
	guint8			*buf;
	gsize			 bufsz;
} FuCcgxHpiFlashReadRetryHelper;

static gboolean
fu_ccgx_hpi_device_i2c_reset_cb (FuDevice *device, gpointer user_data, GError **error)
{
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);
	FuCcgxHpiDeviceRetryHelper *helper = (FuCcgxHpiDeviceRetryHelper *) user_data;
	g_autoptr(GError) error_local = NULL;
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    CY_I2C_RESET_CMD,
					    (self->scb_index << CY_SCB_INDEX_POS) | helper->mode,
					    0x0, NULL, 0x0, NULL,
					    FU_CCGX_HPI_WAIT_TIMEOUT,
					    NULL, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to reset i2c: %s",
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_hpi_device_check_i2c_status (FuCcgxHpiDevice *self,
				     guint8 mode,
				     GError **error)
{
	guint8 buf[CY_I2C_GET_STATUS_LEN] = { 0x0 };
	g_autoptr(GError) error_local =	NULL;
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    CY_I2C_GET_STATUS_CMD,
					    (((guint16) self->scb_index) << CY_SCB_INDEX_POS) | mode,
					    0x0,
					    buf, sizeof(buf),
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
	if (buf[0] & CY_I2C_ERROR_BIT) {
		if (buf[0] & 0x80) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_WRITE,
				     "i2c status write error: 0x%x",
				     buf[0]);
			return FALSE;
		}
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "i2c status read error: 0x%x",
			     buf[0]);
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
		if (buf[0] & 0x80) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_WRITE,
				     "i2c status write error: 0x%x",
				     buf[0]);
			return FALSE;
		}
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "i2c status read error: 0x%x",
			     buf[0]);
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
	guint8 target_address = 0;

	if (!fu_ccgx_hpi_device_check_i2c_status (self, CY_I2C_MODE_READ, error)) {
		g_prefix_error (error, "i2c read error: ");
		return FALSE;
	}
	target_address = (self->target_address & 0x7F) | (self->scb_index << 7);
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    CY_I2C_READ_CMD,
					    (((guint16) target_address) << 8) | cfg_bits,
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
	guint8 target_address;

	if (!fu_ccgx_hpi_device_check_i2c_status (self, CY_I2C_MODE_WRITE, error)) {
		g_prefix_error (error, "i2c get status error: ");
		return FALSE;
	}
	target_address = (self->target_address & 0x7F) | (self->scb_index << 7);
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    CY_I2C_WRITE_CMD,
					    ((guint16) target_address << 8) | (cfg_bits & CY_I2C_DATA_CONFIG_STOP),
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
fu_ccgx_hpi_device_i2c_write_no_resp (FuCcgxHpiDevice *self,
				      guint8 *buf, gsize bufsz,
				      CyI2CDataConfigBits cfg_bits,
				      GError **error)
{
	guint8 target_address = 0;
	g_autoptr(GError) error_local = NULL;

	if (!fu_ccgx_hpi_device_check_i2c_status (self, CY_I2C_MODE_WRITE, error)) {
		g_prefix_error (error, "i2c write error: ");
		return FALSE;
	}
	target_address = (self->target_address & 0x7F) | (self->scb_index << 7);
	if (!g_usb_device_control_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    CY_I2C_WRITE_CMD,
					    ((guint16) target_address << 8) | (cfg_bits & CY_I2C_DATA_CONFIG_STOP),
					    bufsz, NULL, 0x0, NULL,
					    FU_CCGX_HPI_WAIT_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "i2c write error: control xfer: ");
		return FALSE;
	}

	/* device will reboot after this, so txfer will fail */
	if (!g_usb_device_bulk_transfer (fu_usb_device_get_dev (FU_USB_DEVICE (self)),
					 self->ep_bulk_out, buf, bufsz, NULL,
					 FU_CCGX_HPI_WAIT_TIMEOUT,
					 NULL, &error_local)) {
		g_debug ("ignoring i2c write error: bulk xfer: %s",
			 error_local->message);
	}
	return TRUE;
}

static gboolean
fu_ccgx_hpi_device_reg_read_cb (FuDevice *device, gpointer user_data, GError **error)
{
	FuCcgxHpiDeviceRetryHelper *helper = (FuCcgxHpiDeviceRetryHelper *) user_data;
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);
	g_autofree guint8 *bufhw = g_malloc0 (self->hpi_addrsz);

	for (guint32 i = 0; i < self->hpi_addrsz; i++)
		bufhw[i] = (guint8) (helper->addr >> (8 * i));
	if (!fu_ccgx_hpi_device_i2c_write (self, bufhw, self->hpi_addrsz,
					   CY_I2C_DATA_CONFIG_NAK, error)) {
		g_prefix_error (error, "write error: ");
		return FALSE;
	}
	if (!fu_ccgx_hpi_device_i2c_read (self, helper->buf, helper->bufsz,
					  CY_I2C_DATA_CONFIG_STOP | CY_I2C_DATA_CONFIG_NAK,
					  error)) {
		g_prefix_error (error, "read error: ");
		return FALSE;
	}
	g_usleep (HPI_CMD_REG_READ_WRITE_DELAY_US);
	return TRUE;
}

static gboolean
fu_ccgx_hpi_device_reg_read (FuCcgxHpiDevice *self,
			     guint16 addr,
			     guint8 *buf,
			     gsize bufsz,
			     GError **error)
{
	FuCcgxHpiDeviceRetryHelper helper = {
		.addr = addr,
		.mode = CY_I2C_MODE_READ,
		.buf = buf,
		.bufsz = bufsz,
	};
	return fu_device_retry (FU_DEVICE (self),
				fu_ccgx_hpi_device_reg_read_cb,
				HPI_CMD_RESET_RETRY_CNT,
				&helper, error);
}

static gboolean
fu_ccgx_hpi_device_reg_write_cb (FuDevice *device, gpointer user_data, GError **error)
{
	FuCcgxHpiDeviceRetryHelper *helper = (FuCcgxHpiDeviceRetryHelper *) user_data;
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);
	g_autofree guint8 *bufhw = g_malloc0 (helper->bufsz + self->hpi_addrsz);

	for (guint32 i = 0; i < self->hpi_addrsz; i++)
		bufhw[i] = (guint8) (helper->addr >> (8*i));
	memcpy (&bufhw[self->hpi_addrsz], helper->buf, helper->bufsz);
	if (!fu_ccgx_hpi_device_i2c_write (self, bufhw, helper->bufsz + self->hpi_addrsz,
					   CY_I2C_DATA_CONFIG_STOP | CY_I2C_DATA_CONFIG_NAK,
					   error)) {
		g_prefix_error (error, "reg write error: ");
		return FALSE;
	}
	g_usleep (HPI_CMD_REG_READ_WRITE_DELAY_US);
	return TRUE;
}

static gboolean
fu_ccgx_hpi_device_reg_write (FuCcgxHpiDevice *self,
			      guint16 addr,
			      const guint8 *buf,
			      gsize bufsz,
			      GError **error)
{
	FuCcgxHpiDeviceRetryHelper helper = {
		.addr = addr,
		.mode = CY_I2C_MODE_WRITE,
		.buf = (guint8 *) buf,
		.bufsz = bufsz,
	};
	return fu_device_retry (FU_DEVICE (self),
				fu_ccgx_hpi_device_reg_write_cb,
				HPI_CMD_RESET_RETRY_CNT,
				&helper, error);
}

static gboolean
fu_ccgx_hpi_device_reg_write_no_resp (FuCcgxHpiDevice *self,
				      guint16 addr,
				      guint8 *buf,
				      guint16 bufsz,
				      GError **error)
{
	g_autofree guint8 *bufhw = g_malloc0 (bufsz + self->hpi_addrsz);
	for (guint32 i = 0; i < self->hpi_addrsz; i++)
		bufhw[i] = (guint8) (addr >> (8 * i));
	memcpy (&bufhw[self->hpi_addrsz], buf, bufsz);
	if (!fu_ccgx_hpi_device_i2c_write_no_resp (self, bufhw, bufsz + self->hpi_addrsz,
						   CY_I2C_DATA_CONFIG_STOP | CY_I2C_DATA_CONFIG_NAK,
						   error)) {
		g_prefix_error (error, "reg write no-resp error: ");
		return FALSE;
	}
	g_usleep (HPI_CMD_REG_READ_WRITE_DELAY_US);
	return TRUE;
}

static gboolean
fu_ccgx_hpi_device_clear_intr (FuCcgxHpiDevice *self,
			       HPIRegSection section,
			       GError **error)
{
	guint8 intr = 0;
	for (guint8 i = 0; i <= self->num_ports; i++) {
		if (i == section || section == HPI_REG_SECTION_ALL)
			intr |= 1 << i;
	}
	if (!fu_ccgx_hpi_device_reg_write (self, HPI_DEV_REG_INTR_ADDR,
					   &intr, sizeof(intr),
					   error)) {
		g_prefix_error (error, "failed to clear interrupt: ");
		return FALSE;
	}
	return TRUE;
}

static guint16
fu_ccgx_hpi_device_reg_addr_gen (guint8 section, guint8 part, guint8 addr)
{
	return (((guint16) section) << 12) | (((guint16) part) << 8) | addr;
}

static gboolean
fu_ccgx_hpi_device_read_event_reg (FuCcgxHpiDevice *self,
				   HPIRegSection section,
				   HPIEvent *event,
				   GError **error)
{

	if (section != HPI_REG_SECTION_DEV) {
		guint16 reg_addr;
		guint8 buf[4] = { 0x0 };

		/* first read the response register */
		reg_addr = fu_ccgx_hpi_device_reg_addr_gen (section,
							    HPI_REG_PART_PDDATA_READ,
							    0);
		if (!fu_ccgx_hpi_device_reg_read (self,
						  reg_addr,
						  buf, sizeof(buf),
						  error)) {
			g_prefix_error (error, "read response reg error: ");
			return FALSE;
		}

		/* byte 1 is reserved and should read as zero */
		buf[1] = 0;
		memcpy ((guint8 *) event, buf, sizeof(buf));
		if (event->event_length != 0) {
			reg_addr = fu_ccgx_hpi_device_reg_addr_gen (section,
								    HPI_REG_PART_PDDATA_READ,
								    sizeof(buf));
			if (!fu_ccgx_hpi_device_reg_read (self,
							  reg_addr,
							  event->event_data,
							  event->event_length,
							  error)) {
				g_prefix_error (error, "read event data error: ");
				return FALSE;
			}
		}
	} else {
		guint8 buf[2] = { 0x0 };
		if (!fu_ccgx_hpi_device_reg_read (self,
						  CY_PD_REG_RESPONSE_ADDR,
						  buf, sizeof(buf),
						  error)) {
			g_prefix_error (error, "read response reg error: ");
			return FALSE;
		}
		event->event_code   = buf[0];
		event->event_length = buf[1];
		if (event->event_length != 0) {
			/* read the data memory */
			if (!fu_ccgx_hpi_device_reg_read (self,
							  CY_PD_REG_BOOTDATA_MEMORY_ADDR,
							  event->event_data,
							  event->event_length,
							  error)) {
				g_prefix_error (error, "read event data error: ");
				return FALSE;
			}
		}
	}

	/* success */
	return fu_ccgx_hpi_device_clear_intr (self, section, error);
}

static gboolean
fu_ccgx_hpi_device_app_read_intr_reg (FuCcgxHpiDevice *self,
				      HPIRegSection section,
				      HPIEvent *event_array,
				      guint8 *event_count,
				      GError **error)
{
	guint16 reg_addr;
	guint8 event_count_tmp = 0;
	guint8 intr_reg = 0;

	reg_addr = fu_ccgx_hpi_device_reg_addr_gen (HPI_REG_SECTION_DEV,
						    HPI_REG_PART_REG,
						    HPI_DEV_REG_INTR_ADDR);
	if (!fu_ccgx_hpi_device_reg_read (self,
					  reg_addr,
					  &intr_reg,
					  sizeof(intr_reg),
					  error)) {
		g_prefix_error (error, "read intr reg error: ");
		return FALSE;
	}

	/* device section will not come here */
	for (guint8 i = 0; i <= self->num_ports; i++) {
		/* check if this section is needed */
		if (section == i || section == HPI_REG_SECTION_ALL) {
			/* check whether this section has any event/response */
			if ((1 << i) & intr_reg) {
				if (!fu_ccgx_hpi_device_read_event_reg (self,
									section,
									&event_array[i],
									error)) {
					g_prefix_error (error, "read event error: ");
					return FALSE;
				}
				event_count_tmp++;
			}
		}
	}
	if (event_count != NULL)
		*event_count = event_count_tmp;
	return TRUE;
}

static gboolean
fu_ccgx_hpi_device_wait_for_event (FuCcgxHpiDevice *self,
				   HPIRegSection section,
				   HPIEvent *event_array,
				   guint32 timeout_ms,
				   GError **error)
{
	guint8 event_count = 0;
	g_autoptr(GTimer) start_time = g_timer_new ();
	do {
		if (!fu_ccgx_hpi_device_app_read_intr_reg (self,
							   section,
							   event_array,
							   &event_count,
							   error))
			return FALSE;
		if (event_count > 0)
			return TRUE;
	} while (g_timer_elapsed (start_time, NULL) * 1000.f <= timeout_ms);

	/* timed out */
	g_set_error (error,
		     G_IO_ERROR,
		     G_IO_ERROR_TIMED_OUT,
		     "failed to wait for event in %ums",
		     timeout_ms);
	return FALSE;
}

static gboolean
fu_ccgx_hpi_device_get_event (FuCcgxHpiDevice *self,
			      HPIRegSection reg_section,
			      CyPDResp *event,
			      guint32 io_timeout,
			      GError **error )
{
	HPIEvent event_array[HPI_REG_SECTION_ALL + 1] = { 0x0 };
	if (!fu_ccgx_hpi_device_wait_for_event (self,
						reg_section,
						event_array,
						io_timeout,
						error)) {
		g_prefix_error (error, "failed to get event: ");
		return FALSE;
	}
	*event = event_array[reg_section].event_code;
	return TRUE;
}

static gboolean
fu_ccgx_hpi_device_clear_all_events (FuCcgxHpiDevice *self,
				     guint32 io_timeout,
				     GError **error)
{
	HPIEvent event_array[HPI_REG_SECTION_ALL + 1] = { 0x0 };
	if (io_timeout == 0) {
		return fu_ccgx_hpi_device_app_read_intr_reg (self,
							     HPI_REG_SECTION_ALL,
							     event_array,
							     NULL,
							     error);
	}
	for (guint8 i = 0; i < self->num_ports; i++) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_ccgx_hpi_device_wait_for_event (self, i, event_array,
							io_timeout, &error_local)) {
			if (!g_error_matches (error_local,
					      G_IO_ERROR,
					      G_IO_ERROR_TIMED_OUT)) {
				g_propagate_prefixed_error (error,
							    g_steal_pointer (&error_local),
							    "failed to clear events: ");
				return FALSE;
			}
		}
	}
	return TRUE;
}

static gboolean
fu_ccgx_hpi_validate_fw_cb (FuDevice *device, gpointer user_data, GError **error)
{
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);
	guint8 *fw_index = (guint8 *) user_data;
	CyPDResp hpi_event = 0;

	g_return_val_if_fail (fw_index != NULL, FALSE);
	if (!fu_ccgx_hpi_device_clear_all_events (self,
						  HPI_CMD_COMMAND_CLEAR_EVENT_TIME_MS,
						  error))
		return FALSE;

	if (!fu_ccgx_hpi_device_reg_write (self,
					   CY_PD_REG_VALIDATE_FW_ADDR,
					   fw_index, 1,
					   error)) {
		g_prefix_error (error, "validate fw error: ");
		return FALSE;
	}
	if (!fu_ccgx_hpi_device_get_event (self, HPI_REG_SECTION_DEV,
					   &hpi_event,
					   HPI_CMD_COMMAND_RESPONSE_TIME_MS,
					   error)) {
		g_prefix_error (error, "validate fw resp error: ");
		return FALSE;
	}
	if (hpi_event != CY_PD_RESP_SUCCESS) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "validate failed: %s [0x%x]",
			     fu_ccgx_pd_resp_to_string (hpi_event),
			     hpi_event);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_hpi_validate_fw (FuCcgxHpiDevice *self, guint8 fw_index, GError **error)
{
	return fu_device_retry (FU_DEVICE (self),
			        fu_ccgx_hpi_validate_fw_cb,
			        HPI_CMD_VALIDATE_FW_RETRY_CNT,
			        &fw_index, error);
}

static gboolean
fu_ccgx_hpi_enter_flash_mode_cb (FuDevice *device, gpointer user_data, GError **error)
{
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);
	CyPDResp hpi_event = 0;
	guint8 buf[] = { CY_PD_ENTER_FLASHING_MODE_CMD_SIG };

	if (!fu_ccgx_hpi_device_clear_all_events (self,
						  HPI_CMD_COMMAND_CLEAR_EVENT_TIME_MS,
						  error))
		return FALSE;
	if (!fu_ccgx_hpi_device_reg_write (self,
					   CY_PD_REG_ENTER_FLASH_MODE_ADDR,
					   buf, sizeof(buf),
					   error)) {
		g_prefix_error (error, "enter flash mode error: ");
		return FALSE;
	}
	if (!fu_ccgx_hpi_device_get_event (self,
					   HPI_REG_SECTION_DEV,
					   &hpi_event,
					   HPI_CMD_COMMAND_RESPONSE_TIME_MS,
					   error)) {
		g_prefix_error (error, "enter flash mode resp error: ");
		return FALSE;
	}
	if (hpi_event != CY_PD_RESP_SUCCESS) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "enter flash failed: %s [0x%x]",
			     fu_ccgx_pd_resp_to_string (hpi_event),
			     hpi_event);
		return FALSE;
	}

	/* wait 10 msec */
	g_usleep (HPI_CMD_ENTER_FLASH_MODE_DELAY_US);
	return TRUE;
}

static gboolean
fu_ccgx_hpi_enter_flash_mode (FuCcgxHpiDevice *self, GError **error)
{
	return fu_device_retry (FU_DEVICE (self),
			        fu_ccgx_hpi_enter_flash_mode_cb,
			        HPI_CMD_ENTER_LEAVE_FLASH_MODE_RETRY_CNT,
			        NULL, error);
}

static gboolean
fu_ccgx_hpi_leave_flash_mode_cb (FuDevice *device, gpointer user_data, GError **error)
{
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);
	CyPDResp hpi_event = 0;
	guint8 buf = { 0x0 };

	if (!fu_ccgx_hpi_device_clear_all_events (self,
						  HPI_CMD_COMMAND_CLEAR_EVENT_TIME_MS,
						  error))
		return FALSE;

	if (!fu_ccgx_hpi_device_reg_write (self,
					   CY_PD_REG_ENTER_FLASH_MODE_ADDR,
					   &buf, sizeof(buf),
					   error)) {
		g_prefix_error (error, "leave flash mode error: ");
		return FALSE;
	}
	if (!fu_ccgx_hpi_device_get_event (self,
					   HPI_REG_SECTION_DEV,
					   &hpi_event,
					   HPI_CMD_COMMAND_RESPONSE_TIME_MS,
					   error)) {
		g_prefix_error (error, "leave flash mode resp error: ");
		return FALSE;
	}
	if (hpi_event != CY_PD_RESP_SUCCESS) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "leave flash mode failed: %s [0x%x]",
			     fu_ccgx_pd_resp_to_string (hpi_event),
			     hpi_event);
		return FALSE;
	}

	/* wait 10 msec */
	g_usleep (HPI_CMD_ENTER_FLASH_MODE_DELAY_US);
	return TRUE;
}

static gboolean
fu_ccgx_hpi_leave_flash_mode (FuCcgxHpiDevice *self, GError **error)
{
	return fu_device_retry (FU_DEVICE (self),
			        fu_ccgx_hpi_leave_flash_mode_cb,
			        HPI_CMD_ENTER_LEAVE_FLASH_MODE_RETRY_CNT,
			        NULL, error);
}

static gboolean
fu_ccgx_hpi_write_flash_cb (FuDevice *device, gpointer user_data, GError **error)
{
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);
	FuCcgxHpiFlashWriteRetryHelper *helper = (FuCcgxHpiFlashWriteRetryHelper *) user_data;
	CyPDResp hpi_event = 0;
	guint16 addr_tmp = 0;
	guint8 bufhw[] = {
		CY_PD_FLASH_READ_WRITE_CMD_SIG,
		CY_PD_REG_FLASH_ROW_WRITE_CMD,
		helper->addr & 0xFF,
		helper->addr >> 8,
	};

	if (!fu_ccgx_hpi_device_clear_all_events (self,
						  HPI_CMD_COMMAND_CLEAR_EVENT_TIME_MS,
						  error))
		return FALSE;

	/* write data to memory */
	addr_tmp = self->hpi_addrsz > 1 ? HPI_DEV_REG_FLASH_MEM : CY_PD_REG_BOOTDATA_MEMORY_ADDR;
	if (!fu_ccgx_hpi_device_reg_write (self, addr_tmp, helper->buf, helper->bufsz, error)) {
		g_prefix_error (error, "write buf to memory error: ");
		return FALSE;
	}
	if (!fu_ccgx_hpi_device_reg_write (self, CY_PD_REG_FLASH_READ_WRITE_ADDR,
					   bufhw, sizeof(bufhw), error)) {
		g_prefix_error (error, "write flash error: ");
		return FALSE;
	}

	/* wait until flash is written */
	if (!fu_ccgx_hpi_device_get_event (self,
					   HPI_REG_SECTION_DEV,
					   &hpi_event,
					   HPI_CMD_COMMAND_RESPONSE_TIME_MS,
					   error)) {
		g_prefix_error (error, "write flash resp error: ");
		return FALSE;
	}
	if (hpi_event != CY_PD_RESP_SUCCESS) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "write flash failed: %s [0x%x]",
			     fu_ccgx_pd_resp_to_string (hpi_event),
			     hpi_event);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_hpi_write_flash (FuCcgxHpiDevice *self,
			 guint16 addr,
			 const guint8 *buf,
			 guint16 bufsz,
			 GError **error)
{
	FuCcgxHpiFlashWriteRetryHelper helper = {
		.addr = addr,
		.buf = buf,
		.bufsz = bufsz,
	};
	return fu_device_retry (FU_DEVICE (self),
			        fu_ccgx_hpi_write_flash_cb,
			        HPI_CMD_FLASH_WRITE_RETRY_CNT,
			        &helper, error);
}

static gboolean
fu_ccgx_hpi_read_flash_cb (FuDevice *device, gpointer user_data, GError **error)
{
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);
	FuCcgxHpiFlashReadRetryHelper *helper = (FuCcgxHpiFlashReadRetryHelper *) user_data;
	CyPDResp hpi_event = 0;
	guint16 addr_tmp;
	guint8 bufhw[] = {
		CY_PD_FLASH_READ_WRITE_CMD_SIG,
		CY_PD_REG_FLASH_ROW_READ_CMD,
		helper->addr & 0xFF,
		helper->addr >> 8,
	};

	/* set address */
	if (!fu_ccgx_hpi_device_clear_all_events (self,
						  HPI_CMD_COMMAND_CLEAR_EVENT_TIME_MS,
						  error))
		return FALSE;
	if (!fu_ccgx_hpi_device_reg_write (self, CY_PD_REG_FLASH_READ_WRITE_ADDR,
					   bufhw, sizeof(bufhw), error)) {
		g_prefix_error (error, "read flash error: ");
		return FALSE;
	}

	/* wait until flash is read */
	if (!fu_ccgx_hpi_device_get_event (self,
					   HPI_REG_SECTION_DEV,
					   &hpi_event,
					   HPI_CMD_COMMAND_RESPONSE_TIME_MS,
					   error)) {
		g_prefix_error (error, "read flash resp error: ");
		return FALSE;
	}
	if (hpi_event != CY_PD_RESP_FLASH_DATA_AVAILABLE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "read flash failed: %s [0x%x]",
			     fu_ccgx_pd_resp_to_string (hpi_event),
			     hpi_event);
		return FALSE;
	}
	addr_tmp = self->hpi_addrsz > 1 ? HPI_DEV_REG_FLASH_MEM : CY_PD_REG_BOOTDATA_MEMORY_ADDR;
	if (!fu_ccgx_hpi_device_reg_read (self, addr_tmp, helper->buf, helper->bufsz, error)) {
		g_prefix_error (error, "read data from memory error: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_hpi_read_flash (FuCcgxHpiDevice *self,
			guint16 addr,
			guint8 *buf,
			guint16 bufsz,
			GError **error)
{
	FuCcgxHpiFlashReadRetryHelper helper = {
		.addr = addr,
		.buf = buf,
		.bufsz = bufsz,
	};
	return fu_device_retry (FU_DEVICE (self),
			        fu_ccgx_hpi_read_flash_cb,
			        HPI_CMD_FLASH_READ_RETRY_CNT,
			        &helper, error);
}

static gboolean
fu_ccgx_hpi_device_detach (FuDevice *device, GError **error)
{
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);
	guint8 buf[] = {
		CY_PD_JUMP_TO_ALT_FW_CMD_SIG,
	};

	/* not required */
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER) ||
	    self->fw_image_type == FW_IMAGE_TYPE_DUAL_SYMMETRIC)
		return TRUE;

	/* jump to Alt FW */
	if (!fu_ccgx_hpi_device_clear_all_events (self,
						  HPI_CMD_COMMAND_CLEAR_EVENT_TIME_MS,
						  error))
		return FALSE;
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	if (!fu_ccgx_hpi_device_reg_write (self,
					   CY_PD_JUMP_TO_BOOT_REG_ADDR,
					   buf, sizeof(buf),
					   error)) {
		g_prefix_error (error, "jump to alt mode error: ");
		return FALSE;
	}

	/* sym not required */
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success */
	return TRUE;
}

static gboolean
fu_ccgx_hpi_device_attach (FuDevice *device, GError **error)
{
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);
	guint8 buf[] = {
		CY_PD_DEVICE_RESET_CMD_SIG,
		CY_PD_REG_RESET_DEVICE_CMD,
	};
	if (!fu_ccgx_hpi_device_clear_all_events (self,
						  HPI_CMD_COMMAND_CLEAR_EVENT_TIME_MS,
						  error))
		return FALSE;
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	if (!fu_ccgx_hpi_device_reg_write_no_resp (self,
						   CY_PD_REG_RESET_ADDR,
						   buf, sizeof(buf),
						   error)) {
		g_prefix_error (error, "reset device error: ");
		return FALSE;
	}
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static FuFirmware *
fu_ccgx_hpi_device_prepare_firmware (FuDevice *device,
				     GBytes *fw,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);
	FWMode fw_mode;
	guint16 fw_app_type;
	guint16 fw_silicon_id;
	g_autoptr(FuFirmware) firmware = fu_ccgx_firmware_new ();

	/* parse all images */
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;

	/* check the silicon ID */
	fw_silicon_id = fu_ccgx_firmware_get_silicon_id (FU_CCGX_FIRMWARE (firmware));
	if (fw_silicon_id != self->silicon_id) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "silicon id mismatch, expected 0x%x, got 0x%x",
			     self->silicon_id, fw_silicon_id);
		return NULL;
	}
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_VID_PID) == 0) {
		fw_app_type = fu_ccgx_firmware_get_app_type (FU_CCGX_FIRMWARE (firmware));
		if (fw_app_type != self->fw_app_type) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "app type mismatch, expected 0x%x, got 0x%x",
				     self->fw_app_type, fw_app_type);
			return NULL;
		}
	}
	fw_mode = fu_ccgx_firmware_get_fw_mode (FU_CCGX_FIRMWARE (firmware));
	if (fw_mode != fu_ccgx_fw_mode_get_alternate (self->fw_mode)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "FWMode mismatch, expected %s, got %s",
			     fu_ccgx_fw_mode_to_string (fu_ccgx_fw_mode_get_alternate (self->fw_mode)),
			     fu_ccgx_fw_mode_to_string (fw_mode));
		return NULL;
	}
	return g_steal_pointer (&firmware);
}

static gboolean
fu_ccgx_hpi_get_metadata_offset (FuCcgxHpiDevice *self,
				 FWMode fw_mode,
				 guint32 *addr,
				 guint32 *offset,
				 GError **error)
{
	guint32 addr_max;

	/* sanity check */
	if (self->flash_row_size == 0x0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "unset support row size");
		return FALSE;
	}

	/* get the row offset for the flash size */
	addr_max = self->flash_size / self->flash_row_size;
	if (self->flash_row_size == 128) {
		*offset = HPI_META_DATA_OFFSET_ROW_128;
	} else if (self->flash_row_size == 256) {
		*offset = HPI_META_DATA_OFFSET_ROW_256;
	} else {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "unsupported support row size: 0x%x",
			     self->flash_row_size);
		return FALSE;
	}

	/* get the row offset in the flash */
	switch (fw_mode) {
	case FW_MODE_FW1:
		*addr = addr_max - 1;
		break;
	case FW_MODE_FW2:
		*addr = addr_max - 2;
		break;
	default:
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "boot recovery not supported");
		return FALSE;
	}
	return TRUE;
}

/* this will only work after fu_ccgx_hpi_enter_flash_mode() has been used */
static gboolean
fu_ccgx_hpi_load_metadata (FuCcgxHpiDevice *self,
			   FWMode fw_mode,
			   CCGxMetaData *metadata,
			   GError **error)
{
	guint32 addr = 0x0;
	guint32 md_offset = 0x0;
	g_autofree guint8 *buf = NULL;

	/* read flash at correct address */
	if (!fu_ccgx_hpi_get_metadata_offset (self, fw_mode, &addr, &md_offset, error))
		return FALSE;
	buf = g_malloc0 (self->flash_row_size);
	if (!fu_ccgx_hpi_read_flash (self, addr,
				     buf, self->flash_row_size,
				     error)) {
		g_prefix_error (error, "fw metadata read error: ");
		return FALSE;
	}
	return fu_memcpy_safe ((guint8 *) metadata, sizeof(*metadata), 0x0,
			       buf, self->flash_row_size, md_offset,
			       sizeof(metadata), error);
}

/* this will only work after fu_ccgx_hpi_enter_flash_mode() has been used */
static gboolean
fu_ccgx_hpi_save_metadata (FuCcgxHpiDevice *self,
			   FWMode fw_mode,
			   CCGxMetaData *metadata,
			   GError **error)
{
	guint32 addr = 0x0;
	guint32 md_offset = 0x0;
	g_autofree guint8 *buf = NULL;

	/* read entire row of flash at correct address */
	if (!fu_ccgx_hpi_get_metadata_offset (self, fw_mode,
					      &addr, &md_offset,
					      error))
		return FALSE;
	buf = g_malloc0 (self->flash_row_size);
	if (!fu_ccgx_hpi_read_flash (self, addr,
				     buf, self->flash_row_size,
				     error)) {
		g_prefix_error (error, "fw metadata read existing error: ");
		return FALSE;
	}
	if (!fu_memcpy_safe (buf, self->flash_row_size, md_offset,
			     (guint8 *) metadata, sizeof(*metadata), 0x0,
			     sizeof(metadata), error))
		return FALSE;
	if (!fu_ccgx_hpi_write_flash (self, addr,
				      buf, self->flash_row_size,
				      error)) {
		g_prefix_error (error, "fw metadata write error: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_hpi_write_firmware(FuDevice *device,
			   FuFirmware *firmware,
			   FuProgress *progress,
			   FwupdInstallFlags flags,
			   GError **error)
{
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);
	CCGxMetaData metadata = { 0x0 };
	GPtrArray *records = fu_ccgx_firmware_get_records (FU_CCGX_FIRMWARE (firmware));
	FWMode fw_mode_alt = fu_ccgx_fw_mode_get_alternate (self->fw_mode);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* enter flash mode */
	locker = fu_device_locker_new_full (self,
					    (FuDeviceLockerFunc) fu_ccgx_hpi_enter_flash_mode,
					    (FuDeviceLockerFunc) fu_ccgx_hpi_leave_flash_mode,
					    error);
	if (locker == NULL)
		return FALSE;

	/* invalidate metadata for alternate image */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_READ);
	if (!fu_ccgx_hpi_load_metadata (self, fw_mode_alt, &metadata, error))
		return FALSE;
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_ERASE);
	metadata.metadata_valid = 0x00;
	if (!fu_ccgx_hpi_save_metadata (self, fw_mode_alt, &metadata, error))
		return FALSE;

	/* write new image */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 0; i < records->len; i++) {
		FuCcgxFirmwareRecord *rcd = g_ptr_array_index (records, i);

		/* write chunk */
		if (!fu_ccgx_hpi_write_flash (self, rcd->row_number,
					      g_bytes_get_data (rcd->data, NULL),
					      g_bytes_get_size (rcd->data),
					      error)) {
			g_prefix_error (error, "fw write error @0x%x: ", rcd->row_number);
			return FALSE;
		}

		/* update progress */
		fu_progress_set_percentage_full(progress, (gsize)i, (gsize)records->len - 1);
	}

	/* validate fw */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_VERIFY);
	if (!fu_ccgx_hpi_validate_fw (self, fw_mode_alt, error)) {
		g_prefix_error (error, "fw validate error: ");
		return FALSE;
	}

	/* this is a good time to leave the flash mode *before* rebooting */
	if (!fu_device_locker_close (locker, error))
		return FALSE;

	/* success */
	return TRUE;
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
	if (self->flash_row_size == 0x0 ||
	    self->flash_size == 0x0 ||
	    self->flash_size % self->flash_row_size != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "invalid row size for Instance ID %s: 0x%x/0x%x",
			     instance_id,
			     self->flash_row_size,
			     self->flash_size);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_ccgx_hpi_device_set_version_raw (FuCcgxHpiDevice *self, guint32 version_raw)
{
	g_autofree gchar *version = fu_ccgx_version_to_string (version_raw);
	fu_device_set_version (FU_DEVICE (self), version);
	fu_device_set_version_raw (FU_DEVICE (self), version_raw);
}

static void
fu_ccgx_hpi_device_setup_with_fw_mode (FuCcgxHpiDevice *self)
{
	fu_device_set_logical_id (FU_DEVICE (self),
				  fu_ccgx_fw_mode_to_string (self->fw_mode));
}

static void
fu_ccgx_hpi_device_setup_with_app_type (FuCcgxHpiDevice *self)
{
	if (self->silicon_id != 0x0 && self->fw_app_type != 0x0) {
		g_autofree gchar *instance_id1 = NULL;
		g_autofree gchar *instance_id2 = NULL;

		/* we get fw_image_type from the quirk */
		instance_id1 = g_strdup_printf ("USB\\VID_%04X&PID_%04X&SID_%04X&APP_%04X",
						fu_usb_device_get_vid (FU_USB_DEVICE (self)),
						fu_usb_device_get_pid (FU_USB_DEVICE (self)),
						self->silicon_id,
						self->fw_app_type);
		fu_device_add_instance_id (FU_DEVICE (self), instance_id1);
		instance_id2 = g_strdup_printf ("USB\\VID_%04X&PID_%04X&SID_%04X&APP_%04X&MODE_%s",
						fu_usb_device_get_vid (FU_USB_DEVICE (self)),
						fu_usb_device_get_pid (FU_USB_DEVICE (self)),
						self->silicon_id,
						self->fw_app_type,
						fu_ccgx_fw_mode_to_string (self->fw_mode));
		fu_device_add_instance_id (FU_DEVICE (self), instance_id2);
	}
}

static gboolean
fu_ccgx_hpi_device_setup (FuDevice *device, GError **error)
{
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);
	CyI2CConfig i2c_config = { 0x0 };
	guint32 hpi_event = 0;
	guint8 mode = 0;
	g_autoptr(GError) error_local = NULL;

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS (fu_ccgx_hpi_device_parent_class)->setup (device, error))
		return FALSE;

	/* set the new config */
	if (!fu_ccgx_hpi_device_get_i2c_config (self, &i2c_config, error)) {
		g_prefix_error (error, "get config error: ");
		return FALSE;
	}
	i2c_config.frequency = FU_CCGX_HPI_FREQ;
	i2c_config.is_initiator = TRUE;
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
	fu_ccgx_hpi_device_setup_with_fw_mode (self);

	/* get silicon ID */
	if (!fu_ccgx_hpi_device_ensure_silicon_id (self, error))
		return FALSE;

	/* get correct version if not in boot mode */
	if (self->fw_mode != FW_MODE_BOOT) {
		guint16 bufsz;
		guint32 versions[FW_MODE_LAST] = { 0x0 };
		guint8 bufver[HPI_DEVICE_VERSION_SIZE_HPIV2] = { 0x0 };

		bufsz = self->hpi_addrsz == 1 ? HPI_DEVICE_VERSION_SIZE_HPIV1 :
						HPI_DEVICE_VERSION_SIZE_HPIV2;
		if (!fu_ccgx_hpi_device_reg_read (self,
						  CY_PD_GET_VERSION,
						  bufver, bufsz,
						  error)) {
			g_prefix_error (error, "get version error: ");
			return FALSE;
		}

		/* fw1 */
		if (!fu_common_read_uint32_safe (bufver, sizeof(bufver),
						 0x0c, &versions[FW_MODE_FW1],
						 G_LITTLE_ENDIAN, error))
			return FALSE;

		/* fw2 */
		if (!fu_common_read_uint32_safe (bufver, sizeof(bufver),
						 0x14, &versions[FW_MODE_FW2],
						 G_LITTLE_ENDIAN, error))
			return FALSE;

		/* add GUIDs that are specific to the firmware app type */
		self->fw_app_type = versions[self->fw_mode] & 0xffff;
		fu_ccgx_hpi_device_setup_with_app_type (self);

		/* if running in bootloader force an upgrade to any version */
		if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
			fu_ccgx_hpi_device_set_version_raw (self, 0x0);
		} else {
			fu_ccgx_hpi_device_set_version_raw (self, versions[self->fw_mode]);
		}
	}

	/* not supported in boot mode */
	if (self->fw_mode == FW_MODE_BOOT) {
		fu_device_remove_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	} else {
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	}

	/* if we are coming back from reset, wait for hardware to settle */
	if (!fu_ccgx_hpi_device_get_event (self,
					   HPI_REG_SECTION_DEV,
					   &hpi_event,
					   HPI_CMD_SETUP_EVENT_WAIT_TIME_MS,
					   &error_local)) {
		if (!g_error_matches (error_local,
				      G_IO_ERROR,
				      G_IO_ERROR_TIMED_OUT)) {
			g_propagate_error (error, g_steal_pointer (&error_local));
			return FALSE;
		}
	} else {
		if (hpi_event == CY_PD_RESP_RESET_COMPLETE)
			g_usleep (HPI_CMD_RESET_COMPLETE_DELAY_US);
	}

	/* start with no events in the queue */
	return fu_ccgx_hpi_device_clear_all_events (self,
						    HPI_CMD_SETUP_EVENT_CLEAR_TIME_MS,
						    error);
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
	if (g_strcmp0 (key, "CcgxFlashRowSize") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp < G_MAXUINT32) {
			self->flash_row_size = tmp;
			return TRUE;
		}
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid CcgxFlashRowSize");
		return FALSE;
	}
	if (g_strcmp0 (key, "CcgxFlashSize") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp < G_MAXUINT32) {
			self->flash_size = tmp;
			return TRUE;
		}
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid CcgxFlashSize");
		return FALSE;
	}
	if (g_strcmp0 (key, "CcgxImageKind") == 0) {
		self->fw_image_type = fu_ccgx_fw_image_type_from_string (value);
		if (self->fw_image_type != FW_IMAGE_TYPE_UNKNOWN)
			return TRUE;
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid CcgxImageKind");
		return FALSE;
	}
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "no supported");
	return FALSE;
}

static gboolean
fu_ccgx_hpi_device_open (FuDevice *device, GError **error)
{
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);
	g_autoptr(GError) error_local = NULL;

	/* FuUsbDevice->open */
	if (!FU_DEVICE_CLASS (fu_ccgx_hpi_device_parent_class)->open (device, error))
		return FALSE;

	if (!g_usb_device_claim_interface (fu_usb_device_get_dev (FU_USB_DEVICE (device)),
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
fu_ccgx_hpi_device_close (FuDevice *device, GError **error)
{
	FuCcgxHpiDevice *self = FU_CCGX_HPI_DEVICE (device);
	g_autoptr(GError) error_local = NULL;

	/* do not close handle when device restarts */
	if (fu_device_get_status (device) == FWUPD_STATUS_DEVICE_RESTART)
		return TRUE;
	if (!g_usb_device_release_interface (fu_usb_device_get_dev (FU_USB_DEVICE (device)),
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
	/* FuUsbDevice->close */
	return FU_DEVICE_CLASS (fu_ccgx_hpi_device_parent_class)->close (device, error);
}

static void
fu_ccgx_hpi_device_init (FuCcgxHpiDevice *self)
{
	self->inf_num = 0x0;
	self->hpi_addrsz = 1;
	self->num_ports = 1;
	self->target_address = PD_I2C_TARGET_ADDRESS;
	self->ep_bulk_out = PD_I2C_USB_EP_BULK_OUT;
	self->ep_bulk_in = PD_I2C_USB_EP_BULK_IN;
	self->ep_intr_in = PD_I2C_USB_EP_INTR_IN;
	fu_device_add_protocol (FU_DEVICE (self), "com.cypress.ccgx");
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_internal_flag (FU_DEVICE (self), FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
	fu_device_retry_set_delay (FU_DEVICE (self), HPI_CMD_RETRY_DELAY);

	/* we can recover the I²C link using reset */
	fu_device_retry_add_recovery (FU_DEVICE (self),
				      FWUPD_ERROR,
				      FWUPD_ERROR_READ,
				      fu_ccgx_hpi_device_i2c_reset_cb);
	fu_device_retry_add_recovery (FU_DEVICE (self),
				      FWUPD_ERROR,
				      FWUPD_ERROR_WRITE,
				      fu_ccgx_hpi_device_i2c_reset_cb);

	/* this might not be true for future hardware */
	if (self->inf_num > 0)
		self->scb_index = 1;
}

static void
fu_ccgx_hpi_device_class_init (FuCcgxHpiDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->to_string = fu_ccgx_hpi_device_to_string;
	klass_device->write_firmware = fu_ccgx_hpi_write_firmware;
	klass_device->prepare_firmware = fu_ccgx_hpi_device_prepare_firmware;
	klass_device->detach = fu_ccgx_hpi_device_detach;
	klass_device->attach = fu_ccgx_hpi_device_attach;
	klass_device->setup = fu_ccgx_hpi_device_setup;
	klass_device->set_quirk_kv = fu_ccgx_hpi_device_set_quirk_kv;
	klass_device->open = fu_ccgx_hpi_device_open;
	klass_device->close = fu_ccgx_hpi_device_close;
}
