/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_WAC_MODULE (fu_wac_module_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuWacModule, fu_wac_module, FU, WAC_MODULE, FuDevice)

struct _FuWacModuleClass
{
	FuDeviceClass		 parent_class;
};

#define FU_WAC_MODULE_FW_TYPE_TOUCH			0x00
#define FU_WAC_MODULE_FW_TYPE_BLUETOOTH			0x01
#define FU_WAC_MODULE_FW_TYPE_EMR_CORRECTION		0x02
#define FU_WAC_MODULE_FW_TYPE_BLUETOOTH_HID		0x03
#define FU_WAC_MODULE_FW_TYPE_MAIN			0x3f

#define FU_WAC_MODULE_COMMAND_START			0x01
#define FU_WAC_MODULE_COMMAND_DATA			0x02
#define FU_WAC_MODULE_COMMAND_END			0x03

gboolean	 fu_wac_module_set_feature	(FuWacModule		*self,
						 guint8			 command,
						 GBytes			*blob,
						 GError			**error);
