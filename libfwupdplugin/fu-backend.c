/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuBackend"

#include "config.h"

#include "fu-backend-private.h"
#include "fu-device-private.h"
#include "fu-string.h"

/**
 * FuBackend:
 *
 * An device discovery backend, for instance USB, BlueZ or UDev.
 *
 * See also: [class@FuDevice]
 */

typedef struct {
	FuContext *ctx;
	gchar *name;
	gboolean enabled;
	gboolean done_setup;
	gboolean can_invalidate;
	GType device_gtype;
	GHashTable *devices; /* device_id : * FuDevice */
	GThread *thread_init;
} FuBackendPrivate;

enum { SIGNAL_ADDED, SIGNAL_REMOVED, SIGNAL_CHANGED, SIGNAL_LAST };

enum { PROP_0, PROP_NAME, PROP_CAN_INVALIDATE, PROP_CONTEXT, PROP_DEVICE_GTYPE, PROP_LAST };

static guint signals[SIGNAL_LAST] = {0};

static void
fu_backend_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FuBackend,
		       fu_backend,
		       G_TYPE_OBJECT,
		       0,
		       G_ADD_PRIVATE(FuBackend)
			   G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC, fu_backend_codec_iface_init));

#define GET_PRIVATE(o) (fu_backend_get_instance_private(o))

/**
 * fu_backend_device_added:
 * @self: a #FuBackend
 * @device: a device
 *
 * Emits a signal that indicates the device has been added.
 *
 * Since: 1.6.1
 **/
void
fu_backend_device_added(FuBackend *self, FuDevice *device)
{
	FuBackendPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_BACKEND(self));
	g_return_if_fail(FU_IS_DEVICE(device));
	g_return_if_fail(priv->thread_init == g_thread_self());

	/* assign context if set */
	if (priv->ctx != NULL)
		fu_device_set_context(device, priv->ctx);

	/* set backend ID if required */
	if (fu_device_get_backend_id(device) == NULL)
		fu_device_set_backend_id(device, priv->name);

	/* sanity check */
	if (g_hash_table_contains(priv->devices, fu_device_get_backend_id(device))) {
		g_warning("replacing existing device with backend_id %s",
			  fu_device_get_backend_id(device));
	}

	/* add */
	g_hash_table_insert(priv->devices,
			    g_strdup(fu_device_get_backend_id(device)),
			    g_object_ref(device));
	g_signal_emit(self, signals[SIGNAL_ADDED], 0, device);
}

/**
 * fu_backend_device_removed:
 * @self: a #FuBackend
 * @device: a device
 *
 * Emits a signal that indicates the device has been removed.
 *
 * Since: 1.6.1
 **/
void
fu_backend_device_removed(FuBackend *self, FuDevice *device)
{
	FuBackendPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_BACKEND(self));
	g_return_if_fail(FU_IS_DEVICE(device));
	g_return_if_fail(priv->thread_init == g_thread_self());
	g_signal_emit(self, signals[SIGNAL_REMOVED], 0, device);
	g_hash_table_remove(priv->devices, fu_device_get_backend_id(device));
}

/**
 * fu_backend_device_changed:
 * @self: a #FuBackend
 * @device: a device
 *
 * Emits a signal that indicates the device has been changed.
 *
 * Since: 1.6.1
 **/
void
fu_backend_device_changed(FuBackend *self, FuDevice *device)
{
	FuBackendPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_BACKEND(self));
	g_return_if_fail(FU_IS_DEVICE(device));
	g_return_if_fail(priv->thread_init == g_thread_self());
	g_signal_emit(self, signals[SIGNAL_CHANGED], 0, device);
}

/**
 * fu_backend_registered:
 * @self: a #FuBackend
 * @device: a device
 *
 * Calls the ->registered() vfunc for the backend. This allows the backend to perform shared
 * backend actions on superclassed devices.
 *
 * Since: 1.7.4
 **/
void
fu_backend_registered(FuBackend *self, FuDevice *device)
{
	FuBackendClass *klass = FU_BACKEND_GET_CLASS(self);
	g_return_if_fail(FU_IS_BACKEND(self));
	g_return_if_fail(FU_IS_DEVICE(device));
	if (klass->registered != NULL)
		klass->registered(self, device);
}

/**
 * fu_backend_invalidate:
 * @self: a #FuBackend
 *
 * Normally when calling [method@FuBackend.setup] multiple times it is only actually done once.
 * Calling this method causes the next requests to [method@FuBackend.setup] to actually probe the
 * hardware.
 *
 * Only subclassed backends setting `can-invalidate=TRUE` at construction time can use this
 * method, as it is not always safe to call for backends shared between multiple plugins.
 *
 * This should be done in case the backing information source has changed, for instance if
 * a platform subsystem has been updated.
 *
 * This also optionally calls the ->invalidate() vfunc for the backend. This allows the backend
 * to perform shared backend actions on superclassed devices.
 *
 * Since: 1.8.0
 **/
void
fu_backend_invalidate(FuBackend *self)
{
	FuBackendClass *klass = FU_BACKEND_GET_CLASS(self);
	FuBackendPrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FU_IS_BACKEND(self));
	g_return_if_fail(priv->can_invalidate);

	priv->done_setup = FALSE;
	if (klass->invalidate != NULL)
		klass->invalidate(self);
}

/**
 * fu_backend_add_string:
 * @self: a #FuBackend
 * @idt: indent level
 * @str: a string to append to
 *
 * Add backend-specific device metadata to an existing string.
 *
 * Since: 1.8.4
 **/
void
fu_backend_add_string(FuBackend *self, guint idt, GString *str)
{
	FuBackendClass *klass = FU_BACKEND_GET_CLASS(self);
	FuBackendPrivate *priv = GET_PRIVATE(self);

	fwupd_codec_string_append(str, idt, G_OBJECT_TYPE_NAME(self), "");
	fwupd_codec_string_append(str, idt + 1, "Name", priv->name);
	fwupd_codec_string_append_bool(str, idt + 1, "Enabled", priv->enabled);
	fwupd_codec_string_append_bool(str, idt + 1, "DoneSetup", priv->done_setup);
	fwupd_codec_string_append_bool(str, idt + 1, "CanInvalidate", priv->can_invalidate);

	/* subclassed */
	if (klass->to_string != NULL)
		klass->to_string(self, idt, str);
}

/**
 * fu_backend_setup:
 * @self: a #FuBackend
 * @progress: a #FuProgress
 * @error: (nullable): optional return location for an error
 *
 * Sets up the backend ready for use, which typically calls the subclassed setup
 * function. No devices should be added or removed at this point.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.6.1
 **/
gboolean
fu_backend_setup(FuBackend *self, FuProgress *progress, GError **error)
{
	FuBackendClass *klass = FU_BACKEND_GET_CLASS(self);
	FuBackendPrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_BACKEND(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (priv->done_setup)
		return TRUE;
	if (klass->setup != NULL) {
		if (!klass->setup(self, progress, error)) {
			priv->enabled = FALSE;
			return FALSE;
		}
	}
	priv->done_setup = TRUE;
	return TRUE;
}

static gchar *
fu_backend_get_emulation_array_member_name(FuBackend *self)
{
	FuBackendPrivate *priv = GET_PRIVATE(self);

	if (priv->name == NULL)
		return NULL;
	return g_strdup_printf("%c%sDevices", g_ascii_toupper(priv->name[0]), priv->name + 1);
}

/**
 * fu_backend_load:
 * @self: a #FuBackend
 * @json_object: a #JsonObject
 * @tag: a string backend tag, or %NULL
 * @flags: %FuBackendLoadFlags, typically `FU_BACKEND_LOAD_FLAG_NONE`
 * @error: (nullable): optional return location for an error
 *
 * Loads the backend from a JSON object.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.5
 **/
gboolean
fu_backend_load(FuBackend *self,
		JsonObject *json_object,
		const gchar *tag,
		FuBackendLoadFlags flags,
		GError **error)
{
	FuBackendClass *klass = FU_BACKEND_GET_CLASS(self);
	FuBackendPrivate *priv = GET_PRIVATE(self);
	JsonArray *json_array;
	g_autofree gchar *list_name = NULL;
	g_autoptr(GPtrArray) devices_added =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	g_autoptr(GPtrArray) devices_remove =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	g_autoptr(GList) devices = NULL;

	g_return_val_if_fail(FU_IS_BACKEND(self), FALSE);
	g_return_val_if_fail(json_object != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* optional */
	if (klass->load != NULL)
		return klass->load(self, json_object, tag, flags, error);

	/* sanity check */
	list_name = fu_backend_get_emulation_array_member_name(self);
	if (!json_object_has_member(json_object, list_name))
		return TRUE;

	/* four steps:
	 *
	 * 1. store all the existing devices matching the tag in devices_remove
	 * 2. read the devices in the array:
	 *    - if the platform-id exists: replace the event data & remove from devices_remove
	 *    - otherwise add to devices_added
	 * 3. emit devices in devices_remove
	 * 4. emit devices in devices_added
	 */
	devices = g_hash_table_get_values(priv->devices);
	for (GList *l = devices; l != NULL; l = l->next) {
		FuDevice *device = FU_DEVICE(l->data);
		g_ptr_array_add(devices_remove, g_object_ref(device));
	}
	json_array = json_object_get_array_member(json_object, list_name);
	for (guint i = 0; i < json_array_get_length(json_array); i++) {
		JsonNode *node_tmp = json_array_get_element(json_array, i);
		FuDevice *device_old;
		g_autoptr(FuDevice) device_tmp =
		    g_object_new(priv->device_gtype, "context", priv->ctx, NULL);

		if (!fwupd_codec_from_json(FWUPD_CODEC(device_tmp), node_tmp, error))
			return FALSE;

		/* does a device with this platform ID [and the same created date] already exist */
		device_old = fu_backend_lookup_by_id(self, fu_device_get_backend_id(device_tmp));
		if (device_old != NULL && fu_device_get_created_usec(device_old) ==
					      fu_device_get_created_usec(device_tmp)) {
			GPtrArray *events = fu_device_get_events(device_tmp);
			fu_device_clear_events(device_old);
			for (guint j = 0; j < events->len; j++) {
				FuDeviceEvent *event = g_ptr_array_index(events, j);
				fu_device_add_event(device_old, event);
			}
			fu_backend_device_changed(self, device_old);
			g_ptr_array_remove(devices_remove, device_old);
			continue;
		}

		/* new to us! */
		g_ptr_array_add(devices_added, g_object_ref(device_tmp));
	}

	/* emit removes then adds */
	for (guint i = 0; i < devices_remove->len; i++) {
		FuDevice *device = g_ptr_array_index(devices_remove, i);
		fu_backend_device_removed(self, device);
	}
	for (guint i = 0; i < devices_added->len; i++) {
		FuDevice *device = g_ptr_array_index(devices_added, i);
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_EMULATED);
		fu_backend_device_added(self, device);
	}

	/* success */
	return TRUE;
}

/**
 * fu_backend_save:
 * @self: a #FuBackend
 * @json_builder: a #JsonBuilder
 * @tag: a string backend tag, or %NULL
 * @flags: %FuBackendSaveFlags, typically `FU_BACKEND_SAVE_FLAG_NONE`
 * @error: (nullable): optional return location for an error
 *
 * Saves the backend to a JSON builder.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.5
 **/
gboolean
fu_backend_save(FuBackend *self,
		JsonBuilder *json_builder,
		const gchar *tag,
		FuBackendSaveFlags flags,
		GError **error)
{
	FuBackendClass *klass = FU_BACKEND_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_BACKEND(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* optional */
	if (klass->save != NULL)
		return klass->save(self, json_builder, tag, flags, error);

	/* internal */
	json_builder_begin_object(json_builder);
	fwupd_codec_to_json(FWUPD_CODEC(self), json_builder, FWUPD_CODEC_FLAG_NONE);
	json_builder_end_object(json_builder);
	return TRUE;
}

static void
fu_backend_add_json(FwupdCodec *codec, JsonBuilder *builder, FwupdCodecFlags flags)
{
	FuBackend *self = FU_BACKEND(codec);
	FuBackendPrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *list_name = NULL;
	g_autoptr(GList) devices = NULL;

	list_name = fu_backend_get_emulation_array_member_name(self);
	json_builder_set_member_name(builder, list_name);
	json_builder_begin_array(builder);
	devices = g_hash_table_get_values(priv->devices);
	for (GList *l = devices; l != NULL; l = l->next) {
		FuDevice *device = FU_DEVICE(l->data);
		if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATION_TAG))
			continue;
		json_builder_begin_object(builder);
		fwupd_codec_to_json(FWUPD_CODEC(device), builder, FWUPD_CODEC_FLAG_NONE);
		json_builder_end_object(builder);
	}
	json_builder_end_array(builder);
}

/**
 * fu_backend_coldplug:
 * @self: a #FuBackend
 * @progress: a #FuProgress
 * @error: (nullable): optional return location for an error
 *
 * Adds devices using the subclassed backend. If [method@FuBackend.setup] has not
 * already been called then it is run before this function automatically.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.6.1
 **/
gboolean
fu_backend_coldplug(FuBackend *self, FuProgress *progress, GError **error)
{
	FuBackendClass *klass = FU_BACKEND_GET_CLASS(self);
	g_return_val_if_fail(FU_IS_BACKEND(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	if (!fu_backend_setup(self, progress, error))
		return FALSE;
	if (klass->coldplug == NULL)
		return TRUE;
	return klass->coldplug(self, progress, error);
}

/**
 * fu_backend_get_name:
 * @self: a #FuBackend
 *
 * Return the name of the backend, which is normally set by the subclass.
 *
 * Returns: backend name
 *
 * Since: 1.6.1
 **/
const gchar *
fu_backend_get_name(FuBackend *self)
{
	FuBackendPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_BACKEND(self), NULL);
	return priv->name;
}

/**
 * fu_backend_get_context:
 * @self: a #FuBackend
 *
 * Gets the context for a backend.
 *
 * Returns: (transfer none): a #FuContext or %NULL if not set
 *
 * Since: 1.6.1
 **/
FuContext *
fu_backend_get_context(FuBackend *self)
{
	FuBackendPrivate *priv = GET_PRIVATE(self);
	return priv->ctx;
}

/**
 * fu_backend_get_enabled:
 * @self: a #FuBackend
 *
 * Return the boolean value of a key if it's been configured
 *
 * Returns: %TRUE if the backend is enabled
 *
 * Since: 1.6.1
 **/
gboolean
fu_backend_get_enabled(FuBackend *self)
{
	FuBackendPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_BACKEND(self), FALSE);
	return priv->enabled;
}

/**
 * fu_backend_set_enabled:
 * @self: a #FuBackend
 * @enabled: enabled state
 *
 * Sets the backend enabled state.
 *
 * Since: 1.6.1
 **/
void
fu_backend_set_enabled(FuBackend *self, gboolean enabled)
{
	FuBackendPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_BACKEND(self));
	priv->enabled = FALSE;
}

/**
 * fu_backend_lookup_by_id:
 * @self: a #FuBackend
 * @backend_id: a device backend ID
 *
 * Gets a device previously added by the backend.
 *
 * Returns: (transfer none): device, or %NULL if not found
 *
 * Since: 1.6.1
 **/
FuDevice *
fu_backend_lookup_by_id(FuBackend *self, const gchar *backend_id)
{
	FuBackendPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_BACKEND(self), NULL);
	g_return_val_if_fail(backend_id != NULL, NULL);
	return g_hash_table_lookup(priv->devices, backend_id);
}

static gint
fu_backend_get_devices_sort_cb(gconstpointer a, gconstpointer b)
{
	FuDevice *deva = *((FuDevice **)a);
	FuDevice *devb = *((FuDevice **)b);
	return g_strcmp0(fu_device_get_backend_id(deva), fu_device_get_backend_id(devb));
}

/**
 * fu_backend_get_devices:
 * @self: a #FuBackend
 *
 * Gets all the devices added by the backend.
 *
 * Returns: (transfer container) (element-type FuDevice): devices
 *
 * Since: 1.6.1
 **/
GPtrArray *
fu_backend_get_devices(FuBackend *self)
{
	FuBackendPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GList) values = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	g_return_val_if_fail(FU_IS_BACKEND(self), NULL);

	devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	values = g_hash_table_get_values(priv->devices);
	for (GList *l = values; l != NULL; l = l->next)
		g_ptr_array_add(devices, g_object_ref(l->data));
	g_ptr_array_sort(devices, fu_backend_get_devices_sort_cb);
	return g_steal_pointer(&devices);
}

static void
fu_backend_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuBackend *self = FU_BACKEND(object);
	FuBackendPrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_NAME:
		g_value_set_string(value, priv->name);
		break;
	case PROP_CAN_INVALIDATE:
		g_value_set_boolean(value, priv->can_invalidate);
		break;
	case PROP_CONTEXT:
		g_value_set_object(value, priv->ctx);
		break;
	case PROP_DEVICE_GTYPE:
		g_value_set_gtype(value, priv->device_gtype);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_backend_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuBackend *self = FU_BACKEND(object);
	FuBackendPrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_NAME:
		priv->name = g_value_dup_string(value);
		break;
	case PROP_CAN_INVALIDATE:
		priv->can_invalidate = g_value_get_boolean(value);
		break;
	case PROP_CONTEXT:
		g_set_object(&priv->ctx, g_value_get_object(value));
		break;
	case PROP_DEVICE_GTYPE:
		priv->device_gtype = g_value_get_gtype(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_backend_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_json = fu_backend_add_json;
}

static void
fu_backend_init(FuBackend *self)
{
	FuBackendPrivate *priv = GET_PRIVATE(self);
	priv->enabled = TRUE;
	priv->device_gtype = FU_TYPE_DEVICE;
	priv->thread_init = g_thread_self();
	priv->devices =
	    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_object_unref);
}

static void
fu_backend_dispose(GObject *object)
{
	FuBackend *self = FU_BACKEND(object);
	FuBackendPrivate *priv = GET_PRIVATE(self);
	g_hash_table_remove_all(priv->devices);
	G_OBJECT_CLASS(fu_backend_parent_class)->dispose(object);
}

static void
fu_backend_finalize(GObject *object)
{
	FuBackend *self = FU_BACKEND(object);
	FuBackendPrivate *priv = GET_PRIVATE(self);
	if (priv->ctx != NULL)
		g_object_unref(priv->ctx);
	g_free(priv->name);
	g_hash_table_unref(priv->devices);
	G_OBJECT_CLASS(fu_backend_parent_class)->finalize(object);
}

static void
fu_backend_class_init(FuBackendClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->get_property = fu_backend_get_property;
	object_class->set_property = fu_backend_set_property;
	object_class->finalize = fu_backend_finalize;
	object_class->dispose = fu_backend_dispose;

	/**
	 * FuBackend:name:
	 *
	 * The backend name.
	 *
	 * Since: 1.6.1
	 */
	pspec =
	    g_param_spec_string("name",
				NULL,
				NULL,
				NULL,
				G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_NAME, pspec);

	/**
	 * FuBackend:can-invalidate:
	 *
	 * If the backend can be invalidated.
	 *
	 * Since: 1.8.0
	 */
	pspec =
	    g_param_spec_boolean("can-invalidate",
				 NULL,
				 NULL,
				 FALSE,
				 G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_CAN_INVALIDATE, pspec);

	/**
	 * FuBackend:context:
	 *
	 * The #FuContent to use.
	 *
	 * Since: 1.6.1
	 */
	pspec =
	    g_param_spec_object("context",
				NULL,
				NULL,
				FU_TYPE_CONTEXT,
				G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_CONTEXT, pspec);

	/**
	 * FuBackend:device-gtype:
	 *
	 * The #GType to use when creating emulated devices.
	 *
	 * Since: 2.0.0
	 */
	pspec =
	    g_param_spec_gtype("device-gtype",
			       NULL,
			       NULL,
			       FU_TYPE_DEVICE,
			       G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_DEVICE_GTYPE, pspec);

	/**
	 * FuBackend::device-added:
	 * @self: the #FuBackend instance that emitted the signal
	 * @device: the #FuDevice
	 *
	 * The ::device-added signal is emitted when a device has been added.
	 *
	 * Since: 1.6.1
	 **/
	signals[SIGNAL_ADDED] = g_signal_new("device-added",
					     G_TYPE_FROM_CLASS(object_class),
					     G_SIGNAL_RUN_LAST,
					     0,
					     NULL,
					     NULL,
					     g_cclosure_marshal_VOID__OBJECT,
					     G_TYPE_NONE,
					     1,
					     FU_TYPE_DEVICE);
	/**
	 * FuBackend::device-removed:
	 * @self: the #FuBackend instance that emitted the signal
	 * @device: the #FuDevice
	 *
	 * The ::device-removed signal is emitted when a device has been removed.
	 *
	 * Since: 1.6.1
	 **/
	signals[SIGNAL_REMOVED] = g_signal_new("device-removed",
					       G_TYPE_FROM_CLASS(object_class),
					       G_SIGNAL_RUN_LAST,
					       0,
					       NULL,
					       NULL,
					       g_cclosure_marshal_VOID__OBJECT,
					       G_TYPE_NONE,
					       1,
					       FU_TYPE_DEVICE);
	/**
	 * FuBackend::device-changed:
	 * @self: the #FuBackend instance that emitted the signal
	 * @device: the #FuDevice
	 *
	 * The ::device-changed signal is emitted when a device has been changed.
	 *
	 * Since: 1.6.1
	 **/
	signals[SIGNAL_CHANGED] = g_signal_new("device-changed",
					       G_TYPE_FROM_CLASS(object_class),
					       G_SIGNAL_RUN_LAST,
					       0,
					       NULL,
					       NULL,
					       g_cclosure_marshal_VOID__OBJECT,
					       G_TYPE_NONE,
					       1,
					       FU_TYPE_DEVICE);
}
