/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
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

#include "fu-device-private.h"
#include "fu-i2c-device.h"
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
	GUdevDevice *udev_device;
	guint16 vendor;
	guint16 model;
	guint16 subsystem_vendor;
	guint16 subsystem_model;
	guint8 revision;
	gchar *subsystem;
	gchar *bind_id;
	gchar *driver;
	gchar *device_file;
	gint fd;
	FuUdevDeviceFlags flags;
} FuUdevDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuUdevDevice, fu_udev_device, FU_TYPE_DEVICE)

enum {
	PROP_0,
	PROP_UDEV_DEVICE,
	PROP_SUBSYSTEM,
	PROP_DRIVER,
	PROP_DEVICE_FILE,
	PROP_BIND_ID,
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

#ifdef HAVE_GUDEV
static guint16
fu_udev_device_get_sysfs_attr_as_uint16(GUdevDevice *udev_device, const gchar *name)
{
	const gchar *tmp;
	guint64 tmp64 = 0;
	g_autoptr(GError) error_local = NULL;

	tmp = g_udev_device_get_sysfs_attr(udev_device, name);
	if (tmp == NULL)
		return 0x0;
	if (!fu_strtoull(tmp, &tmp64, 0, G_MAXUINT16 - 1, &error_local)) {
		g_warning("reading %s for %s was invalid: %s", name, tmp, error_local->message);
		return 0x0;
	}
	return tmp64;
}

static guint8
fu_udev_device_get_sysfs_attr_as_uint8(GUdevDevice *udev_device, const gchar *name)
{
	const gchar *tmp;
	guint64 tmp64 = 0;
	g_autoptr(GError) error_local = NULL;

	tmp = g_udev_device_get_sysfs_attr(udev_device, name);
	if (tmp == NULL)
		return 0x0;
	if (!fu_strtoull(tmp, &tmp64, 0, G_MAXUINT8 - 1, &error_local)) {
		g_warning("reading %s for %s was invalid: %s",
			  name,
			  g_udev_device_get_sysfs_path(udev_device),
			  error_local->message);
		return 0x0;
	}
	return tmp64;
}

static void
fu_udev_device_to_string_raw(GUdevDevice *udev_device, guint idt, GString *str)
{
	const gchar *const *keys;
	if (udev_device == NULL)
		return;
	keys = g_udev_device_get_property_keys(udev_device);
	for (guint i = 0; keys[i] != NULL; i++) {
		fu_string_append(str,
				 idt,
				 keys[i],
				 g_udev_device_get_property(udev_device, keys[i]));
	}
	keys = g_udev_device_get_sysfs_attr_keys(udev_device);
	for (guint i = 0; keys[i] != NULL; i++) {
		fu_string_append(str,
				 idt,
				 keys[i],
				 g_udev_device_get_sysfs_attr(udev_device, keys[i]));
	}
}
#endif

static void
fu_udev_device_to_string(FuDevice *device, guint idt, GString *str)
{
#ifdef HAVE_GUDEV
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	if (priv->vendor != 0x0)
		fu_string_append_kx(str, idt, "Vendor", priv->vendor);
	if (priv->model != 0x0)
		fu_string_append_kx(str, idt, "Model", priv->model);
	if (priv->subsystem_vendor != 0x0 || priv->subsystem_model != 0x0) {
		fu_string_append_kx(str, idt, "SubsystemVendor", priv->subsystem_vendor);
		fu_string_append_kx(str, idt, "SubsystemModel", priv->subsystem_model);
	}
	if (priv->revision != 0x0)
		fu_string_append_kx(str, idt, "Revision", priv->revision);
	if (priv->subsystem != NULL)
		fu_string_append(str, idt, "Subsystem", priv->subsystem);
	if (priv->driver != NULL)
		fu_string_append(str, idt, "Driver", priv->driver);
	if (priv->bind_id != NULL)
		fu_string_append(str, idt, "BindId", priv->bind_id);
	if (priv->device_file != NULL)
		fu_string_append(str, idt, "DeviceFile", priv->device_file);
	if (priv->udev_device != NULL) {
		fu_string_append(str,
				 idt,
				 "SysfsPath",
				 g_udev_device_get_sysfs_path(priv->udev_device));
	}
	if (g_getenv("FU_UDEV_DEVICE_DEBUG") != NULL) {
		g_autoptr(GUdevDevice) udev_parent = NULL;
		fu_udev_device_to_string_raw(priv->udev_device, idt, str);
		udev_parent = g_udev_device_get_parent(priv->udev_device);
		if (udev_parent != NULL) {
			fu_string_append(str, idt, "Parent", NULL);
			fu_udev_device_to_string_raw(udev_parent, idt + 1, str);
		}
	}
#endif
}

static gboolean
fu_udev_device_ensure_bind_id(FuUdevDevice *self, GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	/* sanity check */
	if (priv->bind_id != NULL)
		return TRUE;

#ifdef HAVE_GUDEV
	/* automatically set the bind ID from the subsystem */
	if (g_strcmp0(priv->subsystem, "pci") == 0) {
		priv->bind_id =
		    g_strdup(g_udev_device_get_property(priv->udev_device, "PCI_SLOT_NAME"));
		return TRUE;
	}
	if (g_strcmp0(priv->subsystem, "hid") == 0) {
		priv->bind_id = g_strdup(g_udev_device_get_property(priv->udev_device, "HID_PHYS"));
		return TRUE;
	}
	if (g_strcmp0(priv->subsystem, "usb") == 0) {
		priv->bind_id =
		    g_path_get_basename(g_udev_device_get_sysfs_path(priv->udev_device));
		return TRUE;
	}
#endif

	/* nothing found automatically */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "cannot derive bind-id from subsystem %s",
		    priv->subsystem);
	return FALSE;
}

static void
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

static void
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

#ifdef HAVE_GUDEV
static const gchar *
fu_udev_device_get_vendor_fallback(GUdevDevice *udev_device)
{
	const gchar *tmp;
	tmp = g_udev_device_get_property(udev_device, "ID_VENDOR_FROM_DATABASE");
	if (tmp != NULL)
		return tmp;
	tmp = g_udev_device_get_property(udev_device, "ID_VENDOR");
	if (tmp != NULL)
		return tmp;
	return NULL;
}
#endif

#ifdef HAVE_GUDEV
static gboolean
fu_udev_device_probe_serio(FuUdevDevice *self, GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;

	/* firmware ID */
	tmp = g_udev_device_get_property(priv->udev_device, "SERIO_FIRMWARE_ID");
	if (tmp != NULL) {
		/* this prefix is not useful */
		if (g_str_has_prefix(tmp, "PNP: "))
			tmp += 5;
		fu_device_add_instance_strsafe(FU_DEVICE(self), "FWID", tmp);
		if (!fu_device_build_instance_id(FU_DEVICE(self), error, "SERIO", "FWID", NULL))
			return FALSE;
	}
	return TRUE;
}

static void
fu_udev_device_set_vendor_from_udev_device(FuUdevDevice *self, GUdevDevice *udev_device)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	priv->vendor = fu_udev_device_get_sysfs_attr_as_uint16(udev_device, "vendor");
	priv->model = fu_udev_device_get_sysfs_attr_as_uint16(udev_device, "device");
	priv->revision = fu_udev_device_get_sysfs_attr_as_uint8(udev_device, "revision");
	priv->subsystem_vendor =
	    fu_udev_device_get_sysfs_attr_as_uint16(udev_device, "subsystem_vendor");
	priv->subsystem_model =
	    fu_udev_device_get_sysfs_attr_as_uint16(udev_device, "subsystem_device");
}

static void
fu_udev_device_set_vendor_from_parent(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GUdevDevice) udev_device = g_object_ref(priv->udev_device);
	while (TRUE) {
		g_autoptr(GUdevDevice) parent = g_udev_device_get_parent(udev_device);
		if (parent == NULL)
			break;
		fu_udev_device_set_vendor_from_udev_device(self, parent);
		if (priv->vendor != 0x0 || priv->model != 0x0 || priv->revision != 0x0)
			break;
		g_set_object(&udev_device, parent);
	}
}
#endif

static gboolean
fu_udev_device_probe(FuDevice *device, GError **error)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
#ifdef HAVE_GUDEV
	const gchar *tmp;
	g_autofree gchar *subsystem = NULL;
	g_autoptr(GUdevDevice) udev_parent = NULL;
	g_autoptr(GUdevDevice) parent_i2c = NULL;
#endif

	/* nothing to do */
	if (priv->udev_device == NULL)
		return TRUE;

#ifdef HAVE_GUDEV
	/* get IDs, but fallback to the parent, grandparent, great-grandparent, etc */
	fu_udev_device_set_vendor_from_udev_device(self, priv->udev_device);
	udev_parent = g_udev_device_get_parent(priv->udev_device);
	if (udev_parent != NULL && priv->flags & FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT)
		fu_udev_device_set_vendor_from_parent(self);

	/* hidraw helpfully encodes the information in a different place */
	if (udev_parent != NULL && priv->vendor == 0x0 && priv->model == 0x0 &&
	    priv->revision == 0x0 && g_strcmp0(priv->subsystem, "hidraw") == 0) {
		tmp = g_udev_device_get_property(udev_parent, "HID_ID");
		if (tmp != NULL) {
			g_auto(GStrv) split = g_strsplit(tmp, ":", -1);
			if (g_strv_length(split) == 3) {
				guint64 val = g_ascii_strtoull(split[1], NULL, 16);
				if (val > G_MAXUINT16) {
					g_warning("reading %s for %s overflowed",
						  split[1],
						  g_udev_device_get_sysfs_path(priv->udev_device));
				} else {
					priv->vendor = val;
				}
				val = g_ascii_strtoull(split[2], NULL, 16);
				if (val > G_MAXUINT32) {
					g_warning("reading %s for %s overflowed",
						  split[2],
						  g_udev_device_get_sysfs_path(priv->udev_device));
				} else {
					priv->model = val;
				}
			}
		}
		tmp = g_udev_device_get_property(udev_parent, "HID_NAME");
		if (tmp != NULL) {
			if (fu_device_get_name(device) == NULL)
				fu_device_set_name(device, tmp);
		}
	}

	/* set the version if the revision has been set */
	if (fu_device_get_version(device) == NULL &&
	    fu_device_get_version_format(device) == FWUPD_VERSION_FORMAT_UNKNOWN) {
		if (priv->revision != 0x00 && priv->revision != 0xFF) {
			g_autofree gchar *version = g_strdup_printf("%02x", priv->revision);
			fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PLAIN);
			fu_device_set_version(device, version);
		}
	}

	/* set model */
	if (fu_device_get_name(device) == NULL) {
		tmp = g_udev_device_get_property(priv->udev_device, "ID_MODEL_FROM_DATABASE");
		if (tmp == NULL)
			tmp = g_udev_device_get_property(priv->udev_device, "ID_MODEL");
		if (tmp == NULL)
			tmp = g_udev_device_get_property(priv->udev_device,
							 "ID_PCI_CLASS_FROM_DATABASE");
		if (tmp != NULL)
			fu_device_set_name(device, tmp);
	}

	/* set vendor */
	if (fu_device_get_vendor(device) == NULL) {
		tmp = fu_udev_device_get_vendor_fallback(priv->udev_device);
		if (tmp != NULL)
			fu_device_set_vendor(device, tmp);
	}

	/* try harder to find a vendor name the user will recognize */
	if (priv->flags & FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT && udev_parent != NULL &&
	    fu_device_get_vendor(device) == NULL) {
		g_autoptr(GUdevDevice) device_tmp = g_object_ref(udev_parent);
		for (guint i = 0; i < 0xff; i++) {
			g_autoptr(GUdevDevice) parent = NULL;
			tmp = fu_udev_device_get_vendor_fallback(device_tmp);
			if (tmp != NULL) {
				fu_device_set_vendor(device, tmp);
				break;
			}
			parent = g_udev_device_get_parent(device_tmp);
			if (parent == NULL)
				break;
			g_set_object(&device_tmp, parent);
		}
	}

	/* set serial */
	if (!fu_device_has_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_NO_SERIAL_NUMBER) &&
	    fu_device_get_serial(device) == NULL) {
		tmp = g_udev_device_get_property(priv->udev_device, "ID_SERIAL_SHORT");
		if (tmp == NULL)
			tmp = g_udev_device_get_property(priv->udev_device, "ID_SERIAL");
		if (tmp != NULL)
			fu_device_set_serial(device, tmp);
	}

	/* set revision */
	if (fu_device_get_version(device) == NULL &&
	    fu_device_get_version_format(device) == FWUPD_VERSION_FORMAT_UNKNOWN) {
		tmp = g_udev_device_get_property(priv->udev_device, "ID_REVISION");
		if (tmp != NULL)
			fu_device_set_version(device, tmp);
	}

	/* set vendor ID */
	subsystem = g_ascii_strup(g_udev_device_get_subsystem(priv->udev_device), -1);
	if (subsystem != NULL && priv->vendor != 0x0000) {
		g_autofree gchar *vendor_id = NULL;
		vendor_id = g_strdup_printf("%s:0x%04X", subsystem, (guint)priv->vendor);
		fu_device_add_vendor_id(device, vendor_id);
	}

	/* add GUIDs in order of priority */
	if (priv->vendor != 0x0000)
		fu_device_add_instance_u16(device, "VEN", priv->vendor);
	if (priv->model != 0x0000)
		fu_device_add_instance_u16(device, "DEV", priv->model);
	if (priv->subsystem_vendor != 0x0000 || priv->subsystem_model != 0x0000) {
		g_autofree gchar *subsys =
		    g_strdup_printf("%04X%04X", priv->subsystem_vendor, priv->subsystem_model);
		fu_device_add_instance_str(device, "SUBSYS", subsys);
	}
	if (priv->revision != 0xFF)
		fu_device_add_instance_u8(device, "REV", priv->revision);

	fu_device_build_instance_id_quirk(device, NULL, subsystem, "VEN", NULL);
	fu_device_build_instance_id(device, NULL, subsystem, "VEN", "DEV", NULL);
	fu_device_build_instance_id(device, NULL, subsystem, "VEN", "DEV", "REV", NULL);
	fu_device_build_instance_id(device, NULL, subsystem, "VEN", "DEV", "SUBSYS", NULL);
	fu_device_build_instance_id(device, NULL, subsystem, "VEN", "DEV", "SUBSYS", "REV", NULL);

	/* add device class */
	tmp = g_udev_device_get_sysfs_attr(priv->udev_device, "class");
	if (tmp != NULL && g_str_has_prefix(tmp, "0x"))
		tmp += 2;
	fu_device_add_instance_strup(device, "CLASS", tmp);
	fu_device_build_instance_id_quirk(device, NULL, subsystem, "VEN", "CLASS", NULL);

	/* add devtype */
	fu_device_add_instance_strup(device, "TYPE", g_udev_device_get_devtype(priv->udev_device));
	fu_device_build_instance_id_quirk(device, NULL, subsystem, "TYPE", NULL);

	/* add the driver */
	fu_device_add_instance_str(device, "DRIVER", priv->driver);
	fu_device_build_instance_id_quirk(device, NULL, subsystem, "DRIVER", NULL);

	/* add subsystem to match in plugins */
	if (subsystem != NULL) {
		fu_device_add_instance_id_full(device,
					       subsystem,
					       FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS);
	}

	/* add firmware_id */
	if (g_strcmp0(g_udev_device_get_subsystem(priv->udev_device), "serio") == 0) {
		if (!fu_udev_device_probe_serio(self, error))
			return FALSE;
	}

	/* determine if we're wired internally */
	parent_i2c = g_udev_device_get_parent_with_subsystem(priv->udev_device, "i2c", NULL);
	if (parent_i2c != NULL)
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);
#endif

	/* success */
	return TRUE;
}

#ifdef HAVE_GUDEV
static gchar *
fu_udev_device_get_miscdev0(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *fn;
	g_autofree gchar *miscdir = NULL;
	g_autoptr(GDir) dir = NULL;

	miscdir = g_build_filename(g_udev_device_get_sysfs_path(priv->udev_device), "misc", NULL);
	dir = g_dir_open(miscdir, 0, NULL);
	if (dir == NULL)
		return NULL;
	fn = g_dir_read_name(dir);
	if (fn == NULL)
		return NULL;
	return g_strdup_printf("/dev/%s", fn);
}
#endif

/**
 * fu_udev_device_set_dev:
 * @self: a #FuUdevDevice
 * @udev_device: a #GUdevDevice
 *
 * Sets the #GUdevDevice. This may need to be used to replace the actual device
 * used for reads and writes before the device is probed.
 *
 * Since: 1.6.2
 **/
void
fu_udev_device_set_dev(FuUdevDevice *self, GUdevDevice *udev_device)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
#ifdef HAVE_GUDEV
	const gchar *summary;
#endif

	g_return_if_fail(FU_IS_UDEV_DEVICE(self));

#ifdef HAVE_GUDEV
	/* the net subsystem is not a real hardware class */
	if (udev_device != NULL &&
	    g_strcmp0(g_udev_device_get_subsystem(udev_device), "net") == 0) {
		g_autoptr(GUdevDevice) udev_device_phys = NULL;
		udev_device_phys = g_udev_device_get_parent(udev_device);
		g_set_object(&priv->udev_device, udev_device_phys);
		fu_device_set_metadata(FU_DEVICE(self),
				       "ParentSubsystem",
				       g_udev_device_get_subsystem(udev_device));
	} else {
		g_set_object(&priv->udev_device, udev_device);
	}
#else
	g_set_object(&priv->udev_device, udev_device);
#endif

	/* set new device */
	if (priv->udev_device == NULL)
		return;
#ifdef HAVE_GUDEV
	fu_udev_device_set_subsystem(self, g_udev_device_get_subsystem(priv->udev_device));
	fu_udev_device_set_driver(self, g_udev_device_get_driver(priv->udev_device));
	fu_udev_device_set_device_file(self, g_udev_device_get_device_file(priv->udev_device));

	/* so we can display something sensible for unclaimed devices */
	fu_device_set_backend_id(FU_DEVICE(self), g_udev_device_get_sysfs_path(priv->udev_device));

	/* fall back to the first thing handled by misc drivers */
	if (priv->device_file == NULL) {
		/* perhaps we should unconditionally fall back? or perhaps
		 * require FU_UDEV_DEVICE_FLAG_FALLBACK_MISC... */
		if (g_strcmp0(priv->subsystem, "serio") == 0)
			priv->device_file = fu_udev_device_get_miscdev0(self);
		if (priv->device_file != NULL)
			g_debug("falling back to misc %s", priv->device_file);
	}

	/* try to get one line summary */
	summary = g_udev_device_get_sysfs_attr(priv->udev_device, "description");
	if (summary == NULL) {
		g_autoptr(GUdevDevice) parent = NULL;
		parent = g_udev_device_get_parent(priv->udev_device);
		if (parent != NULL)
			summary = g_udev_device_get_sysfs_attr(parent, "description");
	}
	if (summary != NULL)
		fu_device_set_summary(FU_DEVICE(self), summary);
#endif
}

/**
 * fu_udev_device_get_slot_depth:
 * @self: a #FuUdevDevice
 * @subsystem: a subsystem
 *
 * Determine how far up a chain a given device is
 *
 * Returns: unsigned integer
 *
 * Since: 1.2.4
 **/
guint
fu_udev_device_get_slot_depth(FuUdevDevice *self, const gchar *subsystem)
{
#ifdef HAVE_GUDEV
	GUdevDevice *udev_device = fu_udev_device_get_dev(FU_UDEV_DEVICE(self));
	g_autoptr(GUdevDevice) device_tmp = NULL;

	device_tmp = g_udev_device_get_parent_with_subsystem(udev_device, subsystem, NULL);
	if (device_tmp == NULL)
		return 0;
	for (guint i = 0; i < 0xff; i++) {
		g_autoptr(GUdevDevice) parent = g_udev_device_get_parent(device_tmp);
		if (parent == NULL)
			return i;
		g_set_object(&device_tmp, parent);
	}
#endif
	return 0;
}

static gboolean
fu_udev_device_unbind_driver(FuDevice *device, GError **error)
{
#ifdef HAVE_GUDEV
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *fn = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GOutputStream) stream = NULL;

	/* is already unbound */
	fn = g_build_filename(g_udev_device_get_sysfs_path(priv->udev_device),
			      "driver",
			      "unbind",
			      NULL);
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
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "driver unbinding not supported");
	return FALSE;
#endif
}

static gboolean
fu_udev_device_bind_driver(FuDevice *device,
			   const gchar *subsystem,
			   const gchar *driver,
			   GError **error)
{
#ifdef HAVE_GUDEV
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *driver_safe = g_strdup(driver);
	g_autofree gchar *fn = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GOutputStream) stream = NULL;

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
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "driver binding not supported on Windows");
	return FALSE;
#endif
}

static void
fu_udev_device_incorporate(FuDevice *self, FuDevice *donor)
{
	FuUdevDevice *uself = FU_UDEV_DEVICE(self);
	FuUdevDevice *udonor = FU_UDEV_DEVICE(donor);
	FuUdevDevicePrivate *priv = GET_PRIVATE(uself);
	FuUdevDevicePrivate *priv_donor = GET_PRIVATE(udonor);

	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	g_return_if_fail(FU_IS_UDEV_DEVICE(donor));

	fu_udev_device_set_dev(uself, fu_udev_device_get_dev(udonor));
	if (priv->device_file == NULL) {
		fu_udev_device_set_subsystem(uself, fu_udev_device_get_subsystem(udonor));
		fu_udev_device_set_bind_id(uself, fu_udev_device_get_bind_id(udonor));
		fu_udev_device_set_device_file(uself, fu_udev_device_get_device_file(udonor));
		fu_udev_device_set_driver(uself, fu_udev_device_get_driver(udonor));
	}
	if (priv->vendor == 0x0 && priv_donor->vendor != 0x0)
		priv->vendor = priv_donor->vendor;
	if (priv->model == 0x0 && priv_donor->model != 0x0)
		priv->model = priv_donor->model;
	if (priv->subsystem_vendor == 0x0 && priv_donor->subsystem_vendor != 0x0)
		priv->subsystem_vendor = priv_donor->subsystem_vendor;
	if (priv->subsystem_model == 0x0 && priv_donor->subsystem_model != 0x0)
		priv->subsystem_model = priv_donor->subsystem_model;
	if (priv->revision == 0x0 && priv_donor->revision != 0x0)
		priv->revision = priv_donor->revision;
}

/**
 * fu_udev_device_get_dev:
 * @self: a #FuUdevDevice
 *
 * Gets the #GUdevDevice.
 *
 * Returns: (transfer none): a #GUdevDevice, or %NULL
 *
 * Since: 1.1.2
 **/
GUdevDevice *
fu_udev_device_get_dev(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	return priv->udev_device;
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
#ifdef HAVE_GUDEV
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	if (priv->udev_device != NULL)
		return g_udev_device_get_sysfs_path(priv->udev_device);
#endif
	return NULL;
}

/**
 * fu_udev_device_get_number:
 * @self: a #FuUdevDevice
 *
 * Gets the device number, if any.
 *
 * Returns: integer, 0 if the data is unavailable, or %G_MAXUINT64 if the
 * feature is not available
 *
 * Since: 1.5.0
 **/
guint64
fu_udev_device_get_number(FuUdevDevice *self)
{
#ifdef HAVE_GUDEV
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), 0);
	if (priv->udev_device != NULL) {
		guint64 tmp = 0;
		g_autoptr(GError) error_local = NULL;
		if (!fu_strtoull(g_udev_device_get_number(priv->udev_device),
				 &tmp,
				 0x0,
				 G_MAXUINT64,
				 &error_local)) {
			g_warning("failed to convert udev number: %s", error_local->message);
			return G_MAXUINT64;
		}
		return tmp;
	}
#endif
	return G_MAXUINT64;
}

/**
 * fu_udev_device_get_vendor:
 * @self: a #FuUdevDevice
 *
 * Gets the device vendor code.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 1.1.2
 **/
guint16
fu_udev_device_get_vendor(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), 0x0000);
	return priv->vendor;
}

/**
 * fu_udev_device_get_model:
 * @self: a #FuUdevDevice
 *
 * Gets the device device code.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 1.1.2
 **/
guint16
fu_udev_device_get_model(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), 0x0000);
	return priv->model;
}

/**
 * fu_udev_device_get_subsystem_vendor:
 * @self: a #FuUdevDevice
 *
 * Gets the device subsystem vendor code.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 1.5.0
 **/
guint16
fu_udev_device_get_subsystem_vendor(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), 0x0000);
	return priv->subsystem_vendor;
}

/**
 * fu_udev_device_get_subsystem_model:
 * @self: a #FuUdevDevice
 *
 * Gets the device subsystem model code.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 1.5.0
 **/
guint16
fu_udev_device_get_subsystem_model(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), 0x0000);
	return priv->subsystem_model;
}

/**
 * fu_udev_device_get_revision:
 * @self: a #FuUdevDevice
 *
 * Gets the device revision.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 1.1.2
 **/
guint8
fu_udev_device_get_revision(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), 0x00);
	return priv->revision;
}

#ifdef HAVE_GUDEV
static gchar *
fu_udev_device_get_parent_subsystems(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	GString *str = g_string_new(NULL);
	g_autoptr(GUdevDevice) udev_device = g_object_ref(priv->udev_device);

	/* find subsystems of self and all parent devices */
	if (priv->subsystem != NULL)
		g_string_append_printf(str, "%s,", priv->subsystem);
	while (TRUE) {
		g_autoptr(GUdevDevice) parent = g_udev_device_get_parent(udev_device);
		if (parent == NULL)
			break;
		if (g_udev_device_get_subsystem(parent) != NULL) {
			g_string_append_printf(str, "%s,", g_udev_device_get_subsystem(parent));
		}
		g_set_object(&udev_device, g_steal_pointer(&parent));
	}
	if (str->len > 0)
		g_string_truncate(str, str->len - 1);
	return g_string_free(str, FALSE);
}

static gboolean
fu_udev_device_match_subsystem_devtype(GUdevDevice *udev_device,
				       const gchar *subsystem,
				       const gchar *devtype)
{
	if (subsystem != NULL) {
		if (g_strcmp0(g_udev_device_get_subsystem(udev_device), subsystem) != 0)
			return FALSE;
	}
	if (devtype != NULL) {
		if (g_strcmp0(g_udev_device_get_devtype(udev_device), devtype) != 0)
			return FALSE;
	}
	return TRUE;
}

static GUdevDevice *
fu_udev_device_get_parent_with_subsystem_devtype(GUdevDevice *udev_device,
						 const gchar *subsystem,
						 const gchar *devtype)
{
	g_autoptr(GUdevDevice) udev_device_tmp = g_object_ref(udev_device);
	while (udev_device_tmp != NULL) {
		g_autoptr(GUdevDevice) parent = NULL;
		if (fu_udev_device_match_subsystem_devtype(udev_device_tmp, subsystem, devtype))
			return g_object_ref(udev_device_tmp);
		parent = g_udev_device_get_parent(udev_device_tmp);
		g_set_object(&udev_device_tmp, parent);
	}
	return NULL;
}
#endif

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
#ifdef HAVE_GUDEV
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;
	g_autofree gchar *physical_id = NULL;
	g_autofree gchar *subsystem = NULL;
	g_auto(GStrv) split = NULL;
	g_autoptr(GUdevDevice) udev_device = NULL;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), FALSE);
	g_return_val_if_fail(subsystems != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* nothing to do */
	if (priv->udev_device == NULL)
		return TRUE;

	/* look for each subsystem[:devtype] in turn */
	split = g_strsplit(subsystems, ",", -1);
	for (guint i = 0; split[i] != NULL; i++) {
		g_auto(GStrv) subsys_devtype = g_strsplit(split[i], ":", 2);

		/* matching on devtype is optional */
		udev_device = fu_udev_device_get_parent_with_subsystem_devtype(priv->udev_device,
									       subsys_devtype[0],
									       subsys_devtype[1]);
		if (udev_device != NULL) {
			subsystem = g_strdup(subsys_devtype[0]);
			break;
		}
	}
	if (udev_device == NULL) {
		g_autofree gchar *str = fu_udev_device_get_parent_subsystems(self);
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_FOUND,
			    "failed to find device with subsystems %s, only got %s",
			    subsystems,
			    str);
		return FALSE;
	}

	if (g_strcmp0(subsystem, "pci") == 0) {
		tmp = g_udev_device_get_property(udev_device, "PCI_SLOT_NAME");
		if (tmp == NULL) {
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_NOT_FOUND,
					    "failed to find PCI_SLOT_NAME");
			return FALSE;
		}
		physical_id = g_strdup_printf("PCI_SLOT_NAME=%s", tmp);
	} else if (g_strcmp0(subsystem, "usb") == 0 || g_strcmp0(subsystem, "mmc") == 0 ||
		   g_strcmp0(subsystem, "i2c") == 0 || g_strcmp0(subsystem, "platform") == 0 ||
		   g_strcmp0(subsystem, "scsi") == 0 || g_strcmp0(subsystem, "mtd") == 0 ||
		   g_strcmp0(subsystem, "block") == 0 || g_strcmp0(subsystem, "gpio") == 0) {
		tmp = g_udev_device_get_property(udev_device, "DEVPATH");
		if (tmp == NULL) {
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_NOT_FOUND,
					    "failed to find DEVPATH");
			return FALSE;
		}
		physical_id = g_strdup_printf("DEVPATH=%s", tmp);
	} else if (g_strcmp0(subsystem, "hid") == 0) {
		tmp = g_udev_device_get_property(udev_device, "HID_PHYS");
		if (tmp == NULL) {
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_NOT_FOUND,
					    "failed to find HID_PHYS");
			return FALSE;
		}
		physical_id = g_strdup_printf("HID_PHYS=%s", tmp);
	} else if (g_strcmp0(subsystem, "tpm") == 0 ||
		   g_strcmp0(subsystem, "drm_dp_aux_dev") == 0) {
		tmp = g_udev_device_get_property(udev_device, "DEVNAME");
		if (tmp == NULL) {
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_NOT_FOUND,
					    "failed to find DEVPATH");
			return FALSE;
		}
		physical_id = g_strdup_printf("DEVNAME=%s", tmp);
	} else {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "cannot handle subsystem %s",
			    subsystem);
		return FALSE;
	}

	/* success */
	fu_device_set_physical_id(FU_DEVICE(self), physical_id);
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not supported as <gudev.h> is unavailable");
	return FALSE;
#endif
}

/**
 * fu_udev_device_set_logical_id:
 * @self: a #FuUdevDevice
 * @subsystem: a subsystem string, e.g. `pci,usb`
 * @error: (nullable): optional return location for an error
 *
 * Sets the logical ID from the device subsystem. Plugins should choose the
 * subsystem that most relevant in the udev tree, for instance choosing `hid`
 * over `usb` for a mouse device.
 *
 * Returns: %TRUE if the logical device was set.
 *
 * Since: 1.5.8
 **/
gboolean
fu_udev_device_set_logical_id(FuUdevDevice *self, const gchar *subsystem, GError **error)
{
#ifdef HAVE_GUDEV
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;
	g_autofree gchar *logical_id = NULL;
	g_autoptr(GUdevDevice) udev_device = NULL;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), FALSE);
	g_return_val_if_fail(subsystem != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* nothing to do */
	if (priv->udev_device == NULL)
		return TRUE;

	/* find correct device matching subsystem */
	if (g_strcmp0(priv->subsystem, subsystem) == 0) {
		udev_device = g_object_ref(priv->udev_device);
	} else {
		udev_device =
		    g_udev_device_get_parent_with_subsystem(priv->udev_device, subsystem, NULL);
	}
	if (udev_device == NULL) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_FOUND,
			    "failed to find device with subsystem %s",
			    subsystem);
		return FALSE;
	}

	/* query each subsystem */
	if (g_strcmp0(subsystem, "hid") == 0) {
		tmp = g_udev_device_get_property(udev_device, "HID_UNIQ");
		if (tmp == NULL) {
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_NOT_FOUND,
					    "failed to find HID_UNIQ");
			return FALSE;
		}
		logical_id = g_strdup_printf("HID_UNIQ=%s", tmp);
	} else {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "cannot handle subsystem %s",
			    subsystem);
		return FALSE;
	}

	/* success */
	fu_device_set_logical_id(FU_DEVICE(self), logical_id);
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not supported as <gudev.h> is unavailable");
	return FALSE;
#endif
}

/**
 * fu_udev_device_get_fd:
 * @self: a #FuUdevDevice
 *
 * Gets the file descriptor if the device is open.
 *
 * Returns: positive integer, or -1 if the device is not open
 *
 * Since: 1.3.3
 **/
gint
fu_udev_device_get_fd(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), -1);
	return priv->fd;
}

/**
 * fu_udev_device_set_fd:
 * @self: a #FuUdevDevice
 * @fd: a valid file descriptor
 *
 * Replace the file descriptor to use when the device has already been opened.
 * This object will automatically close() @fd when fu_device_close() is called.
 *
 * Since: 1.3.3
 **/
void
fu_udev_device_set_fd(FuUdevDevice *self, gint fd)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	if (priv->fd > 0)
		close(priv->fd);
	priv->fd = fd;
}

/**
 * fu_udev_device_set_flags:
 * @self: a #FuUdevDevice
 * @flags: udev device flags, e.g. %FU_UDEV_DEVICE_FLAG_OPEN_READ
 *
 * Sets the parameters to use when opening the device.
 *
 * For example %FU_UDEV_DEVICE_FLAG_OPEN_READ means that fu_device_open()
 * would use `O_RDONLY` rather than `O_RDWR` which is the default.
 *
 * Since: 1.3.6
 **/
void
fu_udev_device_set_flags(FuUdevDevice *self, FuUdevDeviceFlags flags)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	priv->flags = flags;

#ifdef HAVE_GUDEV
	/* overwrite */
	if (flags & FU_UDEV_DEVICE_FLAG_USE_CONFIG) {
		g_free(priv->device_file);
		priv->device_file =
		    g_build_filename(g_udev_device_get_sysfs_path(priv->udev_device),
				     "config",
				     NULL);
	}
#endif
}

static gboolean
fu_udev_device_open(FuDevice *device, GError **error)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	/* open device */
	if (priv->device_file != NULL && priv->flags != FU_UDEV_DEVICE_FLAG_NONE) {
		gint flags;
		if (priv->flags & FU_UDEV_DEVICE_FLAG_OPEN_READ &&
		    priv->flags & FU_UDEV_DEVICE_FLAG_OPEN_WRITE) {
			flags = O_RDWR;
		} else if (priv->flags & FU_UDEV_DEVICE_FLAG_OPEN_WRITE) {
			flags = O_WRONLY;
		} else {
			flags = O_RDONLY;
		}
#ifdef O_NONBLOCK
		if (priv->flags & FU_UDEV_DEVICE_FLAG_OPEN_NONBLOCK)
			flags |= O_NONBLOCK;
#endif
#ifdef O_SYNC
		if (priv->flags & FU_UDEV_DEVICE_FLAG_OPEN_SYNC)
			flags |= O_SYNC;
#endif
		priv->fd = g_open(priv->device_file, flags, 0);
		if (priv->fd < 0) {
			g_set_error(error,
				    G_IO_ERROR,
#ifdef HAVE_ERRNO_H
				    g_io_error_from_errno(errno),
#else
				    G_IO_ERROR_FAILED,
#endif
				    "failed to open %s: %s",
				    priv->device_file,
				    strerror(errno));
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_udev_device_rescan(FuDevice *device, GError **error)
{
#ifdef HAVE_GUDEV
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *sysfs_path;
	g_autoptr(GUdevClient) udev_client = g_udev_client_new(NULL);
	g_autoptr(GUdevDevice) udev_device = NULL;

	/* never set */
	if (priv->udev_device == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "rescan with no previous device");
		return FALSE;
	}
	sysfs_path = g_udev_device_get_sysfs_path(priv->udev_device);
	udev_device = g_udev_client_query_by_sysfs_path(udev_client, sysfs_path);
	if (udev_device == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "rescan could not find device %s",
			    sysfs_path);
		return FALSE;
	}
	fu_udev_device_set_dev(self, udev_device);
	fu_device_probe_invalidate(device);
#endif
	return fu_device_probe(device, error);
}

static gboolean
fu_udev_device_close(FuDevice *device, GError **error)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	/* close device */
	if (priv->fd > 0) {
		if (!g_close(priv->fd, error))
			return FALSE;
		priv->fd = 0;
	}

	/* success */
	return TRUE;
}

/**
 * fu_udev_device_ioctl:
 * @self: a #FuUdevDevice
 * @request: request number
 * @buf: a buffer to use, which *must* be large enough for the request
 * @rc: (out) (nullable): the raw return value from the ioctl
 * @timeout: timeout in ms for the retry action, see %FU_UDEV_DEVICE_FLAG_IOCTL_RETRY
 * @error: (nullable): optional return location for an error
 *
 * Control a device using a low-level request.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.2
 **/
gboolean
fu_udev_device_ioctl(FuUdevDevice *self,
		     gulong request,
		     guint8 *buf,
		     gint *rc,
		     guint timeout,
		     GError **error)
{
#ifdef HAVE_IOCTL_H
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	gint rc_tmp;
	g_autoptr(GTimer) timer = g_timer_new();

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), FALSE);
	g_return_val_if_fail(request != 0x0, FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* not open! */
	if (priv->fd == 0) {
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
		rc_tmp = ioctl(priv->fd, request, buf);
		if (rc_tmp >= 0)
			break;
	} while ((priv->flags & FU_UDEV_DEVICE_FLAG_IOCTL_RETRY) &&
		 (errno == EINTR || errno == EAGAIN) &&
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
			    strerror(errno),
			    errno);
#else
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "unspecified ioctl error");
#endif
		return FALSE;
	}
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

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* not open! */
	if (priv->fd == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "%s [%s] has not been opened",
			    fu_device_get_id(FU_DEVICE(self)),
			    fu_device_get_name(FU_DEVICE(self)));
		return FALSE;
	}

#ifdef HAVE_PWRITE
	if (pread(priv->fd, buf, bufsz, port) != (gssize)bufsz) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "failed to read from port 0x%04x: %s",
			    (guint)port,
			    strerror(errno));
		return FALSE;
	}
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

	/* not open! */
	if (priv->fd == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "%s [%s] has not been opened",
			    fu_device_get_id(FU_DEVICE(self)),
			    fu_device_get_name(FU_DEVICE(self)));
		return FALSE;
	}

#ifdef HAVE_PWRITE
	if (lseek(priv->fd, offset, SEEK_SET) < 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "failed to seek to 0x%04x: %s",
			    (guint)offset,
			    strerror(errno));
		return FALSE;
	}
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not supported as lseek() is unavailable");
	return FALSE;
#endif
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

	/* not open! */
	if (priv->fd == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "%s [%s] has not been opened",
			    fu_device_get_id(FU_DEVICE(self)),
			    fu_device_get_name(FU_DEVICE(self)));
		return FALSE;
	}

#ifdef HAVE_PWRITE
	if (pwrite(priv->fd, buf, bufsz, port) != (gssize)bufsz) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "failed to write to port %04x: %s",
			    (guint)port,
			    strerror(errno));
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
 * fu_udev_device_get_parent_name
 * @self: a #FuUdevDevice
 *
 * Returns the name of the direct ancestor of this device
 *
 * Returns: string or NULL if unset or invalid
 *
 * Since: 1.4.5
 **/
gchar *
fu_udev_device_get_parent_name(FuUdevDevice *self)
{
#ifdef HAVE_GUDEV
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GUdevDevice) parent = NULL;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);

	parent = g_udev_device_get_parent(priv->udev_device);
	return parent == NULL ? NULL : g_strdup(g_udev_device_get_name(parent));
#else
	return NULL;
#endif
}

/**
 * fu_udev_device_get_sysfs_attr:
 * @self: a #FuUdevDevice
 * @attr: name of attribute to get
 * @error: (nullable): optional return location for an error
 *
 * Reads an arbitrary sysfs attribute 'attr' associated with UDEV device
 *
 * Returns: string or NULL
 *
 * Since: 1.4.5
 **/
const gchar *
fu_udev_device_get_sysfs_attr(FuUdevDevice *self, const gchar *attr, GError **error)
{
#ifdef HAVE_GUDEV
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *result;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	g_return_val_if_fail(attr != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* nothing to do */
	if (priv->udev_device == NULL) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "not yet initialized");
		return NULL;
	}
	result = g_udev_device_get_sysfs_attr(priv->udev_device, attr);
	if (result == NULL) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_FOUND,
			    "attribute %s returned no data",
			    attr);
		return NULL;
	}

	return result;
#else
	g_set_error_literal(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "getting attributes is not supported as no GUdev support");
	return NULL;
#endif
}

/**
 * fu_udev_device_get_sysfs_attr_uint64:
 * @self: a #FuUdevDevice
 * @attr: name of attribute to get
 * @value: (out) (optional): value to return
 * @error: (nullable): optional return location for an error
 *
 * Reads an arbitrary sysfs attribute 'attr' associated with UDEV device as a uint64.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.7.2
 **/
gboolean
fu_udev_device_get_sysfs_attr_uint64(FuUdevDevice *self,
				     const gchar *attr,
				     guint64 *value,
				     GError **error)
{
	const gchar *tmp;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), FALSE);
	g_return_val_if_fail(attr != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	tmp = fu_udev_device_get_sysfs_attr(self, attr, error);
	if (tmp == NULL)
		return FALSE;
	return fu_strtoull(tmp, value, 0, G_MAXUINT64, error);
}

/**
 * fu_udev_device_write_sysfs:
 * @self: a #FuUdevDevice
 * @attribute: sysfs attribute name
 * @val: data to write into the attribute
 * @error: (nullable): optional return location for an error
 *
 * Writes data into a sysfs attribute
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.5
 **/
gboolean
fu_udev_device_write_sysfs(FuUdevDevice *self,
			   const gchar *attribute,
			   const gchar *val,
			   GError **error)
{
#ifdef __linux__
	ssize_t n;
	int r;
	int fd;
	g_autofree gchar *path = NULL;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), FALSE);
	g_return_val_if_fail(attribute != NULL, FALSE);
	g_return_val_if_fail(val != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	path = g_build_filename(fu_udev_device_get_sysfs_path(self), attribute, NULL);
	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    g_io_error_from_errno(errno),
			    "could not open %s: %s",
			    path,
			    g_strerror(errno));
		return FALSE;
	}

	do {
		n = write(fd, val, strlen(val));
		if (n < 1 && errno != EINTR) {
			g_set_error(error,
				    G_IO_ERROR,
				    g_io_error_from_errno(errno),
				    "could not write to %s: %s",
				    path,
				    g_strerror(errno));
			(void)close(fd);
			return FALSE;
		}
	} while (n < 1);

	r = close(fd);
	if (r < 0 && errno != EINTR) {
		g_set_error(error,
			    G_IO_ERROR,
			    g_io_error_from_errno(errno),
			    "could not close %s: %s",
			    path,
			    g_strerror(errno));
		return FALSE;
	}

	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "sysfs attributes not supported on Windows");
	return FALSE;
#endif
}

/**
 * fu_udev_device_get_devtype
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
#ifdef HAVE_GUDEV
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	return g_udev_device_get_devtype(priv->udev_device);
#else
	return NULL;
#endif
}

/**
 * fu_udev_device_get_siblings_with_subsystem
 * @self: a #FuUdevDevice
 * @subsystem: the name of a udev subsystem
 *
 * Get a list of devices that are siblings of self and have the
 * provided subsystem.
 *
 * Returns: (element-type FuUdevDevice) (transfer full): devices
 *
 * Since: 1.6.0
 */
GPtrArray *
fu_udev_device_get_siblings_with_subsystem(FuUdevDevice *self, const gchar *const subsystem)
{
	g_autoptr(GPtrArray) out = g_ptr_array_new_with_free_func(g_object_unref);

#ifdef HAVE_GUDEV
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GUdevDevice) udev_parent = g_udev_device_get_parent(priv->udev_device);
	const gchar *udev_parent_path = g_udev_device_get_sysfs_path(udev_parent);
	g_autoptr(GUdevClient) udev_client = g_udev_client_new(NULL);

	g_autoptr(GList) enumerated = g_udev_client_query_by_subsystem(udev_client, subsystem);
	for (GList *element = enumerated; element != NULL; element = element->next) {
		g_autoptr(GUdevDevice) enumerated_device = element->data;
		g_autoptr(GUdevDevice) enumerated_parent = NULL;
		const gchar *enumerated_parent_path;

		/* get parent, if it exists */
		enumerated_parent = g_udev_device_get_parent(enumerated_device);
		if (enumerated_parent == NULL)
			break;
		enumerated_parent_path = g_udev_device_get_sysfs_path(enumerated_parent);

		/* if the sysfs path of self's parent is the same as that of the
		 * located device's parent, they are siblings */
		if (g_strcmp0(udev_parent_path, enumerated_parent_path) == 0) {
			FuUdevDevice *dev =
			    fu_udev_device_new(fu_device_get_context(FU_DEVICE(self)),
					       g_steal_pointer(&enumerated_device));
			g_ptr_array_add(out, dev);
		}
	}
#endif

	return g_steal_pointer(&out);
}

/**
 * fu_udev_device_get_parent_with_subsystem
 * @self: a #FuUdevDevice
 * @subsystem: the name of a udev subsystem
 *
 * Get the device that is a parent of self and has the provided subsystem.
 *
 * Returns: (transfer full): device, or %NULL
 *
 * Since: 1.7.6
 */
FuUdevDevice *
fu_udev_device_get_parent_with_subsystem(FuUdevDevice *self, const gchar *subsystem)
{
#ifdef HAVE_GUDEV
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GUdevDevice) device_tmp = NULL;

	device_tmp = g_udev_device_get_parent_with_subsystem(priv->udev_device, subsystem, NULL);
	if (device_tmp == NULL)
		return NULL;
	return fu_udev_device_new(fu_device_get_context(FU_DEVICE(self)), device_tmp);
#else
	return NULL;
#endif
}

/**
 * fu_udev_device_get_children_with_subsystem
 * @self: a #FuUdevDevice
 * @subsystem: the name of a udev subsystem
 *
 * Get a list of devices that are children of self and have the
 * provided subsystem.
 *
 * Returns: (element-type FuUdevDevice) (transfer full): devices
 *
 * Since: 1.6.2
 */
GPtrArray *
fu_udev_device_get_children_with_subsystem(FuUdevDevice *self, const gchar *const subsystem)
{
	g_autoptr(GPtrArray) out = g_ptr_array_new_with_free_func(g_object_unref);

#ifdef HAVE_GUDEV
	const gchar *self_path = fu_udev_device_get_sysfs_path(self);
	g_autoptr(GUdevClient) udev_client = g_udev_client_new(NULL);

	g_autoptr(GList) enumerated = g_udev_client_query_by_subsystem(udev_client, subsystem);
	for (GList *element = enumerated; element != NULL; element = element->next) {
		g_autoptr(GUdevDevice) enumerated_device = element->data;
		g_autoptr(GUdevDevice) enumerated_parent = NULL;
		const gchar *enumerated_parent_path;

		/* get parent, if it exists */
		enumerated_parent = g_udev_device_get_parent(enumerated_device);
		if (enumerated_parent == NULL)
			break;
		enumerated_parent_path = g_udev_device_get_sysfs_path(enumerated_parent);

		/* enumerated device is a child of self if its parent is the
		 * same as self */
		if (g_strcmp0(self_path, enumerated_parent_path) == 0) {
			FuUdevDevice *dev =
			    fu_udev_device_new(fu_device_get_context(FU_DEVICE(self)),
					       g_steal_pointer(&enumerated_device));
			g_ptr_array_add(out, dev);
		}
	}
#endif

	return g_steal_pointer(&out);
}

static void
fu_udev_device_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(object);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_UDEV_DEVICE:
		g_value_set_object(value, priv->udev_device);
		break;
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
	case PROP_UDEV_DEVICE:
		fu_udev_device_set_dev(self, g_value_get_object(value));
		break;
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

	g_free(priv->subsystem);
	g_free(priv->bind_id);
	g_free(priv->driver);
	g_free(priv->device_file);
	if (priv->udev_device != NULL)
		g_object_unref(priv->udev_device);
	if (priv->fd > 0)
		g_close(priv->fd, NULL);

	G_OBJECT_CLASS(fu_udev_device_parent_class)->finalize(object);
}

static void
fu_udev_device_init(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	priv->flags = FU_UDEV_DEVICE_FLAG_OPEN_READ | FU_UDEV_DEVICE_FLAG_OPEN_WRITE;
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
	 * FuUdevDevice:udev-device:
	 *
	 * The low-level GUdevDevice.
	 *
	 * Since: 1.1.2
	 */
	pspec = g_param_spec_object("udev-device",
				    NULL,
				    NULL,
				    G_UDEV_TYPE_DEVICE,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_UDEV_DEVICE, pspec);

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
}

/**
 * fu_udev_device_new:
 * @ctx: (nullable): a #FuContext
 * @udev_device: a #GUdevDevice
 *
 * Creates a new #FuUdevDevice.
 *
 * Returns: (transfer full): a #FuUdevDevice
 *
 * Since: 1.8.2
 **/
FuUdevDevice *
fu_udev_device_new(FuContext *ctx, GUdevDevice *udev_device)
{
	return g_object_new(FU_TYPE_UDEV_DEVICE, "context", ctx, "udev-device", udev_device, NULL);
}
