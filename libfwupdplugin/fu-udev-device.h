/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#ifdef HAVE_GUDEV
#include <gudev/gudev.h>
#else
#define G_UDEV_TYPE_DEVICE G_TYPE_OBJECT
#define GUdevDevice	   GObject
#endif

#include "fu-linux-device.h"
#include "fu-plugin.h"

#define FU_TYPE_UDEV_DEVICE (fu_udev_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuUdevDevice, fu_udev_device, FU, UDEV_DEVICE, FuLinuxDevice)

struct _FuUdevDeviceClass {
	FuLinuxDeviceClass parent_class;
};

/**
 * FuUdevDeviceFlags:
 * @FU_UDEV_DEVICE_FLAG_NONE:			No flags set
 * @FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT:	Get the vendor ID from a parent or grandparent
 * @FU_UDEV_DEVICE_FLAG_USE_CONFIG:		Read and write from the device config
 *
 * Flags used when opening the device using fu_device_open().
 **/
typedef enum {
	FU_UDEV_DEVICE_FLAG_NONE = 0,
	FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT = 1 << 2,
	FU_UDEV_DEVICE_FLAG_USE_CONFIG = 1 << 3,
	/*< private >*/
	FU_UDEV_DEVICE_FLAG_LAST
} FuUdevDeviceFlags;

FuUdevDevice *
fu_udev_device_new(FuContext *ctx, GUdevDevice *udev_device) G_GNUC_NON_NULL(1, 2);
GUdevDevice *
fu_udev_device_get_dev(FuUdevDevice *self) G_GNUC_NON_NULL(1);
void
fu_udev_device_set_dev(FuUdevDevice *self, GUdevDevice *udev_device) G_GNUC_NON_NULL(1);
guint
fu_udev_device_get_slot_depth(FuUdevDevice *self, const gchar *subsystem) G_GNUC_NON_NULL(1);
gboolean
fu_udev_device_set_physical_id(FuUdevDevice *self,
			       const gchar *subsystems,
			       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_udev_device_set_logical_id(FuUdevDevice *self,
			      const gchar *subsystem,
			      GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
void
fu_udev_device_add_flag(FuUdevDevice *self, FuUdevDeviceFlags flag) G_GNUC_NON_NULL(1);
void
fu_udev_device_remove_flag(FuUdevDevice *self, FuUdevDeviceFlags flag) G_GNUC_NON_NULL(1);

const gchar *
fu_udev_device_get_sysfs_attr(FuUdevDevice *self, const gchar *attr, GError **error)
    G_GNUC_NON_NULL(1);
gboolean
fu_udev_device_get_sysfs_attr_uint64(FuUdevDevice *self,
				     const gchar *attr,
				     guint64 *value,
				     GError **error) G_GNUC_NON_NULL(1);
gchar *
fu_udev_device_get_parent_name(FuUdevDevice *self) G_GNUC_NON_NULL(1);

GPtrArray *
fu_udev_device_get_siblings_with_subsystem(FuUdevDevice *self,
					   const gchar *subsystem,
					   GError **error) G_GNUC_NON_NULL(1, 2);
GPtrArray *
fu_udev_device_get_children_with_subsystem(FuUdevDevice *self, const gchar *subsystem)
    G_GNUC_NON_NULL(1, 2);
FuUdevDevice *
fu_udev_device_get_parent_with_subsystem(FuUdevDevice *self, const gchar *subsystem, GError **error)
    G_GNUC_NON_NULL(1);

FuDevice *
fu_udev_device_find_usb_device(FuUdevDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
