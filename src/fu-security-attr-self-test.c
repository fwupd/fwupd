/*
 * Copyright (C) 2021 Kate Hsuan <hpa@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <glib-object.h>
#include <glib/gstdio.h>
#include <libgcab.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <xmlb.h>

#include "fwupd-security-attr.h"

#include "fu-history.h"
#include "fu-security-attr.h"

gchar *standard_str =
    "{\"SecurityAttributes\":{\"org.fwupd.hsi.test.string0001\":{\"AppstreamId\":\"org."
    "fwupd.hsi.test.string0001\",\"HsiLevel\":0,\"HsiResult\":\"not-supported\",\"Name\":"
    "\"test1\",\"Plugin\":\"test1_plugin\",\"Uri\":\"https://"
    "test1\"},\"org.fwupd.hsi.test.string0002\":{\"AppstreamId\":\"org.fwupd.hsi.test."
    "string0002\",\"HsiLevel\":0,\"HsiResult\":\"not-supported\",\"Name\":\"test2\","
    "\"Plugin\":\"test2_plugin\",\"Uri\":\"https://"
    "test2\",\"Flags\":[\"obsoleted\"]},\"org.fwupd.hsi.test.string0003\":{\"AppstreamId\":"
    "\"org.fwupd.hsi.test.string0003\",\"HsiLevel\":0,\"HsiResult\":\"not-supported\","
    "\"Name\":\"test3\",\"Plugin\":\"test1_plugin\",\"Uri\":\"https://"
    "test3\",\"Guid\":[\"ea9b78bf-8830-47a4-8c72-6093c08b4f88\"]},\"org.fwupd.hsi.test."
    "string0004\":{\"AppstreamId\":\"org.fwupd.hsi.test.string0004\",\"HsiLevel\":0,"
    "\"HsiResult\":\"not-supported\",\"Name\":\"test4\",\"Plugin\":\"test4_plugin\","
    "\"Uri\":\"https://test4\",\"Flags\":[\"success\",\"runtime-updates\"]}}}";

static gchar test_dir[34];

typedef struct {
	sqlite3 *db;
	FuSecurityAttrs *attrs;
	FuHistory *history;
} FuTest;

static void
fu_test_free(FuTest *self)
{
	g_free(self);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuTest, fu_test_free)

static void
fu_security_attr_test_mkroot(void)
{
	memset(test_dir, 0, sizeof(test_dir));
	g_sprintf(test_dir, "/tmp/fu-security-attr-test-XXXXXX");
	g_assert_nonnull(g_mkdtemp_full(test_dir, 00755));
	g_printf("Path %s", test_dir);
}

static void
fu_security_attr_test_tear_down()
{
	g_autoptr(GError) error = NULL;
	if (!fu_common_rmtree(test_dir, &error))
		g_warning("failed to mkroot: %s", error->message);
}

static void
fu_security_attr_insert_db_data(FuTest *self)
{
	self->history = fu_history_new();
	g_autoptr(GError) error = NULL;
	fu_history_add_security_attribute(self->history, standard_str, 9, "1.7.0", "{}", &error);
}

static void
fu_security_attr_to_json_func(gconstpointer user_data)
{
	g_autofree gchar *ret1 = NULL;
	g_autofree gchar *ret2 = NULL;
	g_autoptr(GError) error = NULL;
	FuTest *self = (FuTest *)user_data;
	g_autoptr(GPtrArray) items = NULL;
	items = fu_security_attrs_get_all(self->attrs);
	/*for (guint i = 0; i < items->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index(items, i);
		g_printf("%s\n", fwupd_security_attr_get_appstream_id(attr));
	}*/
	ret1 = fu_security_attrs_to_json_string(self->attrs, &error);
	g_assert_nonnull(ret1);
	g_assert_null(error);
	g_assert_cmpint(g_strcmp0(ret1, standard_str), ==, 0);
	/* Empty attr */
	g_autoptr(FuSecurityAttrs) empty_attrs = fu_security_attrs_new();
	ret2 = fu_security_attrs_to_json_string(empty_attrs, &error);
	g_assert_nonnull(ret2);
	g_assert_null(error);
}

static void
fu_security_attr_hsi_compare(gconstpointer user_data)
{
	guint a, b;
	gint ret = 0;

	a = 5;
	b = 0;
	ret = fu_security_attrs_compare_hsi_score(a, b);
	g_assert_cmpint(ret, ==, -1);
	a = 5;
	b = 5;
	ret = fu_security_attrs_compare_hsi_score(a, b);
	g_assert_cmpint(ret, ==, 0);
	a = 0;
	b = 5;
	ret = fu_security_attrs_compare_hsi_score(a, b);
	g_assert_cmpint(ret, ==, 1);
}

static void
fu_security_attr_test_hsi_change_func(gconstpointer user_data)
{
	g_autoptr(FuSecurityAttrs) current_attrs = NULL;
	g_autoptr(FuSecurityAttrs) current_attrs_all_miss = NULL;
	g_autoptr(FuSecurityAttrs) current_attrs_level_change = NULL;
	g_autoptr(FuSecurityAttrs) current_attrs_mixed = NULL;
	gchar *ret = NULL;
	FwupdSecurityAttr *item = NULL;
	/* equal */
	current_attrs = fu_security_attrs_new();
	for (gint i = 0; i < 4; i++) {
		switch (i) {
		/* no flags and GUID */
		case 0:
			item = fwupd_security_attr_new("org.fwupd.hsi.test.string0001");
			fwupd_security_attr_set_level(item, 0);
			fwupd_security_attr_set_name(item, "test1");
			fwupd_security_attr_set_plugin(item, "test1_plugin");
			fwupd_security_attr_set_result(item,
						       FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
			fwupd_security_attr_set_url(item, "https://test1");
			break;
		/* flags only */
		case 1:

			item = fwupd_security_attr_new("org.fwupd.hsi.test.string0002");
			fwupd_security_attr_set_level(item, 0);
			fwupd_security_attr_set_name(item, "test2");
			fwupd_security_attr_set_result(item,
						       FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
			fwupd_security_attr_set_plugin(item, "test2_plugin");
			fwupd_security_attr_set_url(item, "https://test2");
			/* success */
			fwupd_security_attr_set_flags(item, 0x2);
			break;
		/* GUID only */
		case 2:
			item = fwupd_security_attr_new("org.fwupd.hsi.test.string0003");
			fwupd_security_attr_set_level(item, 0);
			fwupd_security_attr_set_name(item, "test3");
			fwupd_security_attr_set_plugin(item, "test1_plugin");
			fwupd_security_attr_set_result(item,
						       FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
			fwupd_security_attr_set_url(item, "https://test3");
			fwupd_security_attr_add_guid(item, "ea9b78bf-8830-47a4-8c72-6093c08b4f88");
			break;
		/* both flags and GUID */
		case 3:
			item = fwupd_security_attr_new("org.fwupd.hsi.test.string0004");
			fwupd_security_attr_set_level(item, 0);
			fwupd_security_attr_set_name(item, "test4");
			fwupd_security_attr_set_plugin(item, "test4_plugin");
			fwupd_security_attr_set_result(item,
						       FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
			fwupd_security_attr_set_url(item, "https://test4");
			fwupd_security_attr_add_flag(item, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
			fwupd_security_attr_add_flag(item,
						     FWUPD_SECURITY_ATTR_FLAG_RUNTIME_UPDATES);
			break;
		default:
			g_printf("out of index\n");
		}
		fu_security_attrs_append(current_attrs, item);
	}

	ret = fu_security_attrs_hsi_change(current_attrs, standard_str);
	g_assert_cmpstr(ret, ==, "{}");
	g_free(ret);

	/* remove all and new all*/
	current_attrs_all_miss = fu_security_attrs_new();
	for (gint i = 0; i < 4; i++) {
		switch (i) {
		/* no flags and GUID */
		case 0:
			item = fwupd_security_attr_new("org.fwupd.hsi.test.Newstring0001");
			fwupd_security_attr_set_level(item, 0);
			fwupd_security_attr_set_name(item, "test1");
			fwupd_security_attr_set_result(item,
						       FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
			fwupd_security_attr_set_plugin(item, "test1_plugin");
			fwupd_security_attr_set_url(item, "https://test1");
			break;
		/* flags only */
		case 1:

			item = fwupd_security_attr_new("org.fwupd.hsi.test.Newstring0002");
			fwupd_security_attr_set_level(item, 0);
			fwupd_security_attr_set_name(item, "test2");
			fwupd_security_attr_set_result(item,
						       FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
			fwupd_security_attr_set_plugin(item, "test2_plugin");
			fwupd_security_attr_set_url(item, "https://test2");
			/* success */
			fwupd_security_attr_set_flags(item, 0x2);
			break;
		/* GUID only */
		case 2:
			item = fwupd_security_attr_new("org.fwupd.hsi.test.Newstring0003");
			fwupd_security_attr_set_level(item, 0);
			fwupd_security_attr_set_name(item, "test3");
			fwupd_security_attr_set_plugin(item, "test1_plugin");
			fwupd_security_attr_set_result(item,
						       FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
			fwupd_security_attr_set_url(item, "https://test3");
			fwupd_security_attr_add_guid(item, "ea9b78bf-8830-47a4-8c72-6093c08b4f88");
			break;
		/* both flags and GUID */
		case 3:
			item = fwupd_security_attr_new("org.fwupd.hsi.test.Newstring0004");
			fwupd_security_attr_set_level(item, 0);
			fwupd_security_attr_set_name(item, "test4");
			fwupd_security_attr_set_plugin(item, "test4_plugin");
			fwupd_security_attr_set_url(item, "https://test4");
			fwupd_security_attr_set_result(item,
						       FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
			fwupd_security_attr_add_flag(item, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
			fwupd_security_attr_add_flag(item,
						     FWUPD_SECURITY_ATTR_FLAG_RUNTIME_UPDATES);
			break;
		default:
			g_printf("out of index\n");
		}
		fu_security_attrs_append(current_attrs_all_miss, item);
	}
	ret = fu_security_attrs_hsi_change(current_attrs_all_miss, standard_str);
	gchar *result =
	    "{\"org.fwupd.hsi.test.Newstring0001\":{\"new\":{\"HsiLevel\":0,\"HsiResult\":\"not-"
	    "supported\",\"Name\":\"test1\"}},\"org.fwupd.hsi.test.Newstring0002\":{\"new\":{"
	    "\"HsiLevel\":0,\"HsiResult\":\"not-supported\",\"Name\":\"test2\",\"Flags\":["
	    "\"obsoleted\"]}},\"org.fwupd.hsi.test.Newstring0003\":{\"new\":{\"HsiLevel\":0,"
	    "\"HsiResult\":\"not-supported\",\"Name\":\"test3\"}},\"org.fwupd.hsi.test."
	    "Newstring0004\":{\"new\":{\"HsiLevel\":0,\"HsiResult\":\"not-supported\",\"Name\":"
	    "\"test4\",\"Flags\":[\"success\",\"runtime-updates\"]}},\"org.fwupd.hsi.test."
	    "string0004\":{\"removed\":{\"HsiLevel\":0,\"HsiResult\":\"not-supported\",\"Name\":"
	    "\"test4\",\"Flags\":[\"success\",\"runtime-updates\"]}},\"org.fwupd.hsi.test."
	    "string0002\":{\"removed\":{\"HsiLevel\":0,\"HsiResult\":\"not-supported\",\"Name\":"
	    "\"test2\",\"Flags\":[\"obsoleted\"]}},\"org.fwupd.hsi.test.string0003\":{\"removed\":{"
	    "\"HsiLevel\":0,\"HsiResult\":\"not-supported\",\"Name\":\"test3\"}},\"org.fwupd.hsi."
	    "test.string0001\":{\"removed\":{\"HsiLevel\":0,\"HsiResult\":\"not-supported\","
	    "\"Name\":\"test1\"}}}";
	g_assert_cmpstr(ret, ==, result);
	g_free(ret);

	/* all level change */
	current_attrs_level_change = fu_security_attrs_new();
	for (gint i = 0; i < 4; i++) {
		switch (i) {
		/* no flags and GUID */
		case 0:
			item = fwupd_security_attr_new("org.fwupd.hsi.test.string0001");
			fwupd_security_attr_set_level(item, 5);
			fwupd_security_attr_set_name(item, "test1");
			fwupd_security_attr_set_plugin(item, "test1_plugin");
			fwupd_security_attr_set_result(item,
						       FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
			fwupd_security_attr_set_url(item, "https://test1");
			break;
		/* flags only */
		case 1:

			item = fwupd_security_attr_new("org.fwupd.hsi.test.string0002");
			fwupd_security_attr_set_level(item, 2);
			fwupd_security_attr_set_name(item, "test2");
			fwupd_security_attr_set_result(item,
						       FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
			fwupd_security_attr_set_plugin(item, "test2_plugin");
			fwupd_security_attr_set_url(item, "https://test2");
			/* success */
			fwupd_security_attr_set_flags(item, 0x2);
			break;
		/* GUID only */
		case 2:
			item = fwupd_security_attr_new("org.fwupd.hsi.test.string0003");
			fwupd_security_attr_set_level(item, 3);
			fwupd_security_attr_set_name(item, "test3");
			fwupd_security_attr_set_plugin(item, "test1_plugin");
			fwupd_security_attr_set_result(item,
						       FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
			fwupd_security_attr_set_url(item, "https://test3");
			fwupd_security_attr_add_guid(item, "ea9b78bf-8830-47a4-8c72-6093c08b4f88");
			break;
		/* both flags and GUID */
		case 3:
			item = fwupd_security_attr_new("org.fwupd.hsi.test.string0004");
			fwupd_security_attr_set_level(item, 1);
			fwupd_security_attr_set_name(item, "test4");
			fwupd_security_attr_set_plugin(item, "test4_plugin");
			fwupd_security_attr_set_result(item,
						       FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
			fwupd_security_attr_set_url(item, "https://test4");
			fwupd_security_attr_add_flag(item, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
			fwupd_security_attr_add_flag(item,
						     FWUPD_SECURITY_ATTR_FLAG_RUNTIME_UPDATES);
			break;
		default:
			g_printf("out of index\n");
		}
		fu_security_attrs_append(current_attrs_level_change, item);
	}
	ret = fu_security_attrs_hsi_change(current_attrs_level_change, standard_str);
	gchar *result_change =
	    "{\"org.fwupd.hsi.test.string0001\":{\"previous\":{\"HsiLevel\":0,\"HsiResult\":\"not-"
	    "supported\",\"Name\":\"test1\"},\"current\":{\"HsiLevel\":5,\"HsiResult\":\"not-"
	    "supported\",\"Name\":\"test1\"}},\"org.fwupd.hsi.test.string0002\":{\"previous\":{"
	    "\"HsiLevel\":0,\"HsiResult\":\"not-supported\",\"Name\":\"test2\",\"Flags\":["
	    "\"obsoleted\"]},\"current\":{\"HsiLevel\":2,\"HsiResult\":\"not-supported\",\"Name\":"
	    "\"test2\",\"Flags\":[\"obsoleted\"]}},\"org.fwupd.hsi.test.string0003\":{\"previous\":"
	    "{\"HsiLevel\":0,\"HsiResult\":\"not-supported\",\"Name\":\"test3\"},\"current\":{"
	    "\"HsiLevel\":3,\"HsiResult\":\"not-supported\",\"Name\":\"test3\"}},\"org.fwupd.hsi."
	    "test.string0004\":{\"previous\":{\"HsiLevel\":0,\"HsiResult\":\"not-supported\","
	    "\"Name\":\"test4\",\"Flags\":[\"success\",\"runtime-updates\"]},\"current\":{"
	    "\"HsiLevel\":1,\"HsiResult\":\"not-supported\",\"Name\":\"test4\",\"Flags\":["
	    "\"success\",\"runtime-updates\"]}}}";
	g_assert_cmpstr(ret, ==, result_change);
	g_free(ret);

	/* mixed */
	current_attrs_mixed = fu_security_attrs_new();
	for (gint i = 0; i < 3; i++) {
		switch (i) {
		/* no flags and GUID */
		case 0:
			item = fwupd_security_attr_new("org.fwupd.hsi.test.Newstring0001");
			fwupd_security_attr_set_level(item, 5);
			fwupd_security_attr_set_name(item, "test1");
			fwupd_security_attr_set_plugin(item, "test1_plugin");
			fwupd_security_attr_set_result(item,
						       FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
			fwupd_security_attr_set_url(item, "https://test1");
			break;
		/* flags only */
		case 1:

			item = fwupd_security_attr_new("org.fwupd.hsi.test.string0002");
			fwupd_security_attr_set_level(item, 2);
			fwupd_security_attr_set_name(item, "test2");
			fwupd_security_attr_set_result(item,
						       FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
			fwupd_security_attr_set_plugin(item, "test2_plugin");
			fwupd_security_attr_set_url(item, "https://test2");
			/* success */
			fwupd_security_attr_set_flags(item, 0x2);
			break;
		/* GUID only */
		case 2:
			item = fwupd_security_attr_new("org.fwupd.hsi.test.string0003");
			fwupd_security_attr_set_level(item, 3);
			fwupd_security_attr_set_name(item, "test3");
			fwupd_security_attr_set_plugin(item, "test1_plugin");
			fwupd_security_attr_set_result(item,
						       FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
			fwupd_security_attr_set_url(item, "https://test3");
			fwupd_security_attr_add_guid(item, "ea9b78bf-8830-47a4-8c72-6093c08b4f88");
			break;
		default:
			g_printf("out of index\n");
		}
		fu_security_attrs_append(current_attrs_mixed, item);
	}
	ret = fu_security_attrs_hsi_change(current_attrs_mixed, standard_str);
	gchar *standard_mix =
	    "{\"org.fwupd.hsi.test.Newstring0001\":{\"new\":{\"HsiLevel\":5,\"HsiResult\":\"not-"
	    "supported\",\"Name\":\"test1\"}},\"org.fwupd.hsi.test.string0002\":{\"previous\":{"
	    "\"HsiLevel\":0,\"HsiResult\":\"not-supported\",\"Name\":\"test2\",\"Flags\":["
	    "\"obsoleted\"]},\"current\":{\"HsiLevel\":2,\"HsiResult\":\"not-supported\",\"Name\":"
	    "\"test2\",\"Flags\":[\"obsoleted\"]}},\"org.fwupd.hsi.test.string0003\":{\"previous\":"
	    "{\"HsiLevel\":0,\"HsiResult\":\"not-supported\",\"Name\":\"test3\"},\"current\":{"
	    "\"HsiLevel\":3,\"HsiResult\":\"not-supported\",\"Name\":\"test3\"}},\"org.fwupd.hsi."
	    "test.string0004\":{\"removed\":{\"HsiLevel\":0,\"HsiResult\":\"not-supported\","
	    "\"Name\":\"test4\",\"Flags\":[\"success\",\"runtime-updates\"]}},\"org.fwupd.hsi.test."
	    "string0001\":{\"removed\":{\"HsiLevel\":0,\"HsiResult\":\"not-supported\",\"Name\":"
	    "\"test1\"}}}";
	g_assert_cmpstr(ret, ==, standard_mix);
	g_free(ret);

	/* NULL previous */
	ret = fu_security_attrs_hsi_change(current_attrs_mixed, NULL);
	gchar *standard_null =
	    "{\"org.fwupd.hsi.test.Newstring0001\":{\"new\":{\"HsiLevel\":5,\"HsiResult\":\"not-"
	    "supported\",\"Name\":\"test1\"}},\"org.fwupd.hsi.test.string0002\":{\"new\":{"
	    "\"HsiLevel\":2,\"HsiResult\":\"not-supported\",\"Name\":\"test2\",\"Flags\":["
	    "\"obsoleted\"]}},\"org.fwupd.hsi.test.string0003\":{\"new\":{\"HsiLevel\":3,"
	    "\"HsiResult\":\"not-supported\",\"Name\":\"test3\"}}}";
	g_assert_cmpstr(ret, ==, standard_null);
	g_free(ret);
}

int
main(int argc, char **argv)
{
	gboolean ret;
	g_autoptr(FuTest) self = g_new0(FuTest, 1);
	g_autoptr(GError) error = NULL;
	FwupdSecurityAttr *attr = NULL;
	g_autoptr(FuSecurityAttrs) attrs = NULL;
	g_autofree gchar *var_path = NULL;
	g_autofree gchar *system_path = NULL;

	g_test_init(&argc, &argv, NULL);
	fu_security_attr_test_mkroot();

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	g_setenv("G_MESSAGES_DEBUG", "all", TRUE);
	g_setenv("FWUPD_DEVICE_LIST_VERBOSE", "1", TRUE);
	g_setenv("FWUPD_DATADIR", TESTDATADIR_SRC, TRUE);
	g_setenv("FWUPD_PLUGINDIR", TESTDATADIR_SRC, TRUE);
	g_setenv("FWUPD_SYSCONFDIR", TESTDATADIR_SRC, TRUE);
	g_setenv("FWUPD_SYSFSFWDIR", TESTDATADIR_SRC, TRUE);
	system_path = g_build_path("/", test_dir, "system-update");
	g_setenv("FWUPD_OFFLINE_TRIGGER", system_path, TRUE);
	var_path = g_build_path("/", test_dir, "var");
	g_printf("var_path %s", var_path);
	g_setenv("FWUPD_LOCALSTATEDIR", var_path, TRUE);

	/* test convert attr to string */
	attrs = fu_security_attrs_new();
	self->attrs = attrs;
	for (gint i = 0; i < 4; i++) {
		switch (i) {
		/* no flags and GUID */
		case 0:
			attr = fwupd_security_attr_new("org.fwupd.hsi.test.string0001");
			fwupd_security_attr_set_level(attr, 0);
			fwupd_security_attr_set_name(attr, "test1");
			fwupd_security_attr_set_result(attr,
						       FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
			fwupd_security_attr_set_plugin(attr, "test1_plugin");
			fwupd_security_attr_set_url(attr, "https://test1");
			break;
		/* flags only */
		case 1:

			attr = fwupd_security_attr_new("org.fwupd.hsi.test.string0002");
			fwupd_security_attr_set_level(attr, 0);
			fwupd_security_attr_set_name(attr, "test2");
			fwupd_security_attr_set_result(attr,
						       FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
			fwupd_security_attr_set_plugin(attr, "test2_plugin");
			fwupd_security_attr_set_url(attr, "https://test2");
			/* success */
			fwupd_security_attr_set_flags(attr, 0x2);
			break;
		/* GUID only */
		case 2:
			attr = fwupd_security_attr_new("org.fwupd.hsi.test.string0003");
			fwupd_security_attr_set_level(attr, 0);
			fwupd_security_attr_set_name(attr, "test3");
			fwupd_security_attr_set_plugin(attr, "test1_plugin");
			fwupd_security_attr_set_result(attr,
						       FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
			fwupd_security_attr_set_url(attr, "https://test3");
			fwupd_security_attr_add_guid(attr, "ea9b78bf-8830-47a4-8c72-6093c08b4f88");
			break;
		/* both flags and GUID */
		case 3:
			attr = fwupd_security_attr_new("org.fwupd.hsi.test.string0004");
			fwupd_security_attr_set_level(attr, 0);
			fwupd_security_attr_set_name(attr, "test4");
			fwupd_security_attr_set_plugin(attr, "test4_plugin");
			fwupd_security_attr_set_url(attr, "https://test4");
			fwupd_security_attr_set_result(attr,
						       FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
			fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
			fwupd_security_attr_add_flag(attr,
						     FWUPD_SECURITY_ATTR_FLAG_RUNTIME_UPDATES);
			break;
		default:
			g_printf("out of index\n");
		}
		fu_security_attrs_append(attrs, attr);
	}
	/* insert test data */
	fu_security_attr_insert_db_data(self);

	g_test_add_data_func("/fwupd/security-attr{to-json}", self, fu_security_attr_to_json_func);
	g_test_add_data_func("/fwupd/security-attr{hsi-compare}",
			     self,
			     fu_security_attr_hsi_compare);
	g_test_add_data_func("/fwupd/securioty-attr{hsi-change}",
			     self,
			     fu_security_attr_test_hsi_change_func);

	return g_test_run();
}
