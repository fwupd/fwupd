/*
 * Copyright (C) 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuBluezDevice"

#include "config.h"

#include <string.h>

#include "fu-bluez-device.h"
#include "fu-common.h"
#include "fu-device-private.h"
#include "fu-firmware-common.h"
#include "fu-string.h"

#define DEFAULT_PROXY_TIMEOUT 5000

/**
 * FuBluezDevice:
 *
 * A BlueZ Bluetooth device.
 *
 * See also: [class@FuDevice]
 */

typedef struct {
	GDBusObjectManager *object_manager;
	GDBusProxy *proxy;
	GHashTable *uuids; /* utf8 : FuBluezDeviceUuidHelper */
} FuBluezDevicePrivate;

typedef struct {
	FuBluezDevice *self;
	gchar *uuid;
	gchar *path;
	gulong signal_id;
	GDBusProxy *proxy;
} FuBluezDeviceUuidHelper;

enum { PROP_0, PROP_OBJECT_MANAGER, PROP_PROXY, PROP_LAST };

enum { SIGNAL_CHANGED, SIGNAL_LAST };

static guint signals[SIGNAL_LAST] = {0};

G_DEFINE_TYPE_WITH_PRIVATE(FuBluezDevice, fu_bluez_device, FU_TYPE_DEVICE)

#define GET_PRIVATE(o) (fu_bluez_device_get_instance_private(o))

static void
fu_bluez_uuid_free(FuBluezDeviceUuidHelper *uuid_helper)
{
	if (uuid_helper->path != NULL)
		g_free(uuid_helper->path);
	if (uuid_helper->proxy != NULL)
		g_object_unref(uuid_helper->proxy);
	g_free(uuid_helper->uuid);
	g_object_unref(uuid_helper->self);
	g_free(uuid_helper);
}

/*
 * Looks up a UUID in the FuBluezDevice uuids table.
 */
static FuBluezDeviceUuidHelper *
fu_bluez_device_get_uuid_helper(FuBluezDevice *self, const gchar *uuid, GError **error)
{
	FuBluezDevicePrivate *priv = GET_PRIVATE(self);
	FuBluezDeviceUuidHelper *uuid_helper;

	uuid_helper = g_hash_table_lookup(priv->uuids, uuid);
	if (uuid_helper == NULL) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "UUID %s not supported",
			    uuid);
		return NULL;
	}

	return uuid_helper;
}

static void
fu_bluez_device_signal_cb(GDBusProxy *proxy,
			  GVariant *changed_properties,
			  const GStrv invalidated_properties,
			  FuBluezDeviceUuidHelper *uuid_helper)
{
	g_signal_emit(uuid_helper->self, signals[SIGNAL_CHANGED], 0, uuid_helper->uuid);
}

/*
 * Builds the GDBusProxy of the BlueZ object identified by a UUID
 * string. If the object doesn't have a dedicated proxy yet, this
 * creates it and saves it in the FuBluezDeviceUuidHelper object.
 *
 * NOTE: Currently limited to GATT characteristics.
 */
static gboolean
fu_bluez_device_ensure_uuid_helper_proxy(FuBluezDeviceUuidHelper *uuid_helper, GError **error)
{
	if (uuid_helper->proxy != NULL)
		return TRUE;
	uuid_helper->proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
							   G_DBUS_PROXY_FLAGS_NONE,
							   NULL,
							   "org.bluez",
							   uuid_helper->path,
							   "org.bluez.GattCharacteristic1",
							   NULL,
							   error);
	if (uuid_helper->proxy == NULL) {
		g_prefix_error(error, "Failed to create GDBusProxy for uuid_helper: ");
		return FALSE;
	}
	g_dbus_proxy_set_default_timeout(uuid_helper->proxy, DEFAULT_PROXY_TIMEOUT);
	uuid_helper->signal_id = g_signal_connect(G_DBUS_PROXY(uuid_helper->proxy),
						  "g-properties-changed",
						  G_CALLBACK(fu_bluez_device_signal_cb),
						  uuid_helper);
	if (uuid_helper->signal_id <= 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "cannot connect to signal of UUID %s",
			    uuid_helper->uuid);
		return FALSE;
	}
	return TRUE;
}

static void
fu_bluez_device_add_uuid_path(FuBluezDevice *self, const gchar *uuid, const gchar *path)
{
	FuBluezDevicePrivate *priv = GET_PRIVATE(self);
	FuBluezDeviceUuidHelper *uuid_helper;
	g_return_if_fail(FU_IS_BLUEZ_DEVICE(self));
	g_return_if_fail(uuid != NULL);
	g_return_if_fail(path != NULL);

	uuid_helper = g_new0(FuBluezDeviceUuidHelper, 1);
	uuid_helper->self = g_object_ref(self);
	uuid_helper->uuid = g_strdup(uuid);
	uuid_helper->path = g_strdup(path);
	g_hash_table_insert(priv->uuids, g_strdup(uuid), uuid_helper);
}

static void
fu_bluez_device_set_modalias(FuBluezDevice *self, const gchar *modalias)
{
	gsize modaliaslen = strlen(modalias);
	guint16 vid = 0x0;
	guint16 pid = 0x0;
	guint16 rev = 0x0;

	g_return_if_fail(modalias != NULL);

	/* usb:v0461p4EEFd0001 */
	if (g_str_has_prefix(modalias, "usb:")) {
		fu_firmware_strparse_uint16_safe(modalias, modaliaslen, 5, &vid, NULL);
		fu_firmware_strparse_uint16_safe(modalias, modaliaslen, 10, &pid, NULL);
		fu_firmware_strparse_uint16_safe(modalias, modaliaslen, 15, &rev, NULL);

		/* bluetooth:v000ApFFFFdFFFF */
	} else if (g_str_has_prefix(modalias, "bluetooth:")) {
		fu_firmware_strparse_uint16_safe(modalias, modaliaslen, 11, &vid, NULL);
		fu_firmware_strparse_uint16_safe(modalias, modaliaslen, 16, &pid, NULL);
		fu_firmware_strparse_uint16_safe(modalias, modaliaslen, 21, &rev, NULL);
	}

	/* add generated IDs */
	if (vid != 0x0)
		fu_device_add_instance_u16(FU_DEVICE(self), "VID", vid);
	if (pid != 0x0)
		fu_device_add_instance_u16(FU_DEVICE(self), "PID", pid);
	fu_device_add_instance_u16(FU_DEVICE(self), "REV", rev);
	fu_device_build_instance_id_quirk(FU_DEVICE(self), NULL, "BLUETOOTH", "VID", NULL);
	fu_device_build_instance_id(FU_DEVICE(self), NULL, "BLUETOOTH", "VID", "PID", NULL);
	fu_device_build_instance_id(FU_DEVICE(self), NULL, "BLUETOOTH", "VID", "PID", "REV", NULL);

	/* set vendor ID */
	if (vid != 0x0) {
		g_autofree gchar *vendor_id = g_strdup_printf("BLUETOOTH:%04X", vid);
		fu_device_add_vendor_id(FU_DEVICE(self), vendor_id);
	}

	/* set version if the revision has been set */
	if (rev != 0x0 &&
	    fu_device_get_version_format(FU_DEVICE(self)) == FWUPD_VERSION_FORMAT_UNKNOWN) {
		g_autofree gchar *version = NULL;
		version = fu_version_from_uint16(rev, FWUPD_VERSION_FORMAT_BCD);
		fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_BCD);
		fu_device_set_version(FU_DEVICE(self), version);
	}
}

static void
fu_bluez_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuBluezDevice *self = FU_BLUEZ_DEVICE(device);
	FuBluezDevicePrivate *priv = GET_PRIVATE(self);

	if (priv->uuids != NULL) {
		GHashTableIter iter;
		gpointer key, value;
		g_hash_table_iter_init(&iter, priv->uuids);
		while (g_hash_table_iter_next(&iter, &key, &value)) {
			FuBluezDeviceUuidHelper *uuid_helper = (FuBluezDeviceUuidHelper *)value;
			fu_string_append(str, idt + 1, (const gchar *)key, uuid_helper->path);
		}
	}
}

/*
 * Returns the value of a property of an object specified by its path as
 * a GVariant, or NULL if the property wasn't found.
 */
static GVariant *
fu_bluez_device_get_ble_property(const gchar *obj_path,
				 const gchar *iface,
				 const gchar *prop_name,
				 GError **error)
{
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GVariant) val = NULL;

	proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
					      G_DBUS_PROXY_FLAGS_NONE,
					      NULL,
					      "org.bluez",
					      obj_path,
					      iface,
					      NULL,
					      error);
	if (proxy == NULL) {
		g_prefix_error(error, "failed to connect to %s: ", iface);
		return NULL;
	}
	g_dbus_proxy_set_default_timeout(proxy, DEFAULT_PROXY_TIMEOUT);
	val = g_dbus_proxy_get_cached_property(proxy, prop_name);
	if (val == NULL) {
		g_prefix_error(error, "property %s not found in %s: ", prop_name, obj_path);
		return NULL;
	}

	return g_steal_pointer(&val);
}

/*
 * Returns the value of the string property of an object specified by
 * its path, or NULL if the property wasn't found.
 *
 * The returned string must be freed using g_free().
 */
static gchar *
fu_bluez_device_get_ble_string_property(const gchar *obj_path,
					const gchar *iface,
					const gchar *prop_name,
					GError **error)
{
	g_autoptr(GVariant) val = NULL;
	val = fu_bluez_device_get_ble_property(obj_path, iface, prop_name, error);
	if (val == NULL)
		return NULL;
	return g_variant_dup_string(val, NULL);
}

/*
 * Populates the {uuid_helper : object_path} entries of a device for all its
 * characteristics.
 *
 * TODO: Extend to services and descriptors too?
 */
static void
fu_bluez_device_add_device_uuids(FuBluezDevice *self)
{
	FuBluezDevicePrivate *priv = GET_PRIVATE(self);
	g_autolist(GDBusObject) obj_list = NULL;

	obj_list = g_dbus_object_manager_get_objects(priv->object_manager);
	for (GList *l = obj_list; l != NULL; l = l->next) {
		GDBusObject *obj = G_DBUS_OBJECT(l->data);
		g_autoptr(GDBusInterface) iface = NULL;
		g_autoptr(GError) error_local = NULL;
		g_autofree gchar *obj_uuid = NULL;
		const gchar *obj_path = g_dbus_object_get_object_path(obj);

		/* not us */
		if (!g_str_has_prefix(g_dbus_object_get_object_path(obj),
				      g_dbus_proxy_get_object_path(priv->proxy)))
			continue;

		/*
		 * TODO: Currently restricted to getting UUIDs for
		 * characteristics only, as the only use case we're
		 * going to need for now is reading/writing
		 * characteristics.
		 */
		iface = g_dbus_object_get_interface(obj, "org.bluez.GattCharacteristic1");
		if (iface == NULL)
			continue;
		obj_uuid = fu_bluez_device_get_ble_string_property(obj_path,
								   "org.bluez.GattCharacteristic1",
								   "UUID",
								   &error_local);
		if (obj_uuid == NULL) {
			g_debug("failed to get property: %s", error_local->message);
			continue;
		}
		fu_bluez_device_add_uuid_path(self, obj_uuid, obj_path);
	}
}

static gboolean
fu_bluez_device_setup(FuDevice *device, GError **error)
{
	FuBluezDevice *self = FU_BLUEZ_DEVICE(device);

	fu_bluez_device_add_device_uuids(self);

	return TRUE;
}

static gboolean
fu_bluez_device_probe(FuDevice *device, GError **error)
{
	FuBluezDevice *self = FU_BLUEZ_DEVICE(device);
	FuBluezDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GVariant) val_adapter = NULL;
	g_autoptr(GVariant) val_address = NULL;
	g_autoptr(GVariant) val_icon = NULL;
	g_autoptr(GVariant) val_modalias = NULL;
	g_autoptr(GVariant) val_name = NULL;

	val_address = g_dbus_proxy_get_cached_property(priv->proxy, "Address");
	if (val_address == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "No required BLE address");
		return FALSE;
	}
	fu_device_set_logical_id(device, g_variant_get_string(val_address, NULL));
	val_adapter = g_dbus_proxy_get_cached_property(priv->proxy, "Adapter");
	if (val_adapter != NULL)
		fu_device_set_physical_id(device, g_variant_get_string(val_adapter, NULL));
	val_name = g_dbus_proxy_get_cached_property(priv->proxy, "Name");
	if (val_name != NULL)
		fu_device_set_name(device, g_variant_get_string(val_name, NULL));
	val_icon = g_dbus_proxy_get_cached_property(priv->proxy, "Icon");
	if (val_icon != NULL)
		fu_device_add_icon(device, g_variant_get_string(val_name, NULL));
	val_modalias = g_dbus_proxy_get_cached_property(priv->proxy, "Modalias");
	if (val_modalias != NULL)
		fu_bluez_device_set_modalias(self, g_variant_get_string(val_modalias, NULL));

	/* success */
	return TRUE;
}

/**
 * fu_bluez_device_read:
 * @self: a #FuBluezDevice
 * @uuid: the UUID, e.g. `00cde35c-7062-11eb-9439-0242ac130002`
 * @error: (nullable): optional return location for an error
 *
 * Reads from a UUID on the device.
 *
 * Returns: (transfer full): data, or %NULL for error
 *
 * Since: 1.5.7
 **/
GByteArray *
fu_bluez_device_read(FuBluezDevice *self, const gchar *uuid, GError **error)
{
	FuBluezDeviceUuidHelper *uuid_helper;
	guint8 byte;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GVariantBuilder) builder = NULL;
	g_autoptr(GVariantIter) iter = NULL;
	g_autoptr(GVariant) val = NULL;

	uuid_helper = fu_bluez_device_get_uuid_helper(self, uuid, error);
	if (uuid_helper == NULL)
		return NULL;
	if (!fu_bluez_device_ensure_uuid_helper_proxy(uuid_helper, error))
		return NULL;

	/*
	 * Call the "ReadValue" method through the proxy synchronously.
	 *
	 * The method takes one argument: an array of dicts of
	 * {string:value} specifying the options (here the option is
	 * "offset":0.
	 * The result is a byte array.
	 */
	builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(builder, "{sv}", "offset", g_variant_new("q", 0));

	val = g_dbus_proxy_call_sync(uuid_helper->proxy,
				     "ReadValue",
				     g_variant_new("(a{sv})", builder),
				     G_DBUS_CALL_FLAGS_NONE,
				     -1,
				     NULL,
				     error);
	if (val == NULL) {
		g_prefix_error(error, "Failed to read GattCharacteristic1: ");
		return NULL;
	}
	g_variant_get(val, "(ay)", &iter);
	while (g_variant_iter_loop(iter, "y", &byte))
		g_byte_array_append(buf, &byte, 1);

	/* success */
	return g_steal_pointer(&buf);
}

/**
 * fu_bluez_device_read_string:
 * @self: a #FuBluezDevice
 * @uuid: the UUID, e.g. `00cde35c-7062-11eb-9439-0242ac130002`
 * @error: (nullable): optional return location for an error
 *
 * Reads a string from a UUID on the device.
 *
 * Returns: (transfer full): data, or %NULL for error
 *
 * Since: 1.5.7
 **/
gchar *
fu_bluez_device_read_string(FuBluezDevice *self, const gchar *uuid, GError **error)
{
	GByteArray *buf = fu_bluez_device_read(self, uuid, error);
	if (buf == NULL)
		return NULL;
	return (gchar *)g_byte_array_free(buf, FALSE);
}

/**
 * fu_bluez_device_write:
 * @self: a #FuBluezDevice
 * @uuid: the UUID, e.g. `00cde35c-7062-11eb-9439-0242ac130002`
 * @buf: data array
 * @error: (nullable): optional return location for an error
 *
 * Writes to a UUID on the device.
 *
 * Returns: %TRUE if all the data was written
 *
 * Since: 1.5.7
 **/
gboolean
fu_bluez_device_write(FuBluezDevice *self, const gchar *uuid, GByteArray *buf, GError **error)
{
	FuBluezDeviceUuidHelper *uuid_helper;
	g_autoptr(GVariantBuilder) opt_builder = NULL;
	g_autoptr(GVariantBuilder) val_builder = NULL;
	g_autoptr(GVariant) ret = NULL;
	GVariant *opt_variant = NULL;
	GVariant *val_variant = NULL;

	uuid_helper = fu_bluez_device_get_uuid_helper(self, uuid, error);
	if (uuid_helper == NULL)
		return FALSE;
	if (!fu_bluez_device_ensure_uuid_helper_proxy(uuid_helper, error))
		return FALSE;

	/* build the value variant */
	val_builder = g_variant_builder_new(G_VARIANT_TYPE("ay"));
	for (gsize i = 0; i < buf->len; i++)
		g_variant_builder_add(val_builder, "y", buf->data[i]);
	val_variant = g_variant_new("ay", val_builder);

	/* build the options variant (offset = 0) */
	opt_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(opt_builder, "{sv}", "offset", g_variant_new_uint16(0));
	opt_variant = g_variant_new("a{sv}", opt_builder);

	ret = g_dbus_proxy_call_sync(uuid_helper->proxy,
				     "WriteValue",
				     g_variant_new("(@ay@a{sv})", val_variant, opt_variant),
				     G_DBUS_CALL_FLAGS_NONE,
				     -1,
				     NULL,
				     error);
	if (ret == NULL) {
		g_prefix_error(error, "Failed to write GattCharacteristic1: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * fu_bluez_device_notify_start:
 * @self: a #FuBluezDevice
 * @uuid: the UUID, e.g. `00cde35c-7062-11eb-9439-0242ac130002`
 * @error: (nullable): optional return location for an error
 *
 * Enables notifications for property changes in a UUID (StartNotify
 * method).
 *
 * Returns: %TRUE if the method call completed successfully.
 *
 * Since: 1.5.8
 **/
gboolean
fu_bluez_device_notify_start(FuBluezDevice *self, const gchar *uuid, GError **error)
{
	FuBluezDeviceUuidHelper *uuid_helper;
	g_autoptr(GVariant) retval = NULL;

	uuid_helper = fu_bluez_device_get_uuid_helper(self, uuid, error);
	if (uuid_helper == NULL)
		return FALSE;
	if (!fu_bluez_device_ensure_uuid_helper_proxy(uuid_helper, error))
		return FALSE;
	retval = g_dbus_proxy_call_sync(uuid_helper->proxy,
					"StartNotify",
					NULL,
					G_DBUS_CALL_FLAGS_NONE,
					-1,
					NULL,
					error);
	if (retval == NULL) {
		g_prefix_error(error, "Failed to enable notifications: ");
		return FALSE;
	}

	return TRUE;
}

/**
 * fu_bluez_device_notify_stop:
 * @self: a #FuBluezDevice
 * @uuid: the UUID, e.g. `00cde35c-7062-11eb-9439-0242ac130002`
 * @error: (nullable): optional return location for an error
 *
 * Disables notifications for property changes in a UUID (StopNotify
 * method).
 *
 * Returns: %TRUE if the method call completed successfully.
 *
 * Since: 1.5.8
 **/
gboolean
fu_bluez_device_notify_stop(FuBluezDevice *self, const gchar *uuid, GError **error)
{
	FuBluezDeviceUuidHelper *uuid_helper;
	g_autoptr(GVariant) retval = NULL;

	uuid_helper = fu_bluez_device_get_uuid_helper(self, uuid, error);
	if (uuid_helper == NULL)
		return FALSE;
	if (!fu_bluez_device_ensure_uuid_helper_proxy(uuid_helper, error))
		return FALSE;
	retval = g_dbus_proxy_call_sync(uuid_helper->proxy,
					"StopNotify",
					NULL,
					G_DBUS_CALL_FLAGS_NONE,
					-1,
					NULL,
					error);
	if (retval == NULL) {
		g_prefix_error(error, "Failed to enable notifications: ");
		return FALSE;
	}

	return TRUE;
}

static void
fu_bluez_device_incorporate(FuDevice *self, FuDevice *donor)
{
	FuBluezDevice *uself = FU_BLUEZ_DEVICE(self);
	FuBluezDevice *udonor = FU_BLUEZ_DEVICE(donor);
	FuBluezDevicePrivate *priv = GET_PRIVATE(uself);
	FuBluezDevicePrivate *privdonor = GET_PRIVATE(udonor);

	if (g_hash_table_size(priv->uuids) == 0) {
		GHashTableIter iter;
		gpointer key, value;
		g_hash_table_iter_init(&iter, privdonor->uuids);
		while (g_hash_table_iter_next(&iter, &key, &value)) {
			FuBluezDeviceUuidHelper *uuid_helper = (FuBluezDeviceUuidHelper *)value;
			fu_bluez_device_add_uuid_path(uself, (const gchar *)key, uuid_helper->path);
		}
	}
	if (priv->object_manager == NULL)
		priv->object_manager = g_object_ref(privdonor->object_manager);
	if (priv->proxy == NULL)
		priv->proxy = g_object_ref(privdonor->proxy);
}

static void
fu_bluez_device_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuBluezDevice *self = FU_BLUEZ_DEVICE(object);
	FuBluezDevicePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_OBJECT_MANAGER:
		g_value_set_object(value, priv->object_manager);
		break;
	case PROP_PROXY:
		g_value_set_object(value, priv->proxy);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_bluez_device_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuBluezDevice *self = FU_BLUEZ_DEVICE(object);
	FuBluezDevicePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_OBJECT_MANAGER:
		priv->object_manager = g_value_dup_object(value);
		break;
	case PROP_PROXY:
		priv->proxy = g_value_dup_object(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_bluez_device_finalize(GObject *object)
{
	FuBluezDevice *self = FU_BLUEZ_DEVICE(object);
	FuBluezDevicePrivate *priv = GET_PRIVATE(self);

	g_hash_table_unref(priv->uuids);
	g_object_unref(priv->proxy);
	g_object_unref(priv->object_manager);
	G_OBJECT_CLASS(fu_bluez_device_parent_class)->finalize(object);
}

static void
fu_bluez_device_init(FuBluezDevice *self)
{
	FuBluezDevicePrivate *priv = GET_PRIVATE(self);
	priv->uuids = g_hash_table_new_full(g_str_hash,
					    g_str_equal,
					    g_free,
					    (GDestroyNotify)fu_bluez_uuid_free);
}

static void
fu_bluez_device_class_init(FuBluezDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->get_property = fu_bluez_device_get_property;
	object_class->set_property = fu_bluez_device_set_property;
	object_class->finalize = fu_bluez_device_finalize;
	device_class->probe = fu_bluez_device_probe;
	device_class->setup = fu_bluez_device_setup;
	device_class->to_string = fu_bluez_device_to_string;
	device_class->incorporate = fu_bluez_device_incorporate;

	/**
	 * FuBluezDevice::changed:
	 * @self: the #FuBluezDevice instance that emitted the signal
	 * @uuid: the UUID that changed
	 *
	 * The ::changed signal is emitted when a service with a specific UUID changed.
	 *
	 * Since: 1.5.8
	 **/
	signals[SIGNAL_CHANGED] = g_signal_new("changed",
					       G_TYPE_FROM_CLASS(object_class),
					       G_SIGNAL_RUN_LAST,
					       0,
					       NULL,
					       NULL,
					       g_cclosure_marshal_VOID__STRING,
					       G_TYPE_NONE,
					       1,
					       G_TYPE_STRING);

	/**
	 * FuBluezDevice:object-manager:
	 *
	 * The object manager instance for all devices.
	 *
	 * Since: 1.5.8
	 */
	pspec = g_param_spec_object("object-manager",
				    NULL,
				    NULL,
				    G_TYPE_DBUS_OBJECT_MANAGER,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_OBJECT_MANAGER, pspec);

	/**
	 * FuBluezDevice:proxy:
	 *
	 * The D-Bus proxy for the object.
	 *
	 * Since: 1.5.8
	 */
	pspec = g_param_spec_object("proxy",
				    NULL,
				    NULL,
				    G_TYPE_DBUS_PROXY,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_PROXY, pspec);
}
