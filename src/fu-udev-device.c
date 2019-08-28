/*
 * Copyright (C) 2017-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuUdevDevice"

#include "config.h"

#include <string.h>

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
	guint16			 vendor;
	guint16			 model;
	guint8			 revision;
	gchar			*subsystem;
	gchar			*device_file;
} FuUdevDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuUdevDevice, fu_udev_device, FU_TYPE_DEVICE)

#ifndef HAVE_GUDEV_232
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GUdevDevice, g_object_unref)
#pragma clang diagnostic pop
#endif

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

static guint64
fu_udev_device_get_sysfs_attr_as_uint64 (GUdevDevice *udev_device, const gchar *name)
{
	return fu_common_strtoull (g_udev_device_get_sysfs_attr (udev_device, name));
}

static guint16
fu_udev_device_read_uint16 (const gchar *str)
{
	gchar buf[5] = { 0x0, 0x0, 0x0, 0x0, 0x0 };
	memcpy (buf, str, 4);
	return (guint16) g_ascii_strtoull (buf, NULL, 16);
}

static void
fu_udev_device_dump_internal (GUdevDevice *udev_device)
{
#ifdef HAVE_GUDEV_232
	const gchar * const *keys;

	keys = g_udev_device_get_property_keys (udev_device);
	for (guint i = 0; keys[i] != NULL; i++) {
		g_debug ("%s={%s}", keys[i],
			 g_udev_device_get_property (udev_device, keys[i]));
	}
	keys = g_udev_device_get_sysfs_attr_keys (udev_device);
	for (guint i = 0; keys[i] != NULL; i++) {
		g_debug ("%s=[%s]", keys[i],
			 g_udev_device_get_sysfs_attr (udev_device, keys[i]));
	}
#endif
}

void
fu_udev_device_dump (FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	if (priv->udev_device == NULL)
		return;
	fu_udev_device_dump_internal (priv->udev_device);
}

static gboolean
fu_udev_device_probe (FuDevice *device, GError **error)
{
	FuUdevDeviceClass *klass = FU_UDEV_DEVICE_GET_CLASS (device);
	FuUdevDevice *self = FU_UDEV_DEVICE (device);
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	const gchar *tmp;
	g_autofree gchar *subsystem = NULL;
	g_autoptr(GUdevDevice) udev_parent = NULL;

	/* nothing to do */
	if (priv->udev_device == NULL)
		return TRUE;

	/* set ven:dev:rev */
	priv->vendor = fu_udev_device_get_sysfs_attr_as_uint64 (priv->udev_device, "vendor");
	priv->model = fu_udev_device_get_sysfs_attr_as_uint64 (priv->udev_device, "device");
	priv->revision = fu_udev_device_get_sysfs_attr_as_uint64 (priv->udev_device, "revision");

	/* fallback to the parent */
	udev_parent = g_udev_device_get_parent (priv->udev_device);
	if (udev_parent != NULL &&
	    priv->vendor == 0x0 && priv->model == 0x0 && priv->revision == 0x0) {
		priv->vendor = fu_udev_device_get_sysfs_attr_as_uint64 (udev_parent, "vendor");
		priv->model = fu_udev_device_get_sysfs_attr_as_uint64 (udev_parent, "device");
		priv->revision = fu_udev_device_get_sysfs_attr_as_uint64 (udev_parent, "revision");
	}

	/* hidraw helpfully encodes the information in a different place */
	if (udev_parent != NULL &&
	    priv->vendor == 0x0 && priv->model == 0x0 && priv->revision == 0x0 &&
	    g_strcmp0 (priv->subsystem, "hidraw") == 0) {
		tmp = g_udev_device_get_property (udev_parent, "HID_ID");
		if (tmp != NULL && strlen (tmp) == 22) {
			priv->vendor = fu_udev_device_read_uint16 (tmp + 10);
			priv->model = fu_udev_device_read_uint16 (tmp + 18);
		}
		tmp = g_udev_device_get_property (udev_parent, "HID_NAME");
		if (tmp != NULL) {
			g_auto(GStrv) vm = g_strsplit (tmp, " ", 2);
			if (g_strv_length (vm) == 2) {
				if (fu_device_get_vendor (device) == NULL)
					fu_device_set_vendor (device, vm[0]);
				if (fu_device_get_name (device) == NULL)
					fu_device_set_name (device, vm[1]);
			}
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
	if (priv->vendor != 0x0000 && priv->model != 0x0000 && priv->revision != 0x00) {
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

	g_return_if_fail (FU_IS_UDEV_DEVICE (self));

	/* set new device */
	g_set_object (&priv->udev_device, udev_device);
	if (priv->udev_device == NULL)
		return;
	priv->subsystem = g_strdup (g_udev_device_get_subsystem (priv->udev_device));
	priv->device_file = g_strdup (g_udev_device_get_device_file (priv->udev_device));
}

guint
fu_udev_device_get_slot_depth (FuUdevDevice *self, const gchar *subsystem)
{
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
 * @self: A #GUdevDevice
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
 * @self: A #GUdevDevice
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
 * @self: A #GUdevDevice
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
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), NULL);
	if (priv->udev_device == NULL)
		return NULL;
	return g_udev_device_get_sysfs_path (priv->udev_device);
}

/**
 * fu_udev_device_get_vendor:
 * @self: A #GUdevDevice
 *
 * Gets the device vendor code.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 1.1.2
 **/
guint16
fu_udev_device_get_vendor (FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), 0x0000);
	return priv->vendor;
}

/**
 * fu_udev_device_get_model:
 * @self: A #GUdevDevice
 *
 * Gets the device device code.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 1.1.2
 **/
guint16
fu_udev_device_get_model (FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), 0x0000);
	return priv->model;
}

/**
 * fu_udev_device_get_revision:
 * @self: A #GUdevDevice
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

static GString *
fu_udev_device_get_parent_subsystems (FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	GString *str = g_string_new (NULL);
	g_autoptr(GUdevDevice) udev_device = g_object_ref (priv->udev_device);

	/* find subsystems of all parent devices */
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
	return str;
}

/**
 * fu_udev_device_set_physical_id:
 * @self: A #GUdevDevice
 * @subsystem: A subsystem string, e.g. `usb`
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
fu_udev_device_set_physical_id (FuUdevDevice *self, const gchar *subsystem, GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	const gchar *tmp;
	g_autofree gchar *physical_id = NULL;
	g_autoptr(GUdevDevice) udev_device = NULL;

	g_return_val_if_fail (FU_IS_UDEV_DEVICE (self), FALSE);
	g_return_val_if_fail (subsystem != NULL, FALSE);

	/* nothing to do */
	if (priv->udev_device == NULL)
		return TRUE;

	/* get the correct device */
	if (g_strcmp0 (priv->subsystem, subsystem) == 0) {
		udev_device = g_object_ref (priv->udev_device);
	} else {
		udev_device = g_udev_device_get_parent_with_subsystem (priv->udev_device,
								       subsystem, NULL);
		if (udev_device == NULL) {
			g_autoptr(GString) str = NULL;
			str = fu_udev_device_get_parent_subsystems (self);
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_FOUND,
				     "failed to find device with subsystem %s, only got %s",
				     subsystem, str->str);
			return FALSE;
		}
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

	G_OBJECT_CLASS (fu_udev_device_parent_class)->finalize (object);
}

static void
fu_udev_device_init (FuUdevDevice *self)
{
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
