/*
 * Copyright (C) 2021 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <gusb.h>
#include <string.h>

#include "fu-genesys-common.h"
#include "fu-genesys-scaler-device.h"
#include "fu-genesys-scaler-firmware.h"

#define GENESYS_SCALER_MSTAR_READ  0x7a
#define GENESYS_SCALER_MSTAR_WRITE 0x7b

#define GENESYS_SCALER_CMD_DATA_WRITE 0x10
#define GENESYS_SCALER_CMD_DATA_READ  0x11
#define GENESYS_SCALER_CMD_DATA_END   0x12

#define GENESYS_SCALER_INFO 0xA4

#define GENESYS_SCALER_USB_TIMEOUT 5000 /* ms */

/**
 * FU_SCALER_FLAG_PAUSE_R2_CPU:
 *
 * Pause R2 CPU.
 */
#define FU_SCALER_FLAG_PAUSE_R2_CPU (1 << 1)
/**
 * FU_SCALER_FLAG_USE_I2C_CH0:
 *
 * Use I2C ch0.
 */
#define FU_SCALER_FLAG_USE_I2C_CH0 (1 << 0)

typedef struct {
	guint8 req_read;
	guint8 req_write;
} FuGenesysVendorCommand;

struct _FuGenesysScalerDevice {
	FuDevice parent_instance;
	guint8 level;
	guint8 public_key[0x212];
	FuCfiDevice *cfi_device;
	FuGenesysVendorCommand vc;
	guint32 sector_size;
	guint32 page_size;
	guint32 transfer_size;
	guint16 gpio_out_reg;
	guint16 gpio_en_reg;
	guint8 gpio_val;
	FuGenesysMtkFooter footer;
};

G_DEFINE_TYPE(FuGenesysScalerDevice, fu_genesys_scaler_device, FU_TYPE_DEVICE)

static gboolean
fu_genesys_scaler_device_enter_serial_debug_mode(FuGenesysScalerDevice *self, GError **error)
{
	FuDevice *parent_device = fu_device_get_parent(FU_DEVICE(self));
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(parent_device));
	guint8 data[] = {0x53, 0x45, 0x52, 0x44, 0x42};

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_write,
					   0x0001,	 /* value */
					   0x0000,	 /* idx */
					   data,	 /* data */
					   sizeof(data), /* data length */
					   NULL,	 /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "error entering Serial Debug Mode: ");
		return FALSE;
	}

	g_usleep(1000); /* 1 ms */

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_exit_serial_debug_mode(FuGenesysScalerDevice *self, GError **error)
{
	FuDevice *parent_device = fu_device_get_parent(FU_DEVICE(self));
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(parent_device));
	guint8 data[] = {0x45};

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_write,
					   0x0001,	 /* value */
					   0x0000,	 /* idx */
					   data,	 /* data */
					   sizeof(data), /* data length */
					   NULL,	 /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "error exiting Serial Debug Mode: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_enter_single_step_mode(FuGenesysScalerDevice *self, GError **error)
{
	FuDevice *parent_device = fu_device_get_parent(FU_DEVICE(self));
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(parent_device));
	guint8 data1[] = {0x10, 0xc0, 0xc1, 0x53};
	guint8 data2[] = {0x10, 0x1f, 0xc1, 0x53};

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_write,
					   0x0001,	  /* value */
					   0x0000,	  /* idx */
					   data1,	  /* data */
					   sizeof(data1), /* data length */
					   NULL,	  /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "error entering Single Step Mode: ");
		return FALSE;
	}

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_write,
					   0x0001,	  /* value */
					   0x0000,	  /* idx */
					   data2,	  /* data */
					   sizeof(data2), /* data length */
					   NULL,	  /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "error entering Single Step Mode: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_exit_single_step_mode(FuGenesysScalerDevice *self, GError **error)
{
	FuDevice *parent_device = fu_device_get_parent(FU_DEVICE(self));
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(parent_device));
	guint8 data[] = {0x10, 0xc0, 0xc1, 0xff};

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_write,
					   0x0001,	 /* value */
					   0x0000,	 /* idx */
					   data,	 /* data */
					   sizeof(data), /* data length */
					   NULL,	 /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "error exiting Single Step Mode: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_enter_debug_mode(FuGenesysScalerDevice *self, GError **error)
{
	FuDevice *parent_device = fu_device_get_parent(FU_DEVICE(self));
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(parent_device));
	guint8 data[] = {0x10, 0x00, 0x00, 0x00};

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_write,
					   0x0001,	 /* value */
					   0x0000,	 /* idx */
					   data,	 /* data */
					   sizeof(data), /* data length */
					   NULL,	 /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "error entering Debug Mode: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_mst_i2c_bus_ctrl(FuGenesysScalerDevice *self, GError **error)
{
	FuDevice *parent_device = fu_device_get_parent(FU_DEVICE(self));
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(parent_device));
	guint8 data[] = {0x35, 0x71};

	for (guint i = 0; i < sizeof(data); i++) {
		if (!g_usb_device_control_transfer(usb_device,
						   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
						   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
						   G_USB_DEVICE_RECIPIENT_DEVICE,
						   self->vc.req_write,
						   0x0001,	    /* value */
						   0x0000,	    /* idx */
						   &data[i],	    /* data */
						   sizeof(data[i]), /* data length */
						   NULL,	    /* actual length */
						   GENESYS_SCALER_USB_TIMEOUT,
						   NULL,
						   error)) {
			g_prefix_error(error, "error sending i2c bus ctrl 0x%02x: ", data[i]);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_mst_i2c_bus_switch_to_ch0(FuGenesysScalerDevice *self, GError **error)
{
	FuDevice *parent_device = fu_device_get_parent(FU_DEVICE(self));
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(parent_device));
	guint8 data[] = {0x80, 0x82, 0x84, 0x51, 0x7f, 0x37, 0x61};

	for (guint i = 0; i < sizeof(data); i++) {
		if (!g_usb_device_control_transfer(usb_device,
						   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
						   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
						   G_USB_DEVICE_RECIPIENT_DEVICE,
						   self->vc.req_write,
						   0x0001,	    /* value */
						   0x0000,	    /* idx */
						   &data[i],	    /* data */
						   sizeof(data[i]), /* data length */
						   NULL,	    /* actual length */
						   GENESYS_SCALER_USB_TIMEOUT,
						   NULL,
						   error)) {
			g_prefix_error(error, "error sending i2c bus ch0 0x%02x: ", data[i]);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_mst_i2c_bus_switch_to_ch4(FuGenesysScalerDevice *self, GError **error)
{
	FuDevice *parent_device = fu_device_get_parent(FU_DEVICE(self));
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(parent_device));
	guint8 data[] = {0x80, 0x82, 0x85, 0x53, 0x7f};

	for (guint i = 0; i < sizeof(data); i++) {
		if (!g_usb_device_control_transfer(usb_device,
						   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
						   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
						   G_USB_DEVICE_RECIPIENT_DEVICE,
						   self->vc.req_write,
						   0x0001,	    /* value */
						   0x0000,	    /* idx */
						   &data[i],	    /* data */
						   sizeof(data[i]), /* data length */
						   NULL,	    /* actual length */
						   GENESYS_SCALER_USB_TIMEOUT,
						   NULL,
						   error)) {
			g_prefix_error(error, "error sending i2c bus ch4 0x%02x: ", data[i]);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_disable_wp(FuGenesysScalerDevice *self, gboolean disable, GError **error)
{
	FuDevice *parent_device = fu_device_get_parent(FU_DEVICE(self));
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(parent_device));
	guint8 data_out[] = {0x10,
			     0x00 /* gpio_out_reg_h */,
			     0x00 /* gpio_out_reg_l */,
			     0x00 /* gpio_out_val */};
	guint8 data_en[] = {0x10,
			    0x00 /* gpio_en_reg_h */,
			    0x00 /* gpio_en_reg_l */,
			    0x00 /* gpio_en_val */};

	/* disable write protect [output] */

	data_out[1] = (self->gpio_out_reg & 0xff00) >> 8;
	data_out[2] = self->gpio_out_reg & 0x00ff;

	/* read gpio-out register */
	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_write,
					   0x0003,		 /* value */
					   0x0000,		 /* idx */
					   data_out,		 /* data */
					   sizeof(data_out) - 1, /* data length */
					   NULL,		 /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error,
			       "error reading GPIO-Out Register 0x%02x%02x: ",
			       data_out[1],
			       data_out[2]);
		return FALSE;
	}

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_read,
					   0x0003,		/* value */
					   0x0000,		/* idx */
					   &data_out[3],	/* data */
					   sizeof(data_out[3]), /* data length */
					   NULL,		/* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error,
			       "error reading GPIO-Out Register 0x%02x%02x: ",
			       data_out[1],
			       data_out[2]);
		return FALSE;
	}

	if (data_out[3] == 0xff) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "error reading GPIO-Out Register 0x%02x%02x: ",
			    data_out[1],
			    data_out[2]);
		return FALSE;
	}

	if (disable)
		data_out[3] |= self->gpio_val; /* pull high */
	else
		data_out[3] &= ~self->gpio_val; /* pull low */

	/* write gpio-out register */
	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_write,
					   0x0001,	     /* value */
					   0x0000,	     /* idx */
					   data_out,	     /* data */
					   sizeof(data_out), /* data length */
					   NULL,	     /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error,
			       "error writing GPIO-Out Register 0x%02x%02x=0x%02x: ",
			       data_out[1],
			       data_out[2],
			       data_out[3]);
		return FALSE;
	}

	/* disable write protect [enable] */

	data_en[1] = (self->gpio_en_reg & 0xff00) >> 8;
	data_en[2] = self->gpio_en_reg & 0x00ff;

	/* read gpio-enable register */
	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_write,
					   0x0003,		/* value */
					   0x0000,		/* idx */
					   data_en,		/* data */
					   sizeof(data_en) - 1, /* data length */
					   NULL,		/* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error,
			       "error writing GPIO-Enable Register 0x%02x%02x: ",
			       data_en[1],
			       data_en[2]);
		return FALSE;
	}

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_read,
					   0x0003,	       /* value */
					   0x0000,	       /* idx */
					   &data_en[3],	       /* data */
					   sizeof(data_en[3]), /* data length */
					   NULL,	       /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error,
			       "error reading GPIO-Out Register 0x%02x%02x: ",
			       data_en[1],
			       data_en[2]);
		return FALSE;
	}

	if (data_en[3] == 0xff) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "error reading GPIO-Enable Register 0x%02x%02x: ",
			    data_en[1],
			    data_en[2]);
		return FALSE;
	}

	data_en[3] &= ~self->gpio_val;

	/* write gpio-enable register */
	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_write,
					   0x0001,	    /* value */
					   0x0000,	    /* idx */
					   data_en,	    /* data */
					   sizeof(data_en), /* data length */
					   NULL,	    /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error,
			       "error writing GPIO-Enable Register 0x%02x%02x=0x%02x: ",
			       data_en[1],
			       data_en[2],
			       data_en[3]);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_pause_r2_cpu(FuGenesysScalerDevice *self, GError **error)
{
	FuDevice *parent_device = fu_device_get_parent(FU_DEVICE(self));
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(parent_device));
	guint8 data[] = {0x10, 0x00, 0x10, 0x0F, 0xD7, 0x00};

	/*
	 * MST9U only!
	 *
	 * Pause R2 CPU for preventing Scaler entering Power Saving Mode also
	 * need for Disable SPI Flash Write Protect Mode.
	 */
	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_write,
					   0x0003,	     /* value */
					   0x0000,	     /* idx */
					   data,	     /* data */
					   sizeof(data) - 1, /* data length */
					   NULL,	     /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error,
			       "error reading register 0x%02x%02x%02x%02x%02x: ",
			       data[0],
			       data[1],
			       data[2],
			       data[3],
			       data[4]);
		return FALSE;
	}

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_read,
					   0x0003,	    /* value */
					   0x0000,	    /* idx */
					   &data[5],	    /* data */
					   sizeof(data[5]), /* data length */
					   NULL,	    /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error,
			       "error reading register 0x%02x%02x%02x%02x%02x: ",
			       data[0],
			       data[1],
			       data[2],
			       data[3],
			       data[4]);
		return FALSE;
	}

	if (data[5] == 0xff) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "error reading register 0x%02x%02x%02x%02x%02x: ",
			    data[0],
			    data[1],
			    data[2],
			    data[3],
			    data[4]);
		return FALSE;
	}

	data[5] |= 0x80;
	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_write,
					   0x0003,	 /* value */
					   0x0000,	 /* idx */
					   data,	 /* data */
					   sizeof(data), /* data length */
					   NULL,	 /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error,
			       "error writing register 0x%02x%02x%02x%02x%02x: ",
			       data[0],
			       data[1],
			       data[2],
			       data[3],
			       data[4]);
		return FALSE;
	}

	g_usleep(200000); /* 200ms */

	/* success */
	return TRUE;
}

static gboolean
fu_geensys_scaler_device_set_isp_mode(FuDevice *device, gpointer user_data, GError **error)
{
	FuGenesysScalerDevice *self = user_data;
	FuDevice *parent_device = fu_device_get_parent(FU_DEVICE(self));
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(parent_device));
	guint8 data[] = {0x4d, 0x53, 0x54, 0x41, 0x52};

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_write,
					   0x0000,	 /* value */
					   0x0000,	 /* idx */
					   data,	 /* data */
					   sizeof(data), /* data length */
					   NULL,	 /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		return FALSE;
	}

	g_usleep(1000); /* 1ms */

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_enter_isp_mode(FuGenesysScalerDevice *self, GError **error)
{
	/*
	 * Enter ISP mode:
	 *
	 * Note: MStar application note say execute twice to avoid race
	 * condition
	 */

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_geensys_scaler_device_set_isp_mode,
				  2,
				  1000 /* 1ms */,
				  self,
				  error)) {
		g_prefix_error(error, "error entering ISP mode: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_exit_isp_mode(FuGenesysScalerDevice *self, GError **error)
{
	FuDevice *parent_device = fu_device_get_parent(FU_DEVICE(self));
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(parent_device));
	guint8 data[] = {0x24};

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_write,
					   0x0000,	 /* value */
					   0x0000,	 /* idx */
					   data,	 /* data */
					   sizeof(data), /* data length */
					   NULL,	 /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "error exiting ISP mode: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuGenesysScalerDevice *self = FU_GENESYS_SCALER_DEVICE(device);

	/*
	 * Important: do not change the order below; otherwise, unexpected
	 * condition occurs.
	 */

	if (!fu_genesys_scaler_device_enter_serial_debug_mode(self, error))
		return FALSE;

	if (!fu_genesys_scaler_device_enter_single_step_mode(self, error))
		return FALSE;

	if (fu_device_has_private_flag(device, FU_SCALER_FLAG_USE_I2C_CH0))
		if (!fu_genesys_scaler_device_mst_i2c_bus_switch_to_ch0(self, error))
			return FALSE;

	if (!fu_genesys_scaler_device_enter_debug_mode(self, error))
		return FALSE;

	if (!fu_genesys_scaler_device_mst_i2c_bus_ctrl(self, error))
		return FALSE;

	if (!fu_genesys_scaler_device_disable_wp(self, TRUE, error))
		return FALSE;

	if (fu_device_has_private_flag(device, FU_SCALER_FLAG_PAUSE_R2_CPU)) {
		if (!fu_genesys_scaler_device_mst_i2c_bus_switch_to_ch4(self, error))
			return FALSE;

		if (!fu_genesys_scaler_device_mst_i2c_bus_ctrl(self, error))
			return FALSE;

		if (!fu_genesys_scaler_device_pause_r2_cpu(self, error))
			return FALSE;
	}

	if (!fu_genesys_scaler_device_enter_isp_mode(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuGenesysScalerDevice *self = FU_GENESYS_SCALER_DEVICE(device);

	if (!fu_genesys_scaler_device_exit_single_step_mode(self, error))
		return FALSE;

	if (!fu_genesys_scaler_device_exit_serial_debug_mode(self, error))
		return FALSE;

	if (!fu_genesys_scaler_device_exit_isp_mode(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_get_level(FuGenesysScalerDevice *self, guint8 *level, GError **error)
{
	FuDevice *parent_device = fu_device_get_parent(FU_DEVICE(self));
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(parent_device));

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   GENESYS_SCALER_INFO,
					   0x0004, /* value */
					   0x0000, /* idx */
					   level,  /* data */
					   1,	   /* data length */
					   NULL,   /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "error getting level: ");
		return FALSE;
	}

	g_usleep(100000); /* 100ms */

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_get_version(FuGenesysScalerDevice *self,
				     guint8 *buf,
				     guint bufsz,
				     GError **error)
{
	FuDevice *parent_device = fu_device_get_parent(FU_DEVICE(self));
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(parent_device));

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   GENESYS_SCALER_INFO,
					   0x0005, /* value */
					   0x0000, /* idx */
					   buf,	   /* data */
					   bufsz,  /* data length */
					   NULL,   /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "error getting version: ");
		return FALSE;
	}

	g_usleep(100000); /* 100ms */

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_get_public_key(FuGenesysScalerDevice *self,
					guint8 *buf,
					guint bufsz,
					GError **error)
{
	FuDevice *parent_device = fu_device_get_parent(FU_DEVICE(self));
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(parent_device));
	const gsize data_size = 0x20;
	g_autoptr(GPtrArray) chunks = NULL;

	chunks = fu_chunk_array_mutable_new(buf, bufsz, 0, 0, data_size);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);

		if (!g_usb_device_control_transfer(usb_device,
						   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
						   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
						   G_USB_DEVICE_RECIPIENT_DEVICE,
						   GENESYS_SCALER_INFO,
						   0x0006,		       /* value */
						   fu_chunk_get_address(chk),  /* idx */
						   fu_chunk_get_data_out(chk), /* data */
						   fu_chunk_get_data_sz(chk),  /* data length */
						   NULL,		       /* actual length */
						   GENESYS_SCALER_USB_TIMEOUT,
						   NULL,
						   error)) {
			g_prefix_error(error, "error getting public key: ");
			return FALSE;
		}

		g_usleep(100000); /* 100ms */
	}

	return TRUE;
}

static gboolean
fu_genesys_scaler_device_read_flash(FuGenesysScalerDevice *self,
				    guint addr,
				    guint8 *buf,
				    guint bufsz,
				    FuProgress *progress,
				    GError **error)
{
	FuDevice *parent_device = fu_device_get_parent(FU_DEVICE(self));
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(parent_device));
	guint8 data1[] = {
	    GENESYS_SCALER_CMD_DATA_WRITE,
	    0x00, /* Read Data command */
	    (addr & 0x00ff0000) >> 16,
	    (addr & 0x0000ff00) >> 8,
	    (addr & 0x000000ff),
	};
	guint8 data2[] = {
	    GENESYS_SCALER_CMD_DATA_READ,
	};
	guint8 data3[] = {
	    GENESYS_SCALER_CMD_DATA_END,
	};
	g_autoptr(GPtrArray) chunks = NULL;

	if (!fu_cfi_device_get_cmd(self->cfi_device, FU_CFI_DEVICE_CMD_READ_DATA, &data1[1], error))
		return FALSE;

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_write,
					   0x0000,	  /* value */
					   0x0000,	  /* idx */
					   data1,	  /* data */
					   sizeof(data1), /* data length */
					   NULL,	  /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "error reading flash at 0x%06x: ", addr);
		return FALSE;
	}

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_write,
					   0x0000,	  /* value */
					   0x0000,	  /* idx */
					   data2,	  /* data */
					   sizeof(data2), /* data length */
					   NULL,	  /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "error reading flash at 0x%06x: ", addr);
		return FALSE;
	}

	chunks = fu_chunk_array_mutable_new(buf, bufsz, addr, 0, self->transfer_size);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);

		if (!g_usb_device_control_transfer(usb_device,
						   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
						   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
						   G_USB_DEVICE_RECIPIENT_DEVICE,
						   self->vc.req_read,
						   0x0000,		       /* value */
						   0x0000,		       /* idx */
						   fu_chunk_get_data_out(chk), /* data */
						   fu_chunk_get_data_sz(chk),  /* data length */
						   NULL,		       /* actual length */
						   GENESYS_SCALER_USB_TIMEOUT,
						   NULL,
						   error)) {
			g_prefix_error(error,
				       "error reading flash at 0x%06x: ",
				       fu_chunk_get_address(chk));
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_write,
					   0x0000,	  /* value */
					   0x0000,	  /* idx */
					   data3,	  /* data */
					   sizeof(data3), /* data length */
					   NULL,	  /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "error reading flash at 0x%06x: ", addr);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_wait_flash_control_register_cb(FuDevice *dev,
							gpointer user_data,
							GError **error)
{
	FuGenesysScalerDevice *self = FU_GENESYS_SCALER_DEVICE(dev);
	FuDevice *parent_device = fu_device_get_parent(FU_DEVICE(self));
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(parent_device));
	FuGenesysWaitFlashRegisterHelper *helper = user_data;
	guint8 status = 0;

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_read,
					   helper->reg << 8 | 0x04, /* value */
					   0x0000,		    /* idx */
					   &status,		    /* data */
					   sizeof(status),	    /* data length */
					   NULL,		    /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "error reading flash control register: ");
		return FALSE;
	}

	if ((status & 0x81) != helper->expected_val) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "wrong value in flash control register");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_flash_control_write_enable(FuGenesysScalerDevice *self, GError **error)
{
	FuDevice *parent_device = fu_device_get_parent(FU_DEVICE(self));
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(parent_device));
	guint8 data1[] = {
	    GENESYS_SCALER_CMD_DATA_WRITE,
	    0x00, /* Write Enable command */
	};
	guint8 data2[] = {
	    GENESYS_SCALER_CMD_DATA_END,
	};

	if (!fu_cfi_device_get_cmd(self->cfi_device, FU_CFI_DEVICE_CMD_WRITE_EN, &data1[1], error))
		return FALSE;

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_write,
					   0x0000,	  /* value */
					   0x0000,	  /* idx */
					   data1,	  /* data */
					   sizeof(data1), /* data length */
					   NULL,	  /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "error sending flash control write enable: ");
		return FALSE;
	}

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_write,
					   0x0000,	  /* value */
					   0x0000,	  /* idx */
					   data2,	  /* data */
					   sizeof(data2), /* data length */
					   NULL,	  /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "error sending flash control write enable: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_flash_control_write_status(FuGenesysScalerDevice *self,
						    guint8 status,
						    GError **error)
{
	FuDevice *parent_device = fu_device_get_parent(FU_DEVICE(self));
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(parent_device));
	guint8 data1[] = {
	    GENESYS_SCALER_CMD_DATA_WRITE,
	    0x00, /* Write Status command */
	    status,
	};
	guint8 data2[] = {
	    GENESYS_SCALER_CMD_DATA_END,
	};

	if (!fu_cfi_device_get_cmd(self->cfi_device,
				   FU_CFI_DEVICE_CMD_WRITE_STATUS,
				   &data1[1],
				   error))
		return FALSE;

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_write,
					   0x0000,	  /* value */
					   0x0000,	  /* idx */
					   data1,	  /* data */
					   sizeof(data1), /* data length */
					   NULL,	  /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "error sending flash control write status 0x%02x: ", status);
		return FALSE;
	}

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_write,
					   0x0000,	  /* value */
					   0x0000,	  /* idx */
					   data2,	  /* data */
					   sizeof(data2), /* data length */
					   NULL,	  /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "error sending flash control write status 0x%02x: ", status);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_flash_control_sector_erase(FuGenesysScalerDevice *self,
						    guint addr,
						    GError **error)
{
	FuDevice *parent_device = fu_device_get_parent(FU_DEVICE(self));
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(parent_device));
	FuGenesysWaitFlashRegisterHelper helper = {
	    .reg = 0x00, /* Read Status command */
	    .expected_val = 0,
	};
	guint8 data1[] = {
	    GENESYS_SCALER_CMD_DATA_WRITE,
	    0x00, /* Sector Erase command */
	    (addr & 0x00ff0000) >> 16,
	    (addr & 0x0000ff00) >> 8,
	    (addr & 0x000000ff),
	};
	guint8 data2[] = {
	    GENESYS_SCALER_CMD_DATA_END,
	};

	if (!fu_cfi_device_get_cmd(self->cfi_device,
				   FU_CFI_DEVICE_CMD_READ_STATUS,
				   &helper.reg,
				   error))
		return FALSE;

	if (!fu_cfi_device_get_cmd(self->cfi_device,
				   FU_CFI_DEVICE_CMD_SECTOR_ERASE,
				   &data1[1],
				   error))
		return FALSE;

	if (!fu_genesys_scaler_device_flash_control_write_enable(self, error))
		return FALSE;

	if (!fu_genesys_scaler_device_flash_control_write_status(self, 0x00, error))
		return FALSE;

	/* 5s: 500x10ms retries */
	if (!fu_device_retry(FU_DEVICE(self),
			     fu_genesys_scaler_device_wait_flash_control_register_cb,
			     500,
			     &helper,
			     error)) {
		g_prefix_error(error, "error waiting for flash control read status register: ");
		return FALSE;
	}

	if (!fu_genesys_scaler_device_flash_control_write_enable(self, error))
		return FALSE;

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_write,
					   0x0000,	  /* value */
					   0x0000,	  /* idx */
					   data1,	  /* data */
					   sizeof(data1), /* data length */
					   NULL,	  /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error,
			       "error sending flash control erase at address 0x%06x: ",
			       addr);
		return FALSE;
	}

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vc.req_write,
					   0x0000,	  /* value */
					   0x0000,	  /* idx */
					   data2,	  /* data */
					   sizeof(data2), /* data length */
					   NULL,	  /* actual length */
					   GENESYS_SCALER_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error,
			       "error sending flash control erase at address 0x%06x: ",
			       addr);
		return FALSE;
	}

	/* 5s: 500x10ms retries */
	if (!fu_device_retry(FU_DEVICE(self),
			     fu_genesys_scaler_device_wait_flash_control_register_cb,
			     500,
			     &helper,
			     error)) {
		g_prefix_error(error, "error waiting for flash control read status register: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_erase_flash(FuGenesysScalerDevice *self,
				     guint addr,
				     guint bufsz,
				     FuProgress *progress,
				     GError **error)
{
	g_autoptr(GPtrArray) chunks = NULL;

	chunks = fu_chunk_array_new(NULL, bufsz, addr, 0, self->sector_size);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);

		if (!fu_genesys_scaler_device_flash_control_sector_erase(self,
									 fu_chunk_get_address(chk),
									 error)) {
			g_prefix_error(error,
				       "error erasing flash at address 0x%06x: ",
				       fu_chunk_get_address(chk));
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_flash_control_page_program(FuGenesysScalerDevice *self,
						    guint addr,
						    const guint8 *buf,
						    guint bufsz,
						    FuProgress *progress,
						    GError **error)
{
	FuDevice *parent_device = fu_device_get_parent(FU_DEVICE(self));
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(parent_device));
	FuGenesysWaitFlashRegisterHelper helper = {
	    .reg = 0x00, /* Read Status command */
	    .expected_val = 0,
	};
	guint8 data1[] = {
	    GENESYS_SCALER_CMD_DATA_WRITE,
	    0x00, /* Page Program command */
	    (addr & 0x00ff0000) >> 16,
	    (addr & 0x0000ff00) >> 8,
	    (addr & 0x000000ff),
	};
	gsize datasz = 0;
	g_autoptr(GPtrArray) chunks = NULL;
	g_autofree guint8 *data = NULL;

	if (!fu_cfi_device_get_cmd(self->cfi_device,
				   FU_CFI_DEVICE_CMD_READ_STATUS,
				   &helper.reg,
				   error))
		return FALSE;

	if (!fu_cfi_device_get_cmd(self->cfi_device, FU_CFI_DEVICE_CMD_PAGE_PROG, &data1[1], error))
		return FALSE;

	datasz = sizeof(data1) + bufsz;
	data = g_malloc0(datasz);
	if (!fu_memcpy_safe(data,
			    datasz,
			    0, /* dst */
			    data1,
			    sizeof(data1),
			    0, /* src */
			    sizeof(data1),
			    error))
		return FALSE;
	if (!fu_memcpy_safe(data,
			    datasz,
			    sizeof(data1), /* dst */
			    buf,
			    bufsz,
			    0, /* src */
			    bufsz,
			    error))
		return FALSE;

	chunks =
	    fu_chunk_array_mutable_new(data, datasz, addr + sizeof(data1), 0, self->transfer_size);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint16 index = 0x0010 * (i + 1);
		/* last chunk */
		if ((i + 1) == chunks->len)
			index |= 0x0080;

		if (!g_usb_device_control_transfer(usb_device,
						   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
						   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
						   G_USB_DEVICE_RECIPIENT_DEVICE,
						   self->vc.req_write,
						   index,		       /* value */
						   0x0000,		       /* idx */
						   fu_chunk_get_data_out(chk), /* data */
						   fu_chunk_get_data_sz(chk),  /* data length */
						   NULL,		       /* actual length */
						   GENESYS_SCALER_USB_TIMEOUT,
						   NULL,
						   error)) {
			g_prefix_error(
			    error,
			    "error sending flash control page program at address 0x%06x: ",
			    fu_chunk_get_address(chk));
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* 200ms: 20x10ms retries */
	if (!fu_device_retry(FU_DEVICE(self),
			     fu_genesys_scaler_device_wait_flash_control_register_cb,
			     20,
			     &helper,
			     error)) {
		g_prefix_error(error, "error waiting for flash control read status register: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_write_sector(FuGenesysScalerDevice *self,
				      guint addr,
				      const guint8 *buf,
				      guint bufsz,
				      FuProgress *progress,
				      GError **error)
{
	g_autoptr(GPtrArray) chunks = NULL;

	chunks = fu_chunk_array_new(buf, bufsz, addr, 0, self->page_size);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);

		if (!fu_genesys_scaler_device_flash_control_page_program(
			self,
			fu_chunk_get_address(chk),
			fu_chunk_get_data(chk),
			fu_chunk_get_data_sz(chk),
			fu_progress_get_child(progress),
			error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_write_flash(FuGenesysScalerDevice *self,
				     guint addr,
				     const guint8 *buf,
				     guint bufsz,
				     FuProgress *progress,
				     GError **error)
{
	g_autoptr(GPtrArray) chunks = NULL;

	chunks = fu_chunk_array_new(buf, bufsz, addr, 0, self->sector_size);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);

		if (!fu_genesys_scaler_device_write_sector(self,
							   fu_chunk_get_address(chk),
							   fu_chunk_get_data(chk),
							   fu_chunk_get_data_sz(chk),
							   fu_progress_get_child(progress),
							   error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_probe(FuDevice *device, GError **error)
{
	FuGenesysScalerDevice *self = FU_GENESYS_SCALER_DEVICE(device);
	guint8 buf[7 + 1] = {0};
	g_autofree gchar *guid = NULL;
	g_autofree gchar *guid_upper = NULL;
	g_autofree gchar *instance_id = NULL;
	g_autofree gchar *version = NULL;

	if (!fu_genesys_scaler_device_get_level(self, &self->level, error))
		return FALSE;

	if (!fu_genesys_scaler_device_get_public_key(self,
						     self->public_key,
						     sizeof(self->public_key),
						     error))
		return FALSE;
	guid =
	    fwupd_guid_hash_data(self->public_key, sizeof(self->public_key), FWUPD_GUID_FLAG_NONE);

	if (!fu_genesys_scaler_device_get_version(self, buf, sizeof(buf), error))
		return FALSE;
	version =
	    fu_common_strsafe((const gchar *)&buf[1], 6); /* ?RIM123; where ? is 0x06 (length?) */

	fu_device_set_version(device, version);
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_logical_id(device, "scaler");
	guid_upper = g_ascii_strup(guid, -1);
	instance_id = g_strdup_printf("GENESYS_SCALER\\MSTAR_TSUM_G&PUBKEY_%s", guid_upper);
	fu_device_add_instance_id(device, instance_id);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);

	self->vc.req_read = GENESYS_SCALER_MSTAR_READ;
	self->vc.req_write = GENESYS_SCALER_MSTAR_WRITE;
	if (self->level != 1) {
		self->vc.req_read += 3;
		self->vc.req_write += 3;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_scaler_device_open(FuDevice *device, GError **error)
{
	FuDevice *parent_device = fu_device_get_parent(device);

	return fu_device_open(parent_device, error);
}

static gboolean
fu_genesys_scaler_device_close(FuDevice *device, GError **error)
{
	FuDevice *parent_device = fu_device_get_parent(device);

	return fu_device_open(parent_device, error);
}

static gboolean
fu_genesys_scaler_device_setup(FuDevice *device, GError **error)
{
	FuGenesysScalerDevice *self = FU_GENESYS_SCALER_DEVICE(device);
	guint32 sector_size;
	guint32 page_size;

	self->cfi_device = fu_cfi_device_new(fu_device_get_context(FU_DEVICE(self)), "C84016");
	if (!fu_device_probe(FU_DEVICE(self->cfi_device), error))
		return FALSE;

	sector_size = fu_cfi_device_get_sector_size(self->cfi_device);
	if (sector_size != 0)
		self->sector_size = sector_size;
	page_size = fu_cfi_device_get_page_size(self->cfi_device);
	if (page_size != 0)
		self->page_size = page_size;

	if (fu_device_get_firmware_size_max(device) == 0) {
		guint64 size_max = fu_device_get_firmware_size_max(FU_DEVICE(self->cfi_device));

		if (size_max == 0)
			size_max = 0x400000;

		if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_DUAL_IMAGE))
			size_max /= 2;

		fu_device_set_firmware_size_max(FU_DEVICE(self), size_max);
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_genesys_scaler_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuGenesysScalerDevice *self = FU_GENESYS_SCALER_DEVICE(device);
	gsize size = fu_device_get_firmware_size_max(device);
	guint addr = 0x000000;
	g_autofree guint8 *buf = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_DUAL_IMAGE))
		addr = 0x200000;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1); /* detach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 99);

	/* require detach -> attach */
	locker = fu_device_locker_new_full(device,
					   (FuDeviceLockerFunc)fu_device_detach,
					   (FuDeviceLockerFunc)fu_device_attach,
					   error);
	if (locker == NULL)
		return NULL;
	fu_progress_step_done(progress);

	buf = g_malloc0(size);
	if (!fu_genesys_scaler_device_read_flash(self,
						 addr,
						 buf,
						 size,
						 fu_progress_get_child(progress),
						 error))
		return NULL;
	fu_progress_step_done(progress);

	return g_bytes_new_take(g_steal_pointer(&buf), size);
}

static FuFirmware *
fu_genesys_scaler_device_prepare_firmware(FuDevice *device,
					  GBytes *fw,
					  FwupdInstallFlags flags,
					  GError **error)
{
	FuGenesysScalerDevice *self = FU_GENESYS_SCALER_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_genesys_scaler_firmware_new();
	g_autoptr(FuFirmware) footer = fu_firmware_new();
	g_autoptr(FuFirmware) payload = fu_firmware_new();
	g_autoptr(GBytes) fw_payload = NULL;
	g_autoptr(GBytes) fw_footer = NULL;

	/* parse firmware */
	if (!fu_firmware_parse(firmware, fw, flags, error))
		return NULL;

	/* payload */
	fw_payload = fu_common_bytes_new_offset(fw,
						0,
						g_bytes_get_size(fw) - sizeof(FuGenesysMtkFooter),
						error);
	if (fw_payload == NULL)
		return NULL;
	if (!fu_firmware_parse(payload, fw_payload, flags, error))
		return NULL;
	fu_firmware_set_id(payload, FU_FIRMWARE_ID_PAYLOAD);
	fu_firmware_add_image(firmware, payload);

	/* footer */
	fw_footer = fu_common_bytes_new_offset(fw,
					       g_bytes_get_size(fw) - sizeof(FuGenesysMtkFooter),
					       sizeof(FuGenesysMtkFooter),
					       error);
	if (!fu_firmware_parse(footer, fw_footer, flags, error))
		return NULL;
	if (!fu_memcpy_safe((guint8 *)&self->footer,
			    sizeof(self->footer),
			    0, /* dst */
			    g_bytes_get_data(fw_footer, NULL),
			    g_bytes_get_size(fw_footer),
			    0, /* src */
			    sizeof(self->footer),
			    error))
		return NULL;
	fu_genesys_scaler_firmware_decrypt((guint8 *)&self->footer, sizeof(self->footer));
	if (g_getenv("FWUPD_GENESYS_SCALER_VERBOSE") != NULL) {
		fu_common_dump_raw(G_LOG_DOMAIN,
				   "Footer",
				   (const guint8 *)&self->footer,
				   sizeof(self->footer));
	}
	if (memcmp(self->footer.data.header.default_head,
		   MTK_RSA_HEADER,
		   sizeof(self->footer.data.header.default_head)) != 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "invalid footer");
		return NULL;
	}
	if (memcmp(&self->footer.data.public_key,
		   self->public_key,
		   sizeof(self->footer.data.public_key)) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "mismatch public-key");
		return NULL;
	}
	fu_firmware_set_id(footer, FU_FIRMWARE_ID_HEADER);
	fu_firmware_add_image(firmware, footer);

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_genesys_scaler_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuGenesysScalerDevice *self = FU_GENESYS_SCALER_DEVICE(device);
	guint addr = 0x000000;
	gsize size = 0x200000;
	const guint8 *data;
	g_autofree guint8 *buf = NULL;
	g_autoptr(FuFirmware) payload = NULL;
	g_autoptr(GBytes) fw_payload = NULL;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 4);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 54);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 42);

	if (self->footer.data.header.configuration_setting.bits.second_image) {
		addr = fu_common_read_uint32(
		    (const guint8 *)self->footer.data.header.second_image_program_addr,
		    G_LITTLE_ENDIAN);
	}

	payload = fu_firmware_get_image_by_id(firmware, FU_FIRMWARE_ID_PAYLOAD, error);
	if (payload == NULL)
		return FALSE;
	fw_payload = fu_firmware_get_bytes(payload, error);
	if (fw_payload == NULL)
		return FALSE;
	data = g_bytes_get_data(fw_payload, &size);
	if (data == NULL)
		return FALSE;

	if (!fu_genesys_scaler_device_erase_flash(self,
						  addr,
						  size,
						  fu_progress_get_child(progress),
						  error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_genesys_scaler_device_write_flash(self,
						  addr,
						  data,
						  size,
						  fu_progress_get_child(progress),
						  error))
		return FALSE;
	fu_progress_step_done(progress);

	buf = g_malloc0(size);
	if (!fu_genesys_scaler_device_read_flash(self,
						 addr,
						 buf,
						 size,
						 fu_progress_get_child(progress),
						 error))
		return FALSE;
	if (!fu_common_bytes_compare_raw(buf, size, data, size, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_genesys_scaler_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0); /* detach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100); /* write */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0); /* attach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0);	/* reload */
}

static void
fu_genesys_scaler_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuGenesysScalerDevice *self = FU_GENESYS_SCALER_DEVICE(device);
	gchar public_key_e[6 + 1] = {0};
	gchar public_key_n[0x200 + 1] = {0};
	g_autoptr(GError) error_local_e = NULL;
	g_autoptr(GError) error_local_n = NULL;

	fu_common_string_append_kx(str, idt, "Level", self->level);
	if (fu_memcpy_safe((guint8 *)public_key_e,
			   sizeof(public_key_e),
			   0, /* dst */
			   self->public_key,
			   sizeof(self->public_key),
			   sizeof(self->public_key) - 2 - (sizeof(public_key_e) - 1), /* src */
			   sizeof(public_key_e) - 1,
			   &error_local_e)) {
		fu_common_string_append_kv(str, idt, "PublicKeyE", public_key_e);
	} else {
		g_debug("ignoring public-key parameter E: %s", error_local_e->message);
	}
	if (fu_memcpy_safe((guint8 *)public_key_n,
			   sizeof(public_key_n),
			   0, /* dst */
			   self->public_key,
			   sizeof(self->public_key),
			   4, /* src */
			   sizeof(public_key_n) - 1,
			   &error_local_n)) {
		fu_common_string_append_kv(str, idt, "PublicKeyN", public_key_n);
	} else {
		g_debug("ignoring public-key parameter N: %s", error_local_n->message);
	}
	fu_common_string_append_kx(str, idt, "ReadRequestRead", self->vc.req_read);
	fu_common_string_append_kx(str, idt, "WriteRequest", self->vc.req_write);
	fu_common_string_append_kx(str, idt, "SectorSize", self->sector_size);
	fu_common_string_append_kx(str, idt, "PageSize", self->page_size);
	fu_common_string_append_kx(str, idt, "TransferSize", self->transfer_size);
	fu_common_string_append_kx(str, idt, "GpioOutputRegister", self->gpio_out_reg);
	fu_common_string_append_kx(str, idt, "GpioEnableRegister", self->gpio_en_reg);
	fu_common_string_append_kx(str, idt, "GpioValue", self->gpio_val);
}

static gboolean
fu_genesys_scaler_device_set_quirk_kv(FuDevice *device,
				      const gchar *key,
				      const gchar *value,
				      GError **error)
{
	FuGenesysScalerDevice *self = FU_GENESYS_SCALER_DEVICE(device);
	guint64 tmp;

	if (g_strcmp0(key, "GenesysScalerDeviceTransferSize") == 0) {
		if (!fu_common_strtoull_full(value, &tmp, 0, G_MAXUINT32, error))
			return FALSE;
		self->transfer_size = tmp;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysScalerGpioOutputRegister") == 0) {
		if (!fu_common_strtoull_full(value, &tmp, 0, G_MAXUINT16, error))
			return FALSE;
		self->gpio_out_reg = tmp;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysScalerGpioEnableRegister") == 0) {
		if (!fu_common_strtoull_full(value, &tmp, 0, G_MAXUINT16, error))
			return FALSE;
		self->gpio_en_reg = tmp;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysScalerGpioValue") == 0) {
		if (!fu_common_strtoull_full(value, &tmp, 0, G_MAXUINT16, error))
			return FALSE;
		self->gpio_val = tmp;

		/* success */
		return TRUE;
	}

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_genesys_scaler_device_init(FuGenesysScalerDevice *self)
{
	fu_device_set_vendor(FU_DEVICE(self), "MStar Semiconductor");
	fu_device_set_name(FU_DEVICE(self), "TSUMG");
	fu_device_add_protocol(FU_DEVICE(self), "com.mstarsemi.scaler");
	fu_device_retry_set_delay(FU_DEVICE(self), 10); /* ms */
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_SCALER_FLAG_PAUSE_R2_CPU,
					"pause-r2-cpu");
	fu_device_register_private_flag(FU_DEVICE(self), FU_SCALER_FLAG_USE_I2C_CH0, "use-i2c-ch0");
	self->sector_size = 0x1000; /* 4KB */
	self->page_size = 0x100;    /* 256B */
	self->transfer_size = 0x40; /* 64B */
}

static void
fu_genesys_scaler_device_class_init(FuGenesysScalerDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->probe = fu_genesys_scaler_device_probe;
	klass_device->open = fu_genesys_scaler_device_open;
	klass_device->close = fu_genesys_scaler_device_close;
	klass_device->setup = fu_genesys_scaler_device_setup;
	klass_device->dump_firmware = fu_genesys_scaler_device_dump_firmware;
	klass_device->prepare_firmware = fu_genesys_scaler_device_prepare_firmware;
	klass_device->write_firmware = fu_genesys_scaler_device_write_firmware;
	klass_device->set_progress = fu_genesys_scaler_device_set_progress;
	klass_device->detach = fu_genesys_scaler_device_detach;
	klass_device->attach = fu_genesys_scaler_device_attach;
	klass_device->to_string = fu_genesys_scaler_device_to_string;
	klass_device->set_quirk_kv = fu_genesys_scaler_device_set_quirk_kv;
}

FuGenesysScalerDevice *
fu_genesys_scaler_device_new(FuContext *ctx)
{
	FuGenesysScalerDevice *device = NULL;
	device = g_object_new(FU_TYPE_GENESYS_SCALER_DEVICE, "context", ctx, NULL);
	return device;
}
