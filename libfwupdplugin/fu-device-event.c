/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuDeviceEvent"

#include "config.h"

#include "fu-device-event-private.h"
#include "fu-mem.h"
#include "fu-string.h"

/**
 * FuDeviceEvent:
 *
 * A device event, used to enumulate hardware.
 *
 * See also: [class@FuDevice]
 */

typedef struct {
	GType gtype;
	GRefString *key;
	gpointer data;
	GDestroyNotify data_destroy;
} FuDeviceEventBlob;

struct _FuDeviceEvent {
	GObject parent_instance;
	gchar *id;
	gchar *id_uncompressed;
	GPtrArray *values; /* element-type FuDeviceEventBlob */
};

static void
fu_device_event_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_WITH_CODE(FuDeviceEvent,
			fu_device_event,
			G_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC, fu_device_event_codec_iface_init))

/*
 * NOTE: We use an event *counter* that gets the next event in the emulation, and this ID is only
 * used as a sanity check in case we have to skip an entry.
 */
#define FU_DEVICE_EVENT_KEY_HASH_PREFIX_SIZE 8

static void
fu_device_event_blob_free(FuDeviceEventBlob *blob)
{
	g_ref_string_release(blob->key);
	if (blob->data_destroy != NULL)
		blob->data_destroy(blob->data);
	g_free(blob);
}

static FuDeviceEventBlob *
fu_device_event_blob_new_internal(GType gtype,
				  GRefString *key,
				  gpointer data,
				  GDestroyNotify data_destroy)
{
	FuDeviceEventBlob *blob = g_new0(FuDeviceEventBlob, 1);
	blob->key = g_ref_string_acquire(key);
	blob->gtype = gtype;
	blob->data = data;
	blob->data_destroy = data_destroy;
	return blob;
}

static FuDeviceEventBlob *
fu_device_event_blob_new(GType gtype, const gchar *key, gpointer data, GDestroyNotify data_destroy)
{
	g_autoptr(GRefString) key_ref = NULL;
	const gchar *known_keys[] = {
	    "Data",
	    "DataOut",
	    "Error",
	    "ErrorMsg",
	    "Rc",
	    NULL,
	};
	key_ref = g_strv_contains(known_keys, key) ? g_ref_string_new_intern(key)
						   : g_ref_string_new(key);
	return fu_device_event_blob_new_internal(gtype, key_ref, data, data_destroy);
}

/**
 * fu_device_event_build_id:
 * @id: a string
 *
 * Return the hash of the event ID.
 *
 * Returns: string hash prefix
 *
 * Since: 2.0.3
 **/
gchar *
fu_device_event_build_id(const gchar *id)
{
	guint8 buf[20] = {0};
	gsize bufsz = sizeof(buf);
	g_autoptr(GChecksum) csum = g_checksum_new(G_CHECKSUM_SHA1);
	g_autoptr(GString) id_hash = g_string_sized_new(FU_DEVICE_EVENT_KEY_HASH_PREFIX_SIZE + 1);

	g_return_val_if_fail(id != NULL, NULL);

	/* IMPORTANT: if you're reading this we're not using the SHA1 prefix for any kind of secure
	 * hash, just because it is a tiny string that takes up less memory than the full ID. */
	g_checksum_update(csum, (const guchar *)id, strlen(id));
	g_checksum_get_digest(csum, buf, &bufsz);
	g_string_append_c(id_hash, '#');
	for (guint i = 0; i < FU_DEVICE_EVENT_KEY_HASH_PREFIX_SIZE / 2; i++)
		g_string_append_printf(id_hash, "%02x", buf[i]);
	return g_string_free(g_steal_pointer(&id_hash), FALSE);
}

/**
 * fu_device_event_get_id:
 * @self: a #FuDeviceEvent
 *
 * Return the truncated SHA1 of the #FuDeviceEvent key, which is normally set when creating the
 * object.
 *
 * Returns: (nullable): string
 *
 * Since: 2.0.0
 **/
const gchar *
fu_device_event_get_id(FuDeviceEvent *self)
{
	g_return_val_if_fail(FU_IS_DEVICE_EVENT(self), NULL);
	return self->id;
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
	g_return_if_fail(FU_IS_DEVICE_EVENT(self));
	g_return_if_fail(key != NULL);
	g_ptr_array_add(self->values,
			fu_device_event_blob_new(G_TYPE_STRING, key, g_strdup(value), g_free));
}

/**
 * fu_device_event_set_i64:
 * @self: a #FuDeviceEvent
 * @key: (not nullable): a unique key, e.g. `Name`
 * @value: a string
 *
 * Sets an integer value on the string.
 *
 * Since: 2.0.0
 **/
void
fu_device_event_set_i64(FuDeviceEvent *self, const gchar *key, gint64 value)
{
	g_return_if_fail(FU_IS_DEVICE_EVENT(self));
	g_return_if_fail(key != NULL);

	g_ptr_array_add(
	    self->values,
	    fu_device_event_blob_new(G_TYPE_INT, key, g_memdup2(&value, sizeof(value)), g_free));
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
	g_return_if_fail(FU_IS_DEVICE_EVENT(self));
	g_return_if_fail(key != NULL);
	g_return_if_fail(value != NULL);
	g_ptr_array_add(self->values,
			fu_device_event_blob_new(
			    G_TYPE_STRING,
			    key,
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
	g_return_if_fail(FU_IS_DEVICE_EVENT(self));
	g_return_if_fail(key != NULL);
	g_ptr_array_add(
	    self->values,
	    fu_device_event_blob_new(G_TYPE_STRING, key, g_base64_encode(buf, bufsz), g_free));
}

/**
 * fu_device_event_set_error:
 * @self: a #FuDeviceEvent
 * @error: (not nullable): a #GError with domain #FwupdError
 *
 * Sets an error on the event.
 *
 * Since: 2.0.6
 **/
void
fu_device_event_set_error(FuDeviceEvent *self, const GError *error)
{
	g_return_if_fail(FU_IS_DEVICE_EVENT(self));
	g_return_if_fail(error != NULL);
	g_return_if_fail(error->domain == FWUPD_ERROR);
	fu_device_event_set_i64(self, "Error", error->code);
	fu_device_event_set_str(self, "ErrorMsg", error->message);
}

/**
 * fu_device_event_check_error:
 * @self: a #FuDeviceEvent
 * @error: (nullable): optional return location for an error
 *
 * Sets an error from the event if possible.
 *
 * Returns: %FALSE if @error was set
 *
 * Since: 2.0.6
 **/
gboolean
fu_device_event_check_error(FuDeviceEvent *self, GError **error)
{
	gint64 code;
	const gchar *message;

	g_return_val_if_fail(FU_IS_DEVICE_EVENT(self), FALSE);

	/* nothing to do */
	if (error == NULL)
		return TRUE;

	/* anything set */
	code = fu_device_event_get_i64(self, "Error", NULL);
	if (code == G_MAXINT64)
		return TRUE;
	message = fu_device_event_get_str(self, "ErrorMsg", NULL);
	if (message == NULL)
		message = fwupd_error_to_string(code);

	/* success, in a way */
	g_set_error_literal(error, FWUPD_ERROR, code, message);
	return FALSE;
}

static gpointer
fu_device_event_lookup(FuDeviceEvent *self, const gchar *key, GType gtype, GError **error)
{
	FuDeviceEventBlob *blob = NULL;

	for (guint i = 0; i < self->values->len; i++) {
		FuDeviceEventBlob *blob_tmp = g_ptr_array_index(self->values, i);
		if (g_strcmp0(blob_tmp->key, key) == 0) {
			blob = blob_tmp;
			break;
		}
	}
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
 * Returns: integer, or %G_MAXINT64 on error
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
fu_device_event_add_json(FwupdCodec *codec, FwupdJsonObject *json_obj, FwupdCodecFlags flags)
{
	FuDeviceEvent *self = FU_DEVICE_EVENT(codec);

	if (self->id_uncompressed != NULL && (flags & FWUPD_CODEC_FLAG_COMPRESSED) == 0) {
		fwupd_json_object_add_string(json_obj, "Id", self->id_uncompressed);
	} else if (self->id != NULL) {
		fwupd_json_object_add_string(json_obj, "Id", self->id);
	}

	for (guint i = 0; i < self->values->len; i++) {
		FuDeviceEventBlob *blob = g_ptr_array_index(self->values, i);
		if (blob->gtype == G_TYPE_INT) {
			fwupd_json_object_add_integer(json_obj, blob->key, *((gint64 *)blob->data));
		} else if (blob->gtype == G_TYPE_BYTES || blob->gtype == G_TYPE_STRING) {
			fwupd_json_object_add_string(json_obj,
						     blob->key,
						     (const gchar *)blob->data);
		} else {
			g_warning("invalid GType %s, ignoring", g_type_name(blob->gtype));
		}
	}
}

static void
fu_device_event_set_id(FuDeviceEvent *self, const gchar *id)
{
	g_return_if_fail(FU_IS_DEVICE_EVENT(self));
	g_return_if_fail(id != NULL);

	g_clear_pointer(&self->id, g_free);
	g_clear_pointer(&self->id_uncompressed, g_free);

	/* already a truncated SHA1 hash? */
	if (g_str_has_prefix(id, "#")) {
		self->id = g_strdup(id);
	} else {
		self->id_uncompressed = g_strdup(id);
		self->id = fu_device_event_build_id(id);
	}
}

static gboolean
fu_device_event_from_json(FwupdCodec *codec, FwupdJsonObject *json_obj, GError **error)
{
	FuDeviceEvent *self = FU_DEVICE_EVENT(codec);
	for (guint i = 0; i < fwupd_json_object_get_size(json_obj); i++) {
		GRefString *key = fwupd_json_object_get_key_for_index(json_obj, i, NULL);
		g_autoptr(FwupdJsonNode) json_node = NULL;

		json_node = fwupd_json_object_get_node_for_index(json_obj, i, error);
		if (json_node == NULL)
			return FALSE;
		if (fwupd_json_node_get_kind(json_node) == FWUPD_JSON_NODE_KIND_STRING) {
			GRefString *str = fwupd_json_node_get_string(json_node, NULL);
			if (g_strcmp0(key, "Id") == 0) {
				fu_device_event_set_id(self, str);
			} else if (str != NULL) {
				g_ptr_array_add(self->values,
						fu_device_event_blob_new_internal(
						    G_TYPE_STRING,
						    key,
						    g_ref_string_acquire(str),
						    (GDestroyNotify)g_ref_string_release));
			} else {
				g_ptr_array_add(self->values,
						fu_device_event_blob_new_internal(G_TYPE_STRING,
										  key,
										  NULL,
										  NULL));
			}
		} else if (fwupd_json_node_get_kind(json_node) == FWUPD_JSON_NODE_KIND_RAW) {
			GRefString *str;
			gint64 value = 0;

			str = fwupd_json_node_get_raw(json_node, error);
			if (str == NULL)
				return FALSE;
			if (!fu_strtoll(str,
					&value,
					G_MININT64,
					G_MAXINT64,
					FU_INTEGER_BASE_AUTO,
					error))
				return FALSE;
			g_ptr_array_add(
			    self->values,
			    fu_device_event_blob_new_internal(G_TYPE_INT,
							      key,
							      g_memdup2(&value, sizeof(value)),
							      g_free));
		}
	}

	/* we do not need this again, so avoid keeping all the tree data in memory */
	fwupd_json_object_clear(json_obj);

	/* success */
	return TRUE;
}

static void
fu_device_event_init(FuDeviceEvent *self)
{
	self->values = g_ptr_array_new_with_free_func((GDestroyNotify)fu_device_event_blob_free);
}

static void
fu_device_event_finalize(GObject *object)
{
	FuDeviceEvent *self = FU_DEVICE_EVENT(object);
	g_free(self->id);
	g_free(self->id_uncompressed);
	g_ptr_array_unref(self->values);
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
 * @id: a cache key, which is converted to a truncated SHA1 hash if required
 *
 * Return value: (transfer full): a new #FuDeviceEvent object.
 *
 * Since: 2.0.0
 **/
FuDeviceEvent *
fu_device_event_new(const gchar *id)
{
	FuDeviceEvent *self = g_object_new(FU_TYPE_DEVICE_EVENT, NULL);
	if (id != NULL)
		fu_device_event_set_id(self, id);
	return FU_DEVICE_EVENT(self);
}
