/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-remote-private.h"

#include "../plugins/test/fu-test-plugin.h"
#include "fu-bios-settings-private.h"
#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-engine-requirements.h"
#include "fu-engine.h"
#include "fu-history.h"
#include "fu-plugin-private.h"
#include "fu-remote.h"
#include "fu-test.h"

static void
fu_engine_save_remote_broken(FuTemporaryDirectory *tmpdir)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *uri = NULL;
	g_autoptr(FwupdRemote) remote = fwupd_remote_new();
	g_autoptr(GError) error = NULL;

	uri = g_strdup_printf("file://%s/broken.xml.gz", fu_temporary_directory_get_path(tmpdir));
	fwupd_remote_set_id(remote, "broken");
	fwupd_remote_set_metadata_uri(remote, uri);
	fwupd_remote_add_flag(remote, FWUPD_REMOTE_FLAG_ENABLED);

	fn = fu_temporary_directory_build(tmpdir, "remotes.d", "broken.conf", NULL);
	ret = fu_remote_save_to_filename(remote, fn, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_save_remote_stable(FuTemporaryDirectory *tmpdir)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *uri = NULL;
	g_autoptr(FwupdRemote) remote = fwupd_remote_new();
	g_autoptr(GError) error = NULL;

	uri = g_strdup_printf("file://%s/stable.xml", fu_temporary_directory_get_path(tmpdir));
	fwupd_remote_set_id(remote, "stable");
	fwupd_remote_set_metadata_uri(remote, uri);
	fwupd_remote_add_flag(remote, FWUPD_REMOTE_FLAG_ENABLED);

	fn = fu_temporary_directory_build(tmpdir, "remotes.d", "stable.conf", NULL);
	ret = fu_remote_save_to_filename(remote, fn, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_save_remote_directory(FuTemporaryDirectory *tmpdir)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *uri = NULL;
	g_autoptr(FwupdRemote) remote = fwupd_remote_new();
	g_autoptr(GError) error = NULL;

	uri = g_strdup_printf("file://%s", fu_temporary_directory_get_path(tmpdir));
	fwupd_remote_set_id(remote, "directory");
	fwupd_remote_set_metadata_uri(remote, uri);
	fwupd_remote_add_flag(remote, FWUPD_REMOTE_FLAG_ENABLED);

	fn = fu_temporary_directory_build(tmpdir, "remotes.d", "directory.conf", NULL);
	ret = fu_remote_save_to_filename(remote, fn, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_save_remote_testing(FuTemporaryDirectory *tmpdir)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *uri = NULL;
	g_autoptr(FwupdRemote) remote = fwupd_remote_new();
	g_autoptr(GError) error = NULL;

	uri = g_strdup_printf("file://%s/testing.xml", fu_temporary_directory_get_path(tmpdir));
	fwupd_remote_set_id(remote, "testing");
	fwupd_remote_set_metadata_uri(remote, uri);
	fwupd_remote_add_flag(remote, FWUPD_REMOTE_FLAG_ENABLED);
	fwupd_remote_add_flag(remote, FWUPD_REMOTE_FLAG_APPROVAL_REQUIRED);

	fn = fu_temporary_directory_build(tmpdir, "remotes.d", "testing.conf", NULL);
	ret = fu_remote_save_to_filename(remote, fn, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_generate_md_func(void)
{
	const gchar *tmp;
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *fn_archive = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("self-tests", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_METADATA, tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_CACHEDIR_PKG, tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_DATADIR_PKG, tmpdir);
	fu_engine_save_remote_directory(tmpdir);

	/* put cab file somewhere we can parse it */
	filename = g_test_build_filename(G_TEST_BUILT,
					 "..",
					 "libfwupdplugin",
					 "tests",
					 "colorhug",
					 "colorhug-als-3.0.2.cab",
					 NULL);
	data = fu_bytes_get_contents(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);
	fn_archive = fu_temporary_directory_build(tmpdir, "foo.cab", NULL);
	ret = fu_bytes_set_contents(fn_archive, data, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* load engine and check the device was found */
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_REMOTES | FU_ENGINE_LOAD_FLAG_NO_CACHE,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_device_add_instance_id(device, "12345678-1234-1234-1234-123456789012");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.3");
	component = fu_engine_get_component_by_guids(engine, device);
	g_assert_nonnull(component);

	/* check remote ID set */
	tmp = xb_node_query_text(component, "../custom/value[@key='fwupd::RemoteId']", NULL);
	g_assert_cmpstr(tmp, ==, "directory");

	/* verify checksums */
	tmp = xb_node_query_text(component, "releases/release/checksum[@target='container']", NULL);
	g_assert_cmpstr(tmp, ==, "71aefb2a9b412833d8c519d5816ef4c5668e5e76");
	tmp = xb_node_query_text(component, "releases/release/checksum[@target='content']", NULL);
	g_assert_cmpstr(tmp, ==, NULL);
}

static void
fu_engine_test_plugin_mutable_enumeration(void)
{
	g_autofree gchar *fake_localconf_fn = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuEngine) engine = NULL;
	g_autoptr(FuPlugin) plugin = fu_plugin_new(NULL);
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	gboolean ret;

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("mutable-enumeration", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_SYSCONFDIR_PKG, tmpdir);
	fake_localconf_fn = fu_temporary_directory_build(tmpdir, "fwupd.conf", NULL);

	ret = g_file_set_contents(fake_localconf_fn,
				  "# use `man 5 fwupd.conf` for documentation\n"
				  "[fwupd]\n"
				  "RequireImmutableEnumeration=true\n",
				  -1,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	engine = fu_engine_new(ctx);
	g_assert_nonnull(engine);

	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* engine requires, plugin doesn't have */
	ret = fu_engine_plugin_allows_enumeration(engine, plugin);
	g_assert_true(ret);

	/* engine requires, plugin does have */
	fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_MUTABLE_ENUMERATION);
	ret = fu_engine_plugin_allows_enumeration(engine, plugin);
	g_assert_false(ret);

	/* clear config and reload engine */
	ret = g_file_set_contents(fake_localconf_fn, "[fwupd]\n", -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_clear_object(&engine);

	engine = fu_engine_new(ctx);
	g_assert_nonnull(engine);

	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* engine requires, plugin does have */
	ret = fu_engine_plugin_allows_enumeration(engine, plugin);
	g_assert_true(ret);

	/* drop flag, engine shouldn't care */
	fu_plugin_remove_flag(plugin, FWUPD_PLUGIN_FLAG_MUTABLE_ENUMERATION);
	ret = fu_engine_plugin_allows_enumeration(engine, plugin);
	g_assert_true(ret);
}

static void
fu_engine_device_parent_guid_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device1 = fu_device_new(ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(ctx);
	g_autoptr(FuDevice) device3 = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* add child */
	fu_device_set_id(device1, "child");
	fu_device_build_vendor_id_u16(device2, "USB", 0xFFFF);
	fu_device_add_protocol(device2, "com.acme");
	fu_device_add_instance_id(device1, "child-GUID-1");
	fu_device_add_parent_guid(device1, "parent-GUID");
	fu_engine_add_device(engine, device1);

	/* parent */
	fu_device_set_id(device2, "parent");
	fu_device_build_vendor_id_u16(device2, "USB", 0xFFFF);
	fu_device_add_protocol(device2, "com.acme");
	fu_device_add_instance_id(device2, "parent-GUID");
	fu_device_set_vendor(device2, "oem");

	/* add another child */
	fu_device_set_id(device3, "child2");
	fu_device_add_instance_id(device3, "child-GUID-2");
	fu_device_add_parent_guid(device3, "parent-GUID");
	fu_device_add_child(device2, device3);

	/* add two together */
	fu_engine_add_device(engine, device2);

	/* this is normally done by fu_plugin_add_device() */
	fu_engine_add_device(engine, device3);

	/* verify both children were adopted */
	g_assert_true(fu_device_get_parent_internal(device3) == device2);
	g_assert_true(fu_device_get_parent_internal(device1) == device2);
	g_assert_cmpstr(fu_device_get_vendor(device3), ==, "oem");

	/* verify order */
	g_assert_cmpint(fu_device_get_order(device1), ==, -1);
	g_assert_cmpint(fu_device_get_order(device2), ==, 0);
	g_assert_cmpint(fu_device_get_order(device3), ==, -1);
}

static void
fu_engine_device_parent_id_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device1 = fu_device_new(ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(ctx);
	g_autoptr(FuDevice) device3 = fu_device_new(ctx);
	g_autoptr(FuDevice) device4 = fu_device_new(ctx);
	g_autoptr(FuDevice) device5 = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* add child */
	fu_device_set_id(device1, "child1");
	fu_device_set_name(device1, "Child1");
	fu_device_set_physical_id(device1, "child-ID1");
	fu_device_build_vendor_id_u16(device1, "USB", 0xFFFF);
	fu_device_add_protocol(device1, "com.acme");
	fu_device_add_instance_id(device1, "child-GUID-1");
	fu_device_add_parent_physical_id(device1, "parent-ID-notfound");
	fu_device_add_parent_physical_id(device1, "parent-ID");
	fu_engine_add_device(engine, device1);

	/* parent */
	fu_device_set_id(device2, "parent");
	fu_device_set_name(device2, "Parent");
	fu_device_set_backend_id(device2, "/sys/devices/foo/bar/baz");
	fu_device_set_physical_id(device2, "parent-ID");
	fu_device_build_vendor_id_u16(device2, "USB", 0xFFFF);
	fu_device_add_protocol(device2, "com.acme");
	fu_device_add_instance_id(device2, "parent-GUID");
	fu_device_set_vendor(device2, "oem");
	fu_device_add_private_flag(device2, FU_DEVICE_PRIVATE_FLAG_AUTO_PARENT_CHILDREN);

	/* add another child */
	fu_device_set_id(device3, "child2");
	fu_device_set_name(device3, "Child2");
	fu_device_set_physical_id(device3, "child-ID2");
	fu_device_add_instance_id(device3, "child-GUID-2");
	fu_device_add_parent_physical_id(device3, "parent-ID");
	fu_device_add_child(device2, device3);

	/* add two together */
	fu_engine_add_device(engine, device2);

	/* add non-child */
	fu_device_set_id(device4, "child4");
	fu_device_set_name(device4, "Child4");
	fu_device_set_physical_id(device4, "child-ID4");
	fu_device_build_vendor_id(device4, "USB", "FFFF");
	fu_device_add_protocol(device4, "com.acme");
	fu_device_add_instance_id(device4, "child-GUID-4");
	fu_device_add_parent_physical_id(device4, "parent-ID");
	fu_engine_add_device(engine, device4);

	/* this is normally done by fu_plugin_add_device() */
	fu_engine_add_device(engine, device4);

	/* add child with the parent backend ID */
	fu_device_set_id(device5, "child5");
	fu_device_set_name(device5, "Child5");
	fu_device_set_physical_id(device5, "child-ID5");
	fu_device_build_vendor_id(device5, "USB", "FFFF");
	fu_device_add_protocol(device5, "com.acme");
	fu_device_add_instance_id(device5, "child-GUID-5");
	fu_device_add_parent_backend_id(device5, "/sys/devices/foo/bar/baz");
	fu_engine_add_device(engine, device5);

	/* this is normally done by fu_plugin_add_device() */
	fu_engine_add_device(engine, device5);

	/* verify both children were adopted */
	g_assert_true(fu_device_get_parent_internal(device3) == device2);
	g_assert_true(fu_device_get_parent_internal(device4) == device2);
	g_assert_true(fu_device_get_parent_internal(device5) == device2);
	g_assert_true(fu_device_get_parent_internal(device1) == device2);
	g_assert_cmpstr(fu_device_get_vendor(device3), ==, "oem");
}

static void
fu_engine_partial_hash_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device1 = fu_device_new(ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuPlugin) plugin = fu_plugin_new(NULL);
	g_autoptr(GError) error = NULL;
	g_autoptr(GError) error_none = NULL;
	g_autoptr(GError) error_both = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up dummy plugin */
	fu_plugin_set_name(plugin, "test");
	fu_engine_add_plugin(engine, plugin);

	/* add two dummy devices */
	fu_device_set_id(device1, "device1");
	fu_device_build_vendor_id_u16(device1, "USB", 0xFFFF);
	fu_device_add_protocol(device1, "com.acme");
	fu_device_set_plugin(device1, "test");
	fu_device_add_instance_id(device1, "12345678-1234-1234-1234-123456789012");
	fu_device_set_id(device1, "99249eb1bd9ef0b6e192b271a8cb6a3090cfec7a");
	fu_engine_add_device(engine, device1);
	fu_device_set_id(device2, "device21");
	fu_device_build_vendor_id_u16(device2, "USB", 0xFFFF);
	fu_device_add_protocol(device2, "com.acme");
	fu_device_set_plugin(device2, "test");
	fu_device_set_equivalent_id(device2, "b92f5b7560b84ca005a79f5a15de3c003ce494cf");
	fu_device_add_instance_id(device2, "87654321-1234-1234-1234-123456789012");
	fu_device_set_id(device2, "99244162a6daa0b033d649c8d464529cec41d3de");
	fu_engine_add_device(engine, device2);

	/* match nothing */
	ret = fu_engine_unlock(engine, "deadbeef", &error_none);
	g_assert_error(error_none, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);

	/* match both */
	ret = fu_engine_unlock(engine, "9924", &error_both);
	g_assert_error(error_both, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);

	/* match one exactly */
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_LOCKED);
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_LOCKED);
	ret = fu_engine_unlock(engine, "99244162a6daa0b033d649c8d464529cec41d3de", &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* match one partially */
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_LOCKED);
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_LOCKED);
	ret = fu_engine_unlock(engine, "99249", &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* match equivalent ID */
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_LOCKED);
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_LOCKED);
	ret = fu_engine_unlock(engine, "b92f", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_device_unlock_func(void)
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

	/* load engine to get FuConfig set up */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add the hardcoded 'fwupd' metadata */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "metadata.xml", NULL);
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
	fu_device_set_id(device, "UEFI-dummy-dev0");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_add_instance_id(device, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_LOCKED);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PLAIN);
	fu_engine_add_device(engine, device);

	/* ensure the metainfo was matched */
	rel = fwupd_device_get_release_default(FWUPD_DEVICE(device));
	g_assert_nonnull(rel);
	g_assert_false(fwupd_release_has_flag(rel, FWUPD_RELEASE_FLAG_TRUSTED_REPORT));
}

static void
fu_engine_device_equivalent_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device1 = fu_device_new(ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(ctx);
	g_autoptr(FuDevice) device_best = NULL;
	g_autoptr(FuDevice) device_worst = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GError) error = NULL;

	/* load engine to get FuConfig set up */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a wireless (worse) device */
	fu_device_set_id(device1, "99249eb1bd9ef0b6e192b271a8cb6a3090cfec7a");
	fu_device_set_name(device1, "device1");
	fu_device_build_vendor_id_u16(device1, "USB", 0xFFFF);
	fu_device_add_protocol(device1, "com.acme");
	fu_device_add_instance_id(device1, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_engine_add_device(engine, device1);

	/* add a wired (better) device */
	fu_device_set_id(device2, "1a8d0d9a96ad3e67ba76cf3033623625dc6d6882");
	fu_device_set_name(device2, "device2");
	fu_device_set_equivalent_id(device2, "99249eb1bd9ef0b6e192b271a8cb6a3090cfec7a");
	fu_device_set_priority(device2, 999);
	fu_device_build_vendor_id_u16(device2, "USB", 0xFFFF);
	fu_device_add_protocol(device2, "com.acme");
	fu_device_add_instance_id(device2, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_engine_add_device(engine, device2);

	/* make sure the daemon chooses the best device */
	devices = fu_engine_get_devices(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 2);
	device_best = fu_engine_get_device(engine, "9924", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device_best);
	g_assert_cmpstr(fu_device_get_id(device_best),
			==,
			"1a8d0d9a96ad3e67ba76cf3033623625dc6d6882");
	g_assert_true(fu_device_has_flag(device_best, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_problem(device_best, FWUPD_DEVICE_PROBLEM_LOWER_PRIORITY));

	/* get the worst device and make sure it's not updatable */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device_tmp = g_ptr_array_index(devices, i);
		if (device_tmp != device_best) {
			device_worst = g_object_ref(device_tmp);
			break;
		}
	}
	g_assert_nonnull(device_worst);
	g_assert_false(fu_device_has_flag(device_worst, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_true(fu_device_has_problem(device_worst, FWUPD_DEVICE_PROBLEM_LOWER_PRIORITY));
}

static void
fu_engine_device_md_set_flags_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml =
	    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	    "<components version=\"0.9\">\n"
	    "  <component type=\"firmware\">\n"
	    "    <id>org.fwupd.8330a096d9f1af8567c7374cb8403e1ce9cf3163.device</id>\n"
	    "    <provides>\n"
	    "      <firmware type=\"flashed\">2d47f29b-83a2-4f31-a2e8-63474f4d4c2e</firmware>\n"
	    "    </provides>\n"
	    "    <releases>\n"
	    "      <release version=\"1\" />\n"
	    "    </releases>\n"
	    "    <custom>\n"
	    "      <value key=\"LVFS::DeviceFlags\">save-into-backup-remote</value>\n"
	    "    </custom>\n"
	    "  </component>\n"
	    "</components>\n";

	/* load engine to get FuConfig set up */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add the XML metadata */
	ret = xb_builder_source_load_xml(source, xml, XB_BUILDER_SOURCE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	fu_engine_set_silo(engine, silo);

	/* add a dummy device */
	fu_device_set_id(device, "UEFI-dummy-dev0");
	fu_device_set_version(device, "0");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_add_instance_id(device, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_FLAGS);
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PLAIN);
	fu_engine_add_device(engine, device);

	/* check the flag got set */
	g_assert_true(
	    fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_SAVE_INTO_BACKUP_REMOTE));
}

static void
fu_engine_device_md_checksum_set_version_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml =
	    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	    "<components version=\"0.9\">\n"
	    "  <component type=\"firmware\">\n"
	    "    <id>org.fwupd.8330a096d9f1af8567c7374cb8403e1ce9cf3163.device</id>\n"
	    "    <provides>\n"
	    "      <firmware type=\"flashed\">2d47f29b-83a2-4f31-a2e8-63474f4d4c2e</firmware>\n"
	    "    </provides>\n"
	    "    <releases>\n"
	    "      <release version=\"124\">\n"
	    "        <location>https://test.org/foo.cab</location>"
	    "        <checksum type=\"sha256\" "
	    "target=\"device\">cdb7c90d3ab8833d5324f5d8516d41fa990b9ca721fe643fffaef9057d9f9e48</"
	    "checksum>\n"
	    "      </release>\n"
	    "    </releases>\n"
	    "    <custom>\n"
	    "      <value key=\"LVFS::UpdateProtocol\">com.acme</value>\n"
	    "      <value key=\"LVFS::VersionFormat\">plain</value>"
	    "    </custom>\n"
	    "  </component>\n"
	    "</components>\n";

	/* load engine to get FuConfig set up */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add the XML metadata */
	ret = xb_builder_source_load_xml(source, xml, XB_BUILDER_SOURCE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	fu_engine_set_silo(engine, silo);

	/* add a dummy device */
	fu_device_set_id(device, "UEFI-dummy-dev0");
	fu_device_set_version(device, "123");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_add_instance_id(device, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_ONLY_CHECKSUM);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_VERSION);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_VERFMT);
	fu_device_add_checksum(device,
			       "cdb7c90d3ab8833d5324f5d8516d41fa990b9ca721fe643fffaef9057d9f9e48");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_NUMBER);
	fu_engine_add_device(engine, device);

	/* check the version got set */
	g_assert_cmpstr(fu_device_get_version(device), ==, "124");
	g_assert_cmpint(fu_device_get_version_format(device), ==, FWUPD_VERSION_FORMAT_PLAIN);
}

static void
fu_engine_device_md_checksum_set_version_wrong_proto_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml =
	    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	    "<components version=\"0.9\">\n"
	    "  <component type=\"firmware\">\n"
	    "    <id>org.fwupd.8330a096d9f1af8567c7374cb8403e1ce9cf3163.device</id>\n"
	    "    <provides>\n"
	    "      <firmware type=\"flashed\">2d47f29b-83a2-4f31-a2e8-63474f4d4c2e</firmware>\n"
	    "    </provides>\n"
	    "    <releases>\n"
	    "      <release version=\"124\">\n"
	    "        <location>https://test.org/foo.cab</location>"
	    "        <checksum type=\"sha256\" "
	    "target=\"device\">cdb7c90d3ab8833d5324f5d8516d41fa990b9ca721fe643fffaef9057d9f9e48</"
	    "checksum>\n"
	    "      </release>\n"
	    "    </releases>\n"
	    "    <custom>\n"
	    "      <value key=\"LVFS::UpdateProtocol\">com.acme</value>\n"
	    "      <value key=\"LVFS::VersionFormat\">plain</value>"
	    "    </custom>\n"
	    "  </component>\n"
	    "</components>\n";

	/* load engine to get FuConfig set up */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add the XML metadata */
	ret = xb_builder_source_load_xml(source, xml, XB_BUILDER_SOURCE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	fu_engine_set_silo(engine, silo);

	/* add a dummy device */
	fu_device_set_id(device, "UEFI-dummy-dev0");
	fu_device_set_version(device, "123");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "SOMETHING_ELSE_ENTIRELY");
	fu_device_add_instance_id(device, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_ONLY_CHECKSUM);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_VERSION);
	fu_device_add_checksum(device,
			       "cdb7c90d3ab8833d5324f5d8516d41fa990b9ca721fe643fffaef9057d9f9e48");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_NUMBER);
	fu_engine_add_device(engine, device);

	/* check the version did not get set, because the protocol was different */
	g_assert_cmpstr(fu_device_get_version(device), ==, "123");
	g_assert_cmpint(fu_device_get_version_format(device), ==, FWUPD_VERSION_FORMAT_NUMBER);
}

static void
fu_engine_require_hwid_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuCabinet) cabinet = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* set up test harness */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_DATADIR_PKG, testdatadir);

	/* load dummy hwids */
	ret = fu_context_load_hwinfo(ctx, progress, FU_CONTEXT_HWID_FLAG_LOAD_CONFIG, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* load engine to get FuConfig set up */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* get generated file as a blob */
	filename =
	    g_test_build_filename(G_TEST_BUILT, "tests", "missing-hwid", "hwid-1.2.3.cab", NULL);
	stream = fu_input_stream_from_path(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	cabinet = fu_engine_build_cabinet_from_stream(engine, stream, &error);
	g_assert_no_error(error);
	g_assert_nonnull(cabinet);

	/* add a dummy device */
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.2");
	fu_device_add_instance_id(device, "12345678-1234-1234-1234-123456789012");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_engine_add_device(engine, device);

	/* get component */
	component = fu_cabinet_get_component(cabinet, "com.hughski.test.firmware", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check requirements */
	fu_release_set_device(release, device);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_nonnull(error);
	g_assert_cmpstr(error->message,
			==,
			"no HWIDs matched 9342d47a-1bab-5709-9869-c840b2eac501");
	g_assert_false(ret);
}

static void
fu_engine_get_details_added_func(void)
{
	FuDevice *device_tmp;
	FwupdRelease *release;
	gboolean ret;
	g_autofree gchar *checksum_sha256 = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* load engine to get FuConfig set up */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a dummy device */
	fu_device_set_id(device, "test_device");
	fu_device_set_name(device, "test device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.2");
	fu_device_add_instance_id(device, "12345678-1234-1234-1234-123456789012");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_engine_add_device(engine, device);

	/* get details */
	filename =
	    g_test_build_filename(G_TEST_BUILT, "tests", "missing-hwid", "hwid-1.2.3.cab", NULL);
	stream = fu_input_stream_from_path(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	checksum_sha256 = fu_input_stream_compute_checksum(stream, G_CHECKSUM_SHA256, &error);
	g_assert_no_error(error);
	g_assert_nonnull(checksum_sha256);
	devices = fu_engine_get_details(engine, request, stream, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	device_tmp = g_ptr_array_index(devices, 0);
	g_assert_cmpstr(fu_device_get_name(device_tmp), ==, "test device");
	release = fu_device_get_release_default(device_tmp);
	g_assert_nonnull(release);
	g_assert_cmpstr(fwupd_release_get_version(release), ==, "1.2.3");
	g_assert_true(fwupd_release_has_checksum(release, checksum_sha256));
}

static void
fu_engine_get_details_missing_func(void)
{
	FuDevice *device_tmp;
	FwupdRelease *release;
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* load engine to get FuConfig set up */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* get details */
	filename =
	    g_test_build_filename(G_TEST_BUILT, "tests", "missing-hwid", "hwid-1.2.3.cab", NULL);
	stream = fu_input_stream_from_path(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	devices = fu_engine_get_details(engine, request, stream, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	device_tmp = g_ptr_array_index(devices, 0);
	g_assert_cmpstr(fu_device_get_name(device_tmp), ==, NULL);
	release = fu_device_get_release_default(device_tmp);
	g_assert_nonnull(release);
	g_assert_cmpstr(fwupd_release_get_version(release), ==, "1.2.3");
}

static void
fu_engine_downgrade_func(void)
{
	FwupdRelease *rel;
	gboolean ret;
	g_autofree gchar *fn_broken = NULL;
	g_autofree gchar *fn_stable = NULL;
	g_autofree gchar *fn_testing = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) devices_pre = NULL;
	g_autoptr(GPtrArray) releases_dg = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(GPtrArray) releases_up = NULL;
	g_autoptr(GPtrArray) releases_up2 = NULL;
	g_autoptr(GPtrArray) remotes = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("self-tests", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_METADATA, tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_CACHEDIR_PKG, tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_DATADIR_PKG, tmpdir);
	fu_engine_save_remote_broken(tmpdir);
	fu_engine_save_remote_stable(tmpdir);
	fu_engine_save_remote_testing(tmpdir);

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* write a broken file */
	fn_broken = fu_temporary_directory_build(tmpdir, "broken.xml.gz", NULL);
	ret = g_file_set_contents(fn_broken, "this is not a valid", -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* write the main file */
	fn_stable = fu_temporary_directory_build(tmpdir, "stable.xml", NULL);
	ret = g_file_set_contents(
	    fn_stable,
	    "<components>"
	    "  <component type=\"firmware\">"
	    "    <id>test</id>"
	    "    <name>Test Device</name>"
	    "    <provides>"
	    "      <firmware type=\"flashed\">aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee</firmware>"
	    "    </provides>"
	    "    <releases>"
	    "      <release version=\"1.2.3\" date=\"2017-09-15\">"
	    "        <size type=\"installed\">123</size>"
	    "        <size type=\"download\">456</size>"
	    "        <location>https://test.org/foo.cab</location>"
	    "        <checksum filename=\"foo.cab\" target=\"container\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdead1111</checksum>"
	    "        <checksum filename=\"firmware.bin\" target=\"content\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
	    "      </release>"
	    "      <release version=\"1.2.2\" date=\"2017-09-01\">"
	    "        <size type=\"installed\">123</size>"
	    "        <size type=\"download\">456</size>"
	    "        <location>https://test.org/foo.cab</location>"
	    "        <checksum filename=\"foo.cab\" target=\"container\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdead2222</checksum>"
	    "        <checksum filename=\"firmware.bin\" target=\"content\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
	    "      </release>"
	    "    </releases>"
	    "  </component>"
	    "</components>",
	    -1,
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* write the extra file */
	fn_testing = fu_temporary_directory_build(tmpdir, "testing.xml", NULL);
	ret = g_file_set_contents(
	    fn_testing,
	    "<components>"
	    "  <component type=\"firmware\">"
	    "    <id>test</id>"
	    "    <name>Test Device</name>"
	    "    <provides>"
	    "      <firmware type=\"flashed\">aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee</firmware>"
	    "    </provides>"
	    "    <releases>"
	    "      <release version=\"1.2.5\" date=\"2017-09-16\">"
	    "        <size type=\"installed\">123</size>"
	    "        <size type=\"download\">456</size>"
	    "        <location>https://test.org/foo.cab</location>"
	    "        <checksum filename=\"foo.cab\" target=\"container\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdead3333</checksum>"
	    "        <checksum filename=\"firmware.bin\" target=\"content\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
	    "      </release>"
	    "      <release version=\"1.2.4\" date=\"2017-09-15\">"
	    "        <size type=\"installed\">123</size>"
	    "        <size type=\"download\">456</size>"
	    "        <location>https://test.org/foo.cab</location>"
	    "        <checksum filename=\"foo.cab\" target=\"container\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdead4444</checksum>"
	    "        <checksum filename=\"firmware.bin\" target=\"content\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
	    "      </release>"
	    "    </releases>"
	    "  </component>"
	    "</components>",
	    -1,
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_REMOTES | FU_ENGINE_LOAD_FLAG_NO_CACHE,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_test_assert_expected_messages();

	/* return all the remotes, even the broken one */
	remotes = fu_engine_get_remotes(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(remotes);
	g_assert_cmpint(remotes->len, ==, 3);

	/* ensure there are no devices already */
	devices_pre = fu_engine_get_devices(engine, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO);
	g_assert_null(devices_pre);
	g_clear_error(&error);

	/* add a device so we can get upgrades and downgrades */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.3");
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_name(device, "Test Device");
	fu_device_add_instance_id(device, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_engine_add_device(engine, device);
	devices = fu_engine_get_devices(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_SUPPORTED));
	g_assert_true(fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_REGISTERED));

	/* get the releases for one device */
	releases = fu_engine_get_releases(engine, request, fu_device_get_id(device), &error);
	g_assert_no_error(error);
	g_assert_nonnull(releases);
	g_assert_cmpint(releases->len, ==, 4);

	/* no upgrades, as no firmware is approved */
	releases_up = fu_engine_get_upgrades(engine, request, fu_device_get_id(device), &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO);
	g_assert_null(releases_up);
	g_clear_error(&error);

	/* retry with approved firmware set */
	fu_engine_add_approved_firmware(engine, "deadbeefdeadbeefdeadbeefdead1111");
	fu_engine_add_approved_firmware(engine, "deadbeefdeadbeefdeadbeefdead2222");
	fu_engine_add_approved_firmware(engine, "deadbeefdeadbeefdeadbeefdead3333");
	fu_engine_add_approved_firmware(engine, "deadbeefdeadbeefdeadbeefdead4444");
	fu_engine_add_approved_firmware(engine, "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX");

	/* upgrades */
	releases_up = fu_engine_get_upgrades(engine, request, fu_device_get_id(device), &error);
	g_assert_no_error(error);
	g_assert_nonnull(releases_up);
	g_assert_cmpint(releases_up->len, ==, 2);

	/* ensure the list is sorted */
	rel = FWUPD_RELEASE(g_ptr_array_index(releases_up, 0));
	g_assert_cmpstr(fwupd_release_get_version(rel), ==, "1.2.5");
	rel = FWUPD_RELEASE(g_ptr_array_index(releases_up, 1));
	g_assert_cmpstr(fwupd_release_get_version(rel), ==, "1.2.4");

	/* downgrades */
	releases_dg = fu_engine_get_downgrades(engine, request, fu_device_get_id(device), &error);
	g_assert_no_error(error);
	g_assert_nonnull(releases_dg);
	g_assert_cmpint(releases_dg->len, ==, 1);
	rel = FWUPD_RELEASE(g_ptr_array_index(releases_dg, 0));
	g_assert_cmpstr(fwupd_release_get_version(rel), ==, "1.2.2");

	/* enforce that updates have to be explicit */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_ONLY_EXPLICIT_UPDATES);
	releases_up2 = fu_engine_get_upgrades(engine, request, fu_device_get_id(device), &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO);
	g_assert_null(releases_up2);
}

static void
fu_engine_md_verfmt_func(void)
{
	FwupdRemote *remote;
	gboolean ret;
	g_autofree gchar *fn_stable = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("self-tests", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_METADATA, tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_CACHEDIR_PKG, tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_DATADIR_PKG, tmpdir);
	fu_engine_save_remote_stable(tmpdir);

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* write the main file */
	fn_stable = fu_temporary_directory_build(tmpdir, "stable.xml", NULL);
	ret = g_file_set_contents(
	    fn_stable,
	    "<components>"
	    "  <component type=\"firmware\">"
	    "    <id>test</id>"
	    "    <name>Test Device</name>"
	    "    <icon>computer</icon>"
	    "    <developer_name>ACME</developer_name>"
	    "    <provides>"
	    "      <firmware type=\"flashed\">aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee</firmware>"
	    "    </provides>"
	    "    <categories>"
	    "      <category>X-GraphicsTablet</category>"
	    "    </categories>"
	    "    <releases>"
	    "      <release version=\"1.2.3\" date=\"2017-09-15\">"
	    "        <size type=\"installed\">123</size>"
	    "        <size type=\"download\">456</size>"
	    "        <location>https://test.org/foo.cab</location>"
	    "        <checksum filename=\"foo.cab\" target=\"container\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
	    "        <checksum filename=\"firmware.bin\" target=\"content\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
	    "        <artifacts>"
	    "          <artifact type=\"binary\">"
	    "            <size type=\"installed\">1024</size>"
	    "            <size type=\"download\">2048</size>"
	    "          </artifact>"
	    "        </artifacts>"
	    "      </release>"
	    "    </releases>"
	    "    <custom>"
	    "      <value key=\"LVFS::VersionFormat\">triplet</value>"
	    "      <value key=\"LVFS::DeviceIntegrity\">signed</value>"
	    "      <value key=\"LVFS::DeviceFlags\">host-cpu,needs-shutdown</value>"
	    "    </custom>"
	    "  </component>"
	    "</components>",
	    -1,
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_REMOTES | FU_ENGINE_LOAD_FLAG_NO_CACHE,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_test_assert_expected_messages();

	/* pretend this has a signature */
	remote = fu_engine_get_remote_by_id(engine, "stable", &error);
	g_assert_no_error(error);
	g_assert_nonnull(remote);

	/* add a device with no defined version format */
	fu_device_set_version(device, "16908291");
	fu_device_set_version_raw(device, 0x01020003);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_NAME_CATEGORY);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_ICON);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_VENDOR);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_SIGNED);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_VERFMT);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_FLAGS);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_REQUIRED_FREE);
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_add_instance_id(device, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_engine_add_device(engine, device);

	/* ensure the version format was set from the metadata */
	g_assert_cmpint(fu_device_get_version_format(device), ==, FWUPD_VERSION_FORMAT_TRIPLET);
	g_assert_cmpstr(fu_device_get_version(device), ==, "1.2.3");
	g_assert_cmpstr(fu_device_get_name(device), ==, "Graphics Tablet");
	g_assert_cmpstr(fu_device_get_vendor(device), ==, "ACME");
	g_assert_true(fu_device_has_icon(device, "computer"));
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD));
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN));
	g_assert_true(fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_HOST_CPU));
	g_assert_cmpint(fu_device_get_required_free(device), ==, 1024);

	/* ensure the device was added */
	devices = fu_engine_get_devices(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_SUPPORTED));
	g_assert_true(fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_REGISTERED));

	/* ensure the releases are set */
	releases = fu_engine_get_releases(engine, request, fu_device_get_id(device), &error);
	g_assert_no_error(error);
	g_assert_nonnull(releases);
	g_assert_cmpint(releases->len, ==, 1);
}

static void
fu_engine_install_duration_func(void)
{
	FwupdRelease *rel;
	gboolean ret;
	g_autofree gchar *fn_stable = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("self-tests", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_METADATA, tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_CACHEDIR_PKG, tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_DATADIR_PKG, tmpdir);
	fu_engine_save_remote_stable(tmpdir);

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* write the main file */
	fn_stable = fu_temporary_directory_build(tmpdir, "stable.xml", NULL);
	ret = g_file_set_contents(
	    fn_stable,
	    "<components>"
	    "  <component type=\"firmware\">"
	    "    <id>test</id>"
	    "    <provides>"
	    "      <firmware type=\"flashed\">aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee</firmware>"
	    "    </provides>"
	    "    <releases>"
	    "      <release version=\"1.2.3\" date=\"2017-09-15\" install_duration=\"120\">"
	    "        <location>https://test.org/foo.cab</location>"
	    "        <checksum filename=\"foo.cab\" target=\"container\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
	    "        <checksum filename=\"firmware.bin\" target=\"content\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
	    "      </release>"
	    "    </releases>"
	    "  </component>"
	    "</components>",
	    -1,
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_REMOTES | FU_ENGINE_LOAD_FLAG_NO_CACHE,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a device so we can get the install duration */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.3");
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_add_instance_id(device, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
	fu_device_set_install_duration(device, 999);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_engine_add_device(engine, device);
	devices = fu_engine_get_devices(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_SUPPORTED));

	/* check the release install duration */
	releases = fu_engine_get_releases(engine, request, fu_device_get_id(device), &error);
	g_assert_no_error(error);
	g_assert_nonnull(releases);
	g_assert_cmpint(releases->len, ==, 1);
	rel = FWUPD_RELEASE(g_ptr_array_index(releases, 0));
	g_assert_cmpint(fwupd_release_get_install_duration(rel), ==, 120);
}

static void
fu_engine_release_dedupe_func(void)
{
	gboolean ret;
	g_autofree gchar *fn_stable = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("self-tests", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_METADATA, tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_CACHEDIR_PKG, tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_DATADIR_PKG, tmpdir);
	fu_engine_save_remote_stable(tmpdir);

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* write the main file */
	fn_stable = fu_temporary_directory_build(tmpdir, "stable.xml", NULL);
	ret = g_file_set_contents(
	    fn_stable,
	    "<components>"
	    "  <component type=\"firmware\">"
	    "    <id>test</id>"
	    "    <provides>"
	    "      <firmware type=\"flashed\">aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee</firmware>"
	    "    </provides>"
	    "    <releases>"
	    "      <release version=\"1.2.3\" date=\"2017-09-15\" install_duration=\"120\">"
	    "        <location>https://test.org/foo.cab</location>"
	    "        <checksum filename=\"foo.cab\" target=\"container\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
	    "        <checksum filename=\"firmware.bin\" target=\"content\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
	    "      </release>"
	    "      <release version=\"1.2.3\" date=\"2017-09-15\" install_duration=\"120\">"
	    "        <location>https://test.org/foo.cab</location>"
	    "        <checksum filename=\"foo.cab\" target=\"container\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
	    "        <checksum filename=\"firmware.bin\" target=\"content\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
	    "      </release>"
	    "    </releases>"
	    "  </component>"
	    "</components>",
	    -1,
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_REMOTES | FU_ENGINE_LOAD_FLAG_NO_CACHE,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a device so we can get the install duration */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.3");
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_add_instance_id(device, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
	fu_device_set_install_duration(device, 999);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_engine_add_device(engine, device);
	devices = fu_engine_get_devices(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_SUPPORTED));

	/* check the release install duration */
	releases = fu_engine_get_releases(engine, request, fu_device_get_id(device), &error);
	g_assert_no_error(error);
	g_assert_nonnull(releases);
	g_assert_cmpint(releases->len, ==, 1);
}

static void
fu_engine_history_convert_version_func(void)
{
	FuDevice *device_tmp;
	FwupdRelease *release_tmp;
	gboolean ret;
	g_autofree gchar *device_str = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuHistory) history = fu_history_new(ctx);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbSilo) silo = NULL;

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("engine-history-inherit", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_PKG, tmpdir);

	/* add the fake metadata */
	ret = xb_builder_source_load_xml(
	    source,
	    "<?xml version=\"1.0\"?>\n"
	    "<components>\n"
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.example.firmware</id>\n"
	    "  <provides>\n"
	    "    <firmware type=\"flashed\">b585990a-003e-5270-89d5-3705a17f9a43</firmware>\n"
	    "  </provides>\n"
	    "  <custom>\n"
	    "  </custom>\n"
	    "  <releases>\n"
	    "    <release id=\"1\" version=\"0x01020004\">\n"
	    "      <checksum type=\"sha1\" target=\"content\">abcd</checksum>\n"
	    "      <artifacts>\n"
	    "      </artifacts>\n"
	    "    </release>\n"
	    "  </releases>\n"
	    "</component>\n"
	    "</components>",
	    XB_BUILDER_SOURCE_FLAG_NONE,
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	fu_engine_set_silo(engine, silo);

	fu_device_set_id(device, "abc");
	fu_device_set_version(device, "1.2.3");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_AFFECTS_FDE);
	fu_release_set_appstream_id(release, "com.acme.example.firmware");
	fu_release_add_checksum(release, "abcd");
	fu_release_set_version(release, "1.2.4");

	ret = fu_history_remove_all(history, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_history_add_device(history, device, release, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* do not overwrite the history-saved 1.2.4 with the release-provided 0x01020004 */
	devices = fu_engine_get_history(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	device_tmp = g_ptr_array_index(devices, 0);

	device_str = fu_device_to_string(device_tmp);
	g_debug("%s", device_str);

	g_assert_cmpstr(fu_device_get_id(device_tmp),
			==,
			"a9993e364706816aba3e25717850c26c9cd0d89d");
	g_assert_cmpstr(fu_device_get_version(device_tmp), ==, "1.2.3");
	g_assert_true(fu_device_has_flag(device_tmp, FWUPD_DEVICE_FLAG_SUPPORTED));
	g_assert_true(fu_device_has_flag(device_tmp, FWUPD_DEVICE_FLAG_HISTORICAL));
	release_tmp = fu_device_get_release_default(device_tmp);
	g_assert_cmpstr(fwupd_release_get_version(release_tmp), ==, "1.2.4");
}

static void
fu_engine_history_func(void)
{
	gboolean ret;
	g_autofree gchar *checksum = NULL;
	g_autofree gchar *device_str_expected = NULL;
	g_autofree gchar *device_str = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuCabinet) cabinet = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device2 = NULL;
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuHistory) history = NULL;
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_test_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(FwupdDevice) device3 = NULL;
	g_autoptr(FwupdDevice) device4 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("engine-history", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_PKG, tmpdir);

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up dummy plugin */
	ret = fu_plugin_set_config_value(plugin, "AnotherWriteRequired", "true", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_engine_add_plugin(engine, plugin);

	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_NO_CACHE | FU_ENGINE_LOAD_FLAG_ALLOW_TEST_PLUGIN |
				 FU_ENGINE_LOAD_FLAG_HISTORY,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a device so we can get upgrade it */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.2");
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_name(device, "Test Device");
	fu_device_set_plugin(device, "test");
	fu_device_add_instance_id(device, "12345678-1234-1234-1234-123456789012");
	fu_device_add_checksum(device, "0123456789abcdef0123456789abcdef01234567");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_created_usec(device, 1515338000ull * G_USEC_PER_SEC);
	fu_engine_add_device(engine, device);
	devices = fu_engine_get_devices(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	g_assert_true(fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_REGISTERED));

	filename =
	    g_test_build_filename(G_TEST_BUILT, "tests", "missing-hwid", "noreqs-1.2.3.cab", NULL);
	stream = fu_input_stream_from_path(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	cabinet = fu_engine_build_cabinet_from_stream(engine, stream, &error);
	g_assert_no_error(error);
	g_assert_nonnull(cabinet);

	/* get component */
	component = fu_cabinet_get_component(cabinet, "com.hughski.test.firmware", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* set the counter */
	fu_device_set_metadata_integer(device, "nr-update", 0);

	/* install it */
	fu_release_set_device(release, device);
	ret = fu_release_load(release, cabinet, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_install_release(engine, release, progress, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check the write was done more than once */
	g_assert_cmpint(fu_device_get_metadata_integer(device, "nr-update"), ==, 2);

	/* check the history database */
	history = fu_history_new(ctx);
	device2 = fu_history_get_device_by_id(history, fu_device_get_id(device), &error);
	g_assert_no_error(error);
	g_assert_nonnull(device2);
	g_assert_cmpint(fu_device_get_update_state(device2), ==, FWUPD_UPDATE_STATE_SUCCESS);
	g_assert_cmpstr(fu_device_get_update_error(device2), ==, NULL);
	fu_device_set_modified_usec(device2, 1514338000ull * G_USEC_PER_SEC);
	g_hash_table_remove_all(fwupd_release_get_metadata(fu_device_get_release_default(device2)));
	device_str = fu_device_to_string(device2);
	checksum = fu_input_stream_compute_checksum(stream, G_CHECKSUM_SHA1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(checksum);
	device_str_expected =
	    g_strdup_printf("FuDevice:\n"
			    "  DeviceId:             894e8c17a29428b09d10cd90d1db74ea76fbcfe8\n"
			    "  Name:                 Test Device\n"
			    "  Guid:                 12345678-1234-1234-1234-123456789012\n"
			    "  Plugin:               test\n"
			    "  Flags:                updatable|historical|unsigned-payload\n"
			    "  Version:              1.2.2\n"
			    "  VersionFormat:        triplet\n"
			    "  Created:              2018-01-07 15:13:20\n"
			    "  Modified:             2017-12-27 01:26:40\n"
			    "  UpdateState:          success\n"
			    "  FuRelease:\n"
			    "    AppstreamId:        com.hughski.test.firmware\n"
			    "    Version:            1.2.3\n"
			    "    Checksum:           SHA1(%s)\n"
			    "    Flags:              trusted-payload|trusted-metadata\n"
			    "  InstanceId[vi]:       12345678-1234-1234-1234-123456789012\n"
			    "  AcquiesceDelay:       50\n",
			    checksum);
	g_debug("%s", device_str);
	ret = g_strcmp0(device_str, device_str_expected) == 0;
	g_assert_true(ret);

	/* GetResults() */
	device3 = fu_engine_get_results(engine, FWUPD_DEVICE_ID_ANY, &error);
	g_assert_no_error(error);
	g_assert_nonnull(device3);
	g_assert_cmpstr(fu_device_get_id(device3), ==, "894e8c17a29428b09d10cd90d1db74ea76fbcfe8");
	g_assert_cmpint(fu_device_get_update_state(device3), ==, FWUPD_UPDATE_STATE_SUCCESS);
	g_assert_cmpstr(fu_device_get_update_error(device3), ==, NULL);

	/* ClearResults() */
	ret = fu_engine_clear_results(engine, FWUPD_DEVICE_ID_ANY, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* GetResults() */
	device4 = fu_engine_get_results(engine, FWUPD_DEVICE_ID_ANY, &error);
	g_assert_null(device4);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO);
}

static void
fu_engine_history_verfmt_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_DPAUX_DEVICE, "context", ctx, NULL);
	g_autoptr(FuDevice) device_tmp = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuHistory) history = fu_history_new(ctx);
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_test_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuRelease) release_tmp = fu_release_new();
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("engine-history", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_PKG, tmpdir);

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up dummy plugin */
	fu_engine_add_plugin(engine, plugin);
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_NO_CACHE | FU_ENGINE_LOAD_FLAG_HISTORY |
				 FU_ENGINE_LOAD_FLAG_ALLOW_TEST_PLUGIN,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create history entry */
	fu_device_set_id(device_tmp, "test_device");
	fu_device_set_version_format(device_tmp, FWUPD_VERSION_FORMAT_TRIPLET);
	ret = fu_history_add_device(history, device_tmp, release_tmp, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* absorb version format from the database */
	fu_device_set_version_raw(device, 65563);
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_NUMBER);
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_plugin(device, "test");
	fu_device_add_instance_id(device, "12345678-1234-1234-1234-123456789012");
	fu_device_add_checksum(device, "0123456789abcdef0123456789abcdef01234567");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_VERFMT);
	fu_device_set_created_usec(device, 1515338000ull * G_USEC_PER_SEC);
	fu_engine_add_device(engine, device);
	g_assert_cmpint(fu_device_get_version_format(device), ==, FWUPD_VERSION_FORMAT_TRIPLET);
	g_assert_cmpstr(fu_device_get_version(device), ==, "0.1.27");
}

static void
fu_engine_install_loop_restart_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_test_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream_fw = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up dummy plugin */
	ret = fu_plugin_set_config_value(plugin, "InstallLoopRestart", "true", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_engine_add_plugin(engine, plugin);

	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_NO_CACHE | FU_ENGINE_LOAD_FLAG_ALLOW_TEST_PLUGIN,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a device so we can install it */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.2");
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_plugin(device, "test");
	fu_device_add_instance_id(device, "12345678-1234-1234-1234-123456789012");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_engine_add_device(engine, device);

	/* set up counters */
	fu_device_set_metadata_integer(device, "nr-update", 0);
	fu_device_set_metadata_integer(device, "nr-attach", 0);

	stream_fw = g_memory_input_stream_new_from_data((const guint8 *)"1.2.3", 5, NULL);
	fu_release_set_stream(release, stream_fw);
	ret = fu_engine_install_blob(engine,
				     device,
				     release,
				     progress,
				     FWUPD_INSTALL_FLAG_NO_HISTORY |
					 FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM,
				     FWUPD_FEATURE_FLAG_NONE,
				     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check we did two write loops */
	g_assert_cmpint(fu_device_get_metadata_integer(device, "nr-update"), ==, 2);

	/* check we only attached once */
	g_assert_cmpint(fu_device_get_metadata_integer(device, "nr-attach"), ==, 1);
}

static void
fu_engine_multiple_rels_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuCabinet) cabinet = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_test_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(GPtrArray) rels = NULL;
	g_autoptr(XbQuery) query = NULL;

	/* set up test harness */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSCONFDIR_PKG, testdatadir);

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up dummy plugin */
	fu_engine_add_plugin(engine, plugin);

	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_NO_CACHE | FU_ENGINE_LOAD_FLAG_ALLOW_TEST_PLUGIN,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a device so we can get upgrade it */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.2");
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_name(device, "Test Device");
	fu_device_set_plugin(device, "test");
	fu_device_add_instance_id(device, "12345678-1234-1234-1234-123456789012");
	fu_device_add_checksum(device, "0123456789abcdef0123456789abcdef01234567");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INSTALL_ALL_RELEASES);
	fu_device_set_created_usec(device, 1515338000ull * G_USEC_PER_SEC);
	fu_engine_add_device(engine, device);

	filename = g_test_build_filename(G_TEST_BUILT,
					 "tests",
					 "multiple-rels",
					 "multiple-rels-1.2.4.cab",
					 NULL);
	stream = fu_input_stream_from_path(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	cabinet = fu_engine_build_cabinet_from_stream(engine, stream, &error);
	g_assert_no_error(error);
	g_assert_nonnull(cabinet);

	/* get component */
	component = fu_cabinet_get_component(cabinet, "com.hughski.test.firmware", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* set up counters */
	fu_device_set_metadata_integer(device, "nr-update", 0);
	fu_device_set_metadata_integer(device, "nr-attach", 0);

	/* get all */
	query = xb_query_new_full(xb_node_get_silo(component),
				  "releases/release",
				  XB_QUERY_FLAG_FORCE_NODE_CACHE,
				  &error);
	g_assert_no_error(error);
	g_assert_nonnull(query);
	rels = xb_node_query_full(component, query, &error);
	g_assert_no_error(error);
	g_assert_nonnull(rels);

	releases = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (guint i = 0; i < rels->len; i++) {
		XbNode *rel = g_ptr_array_index(rels, i);
		g_autoptr(FuRelease) release = fu_release_new();
		fu_release_set_device(release, device);
		ret = fu_release_load(release,
				      cabinet,
				      component,
				      rel,
				      FWUPD_INSTALL_FLAG_NONE,
				      &error);
		g_assert_no_error(error);
		g_assert_true(ret);
		g_ptr_array_add(releases, g_object_ref(release));
	}

	/* install them */
	fu_progress_reset(progress);
	ret = fu_engine_install_releases(engine,
					 request,
					 releases,
					 cabinet,
					 progress,
					 FWUPD_INSTALL_FLAG_NONE,
					 &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check we did 1.2.2 -> 1.2.3 -> 1.2.4 */
	g_assert_cmpint(fu_device_get_metadata_integer(device, "nr-update"), ==, 2);
	g_assert_cmpint(fu_device_get_metadata_integer(device, "nr-attach"), ==, 2);
	g_assert_cmpstr(fu_device_get_version(device), ==, "1.2.4");
}

static void
fu_engine_history_inherit(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuCabinet) cabinet = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_test_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("engine-history-inherit", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_PKG, tmpdir);

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up dummy plugin */
	ret = fu_plugin_set_config_value(plugin, "NeedsActivation", "true", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_engine_add_plugin(engine, plugin);
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_NO_CACHE | FU_ENGINE_LOAD_FLAG_HISTORY |
				 FU_ENGINE_LOAD_FLAG_ALLOW_TEST_PLUGIN,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a device so we can get upgrade it */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.2");
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_name(device, "Test Device");
	fu_device_set_plugin(device, "test");
	fu_device_add_instance_id(device, "12345678-1234-1234-1234-123456789012");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_created_usec(device, 1515338000ull * G_USEC_PER_SEC);
	fu_engine_add_device(engine, device);
	devices = fu_engine_get_devices(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	g_assert_true(fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_REGISTERED));

	filename =
	    g_test_build_filename(G_TEST_BUILT, "tests", "missing-hwid", "noreqs-1.2.3.cab", NULL);
	stream = fu_input_stream_from_path(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	cabinet = fu_engine_build_cabinet_from_stream(engine, stream, &error);
	g_assert_no_error(error);
	g_assert_nonnull(cabinet);

	/* get component */
	component = fu_cabinet_get_component(cabinet, "com.hughski.test.firmware", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* install it */
	fu_release_set_device(release, device);
	ret = fu_release_load(release, cabinet, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_install_release(engine, release, progress, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check the device requires an activation */
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION));
	g_assert_cmpstr(fu_device_get_version(device), ==, "1.2.2");

	/* activate the device */
	fu_progress_reset(progress);
	ret = fu_engine_activate(engine, fu_device_get_id(device), progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check the device no longer requires an activation */
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION));
	g_assert_cmpstr(fu_device_get_version(device), ==, "1.2.3");

	/* emulate getting the flag for a fresh boot on old firmware */
	fu_progress_reset(progress);
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.2");
	ret = fu_engine_install_release(engine, release, progress, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_object_unref(engine);
	g_object_unref(device);
	engine = fu_engine_new(ctx);
	fu_engine_set_silo(engine, silo_empty);
	fu_engine_add_plugin(engine, plugin);
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_NO_CACHE | FU_ENGINE_LOAD_FLAG_HISTORY,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	device = fu_device_new(ctx);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_INHERIT_ACTIVATION);
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_name(device, "Test Device");
	fu_device_add_instance_id(device, "12345678-1234-1234-1234-123456789012");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.2");
	fu_engine_add_device(engine, device);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION));

	/* emulate not getting the flag */
	g_object_unref(engine);
	g_object_unref(device);
	engine = fu_engine_new(ctx);
	fu_engine_set_silo(engine, silo_empty);
	fu_engine_add_plugin(engine, plugin);
	device = fu_device_new(ctx);
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_name(device, "Test Device");
	fu_device_add_instance_id(device, "12345678-1234-1234-1234-123456789012");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.2");
	fu_engine_add_device(engine, device);
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION));
}

static void
fu_engine_install_needs_reboot(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuCabinet) cabinet = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_test_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();
	g_autofree gchar *reboot_file = NULL;

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("engine-needs-reboot", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_RUNDIR, tmpdir);

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up dummy plugin */
	ret = fu_plugin_set_config_value(plugin, "NeedsReboot", "true", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_engine_add_plugin(engine, plugin);
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_NO_CACHE | FU_ENGINE_LOAD_FLAG_ALLOW_TEST_PLUGIN,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a device so we can get upgrade it */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.2");
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_name(device, "Test Device");
	fu_device_set_plugin(device, "test");
	fu_device_add_instance_id(device, "12345678-1234-1234-1234-123456789012");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_created_usec(device, 1515338000ull * G_USEC_PER_SEC);
	fu_engine_add_device(engine, device);
	devices = fu_engine_get_devices(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	g_assert_true(fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_REGISTERED));

	filename =
	    g_test_build_filename(G_TEST_BUILT, "tests", "missing-hwid", "noreqs-1.2.3.cab", NULL);
	stream = fu_input_stream_from_path(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	cabinet = fu_engine_build_cabinet_from_stream(engine, stream, &error);
	g_assert_no_error(error);
	g_assert_nonnull(cabinet);

	/* get component */
	component = fu_cabinet_get_component(cabinet, "com.hughski.test.firmware", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* install it */
	fu_release_set_device(release, device);
	ret = fu_release_load(release, cabinet, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_install_release(engine, release, progress, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check the device requires reboot */
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT));
	g_assert_cmpstr(fu_device_get_version(device), ==, "1.2.2");

	reboot_file = fu_temporary_directory_build(tmpdir, "reboot-required", NULL);
	g_assert_true(g_file_test(reboot_file, G_FILE_TEST_EXISTS));
}

typedef struct {
	guint request_cnt;
	FwupdStatus last_status;
} FuTestRequestHelper;

static void
fu_test_engine_status_changed_cb(FuProgress *progress, FwupdStatus status, gpointer user_data)
{
	FuTestRequestHelper *helper = (FuTestRequestHelper *)user_data;
	g_debug("status now %s", fwupd_status_to_string(status));
	helper->last_status = status;
}

static void
fu_test_engine_request_cb(FuEngine *engine, FwupdRequest *request, gpointer user_data)
{
	FuTestRequestHelper *helper = (FuTestRequestHelper *)user_data;
	g_assert_cmpint(fwupd_request_get_kind(request), ==, FWUPD_REQUEST_KIND_IMMEDIATE);
	g_assert_cmpstr(fwupd_request_get_id(request), ==, FWUPD_REQUEST_ID_REMOVE_REPLUG);
	g_assert_true(fwupd_request_has_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE));
	g_assert_nonnull(fwupd_request_get_message(request));
	g_assert_cmpint(helper->last_status, ==, FWUPD_STATUS_WAITING_FOR_USER);
	helper->request_cnt++;
}

static void
fu_engine_install_request(void)
{
	FuTestRequestHelper helper = {.request_cnt = 0, .last_status = FWUPD_STATUS_UNKNOWN};
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuCabinet) cabinet = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_test_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up dummy plugin */
	ret = fu_plugin_set_config_value(plugin, "RequestSupported", "true", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_engine_add_plugin(engine, plugin);
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_NO_CACHE | FU_ENGINE_LOAD_FLAG_ALLOW_TEST_PLUGIN,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a device so we can get upgrade it */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.2");
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_name(device, "Test Device");
	fu_device_set_plugin(device, "test");
	fu_device_add_instance_id(device, "12345678-1234-1234-1234-123456789012");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_request_flag(device, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	fu_device_set_created_usec(device, 1515338000ull * G_USEC_PER_SEC);
	fu_engine_add_device(engine, device);
	devices = fu_engine_get_devices(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	g_assert_true(fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_REGISTERED));

	filename =
	    g_test_build_filename(G_TEST_BUILT, "tests", "missing-hwid", "noreqs-1.2.3.cab", NULL);
	stream = fu_input_stream_from_path(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	cabinet = fu_engine_build_cabinet_from_stream(engine, stream, &error);
	g_assert_no_error(error);
	g_assert_nonnull(cabinet);

	/* get component */
	component = fu_cabinet_get_component(cabinet, "com.hughski.test.firmware", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* install it */
	fu_release_set_device(release, device);
	ret = fu_release_load(release, cabinet, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_release_get_firmware_basename(release), ==, "firmware.bin");
	g_assert_cmpstr(fu_release_get_version(release), ==, "1.2.3");

	g_signal_connect(FU_ENGINE(engine),
			 "device-request",
			 G_CALLBACK(fu_test_engine_request_cb),
			 &helper);
	g_signal_connect(FU_PROGRESS(progress),
			 "status-changed",
			 G_CALLBACK(fu_test_engine_status_changed_cb),
			 &helper);

	ret = fu_engine_install_release(engine, release, progress, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(helper.request_cnt, ==, 1);
	g_assert_cmpint(helper.last_status, ==, FWUPD_STATUS_DEVICE_BUSY);
}

static void
fu_engine_history_error_func(void)
{
	gboolean ret;
	g_autofree gchar *checksum = NULL;
	g_autofree gchar *device_str_expected = NULL;
	g_autofree gchar *device_str = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuCabinet) cabinet = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device2 = NULL;
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuHistory) history = NULL;
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_test_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error2 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("engine-history-error", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_PKG, tmpdir);

	/* set up dummy plugin */
	ret = fu_plugin_set_config_value(plugin, "WriteSupported", "false", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_engine_add_plugin(engine, plugin);
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_NO_CACHE | FU_ENGINE_LOAD_FLAG_HISTORY |
				 FU_ENGINE_LOAD_FLAG_ALLOW_TEST_PLUGIN,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a device so we can get upgrade it */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.2");
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_name(device, "Test Device");
	fu_device_set_plugin(device, "test");
	fu_device_add_instance_id(device, "12345678-1234-1234-1234-123456789012");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_created_usec(device, 1515338000ull * G_USEC_PER_SEC);
	fu_engine_add_device(engine, device);
	devices = fu_engine_get_devices(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	g_assert_true(fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_REGISTERED));

	/* install the wrong thing */
	filename =
	    g_test_build_filename(G_TEST_BUILT, "tests", "missing-hwid", "noreqs-1.2.3.cab", NULL);
	stream = fu_input_stream_from_path(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	cabinet = fu_engine_build_cabinet_from_stream(engine, stream, &error);
	g_assert_no_error(error);
	g_assert_nonnull(cabinet);
	component = fu_cabinet_get_component(cabinet, "com.hughski.test.firmware", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);
	fu_release_set_device(release, device);
	ret = fu_release_load(release, cabinet, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_install_release(engine, release, progress, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_cmpstr(error->message,
			==,
			"failed to write-firmware: device was not in supported mode");
	g_assert_false(ret);

	/* check the history database */
	history = fu_history_new(ctx);
	device2 = fu_history_get_device_by_id(history, fu_device_get_id(device), &error2);
	g_assert_no_error(error2);
	g_assert_nonnull(device2);
	g_assert_cmpint(fu_device_get_update_state(device2), ==, FWUPD_UPDATE_STATE_FAILED);
	g_assert_cmpstr(fu_device_get_update_error(device2), ==, error->message);
	g_clear_error(&error);
	fu_device_set_modified_usec(device2, 1514338000ull * G_USEC_PER_SEC);
	g_hash_table_remove_all(fwupd_release_get_metadata(fu_device_get_release_default(device2)));
	device_str = fu_device_to_string(device2);
	checksum = fu_input_stream_compute_checksum(stream, G_CHECKSUM_SHA1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(checksum);
	device_str_expected = g_strdup_printf(
	    "FuDevice:\n"
	    "  DeviceId:             894e8c17a29428b09d10cd90d1db74ea76fbcfe8\n"
	    "  Name:                 Test Device\n"
	    "  Guid:                 12345678-1234-1234-1234-123456789012\n"
	    "  Plugin:               test\n"
	    "  Flags:                updatable|historical|unsigned-payload\n"
	    "  Version:              1.2.2\n"
	    "  VersionFormat:        triplet\n"
	    "  Created:              2018-01-07 15:13:20\n"
	    "  Modified:             2017-12-27 01:26:40\n"
	    "  UpdateState:          failed\n"
	    "  UpdateError:          failed to write-firmware: device was not in supported mode\n"
	    "  FuRelease:\n"
	    "    AppstreamId:        com.hughski.test.firmware\n"
	    "    Version:            1.2.3\n"
	    "    Checksum:           SHA1(%s)\n"
	    "    Flags:              trusted-payload|trusted-metadata\n"
	    "  InstanceId[vi]:       12345678-1234-1234-1234-123456789012\n"
	    "  AcquiesceDelay:       50\n",
	    checksum);
	g_debug("%s", device_str);
	ret = g_strcmp0(device_str, device_str_expected) == 0;
	g_assert_true(ret);
}

static void
fu_engine_device_better_than_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device1 = fu_device_new(ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(ctx);
	g_autoptr(FuDevice) device_best = NULL;
	g_autoptr(FuDevice) device_replug = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuPlugin) plugin1 = fu_plugin_new(ctx);
	g_autoptr(FuPlugin) plugin2 = fu_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	/* add a bad plugin */
	fu_plugin_set_name(plugin2, "plugin2");
	fu_engine_add_plugin(engine, plugin2);

	/* add a good plugin */
	fu_plugin_set_name(plugin1, "plugin1");
	fu_plugin_add_rule(plugin1, FU_PLUGIN_RULE_BETTER_THAN, "plugin2");
	fu_engine_add_plugin(engine, plugin1);

	/* load the daemon */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a higher priority device */
	fu_device_set_id(device1, "87ea5dfc8b8e384d848979496e706390b497e547");
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_instance_id(device1, "12345678-1234-1234-1234-123456789012");
	fu_device_add_protocol(device1, "com.acme");
	fu_device_set_remove_delay(device1, FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_plugin_add_device(plugin1, device1);

	/* should be ignored */
	fu_device_set_id(device2, "87ea5dfc8b8e384d848979496e706390b497e547");
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_instance_id(device2, "12345678-1234-1234-1234-123456789012");
	fu_device_add_protocol(device2, "com.acme");
	fu_plugin_add_device(plugin2, device2);

	/* ensure we still have device1 */
	device_best =
	    fu_engine_get_device(engine, "87ea5dfc8b8e384d848979496e706390b497e547", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device_best);
	g_assert_true(device_best == device1);

	/* should be replaced */
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	fu_plugin_add_device(plugin2, device2);

	/* ensure we now have device2 */
	device_replug =
	    fu_engine_get_device(engine, "87ea5dfc8b8e384d848979496e706390b497e547", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device_replug);
	g_assert_true(device_replug == device2);
}

static void
fu_test_plugin_device_added_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	FuDevice **dev = (FuDevice **)user_data;
	*dev = g_object_ref(device);
	fu_test_loop_quit();
}

static void
fu_test_plugin_device_register_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	/* fake being a daemon */
	fu_plugin_runner_device_register(plugin, device);
}

static void
fu_engine_plugin_module_func(void)
{
	GError *error = NULL;
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_test_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	/* load dummy hwids */
	ret = fu_context_load_hwinfo(ctx,
				     progress,
				     FU_CONTEXT_HWID_FLAG_LOAD_CONFIG |
					 FU_ENGINE_LOAD_FLAG_ALLOW_TEST_PLUGIN,
				     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create a fake device */
	ret = fu_plugin_set_config_value(plugin, "RegistrationSupported", "true", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_signal_connect(FU_PLUGIN(plugin),
			 "device-added",
			 G_CALLBACK(fu_test_plugin_device_added_cb),
			 &device);
	g_signal_connect(FU_PLUGIN(plugin),
			 "device-register",
			 G_CALLBACK(fu_test_plugin_device_register_cb),
			 &device);
	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check we did the right thing */
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_device_get_id(device), ==, "08d460be0f1f9f128413f816022a6439e0078018");
	g_assert_cmpstr(fu_device_get_version_lowest(device), ==, "1.2.0");
	g_assert_cmpstr(fu_device_get_version(device), ==, "1.2.2");
	g_assert_cmpstr(fu_device_get_version_bootloader(device), ==, "0.1.2");
	g_assert_cmpstr(fu_device_get_guid_default(device),
			==,
			"b585990a-003e-5270-89d5-3705a17f9a43");
	g_assert_cmpstr(fu_device_get_name(device), ==, "Integrated Webcam™");
	g_signal_handlers_disconnect_by_data(plugin, &device);
}

static GBytes *
fu_test_build_cab(gboolean compressed, ...)
{
	gboolean ret;
	va_list args;
	g_autoptr(FuCabFirmware) cabinet = fu_cab_firmware_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) cabinet_blob = NULL;

	fu_cab_firmware_set_compressed(cabinet, compressed);

	/* add each file */
	va_start(args, compressed);
	do {
		const gchar *fn;
		const gchar *text;
		g_autoptr(FuCabImage) img = fu_cab_image_new();
		g_autoptr(GBytes) blob = NULL;

		/* get filename */
		fn = va_arg(args, const gchar *);
		if (fn == NULL)
			break;

		/* get contents */
		text = va_arg(args, const gchar *);
		if (text == NULL)
			break;
		g_debug("creating %s with %s", fn, text);

		/* add a GCabFile to the cabinet */
		blob = g_bytes_new_static(text, strlen(text));
		fu_firmware_set_id(FU_FIRMWARE(img), fn);
		fu_firmware_set_bytes(FU_FIRMWARE(img), blob);
		ret = fu_firmware_add_image(FU_FIRMWARE(cabinet), FU_FIRMWARE(img), &error);
		g_assert_no_error(error);
		g_assert_true(ret);
	} while (TRUE);
	va_end(args);

	/* write the archive to a blob */
	cabinet_blob = fu_firmware_write(FU_FIRMWARE(cabinet), &error);
	g_assert_no_error(error);
	g_assert_nonnull(cabinet_blob);
	return g_steal_pointer(&cabinet_blob);
}

static void
fu_test_plugin_composite_device_added_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	GPtrArray *devices = (GPtrArray *)user_data;
	g_ptr_array_add(devices, g_object_ref(device));
}

static gint
fu_plugin_composite_release_sort_cb(gconstpointer a, gconstpointer b)
{
	FuRelease *release1 = *((FuRelease **)a);
	FuRelease *release2 = *((FuRelease **)b);
	FuDevice *device1 = fu_release_get_device(release1);
	FuDevice *device2 = fu_release_get_device(release2);
	if (fu_device_get_order(device1) < fu_device_get_order(device2))
		return 1;
	if (fu_device_get_order(device1) > fu_device_get_order(device2))
		return -1;
	return 0;
}

static void
fu_engine_plugin_composite_func(void)
{
	FuDevice *dev_tmp;
	GError *error = NULL;
	gboolean ret;
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_test_plugin_get_type(), ctx);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GPtrArray) components = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) releases =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* load engine */
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_NO_CACHE | FU_ENGINE_LOAD_FLAG_ALLOW_TEST_PLUGIN,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create CAB file */
	blob = fu_test_build_cab(
	    FALSE,
	    "acme.metainfo.xml",
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.example.firmware</id>\n"
	    "  <provides>\n"
	    "    <firmware type=\"flashed\">b585990a-003e-5270-89d5-3705a17f9a43</firmware>\n"
	    "  </provides>\n"
	    "  <releases>\n"
	    "    <release version=\"1.2.3\"/>\n"
	    "  </releases>\n"
	    "</component>",
	    "acme.module1.metainfo.xml",
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.example.firmware.module1</id>\n"
	    "  <provides>\n"
	    "    <firmware type=\"flashed\">7fddead7-12b5-4fb9-9fa0-6d30305df755</firmware>\n"
	    "  </provides>\n"
	    "  <releases>\n"
	    "    <release version=\"2\"/>\n"
	    "  </releases>\n"
	    "  <custom>\n"
	    "    <value key=\"LVFS::VersionFormat\">plain</value>\n"
	    "  </custom>\n"
	    "</component>",
	    "acme.module2.metainfo.xml",
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.example.firmware.module2</id>\n"
	    "  <provides>\n"
	    "    <firmware type=\"flashed\">b8fe6b45-8702-4bcd-8120-ef236caac76f</firmware>\n"
	    "  </provides>\n"
	    "  <releases>\n"
	    "    <release version=\"11\"/>\n"
	    "  </releases>\n"
	    "  <custom>\n"
	    "    <value key=\"LVFS::VersionFormat\">plain</value>\n"
	    "  </custom>\n"
	    "</component>",
	    "firmware.bin",
	    "world",
	    NULL);
	ret = fu_firmware_parse_bytes(FU_FIRMWARE(cabinet),
				      blob,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_CACHE_BLOB,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	components = fu_cabinet_get_components(cabinet, &error);
	g_assert_no_error(error);
	g_assert_nonnull(components);
	g_assert_cmpint(components->len, ==, 3);

	/* set up dummy plugin */
	ret = fu_plugin_set_config_value(plugin, "CompositeChild", "true", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_engine_add_plugin(engine, plugin);

	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	g_signal_connect(FU_PLUGIN(plugin),
			 "device-added",
			 G_CALLBACK(fu_test_plugin_composite_device_added_cb),
			 devices);

	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check we found all composite devices  */
	g_assert_cmpint(devices->len, ==, 3);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		fu_engine_add_device(engine, device);
		if (g_strcmp0(fu_device_get_id(device),
			      "08d460be0f1f9f128413f816022a6439e0078018") == 0) {
			g_assert_cmpstr(fu_device_get_version(device), ==, "1.2.2");
		} else if (g_strcmp0(fu_device_get_id(device),
				     "c0a0a4aa6480ac28eea1ce164fbb466ca934e1ff") == 0) {
			g_assert_cmpstr(fu_device_get_version(device), ==, "1");
			g_assert_nonnull(fu_device_get_parent_internal(device));
		} else if (g_strcmp0(fu_device_get_id(device),
				     "bf455e9f371d2608d1cb67660fd2b335d3f6ef73") == 0) {
			g_assert_cmpstr(fu_device_get_version(device), ==, "10");
			g_assert_nonnull(fu_device_get_parent_internal(device));
		}
	}

	/* produce install tasks */
	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index(components, i);

		/* do any devices pass the requirements */
		for (guint j = 0; j < devices->len; j++) {
			FuDevice *device = g_ptr_array_index(devices, j);
			g_autoptr(FuRelease) release = fu_release_new();
			g_autoptr(GError) error_local = NULL;

			/* is this component valid for the device */
			fu_release_set_device(release, device);
			fu_release_set_request(release, request);
			if (!fu_release_load(release,
					     cabinet,
					     component,
					     NULL,
					     FWUPD_INSTALL_FLAG_NONE,
					     &error_local)) {
				g_debug("requirement on %s:%s failed: %s",
					fu_device_get_id(device),
					xb_node_query_text(component, "id", NULL),
					error_local->message);
				continue;
			}

			g_ptr_array_add(releases, g_steal_pointer(&release));
		}
	}
	g_assert_cmpint(releases->len, ==, 3);

	/* sort these by version, forcing fu_engine_install_releases() to sort by device order */
	g_ptr_array_sort(releases, fu_plugin_composite_release_sort_cb);
	dev_tmp = fu_release_get_device(FU_RELEASE(g_ptr_array_index(releases, 0)));
	g_assert_cmpstr(fu_device_get_logical_id(dev_tmp), ==, "child1");
	dev_tmp = fu_release_get_device(FU_RELEASE(g_ptr_array_index(releases, 1)));
	g_assert_cmpstr(fu_device_get_logical_id(dev_tmp), ==, "child2");
	dev_tmp = fu_release_get_device(FU_RELEASE(g_ptr_array_index(releases, 2)));
	g_assert_cmpstr(fu_device_get_logical_id(dev_tmp), ==, NULL);

	/* install the cab */
	ret = fu_engine_install_releases(engine,
					 request,
					 releases,
					 cabinet,
					 progress,
					 FWUPD_INSTALL_FLAG_NONE,
					 &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* verify we installed the parent first */
	dev_tmp = fu_release_get_device(FU_RELEASE(g_ptr_array_index(releases, 0)));
	g_assert_cmpstr(fu_device_get_logical_id(dev_tmp), ==, NULL);
	dev_tmp = fu_release_get_device(FU_RELEASE(g_ptr_array_index(releases, 1)));
	g_assert_cmpstr(fu_device_get_logical_id(dev_tmp), ==, "child2");
	dev_tmp = fu_release_get_device(FU_RELEASE(g_ptr_array_index(releases, 2)));
	g_assert_cmpstr(fu_device_get_logical_id(dev_tmp), ==, "child1");

	/* verify everything upgraded */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		const gchar *metadata;
		if (g_strcmp0(fu_device_get_id(device),
			      "08d460be0f1f9f128413f816022a6439e0078018") == 0) {
			g_assert_cmpstr(fu_device_get_version(device), ==, "1.2.3");
		} else if (g_strcmp0(fu_device_get_id(device),
				     "c0a0a4aa6480ac28eea1ce164fbb466ca934e1ff") == 0) {
			g_assert_cmpstr(fu_device_get_version(device), ==, "2");
		} else if (g_strcmp0(fu_device_get_id(device),
				     "bf455e9f371d2608d1cb67660fd2b335d3f6ef73") == 0) {
			g_assert_cmpstr(fu_device_get_version(device), ==, "11");
		}

		/* verify prepare and cleanup ran on all devices */
		metadata = fu_device_get_metadata(device, "frimbulator");
		g_assert_cmpstr(metadata, ==, "1");
		metadata = fu_device_get_metadata(device, "frombulator");
		g_assert_cmpstr(metadata, ==, "1");
	}
}

static void
fu_engine_plugin_composite_multistep_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_test_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbSilo) silo = NULL;

	/* load engine to get FuConfig set up */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add the fake metadata */
	ret = xb_builder_source_load_xml(
	    source,
	    "<?xml version=\"1.0\"?>\n"
	    "<components>\n"
	    "<component type=\"firmware\">\n"
	    "  <provides>\n"
	    "    <firmware type=\"flashed\">b585990a-003e-5270-89d5-3705a17f9a43</firmware>\n"
	    "  </provides>\n"
	    "  <custom>\n"
	    "    <value key=\"LVFS::VersionFormat\">triplet</value>\n"
	    "    <value key=\"LVFS::UpdateProtocol\">com.acme.test</value>\n"
	    "  </custom>\n"
	    "  <releases>\n"
	    "    <release id=\"1\" version=\"1.2.3\">\n"
	    "      <checksum type=\"sha1\" target=\"content\">aaa</checksum>\n"
	    "      <artifacts>\n"
	    "        <artifact type=\"binary\">\n"
	    "          <location>file://filename.cab</location>\n"
	    "          <checksum type=\"sha1\">ccc</checksum>\n"
	    "        </artifact>\n"
	    "      </artifacts>\n"
	    "    </release>\n"
	    "    <release id=\"1\" version=\"1.2.4\">\n"
	    "      <checksum type=\"sha1\" target=\"content\">bbb</checksum>\n"
	    "      <artifacts>\n"
	    "        <artifact type=\"binary\">\n"
	    "          <location>file://filename.cab</location>\n"
	    "          <checksum type=\"sha1\">ccc</checksum>\n"
	    "        </artifact>\n"
	    "      </artifacts>\n"
	    "    </release>\n"
	    "  </releases>\n"
	    "</component>\n"
	    "</components>",
	    XB_BUILDER_SOURCE_FLAG_NONE,
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	fu_engine_set_silo(engine, silo);

	/* set up dummy plugin */
	fu_engine_add_plugin(engine, plugin);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	g_signal_connect(FU_PLUGIN(plugin),
			 "device-added",
			 G_CALLBACK(fu_test_plugin_composite_device_added_cb),
			 devices);
	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add all the found devices  */
	g_assert_cmpint(devices->len, ==, 1);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device_tmp = g_ptr_array_index(devices, i);
		fu_engine_add_device(engine, device_tmp);
	}

	/* check we did not dedupe the composite cab */
	device = fu_engine_get_device(engine, "08d460be0f1f9f128413f816022a6439e0078018", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	releases = fu_engine_get_releases(engine,
					  request,
					  "08d460be0f1f9f128413f816022a6439e0078018",
					  &error);
	g_assert_no_error(error);
	g_assert_nonnull(releases);
	g_assert_cmpint(releases->len, ==, 2);
}

static void
fu_plugin_engine_get_results_appstream_id_func(void)
{
	gboolean ret;
	FwupdRelease *release_default;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device_tmp = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuHistory) history = fu_history_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(FwupdDevice) device = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbSilo) silo = NULL;

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("self-tests", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_DATADIR_PKG, testdatadir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_PKG, tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_METADATA, tmpdir);

	/* load engine to get FuConfig set up */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add the fake metadata */
	ret = xb_builder_source_load_xml(
	    source,
	    "<?xml version=\"1.0\"?>\n"
	    "<components>\n"
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.WRONGDEVICE.firmware</id>\n"
	    "  <provides>\n"
	    "    <firmware type=\"flashed\">00000000-0000-0000-0000-000000000000</firmware>\n"
	    "  </provides>\n"
	    "  <custom>\n"
	    "    <value key=\"LVFS::VersionFormat\">triplet</value>\n"
	    "    <value key=\"LVFS::UpdateProtocol\">com.acme.test</value>\n"
	    "  </custom>\n"
	    "  <releases>\n"
	    "    <release version=\"1.2.3\">\n"
	    "      <checksum type=\"sha1\" target=\"content\">aaa</checksum>\n"
	    "      <artifacts>\n"
	    "        <artifact type=\"binary\">\n"
	    "        <checksum type=\"sha1\">7c211433f02071597741e6ff5a8ea34789abbf43</checksum>\n"
	    "        </artifact>\n"
	    "      </artifacts>\n"
	    "    </release>\n"
	    "  </releases>\n"
	    "</component>\n"
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.example.firmware</id>\n"
	    "  <provides>\n"
	    "    <firmware type=\"flashed\">b585990a-003e-5270-89d5-3705a17f9a43</firmware>\n"
	    "  </provides>\n"
	    "  <custom>\n"
	    "    <value key=\"LVFS::VersionFormat\">triplet</value>\n"
	    "    <value key=\"LVFS::UpdateProtocol\">com.acme.test</value>\n"
	    "  </custom>\n"
	    "  <releases>\n"
	    "    <release version=\"1.2.3\">\n"
	    "      <checksum type=\"sha1\" target=\"content\">aaa</checksum>\n"
	    "      <artifacts>\n"
	    "        <artifact type=\"binary\">\n"
	    "        <checksum type=\"sha1\">7c211433f02071597741e6ff5a8ea34789abbf43</checksum>\n"
	    "        </artifact>\n"
	    "      </artifacts>\n"
	    "    </release>\n"
	    "  </releases>\n"
	    "</component>\n"
	    "</components>",
	    XB_BUILDER_SOURCE_FLAG_NONE,
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	fu_engine_set_silo(engine, silo);

	/* add a dummy device  */
	fu_device_set_id(device_tmp, "08d460be0f1f9f128413f816022a6439e0078018");
	fu_engine_add_device(engine, device_tmp);
	fu_release_set_appstream_id(release, "com.acme.example.firmware");
	fu_release_add_checksum(release, "7c211433f02071597741e6ff5a8ea34789abbf43");
	fu_device_add_release(device_tmp, FWUPD_RELEASE(release));
	fu_device_set_update_state(device_tmp, FWUPD_UPDATE_STATE_SUCCESS);
	ret = fu_history_add_device(history, device_tmp, release, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check we got the correct component */
	device = fu_engine_get_results(engine, "08d460be0f1f9f128413f816022a6439e0078018", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	release_default = fu_device_get_release_default(device);
	g_assert_nonnull(release_default);
	g_assert_cmpstr(fwupd_release_get_appstream_id(release_default),
			==,
			"com.acme.example.firmware");
}

static void
fu_engine_modify_bios_settings_func(void)
{
	gboolean ret;
	const gchar *current;
	FwupdBiosSetting *attr1;
	FwupdBiosSetting *attr2;
	FwupdBiosSetting *attr3;
	FwupdBiosSetting *attr4;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuBiosSettings) attrs = NULL;
	g_autoptr(GPtrArray) items = NULL;
	g_autoptr(GHashTable) bios_settings =
	    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

#ifdef _WIN32
	g_test_skip("BIOS settings not supported on Windows");
	return;
#endif

	/* load contrived attributes */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", "bios-attrs", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_FW_ATTRIB, testdatadir);

	ret = fu_context_reload_bios_settings(ctx, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	attrs = fu_context_get_bios_settings(ctx);
	items = fu_bios_settings_get_all(attrs);
	g_assert_cmpint(items->len, ==, 4);

	/* enumeration */
	attr1 = fu_context_get_bios_setting(ctx, "com.fwupd-internal.Absolute");
	g_assert_nonnull(attr1);

	current = fwupd_bios_setting_get_current_value(attr1);
	g_assert_nonnull(current);

	g_hash_table_insert(bios_settings, g_strdup("Absolute"), g_strdup("Disabled"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO);
	g_assert_false(ret);
	g_clear_error(&error);

	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings, g_strdup("Absolute"), g_strdup("Enabled"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings, g_strdup("Absolute"), g_strdup("off"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings, g_strdup("Absolute"), g_strdup("FOO"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	/* use BiosSettingId instead */
	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings, g_strdup("com.fwupd-internal.Absolute"), g_strdup("on"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings,
			    g_strdup("com.fwupd-internal.Absolute"),
			    g_strdup("off"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* string */
	attr2 = fu_context_get_bios_setting(ctx, "com.fwupd-internal.Asset");
	g_assert_nonnull(attr2);

	current = fwupd_bios_setting_get_current_value(attr2);
	g_assert_nonnull(current);

	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings, g_strdup("Asset"), g_strdup("0"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings, g_strdup("Asset"), g_strdup("1"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(
	    bios_settings,
	    g_strdup("Absolute"),
	    g_strdup("1234567891123456789112345678911234567891123456789112345678911111"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	/* integer */
	attr3 = fu_context_get_bios_setting(ctx, "com.fwupd-internal.CustomChargeStop");
	g_assert_nonnull(attr3);

	current = fwupd_bios_setting_get_current_value(attr3);
	g_assert_nonnull(current);

	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings, g_strdup("CustomChargeStop"), g_strdup("75"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_true(ret);

	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings, g_strdup("CustomChargeStop"), g_strdup("110"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings, g_strdup("CustomChargeStop"), g_strdup("1"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	/* force it to read only */
	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings, g_strdup("CustomChargeStop"), g_strdup("70"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, TRUE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* read only */
	attr4 = fu_context_get_bios_setting(ctx, "com.fwupd-internal.pending_reboot");
	g_assert_nonnull(attr4);

	current = fwupd_bios_setting_get_current_value(attr4);
	g_assert_nonnull(current);

	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings, g_strdup("pending_reboot"), g_strdup("foo"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings, g_strdup("CustomChargeStop"), g_strdup("80"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);
}

static void
fu_engine_report_metadata_func(void)
{
	gboolean ret;
	g_autofree gchar *testdatadir = NULL;
	g_autofree gchar *testdatadir_sysfs = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GHashTable) metadata = NULL;
	g_autoptr(GList) keys = NULL;
	const gchar *keys_exist[] = {
	    "BatteryThreshold",
	    "CompileVersion(org.freedesktop.fwupd)",
	    "CpuArchitecture",
	    "DistroId",
	    "FwupdSupported",
	    "RuntimeVersion(org.freedesktop.fwupd)",
	    "SELinux",
	};

	/* set up test harness */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	testdatadir_sysfs = g_test_build_filename(G_TEST_DIST, "tests", "sys", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSCONFDIR_PKG, testdatadir);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR, testdatadir_sysfs);

	/* load dummy hwids */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_context_load_hwinfo(ctx, progress, FU_CONTEXT_HWID_FLAG_LOAD_CONFIG, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_READONLY | FU_ENGINE_LOAD_FLAG_NO_CACHE,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check report metadata */
	metadata = fu_engine_get_report_metadata(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(metadata);

	keys = g_list_sort(g_hash_table_get_keys(metadata), (GCompareFunc)g_strcmp0);
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		const gchar *value = g_hash_table_lookup(metadata, key);
		g_debug("%s=%s", key, value);
	}
	for (guint i = 0; i < G_N_ELEMENTS(keys_exist); i++) {
		const gchar *value = g_hash_table_lookup(metadata, keys_exist[i]);
		if (value == NULL)
			g_warning("no %s in metadata", keys_exist[i]);
	}
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	(void)g_setenv("FWUPD_SELF_TEST", "1", TRUE);
	g_test_add_func("/fwupd/engine/plugin/module", fu_engine_plugin_module_func);
	g_test_add_func("/fwupd/engine/get-details-added", fu_engine_get_details_added_func);
	g_test_add_func("/fwupd/engine/get-details-missing", fu_engine_get_details_missing_func);
	g_test_add_func("/fwupd/engine/device-unlock", fu_engine_device_unlock_func);
	g_test_add_func("/fwupd/engine/device-equivalent", fu_engine_device_equivalent_func);
	g_test_add_func("/fwupd/engine/device-md-set-flags", fu_engine_device_md_set_flags_func);
	g_test_add_func("/fwupd/engine/device-md-checksum-set-version",
			fu_engine_device_md_checksum_set_version_func);
	g_test_add_func("/fwupd/engine/device-md-checksum-set-version-wrong-proto",
			fu_engine_device_md_checksum_set_version_wrong_proto_func);
	g_test_add_func("/fwupd/engine/multiple-releases", fu_engine_multiple_rels_func);
	g_test_add_func("/fwupd/engine/install-loop-restart", fu_engine_install_loop_restart_func);
	g_test_add_func("/fwupd/engine/install-request", fu_engine_install_request);
	g_test_add_func("/fwupd/engine/history-success", fu_engine_history_func);
	g_test_add_func("/fwupd/engine/history-verfmt", fu_engine_history_verfmt_func);
	g_test_add_func("/fwupd/engine/history-error", fu_engine_history_error_func);
	g_test_add_func("/fwupd/engine/report-metadata", fu_engine_report_metadata_func);
	g_test_add_func("/fwupd/engine/require-hwid", fu_engine_require_hwid_func);
	g_test_add_func("/fwupd/engine/requires-reboot", fu_engine_install_needs_reboot);
	g_test_add_func("/fwupd/engine/history-inherit", fu_engine_history_inherit);
	g_test_add_func("/fwupd/engine/history-convert-version",
			fu_engine_history_convert_version_func);
	g_test_add_func("/fwupd/engine/partial-hash", fu_engine_partial_hash_func);
	g_test_add_func("/fwupd/engine/downgrade", fu_engine_downgrade_func);
	g_test_add_func("/fwupd/engine/md-verfmt", fu_engine_md_verfmt_func);
	g_test_add_func("/fwupd/engine/device-auto-parent-id", fu_engine_device_parent_id_func);
	g_test_add_func("/fwupd/engine/device-auto-parent-guid", fu_engine_device_parent_guid_func);
	g_test_add_func("/fwupd/engine/install-duration", fu_engine_install_duration_func);
	g_test_add_func("/fwupd/engine/get-results-appstream-id",
			fu_plugin_engine_get_results_appstream_id_func);
	g_test_add_func("/fwupd/engine/release-dedupe", fu_engine_release_dedupe_func);
	g_test_add_func("/fwupd/engine/generate-md", fu_engine_generate_md_func);
	g_test_add_func("/fwupd/engine/better-than", fu_engine_device_better_than_func);
	g_test_add_func("/fwupd/engine/plugin/mutable", fu_engine_test_plugin_mutable_enumeration);
	g_test_add_func("/fwupd/engine/plugin/composite", fu_engine_plugin_composite_func);
	g_test_add_func("/fwupd/engine/plugin/composite-multistep",
			fu_engine_plugin_composite_multistep_func);
	g_test_add_func("/fwupd/engine/write-bios-attrs", fu_engine_modify_bios_settings_func);
	return g_test_run();
}
