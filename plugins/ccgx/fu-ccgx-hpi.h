/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma	once

#include "config.h"

#include "fu-device.h"
#include "fu-ccgx-i2c.h"
#include "fu-ccgx-common.h"

/* HPI Handle Structure	*/
typedef struct __attribute__((packed))
{
	CyI2CDeviceHandle	 i2c_handle;	/* i2c handle */
	guint8			 hpi_addr_byte;	/* hpiv1: 1 byte, hpiv2: 2 byte	*/
	guint8			 num_of_ports;	/* max number of ports	*/
	FWMode			 fw_mode;	/* fw mode; 0(boot), 1(FW1), 2(FW2) */
} CyHPIHandle;

#define HPI_CMD_FALSH_READ_WRITE_DELAY_US	30000	/* 30 ms */
#define HPI_CMD_ENTER_FLASH_MODE_DELAY_US	20000	/* 20 ms */
#define HPI_CMD_SETUP_EVENT_WAIT_TIME_MS	200	/* 200 ms */
#define HPI_CMD_SETUP_EVENT_CLEAR_TIME_MS	150	/* 100 ms */
#define HPI_CMD_COMMAND_RESPONSE_TIME_MS	500	/* 500 ms */
#define HPI_CMD_COMMAND_CLEAR_EVENT_TIME_MS	30	/* 30 ms */
#define HPI_CMD_RESET_COMPLETE_DELAY_US		150000	/* 150 ms */

gboolean	 fu_ccgx_hpi_cmd_setup			(FuDevice	*device,
							 CyHPIHandle	*hpi_handle,
							 DMDevice	 dm_device,
							 guint16	 usb_inf_num,
							 guint8		 slave_address,
							 GError		**error);
gboolean	 fu_ccgx_hpi_cmd_get_device_data	(FuDevice	*device,
							 CyHPIHandle	*hpi_handle,
							 PDDeviceData	*device_data,
							 GError		**error);
gboolean	 fu_ccgx_hpi_cmd_enter_flash_mode	(FuDevice	*device,
							 CyHPIHandle	*hpi_handle,
							 GError		**error);
gboolean	 fu_ccgx_hpi_cmd_leave_flash_mode	(FuDevice	*device,
							 CyHPIHandle	*hpi_handle,
							 GError		**error);
gboolean	 fu_ccgx_hpi_cmd_write_flash		(FuDevice	*device,
							 CyHPIHandle	*hpi_handle,
							 guint16	 row_num,
							 guint8		*data,
							 guint16	 size,
							 GError		**error);
gboolean	 fu_ccgx_hpi_cmd_read_flash		(FuDevice	*device,
							 CyHPIHandle	*hpi_handle,
							 guint16	 row_num,
							 guint8		*data,
							 guint16	 size,
							 GError		**error);
gboolean	 fu_ccgx_hpi_cmd_validate_fw		(FuDevice	*device,
							 CyHPIHandle	*hpi_handle,
							 guint8		 fw_index,
							 GError		**error);
gboolean	 fu_ccgx_hpi_cmd_reset_device		(FuDevice	*device,
							 CyHPIHandle	*hpi_handle,
							 GError		**error);
gboolean	 fu_ccgx_hpi_cmd_jump_to_alt_fw		(FuDevice	*device,
							 CyHPIHandle	*hpi_handle,
							 GError		**error);
gboolean	 fu_ccgx_hpi_cmd_jump_to_boot		(FuDevice	*device,
							 CyHPIHandle	*hpi_handle,
							 GError		**error);
