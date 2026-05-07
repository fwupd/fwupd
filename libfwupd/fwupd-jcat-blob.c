/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <inttypes.h>

#include "fwupd-codec.h"
#include "fwupd-error.h"
#include "fwupd-jcat-blob.h"

struct _FwupdJcatBlob {
	GObject parent_instance;
	FwupdJcatBlobKind kind;
	FwupdJcatBlobKind target;
	FwupdJcatBlobFlags flags;
	GBytes *data;
	guint64 timestamp;
};

static void
fwupd_jcat_blob_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FwupdJcatBlob,
		       fwupd_jcat_blob,
		       G_TYPE_OBJECT,
		       0,
		       G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC, fwupd_jcat_blob_codec_iface_init));

static void
fwupd_jcat_blob_add_string(FwupdCodec *codec, guint idt, GString *str)
{
	FwupdJcatBlob *self = FWUPD_JCAT_BLOB(codec);
	g_autofree gchar *flags = fwupd_jcat_blob_flags_to_string(self->flags);

	fwupd_codec_string_append(str, idt, "Kind", fwupd_jcat_blob_kind_to_string(self->kind));
	if (self->target != FWUPD_JCAT_BLOB_KIND_UNKNOWN) {
		fwupd_codec_string_append(str,
					  idt,
					  "Target",
					  fwupd_jcat_blob_kind_to_string(self->target));
	}
	fwupd_codec_string_append(str, idt, "Flags", flags);
	if (self->timestamp != 0) {
		g_autoptr(GDateTime) dt = g_date_time_new_from_unix_utc(self->timestamp);
		if (dt != NULL) {
#if GLIB_CHECK_VERSION(2, 62, 0)
			g_autofree gchar *tmp = g_date_time_format_iso8601(dt);
#else
			g_autofree gchar *tmp = g_date_time_format(dt, "%FT%TZ");
#endif
			fwupd_codec_string_append(str, idt, "Timestamp", tmp);
		}
	}
	if (self->data != NULL) {
		g_autofree gchar *tmp = fwupd_jcat_blob_get_data_as_string(self);
		g_autofree gchar *size =
		    g_strdup_printf("0x%" PRIx64, (guint64)g_bytes_get_size(self->data));
		fwupd_codec_string_append(str, idt, "Size", size);
		fwupd_codec_string_append(str, idt, "Data", tmp);
	}
}

/**
 * fwupd_jcat_blob_get_timestamp:
 * @self: #FwupdJcatBlob
 *
 * Gets the creation timestamp for the blob.
 *
 * Returns: UTC UNIX time, or 0 if unset
 *
 * Since: 2.1.3
 **/
guint64
fwupd_jcat_blob_get_timestamp(FwupdJcatBlob *self)
{
	g_return_val_if_fail(FWUPD_IS_JCAT_BLOB(self), 0);
	return self->timestamp;
}

/**
 * fwupd_jcat_blob_set_timestamp:
 * @self: #FwupdJcatBlob
 * @timestamp: UTC timestamp
 *
 * Sets the creation timestamp for the blob.
 *
 * Since: 2.1.3
 **/
void
fwupd_jcat_blob_set_timestamp(FwupdJcatBlob *self, guint64 timestamp)
{
	g_return_if_fail(FWUPD_IS_JCAT_BLOB(self));
	self->timestamp = timestamp;
}

/**
 * fwupd_jcat_blob_get_data:
 * @self: #FwupdJcatBlob
 *
 * Gets the data stored in the blob, typically in binary (unprintable) form.
 *
 * Returns: (transfer none): a #GBytes, or %NULL if the filename was not found
 *
 * Since: 2.1.3
 **/
GBytes *
fwupd_jcat_blob_get_data(FwupdJcatBlob *self)
{
	g_return_val_if_fail(FWUPD_IS_JCAT_BLOB(self), NULL);
	return self->data;
}

/**
 * fwupd_jcat_blob_get_data_as_string:
 * @self: #FwupdJcatBlob
 *
 * Gets the data stored in the blob, in human readable form.
 *
 * Returns: (transfer full): either UTF-8 text, or base64 encoded version of binary data
 *
 * Since: 2.1.3
 **/
gchar *
fwupd_jcat_blob_get_data_as_string(FwupdJcatBlob *self)
{
	gsize bufsz = 0;
	const guchar *buf;

	g_return_val_if_fail(FWUPD_IS_JCAT_BLOB(self), NULL);

	/* may be binary data or not NULL terminated */
	if (self->data == NULL)
		return NULL;
	buf = g_bytes_get_data(self->data, &bufsz);
	if ((self->flags & FWUPD_JCAT_BLOB_FLAG_IS_UTF8) == 0)
		return g_base64_encode(buf, bufsz);
	return g_strndup((const gchar *)buf, bufsz);
}

static void
fwupd_jcat_blob_set_data_raw(FwupdJcatBlob *self, const guint8 *buf, gsize bufsz)
{
	if (self->data != NULL)
		g_bytes_unref(self->data);
	self->data = g_bytes_new(buf, bufsz);
}

/**
 * fwupd_jcat_blob_get_kind:
 * @self: #FwupdJcatBlob
 *
 * gets the blob kind
 *
 * Returns: #FwupdJcatBlobKind, e.g. %FWUPD_JCAT_BLOB_KIND_SHA256
 *
 * Since: 2.1.3
 **/
FwupdJcatBlobKind
fwupd_jcat_blob_get_kind(FwupdJcatBlob *self)
{
	g_return_val_if_fail(FWUPD_IS_JCAT_BLOB(self), 0);
	return self->kind;
}

/**
 * fwupd_jcat_blob_get_target:
 * @self: #FwupdJcatBlob
 *
 * Gets the blob target.
 *
 * Returns: #FwupdJcatBlobKind, e.g. %FWUPD_JCAT_BLOB_KIND_SHA256
 *
 * Since: 2.1.3
 **/
FwupdJcatBlobKind
fwupd_jcat_blob_get_target(FwupdJcatBlob *self)
{
	g_return_val_if_fail(FWUPD_IS_JCAT_BLOB(self), 0);
	return self->target;
}

/**
 * fwupd_jcat_blob_set_target:
 * @self: #FwupdJcatBlob
 * @target: a #FwupdJcatBlobKind, e.g. %FWUPD_JCAT_BLOB_KIND_SHA256
 *
 * Sets the blob target.
 *
 * Since: 2.1.3
 **/
void
fwupd_jcat_blob_set_target(FwupdJcatBlob *self, FwupdJcatBlobKind target)
{
	g_return_if_fail(FWUPD_IS_JCAT_BLOB(self));
	self->target = target;
}

static gboolean
fwupd_jcat_blob_from_json(FwupdCodec *codec, FwupdJsonObject *json_obj, GError **error)
{
	FwupdJcatBlob *self = FWUPD_JCAT_BLOB(codec);
	const gchar *data_str;
	gint64 timestamp_tmp = 0;
	gint64 flags_tmp = 0;
	gint64 kind_tmp = 0;

	/* get kind, which can be unknown to us for forward compat */
	if (!fwupd_json_object_get_integer(json_obj, "Kind", &kind_tmp, error))
		return FALSE;
	if (kind_tmp < 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "invalid kind");
		return FALSE;
	}
	self->kind = (FwupdJcatBlobKind)kind_tmp;

	/* get flags, which can also be unknown to us */
	if (!fwupd_json_object_get_integer(json_obj, "Flags", &flags_tmp, error))
		return FALSE;
	if (flags_tmp < 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "invalid flags");
		return FALSE;
	}
	self->flags = (FwupdJcatBlobFlags)flags_tmp;

	/* all optional */
	if (!fwupd_json_object_get_integer_with_default(json_obj,
							"Timestamp",
							&timestamp_tmp,
							0,
							error))
		return FALSE;
	if (timestamp_tmp < 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid timestamp");
		return FALSE;
	}
	fwupd_jcat_blob_set_timestamp(self, (guint64)timestamp_tmp);

	if (fwupd_json_object_has_node(json_obj, "Target")) {
		gint64 target_tmp = 0;
		if (!fwupd_json_object_get_integer(json_obj, "Target", &target_tmp, error))
			return FALSE;
		if (target_tmp < 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "invalid target");
			return FALSE;
		}
		fwupd_jcat_blob_set_target(self, (FwupdJcatBlobKind)target_tmp);
	}

	/* get compressed data */
	data_str = fwupd_json_object_get_string(json_obj, "Data", error);
	if (data_str == NULL)
		return FALSE;
	if ((self->flags & FWUPD_JCAT_BLOB_FLAG_IS_UTF8) == 0) {
		gsize bufsz = 0;
		g_autofree guchar *buf = g_base64_decode(data_str, &bufsz);
		fwupd_jcat_blob_set_data_raw(self, (guint8 *)buf, bufsz);
	} else {
		fwupd_jcat_blob_set_data_raw(self, (const guint8 *)data_str, strlen(data_str));
	}

	/* success */
	return TRUE;
}

static void
fwupd_jcat_blob_add_json(FwupdCodec *codec, FwupdJsonObject *json_obj, FwupdCodecFlags flags)
{
	FwupdJcatBlob *self = FWUPD_JCAT_BLOB(codec);
	g_autofree gchar *data_str = fwupd_jcat_blob_get_data_as_string(self);

	fwupd_json_object_add_integer(json_obj, "Kind", self->kind);
	if (self->target != FWUPD_JCAT_BLOB_KIND_UNKNOWN)
		fwupd_json_object_add_integer(json_obj, "Target", self->target);
	fwupd_json_object_add_integer(json_obj, "Flags", self->flags);
	if (self->timestamp > 0 && (flags & FWUPD_CODEC_FLAG_NO_TIMESTAMP) == 0)
		fwupd_json_object_add_integer(json_obj, "Timestamp", self->timestamp);
	if (data_str != NULL)
		fwupd_json_object_add_string(json_obj, "Data", data_str);
}

static void
fwupd_jcat_blob_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_string = fwupd_jcat_blob_add_string;
	iface->add_json = fwupd_jcat_blob_add_json;
	iface->from_json = fwupd_jcat_blob_from_json;
}

static void
fwupd_jcat_blob_finalize(GObject *obj)
{
	FwupdJcatBlob *self = FWUPD_JCAT_BLOB(obj);
	if (self->data != NULL)
		g_bytes_unref(self->data);
	G_OBJECT_CLASS(fwupd_jcat_blob_parent_class)->finalize(obj);
}

static void
fwupd_jcat_blob_class_init(FwupdJcatBlobClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fwupd_jcat_blob_finalize;
}

static void
fwupd_jcat_blob_init(FwupdJcatBlob *self)
{
	self->timestamp = g_get_real_time() / G_USEC_PER_SEC;
}

/**
 * fwupd_jcat_blob_new:
 * @kind: #FwupdJcatBlobKind, e.g. %FWUPD_JCAT_BLOB_KIND_SHA256
 * @data: #GBytes
 * @flags: #FwupdJcatBlobFlags
 *
 * Creates a new blob.
 *
 * Returns: a #FwupdJcatBlob
 *
 * Since: 2.1.3
 **/
FwupdJcatBlob *
fwupd_jcat_blob_new(FwupdJcatBlobKind kind, GBytes *data, FwupdJcatBlobFlags flags)
{
	g_autoptr(FwupdJcatBlob) self = g_object_new(FWUPD_TYPE_JCAT_BLOB, NULL);

	g_return_val_if_fail(data != NULL, NULL);

	self->kind = kind;
	self->data = g_bytes_ref(data);
	self->flags = flags;
	return g_steal_pointer(&self);
}

/**
 * fwupd_jcat_blob_new_utf8:
 * @kind: #FwupdJcatBlobKind, e.g. %FWUPD_JCAT_BLOB_KIND_SHA256
 * @data: (not nullable): ASCII data
 *
 * Creates a new ASCII blob.
 *
 * Returns: a #FwupdJcatBlob
 *
 * Since: 2.1.3
 **/
FwupdJcatBlob *
fwupd_jcat_blob_new_utf8(FwupdJcatBlobKind kind, const gchar *data)
{
	g_autoptr(FwupdJcatBlob) self = g_object_new(FWUPD_TYPE_JCAT_BLOB, NULL);

	g_return_val_if_fail(data != NULL, NULL);

	self->flags = FWUPD_JCAT_BLOB_FLAG_IS_UTF8;
	self->kind = kind;
	fwupd_jcat_blob_set_data_raw(self, (const guint8 *)data, strlen(data));
	return g_steal_pointer(&self);
}
