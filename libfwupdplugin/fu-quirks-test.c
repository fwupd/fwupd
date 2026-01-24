/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include <glib/gstdio.h>

#include "fwupd-enums-private.h"

#include "fu-context-private.h"
#include "fu-test.h"

static void
fu_quirks_performance_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuQuirks) quirks = fu_quirks_new(ctx);
	g_autoptr(GTimer) timer = g_timer_new();
	g_autoptr(GError) error = NULL;
	const gchar *keys[] = {"Name", "Children", "Flags", NULL};

	ret = fu_quirks_load(quirks, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* lookup */
	g_timer_reset(timer);
	for (guint j = 0; j < 1000; j++) {
		const gchar *group = "bb9ec3e2-77b3-53bc-a1f1-b05916715627";
		for (guint i = 0; keys[i] != NULL; i++) {
			const gchar *tmp = fu_quirks_lookup_by_id(quirks, group, keys[i]);
			g_assert_cmpstr(tmp, !=, NULL);
		}
	}
	g_debug("lookup=%.3fms", g_timer_elapsed(timer, NULL) * 1000.f);
}

typedef struct {
	gboolean seen_one;
	gboolean seen_two;
} FuPluginQuirksAppendHelper;

static void
fu_quirks_append_cb(FuQuirks *quirks,
		    const gchar *key,
		    const gchar *value,
		    FuContextQuirkSource source,
		    gpointer user_data)
{
	FuPluginQuirksAppendHelper *helper = (FuPluginQuirksAppendHelper *)user_data;
	g_debug("key=%s, value=%s", key, value);
	if (g_strcmp0(key, "Plugin") == 0 && g_strcmp0(value, "one") == 0) {
		helper->seen_one = TRUE;
		return;
	}
	if (g_strcmp0(key, "Plugin") == 0 && g_strcmp0(value, "two") == 0) {
		helper->seen_two = TRUE;
		return;
	}
	g_assert_not_reached();
}

static void
fu_quirks_append_func(void)
{
	FuPluginQuirksAppendHelper helper = {0};
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuQuirks) quirks = fu_quirks_new(ctx);
	g_autoptr(GError) error = NULL;

	/* lookup a duplicate group name */
	ret = fu_quirks_load(quirks, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_quirks_lookup_by_id_iter(quirks,
					  "b19d1c67-a29a-51ce-9cae-f7b40fe5505b",
					  NULL,
					  fu_quirks_append_cb,
					  &helper);
	g_assert_true(ret);
	g_assert_true(helper.seen_one);
	g_assert_true(helper.seen_two);
}

static void
fu_quirks_vendor_ids_func(void)
{
	gboolean ret;
	const gchar *tmp;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autofree gchar *guid1 = fwupd_guid_hash_string("PCI\\VEN_8086");
	g_autofree gchar *guid2 = fwupd_guid_hash_string("USB\\VID_8086");
	g_autofree gchar *guid3 = fwupd_guid_hash_string("PNP\\VID_ICO");
	g_autofree gchar *guid4 = fwupd_guid_hash_string("PCI\\VEN_8086&DEV_0007");
	g_autofree gchar *guid5 = fwupd_guid_hash_string("USB\\VID_8086&PID_0001");
	g_autofree gchar *quirksdb = fu_path_build(FU_PATH_KIND_CACHEDIR_PKG, "quirks.db", NULL);
	g_autoptr(FuQuirks) quirks = fu_quirks_new(ctx);
	g_autoptr(GError) error = NULL;

	g_debug("deleting %s if exists", quirksdb);
	g_unlink(quirksdb);

	/* lookup a duplicate group name */
	ret = fu_quirks_load(quirks, FU_QUIRKS_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	tmp = fu_quirks_lookup_by_id(quirks, guid1, "Vendor");
	g_assert_true(ret);
	g_assert_cmpstr(tmp, ==, "Intel Corporation");
	tmp = fu_quirks_lookup_by_id(quirks, guid2, "Vendor");
	g_assert_true(ret);
	g_assert_cmpstr(tmp, ==, "Intel Corp.");
	tmp = fu_quirks_lookup_by_id(quirks, guid3, FWUPD_RESULT_KEY_VENDOR);
	g_assert_true(ret);
	g_assert_cmpstr(tmp, ==, "Intel Corp");
	tmp = fu_quirks_lookup_by_id(quirks, guid4, FWUPD_RESULT_KEY_NAME);
	g_assert_true(ret);
	g_assert_cmpstr(tmp, ==, "82379AB");
	tmp = fu_quirks_lookup_by_id(quirks, guid5, FWUPD_RESULT_KEY_NAME);
	g_assert_true(ret);
	g_assert_cmpstr(tmp, ==, "AnyPoint (TM) Home Network 1.6 Mbps Wireless Adapter");
}

int
main(int argc, char **argv)
{
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;

	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);

	g_test_init(&argc, &argv, NULL);

	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_DATADIR", testdatadir, TRUE);

	/* deleted on error */
	tmpdir = fu_temporary_directory_new("quirks", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	(void)g_setenv("FWUPD_LOCALSTATEDIR", fu_temporary_directory_get_path(tmpdir), TRUE);
	(void)g_setenv("CACHE_DIRECTORY", fu_temporary_directory_get_path(tmpdir), TRUE);

	g_test_add_func("/fwupd/quirks/performance", fu_quirks_performance_func);
	g_test_add_func("/fwupd/quirks/append", fu_quirks_append_func);
	g_test_add_func("/fwupd/quirks/vendor-ids", fu_quirks_vendor_ids_func);
	return g_test_run();
}
