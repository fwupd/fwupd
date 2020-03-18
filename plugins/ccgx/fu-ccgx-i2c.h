/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma	once

#include "config.h"
#include <glib-object.h>
#include "fu-device.h"

/* timeout (ms)	for  USB I2C communication */
#define	FU_CCGX_I2C_WAIT_TIMEOUT 5000  

/* max i2c frequency */
#define	FU_CCGX_I2C_FREQ 400000

/* I2C Configuration */
typedef struct __attribute__((packed)) {
	guint32		frequency;		/* I2C clock frequency 1KHz to 400KHz */
	guint8		slave_address;		/* Slave address of the	I2C module, when it is configured as slave */
	guint8		is_master;		/* true- Maste , false- slave */
	guint8		is_clock_stretch;	/* true- Stretch clock in case of no data availability (Valid only for slave mode) false- Do not Stretch clock */
} CyI2CConfig;


/* I2C Data Configuration */
typedef struct __attribute__((packed)) {
	guint8 is_stop_bit;	/* set when stop bit is used */
	guint8 is_nak_bit;	/* set when I2C master wants to NAK the slave after read Applicable only when doing I2C read */
} CyI2CDataConfig;

/* data buffer for I2C communication */
typedef struct __attribute__((packed)) {
    guint8 *buffer;		/* pointer to the buffer from where the	data is read/written */
    guint32 length;		/* length of the buffer */
    guint32 transfer_count;	/* number of bytes actually read/written */
} CyDataBuffer;


/* I2C Handle */
typedef struct __attribute__((packed)){
    guint8 inf_num;		/* USB interface number */
    guint8 slave_address;	/* slave address the master will communicate with */
    struct
    {
	guint8 bulk_in;	/* bulk input endpoint */
	guint8 bulk_out;/* bulk output endpoint */
	guint8 intr_in;	/* iterrupt input endpoint */
    }ep;
}CyI2CDeviceHandle;

gboolean	fu_ccgx_i2c_read (FuDevice *device, CyI2CDeviceHandle *handle, CyI2CDataConfig *data_cfg, CyDataBuffer *data_buffer, GError **error);
gboolean	fu_ccgx_i2c_write (FuDevice *device, CyI2CDeviceHandle *handle, CyI2CDataConfig *data_cfg, CyDataBuffer *data_buffer, GError **error);
gboolean	fu_ccgx_i2c_write_no_resp (FuDevice *device, CyI2CDeviceHandle *handle, CyI2CDataConfig *data_cfg, CyDataBuffer *data_buffer, GError **error);
gboolean	fu_ccgx_i2c_get_config (FuDevice *device, CyI2CDeviceHandle *handle, CyI2CConfig *i2c_config, GError **error);
gboolean	fu_ccgx_i2c_set_config (FuDevice *device, CyI2CDeviceHandle *handle, CyI2CConfig *i2c_config, GError **error);
