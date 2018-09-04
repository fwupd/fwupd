/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_NITROKEY_DEVICE_H
#define __FU_NITROKEY_DEVICE_H

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_NITROKEY_DEVICE (fu_nitrokey_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuNitrokeyDevice, fu_nitrokey_device, FU, NITROKEY_DEVICE, FuUsbDevice)

struct _FuNitrokeyDeviceClass
{
	FuUsbDeviceClass	parent_class;
};

FuNitrokeyDevice *fu_nitrokey_device_new	(FuUsbDevice		*device);

G_END_DECLS

#endif /* __FU_NITROKEY_DEVICE_H */
