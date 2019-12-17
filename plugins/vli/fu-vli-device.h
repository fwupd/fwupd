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

