/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuDeviceEvent"

#include "config.h"

#include "fu-device-event-private.h"
#include "fu-mem.h"

/**
 * FuDeviceEvent:
 *
 * A device event, used to enumulate hardware.
 *
 * See also: [class@FuDevice]
 */

typedef struct {
	GType gtype;
	gpointer data;
	GDestroyNotify destroy;
} FuDeviceEventBlob;

typedef struct {
	gchar *id;
	GHashTable *values; /* (utf-8) (FuDeviceEventBlob) */
} FuDeviceEventPrivate;

static void
fu_device_event_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FuDeviceEvent,
		       fu_device_event,
		       G_TYPE_OBJECT,
		       0,
		       G_ADD_PRIVATE(FuDeviceEvent)
			   G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC,
						 fu_device_event_codec_iface_init));

#define GET_PRIVATE(o) (fu_device_event_get_instance_private(o))

static void
fu_device_event_blob_free(FuDeviceEventBlob *blob)
{
	if (blob->destroy)
		blob->destroy(blob->data);
	g_free(blob);
}

static FuDeviceEventBlob *
fu_device_event_blob_create(GType gtype, gpointer data, GDestroyNotify destroy)
{
	FuDeviceEventBlob *blob = g_new0(FuDeviceEventBlob, 1);
	blob->gtype = gtype;
	blob->data = data;
	blob->destroy = destroy;
	return blob;
}

/**
 * fu_device_event_get_id:
 * @self: a #FuDeviceEvent
 *
 * Return the id of the #FuDeviceEvent, which is normally set when creating the object.
 *
 * Returns: (nullable): string
 *
 * Since: 2.0.0
 **/
const gchar *
fu_device_event_get_id(FuDeviceEvent *self)
{
	FuDeviceEventPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DEVICE_EVENT(self), NULL);
	return priv->id;
}

/**
 * fu_device_event_set_str:
 * @self: a #FuDeviceEvent
 * @key: (not nullable): a unique key, e.g. `Name`
 * @value: (nullable): a string
 *
 * Sets a string value on the event.
 *
 * Since: 2.0.0
 **/
void
fu_device_event_set_str(FuDeviceEvent *self, const gchar *key, const gchar *value)
{
	FuDeviceEventPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_DEVICE_EVENT(self));
	g_return_if_fail(key != NULL);
	g_hash_table_insert(priv->values,
			    g_strdup(key),
			    fu_device_event_blob_create(G_TYPE_STRING, g_strdup(value), g_free));
}

/**
 * fu_device_event_set_i64:
 * @self: a #FuDeviceEvent
 * @key: (not nullable): a unique key, e.g. `Name`
 * @value: (nullable): a string
 *
 * Sets an integer value on the string.
 *
 * Since: 2.0.0
 **/
void
fu_device_event_set_i64(FuDeviceEvent *self, const gchar *key, gint64 value)
{
	FuDeviceEventPrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FU_IS_DEVICE_EVENT(self));
	g_return_if_fail(key != NULL);

	g_hash_table_insert(
	    priv->values,
	    g_strdup(key),
	    fu_device_event_blob_create(G_TYPE_INT, g_memdup2(&value, sizeof(value)), g_free));
}

/**
 * fu_device_event_set_bytes:
 * @self: a #FuDeviceEvent
 * @key: (not nullable): a unique key, e.g. `Name`
 * @value: (not nullable): a #GBytes
 *
 * Sets a blob on the event. Note: blobs are stored internally as BASE-64 strings.
 *
 * Since: 2.0.0
 **/
void
fu_device_event_set_bytes(FuDeviceEvent *self, const gchar *key, GBytes *value)
{
	FuDeviceEventPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_DEVICE_EVENT(self));
	g_return_if_fail(key != NULL);
	g_return_if_fail(value != NULL);
	g_hash_table_insert(
	    priv->values,
	    g_strdup(key),
	    fu_device_event_blob_create(
		G_TYPE_STRING,
		g_base64_encode(g_bytes_get_data(value, NULL), g_bytes_get_size(value)),
		g_free));
}

/**
 * fu_device_event_set_data:
 * @self: a #FuDeviceEvent
 * @key: (not nullable): a unique key, e.g. `Name`
 * @buf: (nullable): a buffer
 * @bufsz: size of @buf
 *
 * Sets a memory buffer on the event. Note: memory buffers are stored internally as BASE-64 strings.
 *
 * Since: 2.0.0
 **/
void
fu_device_event_set_data(FuDeviceEvent *self, const gchar *key, const guint8 *buf, gsize bufsz)
{
	FuDeviceEventPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_DEVICE_EVENT(self));
	g_return_if_fail(key != NULL);
	g_hash_table_insert(
	    priv->values,
	    g_strdup(key),
	    fu_device_event_blob_create(G_TYPE_STRING, g_base64_encode(buf, bufsz), g_free));
}

static gpointer
fu_device_event_lookup(FuDeviceEvent *self, const gchar *key, GType gtype, GError **error)
{
	FuDeviceEventPrivate *priv = GET_PRIVATE(self);
	FuDeviceEventBlob *blob = g_hash_table_lookup(priv->values, key);
	if (blob == NULL) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no event for key %s", key);
		return NULL;
	}
	if (blob->gtype != gtype) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid event type for key %s",
			    key);
		return NULL;
	}
	return blob->data;
}

/**
 * fu_device_event_get_str:
 * @self: a #FuDeviceEvent
 * @key: (not nullable): a unique key, e.g. `Name`
 * @error: (nullable): optional return location for an error
 *
 * Gets a string value from the event.
 *
 * Returns: (nullable): string, or %NULL on error
 *
 * Since: 2.0.0
 **/
const gchar *
fu_device_event_get_str(FuDeviceEvent *self, const gchar *key, GError **error)
{
	g_return_val_if_fail(FU_IS_DEVICE_EVENT(self), NULL);
	g_return_val_if_fail(key != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return (const gchar *)fu_device_event_lookup(self, key, G_TYPE_STRING, error);
}

/**
 * fu_device_event_get_i64:
 * @self: a #FuDeviceEvent
 * @key: (not nullable): a unique key, e.g. `Name`
 * @error: (nullable): optional return location for an error
 *
 * Gets an integer value from the event.
 *
 * Returns: (nullable): integer, or %G_MAXINT64 on error
 *
 * Since: 2.0.0
 **/
gint64
fu_device_event_get_i64(FuDeviceEvent *self, const gchar *key, GError **error)
{
	gint64 *val;
	g_return_val_if_fail(FU_IS_DEVICE_EVENT(self), G_MAXINT64);
	g_return_val_if_fail(key != NULL, G_MAXINT64);
	g_return_val_if_fail(error == NULL || *error == NULL, G_MAXINT64);
	val = fu_device_event_lookup(self, key, G_TYPE_INT, error);
	if (val == NULL)
		return G_MAXINT64;
	return *val;
}

/**
 * fu_device_event_get_bytes:
 * @self: a #FuDeviceEvent
 * @key: (not nullable): a unique key, e.g. `Name`
 * @error: (nullable): optional return location for an error
 *
 * Gets a memory blob from the event.
 *
 * Returns: (transfer full) (nullable): byes data, or %NULL on error
 *
 * Since: 2.0.0
 **/
GBytes *
fu_device_event_get_bytes(FuDeviceEvent *self, const gchar *key, GError **error)
{
	const gchar *blobstr;
	gsize bufsz = 0;
	g_autofree guchar *buf = NULL;

	g_return_val_if_fail(FU_IS_DEVICE_EVENT(self), NULL);
	g_return_val_if_fail(key != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	blobstr = fu_device_event_lookup(self, key, G_TYPE_STRING, error);
	if (blobstr == NULL)
		return NULL;
	if (blobstr[0] == '\0')
		return g_bytes_new(NULL, 0);
	buf = g_base64_decode(blobstr, &bufsz);
	return g_bytes_new_take(g_steal_pointer(&buf), bufsz);
}

/**
 * fu_device_event_copy_data:
 * @self: a #FuDeviceEvent
 * @key: (not nullable): a unique key, e.g. `Name`
 * @buf: (nullable): a buffer
 * @bufsz: size of @buf
 * @actual_length: (out) (optional): the actual number of bytes sent, or %NULL
 * @error: (nullable): optional return location for an error
 *
 * Copies memory from the event.
 *
 * Returns: %TRUE if the buffer was copied
 *
 * Since: 2.0.0
 **/
gboolean
fu_device_event_copy_data(FuDeviceEvent *self,
			  const gchar *key,
			  guint8 *buf,
			  gsize bufsz,
			  gsize *actual_length,
			  GError **error)
{
	const gchar *blobstr;
	gsize bufsz_src = 0;
	g_autofree guchar *buf_src = NULL;

	g_return_val_if_fail(FU_IS_DEVICE_EVENT(self), FALSE);
	g_return_val_if_fail(key != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	blobstr = fu_device_event_lookup(self, key, G_TYPE_STRING, error);
	if (blobstr == NULL)
		return FALSE;
	buf_src = g_base64_decode(blobstr, &bufsz_src);
	if (actual_length != NULL)
		*actual_length = bufsz_src;
	if (buf != NULL)
		return fu_memcpy_safe(buf, bufsz, 0x0, buf_src, bufsz_src, 0x0, bufsz_src, error);
	return TRUE;
}

static void
fu_device_event_add_json(FwupdCodec *codec, JsonBuilder *builder, FwupdCodecFlags flags)
{
	FuDeviceEvent *self = FU_DEVICE_EVENT(codec);
	FuDeviceEventPrivate *priv = GET_PRIVATE(self);
	GHashTableIter iter;
	gpointer key, value;

	if (priv->id != NULL) {
		json_builder_set_member_name(builder, "Id");
		json_builder_add_string_value(builder, priv->id);
	}

	g_hash_table_iter_init(&iter, priv->values);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		FuDeviceEventBlob *blob = (FuDeviceEventBlob *)value;
		if (blob->gtype == G_TYPE_INT) {
			json_builder_set_member_name(builder, (const gchar *)key);
			json_builder_add_int_value(builder, *((gint64 *)blob->data));
		} else if (blob->gtype == G_TYPE_BYTES || blob->gtype == G_TYPE_STRING) {
			json_builder_set_member_name(builder, (const gchar *)key);
			json_builder_add_string_value(builder, (const gchar *)blob->data);
		} else {
			g_warning("invalid GType %s, ignoring", g_type_name(blob->gtype));
		}
	}
}

static gboolean
fu_device_event_from_json(FwupdCodec *codec, JsonNode *json_node, GError **error)
{
	FuDeviceEvent *self = FU_DEVICE_EVENT(codec);
	FuDeviceEventPrivate *priv = GET_PRIVATE(self);
	JsonNode *member_node;
	JsonObjectIter iter;
	JsonObject *json_object = json_node_get_object(json_node);
	const gchar *member_name;

	json_object_iter_init(&iter, json_object);
	while (json_object_iter_next(&iter, &member_name, &member_node)) {
		GType gtype;
		if (JSON_NODE_TYPE(member_node) != JSON_NODE_VALUE)
			continue;
		gtype = json_node_get_value_type(member_node);
		if (gtype == G_TYPE_STRING) {
			if (g_strcmp0(member_name, "Id") == 0) {
				g_free(priv->id);
				priv->id = json_node_dup_string(member_node);
				continue;
			}
			fu_device_event_set_str(self,
						member_name,
						json_node_get_string(member_node));
			continue;
		}
		if (gtype == G_TYPE_INT64) {
			fu_device_event_set_i64(self, member_name, json_node_get_int(member_node));
			continue;
		}
	}

	/* success */
	return TRUE;
}

static void
fu_device_event_init(FuDeviceEvent *self)
{
	FuDeviceEventPrivate *priv = GET_PRIVATE(self);
	priv->values = g_hash_table_new_full(g_str_hash,
					     g_str_equal,
					     g_free,
					     (GDestroyNotify)fu_device_event_blob_free);
}

static void
fu_device_event_finalize(GObject *object)
{
	FuDeviceEvent *self = FU_DEVICE_EVENT(object);
	FuDeviceEventPrivate *priv = GET_PRIVATE(self);
	g_free(priv->id);
	g_hash_table_unref(priv->values);
	G_OBJECT_CLASS(fu_device_event_parent_class)->finalize(object);
}

static void
fu_device_event_class_init(FuDeviceEventClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_device_event_finalize;
}

static void
fu_device_event_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_json = fu_device_event_add_json;
	iface->from_json = fu_device_event_from_json;
}

/**
 * fu_device_event_new:
 * @id: a cache key
 *
 * Return value: (transfer full): a new #FuDeviceEvent object.
 *
 * Since: 2.0.0
 **/
FuDeviceEvent *
fu_device_event_new(const gchar *id)
{
	FuDeviceEvent *self = g_object_new(FU_TYPE_DEVICE_EVENT, NULL);
	FuDeviceEventPrivate *priv = GET_PRIVATE(self);
	priv->id = g_strdup(id);
	return FU_DEVICE_EVENT(self);
}
