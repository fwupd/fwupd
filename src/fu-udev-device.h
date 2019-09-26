/*
 * Copyright (C) 2017-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <gudev/gudev.h>

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_UDEV_DEVICE (fu_udev_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuUdevDevice, fu_udev_device, FU, UDEV_DEVICE, FuDevice)

struct _FuUdevDeviceClass
{
	FuDeviceClass	parent_class;
	gboolean	 (*probe)			(FuUdevDevice	*device,
							 GError		**error);
	gpointer	__reserved[31];
};

FuUdevDevice	*fu_udev_device_new			(GUdevDevice	*udev_device);
GUdevDevice	*fu_udev_device_get_dev			(FuUdevDevice	*self);
const gchar	*fu_udev_device_get_device_file		(FuUdevDevice	*self);
const gchar	*fu_udev_device_get_sysfs_path		(FuUdevDevice	*self);
const gchar	*fu_udev_device_get_subsystem		(FuUdevDevice	*self);
guint16		 fu_udev_device_get_vendor		(FuUdevDevice	*self);
guint16		 fu_udev_device_get_model		(FuUdevDevice	*self);
guint8		 fu_udev_device_get_revision		(FuUdevDevice	*self);
guint		 fu_udev_device_get_slot_depth		(FuUdevDevice	*self,
							 const gchar	*subsystem);
gboolean	 fu_udev_device_set_physical_id		(FuUdevDevice	*self,
							 const gchar	*subsystem,
							 GError		**error);
void		 fu_udev_device_dump			(FuUdevDevice	*self);

G_END_DECLS
