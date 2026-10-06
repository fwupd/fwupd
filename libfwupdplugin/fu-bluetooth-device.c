/*
 * Copyright 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 * Copyright 2024 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuBluetoothDevice"

#include "config.h"

#include <gio/gunixfdlist.h>
#include <string.h>

#include "fu-bluetooth-device.h"
#include "fu-bluetooth-proxy.h"
#include "fu-device-private.h"
#include "fu-dump.h"
#include "fu-firmware-common.h"

/**
 * FuBluetoothDevice:
 *
 * A BlueZ Bluetooth device.
 *
 * See also: [class@FuDevice]
 */

typedef struct {
	gchar *modalias;
} FuBluetoothDevicePrivate;

enum { SIGNAL_CHANGED, SIGNAL_LAST };

static guint signals[SIGNAL_LAST] = {0};

G_DEFINE_TYPE_WITH_PRIVATE(FuBluetoothDevice, fu_bluetooth_device, FU_TYPE_DEVICE)

#define GET_PRIVATE(o) (fu_bluetooth_device_get_instance_private(o))

/**
 * fu_bluetooth_device_set_modalias:
 * @self: a #FuBluetoothDevice
 * @modalias: the modalias, e.g. `bluetooth:v000ApFFFFdFFFF`
 *
 * Sets the device modalias.
 *
 * Since: 2.1.8
 **/
void
fu_bluetooth_device_set_modalias(FuBluetoothDevice *self, const gchar *modalias)
{
	FuBluetoothDevicePrivate *priv = GET_PRIVATE(self);
	gsize modaliaslen;
	guint16 vid = 0x0;
	guint16 pid = 0x0;
	guint16 rev = 0x0;

	g_return_if_fail(FU_IS_BLUETOOTH_DEVICE(self));
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
	g_set_str(&priv->modalias, modalias);
}

/**
 * fu_bluetooth_device_emit_changed:
 * @self: a #FuBluetoothDevice
 * @uuid: the UUID
 *
 * Emits a changed signal from the device with the given UUID.
 *
 * Since: 2.1.8
 **/
void
fu_bluetooth_device_emit_changed(FuBluetoothDevice *self, const gchar *uuid)
{
	g_return_if_fail(FU_IS_BLUETOOTH_DEVICE(self));
	g_return_if_fail(uuid != NULL);
	g_signal_emit(self, signals[SIGNAL_CHANGED], 0, uuid);
}

static void
fu_bluetooth_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuBluetoothDevice *self = FU_BLUETOOTH_DEVICE(device);
	FuBluetoothDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append(str, idt, "Modalias", priv->modalias);
}

/* see https://www.bluetooth.com/specifications/dis-1-2/ spec */
static gboolean
fu_bluetooth_device_parse_device_information_service(FuBluetoothDevice *self, GError **error)
{
	g_autofree gchar *model_number = NULL;
	g_autofree gchar *manufacturer = NULL;

	model_number =
	    fu_bluetooth_device_read_string(self, FU_BLUETOOTH_DEVICE_UUID_DI_MODEL_NUMBER, NULL);
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
		    fu_bluetooth_device_read_string(self,
						    FU_BLUETOOTH_DEVICE_UUID_DI_MANUFACTURER_NAME,
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
		    fu_bluetooth_device_read_string(self,
						    FU_BLUETOOTH_DEVICE_UUID_DI_SERIAL_NUMBER,
						    NULL);
		if (serial_number != NULL)
			fu_device_set_serial(FU_DEVICE(self), serial_number);
	}

	if (!fu_device_has_private_flag(FU_DEVICE(self),
					FU_DEVICE_PRIVATE_FLAG_NO_GENERIC_VERSION)) {
		g_autofree gchar *fw_revision =
		    fu_bluetooth_device_read_string(self,
						    FU_BLUETOOTH_DEVICE_UUID_DI_FIRMWARE_REVISION,
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

static gboolean
fu_bluetooth_device_probe(FuDevice *device, GError **error)
{
	FuBluetoothDevice *self = FU_BLUETOOTH_DEVICE(device);
	FuDevice *proxy;

	/* call back into the proxy implementation */
	if (!fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		proxy = fu_device_get_proxy(FU_DEVICE(self), error);
		if (proxy == NULL)
			return FALSE;
		if (!fu_device_probe(proxy, error))
			return FALSE;
	}

	/* try to parse Device Information service if available */
	return fu_bluetooth_device_parse_device_information_service(self, error);
}

static gboolean
fu_bluetooth_device_reload(FuDevice *device, GError **error)
{
	FuBluetoothDevice *self = FU_BLUETOOTH_DEVICE(device);
	return fu_bluetooth_device_parse_device_information_service(self, error);
}

/**
 * fu_bluetooth_device_read:
 * @self: a #FuBluetoothDevice
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
fu_bluetooth_device_read(FuBluetoothDevice *self, const gchar *uuid, GError **error)
{
	FuDeviceEvent *event = NULL;
	FuDevice *proxy;
	g_autofree gchar *event_id = NULL;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_BLUETOOTH_DEVICE(self), NULL);
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

	/* call into proxy */
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return NULL;
	buf = fu_bluetooth_proxy_read(FU_BLUETOOTH_PROXY(proxy), uuid, &error_local);
	if (buf == NULL) {
		if (event != NULL)
			fu_device_event_set_error(event, error_local);
		g_propagate_error(error, g_steal_pointer(&error_local));
		return NULL;
	}

	/* save response */
	if (event != NULL)
		fu_device_event_set_byte_array(event, "Data", buf);

	/* success */
	return g_steal_pointer(&buf);
}

/**
 * fu_bluetooth_device_read_string:
 * @self: a #FuBluetoothDevice
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
fu_bluetooth_device_read_string(FuBluetoothDevice *self, const gchar *uuid, GError **error)
{
	g_autoptr(GByteArray) buf = fu_bluetooth_device_read(self, uuid, error);
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
 * fu_bluetooth_device_write:
 * @self: a #FuBluetoothDevice
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
fu_bluetooth_device_write(FuBluetoothDevice *self,
			  const gchar *uuid,
			  GByteArray *buf,
			  GError **error)
{
	FuDevice *proxy;
	FuDeviceEvent *event = NULL;
	g_autofree gchar *event_id = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_BLUETOOTH_DEVICE(self), FALSE);
	g_return_val_if_fail(uuid != NULL, FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		g_autofree gchar *data_base64 = fu_base64_encode(buf->data, buf->len);
		event_id = g_strdup_printf("Write:Uuid=%s,Data=%s,Length=0x%x",
					   uuid,
					   data_base64,
					   buf->len);
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

	/* call into proxy */
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;
	if (!fu_bluetooth_proxy_write(FU_BLUETOOTH_PROXY(proxy), uuid, buf, &error_local)) {
		if (event != NULL)
			fu_device_event_set_error(event, error_local);
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * fu_bluetooth_device_notify_start:
 * @self: a #FuBluetoothDevice
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
fu_bluetooth_device_notify_start(FuBluetoothDevice *self, const gchar *uuid, GError **error)
{
	FuDevice *proxy;
	g_return_val_if_fail(FU_IS_BLUETOOTH_DEVICE(self), FALSE);
	g_return_val_if_fail(uuid != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;
	return fu_bluetooth_proxy_notify_start(FU_BLUETOOTH_PROXY(proxy), uuid, error);
}

/**
 * fu_bluetooth_device_notify_stop:
 * @self: a #FuBluetoothDevice
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
fu_bluetooth_device_notify_stop(FuBluetoothDevice *self, const gchar *uuid, GError **error)
{
	FuDevice *proxy;
	g_return_val_if_fail(FU_IS_BLUETOOTH_DEVICE(self), FALSE);
	g_return_val_if_fail(uuid != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;
	return fu_bluetooth_proxy_notify_stop(FU_BLUETOOTH_PROXY(proxy), uuid, error);
}

/**
 * fu_bluetooth_device_notify_acquire:
 * @self: a #FuBluetoothDevice
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
fu_bluetooth_device_notify_acquire(FuBluetoothDevice *self,
				   const gchar *uuid,
				   gint32 *mtu,
				   GError **error)
{
	FuDevice *proxy;
	g_return_val_if_fail(FU_IS_BLUETOOTH_DEVICE(self), NULL);
	g_return_val_if_fail(uuid != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return NULL;
	return fu_bluetooth_proxy_aquire_notify(FU_BLUETOOTH_PROXY(proxy), uuid, mtu, error);
}

/**
 * fu_bluetooth_device_write_acquire:
 * @self: a #FuBluetoothDevice
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
fu_bluetooth_device_write_acquire(FuBluetoothDevice *self,
				  const gchar *uuid,
				  gint32 *mtu,
				  GError **error)
{
	FuDevice *proxy;
	g_return_val_if_fail(FU_IS_BLUETOOTH_DEVICE(self), NULL);
	g_return_val_if_fail(uuid != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return NULL;
	return fu_bluetooth_proxy_aquire_write(FU_BLUETOOTH_PROXY(proxy), uuid, mtu, error);
}

static gchar *
fu_bluetooth_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint16(version_raw, fu_device_get_version_format(device));
}

static void
fu_bluetooth_device_add_json(FuDevice *device, FwupdJsonObject *json_obj, FwupdCodecFlags flags)
{
	FuBluetoothDevice *self = FU_BLUETOOTH_DEVICE(device);
	FuBluetoothDevicePrivate *priv = GET_PRIVATE(self);
	GPtrArray *icons = fu_device_get_icons(device);

	/* optional properties */
	fwupd_json_object_add_string(json_obj, "GType", "FuBluetoothDevice");
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
}

static gboolean
fu_bluetooth_device_from_json(FuDevice *device, FwupdJsonObject *json_obj, GError **error)
{
	FuBluetoothDevice *self = FU_BLUETOOTH_DEVICE(device);
	const gchar *tmp;
	gint64 tmp64 = 0;

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
		fu_bluetooth_device_set_modalias(self, tmp);
	tmp = fwupd_json_object_get_string(json_obj, "Icon", NULL);
	if (tmp != NULL)
		fu_device_add_icon(device, tmp);
	if (!fwupd_json_object_get_integer_with_default(json_obj, "Battery", &tmp64, 100, error))
		return FALSE;
	fu_device_set_battery_level(device, tmp64);

	/* success */
	return TRUE;
}

static void
fu_bluetooth_device_finalize(GObject *object)
{
	FuBluetoothDevice *self = FU_BLUETOOTH_DEVICE(object);
	FuBluetoothDevicePrivate *priv = GET_PRIVATE(self);
	g_free(priv->modalias);
	G_OBJECT_CLASS(fu_bluetooth_device_parent_class)->finalize(object);
}

static void
fu_bluetooth_device_init(FuBluetoothDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_EMULATION_TAG);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REFCOUNTED_PROXY);
	fu_device_set_proxy_gtype(FU_DEVICE(self), FU_TYPE_BLUETOOTH_PROXY);
}

static void
fu_bluetooth_device_class_init(FuBluetoothDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_bluetooth_device_finalize;
	device_class->probe = fu_bluetooth_device_probe;
	device_class->reload = fu_bluetooth_device_reload;
	device_class->to_string = fu_bluetooth_device_to_string;
	device_class->convert_version = fu_bluetooth_device_convert_version;
	device_class->from_json = fu_bluetooth_device_from_json;
	device_class->add_json = fu_bluetooth_device_add_json;

	/**
	 * FuBluetoothDevice::changed:
	 * @self: the #FuBluetoothDevice instance that emitted the signal
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
}
