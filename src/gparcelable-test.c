/*
 * Copyright 2024 Colin Kinloch <colin.kinloch@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "GParcelableTests"

#include "config.h"

#include <glib.h>
#if HAS_BINDER_NDK
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <android/persistable_bundle.h>
#endif /* HAS_BINDER_NDK */

#include "gparcelable.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC(AStatus, AStatus_delete)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(AParcel, AParcel_delete)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(APersistableBundle, APersistableBundle_delete)

// TODO: more tests

static gboolean
variant_loose_equal(GVariant *a, GVariant *b);

static gboolean
variant_loose_equal_iterable(GVariant *a, GVariant *b)
{
	GVariantIter iter_a;
	GVariantIter iter_b;

	g_variant_iter_init(&iter_a, a);
	g_variant_iter_init(&iter_b, b);
	do {
		g_autoptr(GVariant) child_a = g_variant_iter_next_value(&iter_a);
		g_autoptr(GVariant) child_b = g_variant_iter_next_value(&iter_b);

		// Both arrays have reached the end at the same time (equal)
		if (child_a == NULL && child_b == NULL)
			return TRUE;

		// If one arrays is shorter than the other (not equal)
		if (!variant_loose_equal(child_a, child_b)) {
			return FALSE;
		}
	} while (TRUE);
}

/* check if variants are equal apart from signedness of values and order of vardicts */
static gboolean
variant_loose_equal(GVariant *a, GVariant *b)
{
	GVariantClass vclass = g_variant_classify(a);
	const GVariantType *type = g_variant_get_type(a);
	const GVariantType *element_type = NULL;
	GVariant *prop_value_a;
	const gchar *prop_key;
	GVariantIter iter_a;
	GVariantIter iter_b;

	g_debug("loose_eq type: %s", g_variant_type_peek_string(type));

	// TODO: Should identical pointers / NULL be an error (maybe)
	if (a == b) {
		return TRUE;
	}

	if (a == NULL || b == NULL) {
		return FALSE;
	}

	if (!g_variant_is_of_type(b, type))
		return FALSE;

	// This rules out most things
	if (g_variant_equal(a, b)) {
		return TRUE;
	}

	switch (vclass) {
	case G_VARIANT_CLASS_MAYBE: {
		g_autoptr(GVariant) child_a = g_variant_get_maybe(a);
		g_autoptr(GVariant) child_b = g_variant_get_maybe(b);
		return variant_loose_equal(child_a, child_b);
	} break;
	case G_VARIANT_CLASS_ARRAY: {
		element_type = g_variant_type_element(type);
		switch (g_variant_type_peek_string(element_type)[0]) {
		case G_VARIANT_CLASS_DICT_ENTRY: {
			g_return_val_if_fail(g_variant_type_equal(g_variant_type_key(element_type),
								  G_VARIANT_TYPE_STRING),
					     FALSE);
			g_return_val_if_fail(
			    g_variant_type_equal(g_variant_type_value(element_type),
						 G_VARIANT_TYPE_VARIANT),
			    FALSE);
			g_variant_iter_init(&iter_a, a);
			while (g_variant_iter_next(&iter_a, "{&sv}", &prop_key, &prop_value_a)) {
				g_autoptr(GVariant) prop_value_b =
				    g_variant_lookup_value(b,
							   prop_key,
							   g_variant_get_type(prop_value_a));
				if (!variant_loose_equal(prop_value_a, prop_value_b)) {
					return FALSE;
				}
			}
			return TRUE;
		} break;
		default:
			return variant_loose_equal_iterable(a, b);
		}
	} break;
	case G_VARIANT_CLASS_TUPLE:
		return variant_loose_equal_iterable(a, b);
		break;
	}

	return FALSE;
}

static gboolean
convert_check(GVariant *in_val, const GVariantType *type, GError **error)
{
	g_auto(GVariantBuilder) builder;
	g_autoptr(AParcel) parcel = AParcel_create();
	g_autoptr(GVariant) out_val = NULL;

	g_message("Converting \"%s\" to and from AParcel", g_variant_type_peek_string(type));

	if (in_val) {
		g_message("  input variant %s", g_variant_print(in_val, TRUE));
	} else {
		g_warning("  input variant null");
		return FALSE;
	}

	g_message("Converting to parcel");
	if ((gp_parcel_write_variant(parcel, in_val, error) != STATUS_OK) || *error) {
		return FALSE;
	}

	g_message("  parcel size is %d", AParcel_getDataSize(parcel));

	// Reset parcel cursor
	AParcel_setDataPosition(parcel, 0);

	g_message("Converting from parcel");
	g_debug(" - gvb_init %s", g_variant_type_peek_string(type));
	g_variant_builder_init(&builder, type);
	if (!gp_parcel_to_variant(&builder, parcel, type, error)) {
		return FALSE;
	}
	g_debug(" - gvb_end");
	out_val = g_variant_builder_end(&builder);

	g_message(" - Finished - ");

	if (in_val) {
		g_message("  end input variant %s", g_variant_print(in_val, TRUE));
	}
	if (out_val) {
		g_message("  output variant %s", g_variant_print(out_val, TRUE));
	} else {
		g_warning("  output variant null");
		return FALSE;
	}

	if (variant_loose_equal(in_val, out_val)) {
		g_message("  input and output variants loose match 🟩");
	} else {
		g_warning("  input and output variants do not loose match 🟥");
	}

	if (g_variant_equal(in_val, out_val)) {
		g_message("  input and output variants match 🟩");
	} else {
		// This is unavoidable as Bundles store values in c++ std::maps which are sorted by
		// key on insertion
		g_message("  input and output variants do not match 🟥");
	}

	return TRUE;
}

static gboolean
test_1(void)
{
	const GVariantType *type = G_VARIANT_TYPE("mama{sv}");
	g_auto(GVariantBuilder) builder;
	g_autoptr(GVariant) in_val = NULL;
	g_autoptr(GError) error = NULL;

	g_message("- - - start test %s", g_variant_type_peek_string(type));

	g_variant_builder_init(&builder, type);

	in_val = g_variant_builder_end(&builder);

	convert_check(in_val, type, &error);

	if (error)
		g_warning("error converting \"%s\" error: %s",
			  g_variant_type_peek_string(type),
			  error->message);

	g_message("- - - end test %s", g_variant_type_peek_string(type));

	return TRUE;
}

static gboolean
test_2(void)
{
	const GVariantType *type = G_VARIANT_TYPE("mama{sv}");
	g_auto(GVariantBuilder) builder;
	g_autoptr(GVariant) in_val = NULL;
	g_autoptr(GError) error = NULL;

	g_message("- - - start test %s", g_variant_type_peek_string(type));

	g_variant_builder_init(&builder, type);
	g_variant_builder_open(&builder, g_variant_type_element(type));
	g_variant_builder_close(&builder);

	in_val = g_variant_builder_end(&builder);

	convert_check(in_val, type, &error);

	if (error)
		g_warning("error converting \"%s\" error: %s",
			  g_variant_type_peek_string(type),
			  error->message);

	g_message("- - - end test %s", g_variant_type_peek_string(type));

	return TRUE;
}

static gboolean
test_3(void)
{
	const GVariantType *type = G_VARIANT_TYPE("m(mama{sv}i)");
	g_auto(GVariantBuilder) builder;
	g_autoptr(GVariant) in_val = NULL;
	g_autoptr(GError) error = NULL;

	g_message("- - - start test %s", g_variant_type_peek_string(type));

	g_variant_builder_init(&builder, type);

	g_variant_builder_open(&builder, g_variant_type_element(type));
	g_variant_builder_add_value(&builder, g_variant_new_maybe(G_VARIANT_TYPE("ama{sv}"), NULL));
	g_variant_builder_add_value(&builder, g_variant_new_int32(42));
	g_variant_builder_close(&builder);

	in_val = g_variant_builder_end(&builder);

	convert_check(in_val, type, &error);

	if (error)
		g_warning("error converting \"%s\" error: %s",
			  g_variant_type_peek_string(type),
			  error->message);

	g_message("- - - end test %s", g_variant_type_peek_string(type));

	return TRUE;
}

static gboolean
test_4(void)
{
	const GVariantType *type = G_VARIANT_TYPE("aa{sv}");
	g_auto(GVariantBuilder) builder;
	g_autoptr(GVariant) in_val = NULL;
	g_autoptr(GError) error = NULL;

	g_message("- - - start test %s", g_variant_type_peek_string(type));

	g_variant_builder_init(&builder, type);

	g_variant_builder_open(&builder, g_variant_type_element(type));
	g_variant_builder_add(&builder, "{&sv}", "key", g_variant_new_string("value"));
	g_variant_builder_add(&builder, "{&sv}", "key2", g_variant_new_string("value2"));
	g_variant_builder_close(&builder);

	in_val = g_variant_builder_end(&builder);

	convert_check(in_val, type, &error);

	if (error)
		g_warning("error converting \"%s\" error: %s",
			  g_variant_type_peek_string(type),
			  error->message);

	g_message("- - - end test %s", g_variant_type_peek_string(type));

	return TRUE;
}

// Array of vardicts
static gboolean
test_5(void)
{
	const GVariantType *type = G_VARIANT_TYPE("aa{sv}");
	g_auto(GVariantBuilder) builder;
	g_auto(GVariantBuilder) strv_builder;
	g_autoptr(GVariant) strv_val = NULL;
	g_autoptr(GVariant) in_val = NULL;
	g_autoptr(GError) error = NULL;

	g_message("- - - start test %s", g_variant_type_peek_string(type));

	g_variant_builder_init(&builder, type);

	g_variant_builder_open(&builder, g_variant_type_element(type));
	g_variant_builder_init(&strv_builder, G_VARIANT_TYPE_STRING_ARRAY);
	g_variant_builder_add(&strv_builder, "s", "string one");
	g_variant_builder_add(&strv_builder, "s", "string two");
	g_variant_builder_add(&strv_builder, "s", "string three");
	strv_val = g_variant_builder_end(&strv_builder);
	g_variant_builder_add(&builder, "{&sv}", "strings list one", g_variant_ref(strv_val));
	g_variant_builder_add(&builder, "{&sv}", "strings list two", strv_val);
	g_variant_builder_close(&builder);

	in_val = g_variant_builder_end(&builder);

	convert_check(in_val, type, &error);

	if (error)
		g_warning("error converting \"%s\" error: %s",
			  g_variant_type_peek_string(type),
			  error->message);

	g_message("- - - end test %s", g_variant_type_peek_string(type));

	return TRUE;
}

// nested vardict
static gboolean
test_6(void)
{
	const GVariantType *type = G_VARIANT_TYPE("aa{sv}");
	g_auto(GVariantBuilder) builder;
	g_auto(GVariantDict) vardict;
	g_autoptr(GVariant) vardict_val = NULL;
	g_autoptr(GVariant) vardict_val_2 = NULL;
	g_autoptr(GVariant) in_val = NULL;
	g_autoptr(GError) error = NULL;

	g_message("- - - start test %s", g_variant_type_peek_string(type));

	g_variant_builder_init(&builder, type);

	g_variant_builder_open(&builder, g_variant_type_element(type));
	g_variant_dict_init(&vardict, NULL);
	g_variant_dict_insert_value(&vardict, "value 1", g_variant_new_string("hello"));
	g_variant_dict_insert_value(&vardict, "value 2", g_variant_new_string("hello"));
	vardict_val = g_variant_dict_end(&vardict);

	g_variant_dict_init(&vardict, NULL);
	g_variant_dict_insert_value(&vardict, "sub_vardict 1", g_variant_ref(vardict_val));
	g_variant_dict_insert_value(&vardict, "sub_vardict 2", g_variant_ref(vardict_val));
	vardict_val_2 = g_variant_dict_end(&vardict);

	g_variant_builder_add(&builder, "{&sv}", "vardict one", g_variant_ref(vardict_val));
	g_variant_builder_add(&builder, "{&sv}", "vardict two", vardict_val);
	g_variant_builder_add(&builder, "{&sv}", "vardict three", g_variant_ref(vardict_val_2));

	g_variant_builder_close(&builder);

	in_val = g_variant_builder_end(&builder);

	convert_check(in_val, type, &error);

	if (error)
		g_warning("error converting \"%s\" error: %s",
			  g_variant_type_peek_string(type),
			  error->message);

	g_message("- - - end test %s", g_variant_type_peek_string(type));

	return TRUE;
}

static gboolean
test_7(void)
{
	const GVariantType *type = G_VARIANT_TYPE("(ss)");
	g_auto(GVariantBuilder) builder;
	g_autoptr(GVariant) in_val = NULL;
	g_autoptr(GError) error = NULL;

	g_message("- - - start test %s", g_variant_type_peek_string(type));

	g_variant_builder_init(&builder, type);

	g_variant_builder_add_value(&builder, g_variant_new_string("hello world"));
	g_variant_builder_add_value(&builder, g_variant_new_string("goodbye world"));

	in_val = g_variant_builder_end(&builder);

	convert_check(in_val, type, &error);

	if (error)
		g_warning("error converting \"%s\" error: %s",
			  g_variant_type_peek_string(type),
			  error->message);

	g_message("- - - end test %s", g_variant_type_peek_string(type));

	return TRUE;
}

static gboolean
test_8(void)
{
	const GVariantType *type = G_VARIANT_TYPE("(ia{sv})");
	g_auto(GVariantBuilder) builder;
	g_autoptr(GVariant) in_val = NULL;
	g_autoptr(GError) error = NULL;

	g_message("- - - start test %s", g_variant_type_peek_string(type));

	g_variant_builder_init(&builder, type);

	g_variant_builder_add_value(&builder, g_variant_new_int32(42));
	g_variant_builder_open(&builder, G_VARIANT_TYPE("a{sv}"));

	g_variant_builder_close(&builder);

	in_val = g_variant_builder_end(&builder);

	convert_check(in_val, type, &error);

	if (error)
		g_warning("error converting \"%s\" error: %s",
			  g_variant_type_peek_string(type),
			  error->message);

	g_message("- - - end test %s", g_variant_type_peek_string(type));

	return TRUE;
}

static gboolean
test_9(void)
{
	const GVariantType *type = G_VARIANT_TYPE("maa{sv}");
	GVariantType *ctype = NULL;
	g_auto(GVariantBuilder) builder;
	g_auto(GVariantDict) vardict;
	g_autoptr(GVariant) vardict_val = NULL;
	g_autoptr(GVariant) vardict_val_2 = NULL;
	g_autoptr(GVariant) in_val = NULL;
	g_autoptr(GError) error = NULL;

	g_message("- - - start test %s", g_variant_type_peek_string(type));

	// maybe m
	g_variant_builder_init(&builder, type);
	ctype = type;

	// array a
	ctype = g_variant_type_element(ctype);
	g_message("open %s", g_variant_type_peek_string(ctype));
	g_variant_builder_open(&builder, ctype);

	g_variant_dict_init(&vardict, NULL);
	g_variant_dict_insert_value(&vardict, "value 1", g_variant_new_string("hello"));
	g_variant_dict_insert_value(&vardict, "value 2", g_variant_new_string("hello"));
	vardict_val = g_variant_dict_end(&vardict);

	g_variant_dict_init(&vardict, NULL);
	g_variant_dict_insert_value(&vardict, "sub_vardict 1", g_variant_ref(vardict_val));
	g_variant_dict_insert_value(&vardict, "sub_vardict 2", g_variant_ref(vardict_val));
	vardict_val_2 = g_variant_dict_end(&vardict);

	// add vardicts a{sv}
	g_variant_builder_add_value(&builder, g_variant_ref(vardict_val));
	g_variant_builder_add_value(&builder, vardict_val);
	g_variant_builder_add_value(&builder, g_variant_ref(vardict_val_2));

	// close array
	g_variant_builder_close(&builder);

	// close maybe
	// g_variant_builder_close(&builder);

	// end tuple
	in_val = g_variant_builder_end(&builder);

	convert_check(in_val, type, &error);

	if (error)
		g_warning("error converting \"%s\" error: %s",
			  g_variant_type_peek_string(type),
			  error->message);

	g_message("- - - end test %s", g_variant_type_peek_string(type));

	return TRUE;
}

static gboolean
test_10(void)
{
	const GVariantType *type = G_VARIANT_TYPE("(maa{sv}i)");
	GVariantType *ctype = NULL;
	g_auto(GVariantBuilder) builder;
	g_auto(GVariantDict) vardict;
	g_autoptr(GVariant) vardict_val = NULL;
	g_autoptr(GVariant) vardict_val_2 = NULL;
	g_autoptr(GVariant) in_val = NULL;
	g_autoptr(GError) error = NULL;

	g_message("- - - start test %s", g_variant_type_peek_string(type));

	// tuple (
	g_variant_builder_init(&builder, type);
	ctype = type;

	// maybe m
	ctype = g_variant_type_first(type);
	// ctype = g_variant_type_element(ctype);
	g_message("open %s", g_variant_type_peek_string(ctype));
	g_variant_builder_open(&builder, ctype);

	// array a
	ctype = g_variant_type_element(ctype);
	g_message("open %s", g_variant_type_peek_string(ctype));
	g_variant_builder_open(&builder, ctype);

	g_variant_dict_init(&vardict, NULL);
	g_variant_dict_insert_value(&vardict, "value 1", g_variant_new_string("hello"));
	g_variant_dict_insert_value(&vardict, "value 2", g_variant_new_string("hello"));
	vardict_val = g_variant_dict_end(&vardict);

	g_variant_dict_init(&vardict, NULL);
	g_variant_dict_insert_value(&vardict, "sub_vardict 1", g_variant_ref(vardict_val));
	g_variant_dict_insert_value(&vardict, "sub_vardict 2", g_variant_ref(vardict_val));
	vardict_val_2 = g_variant_dict_end(&vardict);

	// add vardicts a{sv}
	g_variant_builder_add_value(&builder, g_variant_ref(vardict_val));
	g_variant_builder_add_value(&builder, vardict_val);
	g_variant_builder_add_value(&builder, g_variant_ref(vardict_val_2));

	// close array
	g_variant_builder_close(&builder);

	// close maybe
	g_variant_builder_close(&builder);

	g_variant_builder_add(&builder, "i", 42);

	// end tuple
	in_val = g_variant_builder_end(&builder);

	convert_check(in_val, type, &error);

	if (error)
		g_warning("error converting \"%s\" error: %s",
			  g_variant_type_peek_string(type),
			  error->message);

	g_message("- - - end test %s", g_variant_type_peek_string(type));

	return TRUE;
}

/* empty array */
static gboolean
test_11(void)
{
	const GVariantType *type = G_VARIANT_TYPE("aa{sv}");
	GVariantType *ctype = NULL;
	g_auto(GVariantBuilder) builder;
	g_auto(GVariantDict) vardict;
	g_autoptr(GVariant) vardict_val = NULL;
	g_autoptr(GVariant) vardict_val_2 = NULL;
	g_autoptr(GVariant) in_val = NULL;
	g_autoptr(GError) error = NULL;

	g_message("- - - start test %s", g_variant_type_peek_string(type));

	// array a
	g_variant_builder_init(&builder, type);
	ctype = type;

	// end array
	in_val = g_variant_builder_end(&builder);

	convert_check(in_val, type, &error);

	if (error)
		g_warning("error converting \"%s\" error: %s",
			  g_variant_type_peek_string(type),
			  error->message);

	g_message("- - - end test %s", g_variant_type_peek_string(type));

	return TRUE;
}

/* empty array */
static gboolean
test_12(void)
{
	const GVariantType *type = G_VARIANT_TYPE("maa{sv}");
	GVariantType *ctype = NULL;
	g_auto(GVariantBuilder) builder;
	g_auto(GVariantDict) vardict;
	g_autoptr(GVariant) vardict_val = NULL;
	g_autoptr(GVariant) vardict_val_2 = NULL;
	g_autoptr(GVariant) in_val = NULL;
	g_autoptr(GError) error = NULL;

	g_message("- - - start test %s", g_variant_type_peek_string(type));

	// maybe m
	g_variant_builder_init(&builder, type);
	ctype = type;

	// array a
	ctype = g_variant_type_element(ctype);
	g_variant_builder_add(&builder, ctype, NULL);

	// end maybe
	in_val = g_variant_builder_end(&builder);

	convert_check(in_val, type, &error);

	if (error)
		g_warning("error converting \"%s\" error: %s",
			  g_variant_type_peek_string(type),
			  error->message);

	g_message("- - - end test %s", g_variant_type_peek_string(type));

	return TRUE;
}

/* empty array */
static gboolean
test_13(void)
{
	const GVariantType *type = G_VARIANT_TYPE("(maa{sv})");
	GVariantType *ctype = NULL;
	g_auto(GVariantBuilder) builder;
	g_auto(GVariantDict) vardict;
	g_autoptr(GVariant) vardict_val = NULL;
	g_autoptr(GVariant) vardict_val_2 = NULL;
	g_autoptr(GVariant) in_val = NULL;
	g_autoptr(GError) error = NULL;

	g_message("- - - start test %s", g_variant_type_peek_string(type));

	// init tuple (
	g_variant_builder_init(&builder, type);
	ctype = type;

	// open maybe m
	ctype = g_variant_type_first(ctype);
	g_variant_builder_open(&builder, ctype);

	// array a
	ctype = g_variant_type_element(ctype);
	g_warning("%s", g_variant_type_peek_string(ctype));
	g_variant_builder_open(&builder, ctype);
	g_variant_builder_close(&builder);

	// close maybe m
	g_variant_builder_close(&builder);

	// end tuple
	in_val = g_variant_builder_end(&builder);

	convert_check(in_val, type, &error);

	if (error)
		g_warning("error converting \"%s\" error: %s",
			  g_variant_type_peek_string(type),
			  error->message);

	g_message("- - - end test %s", g_variant_type_peek_string(type));

	return TRUE;
}

static gboolean
test_14(void)
{
	const GVariantType *type = G_VARIANT_TYPE("(i(maa{sv})i)");
	GVariantType *ctype = NULL;
	GVariantType *outer_type_iter = NULL;
	GVariantType *inner_type_iter = NULL;
	g_auto(GVariantBuilder) builder;
	g_auto(GVariantDict) vardict;
	g_autoptr(GVariant) vardict_val = NULL;
	g_autoptr(GVariant) vardict_val_2 = NULL;
	g_autoptr(GVariant) in_val = NULL;
	g_autoptr(GError) error = NULL;

	g_message("- - - start test %s", g_variant_type_peek_string(type));

	// init tuple (
	g_variant_builder_init(&builder, type);

	// add i
	outer_type_iter = g_variant_type_first(type);
	g_variant_builder_add(&builder, G_VARIANT_TYPE_INT32, 42);

	g_warning("open tuple");
	// open tuple (
	outer_type_iter = g_variant_type_next(outer_type_iter);
	g_variant_builder_open(&builder, outer_type_iter);

	g_warning("open maybe");
	// open maybe m
	inner_type_iter = g_variant_type_first(outer_type_iter);
	g_warning("open maybe %s", g_variant_type_peek_string(inner_type_iter));
	g_variant_builder_open(&builder, inner_type_iter);

	g_warning("open array");
	// array a
	ctype = g_variant_type_element(inner_type_iter);
	g_warning("%s", g_variant_type_peek_string(ctype));
	g_variant_builder_open(&builder, ctype);
	g_variant_builder_close(&builder);

	// close maybe m
	g_variant_builder_close(&builder);

	// close tuple )
	g_variant_builder_close(&builder);

	// add i
	outer_type_iter = g_variant_type_next(outer_type_iter);
	g_variant_builder_add(&builder, G_VARIANT_TYPE_INT32, 42);

	// end tuple
	in_val = g_variant_builder_end(&builder);

	convert_check(in_val, type, &error);

	if (error)
		g_warning("error converting \"%s\" error: %s",
			  g_variant_type_peek_string(type),
			  error->message);

	g_message("- - - end test %s", g_variant_type_peek_string(type));

	return TRUE;
}

static gboolean
test_15(void)
{
	const GVariantType *type = G_VARIANT_TYPE("(aa{sv})");
	g_auto(GVariantBuilder) builder;
	GVariantType *ctype = NULL;
	g_autoptr(GVariant) in_val = NULL;
	g_autoptr(GError) error = NULL;

	g_message("- - - start test %s", g_variant_type_peek_string(type));

	g_debug(" - gvb_init %s", g_variant_type_peek_string(type));
	g_variant_builder_init(&builder, type);

	// open array
	ctype = g_variant_type_first(type);
	g_debug(" - gvb_open %s", g_variant_type_peek_string(ctype));
	g_variant_builder_open(&builder, ctype);

	// open vardict
	ctype = g_variant_type_element(ctype);
	g_debug(" - gvb_open %s", g_variant_type_peek_string(ctype));
	g_variant_builder_open(&builder, ctype);
	g_debug(" - gvb_add {&sv}");
	g_variant_builder_add(&builder, "{&sv}", "key", g_variant_new_string("value"));
	g_debug(" - gvb_add {&sv}");
	g_variant_builder_add(&builder, "{&sv}", "key2", g_variant_new_string("value2"));
	// close vardict
	g_debug(" - gvb_close");
	g_variant_builder_close(&builder);

	// close array
	g_debug(" - gvb_close");
	g_variant_builder_close(&builder);

	g_debug(" - gvb_end");
	in_val = g_variant_builder_end(&builder);

	convert_check(in_val, type, &error);

	if (error)
		g_warning("error converting \"%s\" error: %s",
			  g_variant_type_peek_string(type),
			  error->message);

	g_message("- - - end test %s", g_variant_type_peek_string(type));

	return TRUE;
}

typedef gboolean (*TestFunc)(void);

int
main(void)
{
	static const TestFunc tests[] = {
	    test_1,
	    test_2,
	    test_3,
	    test_4,
	    test_5,
	    test_6,
	    test_7,
	    test_8,
	    test_9,
	    test_10,
	    test_11,
	    test_12,
	    test_13,
	    test_14,
	    test_15,
	};
	for (guint i = 0; i < G_N_ELEMENTS(tests); i++) {
		g_warning("test_%d", i + 1);
		if (!(tests[i] && tests[i]())) {
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}
