/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
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
	void		 (*to_string)			(FuUdevDevice	*self,
							 guint		 indent,
							 GString	*str);
	gpointer	__reserved[28];
};

/**
 * FuUdevDeviceFlags:
 * @FU_UDEV_DEVICE_FLAG_NONE:			No flags set
 * @FU_UDEV_DEVICE_FLAG_OPEN_READ:		Open the device read-only
 * @FU_UDEV_DEVICE_FLAG_OPEN_WRITE:		Open the device write-only
 * @FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT:	Get the vendor ID fallback from the parent
 * @FU_UDEV_DEVICE_FLAG_USE_CONFIG:		Read and write from the device config
 * @FU_UDEV_DEVICE_FLAG_OPEN_NONBLOCK:		Open nonblocking, e.g. O_NONBLOCK
 *
 * Flags used when opening the device using fu_device_open().
 **/
typedef enum {
	FU_UDEV_DEVICE_FLAG_NONE		= 0,
	FU_UDEV_DEVICE_FLAG_OPEN_READ		= 1 << 0,
	FU_UDEV_DEVICE_FLAG_OPEN_WRITE		= 1 << 1,
	FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT	= 1 << 2,
	FU_UDEV_DEVICE_FLAG_USE_CONFIG		= 1 << 3,
	FU_UDEV_DEVICE_FLAG_OPEN_NONBLOCK	= 1 << 4,
	/*< private >*/
	FU_UDEV_DEVICE_FLAG_LAST
} FuUdevDeviceFlags;

FuUdevDevice	*fu_udev_device_new			(GUdevDevice	*udev_device);
GUdevDevice	*fu_udev_device_get_dev			(FuUdevDevice	*self);
const gchar	*fu_udev_device_get_device_file		(FuUdevDevice	*self);
const gchar	*fu_udev_device_get_sysfs_path		(FuUdevDevice	*self);
const gchar	*fu_udev_device_get_subsystem		(FuUdevDevice	*self);
const gchar	*fu_udev_device_get_driver		(FuUdevDevice	*self);
guint32		 fu_udev_device_get_vendor		(FuUdevDevice	*self);
guint32		 fu_udev_device_get_model		(FuUdevDevice	*self);
guint32		 fu_udev_device_get_subsystem_vendor	(FuUdevDevice	*self);
guint32		 fu_udev_device_get_subsystem_model	(FuUdevDevice	*self);
guint8		 fu_udev_device_get_revision		(FuUdevDevice	*self);
guint64		 fu_udev_device_get_number		(FuUdevDevice	*self);
guint		 fu_udev_device_get_slot_depth		(FuUdevDevice	*self,
							 const gchar	*subsystem);
gboolean	 fu_udev_device_set_physical_id		(FuUdevDevice	*self,
							 const gchar	*subsystems,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 fu_udev_device_set_logical_id		(FuUdevDevice	*self,
							 const gchar	*subsystem,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
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
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 fu_udev_device_pwrite			(FuUdevDevice	*self,
							 goffset	 port,
							 guint8		 data,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 fu_udev_device_pwrite_full		(FuUdevDevice	*self,
							 goffset	 port,
							 const guint8	*buf,
							 gsize		 bufsz,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 fu_udev_device_pread			(FuUdevDevice	*self,
							 goffset	 port,
							 guint8		*data,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 fu_udev_device_pread_full		(FuUdevDevice	*self,
							 goffset	 port,
							 guint8		*buf,
							 gsize		 bufsz,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
const gchar	*fu_udev_device_get_sysfs_attr		 (FuUdevDevice	*self,
							  const gchar	*attr,
							  GError	**error);
gchar		*fu_udev_device_get_parent_name		(FuUdevDevice	*self);

gboolean	 fu_udev_device_write_sysfs		(FuUdevDevice	*self,
							 const gchar	*attribute,
							 const gchar	*val,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
const gchar	*fu_udev_device_get_devtype		(FuUdevDevice	*self);
