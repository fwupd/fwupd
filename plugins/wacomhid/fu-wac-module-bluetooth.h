/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_WAC_MODULE_BLUETOOTH_H
#define __FU_WAC_MODULE_BLUETOOTH_H

#include <glib-object.h>
#include <gusb.h>

#include "fu-wac-module.h"

G_BEGIN_DECLS

#define FU_TYPE_WAC_MODULE_BLUETOOTH (fu_wac_module_bluetooth_get_type ())
G_DECLARE_FINAL_TYPE (FuWacModuleBluetooth, fu_wac_module_bluetooth, FU, WAC_MODULE_BLUETOOTH, FuWacModule)

FuWacModule	*fu_wac_module_bluetooth_new	(GUsbDevice	*usb_device);

G_END_DECLS

#endif /* __FU_WAC_MODULE_BLUETOOTH_H */
