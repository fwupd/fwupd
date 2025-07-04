/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-codec.h"
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
	FwupdRequestFlags flags;
	guint64 created;
	gchar *device_id;
	gchar *message;
	gchar *image;
} FwupdRequestPrivate;

enum { SIGNAL_INVALIDATE, SIGNAL_LAST };

enum {
	PROP_0,
	PROP_ID,
	PROP_KIND,
	PROP_FLAGS,
	PROP_MESSAGE,
	PROP_IMAGE,
	PROP_DEVICE_ID,
	PROP_LAST
};

static guint signals[SIGNAL_LAST] = {0};

static void
fwupd_request_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FwupdRequest,
		       fwupd_request,
		       G_TYPE_OBJECT,
		       0,
		       G_ADD_PRIVATE(FwupdRequest)
			   G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC, fwupd_request_codec_iface_init));

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
 * fwupd_request_flag_to_string:
 * @flag: a request flag, e.g. %FWUPD_REQUEST_FLAG_NONE
 *
 * Converts an enumerated request flag to a string.
 *
 * Returns: identifier string
 *
 * Since: 1.8.6
 **/
const gchar *
fwupd_request_flag_to_string(FwupdRequestFlags flag)
{
	if (flag == FWUPD_REQUEST_FLAG_NONE)
		return "none";
	if (flag == FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE)
		return "allow-generic-message";
	if (flag == FWUPD_REQUEST_FLAG_ALLOW_GENERIC_IMAGE)
		return "allow-generic-image";
	if (flag == FWUPD_REQUEST_FLAG_NON_GENERIC_MESSAGE)
		return "non-generic-message";
	if (flag == FWUPD_REQUEST_FLAG_NON_GENERIC_IMAGE)
		return "non-generic-image";
	return NULL;
}

/**
 * fwupd_request_flag_from_string:
 * @flag: (nullable): a string, e.g. `none`
 *
 * Converts a string to an enumerated request flag.
 *
 * Returns: enumerated value
 *
 * Since: 1.8.6
 **/
FwupdRequestFlags
fwupd_request_flag_from_string(const gchar *flag)
{
	if (g_strcmp0(flag, "allow-generic-message") == 0)
		return FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE;
	if (g_strcmp0(flag, "allow-generic-image") == 0)
		return FWUPD_REQUEST_FLAG_ALLOW_GENERIC_IMAGE;
	if (g_strcmp0(flag, "non-generic-message") == 0)
		return FWUPD_REQUEST_FLAG_NON_GENERIC_MESSAGE;
	if (g_strcmp0(flag, "non-generic-image") == 0)
		return FWUPD_REQUEST_FLAG_NON_GENERIC_IMAGE;
	return FWUPD_REQUEST_FLAG_NONE;
}

/**
 * fwupd_request_emit_invalidate:
 * @self: a #FwupdRequest
 *
 * Emits an `invalidate` signal to signify that the request is no longer valid, and any visible
 * UI components should be hidden.
 *
 * Since: 1.9.17
 **/
void
fwupd_request_emit_invalidate(FwupdRequest *self)
{
	g_return_if_fail(FWUPD_IS_REQUEST(self));
	g_debug("emitting FwupdRequest::invalidate()");
	g_signal_emit(self, signals[SIGNAL_INVALIDATE], 0);
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

static void
fwupd_request_add_variant(FwupdCodec *codec, GVariantBuilder *builder, FwupdCodecFlags flags)
{
	FwupdRequest *self = FWUPD_REQUEST(codec);
	FwupdRequestPrivate *priv = GET_PRIVATE(self);

	if (priv->id != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_APPSTREAM_ID,
				      g_variant_new_string(priv->id));
	}
	if (priv->created > 0) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_CREATED,
				      g_variant_new_uint64(priv->created));
	}
	if (priv->device_id != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_DEVICE_ID,
				      g_variant_new_string(priv->device_id));
	}
	if (priv->message != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_UPDATE_MESSAGE,
				      g_variant_new_string(priv->message));
	}
	if (priv->image != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_UPDATE_IMAGE,
				      g_variant_new_string(priv->image));
	}
	if (priv->kind != FWUPD_REQUEST_KIND_UNKNOWN) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_REQUEST_KIND,
				      g_variant_new_uint32(priv->kind));
	}
	if (priv->flags != FWUPD_REQUEST_FLAG_NONE) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_FLAGS,
				      g_variant_new_uint64(priv->flags));
	}
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
	if (g_strcmp0(key, FWUPD_RESULT_KEY_FLAGS) == 0) {
		fwupd_request_set_flags(self, g_variant_get_uint64(value));
		return;
	}
}

/**
 * fwupd_request_get_message:
 * @self: a #FwupdRequest
 *
 * Gets the update message, generating a generic one using the request ID if possible.
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

	/* something custom */
	if (priv->message != NULL)
		return priv->message;

	/* untranslated canned messages */
	if (fwupd_request_has_flag(self, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE)) {
		if (g_strcmp0(priv->id, FWUPD_REQUEST_ID_REMOVE_REPLUG) == 0)
			return "Please unplug and then re-insert the device USB cable.";
		if (g_strcmp0(priv->id, FWUPD_REQUEST_ID_INSERT_USB_CABLE) == 0)
			return "Please re-insert the device USB cable.";
		if (g_strcmp0(priv->id, FWUPD_REQUEST_ID_REMOVE_USB_CABLE) == 0)
			return "Please unplug the device USB cable.";
		if (g_strcmp0(priv->id, FWUPD_REQUEST_ID_REPLUG_POWER) == 0)
			return "Please unplug and then re-insert the device power cable.";
		if (g_strcmp0(priv->id, FWUPD_REQUEST_ID_PRESS_UNLOCK) == 0)
			return "Press unlock on the device.";
		if (g_strcmp0(priv->id, FWUPD_REQUEST_ID_DO_NOT_POWER_OFF) == 0)
			return "Do not turn off your computer or remove the AC adaptor.";
		if (g_strcmp0(priv->id, FWUPD_REQUEST_ID_RESTART_DAEMON) == 0)
			return "Please restart the fwupd service.";
	}

	/* unknown */
	return NULL;
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
 * fwupd_request_get_flags:
 * @self: a #FwupdRequest
 *
 * Gets the request flags.
 *
 * Returns: request flags, or 0 if unset
 *
 * Since: 1.8.6
 **/
FwupdRequestFlags
fwupd_request_get_flags(FwupdRequest *self)
{
	FwupdRequestPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REQUEST(self), 0);
	return priv->flags;
}

/**
 * fwupd_request_set_flags:
 * @self: a #FwupdRequest
 * @flags: request flags, e.g. %FWUPD_REQUEST_FLAG_NONE
 *
 * Sets the request flags.
 *
 * Since: 1.8.6
 **/
void
fwupd_request_set_flags(FwupdRequest *self, FwupdRequestFlags flags)
{
	FwupdRequestPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REQUEST(self));

	/* not changed */
	if (priv->flags == flags)
		return;

	priv->flags = flags;
	g_object_notify(G_OBJECT(self), "flags");
}

/**
 * fwupd_request_add_flag:
 * @self: a #FwupdRequest
 * @flag: the #FwupdRequestFlags
 *
 * Adds a specific flag to the request.
 *
 * Since: 1.8.6
 **/
void
fwupd_request_add_flag(FwupdRequest *self, FwupdRequestFlags flag)
{
	FwupdRequestPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REQUEST(self));
	priv->flags |= flag;
}

/**
 * fwupd_request_remove_flag:
 * @self: a #FwupdRequest
 * @flag: the #FwupdRequestFlags
 *
 * Removes a specific flag from the request.
 *
 * Since: 1.8.6
 **/
void
fwupd_request_remove_flag(FwupdRequest *self, FwupdRequestFlags flag)
{
	FwupdRequestPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_REQUEST(self));
	priv->flags &= ~flag;
}

/**
 * fwupd_request_has_flag:
 * @self: a #FwupdRequest
 * @flag: the #FwupdRequestFlags
 *
 * Finds if the request has a specific flag.
 *
 * Returns: %TRUE if the flag is set
 *
 * Since: 1.8.6
 **/
gboolean
fwupd_request_has_flag(FwupdRequest *self, FwupdRequestFlags flag)
{
	FwupdRequestPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_REQUEST(self), FALSE);
	return (priv->flags & flag) > 0;
}

static void
fwupd_request_add_string(FwupdCodec *codec, guint idt, GString *str)
{
	FwupdRequest *self = FWUPD_REQUEST(codec);
	FwupdRequestPrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append(str, idt, FWUPD_RESULT_KEY_APPSTREAM_ID, priv->id);
	if (priv->kind != FWUPD_REQUEST_KIND_UNKNOWN) {
		fwupd_codec_string_append(str,
					  idt,
					  FWUPD_RESULT_KEY_REQUEST_KIND,
					  fwupd_request_kind_to_string(priv->kind));
	}
	fwupd_codec_string_append(str,
				  idt,
				  FWUPD_RESULT_KEY_FLAGS,
				  fwupd_request_flag_to_string(priv->flags));
	fwupd_codec_string_append(str, idt, FWUPD_RESULT_KEY_DEVICE_ID, priv->device_id);
	fwupd_codec_string_append_time(str, idt, FWUPD_RESULT_KEY_CREATED, priv->created);
	fwupd_codec_string_append(str, idt, FWUPD_RESULT_KEY_UPDATE_MESSAGE, priv->message);
	fwupd_codec_string_append(str, idt, FWUPD_RESULT_KEY_UPDATE_IMAGE, priv->image);
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
	case PROP_FLAGS:
		g_value_set_uint64(value, priv->flags);
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
	case PROP_FLAGS:
		fwupd_request_set_flags(self, g_value_get_uint64(value));
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
	 * FwupdRequest::invalidate:
	 * @self: the #FwupdRequest instance that emitted the signal
	 *
	 * The ::invalidate signal is emitted when the request is no longer valid, and any visible
	 * UI components should be hidden.
	 *
	 * Since: 1.9.17
	 **/
	signals[SIGNAL_INVALIDATE] = g_signal_new("invalidate",
						  G_TYPE_FROM_CLASS(object_class),
						  G_SIGNAL_RUN_LAST,
						  G_STRUCT_OFFSET(FwupdRequestClass, invalidate),
						  NULL,
						  NULL,
						  g_cclosure_marshal_VOID__VOID,
						  G_TYPE_NONE,
						  0);

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
	 * FwupdRequest:flags:
	 *
	 * The flags for the request.
	 *
	 * Since: 1.8.6
	 */
	pspec = g_param_spec_uint64("flags",
				    NULL,
				    NULL,
				    FWUPD_REQUEST_FLAG_NONE,
				    FWUPD_REQUEST_FLAG_UNKNOWN,
				    FWUPD_REQUEST_FLAG_UNKNOWN,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_FLAGS, pspec);

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
fwupd_request_from_variant_iter(FwupdCodec *codec, GVariantIter *iter)
{
	FwupdRequest *self = FWUPD_REQUEST(codec);
	GVariant *value;
	const gchar *key;
	while (g_variant_iter_next(iter, "{&sv}", &key, &value)) {
		fwupd_request_from_key_value(self, key, value);
		g_variant_unref(value);
	}
}

static void
fwupd_request_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_string = fwupd_request_add_string;
	iface->add_variant = fwupd_request_add_variant;
	iface->from_variant_iter = fwupd_request_from_variant_iter;
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
