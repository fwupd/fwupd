/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuUsbDevice"

#include "config.h"

#include "fu-device-private.h"
#include "fu-usb-device-private.h"

/**
 * SECTION:fu-usb-device
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
} FuUsbDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuUsbDevice, fu_usb_device, FU_TYPE_DEVICE)
enum {
	PROP_0,
	PROP_USB_DEVICE,
	PROP_LAST
};

#define GET_PRIVATE(o) (fu_usb_device_get_instance_private (o))

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

static gboolean
fu_usb_device_query_hub (FuUsbDevice *self, GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE (self);
	gsize sz = 0;
	guint16 value = 0x29;
	guint8 data[0x0c] = { 0x0 };
	g_autofree gchar *devid = NULL;

	/* longer descriptor for SuperSpeed */
	if (fu_usb_device_get_spec (self) >= 0x0300)
		value = 0x2a;
	if (!g_usb_device_control_transfer (priv->usb_device,
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_DEVICE,
					    0x06, /* LIBUSB_REQUEST_GET_DESCRIPTOR */
					    value << 8, 0x00,
					    data, sizeof(data), &sz,
					    1000, NULL, error)) {
		g_prefix_error (error, "failed to get USB descriptor: ");
		return FALSE;
	}
	if (g_getenv ("FU_USB_DEVICE_DEBUG") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "HUB_DT", data, sz);

	/* see http://www.usblyzer.com/usb-hub-class-decoder.htm */
	if (sz == 0x09) {
		devid = g_strdup_printf ("USB\\VID_%04X&PID_%04X&HUB_%02X",
					  g_usb_device_get_vid (priv->usb_device),
					  g_usb_device_get_pid (priv->usb_device),
					  data[7]);
		fu_device_add_instance_id (FU_DEVICE (self), devid);
	} else if (sz == 0x0c) {
		devid = g_strdup_printf ("USB\\VID_%04X&PID_%04X&HUB_%02X%02X",
					 g_usb_device_get_vid (priv->usb_device),
					 g_usb_device_get_pid (priv->usb_device),
					 data[11], data[10]);
		fu_device_add_instance_id (FU_DEVICE (self), devid);
	}
	return TRUE;
}

static gboolean
fu_usb_device_open (FuDevice *device, GError **error)
{
	FuUsbDevice *self = FU_USB_DEVICE (device);
	FuUsbDevicePrivate *priv = GET_PRIVATE (self);
	FuUsbDeviceClass *klass = FU_USB_DEVICE_GET_CLASS (device);
	guint idx;
	g_autoptr(FuDeviceLocker) locker = NULL;

	g_return_val_if_fail (FU_IS_USB_DEVICE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already open */
	if (priv->usb_device_locker != NULL)
		return TRUE;

	/* open */
	locker = fu_device_locker_new (priv->usb_device, error);
	if (locker == NULL)
		return FALSE;

	/* get vendor */
	if (fu_device_get_vendor (device) == NULL) {
		idx = g_usb_device_get_manufacturer_index (priv->usb_device);
		if (idx != 0x00) {
			g_autofree gchar *tmp = NULL;
			g_autoptr(GError) error_local = NULL;
			tmp = g_usb_device_get_string_descriptor (priv->usb_device,
								  idx, &error_local);
			if (tmp != NULL)
				fu_device_set_vendor (device, g_strchomp (tmp));
			else
				g_debug ("failed to load manufacturer string for usb device %u:%u: %s",
					 g_usb_device_get_bus (priv->usb_device),
					 g_usb_device_get_address (priv->usb_device),
					 error_local->message);
		}
	}

	/* get product */
	if (fu_device_get_name (device) == NULL) {
		idx = g_usb_device_get_product_index (priv->usb_device);
		if (idx != 0x00) {
			g_autofree gchar *tmp = NULL;
			g_autoptr(GError) error_local = NULL;
			tmp = g_usb_device_get_string_descriptor (priv->usb_device,
								  idx, &error_local);
			if (tmp != NULL)
				fu_device_set_name (device, g_strchomp (tmp));
			else
				g_debug ("failed to load product string for usb device %u:%u: %s",
					 g_usb_device_get_bus (priv->usb_device),
					 g_usb_device_get_address (priv->usb_device),
					 error_local->message);
		}
	}

	/* get serial number */
	if (fu_device_get_serial (device) == NULL) {
		idx = g_usb_device_get_serial_number_index (priv->usb_device);
		if (idx != 0x00) {
			g_autofree gchar *tmp = NULL;
			g_autoptr(GError) error_local = NULL;
			tmp = g_usb_device_get_string_descriptor (priv->usb_device,
								  idx, &error_local);
			if (tmp != NULL)
				fu_device_set_serial (device, g_strchomp (tmp));
			else
				g_debug ("failed to load serial number string for usb device %u:%u: %s",
					 g_usb_device_get_bus (priv->usb_device),
					 g_usb_device_get_address (priv->usb_device),
					 error_local->message);
		}
	}

	/* get version number, falling back to the USB device release */
	idx = g_usb_device_get_custom_index (priv->usb_device,
					     G_USB_DEVICE_CLASS_VENDOR_SPECIFIC,
					     'F', 'W', NULL);
	if (idx != 0x00) {
		g_autofree gchar *tmp = NULL;
		tmp = g_usb_device_get_string_descriptor (priv->usb_device, idx, NULL);
		/* although guessing is a route to insanity, if the device has
		 * provided the extra data it's because the BCD type was not
		 * suitable -- and INTEL_ME is not relevant here */
		fu_device_set_version (device, tmp, fu_common_version_guess_format (tmp));
	}

	/* get GUID from the descriptor if set */
	idx = g_usb_device_get_custom_index (priv->usb_device,
					     G_USB_DEVICE_CLASS_VENDOR_SPECIFIC,
					     'G', 'U', NULL);
	if (idx != 0x00) {
		g_autofree gchar *tmp = NULL;
		tmp = g_usb_device_get_string_descriptor (priv->usb_device, idx, NULL);
		fu_device_add_guid (device, tmp);
	}

	/* get the hub descriptor if this is a hub */
	if (g_usb_device_get_device_class (priv->usb_device) == G_USB_DEVICE_CLASS_HUB) {
		if (!fu_usb_device_query_hub (self, error))
			return FALSE;
	}

	/* subclassed */
	if (klass->open != NULL) {
		if (!klass->open (self, error))
			return FALSE;
	}

	/* success */
	priv->usb_device_locker = g_steal_pointer (&locker);
	return TRUE;
}

static gboolean
fu_usb_device_close (FuDevice *device, GError **error)
{
	FuUsbDevice *self = FU_USB_DEVICE (device);
	FuUsbDevicePrivate *priv = GET_PRIVATE (self);
	FuUsbDeviceClass *klass = FU_USB_DEVICE_GET_CLASS (device);

	g_return_val_if_fail (FU_IS_USB_DEVICE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already open */
	if (priv->usb_device_locker == NULL)
		return TRUE;

	/* subclassed */
	if (klass->close != NULL) {
		if (!klass->close (self, error))
			return FALSE;
	}

	g_clear_object (&priv->usb_device_locker);
	return TRUE;
}

static gboolean
fu_usb_device_probe (FuDevice *device, GError **error)
{
	FuUsbDevice *self = FU_USB_DEVICE (device);
	FuUsbDeviceClass *klass = FU_USB_DEVICE_GET_CLASS (device);
	FuUsbDevicePrivate *priv = GET_PRIVATE (self);
	guint16 release;
	g_autofree gchar *devid0 = NULL;
	g_autofree gchar *devid1 = NULL;
	g_autofree gchar *devid2 = NULL;
	g_autofree gchar *vendor_id = NULL;
	g_autoptr(GPtrArray) intfs = NULL;

	/* set vendor ID */
	vendor_id = g_strdup_printf ("USB:0x%04X", g_usb_device_get_vid (priv->usb_device));
	fu_device_set_vendor_id (device, vendor_id);

	/* set the version if the release has been set */
	release = g_usb_device_get_release (priv->usb_device);
	if (release != 0x0) {
		g_autofree gchar *version = NULL;
		version = fu_common_version_from_uint16 (release, FWUPD_VERSION_FORMAT_BCD);
		fu_device_set_version (device, version, FWUPD_VERSION_FORMAT_BCD);
	}

	/* add GUIDs in order of priority */
	devid2 = g_strdup_printf ("USB\\VID_%04X&PID_%04X&REV_%04X",
				  g_usb_device_get_vid (priv->usb_device),
				  g_usb_device_get_pid (priv->usb_device),
				  release);
	fu_device_add_instance_id (device, devid2);
	devid1 = g_strdup_printf ("USB\\VID_%04X&PID_%04X",
				  g_usb_device_get_vid (priv->usb_device),
				  g_usb_device_get_pid (priv->usb_device));
	fu_device_add_instance_id (device, devid1);
	devid0 = g_strdup_printf ("USB\\VID_%04X",
				  g_usb_device_get_vid (priv->usb_device));
	fu_device_add_instance_id_full (device, devid0,
					FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS);

	/* add the interface GUIDs */
	intfs = g_usb_device_get_interfaces (priv->usb_device, error);
	if (intfs == NULL)
		return FALSE;
	for (guint i = 0; i < intfs->len; i++) {
		GUsbInterface *intf = g_ptr_array_index (intfs, i);
		g_autofree gchar *intid1 = NULL;
		g_autofree gchar *intid2 = NULL;
		g_autofree gchar *intid3 = NULL;
		intid1 = g_strdup_printf ("USB\\CLASS_%02X&SUBCLASS_%02X&PROT_%02X",
					  g_usb_interface_get_class (intf),
					  g_usb_interface_get_subclass (intf),
					  g_usb_interface_get_protocol (intf));
		fu_device_add_instance_id_full (device, intid1,
						FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS);
		intid2 = g_strdup_printf ("USB\\CLASS_%02X&SUBCLASS_%02X",
					  g_usb_interface_get_class (intf),
					  g_usb_interface_get_subclass (intf));
		fu_device_add_instance_id_full (device, intid2,
						FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS);
		intid3 = g_strdup_printf ("USB\\CLASS_%02X",
					  g_usb_interface_get_class (intf));
		fu_device_add_instance_id_full (device, intid3,
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

/**
 * fu_usb_device_get_vid:
 * @self: A #FuUsbDevice
 *
 * Gets the device vendor code.
 *
 * Returns: integer, or 0x0 if unset or invalid
 *
 * Since: 1.1.2
 **/
guint16
fu_usb_device_get_vid (FuUsbDevice *self)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_USB_DEVICE (self), 0x0000);
	if (priv->usb_device == NULL)
		return 0x0;
	return g_usb_device_get_vid (priv->usb_device);
}

/**
 * fu_usb_device_get_pid:
 * @self: A #FuUsbDevice
 *
 * Gets the device product code.
 *
 * Returns: integer, or 0x0 if unset or invalid
 *
 * Since: 1.1.2
 **/
guint16
fu_usb_device_get_pid (FuUsbDevice *self)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_USB_DEVICE (self), 0x0000);
	if (priv->usb_device == NULL)
		return 0x0;
	return g_usb_device_get_pid (priv->usb_device);
}

/**
 * fu_usb_device_get_platform_id:
 * @self: A #FuUsbDevice
 *
 * Gets the device platform ID.
 *
 * Returns: string, or NULL if unset or invalid
 *
 * Since: 1.1.2
 **/
const gchar *
fu_usb_device_get_platform_id (FuUsbDevice *self)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_USB_DEVICE (self), NULL);
	if (priv->usb_device == NULL)
		return NULL;
	return g_usb_device_get_platform_id (priv->usb_device);
}

/**
 * fu_usb_device_get_spec:
 * @self: A #FuUsbDevice
 *
 * Gets the string USB revision for the device.
 *
 * Return value: a specification revision in BCD format, or 0x0 if not supported
 *
 * Since: 1.3.4
 **/
guint16
fu_usb_device_get_spec (FuUsbDevice *self)
{
#if G_USB_CHECK_VERSION(0,3,1)
	FuUsbDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_USB_DEVICE (self), 0x0);
	if (priv->usb_device == NULL)
		return 0x0;
	return g_usb_device_get_spec (priv->usb_device);
#else
	return 0x0;
#endif
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

	g_return_if_fail (FU_IS_USB_DEVICE (device));

	/* need to re-probe hardware */
	fu_device_probe_invalidate (FU_DEVICE (device));

	/* allow replacement */
	g_set_object (&priv->usb_device, usb_device);
	if (usb_device == NULL) {
		g_clear_object (&priv->usb_device_locker);
		return;
	}

	/* set device ID automatically */
	fu_device_set_physical_id (FU_DEVICE (device),
				   g_usb_device_get_platform_id (usb_device));
}

/**
 * fu_usb_device_find_udev_device:
 * @device: A #FuUsbDevice
 * @error: A #GError, or %NULL
 *
 * Gets the matching #GUdevDevice for the #GUsbDevice.
 *
 * Returns: (transfer full): a #GUdevDevice, or NULL if unset or invalid
 *
 * Since: 1.3.2
 **/
GUdevDevice *
fu_usb_device_find_udev_device (FuUsbDevice *device, GError **error)
{
#ifdef HAVE_GUDEV
	FuUsbDevicePrivate *priv = GET_PRIVATE (device);
	g_autoptr(GList) devices = NULL;
	g_autoptr(GUdevClient) gudev_client = g_udev_client_new (NULL);

	/* find all tty devices */
	devices = g_udev_client_query_by_subsystem (gudev_client, "usb");
	for (GList *l = devices; l != NULL; l = l->next) {
		GUdevDevice *dev = G_UDEV_DEVICE (l->data);

		/* check correct device */
		if (g_udev_device_get_sysfs_attr_as_int (dev, "busnum") !=
		    g_usb_device_get_bus (priv->usb_device))
			continue;
		if (g_udev_device_get_sysfs_attr_as_int (dev, "devnum") !=
		    g_usb_device_get_address (priv->usb_device))
			continue;

		/* success */
		g_debug ("USB device %u:%u is %s",
			 g_usb_device_get_bus (priv->usb_device),
			 g_usb_device_get_address (priv->usb_device),
			 g_udev_device_get_sysfs_path (dev));
		return g_object_ref (dev);
	}

	/* failure */
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_SUPPORTED,
		     "could not find sysfs device for %u:%u",
		     g_usb_device_get_bus (priv->usb_device),
		     g_usb_device_get_address (priv->usb_device));
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Not supported as <gudev.h> is unavailable");
#endif
	return NULL;
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

static void
fu_usb_device_incorporate (FuDevice *self, FuDevice *donor)
{
	g_return_if_fail (FU_IS_USB_DEVICE (self));
	g_return_if_fail (FU_IS_USB_DEVICE (donor));
	fu_usb_device_set_dev (FU_USB_DEVICE (self),
			       fu_usb_device_get_dev (FU_USB_DEVICE (donor)));
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
FuUsbDevice *
fu_usb_device_new (GUsbDevice *usb_device)
{
	FuUsbDevice *device = g_object_new (FU_TYPE_USB_DEVICE, NULL);
	fu_usb_device_set_dev (device, usb_device);
	return FU_USB_DEVICE (device);
}

static void
fu_usb_device_class_init (FuUsbDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS (klass);
	GParamSpec *pspec;

	object_class->finalize = fu_usb_device_finalize;
	object_class->get_property = fu_usb_device_get_property;
	object_class->set_property = fu_usb_device_set_property;
	device_class->open = fu_usb_device_open;
	device_class->close = fu_usb_device_close;
	device_class->probe = fu_usb_device_probe;
	device_class->incorporate = fu_usb_device_incorporate;

	pspec = g_param_spec_object ("usb-device", NULL, NULL,
				     G_USB_TYPE_DEVICE,
				     G_PARAM_READWRITE |
				     G_PARAM_CONSTRUCT |
				     G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_USB_DEVICE, pspec);
}
