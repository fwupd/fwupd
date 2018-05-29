/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_COLORHUG_DEVICE_H
#define __FU_COLORHUG_DEVICE_H

#include <glib-object.h>
#include <gusb.h>

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_COLORHUG_DEVICE (fu_colorhug_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuColorhugDevice, fu_colorhug_device, FU, COLORHUG_DEVICE, FuUsbDevice)

struct _FuColorhugDeviceClass
{
	FuUsbDeviceClass	parent_class;
};

FuColorhugDevice *fu_colorhug_device_new		(GUsbDevice		*usb_device);
gboolean	 fu_colorhug_device_get_is_bootloader	(FuColorhugDevice	*device);

/* object methods */
gboolean	 fu_colorhug_device_set_flash_success	(FuColorhugDevice	*device,
							 GError			**error);
gboolean	 fu_colorhug_device_verify_firmware	(FuColorhugDevice	*device,
							 GError			**error);

G_END_DECLS

#endif /* __FU_COLORHUG_DEVICE_H */
