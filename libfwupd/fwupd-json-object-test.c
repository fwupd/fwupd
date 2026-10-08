/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-json-node.h"
#include "fwupd-json-object.h"

static void
fwupd_json_object_roundtrip_func(void)
{
	gboolean bval = FALSE;
	gint64 ival = 0;
	const gchar *key0; /* transfer none */
	g_autoptr(FwupdJsonNode) json_node = NULL;
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) keys = NULL;
	g_autoptr(GPtrArray) nodes = NULL;

	/* empty to start with */
	g_assert_cmpint(fwupd_json_object_get_size(json_obj), ==, 0);
	g_assert_false(fwupd_json_object_has_node(json_obj, "Missing"));

	/* add some typed values */
	fwupd_json_object_add_string(json_obj, "Str", "value");
	fwupd_json_object_add_integer(json_obj, "Int", 1234);
	fwupd_json_object_add_boolean(json_obj, "Bool", TRUE);
	fwupd_json_object_add_raw(json_obj, "Raw", "rawvalue");
	g_assert_cmpint(fwupd_json_object_get_size(json_obj), ==, 4);
	g_assert_true(fwupd_json_object_has_node(json_obj, "Str"));

	/* read them back */
	g_assert_cmpstr(fwupd_json_object_get_string(json_obj, "Str", &error), ==, "value");
	g_assert_no_error(error);
	g_assert_true(fwupd_json_object_get_integer(json_obj, "Int", &ival, &error));
	g_assert_no_error(error);
	g_assert_cmpint(ival, ==, 1234);
	g_assert_true(fwupd_json_object_get_boolean(json_obj, "Bool", &bval, &error));
	g_assert_no_error(error);
	g_assert_true(bval);

	/* keys and nodes */
	keys = fwupd_json_object_get_keys(json_obj);
	g_assert_nonnull(keys);
	g_assert_cmpint(keys->len, ==, 4);
	nodes = fwupd_json_object_get_nodes(json_obj);
	g_assert_nonnull(nodes);
	g_assert_cmpint(nodes->len, ==, 4);

	/* index helpers */
	key0 = fwupd_json_object_get_key_for_index(json_obj, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(key0);
	json_node = fwupd_json_object_get_node_for_index(json_obj, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json_node);
}

static void
fwupd_json_object_default_func(void)
{
	gboolean bval = FALSE;
	gint64 ival = 0;
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(GError) error = NULL;

	/* missing keys return the supplied default */
	g_assert_cmpstr(
	    fwupd_json_object_get_string_with_default(json_obj, "Nope", "fallback", &error),
	    ==,
	    "fallback");
	g_assert_no_error(error);
	g_assert_true(
	    fwupd_json_object_get_integer_with_default(json_obj, "Nope", &ival, 42, &error));
	g_assert_no_error(error);
	g_assert_cmpint(ival, ==, 42);
	g_assert_true(
	    fwupd_json_object_get_boolean_with_default(json_obj, "Nope", &bval, TRUE, &error));
	g_assert_no_error(error);
	g_assert_true(bval);
}

static void
fwupd_json_object_invalid_func(void)
{
	gint64 ival = 0;
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(GError) error1 = NULL;
	g_autoptr(GError) error2 = NULL;

	/* missing key is an error */
	g_assert_null(fwupd_json_object_get_string(json_obj, "Missing", &error1));
	g_assert_nonnull(error1);

	/* reading past the end of the object is an error */
	g_assert_false(fwupd_json_object_get_integer(json_obj, "Missing", &ival, &error2));
	g_assert_nonnull(error2);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/json-object/roundtrip", fwupd_json_object_roundtrip_func);
	g_test_add_func("/fwupd/json-object/default", fwupd_json_object_default_func);
	g_test_add_func("/fwupd/json-object/invalid", fwupd_json_object_invalid_func);
	return g_test_run();
}
