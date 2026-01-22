/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuUefiDevice"

#include "config.h"

#include "fu-bytes.h"
#include "fu-device-event-private.h"
#include "fu-device-private.h"
#include "fu-uefi-device-private.h"

/**
 * FuUefiDevice:
 *
 * A device that represents a UEFI EFI variable.
 *
 * See also: [class@FuDevice]
 */

typedef struct {
	gchar *guid;
	gchar *name;
} FuUefiDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuUefiDevice, fu_uefi_device, FU_TYPE_DEVICE);

#define GET_PRIVATE(o) (fu_uefi_device_get_instance_private(o))

#define FU_UEFI_DEVICE_INHIBIT_ID_NO_EFIVARS_SPACE "no-efivars-space"

/* private */
void
fu_uefi_device_set_guid(FuUefiDevice *self, const gchar *guid)
{
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_UEFI_DEVICE(self));

	/* same */
	if (g_strcmp0(priv->guid, guid) == 0)
		return;
	g_free(priv->guid);
	priv->guid = g_strdup(guid);
	if (guid != NULL)
		fu_device_add_instance_str(FU_DEVICE(self), "GUID", guid);
}

/* private */
const gchar *
fu_uefi_device_get_guid(FuUefiDevice *self)
{
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UEFI_DEVICE(self), NULL);
	return priv->guid;
}

/* private */
void
fu_uefi_device_set_name(FuUefiDevice *self, const gchar *name)
{
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_UEFI_DEVICE(self));

	/* same */
	if (g_strcmp0(priv->name, name) == 0)
		return;
	g_free(priv->name);
	priv->name = g_strdup(name);
	if (name != NULL)
		fu_device_add_instance_str(FU_DEVICE(self), "NAME", name);
}

/* private */
const gchar *
fu_uefi_device_get_name(FuUefiDevice *self)
{
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UEFI_DEVICE(self), NULL);
	return priv->name;
}

/**
 * fu_uefi_device_set_efivar_bytes:
 * @self: a #FuUefiDevice
 * @guid: globally unique identifier
 * @name: variable name
 * @bytes: data blob
 * @attr: attributes
 * @error: (nullable): optional return location for an error
 *
 * Sets the data to a UEFI variable in NVRAM, emulating if required.
 *
 * Returns: %TRUE on success
 *
 * Since: 2.0.5
 **/
gboolean
fu_uefi_device_set_efivar_bytes(FuUefiDevice *self,
				const gchar *guid,
				const gchar *name,
				GBytes *bytes,
				FuEfiVariableAttrs attr,
				GError **error)
{
	FuContext *ctx = fu_device_get_context(FU_DEVICE(self));
	FuDeviceEvent *event = NULL;
	g_autofree gchar *event_id = NULL;

	g_return_val_if_fail(FU_IS_UEFI_DEVICE(self), FALSE);
	g_return_val_if_fail(guid != NULL, FALSE);
	g_return_val_if_fail(name != NULL, FALSE);
	g_return_val_if_fail(bytes != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* need event ID */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event_id = g_strdup_printf("SetEfivar:Guid=%s,Name=%s,Attr=0x%x", guid, name, attr);
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		g_autoptr(GBytes) bytes_tmp = NULL;

		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return FALSE;
		bytes_tmp = fu_device_event_get_bytes(event, "Data", error);
		if (bytes_tmp == NULL)
			return FALSE;
		return fu_bytes_compare(bytes, bytes_tmp, error);
	}

	/* save */
	if (event_id != NULL)
		event = fu_device_save_event(FU_DEVICE(self), event_id);

	/* set */
	if (!fu_efivars_set_data_bytes(fu_context_get_efivars(ctx), guid, name, bytes, attr, error))
		return FALSE;

	/* save response */
	if (event != NULL)
		fu_device_event_set_bytes(event, "Data", bytes);

	/* success */
	return TRUE;
}

/**
 * fu_uefi_device_get_efivar_bytes:
 * @self: a #FuUefiDevice
 * @guid: Globally unique identifier
 * @name: Variable name
 * @attr: (nullable) (out): #FuEfiVariableAttrs, e.g. %FU_EFI_VARIABLE_ATTR_NON_VOLATILE
 * @error: (nullable): optional return location for an error
 *
 * Gets the data from a UEFI variable in NVRAM, emulating if required.
 *
 * Returns: (transfer full): a #GBytes, or %NULL on error
 *
 * Since: 2.0.5
 **/
GBytes *
fu_uefi_device_get_efivar_bytes(FuUefiDevice *self,
				const gchar *guid,
				const gchar *name,
				FuEfiVariableAttrs *attr,
				GError **error)
{
	FuContext *ctx = fu_device_get_context(FU_DEVICE(self));
	FuDeviceEvent *event = NULL;
	guint32 attr_tmp = 0;
	g_autofree gchar *event_id = NULL;
	g_autoptr(GBytes) blob = NULL;

	g_return_val_if_fail(FU_IS_UEFI_DEVICE(self), NULL);
	g_return_val_if_fail(guid != NULL, NULL);
	g_return_val_if_fail(name != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* need event ID */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event_id = g_strdup_printf("GetEfivar:Guid=%s,Name=%s", guid, name);
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return NULL;
		if (attr != NULL) {
			guint64 tmp = fu_device_event_get_i64(event, "Attr", error);
			if (tmp == G_MAXINT64)
				return NULL;
			*attr = (FuEfiVariableAttrs)tmp;
		}
		return fu_device_event_get_bytes(event, "Data", error);
	}

	/* save */
	if (event_id != NULL)
		event = fu_device_save_event(FU_DEVICE(self), event_id);

	/* read */
	blob = fu_efivars_get_data_bytes(fu_context_get_efivars(ctx), guid, name, &attr_tmp, error);
	if (blob == NULL)
		return NULL;
	if (attr != NULL)
		*attr = (FuEfiVariableAttrs)attr_tmp;

	/* save response */
	if (event != NULL) {
		fu_device_event_set_bytes(event, "Data", blob);
		fu_device_event_set_i64(event, "Attr", attr_tmp);
	}

	/* success */
	return g_steal_pointer(&blob);
}

static void
fu_uefi_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(device);
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append(str, idt, "Guid", priv->guid);
	fwupd_codec_string_append(str, idt, "Name", priv->name);
}

static void
fu_uefi_device_incorporate(FuDevice *device, FuDevice *donor)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(device);
	FuUefiDevice *udonor = FU_UEFI_DEVICE(donor);
	fu_uefi_device_set_guid(self, fu_uefi_device_get_guid(udonor));
	fu_uefi_device_set_name(self, fu_uefi_device_get_name(udonor));
}

static gboolean
fu_uefi_device_probe(FuDevice *device, GError **error)
{
	return fu_device_build_instance_id_full(device,
						FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						NULL,
						"UEFI",
						"GUID",
						"NAME",
						NULL);
}

static GBytes *
fu_uefi_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(device);
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);

	/* sanity check */
	if (priv->guid == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "no GUID");
		return NULL;
	}
	if (priv->name == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "no name");
		return NULL;
	}
	return fu_uefi_device_get_efivar_bytes(FU_UEFI_DEVICE(device),
					       priv->guid,
					       priv->name,
					       NULL,
					       error);
}

static void
fu_uefi_device_add_json(FuDevice *device, FwupdJsonObject *json_obj, FwupdCodecFlags flags)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(device);
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	GPtrArray *events = fu_device_get_events(device);

	/* optional properties */
	fwupd_json_object_add_string(json_obj, "GType", "FuUefiDevice");
	if (fu_device_get_backend_id(device) != NULL)
		fwupd_json_object_add_string(json_obj,
					     "BackendId",
					     fu_device_get_backend_id(device));
	if (priv->guid != NULL)
		fwupd_json_object_add_string(json_obj, "Guid", priv->guid);
	if (priv->name != NULL)
		fwupd_json_object_add_string(json_obj, "Name", priv->name);

#if GLIB_CHECK_VERSION(2, 80, 0)
	if (fu_device_get_created_usec(device) != 0) {
		g_autoptr(GDateTime) dt =
		    g_date_time_new_from_unix_utc_usec(fu_device_get_created_usec(device));
		g_autofree gchar *str = g_date_time_format_iso8601(dt);
		fwupd_json_object_add_string(json_obj, "Created", str);
	}
#endif

	/* events */
	if (events->len > 0) {
		g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();
		for (guint i = 0; i < events->len; i++) {
			FuDeviceEvent *event = g_ptr_array_index(events, i);
			g_autoptr(FwupdJsonObject) json_obj_tmp = fwupd_json_object_new();
			fwupd_codec_to_json(FWUPD_CODEC(event), json_obj_tmp, flags);
			fwupd_json_array_add_object(json_arr, json_obj_tmp);
		}
		fwupd_json_object_add_array(json_obj, "Events", json_arr);
	}
}

static gboolean
fu_uefi_device_from_json(FuDevice *device, FwupdJsonObject *json_obj, GError **error)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(device);
	const gchar *tmp;
	g_autoptr(FwupdJsonArray) json_array_events = NULL;

	tmp = fwupd_json_object_get_string(json_obj, "Guid", NULL);
	if (tmp != NULL)
		fu_uefi_device_set_guid(self, tmp);
	tmp = fwupd_json_object_get_string(json_obj, "Name", NULL);
	if (tmp != NULL)
		fu_uefi_device_set_name(self, tmp);
	tmp = fwupd_json_object_get_string(json_obj, "BackendId", NULL);
	if (tmp != NULL)
		fu_device_set_backend_id(device, tmp);

#if GLIB_CHECK_VERSION(2, 80, 0)
	tmp = fwupd_json_object_get_string(json_obj, "Created", NULL);
	if (tmp != NULL) {
		g_autoptr(GDateTime) dt = g_date_time_new_from_iso8601(tmp, NULL);
		if (dt != NULL)
			fu_device_set_created_usec(device, g_date_time_to_unix_usec(dt));
	}
#endif

	/* array of events */
	json_array_events = fwupd_json_object_get_array(json_obj, "Events", NULL);
	if (json_array_events != NULL) {
		for (guint i = 0; i < fwupd_json_array_get_size(json_array_events); i++) {
			g_autoptr(FuDeviceEvent) event = fu_device_event_new(NULL);
			g_autoptr(FwupdJsonObject) json_obj_tmp = NULL;

			json_obj_tmp = fwupd_json_array_get_object(json_array_events, i, error);
			if (json_obj_tmp == NULL)
				return FALSE;
			if (!fwupd_codec_from_json(FWUPD_CODEC(event), json_obj_tmp, error))
				return FALSE;
			fu_device_add_event(device, event);
		}
	}

	/* success */
	return TRUE;
}

static void
fu_uefi_device_finalize(GObject *object)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(object);
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);

	g_free(priv->guid);
	g_free(priv->name);

	G_OBJECT_CLASS(fu_uefi_device_parent_class)->finalize(object);
}

static void
fu_uefi_device_required_free_notify_cb(FuUefiDevice *self, GParamSpec *pspec, gpointer user_data)
{
	FuContext *ctx = fu_device_get_context(FU_DEVICE(self));

	if (fu_device_get_required_free(FU_DEVICE(self)) > 0) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_context_efivars_check_free_space(
			ctx,
			fu_device_get_required_free(FU_DEVICE(self)),
			&error_local)) {
			fu_device_inhibit(FU_DEVICE(self),
					  FU_UEFI_DEVICE_INHIBIT_ID_NO_EFIVARS_SPACE,
					  error_local->message);
		} else {
			fu_device_uninhibit(FU_DEVICE(self),
					    FU_UEFI_DEVICE_INHIBIT_ID_NO_EFIVARS_SPACE);
		}
	} else {
		fu_device_uninhibit(FU_DEVICE(self), FU_UEFI_DEVICE_INHIBIT_ID_NO_EFIVARS_SPACE);
	}
}

static void
fu_uefi_device_init(FuUefiDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_INHIBIT_CHILDREN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_REQUIRED_FREE);
	g_signal_connect(FU_DEVICE(self),
			 "notify::required-free",
			 G_CALLBACK(fu_uefi_device_required_free_notify_cb),
			 NULL);
}

static void
fu_uefi_device_class_init(FuUefiDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_uefi_device_finalize;
	device_class->to_string = fu_uefi_device_to_string;
	device_class->probe = fu_uefi_device_probe;
	device_class->dump_firmware = fu_uefi_device_dump_firmware;
	device_class->incorporate = fu_uefi_device_incorporate;
	device_class->from_json = fu_uefi_device_from_json;
	device_class->add_json = fu_uefi_device_add_json;
}

FuUefiDevice *
fu_uefi_device_new(const gchar *guid, const gchar *name)
{
	g_autofree gchar *backend_id = NULL;
	g_autoptr(FuUefiDevice) self = NULL;

	backend_id = g_strdup_printf("%s-%s", guid, name);
	self = g_object_new(FU_TYPE_UEFI_DEVICE, "backend-id", backend_id, NULL);
	fu_uefi_device_set_guid(self, guid);
	fu_uefi_device_set_name(self, name);
	return g_steal_pointer(&self);
}
