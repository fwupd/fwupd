/*
 * Copyright (C) 2017-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuUdevDevice"

#include "config.h"

#include <fcntl.h>
#include <string.h>
#ifdef HAVE_ERRNO_H
#include <sys/errno.h>
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
	guint8			 revision;
	gchar			*subsystem;
	gchar			*device_file;
	gint			 fd;
	FuUdevDeviceFlags	 flags;
} FuUdevDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuUdevDevice, fu_udev_device, FU_TYPE_DEVICE)

enum {
	PROP_0,
	PROP_UDEV_DEVICE,
	PROP_SUBSYSTEM,
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
	g_return_if_fail (FU_IS_UDEV_DEVICE (self));
	g_debug ("FuUdevDevice emit changed");
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
#ifdef HAVE_GUDEV
	FuUdevDevice *self = FU_UDEV_DEVICE (device);
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GUdevDevice) udev_parent = NULL;

	if (priv->udev_device == NULL)
		return;

	if (g_getenv ("FU_UDEV_DEVICE_DEBUG") == NULL)
		return;

	fu_udev_device_to_string_raw (priv->udev_device, idt, str);
	udev_parent = g_udev_device_get_parent (priv->udev_device);
	if (udev_parent != NULL) {
		fu_common_string_append_kv (str, idt, "Parent", NULL);
		fu_udev_device_to_string_raw (udev_parent, idt + 1, str);
	}
#endif
}

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

#ifdef HAVE_GUDEV
	/* fallback to the parent */
	udev_parent = g_udev_device_get_parent (priv->udev_device);
	if (udev_parent != NULL &&
	    priv->flags & FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT &&
	    priv->vendor == 0x0 && priv->model == 0x0 && priv->revision == 0x0) {
		priv->vendor = fu_udev_device_get_sysfs_attr_as_uint32 (udev_parent, "vendor");
		priv->model = fu_udev_device_get_sysfs_attr_as_uint32 (udev_parent, "device");
		priv->revision = fu_udev_device_get_sysfs_attr_as_uint8 (udev_parent, "revision");
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

	/* try harder to find a vendor name the user will recognise */
	if (priv->flags & FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT &&
	    udev_parent != NULL && fu_device_get_vendor (device) == NULL) {
		g_autoptr(GUdevDevice) device_tmp = g_object_ref (udev_parent);
		for (guint i = 0; i < 0xff; i++) {
			g_autoptr(GUdevDevice) parent = NULL;
			const gchar *id_vendor;
			id_vendor = g_udev_device_get_property (device_tmp,
								"ID_VENDOR_FROM_DATABASE");
			if (id_vendor != NULL) {
				fu_device_set_vendor (device, id_vendor);
				break;
			}
			parent = g_udev_device_get_parent (device_tmp);
			if (parent == NULL)
				break;
			g_set_object (&device_tmp, parent);
		}
	}

	/* set the version if the revision has been set */
	if (fu_device_get_version (device) == NULL) {
		if (priv->revision != 0x00) {
			g_autofree gchar *version = g_strdup_printf ("%02x", priv->revision);
			fu_device_set_version (device, version, FWUPD_VERSION_FORMAT_PLAIN);
		}
	}

	/* set model */
	if (fu_device_get_name (device) == NULL) {
		tmp = g_udev_device_get_property (priv->udev_device, "FWUPD_MODEL");
		if (tmp == NULL)
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
		tmp = g_udev_device_get_property (priv->udev_device, "FWUPD_VENDOR");
		if (tmp == NULL)
			tmp = g_udev_device_get_property (priv->udev_device, "ID_VENDOR_FROM_DATABASE");
		if (tmp == NULL)
			tmp = g_udev_device_get_property (priv->udev_device, "ID_VENDOR");
		if (tmp != NULL)
			fu_device_set_vendor (device, tmp);
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
	if (fu_device_get_version (device) == NULL) {
		tmp = g_udev_device_get_property (priv->udev_device, "ID_REVISION");
		if (tmp != NULL)
			fu_device_set_version (device, tmp, FWUPD_VERSION_FORMAT_UNKNOWN);
	}

	/* set vendor ID */
	subsystem = g_ascii_strup (g_udev_device_get_subsystem (priv->udev_device), -1);
	if (subsystem != NULL && priv->vendor != 0x0000) {
		g_autofree gchar *vendor_id = NULL;
		vendor_id = g_strdup_printf ("%s:0x%04X", subsystem, (guint) priv->vendor);
		fu_device_set_vendor_id (device, vendor_id);
	}

	/* add GUIDs in order of priority */
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

	/* add subsystem to match in plugins */
	if (subsystem != NULL) {
		fu_device_add_instance_id_full (device, subsystem,
						FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS);
	}

	/* determine if we're wired internally */
	parent_i2c = g_udev_device_get_parent_with_subsystem (priv->udev_device,
							      "i2c", NULL);
	if (parent_i2c != NULL)
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_INTERNAL);
#endif

	/* subclassed */
	if (klass->probe != NULL) {
		if (!klass->probe (self, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_udev_device_set_dev (FuUdevDevice *self, GUdevDevice *udev_device)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
#ifdef HAVE_GUDEV
	const gchar *summary;
	g_autoptr(GUdevDevice) parent = NULL;
#endif

	g_return_if_fail (FU_IS_UDEV_DEVICE (self));

	/* set new device */
	g_set_object (&priv->udev_device, udev_device);
	if (priv->udev_device == NULL)
		return;
#ifdef HAVE_GUDEV
	priv->subsystem = g_strdup (g_udev_device_get_subsystem (priv->udev_device));
	priv->device_file = g_strdup (g_udev_device_get_device_file (priv->udev_device));

	/* try to get one line summary */
	summary = g_udev_device_get_sysfs_attr (priv->udev_device, "description");
	if (summary == NULL) {
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
		priv->subsystem = g_strdup (fu_udev_device_get_subsystem (udonor));
		priv->device_file = g_strdup (fu_udev_device_get_device_file (udonor));
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
		if (!klass->open (self, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_udev_device_close (FuDevice *device, GError **error)
{
	FuUdevDevice *self = FU_UDEV_DEVICE (device);
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	FuUdevDeviceClass *klass = FU_UDEV_DEVICE_GET_CLASS (device);

	/* subclassed */
	if (klass->close != NULL) {
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
	g_return_val_if_fail (priv->fd > 0, FALSE);

	rc_tmp = ioctl (priv->fd, request, buf);
	if (rc != NULL)
		*rc = rc_tmp;
	if (rc_tmp < 0) {
		if (rc_tmp == -EPERM) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_PERMISSION_DENIED,
					     "permission denied");
			return FALSE;
		}
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "ioctl not supported: %s",
			     strerror (errno));
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
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);

	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), FALSE);
	g_return_val_if_fail (port != 0x0, FALSE);
	g_return_val_if_fail (priv->fd > 0, FALSE);

#ifdef HAVE_PWRITE
	if (pwrite (priv->fd, &data, 1, port) != 1) {
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
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);

	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), FALSE);
	g_return_val_if_fail (port != 0x0, FALSE);
	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (priv->fd > 0, FALSE);

#ifdef HAVE_PWRITE
	if (pread (priv->fd, data, 1, port) != 1) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to read from port %04x: %s",
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
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	switch (prop_id) {
	case PROP_UDEV_DEVICE:
		fu_udev_device_set_dev (self, g_value_get_object (value));
		break;
	case PROP_SUBSYSTEM:
		priv->subsystem = g_value_dup_string (value);
		break;
	case PROP_DEVICE_FILE:
		priv->device_file = g_value_dup_string (value);
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
	device_class->incorporate = fu_udev_device_incorporate;
	device_class->open = fu_udev_device_open;
	device_class->close = fu_udev_device_close;
	device_class->to_string = fu_udev_device_to_string;

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
