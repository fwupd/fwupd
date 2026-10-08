/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-variant.h"

static void
fwupd_variant_getters_func(void)
{
	const gchar *strv_in[] = {"one", "two", NULL};
	g_autoptr(GVariant) v_bool = g_variant_ref_sink(g_variant_new_boolean(TRUE));
	g_autoptr(GVariant) v_double = g_variant_ref_sink(g_variant_new_double(1.5));
	g_autoptr(GVariant) v_i32 = g_variant_ref_sink(g_variant_new_int32(-5));
	g_autoptr(GVariant) v_i64 = g_variant_ref_sink(g_variant_new_int64(-7));
	g_autoptr(GVariant) v_opath = g_variant_ref_sink(g_variant_new_object_path("/foo"));
	g_autoptr(GVariant) v_sig = g_variant_ref_sink(g_variant_new_signature("s"));
	g_autoptr(GVariant) v_str = g_variant_ref_sink(g_variant_new_string("hello"));
	g_autoptr(GVariant) v_strv = g_variant_ref_sink(g_variant_new_strv(strv_in, -1));
	g_autoptr(GVariant) v_u32 = g_variant_ref_sink(g_variant_new_uint32(5));
	g_autoptr(GVariant) v_u64 = g_variant_ref_sink(g_variant_new_uint64(7));
	g_autofree const gchar **strv_out = NULL;

	/* uint32 accepts int32 (clamped to >= 0) and uint32 */
	g_assert_cmpint(fwupd_variant_get_uint32(v_u32), ==, 5);
	g_assert_cmpint(fwupd_variant_get_uint32(v_i32), ==, 0);
	g_assert_cmpint(fwupd_variant_get_uint32(v_str), ==, 0);

	/* int32 */
	g_assert_cmpint(fwupd_variant_get_int32(v_i32), ==, -5);
	g_assert_cmpint(fwupd_variant_get_int32(v_str), ==, 0);

	/* uint64 accepts int64 (clamped) and uint64 */
	g_assert_cmpint(fwupd_variant_get_uint64(v_u64), ==, 7);
	g_assert_cmpint(fwupd_variant_get_uint64(v_i64), ==, 0);
	g_assert_cmpint(fwupd_variant_get_uint64(v_str), ==, 0);

	/* strings, object paths and signatures are all stringy */
	g_assert_cmpstr(fwupd_variant_get_string(v_str), ==, "hello");
	g_assert_cmpstr(fwupd_variant_get_string(v_opath), ==, "/foo");
	g_assert_cmpstr(fwupd_variant_get_string(v_sig), ==, "s");
	g_assert_null(fwupd_variant_get_string(v_bool));

	/* boolean */
	g_assert_true(fwupd_variant_get_boolean(v_bool));
	g_assert_false(fwupd_variant_get_boolean(v_str));

	/* double */
	g_assert_true(ABS(fwupd_variant_get_double(v_double) - 1.5) < 0.0001);
	g_assert_true(ABS(fwupd_variant_get_double(v_str) - (-1.0)) < 0.0001);

	/* string array */
	strv_out = fwupd_variant_get_strv(v_strv);
	g_assert_nonnull(strv_out);
	g_assert_cmpstr(strv_out[0], ==, "one");
	g_assert_null(fwupd_variant_get_strv(v_str));
}

static void
fwupd_variant_hash_kv_func(void)
{
	g_autoptr(GHashTable) hash = g_hash_table_new(g_str_hash, g_str_equal);
	g_autoptr(GHashTable) hash2 = NULL;
	g_autoptr(GVariant) dict = NULL;
	g_autoptr(GVariant) notdict = g_variant_ref_sink(g_variant_new_boolean(TRUE));

	g_hash_table_insert(hash, (gpointer) "Key", (gpointer) "Value");
	dict = g_variant_ref_sink(fwupd_variant_from_hash_kv(hash));
	g_assert_nonnull(dict);

	/* round-trips back to a hash table */
	hash2 = fwupd_variant_to_hash_kv(dict);
	g_assert_nonnull(hash2);
	g_assert_cmpstr(g_hash_table_lookup(hash2, "Key"), ==, "Value");

	/* the wrong variant type cannot be converted */
	g_assert_null(fwupd_variant_to_hash_kv(notdict));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/variant/getters", fwupd_variant_getters_func);
	g_test_add_func("/fwupd/variant/hash-kv", fwupd_variant_hash_kv_func);
	return g_test_run();
}
