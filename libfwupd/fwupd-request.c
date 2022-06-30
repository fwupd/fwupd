/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fwupd-enums-private.h"
#include "fwupd-request-private.h"

/**
 * FwupdRequest:
 *
 * A user request from the device.
 *
 * See also: [class@FwupdDevice]
 */

typedef struct {
	gchar *id;
	FwupdRequestKind kind;
	guint64 created;
	gchar *device_id;
	gchar *message;
	gchar *image;
} FwupdRequestPrivate;

enum { PROP_0, PROP_ID, PROP_KIND, PROP_MESSAGE, PROP_IMAGE, PROP_DEVICE_ID, PROP_LAST };

G_DEFINE_TYPE_WITH_PRIVATE(FwupdRequest, fwupd_request, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fwupd_request_get_instance_private(o))

/**
 * fwupd_request_kind_to_string:
 * @kind: a update message kind, e.g. %FWUPD_REQUEST_KIND_IMMEDIATE
 *
 * Converts an enumerated update message kind to a string.
 *
 * Returns: identifier string
 *
 * Since: 1.6.2
 **/
const gchar *
fwupd_request_kind_to_string(FwupdRequestKind kind)
{
	if (kind == FWUPD_REQUEST_KIND_UNKNOWN)
		return "unknown";
	if (kind == FWUPD_REQUEST_KIND_POST)
		return "post";
	if (kind == FWUPD_REQUEST_KIND_IMMEDIATE)
		return "immediate";
	return NULL;
}

/**
 * fwupd_request_kind_from_string:
 * @kind: (nullable): a string, e.g. `immediate`
 *
 * Converts a string to an enumerated update message kind.
 *
 * Returns: enumerated value
 *
 * Since: 1.6.2
 **/
FwupdRequestKind
fwupd_request_kind_from_string(const gchar *kind)
{
	if (g_strcmp0(kind, "unknown") == 0)
		return FWUPD_REQUEST_KIND_UNKNOWN;
	if (g_strcmp0(kind, "post") == 0)
		return FWUPD_REQUEST_KIND_POST;
	if (g_strcmp0(kind, "immediate") == 0)
		return FWUPD_REQUEST_KIND_IMMEDIATE;
	return FWUPD_REQUEST_KIND_LAST;
}

/**
 * fwupd_request_get_id:
 * @self: a #FwupdRequest
 *
 * Gets the ID.
 *
 * Returns: the ID, or %NULL if unset
 *
 * Since: 1.6.2
 **/
const gchar *
fwupd_request_get_id(FwupdRequest *self)
{
	FwupdRequestPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REQUEST(self), NULL);
	return priv->id;
}

/**
 * fwupd_request_set_id:
 * @self: a #FwupdRequest
 * @id: (nullable): the request ID, e.g. `USB:foo`
 *
 * Sets the ID.
 *
 * Since: 1.6.2
 **/
void
fwupd_request_set_id(FwupdRequest *self, const gchar *id)
{
	FwupdRequestPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REQUEST(self));

	/* not changed */
	if (g_strcmp0(priv->id, id) == 0)
		return;

	g_free(priv->id);
	priv->id = g_strdup(id);
}

/**
 * fwupd_request_get_device_id:
 * @self: a #FwupdRequest
 *
 * Gets the device_id that created the request.
 *
 * Returns: the device_id, or %NULL if unset
 *
 * Since: 1.6.2
 **/
const gchar *
fwupd_request_get_device_id(FwupdRequest *self)
{
	FwupdRequestPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REQUEST(self), NULL);
	return priv->device_id;
}

/**
 * fwupd_request_set_device_id:
 * @self: a #FwupdRequest
 * @device_id: (nullable): the device_id, e.g. `colorhug`
 *
 * Sets the device_id that created the request.
 *
 * Since: 1.6.2
 **/
void
fwupd_request_set_device_id(FwupdRequest *self, const gchar *device_id)
{
	FwupdRequestPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REQUEST(self));

	/* not changed */
	if (g_strcmp0(priv->device_id, device_id) == 0)
		return;

	g_free(priv->device_id);
	priv->device_id = g_strdup(device_id);
}

/**
 * fwupd_request_get_created:
 * @self: a #FwupdRequest
 *
 * Gets when the request was created.
 *
 * Returns: the UNIX time, or 0 if unset
 *
 * Since: 1.6.2
 **/
guint64
fwupd_request_get_created(FwupdRequest *self)
{
	FwupdRequestPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REQUEST(self), 0);
	return priv->created;
}

/**
 * fwupd_request_set_created:
 * @self: a #FwupdRequest
 * @created: the UNIX time
 *
 * Sets when the request was created.
 *
 * Since: 1.6.2
 **/
void
fwupd_request_set_created(FwupdRequest *self, guint64 created)
{
	FwupdRequestPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REQUEST(self));
	priv->created = created;
}

/**
 * fwupd_request_to_variant:
 * @self: a #FwupdRequest
 *
 * Serialize the request data.
 *
 * Returns: the serialized data, or %NULL for error
 *
 * Since: 1.6.2
 **/
GVariant *
fwupd_request_to_variant(FwupdRequest *self)
{
	FwupdRequestPrivate *priv = GET_PRIVATE(self);
	GVariantBuilder builder;

	g_return_val_if_fail(FWUPD_IS_REQUEST(self), NULL);

	/* create an array with all the metadata in */
	g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
	if (priv->id != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_APPSTREAM_ID,
				      g_variant_new_string(priv->id));
	}
	if (priv->created > 0) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_CREATED,
				      g_variant_new_uint64(priv->created));
	}
	if (priv->device_id != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_DEVICE_ID,
				      g_variant_new_string(priv->device_id));
	}
	if (priv->message != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_UPDATE_MESSAGE,
				      g_variant_new_string(priv->message));
	}
	if (priv->image != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_UPDATE_IMAGE,
				      g_variant_new_string(priv->image));
	}
	if (priv->kind != FWUPD_REQUEST_KIND_UNKNOWN) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_REQUEST_KIND,
				      g_variant_new_uint32(priv->kind));
	}
	return g_variant_new("a{sv}", &builder);
}

static void
fwupd_request_from_key_value(FwupdRequest *self, const gchar *key, GVariant *value)
{
	if (g_strcmp0(key, FWUPD_RESULT_KEY_APPSTREAM_ID) == 0) {
		fwupd_request_set_id(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_CREATED) == 0) {
		fwupd_request_set_created(self, g_variant_get_uint64(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_DEVICE_ID) == 0) {
		fwupd_request_set_device_id(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_UPDATE_MESSAGE) == 0) {
		fwupd_request_set_message(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_UPDATE_IMAGE) == 0) {
		fwupd_request_set_image(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_REQUEST_KIND) == 0) {
		fwupd_request_set_kind(self, g_variant_get_uint32(value));
		return;
	}
}

static void
fwupd_pad_kv_str(GString *str, const gchar *key, const gchar *value)
{
	/* ignore */
	if (key == NULL || value == NULL)
		return;
	g_string_append_printf(str, "  %s: ", key);
	for (gsize i = strlen(key); i < 20; i++)
		g_string_append(str, " ");
	g_string_append_printf(str, "%s\n", value);
}

static void
fwupd_pad_kv_unx(GString *str, const gchar *key, guint64 value)
{
	g_autoptr(GDateTime) date = NULL;
	g_autofree gchar *tmp = NULL;

	/* ignore */
	if (value == 0)
		return;

	date = g_date_time_new_from_unix_utc((gint64)value);
	tmp = g_date_time_format(date, "%F");
	fwupd_pad_kv_str(str, key, tmp);
}

/**
 * fwupd_request_get_message:
 * @self: a #FwupdRequest
 *
 * Gets the update message.
 *
 * Returns: the update message, or %NULL if unset
 *
 * Since: 1.6.2
 **/
const gchar *
fwupd_request_get_message(FwupdRequest *self)
{
	FwupdRequestPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REQUEST(self), NULL);
	return priv->message;
}

/**
 * fwupd_request_set_message:
 * @self: a #FwupdRequest
 * @message: (nullable): the update message string
 *
 * Sets the update message.
 *
 * Since: 1.6.2
 **/
void
fwupd_request_set_message(FwupdRequest *self, const gchar *message)
{
	FwupdRequestPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REQUEST(self));

	/* not changed */
	if (g_strcmp0(priv->message, message) == 0)
		return;

	g_free(priv->message);
	priv->message = g_strdup(message);
	g_object_notify(G_OBJECT(self), "message");
}

/**
 * fwupd_request_get_image:
 * @self: a #FwupdRequest
 *
 * Gets the update image.
 *
 * Returns: the update image URL, or %NULL if unset
 *
 * Since: 1.6.2
 **/
const gchar *
fwupd_request_get_image(FwupdRequest *self)
{
	FwupdRequestPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REQUEST(self), NULL);
	return priv->image;
}

/**
 * fwupd_request_set_image:
 * @self: a #FwupdRequest
 * @image: (nullable): the update image URL
 *
 * Sets the update image.
 *
 * Since: 1.6.2
 **/
void
fwupd_request_set_image(FwupdRequest *self, const gchar *image)
{
	FwupdRequestPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REQUEST(self));

	/* not changed */
	if (g_strcmp0(priv->image, image) == 0)
		return;

	g_free(priv->image);
	priv->image = g_strdup(image);
	g_object_notify(G_OBJECT(self), "image");
}

/**
 * fwupd_request_get_kind:
 * @self: a #FwupdRequest
 *
 * Returns what the request is currently doing.
 *
 * Returns: the kind value, e.g. %FWUPD_STATUS_REQUEST_WRITE
 *
 * Since: 1.6.2
 **/
FwupdRequestKind
fwupd_request_get_kind(FwupdRequest *self)
{
	FwupdRequestPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REQUEST(self), 0);
	return priv->kind;
}

/**
 * fwupd_request_set_kind:
 * @self: a #FwupdRequest
 * @kind: the kind value, e.g. %FWUPD_STATUS_REQUEST_WRITE
 *
 * Sets what the request is currently doing.
 *
 * Since: 1.6.2
 **/
void
fwupd_request_set_kind(FwupdRequest *self, FwupdRequestKind kind)
{
	FwupdRequestPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REQUEST(self));
	if (priv->kind == kind)
		return;
	priv->kind = kind;
	g_object_notify(G_OBJECT(self), "kind");
}

/**
 * fwupd_request_to_string:
 * @self: a #FwupdRequest
 *
 * Builds a text representation of the object.
 *
 * Returns: text, or %NULL for invalid
 *
 * Since: 1.6.2
 **/
gchar *
fwupd_request_to_string(FwupdRequest *self)
{
	FwupdRequestPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GString) str = g_string_new(NULL);

	g_return_val_if_fail(FWUPD_IS_REQUEST(self), NULL);

	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_APPSTREAM_ID, priv->id);
	if (priv->kind != FWUPD_REQUEST_KIND_UNKNOWN) {
		fwupd_pad_kv_str(str,
				 FWUPD_RESULT_KEY_REQUEST_KIND,
				 fwupd_request_kind_to_string(priv->kind));
	}
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_DEVICE_ID, priv->device_id);
	fwupd_pad_kv_unx(str, FWUPD_RESULT_KEY_CREATED, priv->created);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_UPDATE_MESSAGE, priv->message);
	fwupd_pad_kv_str(str, FWUPD_RESULT_KEY_UPDATE_IMAGE, priv->image);
	return g_string_free(g_steal_pointer(&str), FALSE);
}

static void
fwupd_request_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FwupdRequest *self = FWUPD_REQUEST(object);
	FwupdRequestPrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_ID:
		g_value_set_string(value, priv->id);
		break;
	case PROP_MESSAGE:
		g_value_set_string(value, priv->message);
		break;
	case PROP_IMAGE:
		g_value_set_string(value, priv->image);
		break;
	case PROP_DEVICE_ID:
		g_value_set_string(value, priv->device_id);
		break;
	case PROP_KIND:
		g_value_set_uint(value, priv->kind);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fwupd_request_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FwupdRequest *self = FWUPD_REQUEST(object);
	switch (prop_id) {
	case PROP_ID:
		fwupd_request_set_id(self, g_value_get_string(value));
		break;
	case PROP_MESSAGE:
		fwupd_request_set_message(self, g_value_get_string(value));
		break;
	case PROP_IMAGE:
		fwupd_request_set_image(self, g_value_get_string(value));
		break;
	case PROP_DEVICE_ID:
		fwupd_request_set_device_id(self, g_value_get_string(value));
		break;
	case PROP_KIND:
		fwupd_request_set_kind(self, g_value_get_uint(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fwupd_request_finalize(GObject *object)
{
	FwupdRequest *self = FWUPD_REQUEST(object);
	FwupdRequestPrivate *priv = GET_PRIVATE(self);

	g_free(priv->id);
	g_free(priv->device_id);
	g_free(priv->message);
	g_free(priv->image);

	G_OBJECT_CLASS(fwupd_request_parent_class)->finalize(object);
}

static void
fwupd_request_init(FwupdRequest *self)
{
	FwupdRequestPrivate *priv = GET_PRIVATE(self);
	priv->created = g_get_real_time() / G_USEC_PER_SEC;
}

static void
fwupd_request_class_init(FwupdRequestClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->finalize = fwupd_request_finalize;
	object_class->get_property = fwupd_request_get_property;
	object_class->set_property = fwupd_request_set_property;

	/**
	 * FwupdRequest:id:
	 *
	 * The request identifier.
	 *
	 * Since: 1.6.2
	 */
	pspec =
	    g_param_spec_string("id", NULL, NULL, NULL, G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_ID, pspec);

	/**
	 * FwupdRequest:kind:
	 *
	 * The kind of the request.
	 *
	 * Since: 1.6.2
	 */
	pspec = g_param_spec_uint("kind",
				  NULL,
				  NULL,
				  FWUPD_REQUEST_KIND_UNKNOWN,
				  FWUPD_REQUEST_KIND_LAST,
				  FWUPD_REQUEST_KIND_UNKNOWN,
				  G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_KIND, pspec);

	/**
	 * FwupdRequest:message:
	 *
	 * The message text in the request.
	 *
	 * Since: 1.6.2
	 */
	pspec = g_param_spec_string("message",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_MESSAGE, pspec);

	/**
	 * FwupdRequest:image:
	 *
	 * The image link for the request.
	 *
	 * Since: 1.6.2
	 */
	pspec =
	    g_param_spec_string("image", NULL, NULL, NULL, G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_IMAGE, pspec);

	/**
	 * FwupdRequest:device-id:
	 *
	 * The device ID for the request.
	 *
	 * Since: 1.8.2
	 */
	pspec = g_param_spec_string("device-id",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_DEVICE_ID, pspec);
}

static void
fwupd_request_set_from_variant_iter(FwupdRequest *self, GVariantIter *iter)
{
	GVariant *value;
	const gchar *key;
	while (g_variant_iter_next(iter, "{&sv}", &key, &value)) {
		fwupd_request_from_key_value(self, key, value);
		g_variant_unref(value);
	}
}

/**
 * fwupd_request_from_variant:
 * @value: (not nullable): the serialized data
 *
 * Creates a new request using serialized data.
 *
 * Returns: (transfer full): a new #FwupdRequest, or %NULL if @value was invalid
 *
 * Since: 1.6.2
 **/
FwupdRequest *
fwupd_request_from_variant(GVariant *value)
{
	FwupdRequest *self = NULL;
	const gchar *type_string;
	g_autoptr(GVariantIter) iter = NULL;

	g_return_val_if_fail(value != NULL, NULL);

	/* format from GetDetails */
	type_string = g_variant_get_type_string(value);
	if (g_strcmp0(type_string, "(a{sv})") == 0) {
		self = fwupd_request_new();
		g_variant_get(value, "(a{sv})", &iter);
		fwupd_request_set_from_variant_iter(self, iter);
	} else if (g_strcmp0(type_string, "a{sv}") == 0) {
		self = fwupd_request_new();
		g_variant_get(value, "a{sv}", &iter);
		fwupd_request_set_from_variant_iter(self, iter);
	} else {
		g_warning("type %s not known", type_string);
	}
	return self;
}

/**
 * fwupd_request_new:
 *
 * Creates a new request.
 *
 * Returns: a new #FwupdRequest
 *
 * Since: 1.6.2
 **/
FwupdRequest *
fwupd_request_new(void)
{
	FwupdRequest *self;
	self = g_object_new(FWUPD_TYPE_REQUEST, NULL);
	return FWUPD_REQUEST(self);
}
