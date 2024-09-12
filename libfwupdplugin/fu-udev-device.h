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
 * @FU_UDEV_DEVICE_IOCTL_FLAG_RETRY:		Retry the ioctl() call on failure
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

/**
 * FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT:
 *
 * The default IO timeout when reading sysfs attributes.
 */
#define FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT 50 /* ms */

FuUdevDevice *
fu_udev_device_new(FuContext *ctx, GUdevDevice *udev_device) G_GNUC_NON_NULL(1, 2);
GUdevDevice *
fu_udev_device_get_dev(FuUdevDevice *self) G_GNUC_NON_NULL(1);
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
gboolean
fu_udev_device_is_pci_base_cls(FuUdevDevice *self, FuPciBaseCls cls) G_GNUC_NON_NULL(1);
guint32
fu_udev_device_get_cls(FuUdevDevice *self) G_GNUC_NON_NULL(1);
guint16
fu_udev_device_get_vendor(FuUdevDevice *self) G_GNUC_NON_NULL(1);
void
fu_udev_device_set_vendor(FuUdevDevice *self, guint16 vendor);
guint16
fu_udev_device_get_model(FuUdevDevice *self) G_GNUC_NON_NULL(1);
void
fu_udev_device_set_model(FuUdevDevice *self, guint16 model);
guint16
fu_udev_device_get_subsystem_vendor(FuUdevDevice *self) G_GNUC_NON_NULL(1);
guint16
fu_udev_device_get_subsystem_model(FuUdevDevice *self) G_GNUC_NON_NULL(1);
guint8
fu_udev_device_get_revision(FuUdevDevice *self) G_GNUC_NON_NULL(1);
void
fu_udev_device_set_revision(FuUdevDevice *self, guint8 revision);
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
