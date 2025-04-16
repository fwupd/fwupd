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

static void
convert_check(GVariant *in_val, const GVariantType *type, GError **error)
{
	g_auto(GVariantBuilder) builder;
	g_autoptr(AParcel) parcel = AParcel_create();
	g_autoptr(GVariant) out_val = NULL;

	g_message("Converting \"%s\" to and from AParcel", g_variant_type_peek_string(type));

	g_message("  input variant %s", g_variant_print(in_val, TRUE));

	gp_parcel_write_variant(parcel, in_val, error);

	g_message("  parcel size is %d", AParcel_getDataSize(parcel));

	// Reset parcel cursor
	AParcel_setDataPosition(parcel, 0);

	g_variant_builder_init(&builder, type);
	gp_parcel_to_variant(&builder, parcel, type, error);
	out_val = g_variant_builder_end(&builder);

	g_message("  output variant %s", g_variant_print(out_val, TRUE));

	if (g_variant_equal(in_val, out_val)) {
		g_message("  input and output variants match");
	} else {
		g_warning("  input and output variants do not match");
	}
}

static void
test_1(void)
{
	const GVariantType *type = G_VARIANT_TYPE("mama{sv}");
	g_auto(GVariantBuilder) builder;
	g_autoptr(GVariant) in_val = NULL;
	g_autoptr(GError) error = NULL;

	g_variant_builder_init(&builder, type);

	in_val = g_variant_builder_end(&builder);

	convert_check(in_val, type, &error);

	if (error)
		g_warning("error converting \"%s\" error: %s",
			  g_variant_type_peek_string(type),
			  error->message);
}

static void
test_2(void)
{
	const GVariantType *type = G_VARIANT_TYPE("mama{sv}");
	g_auto(GVariantBuilder) builder;
	g_autoptr(GVariant) in_val = NULL;
	g_autoptr(GError) error = NULL;

	g_variant_builder_init(&builder, type);
	g_variant_builder_open(&builder, g_variant_type_element(type));
	g_variant_builder_close(&builder);

	in_val = g_variant_builder_end(&builder);

	convert_check(in_val, type, &error);

	if (error)
		g_warning("error converting \"%s\" error: %s",
			  g_variant_type_peek_string(type),
			  error->message);
}

static void
test_3(void)
{
	const GVariantType *type = G_VARIANT_TYPE("m(mama{sv}i)");
	g_auto(GVariantBuilder) builder;
	g_autoptr(GVariant) in_val = NULL;
	g_autoptr(GError) error = NULL;

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
}

static void
test_4(void)
{
	const GVariantType *type = G_VARIANT_TYPE("aa{sv}");
	g_auto(GVariantBuilder) builder;
	g_autoptr(GVariant) in_val = NULL;
	g_autoptr(GError) error = NULL;

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
}

// Array of vardicts
static void
test_5(void)
{
	const GVariantType *type = G_VARIANT_TYPE("aa{sv}");
	g_auto(GVariantBuilder) builder;
	g_auto(GVariantBuilder) strv_builder;
	g_autoptr(GVariant) strv_val = NULL;
	g_autoptr(GVariant) in_val = NULL;
	g_autoptr(GError) error = NULL;

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
}

// nested vardict
static void
test_6(void)
{
	const GVariantType *type = G_VARIANT_TYPE("aa{sv}");
	g_auto(GVariantBuilder) builder;
	g_auto(GVariantBuilder) strv_builder;
	g_auto(GVariantDict) vardict;
	g_autoptr(GVariant) vardict_val = NULL;
	g_autoptr(GVariant) vardict_val_2 = NULL;
	g_autoptr(GVariant) in_val = NULL;
	g_autoptr(GError) error = NULL;

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
}

static void
test_7(void)
{
	const GVariantType *type = G_VARIANT_TYPE("(ss)");
	g_auto(GVariantBuilder) builder;
	g_auto(GVariantBuilder) strv_builder;
	g_auto(GVariantDict) vardict;
	g_autoptr(GVariant) vardict_val = NULL;
	g_autoptr(GVariant) vardict_val_2 = NULL;
	g_autoptr(GVariant) in_val = NULL;
	g_autoptr(GError) error = NULL;

	g_variant_builder_init(&builder, type);

	g_variant_builder_add_value(&builder, g_variant_new_string("hello world"));
	g_variant_builder_add_value(&builder, g_variant_new_string("goodbye world"));

	in_val = g_variant_builder_end(&builder);

	convert_check(in_val, type, &error);

	if (error)
		g_warning("error converting \"%s\" error: %s",
			  g_variant_type_peek_string(type),
			  error->message);
}

int
main(void)
{
	test_1();
	test_2();
	test_3();
	test_4();
	test_5();
	test_6();
	test_7();

	return EXIT_SUCCESS;
}
