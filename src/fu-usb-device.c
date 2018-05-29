/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <appstream-glib.h>

#include "fu-usb-device.h"

/**
 * SECTION:fu-device
 * @short_description: a USB device
 *
 * An object that represents a USB device.
 *
 * See also: #FuDevice
 */

typedef struct
{
	GUsbDevice		*usb_device;
	FuDeviceLocker		*usb_device_locker;
	gboolean		 done_probe;
} FuUsbDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuUsbDevice, fu_usb_device, FU_TYPE_DEVICE)
enum {
	PROP_0,
	PROP_USB_DEVICE,
	PROP_LAST
};

#define GET_PRIVATE(o) (fu_usb_device_get_instance_private (o))

static void
fu_usb_device_apply_quirks (FuUsbDevice *device)
{
	FuQuirks *quirks = fu_device_get_quirks (FU_DEVICE (device));
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);
	const gchar *type_name = G_OBJECT_TYPE_NAME (device);
	const gchar *tmp;

	/* not set */
	if (quirks == NULL)
		return;

	/* type */
	g_debug ("looking for USB quirks for %s type", type_name);
	tmp = fu_quirks_lookup_by_usb_device (quirks, type_name, usb_device);
	if (tmp != NULL) {
		g_debug ("default plugin hints set to: %s", tmp);
		fu_device_set_plugin_hints (FU_DEVICE (device), tmp);
	}

	/* name */
	g_debug ("looking for USB quirks for %s device",
		 fu_device_get_platform_id (FU_DEVICE (device)));
	tmp = fu_quirks_lookup_by_usb_device (quirks, FU_QUIRKS_USB_NAME, usb_device);
	if (tmp != NULL)
		fu_device_set_name (FU_DEVICE (device), tmp);

	/* summary */
	tmp = fu_quirks_lookup_by_usb_device (quirks, FU_QUIRKS_USB_SUMMARY, usb_device);
	if (tmp != NULL)
		fu_device_set_summary (FU_DEVICE (device), tmp);

	/* vendor */
	tmp = fu_quirks_lookup_by_usb_device (quirks, FU_QUIRKS_USB_VENDOR, usb_device);
	if (tmp != NULL)
		fu_device_set_vendor (FU_DEVICE (device), tmp);

	/* version */
	tmp = fu_quirks_lookup_by_usb_device (quirks, FU_QUIRKS_USB_VERSION, usb_device);
	if (tmp != NULL)
		fu_device_set_version (FU_DEVICE (device), tmp);

	/* icon */
	tmp = fu_quirks_lookup_by_usb_device (quirks, FU_QUIRKS_USB_ICON, usb_device);
	if (tmp != NULL)
		fu_device_add_icon (FU_DEVICE (device), tmp);

	/* GUID */
	tmp = fu_quirks_lookup_by_usb_device (quirks, FU_QUIRKS_USB_GUID, usb_device);
	if (tmp != NULL)
		fu_device_add_guid (FU_DEVICE (device), tmp);
}

static void
fu_usb_device_notify_quirks_cb (FuUsbDevice *device, GParamSpec *pspec, gpointer user_data)
{
	fu_usb_device_apply_quirks (device);
}

static void
fu_usb_device_get_property (GObject *object, guint prop_id,
			    GValue *value, GParamSpec *pspec)
{
	FuUsbDevice *device = FU_USB_DEVICE (object);
	FuUsbDevicePrivate *priv = GET_PRIVATE (device);
	switch (prop_id) {
	case PROP_USB_DEVICE:
		g_value_set_object (value, priv->usb_device);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fu_usb_device_set_property (GObject *object, guint prop_id,
			    const GValue *value, GParamSpec *pspec)
{
	FuUsbDevice *device = FU_USB_DEVICE (object);
	switch (prop_id) {
	case PROP_USB_DEVICE:
		fu_usb_device_set_dev (device, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fu_usb_device_finalize (GObject *object)
{
	FuUsbDevice *device = FU_USB_DEVICE (object);
	FuUsbDevicePrivate *priv = GET_PRIVATE (device);

	if (priv->usb_device_locker != NULL)
		g_object_unref (priv->usb_device_locker);
	if (priv->usb_device != NULL)
		g_object_unref (priv->usb_device);

	G_OBJECT_CLASS (fu_usb_device_parent_class)->finalize (object);
}

static void
fu_usb_device_init (FuUsbDevice *device)
{
	g_signal_connect (device, "notify::quirks",
			  G_CALLBACK (fu_usb_device_notify_quirks_cb), NULL);
}

static void
fu_usb_device_class_init (FuUsbDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamSpec *pspec;

	object_class->finalize = fu_usb_device_finalize;
	object_class->get_property = fu_usb_device_get_property;
	object_class->set_property = fu_usb_device_set_property;

	pspec = g_param_spec_object ("usb-device", NULL, NULL,
				     G_USB_TYPE_DEVICE,
				     G_PARAM_READWRITE |
				     G_PARAM_CONSTRUCT |
				     G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_USB_DEVICE, pspec);
}

/**
 * fu_usb_device_is_open:
 * @device: A #FuUsbDevice
 *
 * Finds out if a USB device is currently open.
 *
 * Returns: %TRUE if the device is open.
 *
 * Since: 1.0.3
 **/
gboolean
fu_usb_device_is_open (FuUsbDevice *device)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (FU_IS_USB_DEVICE (device), FALSE);
	return priv->usb_device_locker != NULL;
}

/**
 * fu_usb_device_open:
 * @device: A #FuUsbDevice
 * @error: A #GError, or %NULL
 *
 * Opens a USB device, optionally running a object-specific vfunc.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.0.2
 **/
gboolean
fu_usb_device_open (FuUsbDevice *device, GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE (device);
	FuUsbDeviceClass *klass = FU_USB_DEVICE_GET_CLASS (device);
	guint idx;
	g_autoptr(AsProfile) profile = as_profile_new ();
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	g_return_val_if_fail (FU_IS_USB_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already open */
	if (priv->usb_device_locker != NULL)
		return TRUE;

	/* probe */
	if (!fu_usb_device_probe (device, error))
		return FALSE;

	/* profile */
	ptask = as_profile_start (profile, "added{%04x:%04x}",
				  g_usb_device_get_vid (priv->usb_device),
				  g_usb_device_get_pid (priv->usb_device));
	g_assert (ptask != NULL);

	/* open */
	locker = fu_device_locker_new (priv->usb_device, error);
	if (locker == NULL)
		return FALSE;

	/* get vendor */
	if (fu_device_get_vendor (FU_DEVICE (device)) == NULL) {
		idx = g_usb_device_get_manufacturer_index (priv->usb_device);
		if (idx != 0x00) {
			g_autofree gchar *tmp = NULL;
			tmp = g_usb_device_get_string_descriptor (priv->usb_device,
								  idx, error);
			if (tmp == NULL)
				return FALSE;
			fu_device_set_vendor (FU_DEVICE (device), tmp);
		}
	}

	/* get product */
	if (fu_device_get_name (FU_DEVICE (device)) == NULL) {
		idx = g_usb_device_get_product_index (priv->usb_device);
		if (idx != 0x00) {
			g_autofree gchar *tmp = NULL;
			tmp = g_usb_device_get_string_descriptor (priv->usb_device,
								  idx, error);
			if (tmp == NULL)
				return FALSE;
			fu_device_set_name (FU_DEVICE (device), tmp);
		}
	}

	/* get serial number */
	if (fu_device_get_serial (FU_DEVICE (device)) == NULL) {
		idx = g_usb_device_get_serial_number_index (priv->usb_device);
		if (idx != 0x00) {
			g_autofree gchar *tmp = NULL;
			tmp = g_usb_device_get_string_descriptor (priv->usb_device,
								  idx, error);
			if (tmp == NULL)
				return FALSE;
			fu_device_set_serial (FU_DEVICE (device), tmp);
		}
	}

	/* get version number, falling back to the USB device release */
	idx = g_usb_device_get_custom_index (priv->usb_device,
					     G_USB_DEVICE_CLASS_VENDOR_SPECIFIC,
					     'F', 'W', NULL);
	if (idx != 0x00) {
		g_autofree gchar *tmp = NULL;
		tmp = g_usb_device_get_string_descriptor (priv->usb_device, idx, NULL);
		fu_device_set_version (FU_DEVICE (device), tmp);
	}

	/* get GUID from the descriptor if set */
	idx = g_usb_device_get_custom_index (priv->usb_device,
					     G_USB_DEVICE_CLASS_VENDOR_SPECIFIC,
					     'G', 'U', NULL);
	if (idx != 0x00) {
		g_autofree gchar *tmp = NULL;
		tmp = g_usb_device_get_string_descriptor (priv->usb_device, idx, NULL);
		fu_device_add_guid (FU_DEVICE (device), tmp);
	}

	/* subclassed */
	if (klass->open != NULL) {
		if (!klass->open (device, error))
			return FALSE;
	}

	/* success */
	priv->usb_device_locker = g_steal_pointer (&locker);
	return TRUE;
}

/**
 * fu_usb_device_open:
 * @device: A #FuUsbDevice
 * @error: A #GError, or %NULL
 *
 * Closes a USB device, optionally running a object-specific vfunc.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.0.2
 **/
gboolean
fu_usb_device_close (FuUsbDevice *device, GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE (device);
	FuUsbDeviceClass *klass = FU_USB_DEVICE_GET_CLASS (device);

	g_return_val_if_fail (FU_IS_USB_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already open */
	if (priv->usb_device_locker == NULL)
		return TRUE;

	/* subclassed */
	if (klass->close != NULL) {
		if (!klass->close (device, error))
			return FALSE;
	}

	g_clear_object (&priv->usb_device_locker);
	return TRUE;
}

/**
 * fu_usb_device_probe:
 * @device: A #FuUsbDevice
 * @error: A #GError, or %NULL
 *
 * Probes a USB device, setting parameters on the object that does not need
 * the device open or the interface claimed.
 * If the device is not compatible then an error should be returned.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.0.2
 **/
gboolean
fu_usb_device_probe (FuUsbDevice *device, GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE (device);
	FuUsbDeviceClass *klass = FU_USB_DEVICE_GET_CLASS (device);

	g_return_val_if_fail (FU_IS_USB_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already done */
	if (priv->done_probe)
		return TRUE;

	/* subclassed */
	if (klass->probe != NULL) {
		if (!klass->probe (device, error))
			return FALSE;
	}
	priv->done_probe = TRUE;
	return TRUE;
}

/**
 * fu_usb_device_set_dev:
 * @device: A #FuUsbDevice
 * @usb_device: A #GUsbDevice, or %NULL
 *
 * Sets the #GUsbDevice to use.
 *
 * Since: 1.0.2
 **/
void
fu_usb_device_set_dev (FuUsbDevice *device, GUsbDevice *usb_device)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE (device);
	guint16 release;
	g_autofree gchar *devid1 = NULL;
	g_autofree gchar *devid2 = NULL;
	g_autofree gchar *vendor_id = NULL;

	g_return_if_fail (FU_IS_USB_DEVICE (device));

	/* need to re-probe hardware */
	priv->done_probe = FALSE;

	/* allow replacement */
	g_set_object (&priv->usb_device, usb_device);
	if (usb_device == NULL) {
		g_clear_object (&priv->usb_device_locker);
		return;
	}

	/* add both device IDs */
	devid1 = g_strdup_printf ("USB\\VID_%04X&PID_%04X",
				  g_usb_device_get_vid (usb_device),
				  g_usb_device_get_pid (usb_device));
	fu_device_add_guid (FU_DEVICE (device), devid1);
	release = g_usb_device_get_release (usb_device);
	devid2 = g_strdup_printf ("USB\\VID_%04X&PID_%04X&REV_%04X",
				  g_usb_device_get_vid (usb_device),
				  g_usb_device_get_pid (usb_device),
				  release);
	fu_device_add_guid (FU_DEVICE (device), devid2);

	/* set vendor ID */
	vendor_id = g_strdup_printf ("USB:0x%04X", g_usb_device_get_vid (usb_device));
	fu_device_set_vendor_id (FU_DEVICE (device), vendor_id);

	/* set the version if the release has been set */
	if (release != 0x0) {
		g_autofree gchar *version = as_utils_version_from_uint16 (release,
									  AS_VERSION_PARSE_FLAG_USE_BCD);
		fu_device_set_version (FU_DEVICE (device), version);
	}

	/* set USB platform ID automatically */
	fu_device_set_platform_id (FU_DEVICE (device),
				   g_usb_device_get_platform_id (usb_device));

	/* set the quirks again */
	fu_usb_device_apply_quirks (device);
}

/**
 * fu_usb_device_get_dev:
 * @device: A #FuUsbDevice
 *
 * Gets the #GUsbDevice.
 *
 * Returns: (transfer none): a #GUsbDevice, or %NULL
 *
 * Since: 1.0.2
 **/
GUsbDevice *
fu_usb_device_get_dev (FuUsbDevice *device)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (FU_IS_USB_DEVICE (device), NULL);
	return priv->usb_device;
}

/**
 * fu_usb_device_new:
 * @usb_device: A #GUsbDevice
 *
 * Creates a new #FuUsbDevice.
 *
 * Returns: (transfer full): a #FuUsbDevice
 *
 * Since: 1.0.2
 **/
FuDevice *
fu_usb_device_new (GUsbDevice *usb_device)
{
	FuUsbDevice *device = g_object_new (FU_TYPE_USB_DEVICE, NULL);
	fu_usb_device_set_dev (device, usb_device);
	return FU_DEVICE (device);
}
