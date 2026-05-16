/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-variant.h"

/**
 * fwupd_variant_from_hash_kv: (skip):
 **/
GVariant *
fwupd_variant_from_hash_kv(GHashTable *hash)
{
	GVariantBuilder builder;
	g_autoptr(GList) keys = g_hash_table_get_keys(hash);
	g_variant_builder_init(&builder, G_VARIANT_TYPE("a{ss}"));
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		const gchar *value = g_hash_table_lookup(hash, key);
		g_variant_builder_add(&builder, "{ss}", key, value);
	}
	return g_variant_builder_end(&builder);
}

/**
 * fwupd_variant_to_hash_kv: (skip):
 **/
GHashTable *
fwupd_variant_to_hash_kv(GVariant *dict)
{
	GHashTable *hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	GVariantIter iter;
	const gchar *key;
	const gchar *value;
	g_variant_iter_init(&iter, dict);
	while (g_variant_iter_loop(&iter, "{&s&s}", &key, &value))
		g_hash_table_insert(hash, g_strdup(key), g_strdup(value));
	return hash;
}

/**
 * fwupd_variant_get_uint32:
 * @value: a #GVariant
 *
 * Gets an unsigned integer from a variant, handling both 'u' and 'i' types.
 *
 * Returns: integer
 *
 * Since: 2.1.4
 **/
guint32
fwupd_variant_get_uint32(GVariant *value)
{
	g_return_val_if_fail(value != NULL, 0);
	if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT32))
		return (guint32)g_variant_get_int32(value); /* nocheck:blocked */
	return g_variant_get_uint32(value);
}

/**
 * fwupd_variant_get_uint64:
 * @value: a #GVariant
 *
 * Gets an unsigned integer from a variant, handling both 't' and 'x' types.
 *
 * Returns: integer
 *
 * Since: 2.1.4
 **/
guint64
fwupd_variant_get_uint64(GVariant *value)
{
	g_return_val_if_fail(value != NULL, 0);
	if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT64))
		return (guint64)g_variant_get_int64(value); /* nocheck:blocked */
	return g_variant_get_uint64(value);
}
