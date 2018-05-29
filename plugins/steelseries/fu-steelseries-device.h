/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_STEELSERIES_DEVICE_H
#define __FU_STEELSERIES_DEVICE_H

#include <glib-object.h>
#include <gusb.h>

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_STEELSERIES_DEVICE (fu_steelseries_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuSteelseriesDevice, fu_steelseries_device, FU, STEELSERIES_DEVICE, FuUsbDevice)

struct _FuSteelseriesDeviceClass
{
	FuUsbDeviceClass	parent_class;
};

FuSteelseriesDevice	*fu_steelseries_device_new	(GUsbDevice	*usb_device);

G_END_DECLS

#endif /* __FU_STEELSERIES_DEVICE_H */
