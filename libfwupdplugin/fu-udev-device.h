/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-device.h"
#include "fu-io-channel.h"

#define FU_TYPE_UDEV_DEVICE (fu_udev_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuUdevDevice, fu_udev_device, FU, UDEV_DEVICE, FuDevice)

struct _FuUdevDeviceClass {
	FuDeviceClass parent_class;
};

/**
 * FuUdevDeviceIoctlFlags:
 * @FU_UDEV_DEVICE_IOCTL_FLAG:			No flags set
 * @FU_UDEV_DEVICE_IOCTL_FLAG_RETRY:		Retry the call on failure
 *
 * Flags used when calling fu_udev_device_ioctl().
 **/
typedef enum {
	FU_UDEV_DEVICE_IOCTL_FLAG_NONE = 0,
	FU_UDEV_DEVICE_IOCTL_FLAG_RETRY = 1 << 0,
	/*< private >*/
	FU_UDEV_DEVICE_IOCTL_FLAG_LAST
} FuUdevDeviceIoctlFlags;

/**
 * FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT:
 *
 * The default IO timeout when reading sysfs attributes.
 */
#define FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT 50 /* ms */

const gchar *
fu_udev_device_get_device_file(FuUdevDevice *self) G_GNUC_NON_NULL(1);
void
fu_udev_device_set_device_file(FuUdevDevice *self, const gchar *device_file) G_GNUC_NON_NULL(1);
const gchar *
fu_udev_device_get_sysfs_path(FuUdevDevice *self) G_GNUC_NON_NULL(1);
const gchar *
fu_udev_device_get_subsystem(FuUdevDevice *self) G_GNUC_NON_NULL(1);
const gchar *
fu_udev_device_get_bind_id(FuUdevDevice *self) G_GNUC_NON_NULL(1);
void
fu_udev_device_set_bind_id(FuUdevDevice *self, const gchar *bind_id) G_GNUC_NON_NULL(1);
const gchar *
fu_udev_device_get_driver(FuUdevDevice *self) G_GNUC_NON_NULL(1);
guint64
fu_udev_device_get_number(FuUdevDevice *self) G_GNUC_NON_NULL(1);
guint
fu_udev_device_get_subsystem_depth(FuUdevDevice *self, const gchar *subsystem) G_GNUC_NON_NULL(1);
gboolean
fu_udev_device_set_physical_id(FuUdevDevice *self,
			       const gchar *subsystems,
			       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
void
fu_udev_device_add_open_flag(FuUdevDevice *self, FuIoChannelOpenFlag flag) G_GNUC_NON_NULL(1);
void
fu_udev_device_remove_open_flag(FuUdevDevice *self, FuIoChannelOpenFlag flag) G_GNUC_NON_NULL(1);

FuIOChannel *
fu_udev_device_get_io_channel(FuUdevDevice *self) G_GNUC_NON_NULL(1);
gboolean
fu_udev_device_ioctl(FuUdevDevice *self,
		     gulong request,
		     guint8 *buf,
		     gsize bufsz,
		     gint *rc,
		     guint timeout,
		     FuUdevDeviceIoctlFlags flags,
		     GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_udev_device_pwrite(FuUdevDevice *self,
		      goffset port,
		      const guint8 *buf,
		      gsize bufsz,
		      GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_udev_device_pread(FuUdevDevice *self, goffset port, guint8 *buf, gsize bufsz, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_udev_device_seek(FuUdevDevice *self, goffset offset, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
gchar *
fu_udev_device_read_property(FuUdevDevice *self,
			     const gchar *key,
			     GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gchar *
fu_udev_device_read_sysfs(FuUdevDevice *self, const gchar *attr, guint timeout_ms, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);

gboolean
fu_udev_device_write_sysfs(FuUdevDevice *self,
			   const gchar *attr,
			   const gchar *val,
			   guint timeout_ms,
			   GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2, 3);
const gchar *
fu_udev_device_get_devtype(FuUdevDevice *self) G_GNUC_NON_NULL(1);
