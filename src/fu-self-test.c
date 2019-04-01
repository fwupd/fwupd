/*
 * Copyright (C) 2015-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <xmlb.h>
#include <fwupd.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <gio/gfiledescriptorbased.h>
#include <libgcab.h>
#include <stdlib.h>
#include <string.h>

#include "fu-archive.h"
#include "fu-common-cab.h"
#include "fu-common-version.h"
#include "fu-chunk.h"
#include "fu-config.h"
#include "fu-device-list.h"
#include "fu-device-private.h"
#include "fu-engine.h"
#include "fu-quirks.h"
#include "fu-keyring.h"
#include "fu-history.h"
#include "fu-install-task.h"
#include "fu-plugin-private.h"
#include "fu-plugin-list.h"
#include "fu-progressbar.h"
#include "fu-hash.h"
#include "fu-hwids.h"
#include "fu-smbios.h"
#include "fu-test.h"

#ifdef ENABLE_GPG
#include "fu-keyring-gpg.h"
#endif
#ifdef ENABLE_PKCS7
#include "fu-keyring-pkcs7.h"
#endif

static void
fu_self_test_mkroot (void)
{
	if (g_file_test ("/tmp/fwupd-self-test", G_FILE_TEST_EXISTS)) {
		g_autoptr(GError) error = NULL;
		if (!fu_common_rmtree ("/tmp/fwupd-self-test", &error))
			g_warning ("failed to mkroot: %s", error->message);
	}
	g_assert_cmpint (g_mkdir_with_parents ("/tmp/fwupd-self-test/var/lib/fwupd", 0755), ==, 0);
}

static void
fu_archive_invalid_func (void)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GError) error = NULL;

	filename = fu_test_get_filename (TESTDATADIR, "metadata.xml");
	g_assert_nonnull (filename);
	data = fu_common_get_contents_bytes (filename, &error);
	g_assert_no_error (error);
	g_assert_nonnull (data);

	archive = fu_archive_new (data, FU_ARCHIVE_FLAG_NONE, &error);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
	g_assert_null (archive);
}

static void
fu_engine_generate_md_func (void)
{
	const gchar *tmp;
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuDevice) device = fu_device_new ();
	g_autoptr(FuEngine) engine = fu_engine_new (FU_APP_FLAGS_NONE);
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;

	/* put cab file somewhere we can parse it */
	filename = fu_test_get_filename (TESTDATADIR, "colorhug/colorhug-als-3.0.2.cab");
	g_assert_nonnull (filename);
	data = fu_common_get_contents_bytes (filename, &error);
	g_assert_no_error (error);
	g_assert_nonnull (data);
	ret = fu_common_set_contents_bytes ("/tmp/fwupd-self-test/var/cache/fwupd/foo.cab",
					    data, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* load engine and check the device was found */
	ret = fu_engine_load (engine, FU_ENGINE_LOAD_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	fu_device_add_guid (device, "12345678-1234-1234-1234-123456789012");
	fu_device_set_version (device, "1.2.3");
	component = fu_engine_get_component_by_guids (engine, device);
	g_assert_nonnull (component);

	/* check remote ID set */
	tmp = xb_node_query_text (component, "../custom/value[@key='fwupd::RemoteId']", NULL);
	g_assert_cmpstr (tmp, ==, "directory");

	/* verify checksums */
	tmp = xb_node_query_text (component, "releases/release/checksum[@target='container']", NULL);
	g_assert_cmpstr (tmp, !=, NULL);
	tmp = xb_node_query_text (component, "releases/release/checksum[@target='content']", NULL);
	g_assert_cmpstr (tmp, ==, NULL);
}

static void
fu_archive_cab_func (void)
{
	g_autofree gchar *checksum1 = NULL;
	g_autofree gchar *checksum2 = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GError) error = NULL;
	GBytes *data_tmp;

	filename = fu_test_get_filename (TESTDATADIR, "colorhug/colorhug-als-3.0.2.cab");
	g_assert_nonnull (filename);
	data = fu_common_get_contents_bytes (filename, &error);
	g_assert_no_error (error);
	g_assert_nonnull (data);

	archive = fu_archive_new (data, FU_ARCHIVE_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert_nonnull (archive);

	data_tmp = fu_archive_lookup_by_fn (archive, "firmware.metainfo.xml", &error);
	g_assert_no_error (error);
	g_assert_nonnull (data_tmp);
	checksum1 = g_compute_checksum_for_bytes (G_CHECKSUM_SHA1, data_tmp);
	g_assert_cmpstr (checksum1, ==, "8611114f51f7151f190de86a5c9259d79ff34216");

	data_tmp = fu_archive_lookup_by_fn (archive, "firmware.bin", &error);
	g_assert_no_error (error);
	g_assert_nonnull (data_tmp);
	checksum2 = g_compute_checksum_for_bytes (G_CHECKSUM_SHA1, data_tmp);
	g_assert_cmpstr (checksum2, ==, "7c0ae84b191822bcadbdcbe2f74a011695d783c7");

	data_tmp = fu_archive_lookup_by_fn (archive, "NOTGOINGTOEXIST.xml", &error);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_assert_null (data_tmp);
}

static void
fu_common_version_guess_format_func (void)
{
	g_assert_cmpint (fu_common_version_guess_format (NULL), ==, FU_VERSION_FORMAT_UNKNOWN);
	g_assert_cmpint (fu_common_version_guess_format (""), ==, FU_VERSION_FORMAT_UNKNOWN);
	g_assert_cmpint (fu_common_version_guess_format ("1234ac"), ==, FU_VERSION_FORMAT_PLAIN);
	g_assert_cmpint (fu_common_version_guess_format ("1.2"), ==, FU_VERSION_FORMAT_PAIR);
	g_assert_cmpint (fu_common_version_guess_format ("1.2.3"), ==, FU_VERSION_FORMAT_TRIPLET);
	g_assert_cmpint (fu_common_version_guess_format ("1.2.3.4"), ==, FU_VERSION_FORMAT_QUAD);
	g_assert_cmpint (fu_common_version_guess_format ("1.2.3.4.5"), ==, FU_VERSION_FORMAT_UNKNOWN);
	g_assert_cmpint (fu_common_version_guess_format ("1a.2b.3"), ==, FU_VERSION_FORMAT_UNKNOWN);
	g_assert_cmpint (fu_common_version_guess_format ("1"), ==, FU_VERSION_FORMAT_PLAIN);
}

static void
fu_engine_requirements_missing_func (void)
{
	gboolean ret;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new (FU_APP_FLAGS_NONE);
	g_autoptr(FuInstallTask) task = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *xml =
		"<component>"
		"  <requires>"
		"    <id compare=\"ge\" version=\"1.2.3\">not.going.to.exist</id>"
		"  </requires>"
		"</component>";

	/* set up a dummy version */
	fu_engine_add_runtime_version (engine, "org.test.dummy", "1.2.3");

	/* make the component require one thing */
	silo = xb_silo_new_from_xml (xml, &error);
	g_assert_no_error (error);
	g_assert_nonnull (silo);
	component = xb_silo_query_first (silo, "component", &error);
	g_assert_no_error (error);
	g_assert_nonnull (component);

	/* check this fails */
	task = fu_install_task_new (NULL, component);
	ret = fu_engine_check_requirements (engine, task,
					    FWUPD_INSTALL_FLAG_NONE,
					    &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert (!ret);
}

static void
fu_engine_requirements_unsupported_func (void)
{
	gboolean ret;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new (FU_APP_FLAGS_NONE);
	g_autoptr(FuInstallTask) task = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *xml =
		"<component>"
		"  <requires>"
		"    <UNKNOWN compare=\"ge\" version=\"2.6.0\"/>"
		"  </requires>"
		"</component>";

	/* set up a dummy version */
	fu_engine_add_runtime_version (engine, "org.test.dummy", "1.2.3");

	/* make the component require one thing that we don't support */
	silo = xb_silo_new_from_xml (xml, &error);
	g_assert_no_error (error);
	g_assert_nonnull (silo);
	component = xb_silo_query_first (silo, "component", &error);
	g_assert_no_error (error);
	g_assert_nonnull (component);

	/* check this fails */
	task = fu_install_task_new (NULL, component);
	ret = fu_engine_check_requirements (engine, task,
					    FWUPD_INSTALL_FLAG_NONE,
					    &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert (!ret);
}

static void
fu_engine_requirements_func (void)
{
	gboolean ret;
	g_autoptr(FuEngine) engine = fu_engine_new (FU_APP_FLAGS_NONE);
	g_autoptr(FuInstallTask) task = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml =
		"<component>"
		"  <requires>"
		"    <id compare=\"ge\" version=\"1.2.3\">org.test.dummy</id>"
		"  </requires>"
		"</component>";

	/* set up some dummy versions */
	fu_engine_add_runtime_version (engine, "org.test.dummy", "1.2.3");
	fu_engine_add_runtime_version (engine, "com.hughski.colorhug", "7.8.9");

	/* make the component require one thing */
	silo = xb_silo_new_from_xml (xml, &error);
	g_assert_no_error (error);
	g_assert_nonnull (silo);
	component = xb_silo_query_first (silo, "component", &error);
	g_assert_no_error (error);
	g_assert_nonnull (component);

	/* check this passes */
	task = fu_install_task_new (NULL, component);
	ret = fu_engine_check_requirements (engine, task,
					    FWUPD_INSTALL_FLAG_NONE,
					    &error);
	g_assert_no_error (error);
	g_assert (ret);
}

static void
fu_engine_requirements_device_func (void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new ();
	g_autoptr(FuEngine) engine = fu_engine_new (FU_APP_FLAGS_NONE);
	g_autoptr(FuInstallTask) task = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml =
		"<component>"
		"  <requires>"
		"    <firmware compare=\"ge\" version=\"1.2.3\"/>"
		"    <firmware compare=\"eq\" version=\"4.5.6\">bootloader</firmware>"
		"    <firmware compare=\"eq\" version=\"FFFF\">vendor-id</firmware>"
		"  </requires>"
		"  <provides>"
		"    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
		"  </provides>"
		"  <releases>"
		"    <release version=\"1.2.4\">"
		"      <checksum type=\"sha1\" filename=\"bios.bin\" target=\"content\"/>"
		"    </release>"
		"  </releases>"
		"</component>";

	/* set up a dummy device */
	fu_device_set_version (device, "1.2.3");
	fu_device_set_version_bootloader (device, "4.5.6");
	fu_device_set_vendor_id (device, "FFFF");
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_guid (device, "12345678-1234-1234-1234-123456789012");

	/* make the component require three things */
	silo = xb_silo_new_from_xml (xml, &error);
	g_assert_no_error (error);
	g_assert_nonnull (silo);
	component = xb_silo_query_first (silo, "component", &error);
	g_assert_no_error (error);
	g_assert_nonnull (component);

	/* check this passes */
	task = fu_install_task_new (device, component);
	ret = fu_engine_check_requirements (engine, task,
					    FWUPD_INSTALL_FLAG_NONE,
					    &error);
	g_assert_no_error (error);
	g_assert (ret);
}

static void
fu_engine_requirements_other_device_func (void)
{
	gboolean ret;
	g_autoptr(FuDevice) device1 = fu_device_new ();
	g_autoptr(FuDevice) device2 = fu_device_new ();
	g_autoptr(FuEngine) engine = fu_engine_new (FU_APP_FLAGS_NONE);
	g_autoptr(FuInstallTask) task = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new ();
	const gchar *xml =
		"<component>"
		"  <requires>"
		"    <firmware compare=\"gt\" version=\"4.0.0\">1ff60ab2-3905-06a1-b476-0371f00c9e9b</firmware>"
		"  </requires>"
		"  <provides>"
		"    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
		"  </provides>"
		"  <releases>"
		"    <release version=\"1.2.4\">"
		"      <checksum type=\"sha1\" filename=\"bios.bin\" target=\"content\"/>"
		"    </release>"
		"  </releases>"
		"</component>";

	/* no metadata in daemon */
	fu_engine_set_silo (engine, silo_empty);

	/* set up a dummy device */
	fu_device_set_version (device1, "1.2.3");
	fu_device_add_flag (device1, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_guid (device1, "12345678-1234-1234-1234-123456789012");

	/* set up a different device */
	fu_device_set_id (device2, "id2");
	fu_device_set_name (device2, "Secondary firmware");
	fu_device_set_version (device2, "4.5.6");
	fu_device_set_vendor_id (device2, "FFFF");
	fu_device_add_guid (device2, "1ff60ab2-3905-06a1-b476-0371f00c9e9b");
	fu_engine_add_device (engine, device2);

	/* import firmware metainfo */
	silo = xb_silo_new_from_xml (xml, &error);
	g_assert_no_error (error);
	g_assert_nonnull (silo);
	component = xb_silo_query_first (silo, "component", &error);
	g_assert_no_error (error);
	g_assert_nonnull (component);

	/* check this passes */
	task = fu_install_task_new (device1, component);
	ret = fu_engine_check_requirements (engine, task,
					    FWUPD_INSTALL_FLAG_NONE,
					    &error);
	g_assert_no_error (error);
	g_assert (ret);
}

static void
fu_engine_device_priority_func (void)
{
	g_autoptr(FuDevice) device1 = fu_device_new ();
	g_autoptr(FuDevice) device2 = fu_device_new ();
	g_autoptr(FuDevice) device3 = fu_device_new ();
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new (FU_APP_FLAGS_NONE);
	g_autoptr(GError) error = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new ();

	/* no metadata in daemon */
	fu_engine_set_silo (engine, silo_empty);

	/* add low prio then high then low */
	fu_device_set_id (device1, "id1");
	fu_device_set_priority (device1, 0);
	fu_device_set_plugin (device1, "udev");
	fu_device_add_instance_id (device1, "GUID1");
	fu_device_convert_instance_ids (device1);
	fu_engine_add_device (engine, device1);
	fu_device_set_id (device2, "id2");
	fu_device_set_priority (device2, 1);
	fu_device_set_plugin (device2, "redfish");
	fu_device_add_instance_id (device2, "GUID1");
	fu_device_convert_instance_ids (device2);
	fu_engine_add_device (engine, device2);
	fu_device_set_id (device3, "id3");
	fu_device_set_priority (device3, 0);
	fu_device_set_plugin (device3, "uefi");
	fu_device_add_instance_id (device3, "GUID1");
	fu_device_convert_instance_ids (device3);
	fu_engine_add_device (engine, device3);

	/* get the high prio device */
	device = fu_engine_get_device (engine, "867d5f8110f8aa79dd63d7440f21724264f10430", &error);
	g_assert_no_error (error);
	g_assert_cmpint (fu_device_get_priority (device), ==, 1);
	g_clear_object (&device);

	/* the now-removed low-prio device */
	device = fu_engine_get_device (engine, "4e89d81a2e6fb4be2578d245fd8511c1f4ad0b58", &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null (device);
	g_clear_error (&error);

	/* the never-added 2nd low-prio device */
	device = fu_engine_get_device (engine, "c48feddbbcfee514f530ce8f7f2dccd98b6cc150", &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null (device);
}

static void
fu_engine_device_parent_func (void)
{
	g_autoptr(FuDevice) device1 = fu_device_new ();
	g_autoptr(FuDevice) device2 = fu_device_new ();
	g_autoptr(FuDevice) device3 = fu_device_new ();
	g_autoptr(FuEngine) engine = fu_engine_new (FU_APP_FLAGS_NONE);
	g_autoptr(XbSilo) silo_empty = xb_silo_new ();

	/* no metadata in daemon */
	fu_engine_set_silo (engine, silo_empty);

	/* add child */
	fu_device_set_id (device1, "child");
	fu_device_add_instance_id (device1, "child-GUID-1");
	fu_device_add_parent_guid (device1, "parent-GUID");
	fu_device_convert_instance_ids (device1);
	fu_engine_add_device (engine, device1);

	/* parent */
	fu_device_set_id (device2, "parent");
	fu_device_add_instance_id (device2, "parent-GUID");
	fu_device_set_vendor (device2, "oem");
	fu_device_convert_instance_ids (device2);

	/* add another child */
	fu_device_set_id (device3, "child2");
	fu_device_add_instance_id (device3, "child-GUID-2");
	fu_device_add_parent_guid (device3, "parent-GUID");
	fu_device_convert_instance_ids (device3);
	fu_device_add_child (device2, device3);

	/* add two together */
	fu_engine_add_device (engine, device2);

	/* verify both children were adopted */
	g_assert (fu_device_get_parent (device3) == device2);
	g_assert (fu_device_get_parent (device1) == device2);
	g_assert_cmpstr (fu_device_get_vendor (device3), ==, "oem");
	g_assert_cmpstr (fu_device_get_vendor (device1), ==, "oem");

	/* verify order */
	g_assert_cmpint (fu_device_get_order (device1), ==, 0);
	g_assert_cmpint (fu_device_get_order (device2), ==, 1);
	g_assert_cmpint (fu_device_get_order (device3), ==, 0);
}

static void
fu_engine_partial_hash_func (void)
{
	gboolean ret;
	g_autoptr(FuDevice) device1 = fu_device_new ();
	g_autoptr(FuDevice) device2 = fu_device_new ();
	g_autoptr(FuEngine) engine = fu_engine_new (FU_APP_FLAGS_NONE);
	g_autoptr(FuPlugin) plugin = fu_plugin_new ();
	g_autoptr(GError) error = NULL;
	g_autoptr(GError) error_none = NULL;
	g_autoptr(GError) error_both = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new ();

	/* no metadata in daemon */
	fu_engine_set_silo (engine, silo_empty);

	/* set up dummy plugin */
	fu_plugin_set_name (plugin, "test");
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_engine_add_plugin (engine, plugin);

	/* add two dummy devices */
	fu_device_set_id (device1, "device1");
	fu_device_set_plugin (device1, "test");
	fu_device_add_guid (device1, "12345678-1234-1234-1234-123456789012");
	fu_engine_add_device (engine, device1);
	fu_device_set_id (device2, "device21");
	fu_device_set_plugin (device2, "test");
	fu_device_set_equivalent_id (device2, "b92f5b7560b84ca005a79f5a15de3c003ce494cf");
	fu_device_add_guid (device2, "12345678-1234-1234-1234-123456789012");
	fu_engine_add_device (engine, device2);

	/* match nothing */
	ret = fu_engine_unlock (engine, "deadbeef", &error_none);
	g_assert_error (error_none, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert (!ret);

	/* match both */
	ret = fu_engine_unlock (engine, "9", &error_both);
	g_assert_error (error_both, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert (!ret);

	/* match one exactly */
	fu_device_add_flag (device1, FWUPD_DEVICE_FLAG_LOCKED);
	fu_device_add_flag (device2, FWUPD_DEVICE_FLAG_LOCKED);
	ret = fu_engine_unlock (engine, "934b4162a6daa0b033d649c8d464529cec41d3de", &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* match one partially */
	fu_device_add_flag (device1, FWUPD_DEVICE_FLAG_LOCKED);
	fu_device_add_flag (device2, FWUPD_DEVICE_FLAG_LOCKED);
	ret = fu_engine_unlock (engine, "934b", &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* match equivalent ID */
	fu_device_add_flag (device1, FWUPD_DEVICE_FLAG_LOCKED);
	fu_device_add_flag (device2, FWUPD_DEVICE_FLAG_LOCKED);
	ret = fu_engine_unlock (engine, "b92f", &error);
	g_assert_no_error (error);
	g_assert (ret);
}

static void
fu_engine_device_unlock_func (void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuDevice) device = fu_device_new ();
	g_autoptr(FuEngine) engine = fu_engine_new (FU_APP_FLAGS_NONE);
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new ();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new ();
	g_autoptr(XbSilo) silo = NULL;

	/* load engine to get FuConfig set up */
	ret = fu_engine_load (engine, FU_ENGINE_LOAD_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* add the hardcoded 'fwupd' metadata */
	filename = fu_test_get_filename (TESTDATADIR, "metadata.xml");
	g_assert (filename != NULL);
	file = g_file_new_for_path (filename);
	ret = xb_builder_source_load_file (source, file,
					   XB_BUILDER_SOURCE_FLAG_NONE,
					   NULL, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	xb_builder_import_source (builder, source);
	silo = xb_builder_compile (builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (silo);
	fu_engine_set_silo (engine, silo);

	/* add a dummy device */
	fu_device_set_id (device, "UEFI-dummy-dev0");
	fu_device_add_guid (device, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_LOCKED);
	fu_engine_add_device (engine, device);

	/* ensure the metainfo was matched */
	g_assert_nonnull (fwupd_device_get_release_default (FWUPD_DEVICE (device)));
}

static void
fu_engine_require_hwid_func (void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuDevice) device = fu_device_new ();
	g_autoptr(FuEngine) engine = fu_engine_new (FU_APP_FLAGS_NONE);
	g_autoptr(FuInstallTask) task = NULL;
	g_autoptr(GBytes) blob_cab = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new ();
	g_autoptr(XbSilo) silo = NULL;

#if !defined(HAVE_GCAB_0_8) && defined(__s390x__)
	/* See https://github.com/hughsie/fwupd/issues/318 for more information */
	g_test_skip ("Skipping HWID test on s390x due to known problem with gcab");
	return;
#endif

	/* no metadata in daemon */
	fu_engine_set_silo (engine, silo_empty);

	/* load engine to get FuConfig set up */
	ret = fu_engine_load (engine, FU_ENGINE_LOAD_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* get generated file as a blob */
	filename = fu_test_get_filename (TESTDATADIR, "missing-hwid/hwid-1.2.3.cab");
	g_assert (filename != NULL);
	blob_cab = fu_common_get_contents_bytes	(filename, &error);
	g_assert_no_error (error);
	g_assert (blob_cab != NULL);
	silo = fu_engine_get_silo_from_blob (engine, blob_cab, &error);
	g_assert_no_error (error);
	g_assert_nonnull (silo);

	/* add a dummy device */
	fu_device_set_id (device, "test_device");
	fu_device_set_version (device, "1.2.2");
	fu_device_add_guid (device, "12345678-1234-1234-1234-123456789012");
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_engine_add_device (engine, device);

	/* get component */
	component = xb_silo_query_first (silo, "components/component/id[text()='com.hughski.test.firmware']/..", &error);
	g_assert_no_error (error);
	g_assert_nonnull (component);

	/* check requirements */
	task = fu_install_task_new (device, component);
	ret = fu_engine_check_requirements (engine, task,
					    FWUPD_INSTALL_FLAG_NONE,
					    &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert (error != NULL);
	g_assert_cmpstr (error->message, ==,
			 "no HWIDs matched 9342d47a-1bab-5709-9869-c840b2eac501");
	g_assert (!ret);
}

static void
fu_engine_downgrade_func (void)
{
	FwupdRelease *rel;
	gboolean ret;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuDevice) device = fu_device_new ();
	g_autoptr(FuEngine) engine = fu_engine_new (FU_APP_FLAGS_NONE);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) devices_pre = NULL;
	g_autoptr(GPtrArray) releases_dg = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(GPtrArray) releases_up = NULL;
	g_autoptr(GPtrArray) remotes = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new ();

	/* ensure empty tree */
	fu_self_test_mkroot ();

	/* no metadata in daemon */
	fu_engine_set_silo (engine, silo_empty);

	/* write a broken file */
	ret = g_file_set_contents ("/tmp/fwupd-self-test/broken.xml.gz",
				   "this is not a valid", -1, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* write the main file */
	ret = g_file_set_contents ("/tmp/fwupd-self-test/stable.xml",
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
				   "        <checksum filename=\"foo.cab\" target=\"container\" type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
				   "        <checksum filename=\"firmware.bin\" target=\"content\" type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
				   "      </release>"
				   "      <release version=\"1.2.2\" date=\"2017-09-01\">"
				   "        <size type=\"installed\">123</size>"
				   "        <size type=\"download\">456</size>"
				   "        <location>https://test.org/foo.cab</location>"
				   "        <checksum filename=\"foo.cab\" target=\"container\" type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
				   "        <checksum filename=\"firmware.bin\" target=\"content\" type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
				   "      </release>"
				   "    </releases>"
				   "  </component>"
				   "</components>", -1, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* write the extra file */
	ret = g_file_set_contents ("/tmp/fwupd-self-test/testing.xml",
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
				   "        <checksum filename=\"foo.cab\" target=\"container\" type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
				   "        <checksum filename=\"firmware.bin\" target=\"content\" type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
				   "      </release>"
				   "      <release version=\"1.2.4\" date=\"2017-09-15\">"
				   "        <size type=\"installed\">123</size>"
				   "        <size type=\"download\">456</size>"
				   "        <location>https://test.org/foo.cab</location>"
				   "        <checksum filename=\"foo.cab\" target=\"container\" type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
				   "        <checksum filename=\"firmware.bin\" target=\"content\" type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
				   "      </release>"
				   "    </releases>"
				   "  </component>"
				   "</components>", -1, &error);
	g_assert_no_error (error);
	g_assert (ret);

	testdatadir = fu_test_get_filename (TESTDATADIR, ".");
	g_assert (testdatadir != NULL);
	g_setenv ("FU_SELF_TEST_REMOTES_DIR", testdatadir, TRUE);
	ret = fu_engine_load (engine, FU_ENGINE_LOAD_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (fu_engine_get_status (engine), ==, FWUPD_STATUS_IDLE);
	g_test_assert_expected_messages ();

	/* return all the remotes, even the broken one */
	remotes = fu_engine_get_remotes (engine, &error);
	g_assert_no_error (error);
	g_assert (remotes != NULL);
	g_assert_cmpint (remotes->len, ==, 4);

	/* ensure there are no devices already */
	devices_pre = fu_engine_get_devices (engine, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO);
	g_assert (devices_pre == NULL);
	g_clear_error (&error);

	/* add a device so we can get upgrades and downgrades */
	fu_device_set_version (device, "1.2.3");
	fu_device_set_id (device, "test_device");
	fu_device_set_name (device, "Test Device");
	fu_device_add_guid (device, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_engine_add_device (engine, device);
	devices = fu_engine_get_devices (engine, &error);
	g_assert_no_error (error);
	g_assert (devices != NULL);
	g_assert_cmpint (devices->len, ==, 1);
	g_assert (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_SUPPORTED));
	g_assert (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_REGISTERED));

	/* get the releases for one device */
	releases = fu_engine_get_releases (engine, fu_device_get_id (device), &error);
	g_assert_no_error (error);
	g_assert (releases != NULL);
	g_assert_cmpint (releases->len, ==, 4);

	/* no upgrades, as no firmware is approved */
	releases_up = fu_engine_get_upgrades (engine, fu_device_get_id (device), &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO);
	g_assert_null (releases_up);
	g_clear_error (&error);

	/* retry with approved firmware set */
	fu_engine_add_approved_firmware (engine, "deadbeefdeadbeefdeadbeefdeadbeef");
	fu_engine_add_approved_firmware (engine, "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX");

	/* upgrades */
	releases_up = fu_engine_get_upgrades (engine, fu_device_get_id (device), &error);
	g_assert_no_error (error);
	g_assert (releases_up != NULL);
	g_assert_cmpint (releases_up->len, ==, 2);

	/* ensure the list is sorted */
	rel = FWUPD_RELEASE (g_ptr_array_index (releases_up, 0));
	g_assert_cmpstr (fwupd_release_get_version (rel), ==, "1.2.5");
	rel = FWUPD_RELEASE (g_ptr_array_index (releases_up, 1));
	g_assert_cmpstr (fwupd_release_get_version (rel), ==, "1.2.4");

	/* downgrades */
	releases_dg = fu_engine_get_downgrades (engine, fu_device_get_id (device), &error);
	g_assert_no_error (error);
	g_assert (releases_dg != NULL);
	g_assert_cmpint (releases_dg->len, ==, 1);
	rel = FWUPD_RELEASE (g_ptr_array_index (releases_dg, 0));
	g_assert_cmpstr (fwupd_release_get_version (rel), ==, "1.2.2");
}

static void
fu_engine_install_duration_func (void)
{
	FwupdRelease *rel;
	gboolean ret;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuDevice) device = fu_device_new ();
	g_autoptr(FuEngine) engine = fu_engine_new (FU_APP_FLAGS_NONE);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new ();

	/* ensure empty tree */
	fu_self_test_mkroot ();

	/* no metadata in daemon */
	fu_engine_set_silo (engine, silo_empty);

	/* write the main file */
	ret = g_file_set_contents ("/tmp/fwupd-self-test/stable.xml",
				   "<components>"
				   "  <component type=\"firmware\">"
				   "    <id>test</id>"
				   "    <provides>"
				   "      <firmware type=\"flashed\">aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee</firmware>"
				   "    </provides>"
				   "    <releases>"
				   "      <release version=\"1.2.3\" date=\"2017-09-15\" install_duration=\"120\">"
				   "        <location>https://test.org/foo.cab</location>"
				   "        <checksum filename=\"foo.cab\" target=\"container\" type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
				   "        <checksum filename=\"firmware.bin\" target=\"content\" type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
				   "      </release>"
				   "    </releases>"
				   "  </component>"
				   "</components>", -1, &error);
	g_assert_no_error (error);
	g_assert (ret);

	testdatadir = fu_test_get_filename (TESTDATADIR, ".");
	g_assert (testdatadir != NULL);
	g_setenv ("FU_SELF_TEST_REMOTES_DIR", testdatadir, TRUE);
	ret = fu_engine_load (engine, FU_ENGINE_LOAD_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* add a device so we can get the install duration */
	fu_device_set_version (device, "1.2.3");
	fu_device_set_id (device, "test_device");
	fu_device_add_guid (device, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
	fu_device_set_install_duration (device, 999);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_engine_add_device (engine, device);
	devices = fu_engine_get_devices (engine, &error);
	g_assert_no_error (error);
	g_assert (devices != NULL);
	g_assert_cmpint (devices->len, ==, 1);
	g_assert (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_SUPPORTED));

	/* check the release install duration */
	releases = fu_engine_get_releases (engine, fu_device_get_id (device), &error);
	g_assert_no_error (error);
	g_assert (releases != NULL);
	g_assert_cmpint (releases->len, ==, 1);
	rel = FWUPD_RELEASE (g_ptr_array_index (releases, 0));
	g_assert_cmpint (fwupd_release_get_install_duration (rel), ==, 120);
}

static void
fu_engine_history_func (void)
{
	gboolean ret;
	g_autofree gchar *checksum = NULL;
	g_autofree gchar *device_str_expected = NULL;
	g_autofree gchar *device_str = NULL;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuDevice) device2 = NULL;
	g_autoptr(FuDevice) device = fu_device_new ();
	g_autoptr(FuEngine) engine = fu_engine_new (FU_APP_FLAGS_NONE);
	g_autoptr(FuHistory) history = NULL;
	g_autoptr(FuInstallTask) task = NULL;
	g_autoptr(FuPlugin) plugin = fu_plugin_new ();
	g_autoptr(FwupdDevice) device3 = NULL;
	g_autoptr(FwupdDevice) device4 = NULL;
	g_autoptr(GBytes) blob_cab = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new ();
	g_autoptr(XbSilo) silo = NULL;

	/* ensure empty tree */
	fu_self_test_mkroot ();

	/* no metadata in daemon */
	fu_engine_set_silo (engine, silo_empty);

	/* set up dummy plugin */
	ret = fu_plugin_open (plugin, PLUGINBUILDDIR "/libfu_plugin_test.so", &error);
	g_assert_no_error (error);
	g_assert (ret);
	fu_engine_add_plugin (engine, plugin);

	testdatadir = fu_test_get_filename (TESTDATADIR, ".");
	g_assert (testdatadir != NULL);
	g_setenv ("FU_SELF_TEST_REMOTES_DIR", testdatadir, TRUE);
	ret = fu_engine_load (engine, FU_ENGINE_LOAD_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (fu_engine_get_status (engine), ==, FWUPD_STATUS_IDLE);

	/* add a device so we can get upgrade it */
	fu_device_set_version (device, "1.2.2");
	fu_device_set_id (device, "test_device");
	fu_device_set_name (device, "Test Device");
	fu_device_set_plugin (device, "test");
	fu_device_add_guid (device, "12345678-1234-1234-1234-123456789012");
	fu_device_add_checksum (device, "0123456789abcdef0123456789abcdef01234567");
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_created (device, 1515338000);
	fu_engine_add_device (engine, device);
	devices = fu_engine_get_devices (engine, &error);
	g_assert_no_error (error);
	g_assert (devices != NULL);
	g_assert_cmpint (devices->len, ==, 1);
	g_assert (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_REGISTERED));

	filename = fu_test_get_filename (TESTDATADIR, "missing-hwid/noreqs-1.2.3.cab");
	g_assert (filename != NULL);
	blob_cab = fu_common_get_contents_bytes	(filename, &error);
	g_assert_no_error (error);
	g_assert (blob_cab != NULL);
	silo = fu_engine_get_silo_from_blob (engine, blob_cab, &error);
	g_assert_no_error (error);
	g_assert_nonnull (silo);

	/* get component */
	component = xb_silo_query_first (silo, "components/component/id[text()='com.hughski.test.firmware']/..", &error);
	g_assert_no_error (error);
	g_assert_nonnull (component);

	/* set the counter */
	g_setenv ("FWUPD_PLUGIN_TEST", "another-write-required", TRUE);
	fu_device_set_metadata_integer (device, "nr-update", 0);

	/* install it */
	task = fu_install_task_new (device, component);
	ret = fu_engine_install (engine, task, blob_cab,
				 FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* check the write was done more than once */
	g_assert_cmpint (fu_device_get_metadata_integer (device, "nr-update"), ==, 2);

	/* check the history database */
	history = fu_history_new ();
	device2 = fu_history_get_device_by_id (history, fu_device_get_id (device), &error);
	g_assert_no_error (error);
	g_assert (device2 != NULL);
	g_assert_cmpint (fu_device_get_update_state (device2), ==, FWUPD_UPDATE_STATE_SUCCESS);
	g_assert_cmpstr (fu_device_get_update_error (device2), ==, NULL);
	fu_device_set_modified (device2, 1514338000);
	g_hash_table_remove_all (fwupd_release_get_metadata (fu_device_get_release_default (device2)));
	device_str = fu_device_to_string (device2);
	checksum = g_compute_checksum_for_bytes (G_CHECKSUM_SHA1, blob_cab);
	device_str_expected = g_strdup_printf (
		"Test Device\n"
		"  DeviceId:             894e8c17a29428b09d10cd90d1db74ea76fbcfe8\n"
		"  Guid:                 12345678-1234-1234-1234-123456789012\n"
		"  Plugin:               test\n"
		"  Flags:                updatable\n"
		"  Version:              1.2.2\n"
		"  Created:              2018-01-07\n"
		"  Modified:             2017-12-27\n"
		"  UpdateState:          success\n"
		"  \n"
		"  [Release]\n"
		"  Version:              1.2.3\n"
		"  Checksum:             SHA1(%s)\n"
		"  Flags:                none\n"
		"  VersionFormat:        triplet\n",
		checksum);
	ret = fu_test_compare_lines (device_str, device_str_expected, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* GetResults() */
	device3 = fu_engine_get_results (engine, FWUPD_DEVICE_ID_ANY, &error);
	g_assert (device3 != NULL);
	g_assert_cmpstr (fu_device_get_id (device3), ==,
			 "894e8c17a29428b09d10cd90d1db74ea76fbcfe8");
	g_assert_cmpint (fu_device_get_update_state (device3), ==, FWUPD_UPDATE_STATE_SUCCESS);
	g_assert_cmpstr (fu_device_get_update_error (device3), ==, NULL);

	/* ClearResults() */
	ret = fu_engine_clear_results (engine, FWUPD_DEVICE_ID_ANY, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* GetResults() */
	device4 = fu_engine_get_results (engine, FWUPD_DEVICE_ID_ANY, &error);
	g_assert (device4 == NULL);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO);
}

static void
fu_engine_history_inherit (void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuDevice) device = fu_device_new ();
	g_autoptr(FuEngine) engine = fu_engine_new (FU_APP_FLAGS_NONE);
	g_autoptr(FuInstallTask) task = NULL;
	g_autoptr(FuPlugin) plugin = fu_plugin_new ();
	g_autoptr(GBytes) blob_cab = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new ();
	g_autoptr(XbSilo) silo = NULL;

	/* no metadata in daemon */
	fu_engine_set_silo (engine, silo_empty);

	/* set up dummy plugin */
	g_setenv ("FWUPD_PLUGIN_TEST", "fail", TRUE);
	ret = fu_plugin_open (plugin, PLUGINBUILDDIR "/libfu_plugin_test.so", &error);
	g_assert_no_error (error);
	g_assert (ret);
	fu_engine_add_plugin (engine, plugin);
	testdatadir = fu_test_get_filename (TESTDATADIR, ".");
	g_assert (testdatadir != NULL);
	g_setenv ("FU_SELF_TEST_REMOTES_DIR", testdatadir, TRUE);
	ret = fu_engine_load (engine, FU_ENGINE_LOAD_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (fu_engine_get_status (engine), ==, FWUPD_STATUS_IDLE);

	/* add a device so we can get upgrade it */
	fu_device_set_version (device, "1.2.2");
	fu_device_set_id (device, "test_device");
	fu_device_set_name (device, "Test Device");
	fu_device_set_plugin (device, "test");
	fu_device_add_guid (device, "12345678-1234-1234-1234-123456789012");
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_created (device, 1515338000);
	fu_engine_add_device (engine, device);
	devices = fu_engine_get_devices (engine, &error);
	g_assert_no_error (error);
	g_assert (devices != NULL);
	g_assert_cmpint (devices->len, ==, 1);
	g_assert (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_REGISTERED));

	filename = fu_test_get_filename (TESTDATADIR, "missing-hwid/noreqs-1.2.3.cab");
	g_assert (filename != NULL);
	blob_cab = fu_common_get_contents_bytes	(filename, &error);
	g_assert_no_error (error);
	g_assert (blob_cab != NULL);
	silo = fu_engine_get_silo_from_blob (engine, blob_cab, &error);
	g_assert_no_error (error);
	g_assert_nonnull (silo);

	/* get component */
	component = xb_silo_query_first (silo, "components/component/id[text()='com.hughski.test.firmware']/..", &error);
	g_assert_no_error (error);
	g_assert_nonnull (component);

	/* install it */
	g_setenv ("FWUPD_PLUGIN_TEST", "requires-activation", TRUE);
	task = fu_install_task_new (device, component);
	ret = fu_engine_install (engine, task, blob_cab,
				 FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* check the device requires an activation */
	g_assert_true (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION));
	g_assert_cmpstr (fu_device_get_version (device), ==, "1.2.2");

	/* activate the device */
	ret = fu_engine_activate (engine, fu_device_get_id (device), &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* check the device no longer requires an activation */
	g_assert_false (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION));
	g_assert_cmpstr (fu_device_get_version (device), ==, "1.2.3");

	/* emulate getting the flag for a fresh boot on old firmware */
	fu_device_set_version (device, "1.2.2");
	ret = fu_engine_install (engine, task, blob_cab,
				 FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_object_unref (engine);
	g_object_unref (device);
	engine = fu_engine_new (FU_APP_FLAGS_NONE);
	fu_engine_set_silo (engine, silo_empty);
	fu_engine_add_plugin (engine, plugin);
	device = fu_device_new ();
	fu_device_set_id (device, "test_device");
	fu_device_set_name (device, "Test Device");
	fu_device_add_guid (device, "12345678-1234-1234-1234-123456789012");
	fu_device_set_version (device, "1.2.2");
	fu_engine_add_device (engine, device);
	g_assert_true (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION));
}

static void
fu_engine_history_error_func (void)
{
	gboolean ret;
	g_autofree gchar *checksum = NULL;
	g_autofree gchar *device_str_expected = NULL;
	g_autofree gchar *device_str = NULL;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuDevice) device2 = NULL;
	g_autoptr(FuDevice) device = fu_device_new ();
	g_autoptr(FuEngine) engine = fu_engine_new (FU_APP_FLAGS_NONE);
	g_autoptr(FuHistory) history = NULL;
	g_autoptr(FuInstallTask) task = NULL;
	g_autoptr(FuPlugin) plugin = fu_plugin_new ();
	g_autoptr(GBytes) blob_cab = NULL;
	g_autoptr(GError) error2 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new ();
	g_autoptr(XbSilo) silo = NULL;

	/* no metadata in daemon */
	fu_engine_set_silo (engine, silo_empty);

	/* set up dummy plugin */
	g_setenv ("FWUPD_PLUGIN_TEST", "fail", TRUE);
	ret = fu_plugin_open (plugin, PLUGINBUILDDIR "/libfu_plugin_test.so", &error);
	g_assert_no_error (error);
	g_assert (ret);
	fu_engine_add_plugin (engine, plugin);

	testdatadir = fu_test_get_filename (TESTDATADIR, ".");
	g_assert (testdatadir != NULL);
	g_setenv ("FU_SELF_TEST_REMOTES_DIR", testdatadir, TRUE);
	ret = fu_engine_load (engine, FU_ENGINE_LOAD_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (fu_engine_get_status (engine), ==, FWUPD_STATUS_IDLE);

	/* add a device so we can get upgrade it */
	fu_device_set_version (device, "1.2.2");
	fu_device_set_id (device, "test_device");
	fu_device_set_name (device, "Test Device");
	fu_device_set_plugin (device, "test");
	fu_device_add_guid (device, "12345678-1234-1234-1234-123456789012");
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_created (device, 1515338000);
	fu_engine_add_device (engine, device);
	devices = fu_engine_get_devices (engine, &error);
	g_assert_no_error (error);
	g_assert (devices != NULL);
	g_assert_cmpint (devices->len, ==, 1);
	g_assert (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_REGISTERED));

	/* install the wrong thing */
	filename = fu_test_get_filename (TESTDATADIR, "missing-hwid/noreqs-1.2.3.cab");
	g_assert (filename != NULL);
	blob_cab = fu_common_get_contents_bytes	(filename, &error);
	g_assert_no_error (error);
	g_assert (blob_cab != NULL);
	silo = fu_engine_get_silo_from_blob (engine, blob_cab, &error);
	g_assert_no_error (error);
	g_assert_nonnull (silo);
	component = xb_silo_query_first (silo, "components/component/id[text()='com.hughski.test.firmware']/..", &error);
	g_assert_no_error (error);
	g_assert_nonnull (component);
	task = fu_install_task_new (device, component);
	ret = fu_engine_install (engine, task, blob_cab,
				 FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert (error != NULL);
	g_assert_cmpstr (error->message, ==,
			 "device was not in supported mode");
	g_assert (!ret);

	/* check the history database */
	history = fu_history_new ();
	device2 = fu_history_get_device_by_id (history, fu_device_get_id (device), &error2);
	g_assert_no_error (error2);
	g_assert (device2 != NULL);
	g_assert_cmpint (fu_device_get_update_state (device2), ==, FWUPD_UPDATE_STATE_FAILED);
	g_assert_cmpstr (fu_device_get_update_error (device2), ==, error->message);
	g_clear_error (&error);
	fu_device_set_modified (device2, 1514338000);
	g_hash_table_remove_all (fwupd_release_get_metadata (fu_device_get_release_default (device2)));
	device_str = fu_device_to_string (device2);
	checksum = g_compute_checksum_for_bytes (G_CHECKSUM_SHA1, blob_cab);
	device_str_expected = g_strdup_printf (
		"Test Device\n"
		"  DeviceId:             894e8c17a29428b09d10cd90d1db74ea76fbcfe8\n"
		"  Guid:                 12345678-1234-1234-1234-123456789012\n"
		"  Plugin:               test\n"
		"  Flags:                updatable\n"
		"  Version:              1.2.2\n"
		"  Created:              2018-01-07\n"
		"  Modified:             2017-12-27\n"
		"  UpdateState:          failed\n"
		"  UpdateError:          device was not in supported mode\n"
		"  \n"
		"  [Release]\n"
		"  Version:              1.2.3\n"
		"  Checksum:             SHA1(%s)\n"
		"  Flags:                none\n"
		"  VersionFormat:        triplet\n",
		checksum);
	ret = fu_test_compare_lines (device_str, device_str_expected, &error);
	g_assert_no_error (error);
	g_assert (ret);
}

static void
_device_list_count_cb (FuDeviceList *device_list, FuDevice *device, gpointer user_data)
{
	guint *cnt = (guint *) user_data;
	(*cnt)++;
}

static void
fu_device_list_delay_func (void)
{
	g_autoptr(FuDevice) device1 = fu_device_new ();
	g_autoptr(FuDevice) device2 = fu_device_new ();
	g_autoptr(FuDeviceList) device_list = fu_device_list_new ();
	guint added_cnt = 0;
	guint changed_cnt = 0;
	guint removed_cnt = 0;

	g_signal_connect (device_list, "added",
			  G_CALLBACK (_device_list_count_cb),
			  &added_cnt);
	g_signal_connect (device_list, "removed",
			  G_CALLBACK (_device_list_count_cb),
			  &removed_cnt);
	g_signal_connect (device_list, "changed",
			  G_CALLBACK (_device_list_count_cb),
			  &changed_cnt);

	/* add one device */
	fu_device_set_id (device1, "device1");
	fu_device_add_instance_id (device1, "foobar");
	fu_device_set_remove_delay (device1, 100);
	fu_device_convert_instance_ids (device1);
	fu_device_list_add (device_list, device1);
	g_assert_cmpint (added_cnt, ==, 1);
	g_assert_cmpint (removed_cnt, ==, 0);
	g_assert_cmpint (changed_cnt, ==, 0);

	/* add the same device again */
	fu_device_list_add (device_list, device1);
	g_assert_cmpint (added_cnt, ==, 1);
	g_assert_cmpint (removed_cnt, ==, 0);
	g_assert_cmpint (changed_cnt, ==, 0);

	/* add a device with the same ID */
	fu_device_set_id (device2, "device1");
	fu_device_list_add (device_list, device2);
	g_assert_cmpint (added_cnt, ==, 1);
	g_assert_cmpint (removed_cnt, ==, 0);
	g_assert_cmpint (changed_cnt, ==, 0);

	/* spin a bit */
	fu_test_loop_run_with_timeout (10);
	fu_test_loop_quit ();

	/* verify only a changed event was generated */
	added_cnt = removed_cnt = changed_cnt = 0;
	fu_device_list_remove (device_list, device1);
	fu_device_list_add (device_list, device1);
	g_assert_cmpint (added_cnt, ==, 0);
	g_assert_cmpint (removed_cnt, ==, 0);
	g_assert_cmpint (changed_cnt, ==, 1);
}

typedef struct {
	FuDevice	*device_new;
	FuDevice	*device_old;
	FuDeviceList	*device_list;
} FuDeviceListReplugHelper;

static gboolean
fu_device_list_remove_cb (gpointer user_data)
{
	FuDeviceListReplugHelper *helper = (FuDeviceListReplugHelper *) user_data;
	fu_device_list_remove (helper->device_list, helper->device_old);
	return FALSE;
}

static gboolean
fu_device_list_add_cb (gpointer user_data)
{
	FuDeviceListReplugHelper *helper = (FuDeviceListReplugHelper *) user_data;
	fu_device_list_add (helper->device_list, helper->device_new);
	return FALSE;
}

static void
fu_device_list_replug_auto_func (void)
{
	gboolean ret;
	g_autoptr(FuDevice) device1 = fu_device_new ();
	g_autoptr(FuDevice) device2 = fu_device_new ();
	g_autoptr(FuDevice) parent = fu_device_new ();
	g_autoptr(FuDeviceList) device_list = fu_device_list_new ();
	g_autoptr(GError) error = NULL;
	FuDeviceListReplugHelper helper;

	/* parent */
	fu_device_set_id (parent, "parent");

	/* fake child devices */
	fu_device_set_id (device1, "device1");
	fu_device_set_physical_id (device1, "ID");
	fu_device_set_plugin (device1, "self-test");
	fu_device_set_remove_delay (device1, FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_child (parent, device1);
	fu_device_set_id (device2, "device2");
	fu_device_set_physical_id (device2, "ID"); /* matches */
	fu_device_set_plugin (device2, "self-test");
	fu_device_set_remove_delay (device2, FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);

	/* not yet added */
	ret = fu_device_list_wait_for_replug (device_list, device1, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* add device */
	fu_device_list_add (device_list, device1);

	/* not waiting */
	ret = fu_device_list_wait_for_replug (device_list, device1, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* waiting */
	helper.device_old = device1;
	helper.device_new = device2;
	helper.device_list = device_list;
	g_timeout_add (100, fu_device_list_remove_cb, &helper);
	g_timeout_add (200, fu_device_list_add_cb, &helper);
	fu_device_add_flag (device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	ret = fu_device_list_wait_for_replug (device_list, device1, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* check device2 now has parent too */
	g_assert (fu_device_get_parent (device2) == parent);

	/* waiting, failed */
	fu_device_add_flag (device2, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	ret = fu_device_list_wait_for_replug (device_list, device2, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert (!ret);
}

static void
fu_device_list_replug_user_func (void)
{
	gboolean ret;
	g_autoptr(FuDevice) device1 = fu_device_new ();
	g_autoptr(FuDevice) device2 = fu_device_new ();
	g_autoptr(FuDeviceList) device_list = fu_device_list_new ();
	g_autoptr(GError) error = NULL;
	FuDeviceListReplugHelper helper;

	/* fake devices */
	fu_device_set_id (device1, "device1");
	fu_device_add_instance_id (device1, "foo");
	fu_device_add_instance_id (device1, "bar");
	fu_device_set_plugin (device1, "self-test");
	fu_device_set_remove_delay (device1, FU_DEVICE_REMOVE_DELAY_USER_REPLUG);
	fu_device_convert_instance_ids (device1);
	fu_device_set_id (device2, "device2");
	fu_device_add_instance_id (device2, "baz");
	fu_device_add_instance_id (device2, "bar"); /* matches */
	fu_device_set_plugin (device2, "self-test");
	fu_device_set_remove_delay (device2, FU_DEVICE_REMOVE_DELAY_USER_REPLUG);
	fu_device_convert_instance_ids (device2);

	/* not yet added */
	ret = fu_device_list_wait_for_replug (device_list, device1, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* add device */
	fu_device_list_add (device_list, device1);

	/* not waiting */
	ret = fu_device_list_wait_for_replug (device_list, device1, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* waiting */
	helper.device_old = device1;
	helper.device_new = device2;
	helper.device_list = device_list;
	g_timeout_add (100, fu_device_list_remove_cb, &helper);
	g_timeout_add (200, fu_device_list_add_cb, &helper);
	fu_device_add_flag (device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	ret = fu_device_list_wait_for_replug (device_list, device1, &error);
	g_assert_no_error (error);
	g_assert (ret);
}

static void
fu_device_list_compatible_func (void)
{
	g_autoptr(FuDevice) device1 = fu_device_new ();
	g_autoptr(FuDevice) device2 = fu_device_new ();
	g_autoptr(FuDevice) device_old = NULL;
	g_autoptr(FuDeviceList) device_list = fu_device_list_new ();
	g_autoptr(GPtrArray) devices_all = NULL;
	g_autoptr(GPtrArray) devices_active = NULL;
	FuDevice *device;
	guint added_cnt = 0;
	guint changed_cnt = 0;
	guint removed_cnt = 0;

	g_signal_connect (device_list, "added",
			  G_CALLBACK (_device_list_count_cb),
			  &added_cnt);
	g_signal_connect (device_list, "removed",
			  G_CALLBACK (_device_list_count_cb),
			  &removed_cnt);
	g_signal_connect (device_list, "changed",
			  G_CALLBACK (_device_list_count_cb),
			  &changed_cnt);

	/* add one device in runtime mode */
	fu_device_set_id (device1, "device1");
	fu_device_set_plugin (device1, "plugin-for-runtime");
	fu_device_set_vendor_id (device1, "USB:0x20A0");
	fu_device_set_version (device1, "1.2.3");
	fu_device_add_instance_id (device1, "foobar");
	fu_device_add_instance_id (device1, "bootloader");
	fu_device_set_remove_delay (device1, 100);
	fu_device_convert_instance_ids (device1);
	fu_device_list_add (device_list, device1);
	g_assert_cmpint (added_cnt, ==, 1);
	g_assert_cmpint (removed_cnt, ==, 0);
	g_assert_cmpint (changed_cnt, ==, 0);

	/* add another device in bootloader mode */
	fu_device_set_id (device2, "device2");
	fu_device_set_plugin (device2, "plugin-for-bootloader");
	fu_device_add_instance_id (device2, "bootloader");
	fu_device_convert_instance_ids (device2);

	/* verify only a changed event was generated */
	added_cnt = removed_cnt = changed_cnt = 0;
	fu_device_list_remove (device_list, device1);
	fu_device_list_add (device_list, device2);
	g_assert_cmpint (added_cnt, ==, 0);
	g_assert_cmpint (removed_cnt, ==, 0);
	g_assert_cmpint (changed_cnt, ==, 1);

	/* device2 should inherit the vendor ID and version from device1 */
	g_assert_cmpstr (fu_device_get_vendor_id (device2), ==, "USB:0x20A0");
	g_assert_cmpstr (fu_device_get_version (device2), ==, "1.2.3");

	/* one device is active */
	devices_active = fu_device_list_get_active (device_list);
	g_assert_cmpint (devices_active->len, ==, 1);
	device = g_ptr_array_index (devices_active, 0);
	g_assert_cmpstr (fu_device_get_id (device), ==,
			 "1a8d0d9a96ad3e67ba76cf3033623625dc6d6882");

	/* the list knows about both devices, list in order of active->old */
	devices_all = fu_device_list_get_all (device_list);
	g_assert_cmpint (devices_all->len, ==, 2);
	device = g_ptr_array_index (devices_all, 0);
	g_assert_cmpstr (fu_device_get_id (device), ==,
			 "1a8d0d9a96ad3e67ba76cf3033623625dc6d6882");
	device = g_ptr_array_index (devices_all, 1);
	g_assert_cmpstr (fu_device_get_id (device), ==,
			 "99249eb1bd9ef0b6e192b271a8cb6a3090cfec7a");

	/* verify we can get the old device from the new device */
	device_old = fu_device_list_get_old (device_list, device2);
	g_assert (device_old == device1);
}

static void
fu_device_list_remove_chain_func (void)
{
	g_autoptr(FuDeviceList) device_list = fu_device_list_new ();
	g_autoptr(FuDevice) device_child = fu_device_new ();
	g_autoptr(FuDevice) device_parent = fu_device_new ();

	guint added_cnt = 0;
	guint changed_cnt = 0;
	guint removed_cnt = 0;

	g_signal_connect (device_list, "added",
			  G_CALLBACK (_device_list_count_cb),
			  &added_cnt);
	g_signal_connect (device_list, "removed",
			  G_CALLBACK (_device_list_count_cb),
			  &removed_cnt);
	g_signal_connect (device_list, "changed",
			  G_CALLBACK (_device_list_count_cb),
			  &changed_cnt);

	/* add child */
	fu_device_set_id (device_child, "child");
	fu_device_add_instance_id (device_child, "child-GUID-1");
	fu_device_convert_instance_ids (device_child);
	fu_device_list_add (device_list, device_child);
	g_assert_cmpint (added_cnt, ==, 1);
	g_assert_cmpint (removed_cnt, ==, 0);
	g_assert_cmpint (changed_cnt, ==, 0);

	/* add parent */
	fu_device_set_id (device_parent, "parent");
	fu_device_add_instance_id (device_parent, "parent-GUID-1");
	fu_device_convert_instance_ids (device_parent);
	fu_device_add_child (device_parent, device_child);
	fu_device_list_add (device_list, device_parent);
	g_assert_cmpint (added_cnt, ==, 2);
	g_assert_cmpint (removed_cnt, ==, 0);
	g_assert_cmpint (changed_cnt, ==, 0);

	/* make sure that removing the parent causes both to go; but the child to go first */
	fu_device_list_remove (device_list, device_parent);
	g_assert_cmpint (added_cnt, ==, 2);
	g_assert_cmpint (removed_cnt, ==, 2);
	g_assert_cmpint (changed_cnt, ==, 0);
}

static void
fu_device_list_func (void)
{
	g_autoptr(FuDeviceList) device_list = fu_device_list_new ();
	g_autoptr(FuDevice) device1 = fu_device_new ();
	g_autoptr(FuDevice) device2 = fu_device_new ();
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) devices2 = NULL;
	g_autoptr(GError) error = NULL;
	FuDevice *device;
	guint added_cnt = 0;
	guint changed_cnt = 0;
	guint removed_cnt = 0;

	g_signal_connect (device_list, "added",
			  G_CALLBACK (_device_list_count_cb),
			  &added_cnt);
	g_signal_connect (device_list, "removed",
			  G_CALLBACK (_device_list_count_cb),
			  &removed_cnt);
	g_signal_connect (device_list, "changed",
			  G_CALLBACK (_device_list_count_cb),
			  &changed_cnt);

	/* add both */
	fu_device_set_id (device1, "device1");
	fu_device_add_instance_id (device1, "foobar");
	fu_device_convert_instance_ids (device1);
	fu_device_list_add (device_list, device1);
	fu_device_set_id (device2, "device2");
	fu_device_add_instance_id (device2, "baz");
	fu_device_convert_instance_ids (device2);
	fu_device_list_add (device_list, device2);
	g_assert_cmpint (added_cnt, ==, 2);
	g_assert_cmpint (removed_cnt, ==, 0);
	g_assert_cmpint (changed_cnt, ==, 0);

	/* get all */
	devices = fu_device_list_get_all (device_list);
	g_assert_cmpint (devices->len, ==, 2);
	device = g_ptr_array_index (devices, 0);
	g_assert_cmpstr (fu_device_get_id (device), ==,
			 "99249eb1bd9ef0b6e192b271a8cb6a3090cfec7a");

	/* find by ID */
	device = fu_device_list_get_by_id (device_list,
					   "99249eb1bd9ef0b6e192b271a8cb6a3090cfec7a",
					   &error);
	g_assert_no_error (error);
	g_assert (device != NULL);
	g_assert_cmpstr (fu_device_get_id (device), ==,
					   "99249eb1bd9ef0b6e192b271a8cb6a3090cfec7a");
	g_clear_object (&device);

	/* find by GUID */
	device = fu_device_list_get_by_guid (device_list,
					     "579a3b1c-d1db-5bdc-b6b9-e2c1b28d5b8a",
					     &error);
	g_assert_no_error (error);
	g_assert (device != NULL);
	g_assert_cmpstr (fu_device_get_id (device), ==,
			 "1a8d0d9a96ad3e67ba76cf3033623625dc6d6882");
	g_clear_object (&device);

	/* find by missing GUID */
	device = fu_device_list_get_by_guid (device_list, "notfound", &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert (device == NULL);

	/* remove device */
	added_cnt = removed_cnt = changed_cnt = 0;
	fu_device_list_remove (device_list, device1);
	g_assert_cmpint (added_cnt, ==, 0);
	g_assert_cmpint (removed_cnt, ==, 1);
	g_assert_cmpint (changed_cnt, ==, 0);
	devices2 = fu_device_list_get_all (device_list);
	g_assert_cmpint (devices2->len, ==, 1);
	device = g_ptr_array_index (devices2, 0);
	g_assert_cmpstr (fu_device_get_id (device), ==,
			 "1a8d0d9a96ad3e67ba76cf3033623625dc6d6882");
}

static void
fu_device_version_format_func (void)
{
	g_autoptr(FuDevice) device = fu_device_new ();
	fu_device_set_version_format (device, FU_VERSION_FORMAT_TRIPLET);
	fu_device_set_version (device, "Ver1.2.3 RELEASE");
	g_assert_cmpstr (fu_device_get_version (device), ==, "1.2.3");
}

static void
fu_device_open_refcount_func (void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new ();
	g_autoptr(GError) error = NULL;
	fu_device_set_id (device, "test_device");
	ret = fu_device_open (device, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	ret = fu_device_open (device, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	ret = fu_device_close (device, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	ret = fu_device_close (device, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	ret = fu_device_close (device, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
	g_assert_false (ret);
}

static void
fu_device_metadata_func (void)
{
	g_autoptr(FuDevice) device = fu_device_new ();

	/* string */
	fu_device_set_metadata (device, "foo", "bar");
	g_assert_cmpstr (fu_device_get_metadata (device, "foo"), ==, "bar");
	fu_device_set_metadata (device, "foo", "baz");
	g_assert_cmpstr (fu_device_get_metadata (device, "foo"), ==, "baz");
	g_assert_null (fu_device_get_metadata (device, "unknown"));

	/* boolean */
	fu_device_set_metadata_boolean (device, "baz", TRUE);
	g_assert_cmpstr (fu_device_get_metadata (device, "baz"), ==, "true");
	g_assert_true (fu_device_get_metadata_boolean (device, "baz"));
	g_assert_false (fu_device_get_metadata_boolean (device, "unknown"));

	/* integer */
	fu_device_set_metadata_integer (device, "dum", 12345);
	g_assert_cmpstr (fu_device_get_metadata (device, "dum"), ==, "12345");
	g_assert_cmpint (fu_device_get_metadata_integer (device, "dum"), ==, 12345);
	g_assert_cmpint (fu_device_get_metadata_integer (device, "unknown"), ==, G_MAXUINT);

	/* broken integer */
	fu_device_set_metadata (device, "dum", "123junk");
	g_assert_cmpint (fu_device_get_metadata_integer (device, "dum"), ==, G_MAXUINT);
	fu_device_set_metadata (device, "huge", "4294967296"); /* not 32 bit */
	g_assert_cmpint (fu_device_get_metadata_integer (device, "huge"), ==, G_MAXUINT);
}

static void
fu_smbios_func (void)
{
	const gchar *str;
	gboolean ret;
	g_autofree gchar *dump = NULL;
	g_autoptr(FuSmbios) smbios = NULL;
	g_autoptr(GError) error = NULL;

	smbios = fu_smbios_new ();
	ret = fu_smbios_setup (smbios, &error);
	g_assert_no_error (error);
	g_assert (ret);
	dump = fu_smbios_to_string (smbios);
	if (g_getenv ("VERBOSE") != NULL)
		g_debug ("%s", dump);

	/* test for missing table */
	str = fu_smbios_get_string (smbios, 0xff, 0, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null (str);
	g_clear_error (&error);

	/* check for invalid offset */
	str = fu_smbios_get_string (smbios, FU_SMBIOS_STRUCTURE_TYPE_BIOS, 0xff, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null (str);
	g_clear_error (&error);

	/* get vendor */
	str = fu_smbios_get_string (smbios, FU_SMBIOS_STRUCTURE_TYPE_BIOS, 0x04, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (str, ==, "LENOVO");
}

static void
fu_smbios3_func (void)
{
	const gchar *str;
	gboolean ret;
	g_autofree gchar *path = NULL;
	g_autoptr(FuSmbios) smbios = NULL;
	g_autoptr(GError) error = NULL;

	path = fu_test_get_filename (TESTDATADIR, "dmi/tables64");
	g_assert_nonnull (path);

	smbios = fu_smbios_new ();
	ret = fu_smbios_setup_from_path (smbios, path, &error);
	g_assert_no_error (error);
	g_assert (ret);
	if (g_getenv ("VERBOSE") != NULL) {
		g_autofree gchar *dump = fu_smbios_to_string (smbios);
		g_debug ("%s", dump);
	}

	/* get vendor */
	str = fu_smbios_get_string (smbios, FU_SMBIOS_STRUCTURE_TYPE_BIOS, 0x04, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (str, ==, "Dell Inc.");
}

static void
fu_hwids_func (void)
{
	g_autoptr(FuHwids) hwids = NULL;
	g_autoptr(FuSmbios) smbios = NULL;
	g_autoptr(GError) error = NULL;
	gboolean ret;

	struct {
		const gchar *key;
		const gchar *value;
	} guids[] = {
		{ "Manufacturer",	"6de5d951-d755-576b-bd09-c5cf66b27234" },
		{ "HardwareID-14",	"6de5d951-d755-576b-bd09-c5cf66b27234" },
		{ "HardwareID-13",	"f8e1de5f-b68c-5f52-9d1a-f1ba52f1f773" },
		{ "HardwareID-12",	"e093d715-70f7-51f4-b6c8-b4a7e31def85" },
		{ "HardwareID-11",	"db73af4c-4612-50f7-b8a7-787cf4871847" },
		{ "HardwareID-10",	"f4275c1f-6130-5191-845c-3426247eb6a1" },
		{ "HardwareID-9",	"0cf8618d-9eff-537c-9f35-46861406eb9c" },
		{ "HardwareID-8",	"059eb22d-6dc7-59af-abd3-94bbe017f67c" },
		{ "HardwareID-7",	"da1da9b6-62f5-5f22-8aaa-14db7eeda2a4" },
		{ "HardwareID-6",	"178cd22d-ad9f-562d-ae0a-34009822cdbe" },
		{ "HardwareID-5",	"8dc9b7c5-f5d5-5850-9ab3-bd6f0549d814" },
		{ "HardwareID-4",	"660ccba8-1b78-5a33-80e6-9fb8354ee873" },
		{ "HardwareID-3",	"3faec92a-3ae3-5744-be88-495e90a7d541" },
		{ "HardwareID-2",	"f5ff077f-3eeb-5bae-be1c-e98ffe8ce5f8" },
		{ "HardwareID-1",	"b7cceb67-774c-537e-bf8b-22c6107e9a74" },
		{ "HardwareID-0",	"147efce9-f201-5fc8-ab0c-c859751c3440" },
		{ NULL, NULL }
	};

	smbios = fu_smbios_new ();
	ret = fu_smbios_setup (smbios, &error);
	g_assert_no_error (error);
	g_assert (ret);

	hwids = fu_hwids_new ();
	ret = fu_hwids_setup (hwids, smbios, &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_MANUFACTURER), ==,
			 "LENOVO");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_ENCLOSURE_KIND), ==,
			 "a");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_FAMILY), ==,
			 "ThinkPad T440s");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_PRODUCT_NAME), ==,
			 "20ARS19C0C");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_BIOS_VENDOR), ==,
			 "LENOVO");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_BIOS_VERSION), ==,
			 "GJET75WW (2.25 )");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_BIOS_MAJOR_RELEASE), ==, "02");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_BIOS_MINOR_RELEASE), ==, "19");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_PRODUCT_SKU), ==,
			 "LENOVO_MT_20AR_BU_Think_FM_ThinkPad T440s");
	for (guint i = 0; guids[i].key != NULL; i++) {
		g_autofree gchar *guid = fu_hwids_get_guid (hwids, guids[i].key, &error);
		g_assert_no_error (error);
		g_assert_cmpstr (guid, ==, guids[i].value);
	}
	for (guint i = 0; guids[i].key != NULL; i++)
		g_assert (fu_hwids_has_guid (hwids, guids[i].value));
}

static void
_plugin_status_changed_cb (FuDevice *device, GParamSpec *pspec, gpointer user_data)
{
	guint *cnt = (guint *) user_data;
	g_debug ("device %s now %s",
		 fu_device_get_id (device),
		 fwupd_status_to_string (fu_device_get_status (device)));
	(*cnt)++;
	fu_test_loop_quit ();
}

static void
_plugin_device_added_cb (FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	FuDevice **dev = (FuDevice **) user_data;
	*dev = g_object_ref (device);
	fu_test_loop_quit ();
}

static void
fu_plugin_delay_func (void)
{
	FuDevice *device_tmp;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) device = NULL;

	plugin = fu_plugin_new ();
	g_signal_connect (plugin, "device-added",
			  G_CALLBACK (_plugin_device_added_cb),
			  &device_tmp);
	g_signal_connect (plugin, "device-removed",
			  G_CALLBACK (_plugin_device_added_cb),
			  &device_tmp);

	/* add device straight away */
	device = fu_device_new ();
	fu_device_set_id (device, "testdev");
	fu_plugin_device_add (plugin, device);
	g_assert (device_tmp != NULL);
	g_assert_cmpstr (fu_device_get_id (device_tmp), ==, "b7eccd0059d6d7dc2ef76c35d6de0048cc8c029d");
	g_clear_object (&device_tmp);

	/* remove device */
	fu_plugin_device_remove (plugin, device);
	g_assert (device_tmp != NULL);
	g_assert_cmpstr (fu_device_get_id (device_tmp), ==, "b7eccd0059d6d7dc2ef76c35d6de0048cc8c029d");
	g_clear_object (&device_tmp);
}

static void
_plugin_device_register_cb (FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	/* fake being a daemon */
	fu_plugin_runner_device_register (plugin, device);
}

static void
fu_plugin_quirks_func (void)
{
	const gchar *tmp;
	gboolean ret;
	g_autoptr(FuQuirks) quirks = fu_quirks_new ();
	g_autoptr(FuPlugin) plugin = fu_plugin_new ();
	g_autoptr(GError) error = NULL;

	ret = fu_quirks_load (quirks, &error);
	g_assert_no_error (error);
	g_assert (ret);
	fu_plugin_set_quirks (plugin, quirks);

	/* exact */
	tmp = fu_plugin_lookup_quirk_by_id (plugin, "USB\\VID_0A5C&PID_6412", "Flags");
	g_assert_cmpstr (tmp, ==, "MERGE_ME,ignore-runtime");
	tmp = fu_plugin_lookup_quirk_by_id (plugin, "ACME Inc.=True", "Test");
	g_assert_cmpstr (tmp, ==, "awesome");
	tmp = fu_plugin_lookup_quirk_by_id (plugin, "CORP*", "Test");
	g_assert_cmpstr (tmp, ==, "town");
	tmp = fu_plugin_lookup_quirk_by_id (plugin, "USB\\VID_FFFF&PID_FFFF", "Flags");
	g_assert_cmpstr (tmp, ==, "");
	tmp = fu_plugin_lookup_quirk_by_id (plugin, "baz", "Unfound");
	g_assert_cmpstr (tmp, ==, NULL);
	tmp = fu_plugin_lookup_quirk_by_id (plugin, "unfound", "tests");
	g_assert_cmpstr (tmp, ==, NULL);
	tmp = fu_plugin_lookup_quirk_by_id (plugin, "unfound", "unfound");
	g_assert_cmpstr (tmp, ==, NULL);
	tmp = fu_plugin_lookup_quirk_by_id (plugin, "bb9ec3e2-77b3-53bc-a1f1-b05916715627", "Flags");
	g_assert_cmpstr (tmp, ==, "clever");
}

static void
fu_plugin_quirks_performance_func (void)
{
	g_autoptr(FuQuirks) quirks = fu_quirks_new ();
	g_autoptr(GTimer) timer = g_timer_new ();
	const gchar *keys[] = {
		"Name", "Icon", "Children", "Plugin", "Flags",
		"FirmwareSizeMin", "FirmwareSizeMax", NULL };

	/* insert */
	for (guint j = 0; j < 1000; j++) {
		g_autofree gchar *group = NULL;
		group = g_strdup_printf ("DeviceInstanceId=USB\\VID_0BDA&PID_%04X", j);
		for (guint i = 0; keys[i] != NULL; i++)
			fu_quirks_add_value (quirks, group, keys[i], "Value");
	}
	g_print ("insert=%.3fms ", g_timer_elapsed (timer, NULL) * 1000.f);

	/* lookup */
	g_timer_reset (timer);
	for (guint j = 0; j < 1000; j++) {
		g_autofree gchar *group = NULL;
		group = g_strdup_printf ("DeviceInstanceId=USB\\VID_0BDA&PID_%04X", j);
		for (guint i = 0; keys[i] != NULL; i++) {
			const gchar *tmp = fu_quirks_lookup_by_id (quirks, group, keys[i]);
			g_assert_cmpstr (tmp, ==, "Value");
		}
	}
	g_print ("lookup=%.3fms ", g_timer_elapsed (timer, NULL) * 1000.f);
}

static void
fu_plugin_quirks_device_func (void)
{
	FuDevice *device_tmp;
	GPtrArray *children;
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new ();
	g_autoptr(FuQuirks) quirks = fu_quirks_new ();
	g_autoptr(GError) error = NULL;

	ret = fu_quirks_load (quirks, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* use quirk file to set device attributes */
	fu_device_set_physical_id (device, "usb:00:05");
	fu_device_set_quirks (device, quirks);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_instance_id (device, "USB\\VID_0BDA&PID_1100");
	fu_device_convert_instance_ids (device);
	g_assert_cmpstr (fu_device_get_name (device), ==, "Hub");

	/* ensure children are created */
	children = fu_device_get_children (device);
	g_assert_cmpint (children->len, ==, 1);
	device_tmp = g_ptr_array_index (children, 0);
	g_assert_cmpstr (fu_device_get_name (device_tmp), ==, "HDMI");
	g_assert (fu_device_has_flag (device_tmp, FWUPD_DEVICE_FLAG_UPDATABLE));
}

static void
fu_plugin_hash_func (void)
{
	GError *error = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new (FU_APP_FLAGS_NONE);
	g_autoptr(FuPlugin) plugin = fu_plugin_new ();
	gboolean ret = FALSE;

	ret = fu_engine_load (engine, FU_ENGINE_LOAD_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* make sure not tainted */
	ret = fu_engine_get_tainted (engine);
	g_assert_false (ret);

	/* create a tainted plugin */
	g_setenv ("FWUPD_PLUGIN_TEST", "build-hash", TRUE);
	ret = fu_plugin_open (plugin, PLUGINBUILDDIR "/libfu_plugin_test.so", &error);
	g_assert_no_error (error);

	/* make sure it tainted now */
	g_test_expect_message ("FuEngine", G_LOG_LEVEL_WARNING, "* has incorrect built version*");
	fu_engine_add_plugin (engine, plugin);
	ret = fu_engine_get_tainted (engine);
	g_assert_true (ret);
}

static void
fu_plugin_module_func (void)
{
	GError *error = NULL;
	FuDevice *device_tmp;
	FwupdRelease *release;
	gboolean ret;
	guint cnt = 0;
	g_autofree gchar *localstatedir = NULL;
	g_autofree gchar *mapped_file_fn = NULL;
	g_autofree gchar *pending_cap = NULL;
	g_autofree gchar *history_db = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDevice) device2 = NULL;
	g_autoptr(FuDevice) device3 = NULL;
	g_autoptr(FuHistory) history = NULL;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(GBytes) blob_cab = NULL;
	g_autoptr(GMappedFile) mapped_file = NULL;

	/* create a fake device */
	plugin = fu_plugin_new ();
	g_setenv ("FWUPD_PLUGIN_TEST", "registration", TRUE);
	ret = fu_plugin_open (plugin, PLUGINBUILDDIR "/libfu_plugin_test.so", &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = fu_plugin_runner_startup (plugin, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_signal_connect (plugin, "device-added",
			  G_CALLBACK (_plugin_device_added_cb),
			  &device);
	g_signal_connect (plugin, "device-register",
			  G_CALLBACK (_plugin_device_register_cb),
			  &device);
	ret = fu_plugin_runner_coldplug (plugin, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* check we did the right thing */
	g_assert (device != NULL);
	g_assert_cmpstr (fu_device_get_id (device), ==, "08d460be0f1f9f128413f816022a6439e0078018");
	g_assert_cmpstr (fu_device_get_version_lowest (device), ==, "1.2.0");
	g_assert_cmpstr (fu_device_get_version (device), ==, "1.2.2");
	g_assert_cmpstr (fu_device_get_version_bootloader (device), ==, "0.1.2");
	g_assert_cmpstr (fu_device_get_guid_default (device), ==,
			 "b585990a-003e-5270-89d5-3705a17f9a43");
	g_assert_cmpstr (fu_device_get_name (device), ==,
			 "Integrated Webcam™");

	/* schedule an offline update */
	g_signal_connect (device, "notify::status",
			  G_CALLBACK (_plugin_status_changed_cb),
			  &cnt);
	mapped_file_fn = fu_test_get_filename (TESTDATADIR, "colorhug/firmware.bin");
	mapped_file = g_mapped_file_new (mapped_file_fn, FALSE, &error);
	g_assert_no_error (error);
	g_assert (mapped_file != NULL);
	blob_cab = g_mapped_file_get_bytes (mapped_file);
	release = fu_device_get_release_default (device);
	fwupd_release_set_version (release, "1.2.3");
	ret = fu_plugin_runner_schedule_update (plugin, device, release, blob_cab, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (cnt, ==, 1);

	/* set on the current device */
	g_assert_true (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT));

	/* lets check the history */
	history = fu_history_new ();
	device2 = fu_history_get_device_by_id (history, fu_device_get_id (device), &error);
	g_assert_no_error (error);
	g_assert (device2 != NULL);
	g_assert_cmpint (fu_device_get_update_state (device2), ==, FWUPD_UPDATE_STATE_PENDING);
	g_assert_cmpstr (fu_device_get_update_error (device2), ==, NULL);
	g_assert_true (fu_device_has_flag (device2, FWUPD_DEVICE_FLAG_NEEDS_REBOOT));
	release = fu_device_get_release_default (device2);
	g_assert (release != NULL);
	g_assert_cmpstr (fwupd_release_get_filename (release), !=, NULL);
	g_assert_cmpstr (fwupd_release_get_version (release), ==, "1.2.3");

	/* save this; we'll need to delete it later */
	pending_cap = g_strdup (fwupd_release_get_filename (release));

	/* lets do this online */
	ret = fu_plugin_runner_update (plugin, device, blob_cab,
				       FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (cnt, ==, 4);

	/* check the new version */
	g_assert_cmpstr (fu_device_get_version (device), ==, "1.2.3");
	g_assert_cmpstr (fu_device_get_version_bootloader (device), ==, "0.1.2");

	/* lets check the history */
	device3 = fu_history_get_device_by_id (history, fu_device_get_id (device), &error);
	g_assert_no_error (error);
	g_assert (device3 != NULL);
	g_assert_cmpint (fu_device_get_update_state (device3), ==, FWUPD_UPDATE_STATE_SUCCESS);
	g_assert_cmpstr (fu_device_get_update_error (device3), ==, NULL);

	/* get the status */
	device_tmp = fu_device_new ();
	fu_device_set_id (device_tmp, "FakeDevice");
	ret = fu_plugin_runner_get_results (plugin, device_tmp, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (fu_device_get_update_state (device_tmp), ==, FWUPD_UPDATE_STATE_SUCCESS);
	g_assert_cmpstr (fu_device_get_update_error (device_tmp), ==, NULL);

	/* clear */
	ret = fu_plugin_runner_clear_results (plugin, device_tmp, &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_object_unref (device_tmp);
	g_clear_error (&error);

	/* delete files */
	localstatedir = fu_common_get_path (FU_PATH_KIND_LOCALSTATEDIR_PKG);
	history_db = g_build_filename (localstatedir, "pending.db", NULL);
	g_unlink (history_db);
	g_unlink (pending_cap);
}

static void
fu_plugin_list_func (void)
{
	GPtrArray *plugins;
	FuPlugin *plugin;
	g_autoptr(FuPluginList) plugin_list = fu_plugin_list_new ();
	g_autoptr(FuPlugin) plugin1 = fu_plugin_new ();
	g_autoptr(FuPlugin) plugin2 = fu_plugin_new ();
	g_autoptr(GError) error = NULL;

	fu_plugin_set_name (plugin1, "plugin1");
	fu_plugin_set_name (plugin2, "plugin2");

	/* get all the plugins */
	fu_plugin_list_add (plugin_list, plugin1);
	fu_plugin_list_add (plugin_list, plugin2);
	plugins = fu_plugin_list_get_all (plugin_list);
	g_assert_cmpint (plugins->len, ==, 2);

	/* get a single plugin */
	plugin = fu_plugin_list_find_by_name (plugin_list, "plugin1", &error);
	g_assert_no_error (error);
	g_assert (plugin != NULL);
	g_assert_cmpstr (fu_plugin_get_name (plugin), ==, "plugin1");

	/* does not exist */
	plugin = fu_plugin_list_find_by_name (plugin_list, "nope", &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert (plugin == NULL);
}

static void
fu_plugin_list_depsolve_func (void)
{
	GPtrArray *plugins;
	FuPlugin *plugin;
	gboolean ret;
	g_autoptr(FuPluginList) plugin_list = fu_plugin_list_new ();
	g_autoptr(FuPlugin) plugin1 = fu_plugin_new ();
	g_autoptr(FuPlugin) plugin2 = fu_plugin_new ();
	g_autoptr(GError) error = NULL;

	fu_plugin_set_name (plugin1, "plugin1");
	fu_plugin_set_name (plugin2, "plugin2");

	/* add rule then depsolve */
	fu_plugin_list_add (plugin_list, plugin1);
	fu_plugin_list_add (plugin_list, plugin2);
	fu_plugin_add_rule (plugin1, FU_PLUGIN_RULE_RUN_AFTER, "plugin2");
	ret = fu_plugin_list_depsolve (plugin_list, &error);
	g_assert_no_error (error);
	g_assert (ret);
	plugins = fu_plugin_list_get_all (plugin_list);
	g_assert_cmpint (plugins->len, ==, 2);
	plugin = g_ptr_array_index (plugins, 0);
	g_assert_cmpstr (fu_plugin_get_name (plugin), ==, "plugin2");
	g_assert_cmpint (fu_plugin_get_order (plugin), ==, 0);
	g_assert (fu_plugin_get_enabled (plugin));

	/* add another rule, then re-depsolve */
	fu_plugin_add_rule (plugin1, FU_PLUGIN_RULE_CONFLICTS, "plugin2");
	ret = fu_plugin_list_depsolve (plugin_list, &error);
	g_assert_no_error (error);
	g_assert (ret);
	plugin = fu_plugin_list_find_by_name (plugin_list, "plugin1", &error);
	g_assert_no_error (error);
	g_assert (plugin != NULL);
	g_assert (fu_plugin_get_enabled (plugin));
	plugin = fu_plugin_list_find_by_name (plugin_list, "plugin2", &error);
	g_assert_no_error (error);
	g_assert (plugin != NULL);
	g_assert (!fu_plugin_get_enabled (plugin));
}

static void
fu_history_migrate_func (void)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file_dst = NULL;
	g_autoptr(GFile) file_src = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuHistory) history = NULL;
	g_autofree gchar *filename = NULL;

	/* load old version */
	filename = fu_test_get_filename (TESTDATADIR, "history_v1.db");
	file_src = g_file_new_for_path (filename);
	file_dst = g_file_new_for_path ("/tmp/fwupd-self-test/var/lib/fwupd/pending.db");
	ret = g_file_copy (file_src, file_dst, G_FILE_COPY_OVERWRITE, NULL,
			   NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* create, migrating as required */
	history = fu_history_new ();
	g_assert (history != NULL);

	/* get device */
	device = fu_history_get_device_by_id (history, "2ba16d10df45823dd4494ff10a0bfccfef512c9d", &error);
	g_assert_no_error (error);
	g_assert (device != NULL);
	g_assert_cmpstr (fu_device_get_id (device), ==, "2ba16d10df45823dd4494ff10a0bfccfef512c9d");
}

static void
fu_history_func (void)
{
	GError *error = NULL;
	GPtrArray *checksums;
	gboolean ret;
	FuDevice *device;
	FwupdRelease *release;
	g_autoptr(FuDevice) device_found = NULL;
	g_autoptr(FuHistory) history = NULL;
	g_autoptr(GPtrArray) approved_firmware = NULL;
	g_autofree gchar *dirname = NULL;
	g_autofree gchar *filename = NULL;

	/* create */
	history = fu_history_new ();
	g_assert (history != NULL);

	/* delete the database */
	dirname = fu_common_get_path (FU_PATH_KIND_LOCALSTATEDIR_PKG);
	if (!g_file_test (dirname, G_FILE_TEST_IS_DIR))
		return;
	filename = g_build_filename (dirname, "pending.db", NULL);
	g_unlink (filename);

	/* add a device */
	device = fu_device_new ();
	fu_device_set_id (device, "self-test");
	fu_device_set_name (device, "ColorHug"),
	fu_device_set_version (device, "3.0.1"),
	fu_device_set_update_state (device, FWUPD_UPDATE_STATE_FAILED);
	fu_device_set_update_error (device, "word");
	fu_device_add_guid (device, "827edddd-9bb6-5632-889f-2c01255503da");
	fu_device_set_flags (device, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_set_created (device, 123);
	fu_device_set_modified (device, 456);
	release = fwupd_release_new ();
	fwupd_release_set_filename (release, "/var/lib/dave.cap"),
	fwupd_release_add_checksum (release, "abcdef");
	fwupd_release_set_version (release, "3.0.2");
	fwupd_release_add_metadata_item (release, "FwupdVersion", VERSION);
	ret = fu_history_add_device (history, device, release, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_object_unref (release);

	/* ensure database was created */
	g_assert (g_file_test (filename, G_FILE_TEST_EXISTS));

	g_object_unref (device);

	/* get device */
	device = fu_history_get_device_by_id (history, "2ba16d10df45823dd4494ff10a0bfccfef512c9d", &error);
	g_assert_no_error (error);
	g_assert (device != NULL);
	g_assert_cmpstr (fu_device_get_id (device), ==, "2ba16d10df45823dd4494ff10a0bfccfef512c9d");
	g_assert_cmpstr (fu_device_get_name (device), ==, "ColorHug");
	g_assert_cmpstr (fu_device_get_version (device), ==, "3.0.1");
	g_assert_cmpint (fu_device_get_update_state (device), ==, FWUPD_UPDATE_STATE_FAILED);
	g_assert_cmpstr (fu_device_get_update_error (device), ==, "word");
	g_assert_cmpstr (fu_device_get_guid_default (device), ==, "827edddd-9bb6-5632-889f-2c01255503da");
	g_assert_cmpint (fu_device_get_flags (device), ==, FWUPD_DEVICE_FLAG_INTERNAL);
	g_assert_cmpint (fu_device_get_created (device), ==, 123);
	g_assert_cmpint (fu_device_get_modified (device), ==, 456);
	release = fu_device_get_release_default (device);
	g_assert (release != NULL);
	g_assert_cmpstr (fwupd_release_get_version (release), ==, "3.0.2");
	g_assert_cmpstr (fwupd_release_get_filename (release), ==, "/var/lib/dave.cap");
	g_assert_cmpstr (fwupd_release_get_metadata_item (release, "FwupdVersion"), ==, VERSION);
	checksums = fwupd_release_get_checksums (release);
	g_assert (checksums != NULL);
	g_assert_cmpint (checksums->len, ==, 1);
	g_assert_cmpstr (fwupd_checksum_get_by_kind (checksums, G_CHECKSUM_SHA1), ==, "abcdef");
	ret = fu_history_add_device (history, device, release, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* get device that does not exist */
	device_found = fu_history_get_device_by_id (history, "XXXXXXXXXXXXX", &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert (device_found == NULL);
	g_clear_error (&error);

	/* get device that does exist */
	device_found = fu_history_get_device_by_id (history, "2ba16d10df45823dd4494ff10a0bfccfef512c9d", &error);
	g_assert_no_error (error);
	g_assert (device_found != NULL);
	g_object_unref (device_found);

	/* remove device */
	ret = fu_history_remove_device (history, device, release, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_object_unref (device);

	/* get device that does not exist */
	device_found = fu_history_get_device_by_id (history, "2ba16d10df45823dd4494ff10a0bfccfef512c9d", &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert (device_found == NULL);
	g_clear_error (&error);

	/* approved firmware */
	ret = fu_history_clear_approved_firmware (history, &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = fu_history_add_approved_firmware (history, "foo", &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = fu_history_add_approved_firmware (history, "bar", &error);
	g_assert_no_error (error);
	g_assert (ret);
	approved_firmware = fu_history_get_approved_firmware (history, &error);
	g_assert_no_error (error);
	g_assert_nonnull (approved_firmware);
	g_assert_cmpint (approved_firmware->len, ==, 2);
	g_assert_cmpstr (g_ptr_array_index (approved_firmware, 0), ==, "foo");
	g_assert_cmpstr (g_ptr_array_index (approved_firmware, 1), ==, "bar");
}

static void
fu_keyring_gpg_func (void)
{
#ifdef ENABLE_GPG
	gboolean ret;
	g_autofree gchar *fw_fail = NULL;
	g_autofree gchar *fw_pass = NULL;
	g_autofree gchar *pki_dir = NULL;
	g_autoptr(FuKeyring) keyring = NULL;
	g_autoptr(FuKeyringResult) result_fail = NULL;
	g_autoptr(FuKeyringResult) result_pass = NULL;
	g_autoptr(GBytes) blob_fail = NULL;
	g_autoptr(GBytes) blob_pass = NULL;
	g_autoptr(GBytes) blob_sig = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *sig_gpgme =
	"-----BEGIN PGP SIGNATURE-----\n"
	"Version: GnuPG v1\n\n"
	"iQEcBAABCAAGBQJVt0B4AAoJEEim2A5FOLrCFb8IAK+QTLY34Wu8xZ8nl6p3JdMu"
	"HOaifXAmX7291UrsFRwdabU2m65pqxQLwcoFrqGv738KuaKtu4oIwo9LIrmmTbEh"
	"IID8uszxBt0bMdcIHrvwd+ADx+MqL4hR3guXEE3YOBTLvv2RF1UBcJPInNf/7Ui1"
	"3lW1c3trL8RAJyx1B5RdKqAMlyfwiuvKM5oT4SN4uRSbQf+9mt78ZSWfJVZZH/RR"
	"H9q7PzR5GdmbsRPM0DgC27Trvqjo3MzoVtoLjIyEb/aWqyulUbnJUNKPYTnZgkzM"
	"v2yVofWKIM3e3wX5+MOtf6EV58mWa2cHJQ4MCYmpKxbIvAIZagZ4c9A8BA6tQWg="
	"=fkit\n"
	"-----END PGP SIGNATURE-----\n";

	/* add keys to keyring */
	keyring = fu_keyring_gpg_new ();
	ret = fu_keyring_setup (keyring, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	pki_dir = fu_test_get_filename (TESTDATADIR, "pki");
	g_assert_nonnull (pki_dir);
	ret = fu_keyring_add_public_keys (keyring, pki_dir, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	/* verify with GnuPG */
	fw_pass = fu_test_get_filename (TESTDATADIR, "colorhug/firmware.bin");
	g_assert_nonnull (fw_pass);
	blob_pass = fu_common_get_contents_bytes (fw_pass, &error);
	g_assert_no_error (error);
	g_assert_nonnull (blob_pass);
	blob_sig = g_bytes_new_static (sig_gpgme, strlen (sig_gpgme));
	result_pass = fu_keyring_verify_data (keyring, blob_pass, blob_sig,
					      FU_KEYRING_VERIFY_FLAG_NONE,
					      &error);
	g_assert_no_error (error);
	g_assert_nonnull (result_pass);
	g_assert_cmpint (fu_keyring_result_get_timestamp (result_pass), == , 1438072952);
	g_assert_cmpstr (fu_keyring_result_get_authority (result_pass), == ,
			 "3FC6B804410ED0840D8F2F9748A6D80E4538BAC2");

	/* verify will fail with GnuPG */
	fw_fail = fu_test_get_filename (TESTDATADIR, "colorhug/colorhug-als-3.0.2.cab");
	g_assert_nonnull (fw_fail);
	blob_fail = fu_common_get_contents_bytes (fw_fail, &error);
	g_assert_no_error (error);
	g_assert_nonnull (blob_fail);
	result_fail = fu_keyring_verify_data (keyring, blob_fail, blob_sig,
					      FU_KEYRING_VERIFY_FLAG_NONE, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_SIGNATURE_INVALID);
	g_assert_null (result_fail);
	g_clear_error (&error);
#else
	g_test_skip ("no GnuPG support enabled");
#endif
}

static void
fu_keyring_pkcs7_func (void)
{
#ifdef ENABLE_PKCS7
	gboolean ret;
	g_autofree gchar *fw_fail = NULL;
	g_autofree gchar *fw_pass = NULL;
	g_autofree gchar *pki_dir = NULL;
	g_autofree gchar *sig_fn = NULL;
	g_autofree gchar *sig_fn2 = NULL;
	g_autoptr(FuKeyring) keyring = NULL;
	g_autoptr(FuKeyringResult) result_fail = NULL;
	g_autoptr(FuKeyringResult) result_pass = NULL;
	g_autoptr(GBytes) blob_fail = NULL;
	g_autoptr(GBytes) blob_pass = NULL;
	g_autoptr(GBytes) blob_sig = NULL;
	g_autoptr(GBytes) blob_sig2 = NULL;
	g_autoptr(GError) error = NULL;

	/* add keys to keyring */
	keyring = fu_keyring_pkcs7_new ();
	ret = fu_keyring_setup (keyring, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	pki_dir = fu_test_get_filename (TESTDATADIR_SRC, "pki");
	g_assert_nonnull (pki_dir);
	ret = fu_keyring_add_public_keys (keyring, pki_dir, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	/* verify with a signature from the old LVFS */
	fw_pass = fu_test_get_filename (TESTDATADIR_SRC, "colorhug/firmware.bin");
	g_assert_nonnull (fw_pass);
	blob_pass = fu_common_get_contents_bytes (fw_pass, &error);
	g_assert_no_error (error);
	g_assert_nonnull (blob_pass);
	sig_fn = fu_test_get_filename (TESTDATADIR_SRC, "colorhug/firmware.bin.p7b");
	g_assert_nonnull (sig_fn);
	blob_sig = fu_common_get_contents_bytes (sig_fn, &error);
	g_assert_no_error (error);
	g_assert_nonnull (blob_sig);
	result_pass = fu_keyring_verify_data (keyring, blob_pass, blob_sig,
					      FU_KEYRING_VERIFY_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert_nonnull (result_pass);
	g_assert_cmpint (fu_keyring_result_get_timestamp (result_pass), >= , 1502871248);
	g_assert_cmpstr (fu_keyring_result_get_authority (result_pass), == , "O=Linux Vendor Firmware Project,CN=LVFS CA");

	/* verify will fail with a self-signed signature */
	sig_fn2 = fu_test_get_filename (TESTDATADIR_DST, "colorhug/firmware.bin.p7c");
	g_assert_nonnull (sig_fn2);
	blob_sig2 = fu_common_get_contents_bytes (sig_fn2, &error);
	g_assert_no_error (error);
	g_assert_nonnull (blob_sig2);
	result_fail = fu_keyring_verify_data (keyring, blob_pass, blob_sig2,
					      FU_KEYRING_VERIFY_FLAG_NONE, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_SIGNATURE_INVALID);
	g_assert_null (result_fail);
	g_clear_error (&error);

	/* verify will fail with valid signature and different data */
	fw_fail = fu_test_get_filename (TESTDATADIR, "colorhug/colorhug-als-3.0.2.cab");
	g_assert_nonnull (fw_fail);
	blob_fail = fu_common_get_contents_bytes (fw_fail, &error);
	g_assert_no_error (error);
	g_assert_nonnull (blob_fail);
	result_fail = fu_keyring_verify_data (keyring, blob_fail, blob_sig,
					      FU_KEYRING_VERIFY_FLAG_NONE, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_SIGNATURE_INVALID);
	g_assert_null (result_fail);
	g_clear_error (&error);
#else
	g_test_skip ("no GnuTLS support enabled");
#endif
}

static void
fu_keyring_pkcs7_self_signed_func (void)
{
#ifdef ENABLE_PKCS7
	gboolean ret;
	g_autoptr(FuKeyring) kr = NULL;
	g_autoptr(FuKeyringResult) kr_result = NULL;
	g_autoptr(GBytes) payload = NULL;
	g_autoptr(GBytes) signature = NULL;
	g_autoptr(GError) error = NULL;

#ifndef HAVE_GNUTLS_3_6_0
	/* required to create the private key correctly */
	g_test_skip ("GnuTLS version too old");
	return;
#endif

	/* create detached signature and verify */
	kr = fu_keyring_pkcs7_new ();
	ret = fu_keyring_setup (kr, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	payload = fu_common_get_contents_bytes ("/etc/machine-id", &error);
	g_assert_no_error (error);
	g_assert_nonnull (payload);
	signature = fu_keyring_sign_data (kr, payload, FU_KEYRING_SIGN_FLAG_ADD_TIMESTAMP, &error);
	g_assert_no_error (error);
	g_assert_nonnull (signature);
	ret = fu_common_set_contents_bytes ("/tmp/test.p7b", signature, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	kr_result = fu_keyring_verify_data (kr, payload, signature,
					    FU_KEYRING_VERIFY_FLAG_USE_CLIENT_CERT, &error);
	g_assert_no_error (error);
	g_assert_nonnull (kr_result);
#else
	g_test_skip ("no GnuTLS support enabled");
#endif
}

static void
fu_common_firmware_builder_func (void)
{
	const gchar *data;
	g_autofree gchar *archive_fn = NULL;
	g_autoptr(GBytes) archive_blob = NULL;
	g_autoptr(GBytes) firmware_blob = NULL;
	g_autoptr(GError) error = NULL;

	/* get test file */
	archive_fn = fu_test_get_filename (TESTDATADIR, "builder/firmware.tar");
	g_assert (archive_fn != NULL);
	archive_blob = fu_common_get_contents_bytes (archive_fn, &error);
	g_assert_no_error (error);
	g_assert (archive_blob != NULL);

	/* generate the firmware */
	firmware_blob = fu_common_firmware_builder (archive_blob,
						    "startup.sh",
						    "firmware.bin",
						    &error);
	if (firmware_blob == NULL) {
		if (g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_PERMISSION_DENIED)) {
			g_test_skip ("Missing permissions to create namespace in container");
			return;
		}
		if (g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			g_test_skip ("User namespaces not supported in container");
			return;
		}
		g_assert_no_error (error);
	}

	/* check it */
	data = g_bytes_get_data (firmware_blob, NULL);
	g_assert_cmpstr (data, ==, "xobdnas eht ni gninnur");
}

static void
fu_test_stdout_cb (const gchar *line, gpointer user_data)
{
	guint *lines = (guint *) user_data;
	g_debug ("got '%s'", line);
	(*lines)++;
}

static gboolean
_open_cb (GObject *device, GError **error)
{
	g_assert_cmpstr (g_object_get_data (device, "state"), ==, "closed");
	g_object_set_data (device, "state", (gpointer) "opened");
	return TRUE;
}

static gboolean
_close_cb (GObject *device, GError **error)
{
	g_assert_cmpstr (g_object_get_data (device, "state"), ==, "opened");
	g_object_set_data (device, "state", (gpointer) "closed-on-unref");
	return TRUE;
}

static void
fu_device_locker_func (void)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GObject) device = g_object_new (G_TYPE_OBJECT, NULL);

	g_object_set_data (device, "state", (gpointer) "closed");
	locker = fu_device_locker_new_full (device, _open_cb, _close_cb, &error);
	g_assert_no_error (error);
	g_assert_nonnull (locker);
	g_clear_object (&locker);
	g_assert_cmpstr (g_object_get_data (device, "state"), ==, "closed-on-unref");
}

static gboolean
_fail_open_cb (GObject *device, GError **error)
{
	g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "fail");
	return FALSE;
}

static gboolean
_fail_close_cb (GObject *device, GError **error)
{
	g_assert_not_reached ();
	return TRUE;
}

static void
fu_device_locker_fail_func (void)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GObject) device = g_object_new (G_TYPE_OBJECT, NULL);
	locker = fu_device_locker_new_full (device, _fail_open_cb, _fail_close_cb, &error);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
	g_assert_null (locker);
}

static void
fu_common_spawn_func (void)
{
	gboolean ret;
	guint lines = 0;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *fn = NULL;
	const gchar *argv[3] = { "replace", "test", NULL };

	fn = fu_test_get_filename (TESTDATADIR, "spawn.sh");
	g_assert (fn != NULL);
	argv[0] = fn;
	ret = fu_common_spawn_sync (argv,
				    fu_test_stdout_cb, &lines, 0, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (lines, ==, 6);
}

static void
fu_common_spawn_timeout_func (void)
{
	gboolean ret;
	guint lines = 0;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *fn = NULL;
	const gchar *argv[3] = { "replace", "test", NULL };

	fn = fu_test_get_filename (TESTDATADIR, "spawn.sh");
	g_assert (fn != NULL);
	argv[0] = fn;
	ret = fu_common_spawn_sync (argv, fu_test_stdout_cb, &lines, 50, NULL, &error);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
	g_assert (!ret);
	g_assert_cmpint (lines, ==, 1);
}

static void
fu_progressbar_func (void)
{
	g_autoptr(FuProgressbar) progressbar = fu_progressbar_new ();

	fu_progressbar_set_length_status (progressbar, 20);
	fu_progressbar_set_length_percentage (progressbar, 50);

	g_print ("\n");
	for (guint i = 0; i < 100; i++) {
		fu_progressbar_update (progressbar, FWUPD_STATUS_DECOMPRESSING, i);
		g_usleep (10000);
	}
	fu_progressbar_update (progressbar, FWUPD_STATUS_IDLE, 0);
	for (guint i = 0; i < 100; i++) {
		guint pc = (i > 25 && i < 75) ? 0 : i;
		fu_progressbar_update (progressbar, FWUPD_STATUS_LOADING, pc);
		g_usleep (10000);
	}
	fu_progressbar_update (progressbar, FWUPD_STATUS_IDLE, 0);

	for (guint i = 0; i < 5000; i++) {
		fu_progressbar_update (progressbar, FWUPD_STATUS_LOADING, 0);
		g_usleep (1000);
	}
	fu_progressbar_update (progressbar, FWUPD_STATUS_IDLE, 0);
}

static void
fu_common_endian_func (void)
{
	guint8 buf[2];

	fu_common_write_uint16 (buf, 0x1234, G_LITTLE_ENDIAN);
	g_assert_cmpint (buf[0], ==, 0x34);
	g_assert_cmpint (buf[1], ==, 0x12);
	g_assert_cmpint (fu_common_read_uint16 (buf, G_LITTLE_ENDIAN), ==, 0x1234);

	fu_common_write_uint16 (buf, 0x1234, G_BIG_ENDIAN);
	g_assert_cmpint (buf[0], ==, 0x12);
	g_assert_cmpint (buf[1], ==, 0x34);
	g_assert_cmpint (fu_common_read_uint16 (buf, G_BIG_ENDIAN), ==, 0x1234);
}

static GBytes *
_build_cab (GCabCompression compression, ...)
{
#ifdef HAVE_GCAB_1_0
	gboolean ret;
	va_list args;
	g_autoptr(GCabCabinet) cabinet = NULL;
	g_autoptr(GCabFolder) cabfolder = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GOutputStream) op = NULL;

	/* create a new archive */
	cabinet = gcab_cabinet_new ();
	cabfolder = gcab_folder_new (compression);
	ret = gcab_cabinet_add_folder (cabinet, cabfolder, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* add each file */
	va_start (args, compression);
	do {
		const gchar *fn;
		const gchar *text;
		g_autoptr(GCabFile) cabfile = NULL;
		g_autoptr(GBytes) blob = NULL;

		/* get filename */
		fn = va_arg (args, const gchar *);
		if (fn == NULL)
			break;

		/* get contents */
		text = va_arg (args, const gchar *);
		if (text == NULL)
			break;
		g_debug ("creating %s with %s", fn, text);

		/* add a GCabFile to the cabinet */
		blob = g_bytes_new_static (text, strlen (text));
		cabfile = gcab_file_new_with_bytes (fn, blob);
		ret = gcab_folder_add_file (cabfolder, cabfile, FALSE, NULL, &error);
		g_assert_no_error (error);
		g_assert (ret);
	} while (TRUE);
	va_end (args);

	/* write the archive to a blob */
	op = g_memory_output_stream_new_resizable ();
	ret = gcab_cabinet_write_simple  (cabinet, op, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = g_output_stream_close (op, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	return g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (op));
#else
	return NULL;
#endif
}

static void
_plugin_composite_device_added_cb (FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	GPtrArray *devices = (GPtrArray *) user_data;
	g_ptr_array_add (devices, g_object_ref (device));
}

static void
fu_plugin_composite_func (void)
{
	GError *error = NULL;
	gboolean ret;
	g_autoptr(FuEngine) engine = fu_engine_new (FU_APP_FLAGS_NONE);
	g_autoptr(FuPlugin) plugin = fu_plugin_new ();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GPtrArray) components = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) install_tasks = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	g_autoptr(XbSilo) silo_empty = xb_silo_new ();
	g_autoptr(XbSilo) silo = NULL;

	/* no metadata in daemon */
	fu_engine_set_silo (engine, silo_empty);

	/* create CAB file */
	blob = _build_cab (GCAB_COMPRESSION_NONE,
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
			   "firmware.bin", "world",
			   NULL);
	if (blob == NULL) {
		g_test_skip ("libgcab too old");
		return;
	}
	silo = fu_common_cab_build_silo (blob, 10240, &error);
	g_assert_no_error (error);
	g_assert_nonnull (silo);
	components = xb_silo_query (silo, "components/component", 0, &error);
	g_assert_no_error (error);
	g_assert_nonnull (components);
	g_assert_cmpint (components->len, ==, 3);

	/* set up dummy plugin */
	g_setenv ("FWUPD_PLUGIN_TEST", "composite", TRUE);
	ret = fu_plugin_open (plugin, PLUGINBUILDDIR "/libfu_plugin_test.so", &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	fu_engine_add_plugin (engine, plugin);

	ret = fu_plugin_runner_startup (plugin, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	g_signal_connect (plugin, "device-added",
			  G_CALLBACK (_plugin_composite_device_added_cb),
			  devices);

	ret = fu_plugin_runner_coldplug (plugin, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	/* check we found all composite devices  */
	g_assert_cmpint (devices->len, ==, 3);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index (devices, i);
		fu_engine_add_device (engine, device);
		if (g_strcmp0 (fu_device_get_id (device),
				"08d460be0f1f9f128413f816022a6439e0078018") == 0) {
			g_assert_cmpstr (fu_device_get_version (device), ==, "1.2.2");
		} else if (g_strcmp0 (fu_device_get_id (device),
					"c0a0a4aa6480ac28eea1ce164fbb466ca934e1ff") == 0) {
			g_assert_cmpstr (fu_device_get_version (device), ==, "1");
			g_assert_nonnull (fu_device_get_parent (device));
		} else if (g_strcmp0 (fu_device_get_id (device),
					"bf455e9f371d2608d1cb67660fd2b335d3f6ef73") == 0) {
			g_assert_cmpstr (fu_device_get_version (device), ==, "10");
			g_assert_nonnull (fu_device_get_parent (device));
		}
	}

	/* produce install tasks */
	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index (components, i);

		/* do any devices pass the requirements */
		for (guint j = 0; j < devices->len; j++) {
			FuDevice *device = g_ptr_array_index (devices, j);
			g_autoptr(FuInstallTask) task = NULL;
			g_autoptr(GError) error_local = NULL;

			/* is this component valid for the device */
			task = fu_install_task_new (device, component);
			if (!fu_engine_check_requirements (engine,
							   task,
							   0,
							   &error_local)) {
				g_debug ("requirement on %s:%s failed: %s",
					 fu_device_get_id (device),
					 xb_node_query_text (component, "id", NULL),
					 error_local->message);
				continue;
			}

			g_ptr_array_add (install_tasks, g_steal_pointer (&task));
		}
	}
	g_assert_cmpint (install_tasks->len, ==, 3);

	/* install the cab */
	ret = fu_engine_install_tasks (engine,
				       install_tasks,
				       blob,
				       FWUPD_DEVICE_FLAG_NONE,
				       &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	/* verify everything upgraded */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index (devices, i);
		const gchar *metadata;
		if (g_strcmp0 (fu_device_get_id (device),
				"08d460be0f1f9f128413f816022a6439e0078018") == 0) {
			g_assert_cmpstr (fu_device_get_version (device), ==, "1.2.3");
		} else if (g_strcmp0 (fu_device_get_id (device),
					"c0a0a4aa6480ac28eea1ce164fbb466ca934e1ff") == 0) {
			g_assert_cmpstr (fu_device_get_version (device), ==, "2");
		} else if (g_strcmp0 (fu_device_get_id (device),
					"bf455e9f371d2608d1cb67660fd2b335d3f6ef73") == 0) {
			g_assert_cmpstr (fu_device_get_version (device), ==, "11");
		}

		/* verify prepare and cleanup ran on all devices */
		metadata = fu_device_get_metadata (device, "frimbulator");
		g_assert_cmpstr (metadata, ==, "1");
		metadata = fu_device_get_metadata (device, "frombulator");
		g_assert_cmpstr (metadata, ==, "1");
	}
}

static void
fu_common_store_cab_func (void)
{
	GBytes *blob_tmp;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbNode) csum = NULL;
	g_autoptr(XbNode) rel = NULL;
	g_autoptr(XbNode) req = NULL;
	g_autoptr(XbSilo) silo = NULL;

	/* create silo */
	blob = _build_cab (GCAB_COMPRESSION_NONE,
			   "acme.metainfo.xml",
	"<component type=\"firmware\">\n"
	"  <id>com.acme.example.firmware</id>\n"
	"  <name>ACME Firmware</name>\n"
	"  <provides>\n"
	"    <firmware type=\"flashed\">ae56e3fb-6528-5bc4-8b03-012f124075d7</firmware>\n"
	"  </provides>\n"
	"  <releases>\n"
	"    <release version=\"1.2.3\" date=\"2017-09-06\">\n"
	"      <size type=\"installed\">5</size>\n"
	"      <checksum filename=\"firmware.dfu\" target=\"content\" type=\"sha1\">7c211433f02071597741e6ff5a8ea34789abbf43</checksum>\n"
	"      <description><p>We fixed things</p></description>\n"
	"    </release>\n"
	"  </releases>\n"
	"  <requires>\n"
	"    <id compare=\"ge\" version=\"1.0.1\">org.freedesktop.fwupd</id>\n"
	"  </requires>\n"
	"</component>",
			   "firmware.dfu", "world",
			   "firmware.dfu.asc", "signature",
			   NULL);
	if (blob == NULL) {
		g_test_skip ("libgcab too old");
		return;
	}
	silo = fu_common_cab_build_silo (blob, 10240, &error);
	g_assert_no_error (error);
	g_assert_nonnull (silo);

	/* verify */
	component = xb_silo_query_first (silo, "components/component/id[text()='com.acme.example.firmware']/..", &error);
	g_assert_no_error (error);
	g_assert_nonnull (component);
	rel = xb_node_query_first (component, "releases/release", &error);
	g_assert_no_error (error);
	g_assert_nonnull (rel);
	g_assert_cmpstr (xb_node_get_attr (rel, "version"), ==, "1.2.3");
	csum = xb_node_query_first (rel, "checksum[@target='content']", &error);
	g_assert_nonnull (csum);
	g_assert_cmpstr (xb_node_get_text (csum), ==, "7c211433f02071597741e6ff5a8ea34789abbf43");
	blob_tmp = xb_node_get_data (rel, "fwupd::ReleaseBlob(firmware.dfu)");
	g_assert_nonnull (blob_tmp);
	blob_tmp = xb_node_get_data (rel, "fwupd::ReleaseBlob(firmware.dfu.asc)");
	g_assert_nonnull (blob_tmp);
	req = xb_node_query_first (component, "requires/id", &error);
	g_assert_no_error (error);
	g_assert_nonnull (req);
}

static void
fu_common_store_cab_unsigned_func (void)
{
	GBytes *blob_tmp;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbNode) csum = NULL;
	g_autoptr(XbNode) rel = NULL;
	g_autoptr(XbSilo) silo = NULL;

	/* create silo */
	blob = _build_cab (GCAB_COMPRESSION_NONE,
			   "acme.metainfo.xml",
	"<component type=\"firmware\">\n"
	"  <id>com.acme.example.firmware</id>\n"
	"  <releases>\n"
	"    <release version=\"1.2.3\"/>\n"
	"  </releases>\n"
	"</component>",
			   "firmware.bin", "world",
			   NULL);
	if (blob == NULL) {
		g_test_skip ("libgcab too old");
		return;
	}
	silo = fu_common_cab_build_silo (blob, 10240, &error);
	g_assert_no_error (error);
	g_assert_nonnull (silo);

	/* verify */
	component = xb_silo_query_first (silo, "components/component/id[text()='com.acme.example.firmware']/..", &error);
	g_assert_no_error (error);
	g_assert_nonnull (component);
	rel = xb_node_query_first (component, "releases/release", &error);
	g_assert_no_error (error);
	g_assert_nonnull (rel);
	g_assert_cmpstr (xb_node_get_attr (rel, "version"), ==, "1.2.3");
	csum = xb_node_query_first (rel, "checksum[@target='content']", &error);
	g_assert_null (csum);
	blob_tmp = xb_node_get_data (rel, "fwupd::ReleaseBlob(firmware.bin)");
	g_assert_nonnull (blob_tmp);
	blob_tmp = xb_node_get_data (rel, "fwupd::ReleaseBlob(firmware.bin.asc)");
	g_assert_null (blob_tmp);
}

static void
fu_common_store_cab_folder_func (void)
{
	GBytes *blob_tmp;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbNode) rel = NULL;
	g_autoptr(XbSilo) silo = NULL;

	/* create silo */
	blob = _build_cab (GCAB_COMPRESSION_NONE,
			   "lvfs\\acme.metainfo.xml",
	"<component type=\"firmware\">\n"
	"  <id>com.acme.example.firmware</id>\n"
	"  <releases>\n"
	"    <release version=\"1.2.3\"/>\n"
	"  </releases>\n"
	"</component>",
			   "lvfs\\firmware.bin", "world",
			   NULL);
	if (blob == NULL) {
		g_test_skip ("libgcab too old");
		return;
	}
	silo = fu_common_cab_build_silo (blob, 10240, &error);
	g_assert_no_error (error);
	g_assert_nonnull (silo);

	/* verify */
	component = xb_silo_query_first (silo, "components/component/id[text()='com.acme.example.firmware']/..", &error);
	g_assert_no_error (error);
	g_assert_nonnull (component);
	rel = xb_node_query_first (component, "releases/release", &error);
	g_assert_no_error (error);
	g_assert_nonnull (rel);
	g_assert_cmpstr (xb_node_get_attr (rel, "version"), ==, "1.2.3");
	blob_tmp = xb_node_get_data (rel, "fwupd::ReleaseBlob(firmware.bin)");
	g_assert_nonnull (blob_tmp);
}

static void
fu_common_store_cab_error_no_metadata_func (void)
{
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	blob = _build_cab (GCAB_COMPRESSION_NONE,
			   "foo.txt", "hello",
			   "bar.txt", "world",
			   NULL);
	if (blob == NULL) {
		g_test_skip ("libgcab too old");
		return;
	}
	silo = fu_common_cab_build_silo (blob, 10240, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null (silo);
}

static void
fu_common_store_cab_error_wrong_size_func (void)
{
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	blob = _build_cab (GCAB_COMPRESSION_NONE,
			   "acme.metainfo.xml",
	"<component type=\"firmware\">\n"
	"  <id>com.acme.example.firmware</id>\n"
	"  <releases>\n"
	"    <release version=\"1.2.3\">\n"
	"      <size type=\"installed\">7004701</size>\n"
	"      <checksum filename=\"firmware.bin\" target=\"content\" type=\"sha1\">deadbeef</checksum>\n"
	"    </release>\n"
	"  </releases>\n"
	"</component>",
			   "firmware.bin", "world",
			   NULL);
	if (blob == NULL) {
		g_test_skip ("libgcab too old");
		return;
	}
	silo = fu_common_cab_build_silo (blob, 10240, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null (silo);
}

static void
fu_common_store_cab_error_missing_file_func (void)
{
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	blob = _build_cab (GCAB_COMPRESSION_NONE,
			   "acme.metainfo.xml",
	"<component type=\"firmware\">\n"
	"  <id>com.acme.example.firmware</id>\n"
	"  <releases>\n"
	"    <release version=\"1.2.3\">\n"
	"      <checksum filename=\"firmware.dfu\" target=\"content\"/>\n"
	"    </release>\n"
	"  </releases>\n"
	"</component>",
			   "firmware.bin", "world",
			   NULL);
	if (blob == NULL) {
		g_test_skip ("libgcab too old");
		return;
	}
	silo = fu_common_cab_build_silo (blob, 10240, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null (silo);
}

static void
fu_common_store_cab_error_size_func (void)
{
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	blob = _build_cab (GCAB_COMPRESSION_NONE,
			   "acme.metainfo.xml",
	"<component type=\"firmware\">\n"
	"  <id>com.acme.example.firmware</id>\n"
	"  <releases>\n"
	"    <release version=\"1.2.3\"/>\n"
	"  </releases>\n"
	"</component>",
			   "firmware.bin", "world",
			   NULL);
	if (blob == NULL) {
		g_test_skip ("libgcab too old");
		return;
	}
	silo = fu_common_cab_build_silo (blob, 123, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null (silo);
}

static void
fu_common_store_cab_error_wrong_checksum_func (void)
{
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	blob = _build_cab (GCAB_COMPRESSION_NONE,
			   "acme.metainfo.xml",
	"<component type=\"firmware\">\n"
	"  <id>com.acme.example.firmware</id>\n"
	"  <releases>\n"
	"    <release version=\"1.2.3\">\n"
	"      <checksum filename=\"firmware.bin\" target=\"content\" type=\"sha1\">deadbeef</checksum>\n"
	"    </release>\n"
	"  </releases>\n"
	"</component>",
			   "firmware.bin", "world",
			   NULL);
	if (blob == NULL) {
		g_test_skip ("libgcab too old");
		return;
	}
	silo = fu_common_cab_build_silo (blob, 10240, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null (silo);
}

static gboolean
fu_device_poll_cb (FuDevice *device, GError **error)
{
	guint64 cnt = fu_device_get_metadata_integer (device, "cnt");
	g_debug ("poll cnt=%" G_GUINT64_FORMAT, cnt);
	fu_device_set_metadata_integer (device, "cnt", cnt + 1);
	return TRUE;
}

static void
fu_device_poll_func (void)
{
	g_autoptr(FuDevice) device = fu_device_new ();
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (device);
	guint cnt;

	/* set up a 10ms poll */
	klass->poll = fu_device_poll_cb;
	fu_device_set_metadata_integer (device, "cnt", 0);
	fu_device_set_poll_interval (device, 10);
	fu_test_loop_run_with_timeout (100);
	fu_test_loop_quit ();
	cnt = fu_device_get_metadata_integer (device, "cnt");
	g_assert_cmpint (cnt, >=, 8);

	/* disable the poll */
	fu_device_set_poll_interval (device, 0);
	fu_test_loop_run_with_timeout (100);
	fu_test_loop_quit ();
	g_assert_cmpint (fu_device_get_metadata_integer (device, "cnt"), ==, cnt);
}

static void
fu_device_incorporate_func (void)
{
	g_autoptr(FuDevice) device = fu_device_new ();
	g_autoptr(FuDevice) donor = fu_device_new ();

	/* set up donor device */
	fu_device_set_alternate_id (donor, "alt-id");
	fu_device_set_equivalent_id (donor, "equiv-id");
	fu_device_set_metadata (donor, "test", "me");
	fu_device_set_metadata (donor, "test2", "me");

	/* base properties */
	fu_device_add_flag (donor, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_set_created (donor, 123);
	fu_device_set_modified (donor, 456);
	fu_device_add_icon (donor, "computer");

	/* existing properties */
	fu_device_set_equivalent_id (device, "DO_NOT_OVERWRITE");
	fu_device_set_metadata (device, "test2", "DO_NOT_OVERWRITE");
	fu_device_set_modified (device, 789);

	/* incorporate properties from donor to device */
	fu_device_incorporate (device, donor);
	g_assert_cmpstr (fu_device_get_alternate_id (device), ==, "alt-id");
	g_assert_cmpstr (fu_device_get_equivalent_id (device), ==, "DO_NOT_OVERWRITE");
	g_assert_cmpstr (fu_device_get_metadata (device, "test"), ==, "me");
	g_assert_cmpstr (fu_device_get_metadata (device, "test2"), ==, "DO_NOT_OVERWRITE");
	g_assert_true (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_REQUIRE_AC));
	g_assert_cmpint (fu_device_get_created (device), ==, 123);
	g_assert_cmpint (fu_device_get_modified (device), ==, 789);
	g_assert_cmpint (fu_device_get_icons(device)->len, ==, 1);
}

static void
fu_chunk_func (void)
{
	g_autofree gchar *chunked1_str = NULL;
	g_autofree gchar *chunked2_str = NULL;
	g_autofree gchar *chunked3_str = NULL;
	g_autofree gchar *chunked4_str = NULL;
	g_autoptr(GPtrArray) chunked1 = NULL;
	g_autoptr(GPtrArray) chunked2 = NULL;
	g_autoptr(GPtrArray) chunked3 = NULL;
	g_autoptr(GPtrArray) chunked4 = NULL;

	chunked3 = fu_chunk_array_new ((const guint8 *) "123456", 6, 0x0, 3, 3);
	chunked3_str = fu_chunk_array_to_string (chunked3);
	g_print ("\n%s", chunked3_str);
	g_assert_cmpstr (chunked3_str, ==, "#00: page:00 addr:0000 len:03 123\n"
					   "#01: page:01 addr:0000 len:03 456\n");

	chunked4 = fu_chunk_array_new ((const guint8 *) "123456", 6, 0x4, 4, 4);
	chunked4_str = fu_chunk_array_to_string (chunked4);
	g_print ("\n%s", chunked4_str);
	g_assert_cmpstr (chunked4_str, ==, "#00: page:01 addr:0000 len:04 1234\n"
					   "#01: page:02 addr:0000 len:02 56\n");

	chunked1 = fu_chunk_array_new ((const guint8 *) "0123456789abcdef", 16, 0x0, 10, 4);
	chunked1_str = fu_chunk_array_to_string (chunked1);
	g_print ("\n%s", chunked1_str);
	g_assert_cmpstr (chunked1_str, ==, "#00: page:00 addr:0000 len:04 0123\n"
					   "#01: page:00 addr:0004 len:04 4567\n"
					   "#02: page:00 addr:0008 len:02 89\n"
					   "#03: page:01 addr:0000 len:04 abcd\n"
					   "#04: page:01 addr:0004 len:02 ef\n");

	chunked2 = fu_chunk_array_new ((const guint8 *) "XXXXXXYYYYYYZZZZZZ", 18, 0x0, 6, 4);
	chunked2_str = fu_chunk_array_to_string (chunked2);
	g_print ("\n%s", chunked2_str);
	g_assert_cmpstr (chunked2_str, ==, "#00: page:00 addr:0000 len:04 XXXX\n"
					   "#01: page:00 addr:0004 len:02 XX\n"
					   "#02: page:01 addr:0000 len:04 YYYY\n"
					   "#03: page:01 addr:0004 len:02 YY\n"
					   "#04: page:02 addr:0000 len:04 ZZZZ\n"
					   "#05: page:02 addr:0004 len:02 ZZ\n");
}

static void
fu_common_strstrip_func (void)
{

	struct {
		const gchar *old;
		const gchar *new;
	} map[] = {
		{ "same", "same" },
		{ " leading", "leading" },
		{ "tailing ", "tailing" },
		{ "  b  ", "b" },
		{ "  ", "" },
		{ NULL, NULL }
	};
	for (guint i = 0; map[i].old != NULL; i++) {
		g_autofree gchar *tmp = fu_common_strstrip (map[i].old);
		g_assert_cmpstr (tmp, ==, map[i].new);
	}
}

static void
fu_common_version_func (void)
{
	guint i;
	struct {
		guint32 val;
		const gchar *ver;
		FuVersionFormat flags;
	} version_from_uint32[] = {
		{ 0x0,		"0.0.0.0",		FU_VERSION_FORMAT_QUAD },
		{ 0xff,		"0.0.0.255",		FU_VERSION_FORMAT_QUAD },
		{ 0xff01,	"0.0.255.1",		FU_VERSION_FORMAT_QUAD },
		{ 0xff0001,	"0.255.0.1",		FU_VERSION_FORMAT_QUAD },
		{ 0xff000100,	"255.0.1.0",		FU_VERSION_FORMAT_QUAD },
		{ 0x0,		"0.0.0",		FU_VERSION_FORMAT_TRIPLET },
		{ 0xff,		"0.0.255",		FU_VERSION_FORMAT_TRIPLET },
		{ 0xff01,	"0.0.65281",		FU_VERSION_FORMAT_TRIPLET },
		{ 0xff0001,	"0.255.1",		FU_VERSION_FORMAT_TRIPLET },
		{ 0xff000100,	"255.0.256",		FU_VERSION_FORMAT_TRIPLET },
		{ 0x0,		"0",			FU_VERSION_FORMAT_PLAIN },
		{ 0xff000100,	"4278190336",		FU_VERSION_FORMAT_PLAIN },
		{ 0x0,		"11.0.0.0",		FU_VERSION_FORMAT_INTEL_ME },
		{ 0xffffffff,	"18.31.255.65535",	FU_VERSION_FORMAT_INTEL_ME },
		{ 0x0b32057a,	"11.11.50.1402",	FU_VERSION_FORMAT_INTEL_ME },
		{ 0xb8320d84,	"11.8.50.3460",		FU_VERSION_FORMAT_INTEL_ME2 },
		{ 0,		NULL }
	};
	struct {
		guint16 val;
		const gchar *ver;
		FuVersionFormat flags;
	} version_from_uint16[] = {
		{ 0x0,		"0.0",			FU_VERSION_FORMAT_PAIR },
		{ 0xff,		"0.255",		FU_VERSION_FORMAT_PAIR },
		{ 0xff01,	"255.1",		FU_VERSION_FORMAT_PAIR },
		{ 0x0,		"0.0",			FU_VERSION_FORMAT_BCD },
		{ 0x0110,	"1.10",			FU_VERSION_FORMAT_BCD },
		{ 0x9999,	"99.99",		FU_VERSION_FORMAT_BCD },
		{ 0x0,		"0",			FU_VERSION_FORMAT_PLAIN },
		{ 0x1234,	"4660",			FU_VERSION_FORMAT_PLAIN },
		{ 0,		NULL }
	};
	struct {
		const gchar *old;
		const gchar *new;
	} version_parse[] = {
		{ "0",		"0" },
		{ "0x1a",	"0.0.26" },
		{ "257",	"0.0.257" },
		{ "1.2.3",	"1.2.3" },
		{ "0xff0001",	"0.255.1" },
		{ "16711681",	"0.255.1" },
		{ "20150915",	"20150915" },
		{ "dave",	"dave" },
		{ "0x1x",	"0x1x" },
		{ NULL,		NULL }
	};

	/* check version conversion */
	for (i = 0; version_from_uint32[i].ver != NULL; i++) {
		g_autofree gchar *ver = NULL;
		ver = fu_common_version_from_uint32 (version_from_uint32[i].val,
						    version_from_uint32[i].flags);
		g_assert_cmpstr (ver, ==, version_from_uint32[i].ver);
	}
	for (i = 0; version_from_uint16[i].ver != NULL; i++) {
		g_autofree gchar *ver = NULL;
		ver = fu_common_version_from_uint16 (version_from_uint16[i].val,
						    version_from_uint16[i].flags);
		g_assert_cmpstr (ver, ==, version_from_uint16[i].ver);
	}

	/* check version parsing */
	for (i = 0; version_parse[i].old != NULL; i++) {
		g_autofree gchar *ver = NULL;
		ver = fu_common_version_parse (version_parse[i].old);
		g_assert_cmpstr (ver, ==, version_parse[i].new);
	}
}

static void
fu_common_vercmp_func (void)
{
	/* same */
	g_assert_cmpint (fu_common_vercmp ("1.2.3", "1.2.3"), ==, 0);
	g_assert_cmpint (fu_common_vercmp ("001.002.003", "001.002.003"), ==, 0);

	/* same, not dotted decimal */
	g_assert_cmpint (fu_common_vercmp ("1.2.3", "0x1020003"), ==, 0);
	g_assert_cmpint (fu_common_vercmp ("0x10203", "0x10203"), ==, 0);

	/* upgrade and downgrade */
	g_assert_cmpint (fu_common_vercmp ("1.2.3", "1.2.4"), <, 0);
	g_assert_cmpint (fu_common_vercmp ("001.002.000", "001.002.009"), <, 0);
	g_assert_cmpint (fu_common_vercmp ("1.2.3", "1.2.2"), >, 0);
	g_assert_cmpint (fu_common_vercmp ("001.002.009", "001.002.000"), >, 0);

	/* unequal depth */
	g_assert_cmpint (fu_common_vercmp ("1.2.3", "1.2.3.1"), <, 0);
	g_assert_cmpint (fu_common_vercmp ("1.2.3.1", "1.2.4"), <, 0);

	/* mixed-alpha-numeric */
	g_assert_cmpint (fu_common_vercmp ("1.2.3a", "1.2.3a"), ==, 0);
	g_assert_cmpint (fu_common_vercmp ("1.2.3a", "1.2.3b"), <, 0);
	g_assert_cmpint (fu_common_vercmp ("1.2.3b", "1.2.3a"), >, 0);

	/* alpha version append */
	g_assert_cmpint (fu_common_vercmp ("1.2.3", "1.2.3a"), <, 0);
	g_assert_cmpint (fu_common_vercmp ("1.2.3a", "1.2.3"), >, 0);

	/* alpha only */
	g_assert_cmpint (fu_common_vercmp ("alpha", "alpha"), ==, 0);
	g_assert_cmpint (fu_common_vercmp ("alpha", "beta"), <, 0);
	g_assert_cmpint (fu_common_vercmp ("beta", "alpha"), >, 0);

	/* alpha-compare */
	g_assert_cmpint (fu_common_vercmp ("1.2a.3", "1.2a.3"), ==, 0);
	g_assert_cmpint (fu_common_vercmp ("1.2a.3", "1.2b.3"), <, 0);
	g_assert_cmpint (fu_common_vercmp ("1.2b.3", "1.2a.3"), >, 0);

	/* tilde is all-powerful */
	g_assert_cmpint (fu_common_vercmp ("1.2.3~rc1", "1.2.3~rc1"), ==, 0);
	g_assert_cmpint (fu_common_vercmp ("1.2.3~rc1", "1.2.3"), <, 0);
	g_assert_cmpint (fu_common_vercmp ("1.2.3", "1.2.3~rc1"), >, 0);
	g_assert_cmpint (fu_common_vercmp ("1.2.3~rc2", "1.2.3~rc1"), >, 0);

	/* invalid */
	g_assert_cmpint (fu_common_vercmp ("1", NULL), ==, G_MAXINT);
	g_assert_cmpint (fu_common_vercmp (NULL, "1"), ==, G_MAXINT);
	g_assert_cmpint (fu_common_vercmp (NULL, NULL), ==, G_MAXINT);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);
	g_setenv ("FWUPD_DATADIR", TESTDATADIR_SRC, TRUE);
	g_setenv ("FWUPD_PLUGINDIR", TESTDATADIR_SRC, TRUE);
	g_setenv ("FWUPD_SYSCONFDIR", TESTDATADIR_SRC, TRUE);
	g_setenv ("FWUPD_SYSFSFWDIR", TESTDATADIR_SRC, TRUE);
	g_setenv ("FWUPD_LOCALSTATEDIR", "/tmp/fwupd-self-test/var", TRUE);

	/* ensure empty tree */
	fu_self_test_mkroot ();

	/* tests go here */
	if (g_test_slow ())
		g_test_add_func ("/fwupd/progressbar", fu_progressbar_func);
	g_test_add_func ("/fwupd/archive{invalid}", fu_archive_invalid_func);
	g_test_add_func ("/fwupd/archive{cab}", fu_archive_cab_func);
	g_test_add_func ("/fwupd/engine{requirements-other-device}", fu_engine_requirements_other_device_func);
	g_test_add_func ("/fwupd/device{incorporate}", fu_device_incorporate_func);
	g_test_add_func ("/fwupd/device{poll}", fu_device_poll_func);
	g_test_add_func ("/fwupd/device-locker{success}", fu_device_locker_func);
	g_test_add_func ("/fwupd/device-locker{fail}", fu_device_locker_fail_func);
	g_test_add_func ("/fwupd/device{metadata}", fu_device_metadata_func);
	g_test_add_func ("/fwupd/device{open-refcount}", fu_device_open_refcount_func);
	g_test_add_func ("/fwupd/device{version-format}", fu_device_version_format_func);
	g_test_add_func ("/fwupd/device-list", fu_device_list_func);
	g_test_add_func ("/fwupd/device-list{delay}", fu_device_list_delay_func);
	g_test_add_func ("/fwupd/device-list{compatible}", fu_device_list_compatible_func);
	g_test_add_func ("/fwupd/device-list{remove-chain}", fu_device_list_remove_chain_func);
	g_test_add_func ("/fwupd/engine{device-unlock}", fu_engine_device_unlock_func);
	g_test_add_func ("/fwupd/engine{history-success}", fu_engine_history_func);
	g_test_add_func ("/fwupd/engine{history-error}", fu_engine_history_error_func);
	if (g_test_slow ())
		g_test_add_func ("/fwupd/device-list{replug-auto}", fu_device_list_replug_auto_func);
	g_test_add_func ("/fwupd/device-list{replug-user}", fu_device_list_replug_user_func);
	g_test_add_func ("/fwupd/engine{require-hwid}", fu_engine_require_hwid_func);
	g_test_add_func ("/fwupd/engine{history-inherit}", fu_engine_history_inherit);
	g_test_add_func ("/fwupd/engine{partial-hash}", fu_engine_partial_hash_func);
	g_test_add_func ("/fwupd/engine{downgrade}", fu_engine_downgrade_func);
	g_test_add_func ("/fwupd/engine{requirements-success}", fu_engine_requirements_func);
	g_test_add_func ("/fwupd/engine{requirements-missing}", fu_engine_requirements_missing_func);
	g_test_add_func ("/fwupd/engine{requirements-unsupported}", fu_engine_requirements_unsupported_func);
	g_test_add_func ("/fwupd/engine{requirements-device}", fu_engine_requirements_device_func);
	g_test_add_func ("/fwupd/engine{device-auto-parent}", fu_engine_device_parent_func);
	g_test_add_func ("/fwupd/engine{device-priority}", fu_engine_device_priority_func);
	g_test_add_func ("/fwupd/engine{install-duration}", fu_engine_install_duration_func);
	g_test_add_func ("/fwupd/engine{generate-md}", fu_engine_generate_md_func);
	g_test_add_func ("/fwupd/hwids", fu_hwids_func);
	g_test_add_func ("/fwupd/smbios", fu_smbios_func);
	g_test_add_func ("/fwupd/smbios3", fu_smbios3_func);
	g_test_add_func ("/fwupd/history", fu_history_func);
	g_test_add_func ("/fwupd/history{migrate}", fu_history_migrate_func);
	g_test_add_func ("/fwupd/plugin-list", fu_plugin_list_func);
	g_test_add_func ("/fwupd/plugin-list{depsolve}", fu_plugin_list_depsolve_func);
	g_test_add_func ("/fwupd/plugin{delay}", fu_plugin_delay_func);
	g_test_add_func ("/fwupd/plugin{module}", fu_plugin_module_func);
	g_test_add_func ("/fwupd/plugin{quirks}", fu_plugin_quirks_func);
	g_test_add_func ("/fwupd/plugin{quirks-performance}", fu_plugin_quirks_performance_func);
	g_test_add_func ("/fwupd/plugin{quirks-device}", fu_plugin_quirks_device_func);
	g_test_add_func ("/fwupd/plugin{composite}", fu_plugin_composite_func);
	g_test_add_func ("/fwupd/keyring{gpg}", fu_keyring_gpg_func);
	g_test_add_func ("/fwupd/keyring{pkcs7}", fu_keyring_pkcs7_func);
	g_test_add_func ("/fwupd/keyring{pkcs7-self-signed}", fu_keyring_pkcs7_self_signed_func);
	g_test_add_func ("/fwupd/plugin{build-hash}", fu_plugin_hash_func);
	g_test_add_func ("/fwupd/chunk", fu_chunk_func);
	g_test_add_func ("/fwupd/common{version-guess-format}", fu_common_version_guess_format_func);
	g_test_add_func ("/fwupd/common{version}", fu_common_version_func);
	g_test_add_func ("/fwupd/common{vercmp}", fu_common_vercmp_func);
	g_test_add_func ("/fwupd/common{strstrip}", fu_common_strstrip_func);
	g_test_add_func ("/fwupd/common{endian}", fu_common_endian_func);
	g_test_add_func ("/fwupd/common{cab-success}", fu_common_store_cab_func);
	g_test_add_func ("/fwupd/common{cab-success-unsigned}", fu_common_store_cab_unsigned_func);
	g_test_add_func ("/fwupd/common{cab-success-folder}", fu_common_store_cab_folder_func);
	g_test_add_func ("/fwupd/common{cab-error-no-metadata}", fu_common_store_cab_error_no_metadata_func);
	g_test_add_func ("/fwupd/common{cab-error-wrong-size}", fu_common_store_cab_error_wrong_size_func);
	g_test_add_func ("/fwupd/common{cab-error-wrong-checksum}", fu_common_store_cab_error_wrong_checksum_func);
	g_test_add_func ("/fwupd/common{cab-error-missing-file}", fu_common_store_cab_error_missing_file_func);
	g_test_add_func ("/fwupd/common{cab-error-size}", fu_common_store_cab_error_size_func);
	g_test_add_func ("/fwupd/common{spawn)", fu_common_spawn_func);
	g_test_add_func ("/fwupd/common{spawn-timeout)", fu_common_spawn_timeout_func);
	g_test_add_func ("/fwupd/common{firmware-builder}", fu_common_firmware_builder_func);
	return g_test_run ();
}
