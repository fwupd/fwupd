/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-device.h"
#include "fu-io-channel.h"

#define FU_TYPE_LINUX_DEVICE (fu_linux_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuLinuxDevice, fu_linux_device, FU, LINUX_DEVICE, FuDevice)

struct _FuLinuxDeviceClass {
	FuDeviceClass parent_class;
};

/**
 * FuLinuxDeviceFlags:
 * @FU_LINUX_DEVICE_FLAG_NONE:			No flags set
 * @FU_LINUX_DEVICE_FLAG_OPEN_READ:		Open the device read-only
 * @FU_LINUX_DEVICE_FLAG_OPEN_WRITE:		Open the device write-only
 * @FU_LINUX_DEVICE_FLAG_OPEN_NONBLOCK:		Open nonblocking, e.g. O_NONBLOCK
 * @FU_LINUX_DEVICE_FLAG_OPEN_SYNC:		Open sync, e.g. O_SYNC
 * @FU_LINUX_DEVICE_FLAG_IOCTL_RETRY:		Retry the ioctl() call when required
 * @FU_LINUX_DEVICE_FLAG_IGNORE_NONE:		The device deliberately has no open flags
 *
 * Flags used when opening the device using fu_device_open().
 **/
typedef enum {
	FU_LINUX_DEVICE_FLAG_NONE = 0,
	FU_LINUX_DEVICE_FLAG_OPEN_READ = 1 << 0,
	FU_LINUX_DEVICE_FLAG_OPEN_WRITE = 1 << 1,
	FU_LINUX_DEVICE_FLAG_OPEN_NONBLOCK = 1 << 4,
	FU_LINUX_DEVICE_FLAG_OPEN_SYNC = 1 << 5,
	FU_LINUX_DEVICE_FLAG_IOCTL_RETRY = 1 << 6,
	FU_LINUX_DEVICE_FLAG_IGNORE_NONE = 1 << 7,
	/*< private >*/
	FU_LINUX_DEVICE_FLAG_LAST
} FuLinuxDeviceFlags;

/**
 * FuLinuxDevicePciBaseClass:
 * @FU_LINUX_DEVICE_PCI_BASE_CLASS_OLD: Device from before classes were defined
 * @FU_LINUX_DEVICE_PCI_BASE_CLASS_MASS_STORAGE: Mass Storage Controller
 * @FU_LINUX_DEVICE_PCI_BASE_CLASS_NETWORK: Network controller
 * @FU_LINUX_DEVICE_PCI_BASE_CLASS_DISPLAY: Display controller
 * @FU_LINUX_DEVICE_PCI_BASE_CLASS_MULTIMEDIA: Multimedia controller
 * @FU_LINUX_DEVICE_PCI_BASE_CLASS_MEMORY: Memory controller
 * @FU_LINUX_DEVICE_PCI_BASE_CLASS_BRIDGE: Bridge
 * @FU_LINUX_DEVICE_PCI_BASE_CLASS_SIMPLE_COMMUNICATION: Simple communications controller
 * @FU_LINUX_DEVICE_PCI_BASE_CLASS_BASE: Base system peripheral
 * @FU_LINUX_DEVICE_PCI_BASE_CLASS_INPUT: Input device
 * @FU_LINUX_DEVICE_PCI_BASE_CLASS_DOCKING: Docking station
 * @FU_LINUX_DEVICE_PCI_BASE_CLASS_PROCESSORS: Processor
 * @FU_LINUX_DEVICE_PCI_BASE_CLASS_SERIAL_BUS: Serial bus controller
 * @FU_LINUX_DEVICE_PCI_BASE_CLASS_WIRELESS: Wireless controller
 * @FU_LINUX_DEVICE_PCI_BASE_CLASS_INTELLIGENT_IO: Intelligent IO controller
 * @FU_LINUX_DEVICE_PCI_BASE_CLASS_SATELLITE: Satellite controller
 * @FU_LINUX_DEVICE_PCI_BASE_CLASS_ENCRYPTION: Encryption/Decryption controller
 * @FU_LINUX_DEVICE_PCI_BASE_CLASS_SIGNAL_PROCESSING: Data acquisition and signal processing
 *controller
 * @FU_LINUX_DEVICE_PCI_BASE_CLASS_ACCELERATOR: Processing accelerator
 * @FU_LINUX_DEVICE_PCI_BASE_CLASS_NON_ESSENTIAL: Non-essential instrumentation
 * @FU_LINUX_DEVICE_PCI_BASE_CLASS_UNDEFINED: Device doesn't fit any defined class
 *
 * PCI base class types returned by fu_linux_device_get_pci_class().
 **/
typedef enum {
	FU_LINUX_DEVICE_PCI_BASE_CLASS_OLD,
	FU_LINUX_DEVICE_PCI_BASE_CLASS_MASS_STORAGE,
	FU_LINUX_DEVICE_PCI_BASE_CLASS_NETWORK,
	FU_LINUX_DEVICE_PCI_BASE_CLASS_DISPLAY,
	FU_LINUX_DEVICE_PCI_BASE_CLASS_MULTIMEDIA,
	FU_LINUX_DEVICE_PCI_BASE_CLASS_MEMORY,
	FU_LINUX_DEVICE_PCI_BASE_CLASS_BRIDGE,
	FU_LINUX_DEVICE_PCI_BASE_CLASS_SIMPLE_COMMUNICATION,
	FU_LINUX_DEVICE_PCI_BASE_CLASS_BASE,
	FU_LINUX_DEVICE_PCI_BASE_CLASS_INPUT,
	FU_LINUX_DEVICE_PCI_BASE_CLASS_DOCKING,
	FU_LINUX_DEVICE_PCI_BASE_CLASS_PROCESSORS,
	FU_LINUX_DEVICE_PCI_BASE_CLASS_SERIAL_BUS,
	FU_LINUX_DEVICE_PCI_BASE_CLASS_WIRELESS,
	FU_LINUX_DEVICE_PCI_BASE_CLASS_INTELLIGENT_IO,
	FU_LINUX_DEVICE_PCI_BASE_CLASS_SATELLITE,
	FU_LINUX_DEVICE_PCI_BASE_CLASS_ENCRYPTION,
	FU_LINUX_DEVICE_PCI_BASE_CLASS_SIGNAL_PROCESSING,
	FU_LINUX_DEVICE_PCI_BASE_CLASS_ACCELERATOR,
	FU_LINUX_DEVICE_PCI_BASE_CLASS_NON_ESSENTIAL,
	FU_LINUX_DEVICE_PCI_BASE_CLASS_UNDEFINED = 0xff
} FuLinuxDevicePciBaseClass;

const gchar *
fu_linux_device_get_sysfs_path(FuLinuxDevice *self) G_GNUC_NON_NULL(1);
void
fu_linux_device_set_sysfs_path(FuLinuxDevice *self, const gchar *sysfs_path) G_GNUC_NON_NULL(1);
const gchar *
fu_linux_device_get_device_file(FuLinuxDevice *self) G_GNUC_NON_NULL(1);
void
fu_linux_device_set_device_file(FuLinuxDevice *self, const gchar *device_file) G_GNUC_NON_NULL(1);
const gchar *
fu_linux_device_get_devtype(FuLinuxDevice *self) G_GNUC_NON_NULL(1);
void
fu_linux_device_set_devtype(FuLinuxDevice *self, const gchar *devtype) G_GNUC_NON_NULL(1);
const gchar *
fu_linux_device_get_subsystem(FuLinuxDevice *self) G_GNUC_NON_NULL(1);
void
fu_linux_device_set_subsystem(FuLinuxDevice *self, const gchar *subsystem) G_GNUC_NON_NULL(1);
const gchar *
fu_linux_device_get_bind_id(FuLinuxDevice *self) G_GNUC_NON_NULL(1);
void
fu_linux_device_set_bind_id(FuLinuxDevice *self, const gchar *bind_id) G_GNUC_NON_NULL(1);
const gchar *
fu_linux_device_get_driver(FuLinuxDevice *self) G_GNUC_NON_NULL(1);
void
fu_linux_device_set_driver(FuLinuxDevice *self, const gchar *driver) G_GNUC_NON_NULL(1);

gboolean
fu_linux_device_is_pci_base_cls(FuLinuxDevice *self, FuLinuxDevicePciBaseClass pci_base_class)
    G_GNUC_NON_NULL(1);

guint32
fu_linux_device_get_pci_class(FuLinuxDevice *self) G_GNUC_NON_NULL(1);
void
fu_linux_device_set_pci_class(FuLinuxDevice *self, guint32 pci_class) G_GNUC_NON_NULL(1);
guint16
fu_linux_device_get_vendor(FuLinuxDevice *self) G_GNUC_NON_NULL(1);
void
fu_linux_device_set_vendor(FuLinuxDevice *self, guint16 vendor) G_GNUC_NON_NULL(1);
guint16
fu_linux_device_get_model(FuLinuxDevice *self) G_GNUC_NON_NULL(1);
void
fu_linux_device_set_model(FuLinuxDevice *self, guint16 model) G_GNUC_NON_NULL(1);
guint16
fu_linux_device_get_subsystem_vendor(FuLinuxDevice *self) G_GNUC_NON_NULL(1);
void
fu_linux_device_set_subsystem_vendor(FuLinuxDevice *self, guint16 subsystem_vendor)
    G_GNUC_NON_NULL(1);
guint16
fu_linux_device_get_subsystem_model(FuLinuxDevice *self) G_GNUC_NON_NULL(1);
void
fu_linux_device_set_subsystem_model(FuLinuxDevice *self, guint16 subsystem_model)
    G_GNUC_NON_NULL(1);
guint8
fu_linux_device_get_revision(FuLinuxDevice *self) G_GNUC_NON_NULL(1);
void
fu_linux_device_set_revision(FuLinuxDevice *self, guint8 revision) G_GNUC_NON_NULL(1);
guint64
fu_linux_device_get_number(FuLinuxDevice *self) G_GNUC_NON_NULL(1);
void
fu_linux_device_set_number(FuLinuxDevice *self, guint64 number) G_GNUC_NON_NULL(1);

void
fu_linux_device_add_flag(FuLinuxDevice *self, FuLinuxDeviceFlags flag) G_GNUC_NON_NULL(1);
void
fu_linux_device_remove_flag(FuLinuxDevice *self, FuLinuxDeviceFlags flag) G_GNUC_NON_NULL(1);

FuIOChannel *
fu_linux_device_get_io_channel(FuLinuxDevice *self) G_GNUC_NON_NULL(1);

gchar *
fu_linux_device_read_attr(FuLinuxDevice *self,
			  const gchar *attribute,
			  GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_linux_device_write_attr(FuLinuxDevice *self,
			   const gchar *attribute,
			   const gchar *val,
			   GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2, 3);
gchar *
fu_linux_device_read_prop(FuLinuxDevice *self,
			  const gchar *key,
			  GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);

FuLinuxDevice *
fu_linux_device_get_parent_with_subsystem(FuLinuxDevice *self,
					  const gchar *subsystem,
					  GError **error) G_GNUC_NON_NULL(1);

gboolean
fu_linux_device_set_physical_id(FuLinuxDevice *self,
				const gchar *subsystems,
				GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_linux_device_set_logical_id(FuLinuxDevice *self,
			       const gchar *subsystem,
			       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);

gboolean
fu_linux_device_ioctl(FuLinuxDevice *self,
		      gulong request,
		      guint8 *buf,
		      gsize bufsz,
		      gint *rc,
		      guint timeout,
		      GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_linux_device_pwrite(FuLinuxDevice *self,
		       goffset port,
		       const guint8 *buf,
		       gsize bufsz,
		       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_linux_device_pread(FuLinuxDevice *self, goffset port, guint8 *buf, gsize bufsz, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_linux_device_seek(FuLinuxDevice *self, goffset offset, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
