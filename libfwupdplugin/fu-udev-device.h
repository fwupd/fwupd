/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#ifdef HAVE_GUDEV
#include <gudev/gudev.h>
#else
#define G_UDEV_TYPE_DEVICE G_TYPE_OBJECT
#define GUdevDevice	   GObject
#endif

#ifdef HAVE_GUSB
#include <gusb.h>
#else
#define GUsbContext GObject
#define GUsbDevice  GObject
#endif

#include "fu-plugin.h"

#define FU_TYPE_UDEV_DEVICE (fu_udev_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuUdevDevice, fu_udev_device, FU, UDEV_DEVICE, FuDevice)

struct _FuUdevDeviceClass {
	FuDeviceClass parent_class;
};

/**
 * FuUdevDeviceFlags:
 * @FU_UDEV_DEVICE_FLAG_NONE:			No flags set
 * @FU_UDEV_DEVICE_FLAG_OPEN_READ:		Open the device read-only
 * @FU_UDEV_DEVICE_FLAG_OPEN_WRITE:		Open the device write-only
 * @FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT:	Get the vendor ID from a parent or grandparent
 * @FU_UDEV_DEVICE_FLAG_USE_CONFIG:		Read and write from the device config
 * @FU_UDEV_DEVICE_FLAG_OPEN_NONBLOCK:		Open nonblocking, e.g. O_NONBLOCK
 * @FU_UDEV_DEVICE_FLAG_OPEN_SYNC:		Open sync, e.g. O_SYNC
 * @FU_UDEV_DEVICE_FLAG_IOCTL_RETRY:		Retry the ioctl() call when required
 *
 * Flags used when opening the device using fu_device_open().
 **/
typedef enum {
	FU_UDEV_DEVICE_FLAG_NONE = 0,
	FU_UDEV_DEVICE_FLAG_OPEN_READ = 1 << 0,
	FU_UDEV_DEVICE_FLAG_OPEN_WRITE = 1 << 1,
	FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT = 1 << 2,
	FU_UDEV_DEVICE_FLAG_USE_CONFIG = 1 << 3,
	FU_UDEV_DEVICE_FLAG_OPEN_NONBLOCK = 1 << 4,
	FU_UDEV_DEVICE_FLAG_OPEN_SYNC = 1 << 5,
	FU_UDEV_DEVICE_FLAG_IOCTL_RETRY = 1 << 6,
	/*< private >*/
	FU_UDEV_DEVICE_FLAG_LAST
} FuUdevDeviceFlags;

/**
 * FuPciBaseCls:
 * @FU_PCI_BASE_CLS_OLD: Device from before classes were defined
 * @FU_PCI_BASE_CLS_MASS_STORAGE: Mass Storage Controller
 * @FU_PCI_BASE_CLS_NETWORK: Network controller
 * @FU_PCI_BASE_CLS_DISPLAY: Display controller
 * @FU_PCI_BASE_CLS_MULTIMEDIA: Multimedia controller
 * @FU_PCI_BASE_CLS_MEMORY: Memory controller
 * @FU_PCI_BASE_CLS_BRIDGE: Bridge
 * @FU_PCI_BASE_CLS_SIMPLE_COMMUNICATION: Simple communications controller
 * @FU_PCI_BASE_CLS_BASE: Base system peripheral
 * @FU_PCI_BASE_CLS_INPUT: Input device
 * @FU_PCI_BASE_CLS_DOCKING: Docking station
 * @FU_PCI_BASE_CLS_PROCESSORS: Processor
 * @FU_PCI_BASE_CLS_SERIAL_BUS: Serial bus controller
 * @FU_PCI_BASE_CLS_WIRELESS: Wireless controller
 * @FU_PCI_BASE_CLS_INTELLIGENT_IO: Intelligent IO controller
 * @FU_PCI_BASE_CLS_SATELLITE: Satellite controller
 * @FU_PCI_BASE_CLS_ENCRYPTION: Encryption/Decryption controller
 * @FU_PCI_BASE_CLS_SIGNAL_PROCESSING: Data acquisition and signal processing controller
 * @FU_PCI_BASE_CLS_ACCELERATOR: Processing accelerator
 * @FU_PCI_BASE_CLS_NON_ESSENTIAL: Non-essential instrumentation
 * @FU_PCI_BASE_CLS_UNDEFINED: Device doesn't fit any defined class
 *
 * PCI base class types returned by fu_udev_device_get_cls().
 **/
typedef enum {
	FU_PCI_BASE_CLS_OLD,
	FU_PCI_BASE_CLS_MASS_STORAGE,
	FU_PCI_BASE_CLS_NETWORK,
	FU_PCI_BASE_CLS_DISPLAY,
	FU_PCI_BASE_CLS_MULTIMEDIA,
	FU_PCI_BASE_CLS_MEMORY,
	FU_PCI_BASE_CLS_BRIDGE,
	FU_PCI_BASE_CLS_SIMPLE_COMMUNICATION,
	FU_PCI_BASE_CLS_BASE,
	FU_PCI_BASE_CLS_INPUT,
	FU_PCI_BASE_CLS_DOCKING,
	FU_PCI_BASE_CLS_PROCESSORS,
	FU_PCI_BASE_CLS_SERIAL_BUS,
	FU_PCI_BASE_CLS_WIRELESS,
	FU_PCI_BASE_CLS_INTELLIGENT_IO,
	FU_PCI_BASE_CLS_SATELLITE,
	FU_PCI_BASE_CLS_ENCRYPTION,
	FU_PCI_BASE_CLS_SIGNAL_PROCESSING,
	FU_PCI_BASE_CLS_ACCELERATOR,
	FU_PCI_BASE_CLS_NON_ESSENTIAL,
	FU_PCI_BASE_CLS_UNDEFINED = 0xff
} FuPciBaseCls;

FuUdevDevice *
fu_udev_device_new(FuContext *ctx, GUdevDevice *udev_device);
GUdevDevice *
fu_udev_device_get_dev(FuUdevDevice *self);
void
fu_udev_device_set_dev(FuUdevDevice *self, GUdevDevice *udev_device);
const gchar *
fu_udev_device_get_device_file(FuUdevDevice *self);
void
fu_udev_device_set_device_file(FuUdevDevice *self, const gchar *device_file);
const gchar *
fu_udev_device_get_sysfs_path(FuUdevDevice *self);
const gchar *
fu_udev_device_get_subsystem(FuUdevDevice *self);
const gchar *
fu_udev_device_get_bind_id(FuUdevDevice *self);
void
fu_udev_device_set_bind_id(FuUdevDevice *self, const gchar *bind_id);
const gchar *
fu_udev_device_get_driver(FuUdevDevice *self);
gboolean
fu_udev_device_is_pci_base_cls(FuUdevDevice *self, FuPciBaseCls cls);
guint32
fu_udev_device_get_cls(FuUdevDevice *self);
guint16
fu_udev_device_get_vendor(FuUdevDevice *self);
guint16
fu_udev_device_get_model(FuUdevDevice *self);
guint16
fu_udev_device_get_subsystem_vendor(FuUdevDevice *self);
guint16
fu_udev_device_get_subsystem_model(FuUdevDevice *self);
guint8
fu_udev_device_get_revision(FuUdevDevice *self);
guint64
fu_udev_device_get_number(FuUdevDevice *self);
guint
fu_udev_device_get_slot_depth(FuUdevDevice *self, const gchar *subsystem);
gboolean
fu_udev_device_set_physical_id(FuUdevDevice *self,
			       const gchar *subsystems,
			       GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_udev_device_set_logical_id(FuUdevDevice *self,
			      const gchar *subsystem,
			      GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fu_udev_device_set_flags(FuUdevDevice *self, FuUdevDeviceFlags flags);

gint
fu_udev_device_get_fd(FuUdevDevice *self);
void
fu_udev_device_set_fd(FuUdevDevice *self, gint fd);
gboolean
fu_udev_device_ioctl(FuUdevDevice *self,
		     gulong request,
		     guint8 *buf,
		     gint *rc,
		     guint timeout,
		     GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_udev_device_pwrite(FuUdevDevice *self,
		      goffset port,
		      const guint8 *buf,
		      gsize bufsz,
		      GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_udev_device_pread(FuUdevDevice *self, goffset port, guint8 *buf, gsize bufsz, GError **error)
    G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_udev_device_seek(FuUdevDevice *self, goffset offset, GError **error) G_GNUC_WARN_UNUSED_RESULT;
const gchar *
fu_udev_device_get_sysfs_attr(FuUdevDevice *self, const gchar *attr, GError **error);
gboolean
fu_udev_device_get_sysfs_attr_uint64(FuUdevDevice *self,
				     const gchar *attr,
				     guint64 *value,
				     GError **error);
gchar *
fu_udev_device_get_parent_name(FuUdevDevice *self);

gboolean
fu_udev_device_write_sysfs(FuUdevDevice *self,
			   const gchar *attribute,
			   const gchar *val,
			   GError **error) G_GNUC_WARN_UNUSED_RESULT;
const gchar *
fu_udev_device_get_devtype(FuUdevDevice *self);
GPtrArray *
fu_udev_device_get_siblings_with_subsystem(FuUdevDevice *self, const gchar *subsystem);
GPtrArray *
fu_udev_device_get_children_with_subsystem(FuUdevDevice *self, const gchar *subsystem);
FuUdevDevice *
fu_udev_device_get_parent_with_subsystem(FuUdevDevice *self, const gchar *subsystem);

GUsbDevice *
fu_udev_device_find_usb_device(FuUdevDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
