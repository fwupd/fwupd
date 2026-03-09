/*
 * Copyright 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 * Copyright 2024 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuBluezDevice"

#include "config.h"

#include <gio/gunixfdlist.h>
#include <string.h>

#include "fu-bluez-device.h"
#include "fu-dump.h"
#include "fu-firmware-common.h"

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
	GPtrArray *uuids; /* element-type FuBluezDeviceUuidItem */
	gchar *modalias;
} FuBluezDevicePrivate;

typedef struct {
	gchar *uuid;
	gchar *path;
	gulong signal_id;
	GDBusProxy *proxy;
} FuBluezDeviceUuidItem;

enum { PROP_0, PROP_OBJECT_MANAGER, PROP_PROXY, PROP_LAST };

enum { SIGNAL_CHANGED, SIGNAL_LAST };

static guint signals[SIGNAL_LAST] = {0};

G_DEFINE_TYPE_WITH_PRIVATE(FuBluezDevice, fu_bluez_device, FU_TYPE_DEVICE)

#define GET_PRIVATE(o) (fu_bluez_device_get_instance_private(o))

static void
fu_bluez_device_uuid_item_free(FuBluezDeviceUuidItem *item)
{
	if (item->proxy != NULL)
		g_object_unref(item->proxy);
	g_free(item->uuid);
	g_free(item->path);
	g_free(item);
}

static FuBluezDeviceUuidItem *
fu_bluez_device_get_uuid_item(FuBluezDevice *self, const gchar *uuid, GError **error)
{
	FuBluezDevicePrivate *priv = GET_PRIVATE(self);

	for (guint i = 0; i < priv->uuids->len; i++) {
		FuBluezDeviceUuidItem *item = g_ptr_array_index(priv->uuids, i);
		if (g_strcmp0(item->uuid, uuid) == 0)
			return item;
	}
	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "UUID %s not supported", uuid);
	return NULL;
}

static void
fu_bluez_device_signal_cb(GDBusProxy *proxy,
			  GVariant *changed_properties,
			  GStrv invalidated_properties,
			  FuBluezDevice *self)
{
	FuBluezDevicePrivate *priv = GET_PRIVATE(self);
	for (guint i = 0; i < priv->uuids->len; i++) {
		FuBluezDeviceUuidItem *item = g_ptr_array_index(priv->uuids, i);
		if (g_strcmp0(item->path, g_dbus_proxy_get_object_path(proxy)) == 0)
			g_signal_emit(self, signals[SIGNAL_CHANGED], 0, item->uuid);
	}
}

/*
 * Builds the GDBusProxy of the BlueZ object identified by a UUID
 * string. If the object doesn't have a dedicated proxy yet, this
 * creates it and saves it in the FuBluezDeviceUuidItem object.
 *
 * NOTE: Currently limited to GATT characteristics.
 */
static gboolean
fu_bluez_device_ensure_uuid_item_proxy(FuBluezDevice *self,
				       FuBluezDeviceUuidItem *item,
				       GError **error)
{
	if (item->proxy != NULL)
		return TRUE;
	item->proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
						    G_DBUS_PROXY_FLAGS_NONE,
						    NULL,
						    "org.bluez",
						    item->path,
						    "org.bluez.GattCharacteristic1",
						    NULL,
						    error);
	if (item->proxy == NULL) {
		g_prefix_error_literal(error, "Failed to create GDBusProxy for item: ");
		return FALSE;
	}
	g_dbus_proxy_set_default_timeout(item->proxy, DEFAULT_PROXY_TIMEOUT);
	item->signal_id = g_signal_connect(G_DBUS_PROXY(item->proxy),
					   "g-properties-changed",
					   G_CALLBACK(fu_bluez_device_signal_cb),
					   self);
	if (item->signal_id <= 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot connect to signal of UUID %s",
			    item->uuid);
		return FALSE;
	}
	return TRUE;
}

static void
fu_bluez_device_add_uuid_path(FuBluezDevice *self, const gchar *uuid, const gchar *path)
{
	FuBluezDevicePrivate *priv = GET_PRIVATE(self);
	FuBluezDeviceUuidItem *item;
	g_return_if_fail(FU_IS_BLUEZ_DEVICE(self));
	g_return_if_fail(uuid != NULL);
	g_return_if_fail(path != NULL);

	item = g_new0(FuBluezDeviceUuidItem, 1);
	item->uuid = g_strdup(uuid);
	item->path = g_strdup(path);
	g_ptr_array_add(priv->uuids, item);
}

static void
fu_bluez_device_set_modalias(FuBluezDevice *self, const gchar *modalias)
{
	FuBluezDevicePrivate *priv = GET_PRIVATE(self);
	gsize modaliaslen;
	guint16 vid = 0x0;
	guint16 pid = 0x0;
	guint16 rev = 0x0;

	g_return_if_fail(modalias != NULL);

	/* usb:v0461p4EEFd0001 */
	modaliaslen = strlen(modalias);
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
	if (vid != 0x0) {
		fu_device_set_vid(FU_DEVICE(self), vid);
		fu_device_add_instance_u16(FU_DEVICE(self), "VID", vid);
	}
	if (pid != 0x0) {
		fu_device_set_pid(FU_DEVICE(self), pid);
		fu_device_add_instance_u16(FU_DEVICE(self), "PID", pid);
	}
	fu_device_add_instance_u16(FU_DEVICE(self), "REV", rev);
	fu_device_build_instance_id_full(FU_DEVICE(self),
					 FU_DEVICE_INSTANCE_FLAG_GENERIC |
					     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "BLUETOOTH",
					 "VID",
					 NULL);
	fu_device_build_instance_id_full(FU_DEVICE(self),
					 FU_DEVICE_INSTANCE_FLAG_GENERIC |
					     FU_DEVICE_INSTANCE_FLAG_VISIBLE |
					     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "BLUETOOTH",
					 "VID",
					 "PID",
					 NULL);
	if (fu_device_has_private_flag(FU_DEVICE(self),
				       FU_DEVICE_PRIVATE_FLAG_ADD_INSTANCE_ID_REV)) {
		fu_device_build_instance_id_full(FU_DEVICE(self),
						 FU_DEVICE_INSTANCE_FLAG_GENERIC |
						     FU_DEVICE_INSTANCE_FLAG_VISIBLE |
						     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						 NULL,
						 "BLUETOOTH",
						 "VID",
						 "PID",
						 "REV",
						 NULL);
	}

	/* set vendor ID */
	if (vid != 0x0) {
		g_autofree gchar *vendor_id = g_strdup_printf("%04X", vid);
		fu_device_build_vendor_id(FU_DEVICE(self), "BLUETOOTH", vendor_id); /* compat */
		fu_device_build_vendor_id_u16(FU_DEVICE(self), "BLUETOOTH", vid);
	}

	/* set version if the revision has been set */
	if (rev != 0x0 &&
	    fu_device_get_version_format(FU_DEVICE(self)) == FWUPD_VERSION_FORMAT_UNKNOWN &&
	    !fu_device_has_private_flag(FU_DEVICE(self),
					FU_DEVICE_PRIVATE_FLAG_NO_GENERIC_VERSION)) {
		fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_BCD);
		fu_device_set_version_raw(FU_DEVICE(self), rev);
	}

	/* save in case we need this for emulation */
	priv->modalias = g_strdup(modalias);
}

static void
fu_bluez_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuBluezDevice *self = FU_BLUEZ_DEVICE(device);
	FuBluezDevicePrivate *priv = GET_PRIVATE(self);

	fwupd_codec_string_append(str, idt, "Modalias", priv->modalias);
	for (guint i = 0; i < priv->uuids->len; i++) {
		FuBluezDeviceUuidItem *item = g_ptr_array_index(priv->uuids, i);
		fwupd_codec_string_append(str, idt + 1, item->uuid, item->path);
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
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "property %s not found in %s",
			    prop_name,
			    obj_path);
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

static gchar *
fu_bluez_device_get_interface_uuid(FuBluezDevice *self,
				   GDBusObject *obj,
				   const gchar *obj_path,
				   const gchar *iface_name,
				   GError **error)
{
	g_autofree gchar *obj_uuid = NULL;
	g_autoptr(GDBusInterface) iface = NULL;

	iface = g_dbus_object_get_interface(obj, iface_name);
	if (iface == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no %s interface",
			    iface_name);
		return NULL;
	}
	obj_uuid = fu_bluez_device_get_ble_string_property(obj_path, iface_name, "UUID", error);
	if (obj_uuid == NULL) {
		g_prefix_error(error, "failed to get %s property: ", iface_name);
		return NULL;
	}

	/* success */
	return g_steal_pointer(&obj_uuid);
}

/*
 * Populates the {item : object_path} entry of a device for its
 * characteristic.
 */
static gboolean
fu_bluez_device_add_characteristic_uuid(FuBluezDevice *self,
					GDBusObject *obj,
					const gchar *obj_path,
					const gchar *iface_name,
					GError **error)
{
	g_autofree gchar *obj_uuid = NULL;

	obj_uuid = fu_bluez_device_get_interface_uuid(self, obj, obj_path, iface_name, error);
	if (obj_uuid == NULL)
		return FALSE;
	fu_bluez_device_add_uuid_path(self, obj_uuid, obj_path);
	return TRUE;
}

static gboolean
fu_bluez_device_add_instance_by_service_uuid(FuBluezDevice *self,
					     GDBusObject *obj,
					     const gchar *obj_path,
					     const gchar *iface_name,
					     GError **error)
{
	g_autofree gchar *obj_uuid = NULL;

	/* register device by service UUID */
	obj_uuid = fu_bluez_device_get_interface_uuid(self, obj, obj_path, iface_name, error);
	if (obj_uuid == NULL)
		return FALSE;
	fu_device_add_instance_str(FU_DEVICE(self), "GATT", obj_uuid);
	if (!fu_device_build_instance_id_full(FU_DEVICE(self),
					      FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					      error,
					      "BLUETOOTH",
					      "GATT",
					      NULL)) {
		g_prefix_error(error, "failed to register %s service: ", obj_uuid);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_bluez_device_read_battery_interface(FuBluezDevice *self,
				       GDBusObject *obj,
				       const gchar *obj_path,
				       const gchar *iface_name,
				       GError **error)
{
	guint8 percentage = FWUPD_BATTERY_LEVEL_INVALID;
	g_autoptr(GDBusInterface) iface = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GVariant) obj_percentage = NULL;

	iface = g_dbus_object_get_interface(obj, iface_name);
	if (iface == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no %s interface",
			    iface_name);
		return FALSE;
	}

	/* sometimes battery service announced but has no value, no error in that case */
	obj_percentage =
	    fu_bluez_device_get_ble_property(obj_path, iface_name, "Percentage", &error_local);
	if (obj_percentage == NULL) {
		g_debug("failed to get battery percentage from org.bluez.Battery1: %s",
			error_local->message);
		/* return TRUE since that situation should not affect to further interaction */
		return TRUE;
	}

	percentage = g_variant_get_byte(obj_percentage);
	fu_device_set_battery_level(FU_DEVICE(self), percentage);

	/* success */
	return TRUE;
}

/* see https://www.bluetooth.com/specifications/dis-1-2/ spec */
static gboolean
fu_bluez_device_parse_device_information_service(FuBluezDevice *self, GError **error)
{
	g_autofree gchar *model_number = NULL;
	g_autofree gchar *manufacturer = NULL;

	model_number =
	    fu_bluez_device_read_string(self, FU_BLUEZ_DEVICE_UUID_DI_MODEL_NUMBER, NULL);
	if (model_number != NULL) {
		fu_device_add_instance_str(FU_DEVICE(self), "MODEL", model_number);
		if (!fu_device_build_instance_id_full(FU_DEVICE(self),
						      FU_DEVICE_INSTANCE_FLAG_GENERIC |
							  FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						      error,
						      "BLUETOOTH",
						      "MODEL",
						      NULL)) {
			g_prefix_error(error, "failed to register model %s: ", model_number);
			return FALSE;
		}
		manufacturer =
		    fu_bluez_device_read_string(self,
						FU_BLUEZ_DEVICE_UUID_DI_MANUFACTURER_NAME,
						NULL);
		if (manufacturer != NULL) {
			fu_device_add_instance_str(FU_DEVICE(self), "MANUFACTURER", manufacturer);
			if (!fu_device_build_instance_id_full(FU_DEVICE(self),
							      FU_DEVICE_INSTANCE_FLAG_GENERIC |
								  FU_DEVICE_INSTANCE_FLAG_QUIRKS,
							      error,
							      "BLUETOOTH",
							      "MANUFACTURER",
							      "MODEL",
							      NULL)) {
				g_prefix_error(error,
					       "failed to register manufacturer %s: ",
					       manufacturer);
				return FALSE;
			}
		}
	}

	if (!fu_device_has_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_NO_SERIAL_NUMBER)) {
		g_autofree gchar *serial_number =
		    fu_bluez_device_read_string(self, FU_BLUEZ_DEVICE_UUID_DI_SERIAL_NUMBER, NULL);
		if (serial_number != NULL)
			fu_device_set_serial(FU_DEVICE(self), serial_number);
	}

	if (!fu_device_has_private_flag(FU_DEVICE(self),
					FU_DEVICE_PRIVATE_FLAG_NO_GENERIC_VERSION)) {
		g_autofree gchar *fw_revision =
		    fu_bluez_device_read_string(self,
						FU_BLUEZ_DEVICE_UUID_DI_FIRMWARE_REVISION,
						NULL);
		if (fw_revision != NULL) {
			fu_device_set_version_format(FU_DEVICE(self),
						     fu_version_guess_format(fw_revision));
			fu_device_set_version(FU_DEVICE(self),
					      fw_revision); /* nocheck:set-version */
		}
	}

	/* success */
	return TRUE;
}

/*
 * Populates the {item : object_path} entries of a device for all its
 * characteristics.
 */
static gboolean
fu_bluez_device_ensure_gatt_interfaces(FuBluezDevice *self, GError **error)
{
	FuBluezDevicePrivate *priv = GET_PRIVATE(self);
	guint valid = 0;
	g_autolist(GDBusObject) obj_list = NULL;

	obj_list = g_dbus_object_manager_get_objects(priv->object_manager);
	for (GList *l = obj_list; l != NULL; l = l->next) {
		GDBusObject *obj = G_DBUS_OBJECT(l->data);
		const gchar *obj_path = g_dbus_object_get_object_path(obj);

		/* not us */
		if (!g_str_has_prefix(g_dbus_object_get_object_path(obj),
				      g_dbus_proxy_get_object_path(priv->proxy)))
			continue;

		/* add characteristics UUID for reading and writing */
		if (g_dbus_object_get_interface(obj, "org.bluez.GattCharacteristic1")) {
			if (!fu_bluez_device_add_characteristic_uuid(
				self,
				obj,
				obj_path,
				"org.bluez.GattCharacteristic1",
				error)) {
				g_prefix_error_literal(error,
						       "failed to add characteristic UUID: ");
				return FALSE;
			}
			valid += 1;
		}
		if (g_dbus_object_get_interface(obj, "org.bluez.GattService1")) {
			if (!fu_bluez_device_add_instance_by_service_uuid(self,
									  obj,
									  obj_path,
									  "org.bluez.GattService1",
									  error)) {
				g_prefix_error_literal(error, "failed to add service UUID: ");
				return FALSE;
			}
			valid += 1;
		}

		/* battery level is optional */
		if (g_dbus_object_get_interface(obj, "org.bluez.Battery1")) {
			if (!fu_bluez_device_read_battery_interface(self,
								    obj,
								    obj_path,
								    "org.bluez.Battery1",
								    error)) {
				g_prefix_error_literal(error, "failed to add battery: ");
				return FALSE;
			}
		}
	}

	/* found nothing */
	if (valid == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no supported GATT characteristic or service");
		return FALSE;
	}

	/* success */
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
	g_autoptr(GVariant) val_alias = NULL;

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return fu_bluez_device_parse_device_information_service(self, error);

	/* sanity check */
	if (priv->proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy set");
		return FALSE;
	}

	val_address = g_dbus_proxy_get_cached_property(priv->proxy, "Address");
	if (val_address == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "No required BLE address");
		return FALSE;
	}
	fu_device_set_logical_id(device, g_variant_get_string(val_address, NULL));
	val_adapter = g_dbus_proxy_get_cached_property(priv->proxy, "Adapter");
	if (val_adapter != NULL)
		fu_device_set_physical_id(device, g_variant_get_string(val_adapter, NULL));
	val_name = g_dbus_proxy_get_cached_property(priv->proxy, "Name");
	if (val_name != NULL) {
		fu_device_set_name(device, g_variant_get_string(val_name, NULL));
		/* register the device by its alias, since modalias could be absent */
		fu_device_add_instance_str(FU_DEVICE(self),
					   "NAME",
					   g_variant_get_string(val_name, NULL));
		fu_device_build_instance_id_full(FU_DEVICE(self),
						 FU_DEVICE_INSTANCE_FLAG_VISIBLE |
						     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						 NULL,
						 "BLUETOOTH",
						 "NAME",
						 NULL);
	}
	val_alias = g_dbus_proxy_get_cached_property(priv->proxy, "Alias");
	if (val_alias != NULL) {
		fu_device_set_name(device, g_variant_get_string(val_alias, NULL));
		/* register the device by its alias, since modalias could be absent */
		fu_device_add_instance_str(FU_DEVICE(self),
					   "ALIAS",
					   g_variant_get_string(val_alias, NULL));
		fu_device_build_instance_id_full(FU_DEVICE(self),
						 FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						 NULL,
						 "BLUETOOTH",
						 "ALIAS",
						 NULL);
	}
	val_icon = g_dbus_proxy_get_cached_property(priv->proxy, "Icon");
	if (val_icon != NULL)
		fu_device_add_icon(device, g_variant_get_string(val_icon, NULL));
	val_modalias = g_dbus_proxy_get_cached_property(priv->proxy, "Modalias");
	if (val_modalias != NULL)
		fu_bluez_device_set_modalias(self, g_variant_get_string(val_modalias, NULL));

	/* success, if we added one service or characteristic */
	if (!fu_bluez_device_ensure_gatt_interfaces(self, error))
		return FALSE;

	/* try to parse Device Information service if available */
	return fu_bluez_device_parse_device_information_service(self, error);
}

static gboolean
fu_bluez_device_reload(FuDevice *device, GError **error)
{
	FuBluezDevice *self = FU_BLUEZ_DEVICE(device);
	return fu_bluez_device_parse_device_information_service(self, error);
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
	FuBluezDeviceUuidItem *item;
	guint8 byte;
	FuDeviceEvent *event = NULL;
	g_autofree gchar *event_id = NULL;
	g_autofree gchar *title = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GVariantBuilder) builder = NULL;
	g_autoptr(GVariantIter) iter = NULL;
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail(FU_IS_BLUEZ_DEVICE(self), NULL);
	g_return_val_if_fail(uuid != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* need event ID */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event_id = g_strdup_printf("Read:Uuid=%s", uuid);
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return NULL;
		if (!fu_device_event_check_error(event, error))
			return NULL;
		return fu_device_event_get_byte_array(event, "Data", error);
	}

	/* save */
	if (event_id != NULL)
		event = fu_device_save_event(FU_DEVICE(self), event_id);

	item = fu_bluez_device_get_uuid_item(self, uuid, error);
	if (item == NULL)
		return NULL;
	if (!fu_bluez_device_ensure_uuid_item_proxy(self, item, error))
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

	val = g_dbus_proxy_call_sync(item->proxy,
				     "ReadValue",
				     g_variant_new("(a{sv})", builder),
				     G_DBUS_CALL_FLAGS_NONE,
				     -1,
				     NULL,
				     error);
	if (val == NULL) {
		g_prefix_error_literal(error, "Failed to read GattCharacteristic1: ");
		return NULL;
	}
	g_variant_get(val, "(ay)", &iter);
	while (g_variant_iter_loop(iter, "y", &byte))
		g_byte_array_append(buf, &byte, 1);

	/* debug a bit */
	title = g_strdup_printf("ReadValue[%s]", uuid);
	fu_dump_raw(G_LOG_DOMAIN, title, buf->data, buf->len);

	/* save response */
	if (event != NULL)
		fu_device_event_set_byte_array(event, "Data", buf);

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
 * Returns: (transfer full): NUL-terminated string, or %NULL for error
 *
 * Since: 1.5.7
 **/
gchar *
fu_bluez_device_read_string(FuBluezDevice *self, const gchar *uuid, GError **error)
{
	g_autoptr(GByteArray) buf = fu_bluez_device_read(self, uuid, error);
	if (buf == NULL)
		return NULL;
	if (!g_utf8_validate((const gchar *)buf->data, buf->len, NULL)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "UUID %s did not return a valid UTF-8 string",
			    uuid);
		return NULL;
	}
	return g_strndup((const gchar *)buf->data, buf->len);
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
	FuBluezDeviceUuidItem *item;
	FuDeviceEvent *event = NULL;
	g_autofree gchar *event_id = NULL;
	g_autofree gchar *title = NULL;
	g_autoptr(GVariantBuilder) opt_builder = NULL;
	g_autoptr(GVariantBuilder) val_builder = NULL;
	g_autoptr(GVariant) ret = NULL;
	GVariant *opt_variant = NULL;
	GVariant *val_variant = NULL;

	g_return_val_if_fail(FU_IS_BLUEZ_DEVICE(self), FALSE);
	g_return_val_if_fail(uuid != NULL, FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		g_autofree gchar *data_base64 = g_base64_encode(buf->data, buf->len);
		event_id = g_strdup_printf("Write:Uuid=%s,Data=%s,Length=0x%x",
					   uuid,
					   data_base64,
					   (guint)buf->len);
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return FALSE;
		return fu_device_event_check_error(event, error);
	}

	/* save */
	if (event_id != NULL)
		event = fu_device_save_event(FU_DEVICE(self), event_id);

	item = fu_bluez_device_get_uuid_item(self, uuid, error);
	if (item == NULL)
		return FALSE;
	if (!fu_bluez_device_ensure_uuid_item_proxy(self, item, error))
		return FALSE;

	/* debug a bit */
	title = g_strdup_printf("WriteValue[%s]", uuid);
	fu_dump_raw(G_LOG_DOMAIN, title, buf->data, buf->len);

	/* build the value variant */
	val_builder = g_variant_builder_new(G_VARIANT_TYPE("ay"));
	for (gsize i = 0; i < buf->len; i++)
		g_variant_builder_add(val_builder, "y", buf->data[i]);
	val_variant = g_variant_new("ay", val_builder);

	/* build the options variant (offset = 0) */
	opt_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(opt_builder, "{sv}", "offset", g_variant_new_uint16(0));
	opt_variant = g_variant_new("a{sv}", opt_builder);

	ret = g_dbus_proxy_call_sync(item->proxy,
				     "WriteValue",
				     g_variant_new("(@ay@a{sv})", val_variant, opt_variant),
				     G_DBUS_CALL_FLAGS_NONE,
				     -1,
				     NULL,
				     error);
	if (ret == NULL) {
		g_prefix_error_literal(error, "Failed to write GattCharacteristic1: ");
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
	FuBluezDeviceUuidItem *item;
	g_autoptr(GVariant) retval = NULL;

	g_return_val_if_fail(FU_IS_BLUEZ_DEVICE(self), FALSE);
	g_return_val_if_fail(uuid != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	item = fu_bluez_device_get_uuid_item(self, uuid, error);
	if (item == NULL)
		return FALSE;
	if (!fu_bluez_device_ensure_uuid_item_proxy(self, item, error))
		return FALSE;
	retval = g_dbus_proxy_call_sync(item->proxy,
					"StartNotify",
					NULL,
					G_DBUS_CALL_FLAGS_NONE,
					-1,
					NULL,
					error);
	if (retval == NULL) {
		g_prefix_error_literal(error, "Failed to enable notifications: ");
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
	FuBluezDeviceUuidItem *item;
	g_autoptr(GVariant) retval = NULL;

	g_return_val_if_fail(FU_IS_BLUEZ_DEVICE(self), FALSE);
	g_return_val_if_fail(uuid != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	item = fu_bluez_device_get_uuid_item(self, uuid, error);
	if (item == NULL)
		return FALSE;
	if (!fu_bluez_device_ensure_uuid_item_proxy(self, item, error))
		return FALSE;
	retval = g_dbus_proxy_call_sync(item->proxy,
					"StopNotify",
					NULL,
					G_DBUS_CALL_FLAGS_NONE,
					-1,
					NULL,
					error);
	if (retval == NULL) {
		g_prefix_error_literal(error, "Failed to enable notifications: ");
		return FALSE;
	}

	return TRUE;
}

static FuIOChannel *
fu_bluez_device_method_acquire(FuBluezDevice *self,
			       const gchar *method,
			       const gchar *uuid,
			       gint32 *mtu,
			       GError **error)
{
#ifdef HAVE_GIO_UNIX
	FuBluezDeviceUuidItem *item;
	GVariant *opt_variant = NULL;
	gint fd_id;
	g_autoptr(GVariantBuilder) opt_builder = NULL;
	g_autoptr(GVariant) val = NULL;
	g_autoptr(GUnixFDList) out_fd_list = NULL;

	item = fu_bluez_device_get_uuid_item(self, uuid, error);
	if (item == NULL)
		return NULL;
	if (!fu_bluez_device_ensure_uuid_item_proxy(self, item, error))
		return NULL;

	opt_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	opt_variant = g_variant_new("a{sv}", opt_builder);

	val = g_dbus_proxy_call_with_unix_fd_list_sync(item->proxy,
						       method,
						       g_variant_new("(@a{sv})", opt_variant),
						       G_DBUS_CALL_FLAGS_NONE,
						       -1,
						       NULL, /* fd list */
						       &out_fd_list,
						       NULL,
						       error);
	if (val == NULL) {
		g_prefix_error(error, "failed to call %s: ", method);
		return NULL;
	}

	g_variant_get_child(val, 0, "h", &fd_id);
	if (mtu != NULL)
		g_variant_get_child(val, 1, "q", mtu);

	return fu_io_channel_unix_new(g_unix_fd_list_get(out_fd_list, fd_id, NULL));
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "not supported as <gio-unix.h> not available");
	return NULL;
#endif
}

/**
 * fu_bluez_device_notify_acquire:
 * @self: a #FuBluezDevice
 * @uuid: the UUID, e.g. `00cde35c-7062-11eb-9439-0242ac130002`
 * @mtu: (out) (optional): MTU of the channel on success
 * @error: (nullable): optional return location for an error
 *
 * Acquire notifications for property changes in a UUID (AcquireNotify
 * method). Closing IO channel releases the notify.
 *
 * Returns: (transfer full): a #FuIOChannel or %NULL on error
 *
 * Since: 2.0.0
 **/
FuIOChannel *
fu_bluez_device_notify_acquire(FuBluezDevice *self, const gchar *uuid, gint32 *mtu, GError **error)
{
	g_return_val_if_fail(FU_IS_BLUEZ_DEVICE(self), NULL);
	g_return_val_if_fail(uuid != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return fu_bluez_device_method_acquire(self, "AcquireNotify", uuid, mtu, error);
}

/**
 * fu_bluez_device_write_acquire:
 * @self: a #FuBluezDevice
 * @uuid: the UUID, e.g. `00cde35c-7062-11eb-9439-0242ac130002`
 * @mtu: (out) (optional): MTU of the channel
 * @error: (nullable): optional return location for an error
 *
 * Acquire notifications for property changes in a UUID (AcquireNotify
 * method). Closing IO channel releases the notify.
 *
 * Returns: (transfer full): a #FuIOChannel or %NULL on error
 *
 * Since: 2.0.0
 **/
FuIOChannel *
fu_bluez_device_write_acquire(FuBluezDevice *self, const gchar *uuid, gint32 *mtu, GError **error)
{
	g_return_val_if_fail(FU_IS_BLUEZ_DEVICE(self), NULL);
	g_return_val_if_fail(uuid != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return fu_bluez_device_method_acquire(self, "AcquireWrite", uuid, mtu, error);
}

static void
fu_bluez_device_incorporate(FuDevice *device, FuDevice *donor)
{
	FuBluezDevice *uself = FU_BLUEZ_DEVICE(device);
	FuBluezDevice *udonor = FU_BLUEZ_DEVICE(donor);
	FuBluezDevicePrivate *priv = GET_PRIVATE(uself);
	FuBluezDevicePrivate *privdonor = GET_PRIVATE(udonor);

	g_return_if_fail(FU_IS_BLUEZ_DEVICE(device));
	g_return_if_fail(FU_IS_BLUEZ_DEVICE(donor));

	for (guint i = 0; i < privdonor->uuids->len; i++) {
		FuBluezDeviceUuidItem *item = g_ptr_array_index(privdonor->uuids, i);
		fu_bluez_device_add_uuid_path(uself, item->uuid, item->path);
	}
	if (priv->object_manager == NULL && privdonor->object_manager != NULL)
		priv->object_manager = g_object_ref(privdonor->object_manager);
	if (priv->proxy == NULL && privdonor->proxy != NULL)
		priv->proxy = g_object_ref(privdonor->proxy);
}

static gchar *
fu_bluez_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint16(version_raw, fu_device_get_version_format(device));
}

static void
fu_bluez_device_add_json(FuDevice *device, FwupdJsonObject *json_obj, FwupdCodecFlags flags)
{
	FuBluezDevice *self = FU_BLUEZ_DEVICE(device);
	FuBluezDevicePrivate *priv = GET_PRIVATE(self);
	GPtrArray *icons = fu_device_get_icons(device);

	/* optional properties */
	fwupd_json_object_add_string(json_obj, "GType", "FuBluezDevice");
	fwupd_json_object_add_string(json_obj, "PhysicalId", fu_device_get_physical_id(device));
	fwupd_json_object_add_string(json_obj, "LogicalId", fu_device_get_logical_id(device));
	fwupd_json_object_add_string(json_obj, "BackendId", fu_device_get_backend_id(device));
	fwupd_json_object_add_string(json_obj, "Name", fu_device_get_name(device));
	fwupd_json_object_add_string(json_obj, "Modalias", priv->modalias);
	fwupd_json_object_add_integer(json_obj, "Battery", fu_device_get_battery_level(device));
	if (icons->len > 0) {
		const gchar *icon = g_ptr_array_index(icons, 0);
		fwupd_json_object_add_string(json_obj, "Icon", icon);
	}

	/* UUID -> path */
	if (priv->uuids->len > 0) {
		g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();
		for (guint i = 0; i < priv->uuids->len; i++) {
			FuBluezDeviceUuidItem *item = g_ptr_array_index(priv->uuids, i);
			g_autoptr(FwupdJsonObject) json_obj_tmp = fwupd_json_object_new();
			fwupd_json_object_add_string(json_obj_tmp, "Uuid", item->uuid);
			fwupd_json_object_add_string(json_obj_tmp, "Path", item->path);
			fwupd_json_array_add_object(json_arr, json_obj_tmp);
		}
		fwupd_json_object_add_array(json_obj, "Uuids", json_arr);
	}
}

static gboolean
fu_bluez_device_from_json(FuDevice *device, FwupdJsonObject *json_obj, GError **error)
{
	FuBluezDevice *self = FU_BLUEZ_DEVICE(device);
	const gchar *tmp;
	gint64 tmp64 = 0;
	g_autoptr(FwupdJsonArray) json_array_uuids = NULL;

	tmp = fwupd_json_object_get_string(json_obj, "PhysicalId", NULL);
	if (tmp != NULL)
		fu_device_set_physical_id(device, tmp);
	tmp = fwupd_json_object_get_string(json_obj, "LogicalId", NULL);
	if (tmp != NULL)
		fu_device_set_logical_id(device, tmp);
	tmp = fwupd_json_object_get_string(json_obj, "BackendId", NULL);
	if (tmp != NULL)
		fu_device_set_backend_id(device, tmp);
	tmp = fwupd_json_object_get_string(json_obj, "Name", NULL);
	if (tmp != NULL)
		fu_device_set_name(device, tmp);
	tmp = fwupd_json_object_get_string(json_obj, "Modalias", NULL);
	if (tmp != NULL)
		fu_bluez_device_set_modalias(self, tmp);
	tmp = fwupd_json_object_get_string(json_obj, "Icon", NULL);
	if (tmp != NULL)
		fu_device_add_icon(device, tmp);
	if (!fwupd_json_object_get_integer_with_default(json_obj, "Battery", &tmp64, 100, error))
		return FALSE;
	fu_device_set_battery_level(device, tmp64);

	/* array of UUIDs -> paths */
	json_array_uuids = fwupd_json_object_get_array(json_obj, "Uuids", NULL);
	if (json_array_uuids != NULL) {
		for (guint i = 0; i < fwupd_json_array_get_size(json_array_uuids); i++) {
			const gchar *uuid;
			const gchar *path;
			g_autoptr(FwupdJsonObject) json_obj_tmp = NULL;

			json_obj_tmp = fwupd_json_array_get_object(json_array_uuids, i, error);
			if (json_obj_tmp == NULL)
				return FALSE;
			uuid = fwupd_json_object_get_string(json_obj_tmp, "Uuid", error);
			if (uuid == NULL)
				return FALSE;
			path = fwupd_json_object_get_string(json_obj_tmp, "Path", error);
			if (path == NULL)
				return FALSE;
			fu_bluez_device_add_uuid_path(self, uuid, path);
		}
	}

	/* success */
	return TRUE;
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

	g_free(priv->modalias);
	g_ptr_array_unref(priv->uuids);
	if (priv->proxy != NULL)
		g_object_unref(priv->proxy);
	if (priv->object_manager != NULL)
		g_object_unref(priv->object_manager);
	G_OBJECT_CLASS(fu_bluez_device_parent_class)->finalize(object);
}

static void
fu_bluez_device_init(FuBluezDevice *self)
{
	FuBluezDevicePrivate *priv = GET_PRIVATE(self);
	priv->uuids =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_bluez_device_uuid_item_free);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_EMULATION_TAG);
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
	device_class->reload = fu_bluez_device_reload;
	device_class->to_string = fu_bluez_device_to_string;
	device_class->incorporate = fu_bluez_device_incorporate;
	device_class->convert_version = fu_bluez_device_convert_version;
	device_class->from_json = fu_bluez_device_from_json;
	device_class->add_json = fu_bluez_device_add_json;

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
