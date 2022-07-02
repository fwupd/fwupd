/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-common.h"
#include "fu-fdt-image.h"
#include "fu-mem.h"
#include "fu-string.h"

/**
 * FuFdtImage:
 *
 * A Flattened DeviceTree firmware image.
 *
 * See also: [class@FuFdtFirmware]
 */

typedef struct {
	GHashTable *hash_attrs;
	GHashTable *hash_attrs_format;
} FuFdtImagePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuFdtImage, fu_fdt_image, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_fdt_image_get_instance_private(o))

#define FU_FDT_IMAGE_FORMAT_STR	    "str"
#define FU_FDT_IMAGE_FORMAT_STRLIST "strlist"
#define FU_FDT_IMAGE_FORMAT_UINT32  "uint32"
#define FU_FDT_IMAGE_FORMAT_UINT64  "uint64"
#define FU_FDT_IMAGE_FORMAT_DATA    "data"

static const gchar *
fu_fdt_image_guess_format_from_key(const gchar *key)
{
	struct {
		const gchar *key;
		const gchar *format;
	} key_format_map[] = {{"#address-cells", FU_FDT_IMAGE_FORMAT_UINT32},
			      {"algo", FU_FDT_IMAGE_FORMAT_STR},
			      {"arch", FU_FDT_IMAGE_FORMAT_STR},
			      {"compatible", FU_FDT_IMAGE_FORMAT_STRLIST},
			      {"compression", FU_FDT_IMAGE_FORMAT_STR},
			      {"creator", FU_FDT_IMAGE_FORMAT_STR},
			      {"data-offset", FU_FDT_IMAGE_FORMAT_UINT32},
			      {"data-size", FU_FDT_IMAGE_FORMAT_UINT32},
			      {"default", FU_FDT_IMAGE_FORMAT_STR},
			      {"description", FU_FDT_IMAGE_FORMAT_STR},
			      {"entry", FU_FDT_IMAGE_FORMAT_STR},
			      {"firmware", FU_FDT_IMAGE_FORMAT_STR},
			      {"load", FU_FDT_IMAGE_FORMAT_UINT32},
			      {"os", FU_FDT_IMAGE_FORMAT_STR},
			      {"timestamp", FU_FDT_IMAGE_FORMAT_UINT32},
			      {"type", FU_FDT_IMAGE_FORMAT_STR},
			      {"version", FU_FDT_IMAGE_FORMAT_STR},
			      {NULL, NULL}};
	for (guint i = 0; key_format_map[i].key != NULL; i++) {
		if (g_strcmp0(key, key_format_map[i].key) == 0)
			return key_format_map[i].format;
	}
	return NULL;
}

static gchar **
fu_fdt_image_strlist_from_blob(GBytes *blob)
{
	gchar **val;
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(blob, &bufsz);
	g_autoptr(GPtrArray) strs = g_ptr_array_new();

	/* delimit by NUL */
	for (gsize i = 0; i < bufsz; i++) {
		const gchar *tmp = (const gchar *)buf + i;
		g_ptr_array_add(strs, (gpointer)tmp);
		i += strlen(tmp);
	}

	/* copy to GStrv */
	val = g_new0(gchar *, strs->len + 1);
	for (guint i = 0; i < strs->len; i++)
		val[i] = g_strdup(g_ptr_array_index(strs, i));
	return val;
}

static void
fu_fdt_image_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuFdtImage *self = FU_FDT_IMAGE(firmware);
	FuFdtImagePrivate *priv = GET_PRIVATE(self);
	GHashTableIter iter;
	gpointer key, value;

	g_hash_table_iter_init(&iter, priv->hash_attrs);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		gsize bufsz = 0;
		const guint8 *buf = g_bytes_get_data(value, &bufsz);
		const gchar *format = g_hash_table_lookup(priv->hash_attrs_format, key);
		g_autofree gchar *str = NULL;
		g_autoptr(XbBuilderNode) bc = NULL;

		/* guess format based on key name to improve debugging experience */
		if (format == NULL)
			format = fu_fdt_image_guess_format_from_key(key);
		if (g_strcmp0(format, FU_FDT_IMAGE_FORMAT_UINT32) == 0 && bufsz == 4) {
			guint64 tmp = fu_memread_uint32(buf, G_BIG_ENDIAN);
			str = g_strdup_printf("0x%x", (guint)tmp);
		} else if (g_strcmp0(format, FU_FDT_IMAGE_FORMAT_UINT64) == 0 && bufsz == 8) {
			guint64 tmp = fu_memread_uint64(buf, G_BIG_ENDIAN);
			str = g_strdup_printf("0x%x", (guint)tmp);
		} else if (g_strcmp0(format, FU_FDT_IMAGE_FORMAT_STR) == 0 && bufsz > 0) {
			str = g_strndup((const gchar *)buf, bufsz);
		} else if (g_strcmp0(format, FU_FDT_IMAGE_FORMAT_STRLIST) == 0 && bufsz > 0) {
			g_auto(GStrv) tmp = fu_fdt_image_strlist_from_blob(value);
			str = g_strjoinv(":", tmp);
		} else {
			str = g_base64_encode(buf, bufsz);
		}
		bc = xb_builder_node_insert(bn, "metadata", "key", key, NULL);
		if (str != NULL)
			xb_builder_node_set_text(bc, str, -1);
		if (format != NULL)
			xb_builder_node_set_attr(bc, "format", format);
	}
}

/**
 * fu_fdt_image_get_attrs:
 * @self: a #FuFdtImage
 *
 * Gets all the attributes stored on the image.
 *
 * Returns: (transfer container) (element-type utf8): keys
 *
 * Since: 1.8.2
 **/
GPtrArray *
fu_fdt_image_get_attrs(FuFdtImage *self)
{
	FuFdtImagePrivate *priv = GET_PRIVATE(self);
	GPtrArray *array = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(GList) keys = NULL;

	g_return_val_if_fail(FU_IS_FDT_IMAGE(self), NULL);

	keys = g_hash_table_get_keys(priv->hash_attrs);
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		g_ptr_array_add(array, g_strdup(key));
	}
	return array;
}

/**
 * fu_fdt_image_get_attr:
 * @self: a #FuFdtImage
 * @key: string, e.g. `creator`
 * @error: (nullable): optional return location for an error
 *
 * Gets a attribute from the image.
 *
 * Returns: (transfer full): blob
 *
 * Since: 1.8.2
 **/
GBytes *
fu_fdt_image_get_attr(FuFdtImage *self, const gchar *key, GError **error)
{
	FuFdtImagePrivate *priv = GET_PRIVATE(self);
	GBytes *blob;

	g_return_val_if_fail(FU_IS_FDT_IMAGE(self), NULL);
	g_return_val_if_fail(key != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	blob = g_hash_table_lookup(priv->hash_attrs, key);
	if (blob == NULL) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no data for %s", key);
		return NULL;
	}

	/* success */
	return g_bytes_ref(blob);
}

/**
 * fu_fdt_image_get_attr_u32:
 * @self: a #FuFdtImage
 * @key: string, e.g. `creator`
 * @val: (out) (nullable): value
 * @error: (nullable): optional return location for an error
 *
 * Gets a uint32 attribute from the image.
 *
 * Returns: %TRUE if @val was set.
 *
 * Since: 1.8.2
 **/
gboolean
fu_fdt_image_get_attr_u32(FuFdtImage *self, const gchar *key, guint32 *val, GError **error)
{
	g_autoptr(GBytes) blob = NULL;

	g_return_val_if_fail(FU_IS_FDT_IMAGE(self), FALSE);
	g_return_val_if_fail(key != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	blob = fu_fdt_image_get_attr(self, key, error);
	if (blob == NULL)
		return FALSE;
	if (g_bytes_get_size(blob) != sizeof(guint32)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid data size for %s, got 0x%x, expected 0x%x",
			    key,
			    (guint)g_bytes_get_size(blob),
			    (guint)sizeof(guint32));
		return FALSE;
	}
	if (val != NULL)
		*val = fu_memread_uint32(g_bytes_get_data(blob, NULL), G_BIG_ENDIAN);
	return TRUE;
}

/**
 * fu_fdt_image_get_attr_u64:
 * @self: a #FuFdtImage
 * @key: string, e.g. `creator`
 * @val: (out) (nullable): value
 * @error: (nullable): optional return location for an error
 *
 * Gets a uint64 attribute from the image.
 *
 * Returns: %TRUE if @val was set.
 *
 * Since: 1.8.2
 **/
gboolean
fu_fdt_image_get_attr_u64(FuFdtImage *self, const gchar *key, guint64 *val, GError **error)
{
	g_autoptr(GBytes) blob = NULL;

	g_return_val_if_fail(FU_IS_FDT_IMAGE(self), FALSE);
	g_return_val_if_fail(key != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	blob = fu_fdt_image_get_attr(self, key, error);
	if (blob == NULL)
		return FALSE;
	if (g_bytes_get_size(blob) != sizeof(guint64)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid data size for %s, got 0x%x, expected 0x%x",
			    key,
			    (guint)g_bytes_get_size(blob),
			    (guint)sizeof(guint64));
		return FALSE;
	}
	if (val != NULL)
		*val = fu_memread_uint64(g_bytes_get_data(blob, NULL), G_BIG_ENDIAN);
	return TRUE;
}

/**
 * fu_fdt_image_get_attr_strlist:
 * @self: a #FuFdtImage
 * @key: string, e.g. `compatible`
 * @val: (out) (nullable) (transfer full): values
 * @error: (nullable): optional return location for an error
 *
 * Gets a stringlist attribute from the image. @val is always `NUL` terminated.
 *
 * Returns: %TRUE if @val was set.
 *
 * Since: 1.8.2
 **/
gboolean
fu_fdt_image_get_attr_strlist(FuFdtImage *self, const gchar *key, gchar ***val, GError **error)
{
	g_autoptr(GBytes) blob = NULL;
	const guint8 *buf;
	gsize bufsz = 0;

	g_return_val_if_fail(FU_IS_FDT_IMAGE(self), FALSE);
	g_return_val_if_fail(key != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	blob = fu_fdt_image_get_attr(self, key, error);
	if (blob == NULL)
		return FALSE;
	if (g_bytes_get_size(blob) == 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid data size for %s, got 0x%x",
			    key,
			    (guint)g_bytes_get_size(blob));
		return FALSE;
	}

	/* sanity check */
	buf = g_bytes_get_data(blob, &bufsz);
	for (gsize i = 0; i < bufsz; i++) {
		if (buf[i] != 0x0 && !g_ascii_isprint((gchar)buf[i])) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "nonprintable character 0x%02x at offset 0x%x in %s",
				    buf[i],
				    (guint)i,
				    key);
			return FALSE;
		}
	}

	/* success */
	if (val != NULL)
		*val = fu_fdt_image_strlist_from_blob(blob);
	return TRUE;
}

/**
 * fu_fdt_image_get_attr_str:
 * @self: a #FuFdtImage
 * @key: string, e.g. `creator`
 * @val: (out) (nullable) (transfer full): value
 * @error: (nullable): optional return location for an error
 *
 * Gets a string attribute from the image. @val is always `NUL` terminated.
 *
 * Returns: %TRUE if @val was set.
 *
 * Since: 1.8.2
 **/
gboolean
fu_fdt_image_get_attr_str(FuFdtImage *self, const gchar *key, gchar **val, GError **error)
{
	g_autoptr(GBytes) blob = NULL;
	const guint8 *buf;
	gsize bufsz = 0;

	g_return_val_if_fail(FU_IS_FDT_IMAGE(self), FALSE);
	g_return_val_if_fail(key != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	blob = fu_fdt_image_get_attr(self, key, error);
	if (blob == NULL)
		return FALSE;
	if (g_bytes_get_size(blob) == 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid data size for %s, got 0x%x",
			    key,
			    (guint)g_bytes_get_size(blob));
		return FALSE;
	}

	/* sanity check */
	buf = g_bytes_get_data(blob, &bufsz);
	for (gsize i = 0; i < bufsz; i++) {
		if (buf[i] != 0x0 && !g_ascii_isprint((gchar)buf[i])) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "nonprintable character 0x%02x at offset 0x%x in %s",
				    buf[i],
				    (guint)i,
				    key);
			return FALSE;
		}
	}

	/* success */
	if (val != NULL)
		*val = g_strndup(g_bytes_get_data(blob, NULL), g_bytes_get_size(blob));
	return TRUE;
}

/**
 * fu_fdt_image_set_attr:
 * @self: a #FuFdtImage
 * @key: string, e.g. `creator`
 * @blob: a #GBytes
 *
 * Sets a attribute for the image.
 *
 * Since: 1.8.2
 **/
void
fu_fdt_image_set_attr(FuFdtImage *self, const gchar *key, GBytes *blob)
{
	FuFdtImagePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FDT_IMAGE(self));
	g_return_if_fail(key != NULL);
	g_hash_table_insert(priv->hash_attrs, g_strdup(key), g_bytes_ref(blob));
}

static void
fu_fdt_image_set_attr_format(FuFdtImage *self, const gchar *key, const gchar *format)
{
	FuFdtImagePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FDT_IMAGE(self));
	g_return_if_fail(format != NULL);
	g_hash_table_insert(priv->hash_attrs_format, g_strdup(key), strdup(format));
}

/**
 * fu_fdt_image_set_attr_uint32:
 * @self: a #FuFdtImage
 * @key: string, e.g. `creator`
 * @value: value to store
 *
 * Sets a uint32 attribute for the image.
 *
 * Since: 1.8.2
 **/
void
fu_fdt_image_set_attr_uint32(FuFdtImage *self, const gchar *key, guint32 value)
{
	guint8 buf[4] = {0x0};
	g_autoptr(GBytes) blob = NULL;

	g_return_if_fail(FU_IS_FDT_IMAGE(self));
	g_return_if_fail(key != NULL);

	fu_memwrite_uint32(buf, value, G_BIG_ENDIAN);
	blob = g_bytes_new(buf, sizeof(buf));
	fu_fdt_image_set_attr(self, key, blob);
	fu_fdt_image_set_attr_format(self, key, FU_FDT_IMAGE_FORMAT_UINT32);
}

/**
 * fu_fdt_image_set_attr_uint64:
 * @self: a #FuFdtImage
 * @key: string, e.g. `creator`
 * @value: value to store
 *
 * Sets a uint64 attribute for the image.
 *
 * Since: 1.8.2
 **/
void
fu_fdt_image_set_attr_uint64(FuFdtImage *self, const gchar *key, guint64 value)
{
	guint8 buf[8] = {0x0};
	g_autoptr(GBytes) blob = NULL;

	g_return_if_fail(FU_IS_FDT_IMAGE(self));
	g_return_if_fail(key != NULL);

	fu_memwrite_uint64(buf, value, G_BIG_ENDIAN);
	blob = g_bytes_new(buf, sizeof(buf));
	fu_fdt_image_set_attr(self, key, blob);
	fu_fdt_image_set_attr_format(self, key, FU_FDT_IMAGE_FORMAT_UINT64);
}

/**
 * fu_fdt_image_set_attr_str:
 * @self: a #FuFdtImage
 * @key: string, e.g. `creator`
 * @value: value to store
 *
 * Sets a string attribute for the image.
 *
 * Since: 1.8.2
 **/
void
fu_fdt_image_set_attr_str(FuFdtImage *self, const gchar *key, const gchar *value)
{
	g_autoptr(GBytes) blob = NULL;

	g_return_if_fail(FU_IS_FDT_IMAGE(self));
	g_return_if_fail(key != NULL);
	g_return_if_fail(value != NULL);

	blob = g_bytes_new((const guint8 *)value, strlen(value) + 1);
	fu_fdt_image_set_attr(self, key, blob);
	fu_fdt_image_set_attr_format(self, key, FU_FDT_IMAGE_FORMAT_STR);
}

/**
 * fu_fdt_image_set_attr_strlist:
 * @self: a #FuFdtImage
 * @key: string, e.g. `compatible`
 * @value: values to store
 *
 * Sets a stringlist attribute for the image.
 *
 * Since: 1.8.2
 **/
void
fu_fdt_image_set_attr_strlist(FuFdtImage *self, const gchar *key, gchar **value)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob = NULL;

	g_return_if_fail(FU_IS_FDT_IMAGE(self));
	g_return_if_fail(key != NULL);
	g_return_if_fail(value != NULL);
	g_return_if_fail(value[0] != NULL);

	for (guint i = 0; value[i] != NULL; i++) {
		g_byte_array_append(buf, (const guint8 *)value[i], strlen(value[i]));
		fu_byte_array_append_uint8(buf, 0x0);
	}
	blob = g_bytes_new(buf->data, buf->len);
	fu_fdt_image_set_attr(self, key, blob);
	fu_fdt_image_set_attr_format(self, key, FU_FDT_IMAGE_FORMAT_STRLIST);
}

static gboolean
fu_fdt_image_build_metadata_node(FuFdtImage *self, XbNode *n, GError **error)
{
	const gchar *key;
	const gchar *format;
	const gchar *value = xb_node_get_text(n);

	key = xb_node_get_attr(n, "key");
	if (key == NULL) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "key invalid");
		return FALSE;
	}
	format = xb_node_get_attr(n, "format");
	if (format == NULL) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "format unspecified for %s, expected uint64|uint32|str|strlist|data",
			    key);
		return FALSE;
	}
	fu_fdt_image_set_attr_format(self, key, format);

	/* actually parse values */
	if (g_strcmp0(format, FU_FDT_IMAGE_FORMAT_UINT32) == 0) {
		guint64 tmp = 0;
		if (value != NULL) {
			if (!fu_strtoull(value, &tmp, 0x0, G_MAXUINT32, error))
				return FALSE;
		}
		fu_fdt_image_set_attr_uint32(self, key, tmp);
		return TRUE;
	}
	if (g_strcmp0(format, FU_FDT_IMAGE_FORMAT_UINT64) == 0) {
		guint64 tmp = 0;
		if (value != NULL) {
			if (!fu_strtoull(value, &tmp, 0x0, G_MAXUINT64, error))
				return FALSE;
		}
		fu_fdt_image_set_attr_uint64(self, key, tmp);
		return TRUE;
	}
	if (g_strcmp0(format, FU_FDT_IMAGE_FORMAT_STR) == 0) {
		if (value != NULL) {
			fu_fdt_image_set_attr_str(self, key, value);
		} else {
			g_autoptr(GBytes) blob = g_bytes_new(NULL, 0);
			fu_fdt_image_set_attr(self, key, blob);
		}
		return TRUE;
	}
	if (g_strcmp0(format, FU_FDT_IMAGE_FORMAT_STRLIST) == 0) {
		if (value != NULL) {
			g_auto(GStrv) split = g_strsplit(value, ":", -1);
			fu_fdt_image_set_attr_strlist(self, key, split);
		} else {
			g_autoptr(GBytes) blob = g_bytes_new(NULL, 0);
			fu_fdt_image_set_attr(self, key, blob);
		}
		return TRUE;
	}
	if (g_strcmp0(format, FU_FDT_IMAGE_FORMAT_DATA) == 0) {
		g_autoptr(GBytes) blob = NULL;
		if (value != NULL) {
			gsize bufsz = 0;
			g_autofree guchar *buf = g_base64_decode(value, &bufsz);
			blob = g_bytes_new(buf, bufsz);
		} else {
			blob = g_bytes_new(NULL, 0);
		}
		fu_fdt_image_set_attr(self, key, blob);
		return TRUE;
	}

	/* failed */
	g_set_error(error,
		    G_IO_ERROR,
		    G_IO_ERROR_INVALID_DATA,
		    "format for %s invalid, expected uint64|uint32|str|strlist|data",
		    key);
	return FALSE;
}

static gboolean
fu_fdt_image_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuFdtImage *self = FU_FDT_IMAGE(firmware);
	g_autoptr(GPtrArray) metadata = NULL;

	metadata = xb_node_query(n, "metadata", 0, NULL);
	if (metadata != NULL) {
		for (guint i = 0; i < metadata->len; i++) {
			XbNode *c = g_ptr_array_index(metadata, i);
			if (!fu_fdt_image_build_metadata_node(self, c, error))
				return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static void
fu_fdt_image_init(FuFdtImage *self)
{
	FuFdtImagePrivate *priv = GET_PRIVATE(self);
	priv->hash_attrs =
	    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_bytes_unref);
	priv->hash_attrs_format = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
}

static void
fu_fdt_image_finalize(GObject *object)
{
	FuFdtImage *self = FU_FDT_IMAGE(object);
	FuFdtImagePrivate *priv = GET_PRIVATE(self);
	g_hash_table_unref(priv->hash_attrs);
	g_hash_table_unref(priv->hash_attrs_format);
	G_OBJECT_CLASS(fu_fdt_image_parent_class)->finalize(object);
}

static void
fu_fdt_image_class_init(FuFdtImageClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_fdt_image_finalize;
	klass_firmware->export = fu_fdt_image_export;
	klass_firmware->build = fu_fdt_image_build;
}

/**
 * fu_fdt_image_new:
 *
 * Creates a new #FuFirmware of sub type FDT image
 *
 * Since: 1.8.2
 **/
FuFirmware *
fu_fdt_image_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_FDT_IMAGE, NULL));
}
