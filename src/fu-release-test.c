/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-remote-private.h"

#include "fu-cabinet.h"
#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-engine.h"
#include "fu-release-common.h"
#include "fu-release.h"

static gint
fu_release_compare_func_cb(gconstpointer a, gconstpointer b)
{
	FuRelease *release1 = *((FuRelease **)a);
	FuRelease *release2 = *((FuRelease **)b);
	return fu_release_compare(release1, release2);
}

static void
fu_release_compare_func(void)
{
	g_autoptr(GPtrArray) releases = g_ptr_array_new();
	g_autoptr(FuDevice) device1 = fu_device_new(NULL);
	g_autoptr(FuDevice) device2 = fu_device_new(NULL);
	g_autoptr(FuDevice) device3 = fu_device_new(NULL);
	g_autoptr(FuRelease) release1 = fu_release_new();
	g_autoptr(FuRelease) release2 = fu_release_new();
	g_autoptr(FuRelease) release3 = fu_release_new();

	fu_device_set_order(device1, 33);
	fu_release_set_device(release1, device1);
	fu_release_set_priority(release1, 0);
	fu_release_set_branch(release1, "1");

	fu_device_set_order(device2, 11);
	fu_release_set_device(release2, device2);
	fu_release_set_priority(release2, 0);
	fu_release_set_branch(release2, "2");

	fu_device_set_order(device3, 11);
	fu_release_set_device(release3, device3);
	fu_release_set_priority(release3, 99);
	fu_release_set_branch(release3, "3");

	g_ptr_array_add(releases, release1);
	g_ptr_array_add(releases, release2);
	g_ptr_array_add(releases, release3);

	/* order the install tasks */
	g_ptr_array_sort(releases, fu_release_compare_func_cb);
	g_assert_cmpint(releases->len, ==, 3);
	g_assert_cmpstr(fu_release_get_branch(g_ptr_array_index(releases, 0)), ==, "3");
	g_assert_cmpstr(fu_release_get_branch(g_ptr_array_index(releases, 1)), ==, "2");
	g_assert_cmpstr(fu_release_get_branch(g_ptr_array_index(releases, 2)), ==, "1");
}

static void
fu_release_uri_scheme_func(void)
{
	struct {
		const gchar *in;
		const gchar *op;
	} strs[] = {{"https://foo.bar/baz", "https"},
		    {"HTTP://FOO.BAR/BAZ", "http"},
		    {"ftp://", "ftp"},
		    {"ftp:", "ftp"},
		    {"foobarbaz", NULL},
		    {"", NULL},
		    {NULL, NULL}};
	for (guint i = 0; strs[i].in != NULL; i++) {
		g_autofree gchar *tmp = fu_release_uri_get_scheme(strs[i].in);
		g_assert_cmpstr(tmp, ==, strs[i].op);
	}
}

static void
fu_release_trusted_report_func(void)
{
	FwupdRelease *rel;
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FwupdRemote) remote = fwupd_remote_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderNode) custom = xb_builder_node_new("custom");
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);

	/* set up test harness */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSCONFDIR_PKG, testdatadir);

	/* add fake LVFS remote */
	fwupd_remote_set_id(remote, "lvfs");
	fu_engine_add_remote(engine, remote);

	/* load engine to get FuConfig set up */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* metadata with vendor id=123 */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "metadata-report1.xml", NULL);
	file = g_file_new_for_path(filename);
	ret = xb_builder_source_load_file(source, file, XB_BUILDER_SOURCE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	xb_builder_node_insert_text(custom, "value", "lvfs", "key", "fwupd::RemoteId", NULL);
	xb_builder_source_set_info(source, custom);
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	fu_engine_set_silo(engine, silo);

	/* add a dummy device */
	fu_device_set_id(device, "dummy");
	fu_device_set_version(device, "1.2.2");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_protocol(device, "com.acme");
	fu_device_add_instance_id(device, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_engine_add_device(engine, device);

	/* ensure we set this as trusted */
	releases = fu_engine_get_releases_for_device(engine, request, device, &error);
	g_assert_no_error(error);
	g_assert_nonnull(releases);
	g_assert_cmpint(releases->len, ==, 1);
	rel = g_ptr_array_index(releases, 0);
	g_assert_true(fwupd_release_has_flag(rel, FWUPD_RELEASE_FLAG_TRUSTED_REPORT));
}

static void
fu_release_trusted_report_oem_func(void)
{
	FwupdRelease *rel;
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);

	/* load engine to get FuConfig set up */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* metadata with FromOEM */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "metadata-report2.xml", NULL);
	file = g_file_new_for_path(filename);
	ret = xb_builder_source_load_file(source, file, XB_BUILDER_SOURCE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	fu_engine_set_silo(engine, silo);

	/* add a dummy device */
	fu_device_set_id(device, "dummy");
	fu_device_set_version(device, "1.2.2");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_protocol(device, "com.acme");
	fu_device_add_instance_id(device, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_engine_add_device(engine, device);

	/* ensure we set this as trusted */
	releases = fu_engine_get_releases_for_device(engine, request, device, &error);
	g_assert_no_error(error);
	g_assert_nonnull(releases);
	g_assert_cmpint(releases->len, ==, 1);
	rel = g_ptr_array_index(releases, 0);
	g_assert_true(fwupd_release_has_flag(rel, FWUPD_RELEASE_FLAG_TRUSTED_REPORT));
}

static void
fu_release_no_trusted_report_upgrade_func(void)
{
	FwupdRelease *rel;
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);

	/* set up test harness */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSCONFDIR_PKG, testdatadir);

	/* load engine to get FuConfig set up */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* metadata with FromOEM, but *NOT* an upgrade */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "metadata-report4.xml", NULL);
	file = g_file_new_for_path(filename);
	ret = xb_builder_source_load_file(source, file, XB_BUILDER_SOURCE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	fu_engine_set_silo(engine, silo);

	/* add a dummy device */
	fu_device_set_id(device, "dummy");
	fu_device_set_version(device, "1.2.3");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_protocol(device, "com.acme");
	fu_device_add_instance_id(device, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_engine_add_device(engine, device);

	/* ensure we set this as trusted */
	releases = fu_engine_get_releases_for_device(engine, request, device, &error);
	g_assert_no_error(error);
	g_assert_nonnull(releases);
	g_assert_cmpint(releases->len, ==, 1);
	rel = g_ptr_array_index(releases, 0);
	g_assert_false(fwupd_release_has_flag(rel, FWUPD_RELEASE_FLAG_TRUSTED_REPORT));
}

static void
fu_release_no_trusted_report_func(void)
{
	FwupdRelease *rel;
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);

	/* load engine to get FuConfig set up */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* metadata without OEM or valid VendorId as per tests/fwupd.conf */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "metadata-report3.xml", NULL);
	file = g_file_new_for_path(filename);
	ret = xb_builder_source_load_file(source, file, XB_BUILDER_SOURCE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	fu_engine_set_silo(engine, silo);

	/* add a dummy device */
	fu_device_set_id(device, "dummy");
	fu_device_set_version(device, "1.2.2");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_protocol(device, "com.acme");
	fu_device_add_instance_id(device, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_engine_add_device(engine, device);

	/* ensure trusted reports flag is not set */
	releases = fu_engine_get_releases_for_device(engine, request, device, &error);
	g_assert_no_error(error);
	g_assert_nonnull(releases);
	g_assert_cmpint(releases->len, ==, 1);
	rel = g_ptr_array_index(releases, 0);
	g_assert_false(fwupd_release_has_flag(rel, FWUPD_RELEASE_FLAG_TRUSTED_REPORT));
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/release/compare", fu_release_compare_func);
	g_test_add_func("/fwupd/release/uri-scheme", fu_release_uri_scheme_func);
	g_test_add_func("/fwupd/release/trusted-report", fu_release_trusted_report_func);
	g_test_add_func("/fwupd/release/trusted-report-oem", fu_release_trusted_report_oem_func);
	g_test_add_func("/fwupd/release/no-trusted-report-upgrade",
			fu_release_no_trusted_report_upgrade_func);
	g_test_add_func("/fwupd/release/no-trusted-report", fu_release_no_trusted_report_func);
	return g_test_run();
}
