/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <appstream-glib.h>

#include "fu-udev-device.h"

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
} FuUdevDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuUdevDevice, fu_udev_device, FU_TYPE_DEVICE)

enum {
	PROP_0,
	PROP_UDEV_DEVICE,
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
fu_udev_device_get_sysfs_attr_as_uint64 (FuUdevDevice *self, const gchar *name)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	return fu_common_strtoull (g_udev_device_get_sysfs_attr (priv->udev_device, name));
}

static gboolean
fu_udev_device_probe (FuDevice *device, GError **error)
{
	FuUdevDevice *self = FU_UDEV_DEVICE (device);
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	const gchar *tmp;
	g_autofree gchar *subsystem = NULL;

	/* set ven:dev:rev */
	priv->vendor = fu_udev_device_get_sysfs_attr_as_uint64 (self, "vendor");
	priv->model = fu_udev_device_get_sysfs_attr_as_uint64 (self, "device");
	priv->revision = fu_udev_device_get_sysfs_attr_as_uint64 (self, "revision");

	/* set the version if the revision has been set */
	if (priv->revision != 0x00) {
		g_autofree gchar *version = g_strdup_printf ("%02x", priv->revision);
		fu_device_set_version (device, version);
	}

	/* set model */
	tmp = g_udev_device_get_property (priv->udev_device, "FWUPD_MODEL");
	if (tmp == NULL)
		tmp = g_udev_device_get_property (priv->udev_device, "ID_MODEL_FROM_DATABASE");
	if (tmp != NULL)
		fu_device_set_name (device, tmp);

	/* set vendor */
	tmp = g_udev_device_get_property (priv->udev_device, "FWUPD_VENDOR");
	if (tmp == NULL)
		tmp = g_udev_device_get_property (priv->udev_device, "ID_VENDOR_FROM_DATABASE");
	if (tmp != NULL)
		fu_device_set_vendor (device, tmp);

	/* set vendor ID */
	subsystem = g_ascii_strup (fu_udev_device_get_subsystem (self), -1);
	if (subsystem != NULL) {
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
		fu_device_add_guid (device, devid);
	}
	if (priv->vendor != 0x0000 && priv->model != 0x0000) {
		g_autofree gchar *devid = NULL;
		devid = g_strdup_printf ("%s\\VEN_%04X&DEV_%04X",
					 subsystem, priv->vendor, priv->model);
		fu_device_add_guid (device, devid);
	}
	if (priv->vendor != 0x0000) {
		g_autofree gchar *devid = NULL;
		devid = g_strdup_printf ("%s\\VEN_%04X", subsystem, priv->vendor);
		fu_device_add_guid (device, devid);
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

	/* set udev platform ID automatically */
	fu_device_set_platform_id (FU_DEVICE (self),
				   g_udev_device_get_sysfs_path (udev_device));

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
 * @udev_device: A #GUdevDevice
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
	return g_udev_device_get_subsystem (priv->udev_device);
}

/**
 * fu_udev_device_get_vendor:
 * @udev_device: A #GUdevDevice
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
 * @udev_device: A #GUdevDevice
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
 * @udev_device: A #GUdevDevice
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
FuDevice *
fu_udev_device_new (GUdevDevice *udev_device)
{
	FuUdevDevice *self = g_object_new (FU_TYPE_UDEV_DEVICE,
					   "udev-device", udev_device,
					   NULL);
	return FU_DEVICE (self);
}
