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

static void
fu_udev_device_apply_quirks (FuUdevDevice *self)
{
	FuQuirks *quirks = fu_device_get_quirks (FU_DEVICE (self));
	GUdevDevice *udev_device = fu_udev_device_get_dev (self);
	const gchar *tmp;

	/* not set */
	if (quirks == NULL)
		return;

	/* flags */
	tmp = fu_quirks_lookup_by_udev_device (quirks, udev_device, FU_QUIRKS_FLAGS);
	if (tmp != NULL)
		fu_device_set_custom_flags (FU_DEVICE (self), tmp);

	/* name */
	tmp = fu_quirks_lookup_by_udev_device (quirks, udev_device, FU_QUIRKS_NAME);
	if (tmp != NULL)
		fu_device_set_name (FU_DEVICE (self), tmp);

	/* summary */
	tmp = fu_quirks_lookup_by_udev_device (quirks, udev_device, FU_QUIRKS_SUMMARY);
	if (tmp != NULL)
		fu_device_set_summary (FU_DEVICE (self), tmp);

	/* vendor */
	tmp = fu_quirks_lookup_by_udev_device (quirks, udev_device, FU_QUIRKS_VENDOR);
	if (tmp != NULL)
		fu_device_set_vendor (FU_DEVICE (self), tmp);

	/* version */
	tmp = fu_quirks_lookup_by_udev_device (quirks, udev_device, FU_QUIRKS_VERSION);
	if (tmp != NULL)
		fu_device_set_version (FU_DEVICE (self), tmp);

	/* icon */
	tmp = fu_quirks_lookup_by_udev_device (quirks, udev_device, FU_QUIRKS_ICON);
	if (tmp != NULL)
		fu_device_add_icon (FU_DEVICE (self), tmp);

	/* GUID */
	tmp = fu_quirks_lookup_by_udev_device (quirks, udev_device, FU_QUIRKS_GUID);
	if (tmp != NULL)
		fu_device_add_guid (FU_DEVICE (self), tmp);
}

static void
fu_udev_device_notify_quirks_cb (FuUdevDevice *self, GParamSpec *pspec, gpointer user_data)
{
	fu_udev_device_apply_quirks (self);
}

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
	const gchar *str;
	guint base = 10;

	str = g_udev_device_get_sysfs_attr (priv->udev_device, name);
	if (str == NULL)
		return 0;
	if (g_str_has_prefix (str, "0x")) {
		str += 2;
		base = 16;
	}
	return (guint16) g_ascii_strtoull (str, NULL, base);
}

static void
fu_udev_device_set_dev (FuUdevDevice *self, GUdevDevice *udev_device)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE (self);
	const gchar *tmp;
	g_autofree gchar *devid1 = NULL;
	g_autofree gchar *devid2 = NULL;
	g_autofree gchar *subsystem = NULL;

	g_return_if_fail (FU_IS_UDEV_DEVICE (self));

	/* set new device */
	g_set_object (&priv->udev_device, udev_device);
	if (priv->udev_device == NULL)
		return;

	/* set ven:dev:rev */
	priv->vendor = fu_udev_device_get_sysfs_attr_as_uint64 (self, "vendor");
	priv->model = fu_udev_device_get_sysfs_attr_as_uint64 (self, "device");
	priv->revision = fu_udev_device_get_sysfs_attr_as_uint64 (self, "revision");

	/* add both device IDs */
	subsystem = g_ascii_strup (fu_udev_device_get_subsystem (self), -1);
	devid1 = g_strdup_printf ("%s\\VEN_%04X&DEV_%04X",
				  subsystem, priv->vendor, priv->model);
	fu_device_add_guid (FU_DEVICE (self), devid1);
	devid2 = g_strdup_printf ("%s\\VEN_%04X&DEV_%04X&REV_%02X",
				  subsystem, priv->vendor, priv->model, priv->revision);
	fu_device_add_guid (FU_DEVICE (self), devid2);

	/* set the version if the revision has been set */
	if (priv->revision != 0x00) {
		g_autofree gchar *version = g_strdup_printf ("%02x", priv->revision);
		fu_device_set_version (FU_DEVICE (self), version);
	}

	/* set model */
	tmp = g_udev_device_get_property (udev_device, "FWUPD_MODEL");
	if (tmp == NULL)
		tmp = g_udev_device_get_property (udev_device, "ID_MODEL_FROM_DATABASE");
	if (tmp != NULL)
		fu_device_set_name (FU_DEVICE (self), tmp);

	/* set vendor */
	tmp = g_udev_device_get_property (udev_device, "FWUPD_VENDOR");
	if (tmp == NULL)
		tmp = g_udev_device_get_property (udev_device, "ID_VENDOR_FROM_DATABASE");
	if (tmp != NULL)
		fu_device_set_vendor (FU_DEVICE (self), tmp);

	/* set vendor ID */
	tmp = g_udev_device_get_subsystem (udev_device);
	if (tmp != NULL) {
		g_autofree gchar *subsys = g_ascii_strup (tmp, -1);
		g_autofree gchar *vendor_id = NULL;
		vendor_id = g_strdup_printf ("%s:0x%04X", subsys, (guint) priv->vendor);
		fu_device_set_vendor_id (FU_DEVICE (self), vendor_id);
	}

	/* set udev platform ID automatically */
	fu_device_set_platform_id (FU_DEVICE (self),
				   g_udev_device_get_sysfs_path (udev_device));

	/* set the quirks again */
	fu_udev_device_apply_quirks (self);
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
	g_signal_connect (self, "notify::quirks",
			  G_CALLBACK (fu_udev_device_notify_quirks_cb), NULL);
}

static void
fu_udev_device_class_init (FuUdevDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamSpec *pspec;

	object_class->finalize = fu_udev_device_finalize;
	object_class->get_property = fu_udev_device_get_property;
	object_class->set_property = fu_udev_device_set_property;

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
