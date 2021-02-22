/*
 * Copyright (C) 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuBluezDevice"

#include "config.h"

#include <string.h>

#include "fu-bluez-device.h"
#include "fu-common.h"
#include "fu-device-private.h"
#include "fu-firmware-common.h"

#define DEFAULT_PROXY_TIMEOUT	5000

/**
 * SECTION:fu-bluez-device
 * @short_description: a BlueZ Bluetooth device
 *
 * An object that represents a BlueZ Bluetooth device.
 *
 * See also: #FuBluezDevice
 */

typedef struct {
	FuBluezDevice		 parent_instance;
	GDBusObjectManager	*object_manager;
	GDBusProxy		*proxy;
	GHashTable		*uuid_paths;	/* utf8 : utf8 */
} FuBluezDevicePrivate;

enum {
	PROP_0,
	PROP_OBJECT_MANAGER,
	PROP_PROXY,
	PROP_LAST
};

G_DEFINE_TYPE_WITH_PRIVATE (FuBluezDevice, fu_bluez_device, FU_TYPE_DEVICE)

#define GET_PRIVATE(o) (fu_bluez_device_get_instance_private (o))

static void
fu_bluez_device_add_uuid_path (FuBluezDevice *self, const gchar *uuid, const gchar *path)
{
	FuBluezDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_BLUEZ_DEVICE (self));
	g_return_if_fail (uuid != NULL);
	g_return_if_fail (path != NULL);
	g_hash_table_insert (priv->uuid_paths,
			     g_strdup (uuid),
			     g_strdup (path));
}

static void
fu_bluez_device_set_modalias (FuBluezDevice *self, const gchar *modalias)
{
	gsize modaliaslen = strlen (modalias);
	guint16 vid = 0x0;
	guint16 pid = 0x0;
	guint16 rev = 0x0;

	g_return_if_fail (modalias != NULL);

	/* usb:v0461p4EEFd0001 */
	if (g_str_has_prefix (modalias, "usb:")) {
		fu_firmware_strparse_uint16_safe (modalias, modaliaslen, 5, &vid, NULL);
		fu_firmware_strparse_uint16_safe (modalias, modaliaslen, 10, &pid, NULL);
		fu_firmware_strparse_uint16_safe (modalias, modaliaslen, 15, &rev, NULL);

	/* bluetooth:v000ApFFFFdFFFF */
	} else if (g_str_has_prefix (modalias, "bluetooth:")) {
		fu_firmware_strparse_uint16_safe (modalias, modaliaslen, 11, &vid, NULL);
		fu_firmware_strparse_uint16_safe (modalias, modaliaslen, 16, &pid, NULL);
		fu_firmware_strparse_uint16_safe (modalias, modaliaslen, 21, &rev, NULL);
	}

	if (vid != 0x0 && pid != 0x0 && rev != 0x0) {
		g_autofree gchar *devid = NULL;
		devid = g_strdup_printf ("BLUETOOTH\\VID_%04X&PID_%04X&REV_%04X",
					 vid, pid, rev);
		fu_device_add_instance_id (FU_DEVICE (self), devid);
	}
	if (vid != 0x0 && pid != 0x0) {
		g_autofree gchar *devid = NULL;
		devid = g_strdup_printf ("BLUETOOTH\\VID_%04X&PID_%04X", vid, pid);
		fu_device_add_instance_id (FU_DEVICE (self), devid);
	}
	if (vid != 0x0) {
		g_autofree gchar *devid = NULL;
		g_autofree gchar *vendor_id = NULL;
		devid = g_strdup_printf ("BLUETOOTH\\VID_%04X", vid);
		fu_device_add_instance_id_full (FU_DEVICE (self), devid,
						FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS);
		vendor_id = g_strdup_printf ("BLUETOOTH:%04X", vid);
		fu_device_add_vendor_id (FU_DEVICE (self), vendor_id);
	}
}

static void
fu_bluez_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuBluezDevice *self = FU_BLUEZ_DEVICE (device);
	FuBluezDevicePrivate *priv = GET_PRIVATE (self);

	if (priv->uuid_paths != NULL) {
		GHashTableIter iter;
		gpointer key, value;
		g_hash_table_iter_init (&iter, priv->uuid_paths);
		while (g_hash_table_iter_next (&iter, &key, &value)) {
			fu_common_string_append_kv (str, idt + 1,
						    (const gchar *) key,
						    (const gchar *) value);
		}
	}
}

/*
 * Returns the value of a property of an object specified by its path as
 * a GVariant, or NULL if the property wasn't found.
 */
static GVariant *
fu_bluez_device_get_ble_property (const gchar *obj_path,
				  const gchar *iface,
				  const gchar *prop_name,
				  GError **error)
{
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GVariant) val = NULL;

	proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
					       G_DBUS_PROXY_FLAGS_NONE,
					       NULL,
					       "org.bluez",
					       obj_path, iface, NULL, error);
	if (proxy == NULL) {
		g_prefix_error (error, "failed to connect to %s: ", iface);
		return NULL;
	}
	val = g_dbus_proxy_get_cached_property (proxy, prop_name);
	if (val == NULL) {
		g_prefix_error (error, "property %s not found in %s: ",
				prop_name, obj_path);
		return NULL;
	}

	return g_steal_pointer (&val);
}

/*
 * Returns the value of the string property of an object specified by
 * its path, or NULL if the property wasn't found.
 *
 * The returned string must be freed using g_free().
 */
static gchar *
fu_bluez_device_get_ble_string_property (const gchar *obj_path,
					 const gchar *iface,
					 const gchar *prop_name,
					 GError **error)
{
	g_autoptr(GVariant) val = NULL;
	val = fu_bluez_device_get_ble_property (obj_path, iface, prop_name, error);
	if (val == NULL)
		return NULL;
	return g_variant_dup_string (val, NULL);
}

/*
 * Populates the {uuid : object_path} entries of a device for all its
 * characteristics.
 *
 * TODO: Extend to services and descriptors too?
 */
static void
fu_bluez_device_add_device_uuids (FuBluezDevice *self)
{
	FuBluezDevicePrivate *priv = GET_PRIVATE (self);
	g_autolist(GDBusObject) obj_list = NULL;

	obj_list = g_dbus_object_manager_get_objects (priv->object_manager);
	for (GList *l = obj_list; l != NULL; l = l->next) {
		GDBusObject *obj = G_DBUS_OBJECT (l->data);
		g_autoptr(GDBusInterface) iface = NULL;
		g_autoptr(GError) error_local = NULL;
		g_autofree gchar *obj_uuid = NULL;
		const gchar *obj_path = g_dbus_object_get_object_path (obj);

		/* not us */
		if (!g_str_has_prefix (g_dbus_object_get_object_path (obj),
				       g_dbus_proxy_get_object_path (priv->proxy)))
			continue;

		/*
		 * TODO: Currently restricted to getting UUIDs for
		 * characteristics only, as the only use case we're
		 * going to need for now is reading/writing
		 * characteristics.
		 */
		iface = g_dbus_object_get_interface (obj, "org.bluez.GattCharacteristic1");
		if (iface == NULL)
			continue;
		obj_uuid = fu_bluez_device_get_ble_string_property (obj_path,
								    "org.bluez.GattCharacteristic1",
								    "UUID",
								    &error_local);
		if (obj_uuid == NULL) {
			g_debug ("failed to get property: %s", error_local->message);
			continue;
		}
		fu_bluez_device_add_uuid_path (self, obj_uuid, obj_path);
	}
}

static gboolean
fu_bluez_device_setup (FuDevice *device, GError **error)
{
	FuBluezDevice *self = FU_BLUEZ_DEVICE (device);

	fu_bluez_device_add_device_uuids (self);

	return TRUE;
}

static gboolean
fu_bluez_device_probe (FuDevice *device, GError **error)
{
	FuBluezDevice *self = FU_BLUEZ_DEVICE (device);
	FuBluezDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GVariant) val_adapter = NULL;
	g_autoptr(GVariant) val_address = NULL;
	g_autoptr(GVariant) val_icon = NULL;
	g_autoptr(GVariant) val_modalias = NULL;
	g_autoptr(GVariant) val_name = NULL;

	val_address = g_dbus_proxy_get_cached_property (priv->proxy, "Address");
	if (val_address == NULL) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "No required BLE address");
		return FALSE;
	}
	fu_device_set_logical_id (device, g_variant_get_string (val_address, NULL));
	val_adapter = g_dbus_proxy_get_cached_property (priv->proxy, "Adapter");
	if (val_adapter != NULL)
		fu_device_set_physical_id (device, g_variant_get_string (val_adapter, NULL));
	val_name = g_dbus_proxy_get_cached_property (priv->proxy, "Name");
	if (val_name != NULL)
		fu_device_set_name (device, g_variant_get_string (val_name, NULL));
	val_icon = g_dbus_proxy_get_cached_property (priv->proxy, "Icon");
	if (val_icon != NULL)
		fu_device_add_icon (device, g_variant_get_string (val_name, NULL));
	val_modalias = g_dbus_proxy_get_cached_property (priv->proxy, "Modalias");
	if (val_modalias != NULL)
		fu_bluez_device_set_modalias (self, g_variant_get_string (val_modalias, NULL));

	/* success */
	return TRUE;
}

/**
 * fu_bluez_device_read:
 * @self: A #FuBluezDevice
 * @uuid: The UUID, e.g. `00cde35c-7062-11eb-9439-0242ac130002`
 * @error: A #GError, or %NULL
 *
 * Reads from a UUID on the device.
 *
 * Returns: (transfer full): data, or %NULL for error
 *
 * Since: 1.5.7
 **/
GByteArray *
fu_bluez_device_read (FuBluezDevice *self, const gchar *uuid, GError **error)
{
	FuBluezDevicePrivate *priv = GET_PRIVATE (self);
	guint8 byte;
	const gchar *path;
	g_autoptr(GByteArray) buf = g_byte_array_new ();
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GVariantBuilder) builder = NULL;
	g_autoptr(GVariantIter) iter = NULL;
	g_autoptr(GVariant) val = NULL;

	path = g_hash_table_lookup (priv->uuid_paths, uuid);
	if (path == NULL) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "UUID %s not supported", uuid);
		return NULL;
	}
	proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
					       G_DBUS_PROXY_FLAGS_NONE,
					       NULL,
					       "org.bluez",
					       path,
					       "org.bluez.GattCharacteristic1",
					       NULL, error);
	if (proxy == NULL) {
		g_prefix_error (error, "Failed to connect GattCharacteristic1: ");
		return NULL;
	}
	g_dbus_proxy_set_default_timeout (proxy, DEFAULT_PROXY_TIMEOUT);

	/*
	 * Call the "ReadValue" method through the proxy synchronously.
	 *
	 * The method takes one argument: an array of dicts of
	 * {string:value} specifing the options (here the option is
	 * "offset":0.
	 * The result is a byte array.
	 */
	builder = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
	g_variant_builder_add (builder, "{sv}", "offset",
			       g_variant_new("q", 0));

	val = g_dbus_proxy_call_sync (proxy,
				      "ReadValue",
				      g_variant_new ("(a{sv})", builder),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1, NULL, error);
	if (val == NULL) {
		g_prefix_error (error, "Failed to read GattCharacteristic1: ");
		return NULL;
	}
	g_variant_get(val, "(ay)", &iter);
	while (g_variant_iter_loop (iter, "y", &byte))
		g_byte_array_append (buf, &byte, 1);

	/* success */
	return g_steal_pointer (&buf);
}

/**
 * fu_bluez_device_read_string:
 * @self: A #FuBluezDevice
 * @uuid: The UUID, e.g. `00cde35c-7062-11eb-9439-0242ac130002`
 * @error: A #GError, or %NULL
 *
 * Reads a string from a UUID on the device.
 *
 * Returns: (transfer full): data, or %NULL for error
 *
 * Since: 1.5.7
 **/
gchar *
fu_bluez_device_read_string (FuBluezDevice *self, const gchar *uuid, GError **error)
{
	GByteArray *buf = fu_bluez_device_read (self, uuid, error);
	if (buf == NULL)
		return NULL;
	return (gchar *) g_byte_array_free (buf, FALSE);
}

/**
 * fu_bluez_device_write:
 * @self: A #FuBluezDevice
 * @uuid: The UUID, e.g. `00cde35c-7062-11eb-9439-0242ac130002`
 * @error: A #GError, or %NULL
 *
 * Writes to a UUID on the device.
 *
 * Returns: %TRUE if all the data was written
 *
 * Since: 1.5.7
 **/
gboolean
fu_bluez_device_write (FuBluezDevice *self,
		       const gchar *uuid,
		       GByteArray *buf,
		       GError **error)
{
	FuBluezDevicePrivate *priv = GET_PRIVATE (self);
	const gchar *path;
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GVariantBuilder) opt_builder = NULL;
	g_autoptr(GVariantBuilder) val_builder = NULL;
	g_autoptr(GVariant) ret = NULL;
	GVariant *opt_variant = NULL;
	GVariant *val_variant = NULL;

	path = g_hash_table_lookup (priv->uuid_paths, uuid);
	if (path == NULL) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "UUID %s not supported", uuid);
		return FALSE;
	}
	proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
					       G_DBUS_PROXY_FLAGS_NONE,
					       NULL,
					       "org.bluez",
					       path,
					       "org.bluez.GattCharacteristic1",
					       NULL, error);
	if (proxy == NULL) {
		g_prefix_error (error, "Failed to connect GattCharacteristic1: ");
		return FALSE;
	}

	/* build the value variant */
	val_builder = g_variant_builder_new (G_VARIANT_TYPE ("ay"));
	for (gsize i = 0; i < buf->len; i++)
		g_variant_builder_add (val_builder, "y", buf->data[i]);
	val_variant = g_variant_new ("ay", val_builder);

	/* build the options variant (offset = 0) */
	opt_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add (opt_builder, "{sv}", "offset",
			       g_variant_new_uint16 (0));
	opt_variant = g_variant_new("a{sv}", opt_builder);

	ret = g_dbus_proxy_call_sync (proxy,
				      "WriteValue",
				      g_variant_new ("(@ay@a{sv})",
						     val_variant,
						     opt_variant),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1, NULL, error);
	if (ret == NULL) {
		g_prefix_error (error, "Failed to write GattCharacteristic1: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_bluez_device_incorporate (FuDevice *self, FuDevice *donor)
{
	FuBluezDevice *uself = FU_BLUEZ_DEVICE (self);
	FuBluezDevice *udonor = FU_BLUEZ_DEVICE (donor);
	FuBluezDevicePrivate *priv = GET_PRIVATE (uself);
	FuBluezDevicePrivate *privdonor = GET_PRIVATE (udonor);

	if (g_hash_table_size (priv->uuid_paths) == 0) {
		GHashTableIter iter;
		gpointer key, value;
		g_hash_table_iter_init (&iter, privdonor->uuid_paths);
		while (g_hash_table_iter_next (&iter, &key, &value)) {
			fu_bluez_device_add_uuid_path (uself,
						       (const gchar *) key,
						       (const gchar *) value);
		}
	}
	if (priv->object_manager == NULL)
		priv->object_manager = g_object_ref (privdonor->object_manager);
	if (priv->proxy == NULL)
		priv->proxy = g_object_ref (privdonor->proxy);
}

static void
fu_bluez_device_get_property (GObject *object, guint prop_id,
			     GValue *value, GParamSpec *pspec)
{
	FuBluezDevice *self = FU_BLUEZ_DEVICE (object);
	FuBluezDevicePrivate *priv = GET_PRIVATE (self);
	switch (prop_id) {
	case PROP_OBJECT_MANAGER:
		g_value_set_object (value, priv->object_manager);
		break;
	case PROP_PROXY:
		g_value_set_object (value, priv->proxy);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fu_bluez_device_set_property (GObject *object, guint prop_id,
			     const GValue *value, GParamSpec *pspec)
{
	FuBluezDevice *self = FU_BLUEZ_DEVICE (object);
	FuBluezDevicePrivate *priv = GET_PRIVATE (self);
	switch (prop_id) {
	case PROP_OBJECT_MANAGER:
		priv->object_manager = g_value_dup_object (value);
		break;
	case PROP_PROXY:
		priv->proxy = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fu_bluez_device_finalize (GObject *object)
{
	FuBluezDevice *self = FU_BLUEZ_DEVICE (object);
	FuBluezDevicePrivate *priv = GET_PRIVATE (self);

	g_hash_table_unref (priv->uuid_paths);
	g_object_unref (priv->proxy);
	g_object_unref (priv->object_manager);
	G_OBJECT_CLASS (fu_bluez_device_parent_class)->finalize (object);
}

static void
fu_bluez_device_init (FuBluezDevice *self)
{
	FuBluezDevicePrivate *priv = GET_PRIVATE (self);
	priv->uuid_paths = g_hash_table_new_full (g_str_hash, g_str_equal,
						  g_free, g_free);
}

static void
fu_bluez_device_class_init (FuBluezDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamSpec *pspec;

	object_class->get_property = fu_bluez_device_get_property;
	object_class->set_property = fu_bluez_device_set_property;
	object_class->finalize = fu_bluez_device_finalize;
	device_class->probe = fu_bluez_device_probe;
	device_class->setup = fu_bluez_device_setup;
	device_class->to_string = fu_bluez_device_to_string;
	device_class->incorporate = fu_bluez_device_incorporate;

	pspec = g_param_spec_object ("object-manager", NULL, NULL,
				     G_TYPE_DBUS_OBJECT_MANAGER,
				     G_PARAM_READWRITE |
				     G_PARAM_CONSTRUCT |
				     G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_OBJECT_MANAGER, pspec);

	pspec = g_param_spec_object ("proxy", NULL, NULL,
				     G_TYPE_DBUS_PROXY,
				     G_PARAM_READWRITE |
				     G_PARAM_CONSTRUCT |
				     G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_PROXY, pspec);
}
