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

void		 fu_vli_device_set_kind			(FuVliDevice	*self,
							 FuVliDeviceKind device_kind);
FuVliDeviceKind	 fu_vli_device_get_kind			(FuVliDevice	*self);

gboolean	 fu_vli_device_vdr_reg_read		(FuVliDevice	*self,
							 guint8		 fun_num,
							 guint16	 offset,
							 guint8		*buf,
							 GError		**error);
gboolean	 fu_vli_device_vdr_reg_write		(FuVliDevice	*self,
							 guint8		 fun_num,
							 guint16	 offset,
							 guint8		 value,
							 GError		**error);

gboolean	 fu_vli_device_spi_write_enable		(FuVliDevice	*self,
							 GError		**error);
gboolean	 fu_vli_device_spi_erase		(FuVliDevice	*self,
							 guint32	 addr,
							 gsize		 sz,
							 GError		**error);
gboolean	 fu_vli_device_spi_erase_verify		(FuVliDevice	*self,
							 guint32	 addr,
							 GError		**error);
gboolean	 fu_vli_device_spi_erase_all		(FuVliDevice	*self,
							 GError		**error);
gboolean	 fu_vli_device_spi_write		(FuVliDevice	*self,
							 guint32	 address,
							 const guint8	*buf,
							 gsize		 bufsz,
							 GError		**error);
gboolean	 fu_vli_device_spi_write_verify		(FuVliDevice	*self,
							 guint32	 address,
							 const guint8	*buf,
							 gsize		 bufsz,
							 GError		**error);
gboolean	 fu_vli_device_spi_read			(FuVliDevice	*self,
							 guint32	 data_addr,
							 guint8		*buf,
							 gsize		 length,
							 GError		**error);
GBytes		*fu_vli_device_spi_read_all		(FuVliDevice	*self,
							 guint32	 address,
							 gsize		 bufsz,
							 GError		**error);

gboolean	 fu_vli_device_i2c_read			(FuVliDevice	*self,
							 guint8		 cmd,
							 guint8		*buf,
							 gsize		 bufsz,
							 GError		**error);
gboolean	 fu_vli_device_i2c_read_status		(FuVliDevice	*self,
							 FuVliDeviceI2cStatus	*status,
							 GError		**error);
gboolean	 fu_vli_device_i2c_write		(FuVliDevice	*self,
							 guint8		 cmd,
							 const guint8	*buf,
							 gsize		 bufsz,
							 GError		**error);
gboolean	 fu_vli_device_i2c_write_data		(FuVliDevice	*self,
							 guint8		 skip_s,
							 guint8		 skip_p,
							 const guint8	*buf,
							 gsize		 bufsz,
							 GError		**error);
