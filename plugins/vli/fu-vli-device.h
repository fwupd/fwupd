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
	gboolean		 (*reset)		(FuVliDevice	*self,
							 GError		**error);
	gboolean		 (*spi_chip_erase)	(FuVliDevice	*self,
							 GError		**error);
	gboolean		 (*spi_sector_erase)	(FuVliDevice	*self,
							 guint32	 addr,
							 GError		**error);
	gboolean		 (*spi_read_data)	(FuVliDevice	*self,
							 guint32	 addr,
							 guint8		*buf,
							 gsize		 bufsz,
							 GError		**error);
	gboolean		 (*spi_read_status)	(FuVliDevice	*self,
							 guint8		*status,
							 GError		**error);
	gboolean		 (*spi_write_enable)	(FuVliDevice	*self,
							 GError		**error);
	gboolean		 (*spi_write_data)	(FuVliDevice	*self,
							 guint32	 addr,
							 const guint8	*buf,
							 gsize		 bufsz,
							 GError		**error);
	gboolean		 (*spi_write_status)	(FuVliDevice	*self,
							 guint8		 status,
							 GError		**error);
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
#define FU_VLI_DEVICE_TXSIZE			0x20	/* bytes */

void		 fu_vli_device_set_kind			(FuVliDevice	*self,
							 FuVliDeviceKind device_kind);
void		 fu_vli_device_set_spi_auto_detect	(FuVliDevice	*self,
							 gboolean	 spi_auto_detect);
FuVliDeviceKind	 fu_vli_device_get_kind			(FuVliDevice	*self);
guint32		 fu_vli_device_get_offset		(FuVliDevice	*self);
gboolean	 fu_vli_device_reset			(FuVliDevice	*self,
							 GError		**error);
gboolean	 fu_vli_device_get_spi_cmd		(FuVliDevice	*self,
							 FuVliDeviceSpiReq req,
							 guint8		*cmd,
							 GError		**error);
gboolean	 fu_vli_device_spi_erase_sector		(FuVliDevice	*self,
							 guint32	 addr,
							 GError		**error);
gboolean	 fu_vli_device_spi_erase_all		(FuVliDevice	*self,
							 GError		**error);
gboolean	 fu_vli_device_spi_erase		(FuVliDevice	*self,
							 guint32	 addr,
							 gsize		 sz,
							 GError		**error);
gboolean	 fu_vli_device_spi_read_block		(FuVliDevice	*self,
							 guint32	 addr,
							 guint8		*buf,
							 gsize		 bufsz,
							 GError		**error);
GBytes		*fu_vli_device_spi_read			(FuVliDevice	*self,
							 guint32	 address,
							 gsize		 bufsz,
							 GError		**error);
gboolean	 fu_vli_device_spi_write_block		(FuVliDevice	*self,
							 guint32	 address,
							 const guint8	*buf,
							 gsize		 bufsz,
							 GError		**error);
gboolean	 fu_vli_device_spi_write		(FuVliDevice	*self,
							 guint32	 address,
							 const guint8	*buf,
							 gsize		 bufsz,
							 GError		**error);
