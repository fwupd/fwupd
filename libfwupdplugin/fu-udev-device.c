/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuUdevDevice"

#include "config.h"

#include <fcntl.h>
#include <string.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_IOCTL_H
#include <sys/ioctl.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib/gstdio.h>

#include "fu-device-private.h"
#include "fu-udev-device-private.h"

/**
 * SECTION:fu-udev-device
 * @short_description: a udev device
 *
 * An object that represents a udev device.
 *
 * See also: #FuDevice
 */

typedef struct
{
	GUdevDevice		*udev_device;
	guint32			 vendor;
	guint32			 model;
	guint32			 subsystem_vendor;
	guint32			 subsystem_model;
	guint8			 revision;
	gchar			*subsystem;
	gchar			*driver;
	gchar			*device_file;
	gint			 fd;
	FuUdevDeviceFlags	 flags;
} FuUdevDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuUdevDevice, fu_udev_device, FU_TYPE_DEVICE)

enum {
	PROP_0,
	PROP_UDEV_DEVICE,
	PROP_SUBSYSTEM,
	PROP_DRIVER,
	PROP_DEVICE_FILE,
	PROP_LAST
};

enum {
	SIGNAL_CHANGED,
	SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

#define GET_PRIVATE(o) (fu_udev_device_get_instance_private (o))

/**
 * fu_udev_device_emit_changed:
 * @self: A #FuUdevDevice
 *
 * Emits the ::changed signal for the object.
 *
 * Since: 1.1.2
 **/
void
fu_udev_device_emit_changed (FuUdevDevice *self)
{
	g_autoptr(GError) error = NULL;
	g_return_if_fail (FU_IS_UDEV_DEVICE (self));
	g_debug ("FuUdevDevice emit changed");
	if (!fu_device_rescan (FU_DEVICE (self), &error))
		g_debug ("%s", error->message);
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

static guint32
fu_udev_device_get_sysfs_attr_as_uint32 (GUdevDevice *udev_device, const gchar *name)
{
#ifdef HAVE_GUDEV
	guint64 tmp = fu_common_strtoull (g_udev_device_get_sysfs_attr (udev_device, name));
	if (tmp > G_MAXUINT32) {
		g_warning ("reading %s for %s overflowed",
			   name,
			   g_udev_device_get_sysfs_path (udev_device));
		return G_MAXUINT32;
	}
	return tmp;
#else
	return G_MAXUINT32;
#endif
}

static guint8
fu_udev_device_get_sysfs_attr_as_uint8 (GUdevDevice *udev_device, const gchar *name)
{
#ifdef HAVE_GUDEV
	guint64 tmp = fu_common_strtoull (g_udev_device_get_sysfs_attr (udev_device, name));
	if (tmp > G_MAXUINT8) {
		g_warning ("reading %s for %s overflowed",
			   name,
			   g_udev_device_get_sysfs_path (udev_device));
		return G_MAXUINT8;
	}
	return tmp;
#else
	return G_MAXUINT8;
#endif
}

#ifdef HAVE_GUDEV
static void
fu_udev_device_to_string_raw (GUdevDevice *udev_device, guint idt, GString *str)
{
	const gchar * const *keys;
	if (udev_device == NULL)
		return;
	keys = g_udev_device_get_property_keys (udev_device);
	for (guint i = 0; keys[i] != NULL; i++) {
		fu_common_string_append_kv (str, idt, keys[i],
					    g_udev_device_get_property (udev_device, keys[i]));
	}
	keys = g_udev_device_get_sysfs_attr_keys (udev_device);
	for (guint i = 0; keys[i] != NULL; i++) {
		fu_common_string_append_kv (str, idt, keys[i],
					    g_udev_device_get_sysfs_attr (udev_device, keys[i]));
	}
}
#endif

static void
fu_udev_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuUdevDevice *self = FU_UDEV_DEVICE (device);
	FuUdevDeviceClass *klass = FU_UDEV_DEVICE_GET_CLASS (self);
#ifdef HAVE_GUDEV
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	if (priv->udev_device != NULL) {
		fu_common_string_append_kv (str, idt, "SysfsPath",
					    g_udev_device_get_sysfs_path (priv->udev_device));
		fu_common_string_append_kv (str, idt, "Subsystem", priv->subsystem);
		if (priv->driver != NULL)
			fu_common_string_append_kv (str, idt, "Driver", priv->driver);
		if (priv->device_file != NULL)
			fu_common_string_append_kv (str, idt, "DeviceFile", priv->device_file);
	}
	if (g_getenv ("FU_UDEV_DEVICE_DEBUG") != NULL) {
		g_autoptr(GUdevDevice) udev_parent = NULL;
		fu_udev_device_to_string_raw (priv->udev_device, idt, str);
		udev_parent = g_udev_device_get_parent (priv->udev_device);
		if (udev_parent != NULL) {
			fu_common_string_append_kv (str, idt, "Parent", NULL);
			fu_udev_device_to_string_raw (udev_parent, idt + 1, str);
		}
	}
#endif

	/* subclassed */
	if (klass->to_string != NULL) {
		g_warning ("FuUdevDevice->to_string is deprecated!");
		klass->to_string (self, idt, str);
	}
}

static void
fu_udev_device_set_subsystem (FuUdevDevice *self, const gchar *subsystem)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);

	/* not changed */
	if (g_strcmp0 (priv->subsystem, subsystem) == 0)
		return;

	g_free (priv->subsystem);
	priv->subsystem = g_strdup (subsystem);
	g_object_notify (G_OBJECT (self), "subsystem");
}

static void
fu_udev_device_set_driver (FuUdevDevice *self, const gchar *driver)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);

	/* not changed */
	if (g_strcmp0 (priv->driver, driver) == 0)
		return;

	g_free (priv->driver);
	priv->driver = g_strdup (driver);
	g_object_notify (G_OBJECT (self), "driver");
}

static void
fu_udev_device_set_device_file (FuUdevDevice *self, const gchar *device_file)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);

	/* not changed */
	if (g_strcmp0 (priv->device_file, device_file) == 0)
		return;

	g_free (priv->device_file);
	priv->device_file = g_strdup (device_file);
	g_object_notify (G_OBJECT (self), "device-file");
}

#ifdef HAVE_GUDEV
static const gchar *
fu_udev_device_get_vendor_fallback (GUdevDevice *udev_device)
{
	const gchar *tmp;
	tmp = g_udev_device_get_property (udev_device, "ID_VENDOR_FROM_DATABASE");
	if (tmp != NULL)
		return tmp;
	tmp = g_udev_device_get_property (udev_device, "ID_VENDOR");
	if (tmp != NULL)
		return tmp;
	return NULL;
}
#endif

#ifdef HAVE_GUDEV
static gboolean
fu_udev_device_probe_i2c_dev (FuUdevDevice *self, GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	const gchar *name = g_udev_device_get_sysfs_attr (priv->udev_device, "name");
	if (name != NULL) {
		g_autofree gchar *devid = NULL;
		g_autofree gchar *name_safe = g_strdup (name);
		g_strdelimit (name_safe, " /\\\"", '-');
		devid = g_strdup_printf ("I2C\\NAME_%s", name_safe);
		fu_device_add_instance_id (FU_DEVICE (self), devid);
	}
	return TRUE;
}

static gboolean
fu_udev_device_probe_serio (FuUdevDevice *self, GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	const gchar *tmp;

	/* firmware ID */
	tmp = g_udev_device_get_property (priv->udev_device, "SERIO_FIRMWARE_ID");
	if (tmp != NULL) {
		g_autofree gchar *devid = NULL;
		g_autofree gchar *id_safe = NULL;
		/* this prefix is not useful */
		if (g_str_has_prefix (tmp, "PNP: "))
			tmp += 5;
		id_safe = g_utf8_strup (tmp, -1);
		g_strdelimit (id_safe, " /\\\"", '-');
		devid = g_strdup_printf ("SERIO\\FWID_%s", id_safe);
		fu_device_add_instance_id (FU_DEVICE (self), devid);
	}
	return TRUE;
}
#endif

static gboolean
fu_udev_device_probe (FuDevice *device, GError **error)
{
	FuUdevDeviceClass *klass = FU_UDEV_DEVICE_GET_CLASS (device);
	FuUdevDevice *self = FU_UDEV_DEVICE (device);
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
#ifdef HAVE_GUDEV
	const gchar *tmp;
	g_autofree gchar *subsystem = NULL;
	g_autoptr(GUdevDevice) udev_parent = NULL;
	g_autoptr(GUdevDevice) parent_i2c = NULL;
#endif

	/* nothing to do */
	if (priv->udev_device == NULL)
		return TRUE;

	/* set ven:dev:rev */
	priv->vendor = fu_udev_device_get_sysfs_attr_as_uint32 (priv->udev_device, "vendor");
	priv->model = fu_udev_device_get_sysfs_attr_as_uint32 (priv->udev_device, "device");
	priv->revision = fu_udev_device_get_sysfs_attr_as_uint8 (priv->udev_device, "revision");
	priv->subsystem_vendor = fu_udev_device_get_sysfs_attr_as_uint32 (priv->udev_device, "subsystem_vendor");
	priv->subsystem_model = fu_udev_device_get_sysfs_attr_as_uint32 (priv->udev_device, "subsystem_device");

#ifdef HAVE_GUDEV
	/* fallback to the parent */
	udev_parent = g_udev_device_get_parent (priv->udev_device);
	if (udev_parent != NULL &&
	    priv->flags & FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT &&
	    priv->vendor == 0x0 && priv->model == 0x0 && priv->revision == 0x0) {
		priv->vendor = fu_udev_device_get_sysfs_attr_as_uint32 (udev_parent, "vendor");
		priv->model = fu_udev_device_get_sysfs_attr_as_uint32 (udev_parent, "device");
		priv->revision = fu_udev_device_get_sysfs_attr_as_uint8 (udev_parent, "revision");
		priv->subsystem_vendor = fu_udev_device_get_sysfs_attr_as_uint32 (udev_parent, "subsystem_vendor");
		priv->subsystem_model = fu_udev_device_get_sysfs_attr_as_uint32 (udev_parent, "subsystem_device");
	}

	/* hidraw helpfully encodes the information in a different place */
	if (udev_parent != NULL &&
	    priv->vendor == 0x0 && priv->model == 0x0 && priv->revision == 0x0 &&
	    g_strcmp0 (priv->subsystem, "hidraw") == 0) {
		tmp = g_udev_device_get_property (udev_parent, "HID_ID");
		if (tmp != NULL) {
			g_auto(GStrv) split = g_strsplit (tmp, ":", -1);
			if (g_strv_length (split) == 3) {
				guint64 val = g_ascii_strtoull (split[1], NULL, 16);
				if (val > G_MAXUINT32) {
					g_warning ("reading %s for %s overflowed",
						   split[1],
						   g_udev_device_get_sysfs_path (priv->udev_device));
				} else {
					priv->vendor = val;
				}
				val = g_ascii_strtoull (split[2], NULL, 16);
				if (val > G_MAXUINT32) {
					g_warning ("reading %s for %s overflowed",
						   split[2],
						   g_udev_device_get_sysfs_path (priv->udev_device));
				} else {
					priv->model = val;
				}
			}
		}
		tmp = g_udev_device_get_property (udev_parent, "HID_NAME");
		if (tmp != NULL) {
			if (fu_device_get_name (device) == NULL)
				fu_device_set_name (device, tmp);
		}
	}

	/* set the version if the revision has been set */
	if (fu_device_get_version (device) == NULL &&
	    fu_device_get_version_format (device) == FWUPD_VERSION_FORMAT_UNKNOWN) {
		if (priv->revision != 0x00) {
			g_autofree gchar *version = g_strdup_printf ("%02x", priv->revision);
			fu_device_set_version_format (device, FWUPD_VERSION_FORMAT_PLAIN);
			fu_device_set_version (device, version);
		}
	}

	/* set model */
	if (fu_device_get_name (device) == NULL) {
		tmp = g_udev_device_get_property (priv->udev_device, "ID_MODEL_FROM_DATABASE");
		if (tmp == NULL)
			tmp = g_udev_device_get_property (priv->udev_device, "ID_MODEL");
		if (tmp == NULL)
			tmp = g_udev_device_get_property (priv->udev_device, "ID_PCI_CLASS_FROM_DATABASE");
		if (tmp != NULL)
			fu_device_set_name (device, tmp);
	}

	/* set vendor */
	if (fu_device_get_vendor (device) == NULL) {
		tmp = fu_udev_device_get_vendor_fallback (priv->udev_device);
		if (tmp != NULL)
			fu_device_set_vendor (device, tmp);
	}

	/* try harder to find a vendor name the user will recognize */
	if (priv->flags & FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT &&
	    udev_parent != NULL && fu_device_get_vendor (device) == NULL) {
		g_autoptr(GUdevDevice) device_tmp = g_object_ref (udev_parent);
		for (guint i = 0; i < 0xff; i++) {
			g_autoptr(GUdevDevice) parent = NULL;
			tmp = fu_udev_device_get_vendor_fallback (device_tmp);
			if (tmp != NULL) {
				fu_device_set_vendor (device, tmp);
				break;
			}
			parent = g_udev_device_get_parent (device_tmp);
			if (parent == NULL)
				break;
			g_set_object (&device_tmp, parent);
		}
	}

	/* set serial */
	if (fu_device_get_serial (device) == NULL) {
		tmp = g_udev_device_get_property (priv->udev_device, "ID_SERIAL_SHORT");
		if (tmp == NULL)
			tmp = g_udev_device_get_property (priv->udev_device, "ID_SERIAL");
		if (tmp != NULL)
			fu_device_set_serial (device, tmp);
	}

	/* set revision */
	if (fu_device_get_version (device) == NULL &&
	    fu_device_get_version_format (device) == FWUPD_VERSION_FORMAT_UNKNOWN) {
		tmp = g_udev_device_get_property (priv->udev_device, "ID_REVISION");
		if (tmp != NULL)
			fu_device_set_version (device, tmp);
	}

	/* set vendor ID */
	subsystem = g_ascii_strup (g_udev_device_get_subsystem (priv->udev_device), -1);
	if (subsystem != NULL && priv->vendor != 0x0000) {
		g_autofree gchar *vendor_id = NULL;
		vendor_id = g_strdup_printf ("%s:0x%04X", subsystem, (guint) priv->vendor);
		fu_device_add_vendor_id (device, vendor_id);
	}

	/* add GUIDs in order of priority */
	if (priv->vendor != 0x0000 && priv->model != 0x0000 &&
	    priv->subsystem_vendor != 0x0000 && priv->subsystem_model != 0x0000) {
		g_autofree gchar *devid1 = NULL;
		g_autofree gchar *devid2 = NULL;
		devid1 = g_strdup_printf ("%s\\VEN_%04X&DEV_%04X&SUBSYS_%04X%04X&REV_%02X",
					  subsystem,
					  priv->vendor, priv->model,
					  priv->subsystem_vendor, priv->subsystem_model,
					  priv->revision);
		fu_device_add_instance_id (device, devid1);
		devid2 = g_strdup_printf ("%s\\VEN_%04X&DEV_%04X&SUBSYS_%04X%04X",
					  subsystem,
					  priv->vendor, priv->model,
					  priv->subsystem_vendor, priv->subsystem_model);
		fu_device_add_instance_id (device, devid2);
	}
	if (priv->vendor != 0x0000 && priv->model != 0x0000) {
		g_autofree gchar *devid = NULL;
		devid = g_strdup_printf ("%s\\VEN_%04X&DEV_%04X&REV_%02X",
					 subsystem, priv->vendor,
					 priv->model, priv->revision);
		fu_device_add_instance_id (device, devid);
	}
	if (priv->vendor != 0x0000 && priv->model != 0x0000) {
		g_autofree gchar *devid = NULL;
		devid = g_strdup_printf ("%s\\VEN_%04X&DEV_%04X",
					 subsystem, priv->vendor, priv->model);
		fu_device_add_instance_id (device, devid);
	}
	if (priv->vendor != 0x0000) {
		g_autofree gchar *devid = NULL;
		devid = g_strdup_printf ("%s\\VEN_%04X", subsystem, priv->vendor);
		fu_device_add_instance_id_full (device, devid,
						FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS);
	}

	/* add device class */
	tmp = g_udev_device_get_sysfs_attr (priv->udev_device, "class");
	if (tmp != NULL && g_str_has_prefix (tmp, "0x")) {
		g_autofree gchar *class_id = g_utf8_strup (tmp + 2, -1);
		g_autofree gchar *devid = NULL;
		devid = g_strdup_printf ("%s\\VEN_%04X&CLASS_%s",
					 subsystem,
					 priv->vendor,
					 class_id);
		fu_device_add_instance_id_full (device, devid,
						FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS);
	}

	/* add the driver */
	if (priv->driver != NULL) {
		g_autofree gchar *devid = NULL;
		devid = g_strdup_printf ("%s\\DRIVER_%s",
					 subsystem,
					 priv->driver);
		fu_device_add_instance_id_full (device, devid,
						FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS);
	}

	/* add subsystem to match in plugins */
	if (subsystem != NULL) {
		fu_device_add_instance_id_full (device, subsystem,
						FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS);
	}

	/* i2c devices all expose a name */
	if (g_strcmp0 (g_udev_device_get_subsystem (priv->udev_device), "i2c-dev") == 0) {
		if (!fu_udev_device_probe_i2c_dev (self, error))
			return FALSE;
	}

	/* add firmware_id */
	if (g_strcmp0 (g_udev_device_get_subsystem (priv->udev_device), "serio") == 0) {
		if (!fu_udev_device_probe_serio (self, error))
			return FALSE;
	}

	/* determine if we're wired internally */
	parent_i2c = g_udev_device_get_parent_with_subsystem (priv->udev_device,
							      "i2c", NULL);
	if (parent_i2c != NULL)
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_INTERNAL);
#endif

	/* subclassed */
	if (klass->probe != NULL) {
		g_warning ("FuUdevDevice->probe is deprecated!");
		if (!klass->probe (self, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

#ifdef HAVE_GUDEV
static gchar *
fu_udev_device_get_miscdev0 (FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	const gchar *fn;
	g_autofree gchar *miscdir = NULL;
	g_autoptr(GDir) dir = NULL;

	miscdir = g_build_filename (g_udev_device_get_sysfs_path (priv->udev_device), "misc", NULL);
	dir = g_dir_open (miscdir, 0, NULL);
	if (dir == NULL)
		return NULL;
	fn = g_dir_read_name (dir);
	if (fn == NULL)
		return NULL;
	return g_strdup_printf ("/dev/%s", fn);
}
#endif

static void
fu_udev_device_set_dev (FuUdevDevice *self, GUdevDevice *udev_device)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
#ifdef HAVE_GUDEV
	const gchar *summary;
#endif

	g_return_if_fail (FU_IS_UDEV_DEVICE (self));

#ifdef HAVE_GUDEV
	/* the net subsystem is not a real hardware class */
	if (udev_device != NULL &&
	    g_strcmp0 (g_udev_device_get_subsystem (udev_device), "net") == 0) {
		g_autoptr(GUdevDevice) udev_device_phys = NULL;
		udev_device_phys = g_udev_device_get_parent (udev_device);
		g_set_object (&priv->udev_device, udev_device_phys);
		fu_device_set_metadata (FU_DEVICE (self),
					"ParentSubsystem",
					g_udev_device_get_subsystem (udev_device));
	} else {
		g_set_object (&priv->udev_device, udev_device);
	}
#else
	g_set_object (&priv->udev_device, udev_device);
#endif

	/* set new device */
	if (priv->udev_device == NULL)
		return;
#ifdef HAVE_GUDEV
	fu_udev_device_set_subsystem (self, g_udev_device_get_subsystem (priv->udev_device));
	fu_udev_device_set_driver (self, g_udev_device_get_driver (priv->udev_device));
	fu_udev_device_set_device_file (self, g_udev_device_get_device_file (priv->udev_device));

	/* so we can display something sensible for unclaimed devices */
	fu_device_set_backend_id (FU_DEVICE (self), g_udev_device_get_sysfs_path (priv->udev_device));

	/* fall back to the first thing handled by misc drivers */
	if (priv->device_file == NULL) {
		/* perhaps we should unconditionally fall back? or perhaps
		 * require FU_UDEV_DEVICE_FLAG_FALLBACK_MISC... */
		if (g_strcmp0 (priv->subsystem, "serio") == 0)
			priv->device_file = fu_udev_device_get_miscdev0 (self);
		if (priv->device_file != NULL)
			g_debug ("falling back to misc %s", priv->device_file);
	}

	/* try to get one line summary */
	summary = g_udev_device_get_sysfs_attr (priv->udev_device, "description");
	if (summary == NULL) {
		g_autoptr(GUdevDevice) parent = NULL;
		parent = g_udev_device_get_parent (priv->udev_device);
		if (parent != NULL)
			summary = g_udev_device_get_sysfs_attr (parent, "description");
	}
	if (summary != NULL)
		fu_device_set_summary (FU_DEVICE (self), summary);
#endif
}

/**
 * fu_udev_device_get_slot_depth:
 * @self: A #FuUdevDevice
 * @subsystem: a subsystem
 *
 * Determine how far up a chain a given device is
 *
 * Returns: unsigned integer
 *
 * Since: 1.2.4
 **/
guint
fu_udev_device_get_slot_depth (FuUdevDevice *self, const gchar *subsystem)
{
#ifdef HAVE_GUDEV
	GUdevDevice *udev_device = fu_udev_device_get_dev (FU_UDEV_DEVICE (self));
	g_autoptr(GUdevDevice) device_tmp = NULL;

	device_tmp = g_udev_device_get_parent_with_subsystem (udev_device, subsystem, NULL);
	if (device_tmp == NULL)
		return 0;
	for (guint i = 0; i < 0xff; i++) {
		g_autoptr(GUdevDevice) parent = g_udev_device_get_parent (device_tmp);
		if (parent == NULL)
			return i;
		g_set_object (&device_tmp, parent);
	}
#endif
	return 0;
}

#ifdef HAVE_GUDEV
static gchar *
fu_udev_device_get_bind_id (FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	if (g_strcmp0 (fu_udev_device_get_subsystem (self), "pci") == 0)
		return g_strdup (g_udev_device_get_property (priv->udev_device, "PCI_SLOT_NAME"));
	if (g_strcmp0 (fu_udev_device_get_subsystem (self), "hid") == 0)
		return g_strdup (g_udev_device_get_property (priv->udev_device, "HID_PHYS"));
	if (g_strcmp0 (fu_udev_device_get_subsystem (self), "usb") == 0)
		return g_path_get_basename (g_udev_device_get_sysfs_path (priv->udev_device));
	return NULL;
}
#endif

static gboolean
fu_udev_device_unbind_driver (FuDevice *device, GError **error)
{
#ifdef HAVE_GUDEV
	FuUdevDevice *self = FU_UDEV_DEVICE (device);
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	g_autofree gchar *bind_id = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GOutputStream) stream = NULL;

	/* is already unbound */
	fn = g_build_filename (g_udev_device_get_sysfs_path (priv->udev_device),
			       "driver", "unbind", NULL);
	if (!g_file_test (fn, G_FILE_TEST_EXISTS))
		return TRUE;

	/* write bus ID to file */
	bind_id = fu_udev_device_get_bind_id (self);
	if (bind_id == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "bind-id not set for subsystem %s",
			     priv->subsystem);
		return FALSE;
	}
	file = g_file_new_for_path (fn);
	stream = G_OUTPUT_STREAM (g_file_replace (file, NULL, FALSE,
				  G_FILE_CREATE_NONE, NULL, error));
	if (stream == NULL)
		return FALSE;
	return g_output_stream_write_all (stream, bind_id, strlen (bind_id),
					  NULL, NULL, error);
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "driver unbinding not supported");
	return FALSE;
#endif
}

static gboolean
fu_udev_device_bind_driver (FuDevice *device,
			    const gchar *subsystem,
			    const gchar *driver,
			    GError **error)
{
#ifdef HAVE_GUDEV
	FuUdevDevice *self = FU_UDEV_DEVICE (device);
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	g_autofree gchar *bind_id = NULL;
	g_autofree gchar *driver_safe = g_strdup (driver);
	g_autofree gchar *fn = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GOutputStream) stream = NULL;

	/* copy the logic from modprobe */
	g_strdelimit (driver_safe, "-", '_');

	/* driver exists */
	fn = g_strdup_printf ("/sys/module/%s/drivers/%s:%s/bind",
			      driver_safe, subsystem, driver_safe);
	if (!g_file_test (fn, G_FILE_TEST_EXISTS)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "cannot bind with %s:%s",
			     subsystem, driver);
		return FALSE;
	}

	/* write bus ID to file */
	bind_id = fu_udev_device_get_bind_id (self);
	if (bind_id == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "bind-id not set for subsystem %s",
			     priv->subsystem);
		return FALSE;
	}
	file = g_file_new_for_path (fn);
	stream = G_OUTPUT_STREAM (g_file_replace (file, NULL, FALSE,
				  G_FILE_CREATE_NONE, NULL, error));
	if (stream == NULL)
		return FALSE;
	return g_output_stream_write_all (stream, bind_id, strlen (bind_id),
					  NULL, NULL, error);
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "driver binding not supported on Windows");
	return FALSE;
#endif
}

static void
fu_udev_device_incorporate (FuDevice *self, FuDevice *donor)
{
	FuUdevDevice *uself = FU_UDEV_DEVICE (self);
	FuUdevDevice *udonor = FU_UDEV_DEVICE (donor);
	FuUdevDevicePrivate *priv = GET_PRIVATE (uself);

	g_return_if_fail (FU_IS_UDEV_DEVICE (self));
	g_return_if_fail (FU_IS_UDEV_DEVICE (donor));

	fu_udev_device_set_dev (uself, fu_udev_device_get_dev (udonor));
	if (priv->device_file == NULL) {
		fu_udev_device_set_subsystem (uself, fu_udev_device_get_subsystem (udonor));
		fu_udev_device_set_device_file (uself, fu_udev_device_get_device_file (udonor));
	}
}

/**
 * fu_udev_device_get_dev:
 * @self: A #FuUdevDevice
 *
 * Gets the #GUdevDevice.
 *
 * Returns: (transfer none): a #GUdevDevice, or %NULL
 *
 * Since: 1.1.2
 **/
GUdevDevice *
fu_udev_device_get_dev (FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), NULL);
	return priv->udev_device;
}

/**
 * fu_udev_device_get_subsystem:
 * @self: A #FuUdevDevice
 *
 * Gets the device subsystem, e.g. "pci".
 *
 * Returns: a subsystem, or NULL if unset or invalid
 *
 * Since: 1.1.2
 **/
const gchar *
fu_udev_device_get_subsystem (FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), NULL);
	return priv->subsystem;
}

/**
 * fu_udev_device_get_driver:
 * @self: A #FuUdevDevice
 *
 * Gets the device driver, e.g. "psmouse".
 *
 * Returns: a subsystem, or NULL if unset or invalid
 *
 * Since: 1.5.3
 **/
const gchar *
fu_udev_device_get_driver (FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), NULL);
	return priv->driver;
}

/**
 * fu_udev_device_get_device_file:
 * @self: A #FuUdevDevice
 *
 * Gets the device node.
 *
 * Returns: a device file, or NULL if unset
 *
 * Since: 1.3.1
 **/
const gchar *
fu_udev_device_get_device_file (FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), NULL);
	return priv->device_file;
}

/**
 * fu_udev_device_get_sysfs_path:
 * @self: A #FuUdevDevice
 *
 * Gets the device sysfs path, e.g. "/sys/devices/pci0000:00/0000:00:14.0".
 *
 * Returns: a local path, or NULL if unset or invalid
 *
 * Since: 1.1.2
 **/
const gchar *
fu_udev_device_get_sysfs_path (FuUdevDevice *self)
{
#ifdef HAVE_GUDEV
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), NULL);
	if (priv->udev_device != NULL)
		return g_udev_device_get_sysfs_path (priv->udev_device);
#endif
	return NULL;
}

/**
 * fu_udev_device_get_number:
 * @self: A #FuUdevDevice
 *
 * Gets the device number, if any.
 *
 * Returns: integer, 0 if the data is unavailable, or %G_MAXUINT64 if the
 * feature is not available
 *
 * Since: 1.5.0
 **/
guint64
fu_udev_device_get_number (FuUdevDevice *self)
{
#ifdef HAVE_GUDEV
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), 0);
	if (priv->udev_device != NULL)
		return fu_common_strtoull (g_udev_device_get_number (priv->udev_device));
#endif
	return G_MAXUINT64;
}

/**
 * fu_udev_device_get_vendor:
 * @self: A #FuUdevDevice
 *
 * Gets the device vendor code.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 1.1.2
 **/
guint32
fu_udev_device_get_vendor (FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), 0x0000);
	return priv->vendor;
}

/**
 * fu_udev_device_get_model:
 * @self: A #FuUdevDevice
 *
 * Gets the device device code.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 1.1.2
 **/
guint32
fu_udev_device_get_model (FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), 0x0000);
	return priv->model;
}

/**
 * fu_udev_device_get_subsystem_vendor:
 * @self: A #FuUdevDevice
 *
 * Gets the device subsystem vendor code.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 1.5.0
 **/
guint32
fu_udev_device_get_subsystem_vendor (FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), 0x0000);
	return priv->subsystem_vendor;
}

/**
 * fu_udev_device_get_subsystem_model:
 * @self: A #FuUdevDevice
 *
 * Gets the device subsystem model code.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 1.5.0
 **/
guint32
fu_udev_device_get_subsystem_model (FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), 0x0000);
	return priv->subsystem_model;
}

/**
 * fu_udev_device_get_revision:
 * @self: A #FuUdevDevice
 *
 * Gets the device revision.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 1.1.2
 **/
guint8
fu_udev_device_get_revision (FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), 0x00);
	return priv->revision;
}

#ifdef HAVE_GUDEV
static gchar *
fu_udev_device_get_parent_subsystems (FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	GString *str = g_string_new (NULL);
	g_autoptr(GUdevDevice) udev_device = g_object_ref (priv->udev_device);

	/* find subsystems of self and all parent devices */
	if (priv->subsystem != NULL)
		g_string_append_printf (str, "%s,", priv->subsystem);
	while (TRUE) {
		g_autoptr(GUdevDevice) parent = g_udev_device_get_parent (udev_device);
		if (parent == NULL)
			break;
		if (g_udev_device_get_subsystem (parent) != NULL) {
			g_string_append_printf (str, "%s,",
						g_udev_device_get_subsystem (parent));
		}
		g_set_object (&udev_device, g_steal_pointer (&parent));
	}
	if (str->len > 0)
		g_string_truncate (str, str->len - 1);
	return g_string_free (str, FALSE);
}
#endif

/**
 * fu_udev_device_set_physical_id:
 * @self: A #FuUdevDevice
 * @subsystems: A subsystem string, e.g. `pci,usb`
 * @error: A #GError, or %NULL
 *
 * Sets the physical ID from the device subsystem. Plugins should choose the
 * subsystem that is "deepest" in the udev tree, for instance choosing 'usb'
 * over 'pci' for a mouse device.
 *
 * Returns: %TRUE if the physical device was set.
 *
 * Since: 1.1.2
 **/
gboolean
fu_udev_device_set_physical_id (FuUdevDevice *self, const gchar *subsystems, GError **error)
{
#ifdef HAVE_GUDEV
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	const gchar *subsystem = NULL;
	const gchar *tmp;
	g_autofree gchar *physical_id = NULL;
	g_auto(GStrv) split = NULL;
	g_autoptr(GUdevDevice) udev_device = NULL;

	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), FALSE);
	g_return_val_if_fail (subsystems != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* nothing to do */
	if (priv->udev_device == NULL)
		return TRUE;

	/* look for each subsystem in turn */
	split = g_strsplit (subsystems, ",", -1);
	for (guint i = 0; split[i] != NULL; i++) {
		subsystem = split[i];
		if (g_strcmp0 (priv->subsystem, subsystem) == 0) {
			udev_device = g_object_ref (priv->udev_device);
			break;
		}
		udev_device = g_udev_device_get_parent_with_subsystem (priv->udev_device,
								       subsystem, NULL);
		if (udev_device != NULL)
			break;
	}
	if (udev_device == NULL) {
		g_autofree gchar *str = fu_udev_device_get_parent_subsystems (self);
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_FOUND,
			     "failed to find device with subsystems %s, only got %s",
			     subsystems, str);
		return FALSE;
	}

	if (g_strcmp0 (subsystem, "pci") == 0) {
		tmp = g_udev_device_get_property (udev_device, "PCI_SLOT_NAME");
		if (tmp == NULL) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_NOT_FOUND,
					     "failed to find PCI_SLOT_NAME");
			return FALSE;
		}
		physical_id = g_strdup_printf ("PCI_SLOT_NAME=%s", tmp);
	} else if (g_strcmp0 (subsystem, "usb") == 0 ||
		   g_strcmp0 (subsystem, "mmc") == 0 ||
		   g_strcmp0 (subsystem, "i2c") == 0 ||
		   g_strcmp0 (subsystem, "platform") == 0 ||
		   g_strcmp0 (subsystem, "scsi") == 0) {
		tmp = g_udev_device_get_property (udev_device, "DEVPATH");
		if (tmp == NULL) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_NOT_FOUND,
					     "failed to find DEVPATH");
			return FALSE;
		}
		physical_id = g_strdup_printf ("DEVPATH=%s", tmp);
	} else if (g_strcmp0 (subsystem, "hid") == 0) {
		tmp = g_udev_device_get_property (udev_device, "HID_PHYS");
		if (tmp == NULL) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_NOT_FOUND,
					     "failed to find HID_PHYS");
			return FALSE;
		}
		physical_id = g_strdup_printf ("HID_PHYS=%s", tmp);
	} else if (g_strcmp0 (subsystem, "tpm") == 0 ||
		   g_strcmp0 (subsystem, "drm_dp_aux_dev") == 0) {
		tmp = g_udev_device_get_property (udev_device, "DEVNAME");
		if (tmp == NULL) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_NOT_FOUND,
					     "failed to find DEVPATH");
			return FALSE;
		}
		physical_id = g_strdup_printf ("DEVNAME=%s", tmp);
	} else {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "cannot handle subsystem %s",
			     subsystem);
		return FALSE;
	}

	/* success */
	fu_device_set_physical_id (FU_DEVICE (self), physical_id);
	return TRUE;
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Not supported as <gudev.h> is unavailable");
	return FALSE;
#endif
}

/**
 * fu_udev_device_set_logical_id:
 * @self: A #FuUdevDevice
 * @subsystem: A subsystem string, e.g. `pci,usb`
 * @error: A #GError, or %NULL
 *
 * Sets the logical ID from the device subsystem. Plugins should choose the
 * subsystem that most relevant in the udev tree, for instance choosing 'hid'
 * over 'usb' for a mouse device.
 *
 * Returns: %TRUE if the logical device was set.
 *
 * Since: 1.5.8
 **/
gboolean
fu_udev_device_set_logical_id (FuUdevDevice *self, const gchar *subsystem, GError **error)
{
#ifdef HAVE_GUDEV
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	const gchar *tmp;
	g_autofree gchar *logical_id = NULL;
	g_autoptr(GUdevDevice) udev_device = NULL;

	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), FALSE);
	g_return_val_if_fail (subsystem != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* nothing to do */
	if (priv->udev_device == NULL)
		return TRUE;

	/* find correct device matching subsystem */
	if (g_strcmp0 (priv->subsystem, subsystem) == 0) {
		udev_device = g_object_ref (priv->udev_device);
	} else {
		udev_device = g_udev_device_get_parent_with_subsystem (priv->udev_device,
								       subsystem, NULL);
	}
	if (udev_device == NULL) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_FOUND,
			     "failed to find device with subsystem %s",
			     subsystem);
		return FALSE;
	}

	/* query each subsystem */
	if (g_strcmp0 (subsystem, "hid") == 0) {
		tmp = g_udev_device_get_property (udev_device, "HID_UNIQ");
		if (tmp == NULL) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_NOT_FOUND,
					     "failed to find HID_UNIQ");
			return FALSE;
		}
		logical_id = g_strdup_printf ("HID_UNIQ=%s", tmp);
	} else {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "cannot handle subsystem %s",
			     subsystem);
		return FALSE;
	}

	/* success */
	fu_device_set_logical_id (FU_DEVICE (self), logical_id);
	return TRUE;
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Not supported as <gudev.h> is unavailable");
	return FALSE;
#endif
}

/**
 * fu_udev_device_get_fd:
 * @self: A #FuUdevDevice
 *
 * Gets the file descriptor if the device is open.
 *
 * Returns: positive integer, or -1 if the device is not open
 *
 * Since: 1.3.3
 **/
gint
fu_udev_device_get_fd (FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), -1);
	return priv->fd;
}

/**
 * fu_udev_device_set_fd:
 * @self: A #FuUdevDevice
 * @fd: A valid file descriptor
 *
 * Replace the file descriptor to use when the device has already been opened.
 * This object will automatically close() @fd when fu_device_close() is called.
 *
 * Since: 1.3.3
 **/
void
fu_udev_device_set_fd (FuUdevDevice *self, gint fd)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);

	g_return_if_fail (FU_IS_UDEV_DEVICE (self));
	if (priv->fd > 0)
		close (priv->fd);
	priv->fd = fd;
}

/**
 * fu_udev_device_set_readonly:
 * @self: A #FuUdevDevice
 * @readonly: %TRUE if the device file should be opened readonly
 *
 * Sets the open mode to `O_RDONLY` use when opening the device with
 * fu_device_open(). By default devices are opened with `O_RDWR`.
 *
 * Since: 1.3.3
 **/
void
fu_udev_device_set_readonly (FuUdevDevice *self, gboolean readonly)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_UDEV_DEVICE (self));
	priv->flags = readonly ? FU_UDEV_DEVICE_FLAG_OPEN_READ :
				 FU_UDEV_DEVICE_FLAG_OPEN_READ |
				 FU_UDEV_DEVICE_FLAG_OPEN_WRITE;
}

/**
 * fu_udev_device_set_flags:
 * @self: A #FuUdevDevice
 * @flags: a #FuUdevDeviceFlags, e.g. %FU_UDEV_DEVICE_FLAG_OPEN_READ
 *
 * Sets the parameters to use when opening the device.
 *
 * For example %FU_UDEV_DEVICE_FLAG_OPEN_READ means that fu_device_open()
 * would use `O_RDONLY` rather than `O_RDWR` which is the default.
 *
 * Since: 1.3.6
 **/
void
fu_udev_device_set_flags (FuUdevDevice *self, FuUdevDeviceFlags flags)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_UDEV_DEVICE (self));
	priv->flags = flags;

#ifdef HAVE_GUDEV
	/* overwrite */
	if (flags & FU_UDEV_DEVICE_FLAG_USE_CONFIG) {
		g_free (priv->device_file);
		priv->device_file = g_build_filename (g_udev_device_get_sysfs_path (priv->udev_device),
						      "config", NULL);
	}
#endif
}

static gboolean
fu_udev_device_open (FuDevice *device, GError **error)
{
	FuUdevDevice *self = FU_UDEV_DEVICE (device);
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	FuUdevDeviceClass *klass = FU_UDEV_DEVICE_GET_CLASS (device);

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
		priv->fd = g_open (priv->device_file, flags, 0);
		if (priv->fd < 0) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "failed to open %s: %s",
				     priv->device_file,
				     strerror (errno));
			return FALSE;
		}
	}

	/* subclassed */
	if (klass->open != NULL) {
		g_warning ("FuUdevDevice->open is deprecated!");
		if (!klass->open (self, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_udev_device_rescan (FuDevice *device, GError **error)
{
#ifdef HAVE_GUDEV
	FuUdevDevice *self = FU_UDEV_DEVICE (device);
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	const gchar *sysfs_path;
	g_autoptr(GUdevClient) udev_client = g_udev_client_new (NULL);
	g_autoptr(GUdevDevice) udev_device = NULL;

	/* never set */
	if (priv->udev_device == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "rescan with no previous device");
		return FALSE;
	}
	sysfs_path = g_udev_device_get_sysfs_path (priv->udev_device);
	udev_device = g_udev_client_query_by_sysfs_path (udev_client, sysfs_path);
	if (udev_device == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "rescan could not find device %s",
			     sysfs_path);
		return FALSE;
	}
	fu_udev_device_set_dev (self, udev_device);
	fu_device_probe_invalidate (device);
#endif
	return fu_device_probe (device, error);
}

static gboolean
fu_udev_device_close (FuDevice *device, GError **error)
{
	FuUdevDevice *self = FU_UDEV_DEVICE (device);
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	FuUdevDeviceClass *klass = FU_UDEV_DEVICE_GET_CLASS (device);

	/* subclassed */
	if (klass->close != NULL) {
		g_warning ("FuUdevDevice->close is deprecated!");
		if (!klass->close (self, error))
			return FALSE;
	}

	/* close device */
	if (priv->fd > 0) {
		if (!g_close (priv->fd, error))
			return FALSE;
		priv->fd = 0;
	}

	/* success */
	return TRUE;
}

/**
 * fu_udev_device_ioctl:
 * @self: A #FuUdevDevice
 * @request: request number
 * @buf: A buffer to use, which *must* be large enough for the request
 * @rc: (out) (allow-none): the raw return value from the ioctl
 * @error: A #GError, or %NULL
 *
 * Control a device using a low-level request.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.3.3
 **/
gboolean
fu_udev_device_ioctl (FuUdevDevice *self,
		      gulong request,
		      guint8 *buf,
		      gint *rc,
		      GError **error)
{
#ifdef HAVE_IOCTL_H
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	gint rc_tmp;

	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), FALSE);
	g_return_val_if_fail (request != 0x0, FALSE);
	g_return_val_if_fail (buf != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not open! */
	if (priv->fd == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "%s [%s] has not been opened",
			     fu_device_get_id (FU_DEVICE (self)),
			     fu_device_get_name (FU_DEVICE (self)));
		return FALSE;
	}

	rc_tmp = ioctl (priv->fd, request, buf);
	if (rc != NULL)
		*rc = rc_tmp;
	if (rc_tmp < 0) {
#ifdef HAVE_ERRNO_H
		if (errno == EPERM) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_PERMISSION_DENIED,
					     "permission denied");
			return FALSE;
		}
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "ioctl error: %s",
			     strerror (errno));
#else
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "unspecified ioctl error");
#endif
		return FALSE;
	}
	return TRUE;
#else
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_SUPPORTED,
		     "Not supported as <sys/ioctl.h> not found");
	return FALSE;
#endif
}

/**
 * fu_udev_device_pread_full:
 * @self: A #FuUdevDevice
 * @port: offset address
 * @buf: (in): data
 * @bufsz: size of @buf
 * @error: A #GError, or %NULL
 *
 * Read a buffer from a file descriptor at a given offset.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.5
 **/
gboolean
fu_udev_device_pread_full (FuUdevDevice *self, goffset port,
			   guint8 *buf, gsize bufsz,
			   GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);

	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), FALSE);
	g_return_val_if_fail (buf != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not open! */
	if (priv->fd == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "%s [%s] has not been opened",
			     fu_device_get_id (FU_DEVICE (self)),
			     fu_device_get_name (FU_DEVICE (self)));
		return FALSE;
	}

#ifdef HAVE_PWRITE
	if (pread (priv->fd, buf, bufsz, port) != (gssize) bufsz) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to read from port 0x%04x: %s",
			     (guint) port,
			     strerror (errno));
		return FALSE;
	}
	return TRUE;
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Not supported as pread() is unavailable");
	return FALSE;
#endif
}

/**
 * fu_udev_device_pwrite_full:
 * @self: A #FuUdevDevice
 * @port: offset address
 * @buf: (out): data
 * @bufsz: size of @data
 * @error: A #GError, or %NULL
 *
 * Write a buffer to a file descriptor at a given offset.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.5
 **/
gboolean
fu_udev_device_pwrite_full (FuUdevDevice *self, goffset port,
			    const guint8 *buf, gsize bufsz,
			    GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);

	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not open! */
	if (priv->fd == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "%s [%s] has not been opened",
			     fu_device_get_id (FU_DEVICE (self)),
			     fu_device_get_name (FU_DEVICE (self)));
		return FALSE;
	}

#ifdef HAVE_PWRITE
	if (pwrite (priv->fd, buf, bufsz, port) != (gssize) bufsz) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to write to port %04x: %s",
			     (guint) port,
			     strerror (errno));
		return FALSE;
	}
	return TRUE;
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Not supported as pwrite() is unavailable");
	return FALSE;
#endif
}

/**
 * fu_udev_device_pwrite:
 * @self: A #FuUdevDevice
 * @port: offset address
 * @data: value
 * @error: A #GError, or %NULL
 *
 * Write to a file descriptor at a given offset.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.3.3
 **/
gboolean
fu_udev_device_pwrite (FuUdevDevice *self, goffset port, guint8 data, GError **error)
{
	return fu_udev_device_pwrite_full (self, port, &data, 0x01, error);
}

/**
 * fu_udev_device_get_parent_name
 * @self: A #FuUdevDevice
 *
 * Returns the name of the direct ancestor of this device
 *
 * Returns: string or NULL if unset or invalid
 *
 * Since: 1.4.5
 **/
gchar *
fu_udev_device_get_parent_name (FuUdevDevice *self)
{
#ifdef HAVE_GUDEV
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GUdevDevice) parent = NULL;

	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), NULL);

	parent = g_udev_device_get_parent (priv->udev_device);
	return parent == NULL ? NULL : g_strdup (g_udev_device_get_name (parent));
#else
	return NULL;
#endif
}

/**
 * fu_udev_device_get_sysfs_attr:
 * @self: A #FuUdevDevice
 * @attr: name of attribute to get
 * @error: A #GError, or %NULL
 *
 * Reads an arbitrary sysfs attribute 'attr' associated with UDEV device
 *
 * Returns: string or NULL
 *
 * Since: 1.4.5
 **/
const gchar *
fu_udev_device_get_sysfs_attr (FuUdevDevice *self, const gchar *attr,
			       GError **error)
{
#ifdef HAVE_GUDEV
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	const gchar *result;

	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), NULL);
	g_return_val_if_fail (attr != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* nothing to do */
	if (priv->udev_device == NULL) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_FOUND,
				     "not yet initialized");
		return NULL;
	}
	result = g_udev_device_get_sysfs_attr (priv->udev_device, attr);
	if (result == NULL) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_FOUND,
			     "attribute %s returned no data",
			     attr);
		return NULL;
	}

	return result;
#else
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "not supported");
	return NULL;
#endif
}

/**
 * fu_udev_device_pread:
 * @self: A #FuUdevDevice
 * @port: offset address
 * @data: (out): value
 * @error: A #GError, or %NULL
 *
 * Read from a file descriptor at a given offset.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.3.3
 **/
gboolean
fu_udev_device_pread (FuUdevDevice *self, goffset port, guint8 *data, GError **error)
{
	return fu_udev_device_pread_full (self, port, data, 0x1, error);
}


/**
 * fu_udev_device_write_sysfs:
 * @self: A #FuUdevDevice
 * @attribute: sysfs attribute name
 * @val: data to write into the attribute
 * @error: A #GError, or %NULL
 *
 * Writes data into a sysfs attribute
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.5
 **/
gboolean
fu_udev_device_write_sysfs (FuUdevDevice *self, const gchar *attribute,
			    const gchar *val, GError **error)
{
#ifndef _WIN32
	ssize_t n;
	int r;
	int fd;
	g_autofree gchar *path = NULL;

	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), FALSE);
	g_return_val_if_fail (attribute != NULL, FALSE);
	g_return_val_if_fail (val != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	path = g_build_filename (fu_udev_device_get_sysfs_path (self),
				 attribute, NULL);
	fd = open (path, O_WRONLY | O_CLOEXEC);
	if (fd < 0) {
		g_set_error (error, G_IO_ERROR,
			     g_io_error_from_errno (errno),
			     "could not open %s: %s",
			     path,
			     g_strerror (errno));
		return FALSE;
	}

	do {
		n = write (fd, val, strlen (val));
		if (n < 1 && errno != EINTR) {
			g_set_error (error, G_IO_ERROR,
				     g_io_error_from_errno (errno),
				     "could not write to %s: %s",
				     path,
				     g_strerror (errno));
			(void) close (fd);
			return FALSE;
		}
	} while (n < 1);

	r = close (fd);
	if (r < 0 && errno != EINTR) {
		g_set_error (error, G_IO_ERROR,
			     g_io_error_from_errno (errno),
			     "could not close %s: %s",
			     path,
			     g_strerror (errno));
		return FALSE;
	}

	return TRUE;
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "sysfs attributes not supported on Windows");
	return FALSE;
#endif
}

/**
 * fu_udev_device_get_devtype
 * @self: A #FuUdevDevice
 *
 * Returns the Udev device type
 *
 * Returns: device type specified in the uevent
 *
 * Since: 1.4.5
 **/
const gchar *
fu_udev_device_get_devtype (FuUdevDevice *self)
{
#ifdef HAVE_GUDEV
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);

	return g_udev_device_get_devtype (priv->udev_device);
#else
	return NULL;
#endif
}

static void
fu_udev_device_get_property (GObject *object, guint prop_id,
			    GValue *value, GParamSpec *pspec)
{
	FuUdevDevice *self = FU_UDEV_DEVICE (object);
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	switch (prop_id) {
	case PROP_UDEV_DEVICE:
		g_value_set_object (value, priv->udev_device);
		break;
	case PROP_SUBSYSTEM:
		g_value_set_string (value, priv->subsystem);
		break;
	case PROP_DRIVER:
		g_value_set_string (value, priv->driver);
		break;
	case PROP_DEVICE_FILE:
		g_value_set_string (value, priv->device_file);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fu_udev_device_set_property (GObject *object, guint prop_id,
			     const GValue *value, GParamSpec *pspec)
{
	FuUdevDevice *self = FU_UDEV_DEVICE (object);
	switch (prop_id) {
	case PROP_UDEV_DEVICE:
		fu_udev_device_set_dev (self, g_value_get_object (value));
		break;
	case PROP_SUBSYSTEM:
		fu_udev_device_set_subsystem (self, g_value_get_string (value));
		break;
	case PROP_DRIVER:
		fu_udev_device_set_driver (self, g_value_get_string (value));
		break;
	case PROP_DEVICE_FILE:
		fu_udev_device_set_device_file (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fu_udev_device_finalize (GObject *object)
{
	FuUdevDevice *self = FU_UDEV_DEVICE (object);
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);

	g_free (priv->subsystem);
	g_free (priv->driver);
	g_free (priv->device_file);
	if (priv->udev_device != NULL)
		g_object_unref (priv->udev_device);
	if (priv->fd > 0)
		g_close (priv->fd, NULL);

	G_OBJECT_CLASS (fu_udev_device_parent_class)->finalize (object);
}

static void
fu_udev_device_init (FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	priv->flags = FU_UDEV_DEVICE_FLAG_OPEN_READ |
		      FU_UDEV_DEVICE_FLAG_OPEN_WRITE;
}

static void
fu_udev_device_class_init (FuUdevDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
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

	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	pspec = g_param_spec_object ("udev-device", NULL, NULL,
				     G_UDEV_TYPE_DEVICE,
				     G_PARAM_READWRITE |
				     G_PARAM_CONSTRUCT |
				     G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_UDEV_DEVICE, pspec);

	pspec = g_param_spec_string ("subsystem", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE |
				     G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_SUBSYSTEM, pspec);

	pspec = g_param_spec_string ("driver", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE |
				     G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_DRIVER, pspec);

	pspec = g_param_spec_string ("device-file", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE |
				     G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_DEVICE_FILE, pspec);
}

/**
 * fu_udev_device_new:
 * @udev_device: A #GUdevDevice
 *
 * Creates a new #FuUdevDevice.
 *
 * Returns: (transfer full): a #FuUdevDevice
 *
 * Since: 1.1.2
 **/
FuUdevDevice *
fu_udev_device_new (GUdevDevice *udev_device)
{
	FuUdevDevice *self = g_object_new (FU_TYPE_UDEV_DEVICE,
					   "udev-device", udev_device,
					   NULL);
	return FU_UDEV_DEVICE (self);
}
