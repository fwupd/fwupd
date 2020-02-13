/*
 * Copyright (C) 2017-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#ifdef HAVE_GUDEV
#include <gudev/gudev.h>
#else
#define G_UDEV_TYPE_DEVICE	G_TYPE_OBJECT
#define GUdevDevice		GObject
#endif

#include "fu-plugin.h"

#define FU_TYPE_UDEV_DEVICE (fu_udev_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuUdevDevice, fu_udev_device, FU, UDEV_DEVICE, FuDevice)

struct _FuUdevDeviceClass
{
	FuDeviceClass	parent_class;
	gboolean	 (*probe)			(FuUdevDevice	*device,
							 GError		**error);
	gboolean	 (*open)			(FuUdevDevice	*device,
							 GError		**error);
	gboolean	 (*close)			(FuUdevDevice	*device,
							 GError		**error);
	gpointer	__reserved[29];
};

/**
 * FuUdevDeviceFlags:
 * @FU_UDEV_DEVICE_FLAG_NONE:			No flags set
 * @FU_UDEV_DEVICE_FLAG_OPEN_READ:		Open the device read-only
 * @FU_UDEV_DEVICE_FLAG_OPEN_WRITE:		Open the device write-only
 * @FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT:	Get the vendor ID fallback from the parent
 *
 * Flags used when opening the device using fu_device_open().
 **/
typedef enum {
	FU_UDEV_DEVICE_FLAG_NONE		= 0,
	FU_UDEV_DEVICE_FLAG_OPEN_READ		= 1 << 0,
	FU_UDEV_DEVICE_FLAG_OPEN_WRITE		= 1 << 1,
	FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT	= 1 << 2,
	/*< private >*/
	FU_UDEV_DEVICE_FLAG_LAST
} FuUdevDeviceFlags;

FuUdevDevice	*fu_udev_device_new			(GUdevDevice	*udev_device);
GUdevDevice	*fu_udev_device_get_dev			(FuUdevDevice	*self);
const gchar	*fu_udev_device_get_device_file		(FuUdevDevice	*self);
const gchar	*fu_udev_device_get_sysfs_path		(FuUdevDevice	*self);
const gchar	*fu_udev_device_get_subsystem		(FuUdevDevice	*self);
guint32		 fu_udev_device_get_vendor		(FuUdevDevice	*self);
guint32		 fu_udev_device_get_model		(FuUdevDevice	*self);
guint8		 fu_udev_device_get_revision		(FuUdevDevice	*self);
guint		 fu_udev_device_get_slot_depth		(FuUdevDevice	*self,
							 const gchar	*subsystem);
gboolean	 fu_udev_device_set_physical_id		(FuUdevDevice	*self,
							 const gchar	*subsystems,
							 GError		**error);
void		 fu_udev_device_set_readonly		(FuUdevDevice	*self,
							 gboolean	 readonly)
G_GNUC_DEPRECATED_FOR(fu_udev_device_set_flags);
void		 fu_udev_device_set_flags		(FuUdevDevice	*self,
							 FuUdevDeviceFlags flags);

gint		 fu_udev_device_get_fd			(FuUdevDevice	*self);
void		 fu_udev_device_set_fd			(FuUdevDevice	*self,
							 gint		 fd);
gboolean	 fu_udev_device_ioctl			(FuUdevDevice	*self,
							 gulong		 request,
							 guint8		*buf,
							 gint		*rc,
							 GError		**error);
gboolean	 fu_udev_device_pwrite			(FuUdevDevice	*self,
							 goffset	 port,
							 guint8		 data,
							 GError		**error);
gboolean	 fu_udev_device_pread			(FuUdevDevice	*self,
							 goffset	 port,
							 guint8		*data,
							 GError		**error);
