/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_UDEV_DEVICE_H
#define __FU_UDEV_DEVICE_H

#include <glib-object.h>
#include <gudev/gudev.h>

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_UDEV_DEVICE (fu_udev_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuUdevDevice, fu_udev_device, FU, UDEV_DEVICE, FuDevice)

struct _FuUdevDeviceClass
{
	FuDeviceClass	parent_class;
	gpointer	__reserved[31];
};

FuDevice	*fu_udev_device_new			(GUdevDevice	*udev_device);
void		 fu_udev_device_emit_changed		(FuUdevDevice	*self);
GUdevDevice	*fu_udev_device_get_dev			(FuUdevDevice	*self);
const gchar	*fu_udev_device_get_subsystem		(FuUdevDevice	*self);
guint16		 fu_udev_device_get_vendor		(FuUdevDevice	*self);
guint16		 fu_udev_device_get_model		(FuUdevDevice	*self);
guint8		 fu_udev_device_get_revision		(FuUdevDevice	*self);

G_END_DECLS

#endif /* __FU_UDEV_DEVICE_H */
