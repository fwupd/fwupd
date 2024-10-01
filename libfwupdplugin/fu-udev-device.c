/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuUdevDevice"

#include "config.h"

#include <fcntl.h>
#include <string.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_IOCTL_H
#include <sys/ioctl.h>
#endif
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "fu-device-event-private.h"
#include "fu-device-private.h"
#include "fu-i2c-device.h"
#include "fu-path.h"
#include "fu-string.h"
#include "fu-udev-device-private.h"

/**
 * FuUdevDevice:
 *
 * A UDev device, typically only available on Linux.
 *
 * See also: [class@FuDevice]
 */

typedef struct {
	gchar *subsystem;
	gchar *bind_id;
	gchar *driver;
	gchar *device_file;
	gchar *devtype;
	guint64 number;
	FuIOChannel *io_channel;
	FuIoChannelOpenFlag open_flags;
	GHashTable *properties;
	gboolean properties_valid;
} FuUdevDevicePrivate;

static void
fu_udev_device_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FuUdevDevice,
		       fu_udev_device,
		       FU_TYPE_DEVICE,
		       0,
		       G_ADD_PRIVATE(FuUdevDevice)
			   G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC,
						 fu_udev_device_codec_iface_init));

enum {
	PROP_0,
	PROP_SUBSYSTEM,
	PROP_DRIVER,
	PROP_DEVICE_FILE,
	PROP_BIND_ID,
	PROP_DEVTYPE,
	PROP_LAST
};

enum { SIGNAL_CHANGED, SIGNAL_LAST };

static guint signals[SIGNAL_LAST] = {0};

#define GET_PRIVATE(o) (fu_udev_device_get_instance_private(o))

/**
 * fu_udev_device_emit_changed:
 * @self: a #FuUdevDevice
 *
 * Emits the ::changed signal for the object.
 *
 * Since: 1.1.2
 **/
void
fu_udev_device_emit_changed(FuUdevDevice *self)
{
	g_autoptr(GError) error = NULL;
	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	g_debug("FuUdevDevice emit changed");
	if (!fu_device_rescan(FU_DEVICE(self), &error))
		g_debug("%s", error->message);
	g_signal_emit(self, signals[SIGNAL_CHANGED], 0);
}

static void
fu_udev_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *open_flags = fu_io_channel_open_flag_to_string(priv->open_flags);

	fwupd_codec_string_append_hex(str, idt, "Number", priv->number);
	fwupd_codec_string_append(str, idt, "Subsystem", priv->subsystem);
	fwupd_codec_string_append(str, idt, "Driver", priv->driver);
	fwupd_codec_string_append(str, idt, "BindId", priv->bind_id);
	fwupd_codec_string_append(str, idt, "DeviceFile", priv->device_file);
	fwupd_codec_string_append(str, idt, "OpenFlags", open_flags);
}

static gboolean
fu_udev_device_ensure_bind_id(FuUdevDevice *self, GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	/* sanity check */
	if (priv->bind_id != NULL)
		return TRUE;

	/* automatically set the bind ID from the subsystem */
	if (g_strcmp0(priv->subsystem, "pci") == 0) {
		priv->bind_id = fu_udev_device_read_property(self, "PCI_SLOT_NAME", error);
		return priv->bind_id != NULL;
	}
	if (g_strcmp0(priv->subsystem, "hid") == 0) {
		priv->bind_id = fu_udev_device_read_property(self, "HID_PHYS", error);
		return priv->bind_id != NULL;
	}
	if (g_strcmp0(priv->subsystem, "usb") == 0) {
		priv->bind_id = g_path_get_basename(fu_udev_device_get_sysfs_path(self));
		return TRUE;
	}

	/* nothing found automatically */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "cannot derive bind-id from subsystem %s",
		    priv->subsystem);
	return FALSE;
}

/* private */
void
fu_udev_device_set_subsystem(FuUdevDevice *self, const gchar *subsystem)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->subsystem, subsystem) == 0)
		return;

	g_free(priv->subsystem);
	priv->subsystem = g_strdup(subsystem);
	g_object_notify(G_OBJECT(self), "subsystem");
}

/**
 * fu_udev_device_set_bind_id:
 * @self: a #FuUdevDevice
 * @bind_id: a bind-id string, e.g. `pci:0:0:1`
 *
 * Sets the device ID used for binding the device, e.g. `pci:1:2:3`
 *
 * Since: 1.7.2
 **/
void
fu_udev_device_set_bind_id(FuUdevDevice *self, const gchar *bind_id)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->bind_id, bind_id) == 0)
		return;

	g_free(priv->bind_id);
	priv->bind_id = g_strdup(bind_id);
	g_object_notify(G_OBJECT(self), "bind-id");
}

static void
fu_udev_device_set_driver(FuUdevDevice *self, const gchar *driver)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->driver, driver) == 0)
		return;

	g_free(priv->driver);
	priv->driver = g_strdup(driver);
	g_object_notify(G_OBJECT(self), "driver");
}

/**
 * fu_udev_device_set_device_file:
 * @self: a #FuUdevDevice
 * @device_file: (nullable): a device path
 *
 * Sets the device file to use for reading and writing.
 *
 * Since: 1.8.7
 **/
void
fu_udev_device_set_device_file(FuUdevDevice *self, const gchar *device_file)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->device_file, device_file) == 0)
		return;

	g_free(priv->device_file);
	priv->device_file = g_strdup(device_file);
	g_object_notify(G_OBJECT(self), "device-file");
}

static gchar *
fu_udev_device_get_symlink_target(FuUdevDevice *self, const gchar *attr, GError **error)
{
	FuDeviceEvent *event = NULL;
	g_autofree gchar *event_id = NULL;
	g_autofree gchar *fn_attr = NULL;
	g_autofree gchar *symlink_target = NULL;
	g_autofree gchar *value = NULL;

	if (fu_udev_device_get_sysfs_path(self) == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no sysfs path");
		return NULL;
	}

	/* need event ID */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event_id = g_strdup_printf("GetSymlinkTarget:Attr=%s", attr);
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return NULL;
		return g_strdup(fu_device_event_get_str(event, "Data", error));
	}

	/* save */
	if (event_id != NULL)
		event = fu_device_save_event(FU_DEVICE(self), event_id);

	/* find target */
	fn_attr = g_build_filename(fu_udev_device_get_sysfs_path(self), attr, NULL);
	symlink_target = fu_path_get_symlink_target(fn_attr, error);
	if (symlink_target == NULL)
		return NULL;
	value = g_path_get_basename(symlink_target);

	/* save response */
	if (event != NULL)
		fu_device_event_set_str(event, "Data", value);

	/* success */
	return g_steal_pointer(&value);
}

/* private */
void
fu_udev_device_set_number(FuUdevDevice *self, guint64 number)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	priv->number = number;
}

static void
fu_udev_device_set_devtype(FuUdevDevice *self, const gchar *devtype)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->devtype, devtype) == 0)
		return;

	g_free(priv->devtype);
	priv->devtype = g_strdup(devtype);
	g_object_notify(G_OBJECT(self), "devtype");
}

/* private */
gboolean
fu_udev_device_parse_number(FuUdevDevice *self, GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GString) path = g_string_new(fu_udev_device_get_sysfs_path(self));

	if (path->len == 0)
		return TRUE;
	for (guint i = path->len - 1; i > 0; i--) {
		if (!g_ascii_isdigit(path->str[i])) {
			g_string_erase(path, 0, i + 1);
			break;
		}
	}
	if (path->len > 0) {
		if (!fu_strtoull(path->str,
				 &priv->number,
				 0x0,
				 G_MAXUINT64,
				 FU_INTEGER_BASE_AUTO,
				 error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_udev_device_probe(FuDevice *device, GError **error)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *subsystem = NULL;
	g_autofree gchar *attr_device = NULL;
	g_autofree gchar *attr_vendor = NULL;

	/* find the subsystem, driver and devtype */
	if (priv->subsystem == NULL) {
		g_autofree gchar *subsystem_tmp =
		    fu_udev_device_get_symlink_target(self, "subsystem", error);
		if (subsystem_tmp == NULL) {
			g_prefix_error(error, "failed to read subsystem: ");
			return FALSE;
		}
		fu_udev_device_set_subsystem(self, subsystem_tmp);
	}
	if (priv->driver == NULL)
		priv->driver = fu_udev_device_get_symlink_target(self, "driver", NULL);
	if (priv->devtype == NULL)
		priv->devtype = fu_udev_device_read_property(self, "DEVTYPE", NULL);
	if (priv->device_file == NULL) {
		g_autofree gchar *prop_devname =
		    fu_udev_device_read_property(self, "DEVNAME", NULL);
		if (prop_devname != NULL) {
			g_autofree gchar *device_file = g_strdup_printf("/dev/%s", prop_devname);
			fu_udev_device_set_device_file(self, device_file);
		}
	}

	/* get IDs */
	attr_vendor = fu_udev_device_read_sysfs(self,
						"vendor",
						FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						NULL);
	if (attr_vendor != NULL) {
		guint64 tmp64 = 0;
		if (!fu_strtoull(attr_vendor, &tmp64, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, NULL)) {
			fu_device_set_vendor(device, attr_vendor);
		} else {
			fu_device_set_vid(device, (guint16)tmp64);
		}
	}
	attr_device = fu_udev_device_read_sysfs(self,
						"device",
						FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						NULL);
	if (attr_device != NULL) {
		guint64 tmp64 = 0;
		if (!fu_strtoull(attr_device, &tmp64, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		fu_device_set_pid(device, (guint16)tmp64);
	}

	/* set number */
	if (fu_udev_device_get_sysfs_path(self) != NULL) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_udev_device_parse_number(self, &error_local))
			g_debug("failed to convert udev number: %s", error_local->message);
	}

	/* set vendor ID */
	if (priv->subsystem != NULL)
		subsystem = g_ascii_strup(priv->subsystem, -1);
	if (subsystem != NULL)
		fu_device_build_vendor_id_u16(device, subsystem, fu_device_get_vid(device));

	/* add GUIDs in order of priority */
	if (fu_device_get_vid(device) != 0x0000)
		fu_device_add_instance_u16(device, "VEN", fu_device_get_vid(device));
	if (fu_device_get_pid(device) != 0x0000)
		fu_device_add_instance_u16(device, "DEV", fu_device_get_pid(device));
	if (subsystem != NULL) {
		fu_device_build_instance_id_full(device,
						 FU_DEVICE_INSTANCE_FLAG_GENERIC |
						     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						 NULL,
						 subsystem,
						 "VEN",
						 NULL);
		fu_device_build_instance_id_full(device,
						 FU_DEVICE_INSTANCE_FLAG_GENERIC |
						     FU_DEVICE_INSTANCE_FLAG_VISIBLE |
						     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						 NULL,
						 subsystem,
						 "VEN",
						 "DEV",
						 NULL);
	}

	/* add device class */
	if (subsystem != NULL) {
		g_autofree gchar *cls =
		    fu_udev_device_read_sysfs(self,
					      "class",
					      FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					      NULL);
		g_autofree gchar *devtype = fu_udev_device_read_property(self, "DEVTYPE", NULL);
		if (cls != NULL && g_str_has_prefix(cls, "0x"))
			fu_device_add_instance_strup(device, "CLASS", cls + 2);
		else
			fu_device_add_instance_strup(device, "CLASS", cls);
		fu_device_build_instance_id_full(device,
						 FU_DEVICE_INSTANCE_FLAG_GENERIC |
						     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						 NULL,
						 subsystem,
						 "VEN",
						 "CLASS",
						 NULL);

		/* add devtype */
		fu_device_add_instance_strup(device, "TYPE", devtype);
		fu_device_build_instance_id_full(device,
						 FU_DEVICE_INSTANCE_FLAG_GENERIC |
						     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						 NULL,
						 subsystem,
						 "TYPE",
						 NULL);

		/* add the driver */
		fu_device_add_instance_str(device, "DRIVER", priv->driver);
		fu_device_build_instance_id_full(device,
						 FU_DEVICE_INSTANCE_FLAG_GENERIC |
						     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						 NULL,
						 subsystem,
						 "DRIVER",
						 NULL);
	}

	/* determine if we're wired internally */
	if (g_strcmp0(priv->subsystem, "i2c") != 0) {
		g_autoptr(FuDevice) parent_i2c =
		    fu_device_get_backend_parent_with_subsystem(device, "i2c", NULL);
		if (parent_i2c != NULL)
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);
	}

	/* success */
	return TRUE;
}

/**
 * fu_udev_device_get_subsystem_depth:
 * @self: a #FuUdevDevice
 * @subsystem: a subsystem
 *
 * Determine how far up a chain a given device is
 *
 * Returns: unsigned integer
 *
 * Since: 2.0.0
 **/
guint
fu_udev_device_get_subsystem_depth(FuUdevDevice *self, const gchar *subsystem)
{
	g_autoptr(FuDevice) device_tmp = NULL;

	device_tmp = fu_device_get_backend_parent_with_subsystem(FU_DEVICE(self), subsystem, NULL);
	if (device_tmp == NULL)
		return 0;
	if (g_strcmp0(fu_device_get_id(device_tmp), fu_device_get_id(FU_DEVICE(self))) == 0)
		return 0;
	for (guint i = 0;; i++) {
		g_autoptr(FuDevice) parent =
		    fu_device_get_backend_parent(FU_DEVICE(device_tmp), NULL);
		if (parent == NULL)
			return i;
		g_set_object(&device_tmp, parent);
	}
	return 0;
}

static void
fu_udev_device_probe_complete(FuDevice *device)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_hash_table_remove_all(priv->properties);
	priv->properties_valid = FALSE;
}

static gboolean
fu_udev_device_unbind_driver(FuDevice *device, GError **error)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *fn = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GOutputStream) stream = NULL;

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	/* is already unbound */
	if (fu_udev_device_get_sysfs_path(self) == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "not initialized");
		return FALSE;
	}
	fn = g_build_filename(fu_udev_device_get_sysfs_path(self), "driver", "unbind", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS))
		return TRUE;

	/* write bus ID to file */
	if (!fu_udev_device_ensure_bind_id(self, error))
		return FALSE;
	file = g_file_new_for_path(fn);
	stream =
	    G_OUTPUT_STREAM(g_file_replace(file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, error));
	if (stream == NULL)
		return FALSE;
	return g_output_stream_write_all(stream,
					 priv->bind_id,
					 strlen(priv->bind_id),
					 NULL,
					 NULL,
					 error);
}

static gboolean
fu_udev_device_bind_driver(FuDevice *device,
			   const gchar *subsystem,
			   const gchar *driver,
			   GError **error)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *driver_safe = g_strdup(driver);
	g_autofree gchar *fn = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GOutputStream) stream = NULL;

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	/* copy the logic from modprobe */
	g_strdelimit(driver_safe, "-", '_');

	/* driver exists */
	fn = g_strdup_printf("/sys/module/%s/drivers/%s:%s/bind",
			     driver_safe,
			     subsystem,
			     driver_safe);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot bind with %s:%s",
			    subsystem,
			    driver);
		return FALSE;
	}

	/* write bus ID to file */
	if (!fu_udev_device_ensure_bind_id(self, error))
		return FALSE;
	if (priv->bind_id == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "bind-id not set for subsystem %s",
			    priv->subsystem);
		return FALSE;
	}
	file = g_file_new_for_path(fn);
	stream =
	    G_OUTPUT_STREAM(g_file_replace(file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, error));
	if (stream == NULL)
		return FALSE;
	return g_output_stream_write_all(stream,
					 priv->bind_id,
					 strlen(priv->bind_id),
					 NULL,
					 NULL,
					 error);
}

static FuIoChannelOpenFlag
fu_udev_device_get_open_flags(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), 0);
	return priv->open_flags;
}

static void
fu_udev_device_incorporate(FuDevice *self, FuDevice *donor)
{
	FuUdevDevice *uself = FU_UDEV_DEVICE(self);
	FuUdevDevice *udonor = FU_UDEV_DEVICE(donor);
	FuUdevDevicePrivate *priv = GET_PRIVATE(uself);

	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	g_return_if_fail(FU_IS_UDEV_DEVICE(donor));

	if (priv->device_file == NULL)
		fu_udev_device_set_device_file(uself, fu_udev_device_get_device_file(udonor));
	if (priv->subsystem == NULL)
		fu_udev_device_set_subsystem(uself, fu_udev_device_get_subsystem(udonor));
	if (priv->bind_id == NULL)
		fu_udev_device_set_bind_id(uself, fu_udev_device_get_bind_id(udonor));
	if (priv->driver == NULL)
		fu_udev_device_set_driver(uself, fu_udev_device_get_driver(udonor));
	if (priv->devtype == NULL)
		fu_udev_device_set_devtype(uself, fu_udev_device_get_devtype(udonor));
	if (priv->number == 0x0)
		fu_udev_device_set_number(uself, fu_udev_device_get_number(udonor));
	if (priv->open_flags == FU_IO_CHANNEL_OPEN_FLAG_NONE)
		priv->open_flags = fu_udev_device_get_open_flags(udonor);
}

/**
 * fu_udev_device_get_subsystem:
 * @self: a #FuUdevDevice
 *
 * Gets the device subsystem, e.g. `pci`
 *
 * Returns: a subsystem, or NULL if unset or invalid
 *
 * Since: 1.1.2
 **/
const gchar *
fu_udev_device_get_subsystem(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	return priv->subsystem;
}

/**
 * fu_udev_device_get_bind_id:
 * @self: a #FuUdevDevice
 *
 * Gets the device ID used for binding the device, e.g. `pci:1:2:3`
 *
 * Returns: a bind_id, or NULL if unset or invalid
 *
 * Since: 1.7.2
 **/
const gchar *
fu_udev_device_get_bind_id(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	fu_udev_device_ensure_bind_id(self, NULL);
	return priv->bind_id;
}

/**
 * fu_udev_device_get_driver:
 * @self: a #FuUdevDevice
 *
 * Gets the device driver, e.g. `psmouse`.
 *
 * Returns: a subsystem, or NULL if unset or invalid
 *
 * Since: 1.5.3
 **/
const gchar *
fu_udev_device_get_driver(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	return priv->driver;
}

/**
 * fu_udev_device_get_device_file:
 * @self: a #FuUdevDevice
 *
 * Gets the device node.
 *
 * Returns: a device file, or NULL if unset
 *
 * Since: 1.3.1
 **/
const gchar *
fu_udev_device_get_device_file(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	return priv->device_file;
}

/**
 * fu_udev_device_get_sysfs_path:
 * @self: a #FuUdevDevice
 *
 * Gets the device sysfs path, e.g. `/sys/devices/pci0000:00/0000:00:14.0`.
 *
 * Returns: a local path, or NULL if unset or invalid
 *
 * Since: 1.1.2
 **/
const gchar *
fu_udev_device_get_sysfs_path(FuUdevDevice *self)
{
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	return fu_device_get_backend_id(FU_DEVICE(self));
}

/**
 * fu_udev_device_get_number:
 * @self: a #FuUdevDevice
 *
 * Gets the device number, if any.
 *
 * Returns: integer, 0 if the data is unavailable.
 *
 * Since: 1.5.0
 **/
guint64
fu_udev_device_get_number(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), 0);
	return priv->number;
}

static gchar *
fu_udev_device_get_parent_subsystems(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GString) str = g_string_new(NULL);
	g_autoptr(FuUdevDevice) udev_device = g_object_ref(self);

	/* not true, but good enough for emulation */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return g_strdup(priv->subsystem);

	/* find subsystems of self and all parent devices */
	while (TRUE) {
		g_autoptr(FuUdevDevice) parent = NULL;
		if (fu_udev_device_get_devtype(udev_device) != NULL) {
			g_string_append_printf(str,
					       "%s:%s,",
					       fu_udev_device_get_subsystem(udev_device),
					       fu_udev_device_get_devtype(udev_device));
		} else {
			g_string_append_printf(str,
					       "%s,",
					       fu_udev_device_get_subsystem(udev_device));
		}
		parent = FU_UDEV_DEVICE(
		    fu_device_get_backend_parent_with_subsystem(FU_DEVICE(udev_device),
								NULL,
								NULL));
		if (parent == NULL)
			break;
		g_set_object(&udev_device, parent);
	}
	if (str->len > 0)
		g_string_truncate(str, str->len - 1);
	return g_string_free(g_steal_pointer(&str), FALSE);
}

/* private */
gboolean
fu_udev_device_match_subsystem(FuUdevDevice *self, const gchar *subsystem)
{
	g_auto(GStrv) subsys_devtype = NULL;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), FALSE);

	if (subsystem == NULL)
		return TRUE;
	subsys_devtype = g_strsplit(subsystem, ":", 2);
	if (g_strcmp0(fu_udev_device_get_subsystem(self), subsys_devtype[0]) != 0)
		return FALSE;
	if (subsys_devtype[1] != NULL &&
	    g_strcmp0(fu_udev_device_get_devtype(self), subsys_devtype[1]) != 0) {
		return FALSE;
	}
	return TRUE;
}

/* private */
gchar *
fu_udev_device_get_device_file_from_subsystem(FuUdevDevice *self,
					      const gchar *subsystem,
					      GError **error)
{
	const gchar *fn;
	g_autofree gchar *subsystem_dir = NULL;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	g_return_val_if_fail(subsystem != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	subsystem_dir =
	    g_build_filename(fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(self)), subsystem, NULL);
	dir = g_dir_open(subsystem_dir, 0, &error_local);
	if (dir == NULL) {
		if (g_error_matches(error_local, G_FILE_ERROR_NOENT, G_FILE_ERROR_NOENT)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "failed to find subsystem directory %s",
				    subsystem_dir);
			return NULL;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		fu_error_convert(error);
		return NULL;
	}
	fn = g_dir_read_name(dir);
	if (fn == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "failed to find subsystem device in %s",
			    subsystem_dir);
		return NULL;
	}
	return g_strdup_printf("/dev/%s", fn);
}

/**
 * fu_udev_device_set_physical_id:
 * @self: a #FuUdevDevice
 * @subsystems: a subsystem string, e.g. `pci,usb,scsi:scsi_target`
 * @error: (nullable): optional return location for an error
 *
 * Sets the physical ID from the device subsystem. Plugins should choose the
 * subsystem that is "deepest" in the udev tree, for instance choosing `usb`
 * over `pci` for a mouse device.
 *
 * The devtype can also be specified for a specific device, which is useful when the
 * subsystem alone is not enough to identify the physical device. e.g. ignoring the
 * specific LUNs for a SCSI device.
 *
 * Returns: %TRUE if the physical device was set.
 *
 * Since: 1.1.2
 **/
gboolean
fu_udev_device_set_physical_id(FuUdevDevice *self, const gchar *subsystems, GError **error)
{
	const gchar *subsystem = NULL;
	g_autofree gchar *physical_id = NULL;
	g_auto(GStrv) split = NULL;
	g_autoptr(FuUdevDevice) udev_device = NULL;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), FALSE);
	g_return_val_if_fail(subsystems != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* look for each subsystem[:devtype] in turn */
	split = g_strsplit(subsystems, ",", -1);
	for (guint i = 0; split[i] != NULL; i++) {
		g_autoptr(FuUdevDevice) device_parent = NULL;

		/* do we match */
		if (fu_udev_device_match_subsystem(self, split[i])) {
			udev_device = g_object_ref(self);
			break;
		}

		/* does a parent match? */
		device_parent = FU_UDEV_DEVICE(
		    fu_device_get_backend_parent_with_subsystem(FU_DEVICE(self), split[i], NULL));
		if (device_parent != NULL) {
			udev_device = g_object_ref(device_parent);
			break;
		}
	}
	if (udev_device == NULL) {
		g_autofree gchar *str = fu_udev_device_get_parent_subsystems(self);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "failed to find device with subsystems %s, only got %s",
			    subsystems,
			    str);
		return FALSE;
	}

	subsystem = fu_udev_device_get_subsystem(udev_device);
	if (g_strcmp0(subsystem, "pci") == 0) {
		g_autofree gchar *prop_id =
		    fu_udev_device_read_property(udev_device, "PCI_SLOT_NAME", error);
		if (prop_id == NULL)
			return FALSE;
		physical_id = g_strdup_printf("PCI_SLOT_NAME=%s", prop_id);
	} else if (g_strcmp0(subsystem, "usb") == 0 || g_strcmp0(subsystem, "mmc") == 0 ||
		   g_strcmp0(subsystem, "i2c") == 0 || g_strcmp0(subsystem, "platform") == 0 ||
		   g_strcmp0(subsystem, "mtd") == 0 || g_strcmp0(subsystem, "block") == 0 ||
		   g_strcmp0(subsystem, "gpio") == 0 || g_strcmp0(subsystem, "video4linux") == 0) {
		g_auto(GStrv) sysfs_parts =
		    g_strsplit(fu_udev_device_get_sysfs_path(udev_device), "/sys", 2);
		if (sysfs_parts[1] != NULL)
			physical_id = g_strdup_printf("DEVPATH=%s", sysfs_parts[1]);
	} else if (g_strcmp0(subsystem, "hid") == 0) {
		g_autofree gchar *prop_id =
		    fu_udev_device_read_property(udev_device, "HID_PHYS", error);
		if (prop_id == NULL)
			return FALSE;
		physical_id = g_strdup_printf("HID_PHYS=%s", prop_id);
	} else if (g_strcmp0(subsystem, "drm_dp_aux_dev") == 0) {
		g_autofree gchar *prop_id =
		    fu_udev_device_read_property(udev_device, "DEVNAME", error);
		if (prop_id == NULL)
			return FALSE;
		physical_id = g_strdup_printf("DEVNAME=%s", prop_id);
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot handle subsystem %s",
			    subsystem);
		return FALSE;
	}

	/* success */
	fu_device_set_physical_id(FU_DEVICE(self), physical_id);
	return TRUE;
}

/**
 * fu_udev_device_get_io_channel:
 * @self: a #FuUdevDevice
 *
 * Gets the IO channel.
 *
 * Returns: (transfer none): a #FuIOChannel, or %NULL if the device is not open
 *
 * Since: 1.9.8
 **/
FuIOChannel *
fu_udev_device_get_io_channel(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	return priv->io_channel;
}

/**
 * fu_udev_device_set_io_channel:
 * @self: a #FuUdevDevice
 * @io_channel: a #FuIOChannel
 *
 * Replace the IO channel to use when the device has already been opened.
 * This object will automatically unref @io_channel when fu_device_close() is called.
 *
 * Since: 1.9.8
 **/
void
fu_udev_device_set_io_channel(FuUdevDevice *self, FuIOChannel *io_channel)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	g_return_if_fail(FU_IS_IO_CHANNEL(io_channel));
	g_set_object(&priv->io_channel, io_channel);
}

/**
 * fu_udev_device_remove_open_flag:
 * @self: a #FuUdevDevice
 * @flag: udev device flag, e.g. %FU_IO_CHANNEL_OPEN_FLAG_READ
 *
 * Removes a open flag.
 *
 * Since: 2.0.0
 **/
void
fu_udev_device_remove_open_flag(FuUdevDevice *self, FuIoChannelOpenFlag flag)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	priv->open_flags &= ~flag;
}

/**
 * fu_udev_device_add_open_flag:
 * @self: a #FuUdevDevice
 * @flag: udev device flag, e.g. %FU_IO_CHANNEL_OPEN_FLAG_READ
 *
 * Sets the parameters to use when opening the device.
 *
 * For example %FU_IO_CHANNEL_OPEN_FLAG_READ means that fu_device_open()
 * would use `O_RDONLY` rather than `O_RDWR` which is the default.
 *
 * Since: 2.0.0
 **/
void
fu_udev_device_add_open_flag(FuUdevDevice *self, FuIoChannelOpenFlag flag)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_UDEV_DEVICE(self));

	/* already set */
	if (priv->open_flags & flag)
		return;
	priv->open_flags |= flag;
}

static gboolean
fu_udev_device_open(FuDevice *device, GError **error)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	/* old versions of fwupd used to start with OPEN_READ|OPEN_WRITE and then plugins
	 * could add more flags, or set the flags back to NONE -- detect and fixup */
	if (priv->device_file != NULL && priv->open_flags == FU_IO_CHANNEL_OPEN_FLAG_NONE) {
#ifndef SUPPORTED_BUILD
		g_critical(
		    "%s [%s] forgot to call fu_udev_device_add_open_flag() with OPEN_READ and/or "
		    "OPEN_WRITE",
		    fu_device_get_name(device),
		    fu_device_get_id(device));
#endif
		fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
		fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	}

	/* open device */
	if (priv->device_file != NULL) {
		g_autoptr(FuIOChannel) io_channel = NULL;
		io_channel = fu_io_channel_new_file(priv->device_file, priv->open_flags, error);
		if (io_channel == NULL)
			return FALSE;
		g_set_object(&priv->io_channel, io_channel);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_udev_device_rescan(FuDevice *device, GError **error)
{
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;
	fu_device_probe_invalidate(device);
	return fu_device_probe(device, error);
}

static gboolean
fu_udev_device_close(FuDevice *device, GError **error)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	/* optional */
	if (priv->io_channel != NULL) {
		if (!fu_io_channel_shutdown(priv->io_channel, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * fu_udev_device_ioctl:
 * @self: a #FuUdevDevice
 * @request: request number
 * @buf: a buffer to use, which *must* be large enough for the request
 * @bufsz: the size of @buf
 * @rc: (out) (nullable): the raw return value from the ioctl
 * @timeout: timeout in ms for the retry action, see %FU_UDEV_DEVICE_IOCTL_FLAG_RETRY
 * @flags: some #FuUdevDeviceIoctlFlags, e.g. %FU_UDEV_DEVICE_IOCTL_FLAG_RETRY
 * @error: (nullable): optional return location for an error
 *
 * Control a device using a low-level request.
 *
 * NOTE: In version 2.0.0 the @bufsz parameter was added -- which isn't required to perform the
 * ioctl, but *is* required to accurately track and emulate the device buffer.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.0
 **/
gboolean
fu_udev_device_ioctl(FuUdevDevice *self,
		     gulong request,
		     guint8 *buf,
		     gsize bufsz,
		     gint *rc,
		     guint timeout,
		     FuUdevDeviceIoctlFlags flags,
		     GError **error)
{
#ifdef HAVE_IOCTL_H
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	gint rc_tmp;
	g_autoptr(GTimer) timer = g_timer_new();
	FuDeviceEvent *event = NULL;
	g_autofree gchar *event_id = NULL;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), FALSE);
	g_return_val_if_fail(request != 0x0, FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		g_autofree gchar *buf_base64 = g_base64_encode(buf, bufsz);
		event_id = g_strdup_printf("Ioctl:"
					   "Request=0x%04x,"
					   "Data=%s,"
					   "Length=0x%x",
					   (guint)request,
					   buf_base64,
					   (guint)bufsz);
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return FALSE;
		return fu_device_event_copy_data(event, "DataOut", buf, bufsz, NULL, error);
	}

	/* save */
	if (fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event = fu_device_save_event(FU_DEVICE(self), event_id);
		fu_device_event_set_data(event, "Data", buf, bufsz);
	}

	/* not open! */
	if (priv->io_channel == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "%s [%s] has not been opened",
			    fu_device_get_id(FU_DEVICE(self)),
			    fu_device_get_name(FU_DEVICE(self)));
		return FALSE;
	}

	/* poll if required  up to the timeout */
	do {
		rc_tmp = ioctl(fu_io_channel_unix_get_fd(priv->io_channel), /* nocheck:blocked */
			       request,
			       buf);
		if (rc_tmp >= 0)
			break;
	} while ((flags & FU_UDEV_DEVICE_IOCTL_FLAG_RETRY) && (errno == EINTR || errno == EAGAIN) &&
		 g_timer_elapsed(timer, NULL) < timeout * 1000.f);
	if (rc != NULL)
		*rc = rc_tmp;
	if (rc_tmp < 0) {
#ifdef HAVE_ERRNO_H
		if (errno == EPERM) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_PERMISSION_DENIED,
					    "permission denied");
			return FALSE;
		}
		if (errno == ENOTTY) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "permission denied");
			return FALSE;
		}
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "ioctl error: %s [%i]",
			    g_strerror(errno),
			    errno);
#else
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "unspecified ioctl error");
#endif
		return FALSE;
	}

	/* save response */
	if (event != NULL)
		fu_device_event_set_data(event, "DataOut", buf, bufsz);

	/* success */
	return TRUE;
#else
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "Not supported as <sys/ioctl.h> not found");
	return FALSE;
#endif
}

/**
 * fu_udev_device_pread:
 * @self: a #FuUdevDevice
 * @port: offset address
 * @buf: (in): data
 * @bufsz: size of @buf
 * @error: (nullable): optional return location for an error
 *
 * Read a buffer from a file descriptor at a given offset.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.2
 **/
gboolean
fu_udev_device_pread(FuUdevDevice *self, goffset port, guint8 *buf, gsize bufsz, GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	FuDeviceEvent *event = NULL;
	g_autofree gchar *event_id = NULL;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event_id = g_strdup_printf("Pread:"
					   "Port=0x%x,"
					   "Length=0x%x",
					   (guint)port,
					   (guint)bufsz);
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return FALSE;
		return fu_device_event_copy_data(event, "Data", buf, bufsz, NULL, error);
	}

	/* save */
	if (event_id != NULL)
		event = fu_device_save_event(FU_DEVICE(self), event_id);

	/* not open! */
	if (priv->io_channel == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "%s [%s] has not been opened",
			    fu_device_get_id(FU_DEVICE(self)),
			    fu_device_get_name(FU_DEVICE(self)));
		return FALSE;
	}

#ifdef HAVE_PWRITE
	if (pread(fu_io_channel_unix_get_fd(priv->io_channel), buf, bufsz, port) != (gssize)bufsz) {
		g_set_error(error,
			    G_IO_ERROR, /* nocheck:error */
#ifdef HAVE_ERRNO_H
			    g_io_error_from_errno(errno),
#else
			    G_IO_ERROR_FAILED, /* nocheck:error */
#endif
			    "failed to read from port 0x%04x: %s",
			    (guint)port,
			    g_strerror(errno));
		fwupd_error_convert(error);
		return FALSE;
	}

	/* save response */
	if (event != NULL)
		fu_device_event_set_data(event, "Data", buf, bufsz);
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not supported as pread() is unavailable");
	return FALSE;
#endif
}

/**
 * fu_udev_device_seek:
 * @self: a #FuUdevDevice
 * @offset: offset address
 * @error: (nullable): optional return location for an error
 *
 * Seeks a file descriptor to a given offset.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.7.2
 **/
gboolean
fu_udev_device_seek(FuUdevDevice *self, goffset offset, GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	/* not open! */
	if (priv->io_channel == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "%s [%s] has not been opened",
			    fu_device_get_id(FU_DEVICE(self)),
			    fu_device_get_name(FU_DEVICE(self)));
		return FALSE;
	}
	return fu_io_channel_seek(priv->io_channel, offset, error);
}

/**
 * fu_udev_device_pwrite:
 * @self: a #FuUdevDevice
 * @port: offset address
 * @buf: (out): data
 * @bufsz: size of @data
 * @error: (nullable): optional return location for an error
 *
 * Write a buffer to a file descriptor at a given offset.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.2
 **/
gboolean
fu_udev_device_pwrite(FuUdevDevice *self,
		      goffset port,
		      const guint8 *buf,
		      gsize bufsz,
		      GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	/* not open! */
	if (priv->io_channel == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "%s [%s] has not been opened",
			    fu_device_get_id(FU_DEVICE(self)),
			    fu_device_get_name(FU_DEVICE(self)));
		return FALSE;
	}

#ifdef HAVE_PWRITE
	if (pwrite(fu_io_channel_unix_get_fd(priv->io_channel), buf, bufsz, port) !=
	    (gssize)bufsz) {
		g_set_error(error,
			    G_IO_ERROR, /* nocheck:error */
#ifdef HAVE_ERRNO_H
			    g_io_error_from_errno(errno),
#else
			    G_IO_ERROR_FAILED, /* nocheck:blocked */
#endif
			    "failed to write to port %04x: %s",
			    (guint)port,
			    g_strerror(errno));
		fwupd_error_convert(error);
		return FALSE;
	}
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not supported as pwrite() is unavailable");
	return FALSE;
#endif
}

/**
 * fu_udev_device_read_sysfs:
 * @self: a #FuUdevDevice
 * @attr: sysfs attribute name
 * @timeout_ms: IO timeout in milliseconds
 * @error: (nullable): optional return location for an error
 *
 * Reads data from a sysfs attribute, removing any newline trailing chars.
 *
 * Returns: (transfer full): string value, or %NULL
 *
 * Since: 2.0.0
 **/
gchar *
fu_udev_device_read_sysfs(FuUdevDevice *self, const gchar *attr, guint timeout_ms, GError **error)
{
	FuDeviceEvent *event = NULL;
	g_autofree gchar *event_id = NULL;
	g_autofree gchar *path = NULL;
	g_autofree gchar *value = NULL;
	g_autoptr(FuIOChannel) io_channel = NULL;
	g_autoptr(GByteArray) buf = NULL;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	g_return_val_if_fail(attr != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* need event ID */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event_id = g_strdup_printf("ReadAttr:Attr=%s", attr);
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return NULL;
		return g_strdup(fu_device_event_get_str(event, "Data", error));
	}

	/* save */
	if (event_id != NULL)
		event = fu_device_save_event(FU_DEVICE(self), event_id);

	/* open the file */
	if (fu_udev_device_get_sysfs_path(self) == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "sysfs_path undefined");
		return NULL;
	}
	path = g_build_filename(fu_udev_device_get_sysfs_path(self), attr, NULL);
	io_channel = fu_io_channel_new_file(path, FU_IO_CHANNEL_OPEN_FLAG_READ, error);
	if (io_channel == NULL)
		return NULL;
	buf = fu_io_channel_read_byte_array(io_channel,
					    -1,
					    timeout_ms,
					    FU_IO_CHANNEL_FLAG_NONE,
					    error);
	if (buf == NULL)
		return NULL;
	if (!g_utf8_validate((const gchar *)buf->data, buf->len, NULL)) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "non UTF-8 data");
		return NULL;
	}

	/* save response */
	value = g_strndup((const gchar *)buf->data, buf->len);

	/* remove the trailing newline */
	if (buf->len > 0) {
		if (value[buf->len - 1] == '\n')
			value[buf->len - 1] = '\0';
	}

	/* save for emulation */
	if (event != NULL)
		fu_device_event_set_str(event, "Data", value);

	/* success */
	return g_steal_pointer(&value);
}

/**
 * fu_udev_device_write_sysfs:
 * @self: a #FuUdevDevice
 * @attr: sysfs attribute name
 * @val: data to write into the attribute
 * @timeout_ms: IO timeout in milliseconds
 * @error: (nullable): optional return location for an error
 *
 * Writes data into a sysfs attribute
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.0
 **/
gboolean
fu_udev_device_write_sysfs(FuUdevDevice *self,
			   const gchar *attr,
			   const gchar *val,
			   guint timeout_ms,
			   GError **error)
{
	g_autofree gchar *path = NULL;
	g_autoptr(FuIOChannel) io_channel = NULL;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), FALSE);
	g_return_val_if_fail(attr != NULL, FALSE);
	g_return_val_if_fail(val != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	/* open the file */
	if (fu_udev_device_get_sysfs_path(self) == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "sysfs_path undefined");
		return FALSE;
	}
	path = g_build_filename(fu_udev_device_get_sysfs_path(self), attr, NULL);
	io_channel = fu_io_channel_new_file(path, FU_IO_CHANNEL_OPEN_FLAG_WRITE, error);
	if (io_channel == NULL)
		return FALSE;
	return fu_io_channel_write_raw(io_channel,
				       (const guint8 *)val,
				       strlen(val),
				       timeout_ms,
				       FU_IO_CHANNEL_FLAG_NONE,
				       error);
}

/**
 * fu_udev_device_get_devtype:
 * @self: a #FuUdevDevice
 *
 * Returns the Udev device type
 *
 * Returns: device type specified in the uevent
 *
 * Since: 1.4.5
 **/
const gchar *
fu_udev_device_get_devtype(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	return priv->devtype;
}

static GBytes *
fu_udev_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	guint number_reads = 0;
	g_autofree gchar *fn = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* open the file */
	if (priv->device_file == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Unable to read firmware from device");
		return NULL;
	}

	/* open file */
	file = g_file_new_for_path(priv->device_file);
	stream = G_INPUT_STREAM(g_file_read(file, NULL, &error_local));
	if (stream == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_AUTH_FAILED,
				    error_local->message);
		return NULL;
	}

	/* we have to enable the read for devices */
	fn = g_file_get_path(file);
	if (g_str_has_prefix(fn, "/sys")) {
		g_autoptr(GFileOutputStream) output_stream = NULL;
		output_stream = g_file_replace(file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, error);
		if (output_stream == NULL) {
			fu_error_convert(error);
			return NULL;
		}
		if (!g_output_stream_write_all(G_OUTPUT_STREAM(output_stream),
					       "1",
					       1,
					       NULL,
					       NULL,
					       error)) {
			fu_error_convert(error);
			return NULL;
		}
	}

	/* ensure we got enough data to fill the buffer */
	while (TRUE) {
		gssize sz;
		guint8 tmp[32 * 1024] = {0x0};
		sz = g_input_stream_read(stream, tmp, sizeof(tmp), NULL, error);
		if (sz == 0)
			break;
		g_debug("ROM returned 0x%04x bytes", (guint)sz);
		if (sz < 0)
			return NULL;
		g_byte_array_append(buf, tmp, sz);

		/* check the firmware isn't serving us small chunks */
		if (number_reads++ > 1024) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "firmware not fulfilling requests");
			return NULL;
		}
	}
	return g_bytes_new(buf->data, buf->len);
}

/* private */
void
fu_udev_device_add_property(FuUdevDevice *self, const gchar *key, const gchar *value)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	g_return_if_fail(key != NULL);
	g_hash_table_insert(priv->properties, g_strdup(key), g_strdup(value));
}

/**
 * fu_udev_device_read_property:
 * @self: a #FuUdevDevice
 * @key: uevent key name, e.g. `HID_PHYS`
 * @error: (nullable): optional return location for an error
 *
 * Gets a value from the `uevent` file.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.0
 **/
gchar *
fu_udev_device_read_property(FuUdevDevice *self, const gchar *key, GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	FuDeviceEvent *event = NULL;
	g_autofree gchar *event_id = NULL;
	g_autofree gchar *value = NULL;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	g_return_val_if_fail(key != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* need event ID */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event_id = g_strdup_printf("ReadProp:Key=%s", key);
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return NULL;
		return g_strdup(fu_device_event_get_str(event, "Data", error));
	}

	/* save */
	if (event_id != NULL)
		event = fu_device_save_event(FU_DEVICE(self), event_id);

	/* parse key */
	if (!priv->properties_valid) {
		g_autofree gchar *str = NULL;
		g_auto(GStrv) uevent_lines = NULL;
		str = fu_udev_device_read_sysfs(self,
						"uevent",
						FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						error);
		if (str == NULL)
			return NULL;
		uevent_lines = g_strsplit(str, "\n", -1);
		for (guint i = 0; uevent_lines[i] != NULL; i++) {
			g_autofree gchar **kvs = g_strsplit(uevent_lines[i], "=", 2);
			g_hash_table_insert(priv->properties,
					    g_steal_pointer(&kvs[0]),
					    g_steal_pointer(&kvs[1]));
		}
		priv->properties_valid = TRUE;
	}
	value = g_strdup(g_hash_table_lookup(priv->properties, key));
	if (value == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "property key %s was not found",
			    key);
		return NULL;
	}

	/* save response */
	if (event != NULL)
		fu_device_event_set_str(event, "Data", value);

	/* success */
	return g_steal_pointer(&value);
}

static void
fu_udev_device_add_json(FwupdCodec *codec, JsonBuilder *builder, FwupdCodecFlags flags)
{
	FuDevice *device = FU_DEVICE(codec);
	FuUdevDevice *self = FU_UDEV_DEVICE(codec);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	GPtrArray *events = fu_device_get_events(device);

	/* optional properties */
	fwupd_codec_json_append(builder, "GType", "FuUdevDevice");
	if (fu_udev_device_get_sysfs_path(self) != NULL)
		fwupd_codec_json_append(builder, "BackendId", fu_udev_device_get_sysfs_path(self));
	if (priv->device_file != NULL)
		fwupd_codec_json_append(builder, "DeviceFile", priv->device_file);
	if (priv->subsystem != NULL)
		fwupd_codec_json_append(builder, "Subsystem", priv->subsystem);
	if (priv->driver != NULL)
		fwupd_codec_json_append(builder, "Driver", priv->driver);
	if (priv->bind_id != NULL)
		fwupd_codec_json_append(builder, "BindId", priv->bind_id);
	if (fu_device_get_vid(device) != 0)
		fwupd_codec_json_append_int(builder, "Vendor", fu_device_get_vid(device));
	if (fu_device_get_pid(device) != 0)
		fwupd_codec_json_append_int(builder, "Model", fu_device_get_pid(device));

#if GLIB_CHECK_VERSION(2, 80, 0)
	if (fu_device_get_created_usec(device) != 0) {
		g_autoptr(GDateTime) dt =
		    g_date_time_new_from_unix_utc_usec(fu_device_get_created_usec(device));
		g_autofree gchar *str = g_date_time_format_iso8601(dt);
		json_builder_set_member_name(builder, "Created");
		json_builder_add_string_value(builder, str);
	}
#endif

	/* events */
	if (events->len > 0) {
		json_builder_set_member_name(builder, "Events");
		json_builder_begin_array(builder);
		for (guint i = 0; i < events->len; i++) {
			FuDeviceEvent *event = g_ptr_array_index(events, i);
			json_builder_begin_object(builder);
			fwupd_codec_to_json(FWUPD_CODEC(event), builder, flags);
			json_builder_end_object(builder);
		}
		json_builder_end_array(builder);
	}
}

static gboolean
fu_udev_device_from_json(FwupdCodec *codec, JsonNode *json_node, GError **error)
{
	FuDevice *device = FU_DEVICE(codec);
	FuUdevDevice *self = FU_UDEV_DEVICE(codec);
	JsonObject *json_object = json_node_get_object(json_node);
	const gchar *tmp;
	gint64 tmp64;

	tmp = json_object_get_string_member_with_default(json_object, "BackendId", NULL);
	if (tmp != NULL)
		fu_device_set_backend_id(device, tmp);
	tmp = json_object_get_string_member_with_default(json_object, "Subsystem", NULL);
	if (tmp != NULL)
		fu_udev_device_set_subsystem(self, tmp);
	tmp = json_object_get_string_member_with_default(json_object, "Driver", NULL);
	if (tmp != NULL)
		fu_udev_device_set_driver(self, tmp);
	tmp = json_object_get_string_member_with_default(json_object, "BindId", NULL);
	if (tmp != NULL)
		fu_udev_device_set_bind_id(self, tmp);
	tmp = json_object_get_string_member_with_default(json_object, "DeviceFile", NULL);
	if (tmp != NULL)
		fu_udev_device_set_device_file(self, tmp);
	tmp64 = json_object_get_int_member_with_default(json_object, "Vendor", 0);
	if (tmp64 != 0)
		fu_device_set_vid(device, tmp64);
	tmp64 = json_object_get_int_member_with_default(json_object, "Model", 0);
	if (tmp64 != 0)
		fu_device_set_pid(device, tmp64);

#if GLIB_CHECK_VERSION(2, 80, 0)
	tmp = json_object_get_string_member_with_default(json_object, "Created", NULL);
	if (tmp != NULL) {
		g_autoptr(GDateTime) dt = g_date_time_new_from_iso8601(tmp, NULL);
		if (dt != NULL)
			fu_device_set_created_usec(device, g_date_time_to_unix_usec(dt));
	}
#endif

	/* array of events */
	if (json_object_has_member(json_object, "Events")) {
		JsonArray *json_array = json_object_get_array_member(json_object, "Events");
		for (guint i = 0; i < json_array_get_length(json_array); i++) {
			JsonNode *node_tmp = json_array_get_element(json_array, i);
			g_autoptr(FuDeviceEvent) event = fu_device_event_new(NULL);
			if (!fwupd_codec_from_json(FWUPD_CODEC(event), node_tmp, error))
				return FALSE;
			fu_device_add_event(device, event);
		}
	}

	/* success */
	return TRUE;
}

static void
fu_udev_device_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(object);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_SUBSYSTEM:
		g_value_set_string(value, priv->subsystem);
		break;
	case PROP_BIND_ID:
		g_value_set_string(value, priv->bind_id);
		break;
	case PROP_DRIVER:
		g_value_set_string(value, priv->driver);
		break;
	case PROP_DEVICE_FILE:
		g_value_set_string(value, priv->device_file);
		break;
	case PROP_DEVTYPE:
		g_value_set_string(value, priv->devtype);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_udev_device_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(object);
	switch (prop_id) {
	case PROP_SUBSYSTEM:
		fu_udev_device_set_subsystem(self, g_value_get_string(value));
		break;
	case PROP_BIND_ID:
		fu_udev_device_set_bind_id(self, g_value_get_string(value));
		break;
	case PROP_DRIVER:
		fu_udev_device_set_driver(self, g_value_get_string(value));
		break;
	case PROP_DEVICE_FILE:
		fu_udev_device_set_device_file(self, g_value_get_string(value));
		break;
	case PROP_DEVTYPE:
		fu_udev_device_set_devtype(self, g_value_get_string(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_udev_device_finalize(GObject *object)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(object);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	g_hash_table_unref(priv->properties);
	g_free(priv->subsystem);
	g_free(priv->devtype);
	g_free(priv->bind_id);
	g_free(priv->driver);
	g_free(priv->device_file);
	if (priv->io_channel != NULL)
		g_object_unref(priv->io_channel);

	G_OBJECT_CLASS(fu_udev_device_parent_class)->finalize(object);
}

static void
fu_udev_device_init(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	priv->properties = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	fu_device_set_acquiesce_delay(FU_DEVICE(self), 2500);
}

static void
fu_udev_device_class_init(FuUdevDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->finalize = fu_udev_device_finalize;
	object_class->get_property = fu_udev_device_get_property;
	object_class->set_property = fu_udev_device_set_property;
	device_class->probe = fu_udev_device_probe;
	device_class->rescan = fu_udev_device_rescan;
	device_class->incorporate = fu_udev_device_incorporate;
	device_class->open = fu_udev_device_open;
	device_class->close = fu_udev_device_close;
	device_class->to_string = fu_udev_device_to_string;
	device_class->bind_driver = fu_udev_device_bind_driver;
	device_class->unbind_driver = fu_udev_device_unbind_driver;
	device_class->probe_complete = fu_udev_device_probe_complete;
	device_class->dump_firmware = fu_udev_device_dump_firmware;

	/**
	 * FuUdevDevice::changed:
	 * @self: the #FuUdevDevice instance that emitted the signal
	 *
	 * The ::changed signal is emitted when the low-level GUdevDevice has changed.
	 *
	 * Since: 1.1.2
	 **/
	signals[SIGNAL_CHANGED] = g_signal_new("changed",
					       G_TYPE_FROM_CLASS(object_class),
					       G_SIGNAL_RUN_LAST,
					       0,
					       NULL,
					       NULL,
					       g_cclosure_marshal_VOID__VOID,
					       G_TYPE_NONE,
					       0);

	/**
	 * FuUdevDevice:subsystem:
	 *
	 * The device subsystem.
	 *
	 * Since: 1.1.2
	 */
	pspec = g_param_spec_string("subsystem",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_SUBSYSTEM, pspec);

	/**
	 * FuUdevDevice:bind-id:
	 *
	 * The bind ID to use when binding a new driver.
	 *
	 * Since: 1.7.2
	 */
	pspec = g_param_spec_string("bind-id",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_BIND_ID, pspec);

	/**
	 * FuUdevDevice:driver:
	 *
	 * The driver being used for the device.
	 *
	 * Since: 1.5.3
	 */
	pspec = g_param_spec_string("driver",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_DRIVER, pspec);

	/**
	 * FuUdevDevice:device-file:
	 *
	 * The low level file to use for device access.
	 *
	 * Since: 1.3.1
	 */
	pspec = g_param_spec_string("device-file",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_DEVICE_FILE, pspec);

	/**
	 * FuUdevDevice:devtype:
	 *
	 * The device type.
	 *
	 * Since: 2.0.0
	 */
	pspec = g_param_spec_string("devtype",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_DEVTYPE, pspec);
}

static void
fu_udev_device_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_json = fu_udev_device_add_json;
	iface->from_json = fu_udev_device_from_json;
}

/**
 * fu_udev_device_new:
 * @ctx: (nullable): a #FuContext
 * @sysfs_path: a sysfs path
 *
 * Creates a new #FuUdevDevice.
 *
 * Returns: (transfer full): a #FuUdevDevice
 *
 * Since: 2.0.0
 **/
FuUdevDevice *
fu_udev_device_new(FuContext *ctx, const gchar *sysfs_path)
{
	return g_object_new(FU_TYPE_UDEV_DEVICE, "context", ctx, "backend-id", sysfs_path, NULL);
}
