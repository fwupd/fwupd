/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#include "fu-vli-common.h"

#define FU_TYPE_VLI_DEVICE (fu_vli_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuVliDevice, fu_vli_device, FU, VLI_DEVICE, FuUsbDevice)

struct _FuVliDeviceClass
{
	FuUsbDeviceClass	parent_class;
	gboolean		 (*setup)		(FuVliDevice	*self,
							 GError		**error);
	void			 (*to_string)		(FuVliDevice	*self,
							 guint		 idt,
							 GString 	*str);
};

typedef enum {
	FU_VLI_DEVICE_SPI_REQ_READ_ID,
	FU_VLI_DEVICE_SPI_REQ_PAGE_PROG,
	FU_VLI_DEVICE_SPI_REQ_CHIP_ERASE,
	FU_VLI_DEVICE_SPI_REQ_READ_DATA,
	FU_VLI_DEVICE_SPI_REQ_READ_STATUS,
	FU_VLI_DEVICE_SPI_REQ_SECTOR_ERASE,
	FU_VLI_DEVICE_SPI_REQ_WRITE_EN,
	FU_VLI_DEVICE_SPI_REQ_WRITE_STATUS,
	FU_VLI_DEVICE_SPI_REQ_LAST
} FuVliDeviceSpiReq;

#define FU_VLI_DEVICE_TIMEOUT			3000	/* ms */

void		 fu_vli_device_set_kind			(FuVliDevice	*self,
							 FuVliDeviceKind device_kind);
FuVliDeviceKind	 fu_vli_device_get_kind			(FuVliDevice	*self);
gboolean	 fu_vli_device_get_spi_cmd		(FuVliDevice	*self,
							 FuVliDeviceSpiReq req,
							 guint8		*cmd,
							 GError		**error);
