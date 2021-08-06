/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

struct FuPluginData {
	guint delay_decompress_ms;
	guint delay_write_ms;
	guint delay_verify_ms;
};

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	g_debug ("init");
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	//FuPluginData *data = fu_plugin_get_data (plugin);
	g_debug ("destroy");
}

static gboolean
fu_plugin_test_load_xml(FuPlugin *plugin, const gchar *xml, GError **error)
{
	FuPluginData *data = fu_plugin_get_data(plugin);
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbNode) delay_decompress_ms = NULL;
	g_autoptr(XbNode) delay_verify_ms = NULL;
	g_autoptr(XbNode) delay_write_ms = NULL;
	g_autoptr(XbSilo) silo = NULL;

	/* build silo */
	if (!xb_builder_source_load_xml(source, xml, XB_BUILDER_SOURCE_FLAG_NONE, error))
		return FALSE;
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, error);
	if (silo == NULL)
		return FALSE;

	/* parse markup */
	delay_decompress_ms = xb_silo_query_first(silo, "config/delay_decompress_ms", NULL);
	if (delay_decompress_ms != NULL)
		data->delay_decompress_ms = xb_node_get_text_as_uint(delay_decompress_ms);
	delay_write_ms = xb_silo_query_first(silo, "config/delay_write_ms", NULL);
	if (delay_write_ms != NULL)
		data->delay_write_ms = xb_node_get_text_as_uint(delay_write_ms);
	delay_verify_ms = xb_silo_query_first(silo, "config/delay_verify_ms", NULL);
	if (delay_verify_ms != NULL)
		data->delay_verify_ms = xb_node_get_text_as_uint(delay_verify_ms);

	/* success */
	return TRUE;
}

gboolean
fu_plugin_startup(FuPlugin *plugin, GError **error)
{
	const gchar *xml = g_getenv("FWUPD_TEST_PLUGIN_XML");
	if (xml != NULL) {
		if (!fu_plugin_test_load_xml(plugin, xml, error))
			return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	FuContext *ctx = fu_plugin_get_context (plugin);
	g_autoptr(FuDevice) device = NULL;
	device = fu_device_new_with_context (ctx);
	fu_device_set_id (device, "FakeDevice");
	fu_device_add_guid (device, "b585990a-003e-5270-89d5-3705a17f9a43");
	fu_device_set_name (device, "Integrated_Webcam(TM)");
	fu_device_add_icon (device, "preferences-desktop-keyboard");
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_protocol (device, "com.acme.test");
	fu_device_set_summary (device, "Fake webcam");
	fu_device_set_vendor (device, "ACME Corp.");
	fu_device_add_vendor_id (device, "USB:0x046D");
	fu_device_set_version_format (device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version_bootloader (device, "0.1.2");
	fu_device_set_version (device, "1.2.2");
	fu_device_set_version_lowest (device, "1.2.0");
	if (g_strcmp0 (g_getenv ("FWUPD_PLUGIN_TEST"), "registration") == 0) {
		fu_plugin_device_register (plugin, device);
		if (fu_device_get_metadata (device, "BestDevice") == NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "Device not set by another plugin");
			return FALSE;
		}
	}
	fu_plugin_device_add (plugin, device);

	if (g_strcmp0 (g_getenv ("FWUPD_PLUGIN_TEST"), "composite") == 0) {
		g_autoptr(FuDevice) child1 = NULL;
		g_autoptr(FuDevice) child2 = NULL;

		child1 = fu_device_new_with_context (ctx);
		fu_device_add_vendor_id (child1, "USB:FFFF");
		fu_device_add_protocol (child1, "com.acme");
		fu_device_set_physical_id (child1, "fake");
		fu_device_set_logical_id (child1, "child1");
		fu_device_add_guid (child1, "7fddead7-12b5-4fb9-9fa0-6d30305df755");
		fu_device_set_name (child1, "Module1");
		fu_device_set_version_format (child1, FWUPD_VERSION_FORMAT_PLAIN);
		fu_device_set_version (child1, "1");
		fu_device_add_parent_guid (child1, "b585990a-003e-5270-89d5-3705a17f9a43");
		fu_device_add_flag (child1, FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_plugin_device_add (plugin, child1);

		child2 = fu_device_new_with_context (ctx);
		fu_device_add_vendor_id (child2, "USB:FFFF");
		fu_device_add_protocol (child2, "com.acme");
		fu_device_set_physical_id (child2, "fake");
		fu_device_set_logical_id (child2, "child2");
		fu_device_add_guid (child2, "b8fe6b45-8702-4bcd-8120-ef236caac76f");
		fu_device_set_name (child2, "Module2");
		fu_device_set_version_format (child2, FWUPD_VERSION_FORMAT_PLAIN);
		fu_device_set_version (child2, "10");
		fu_device_add_parent_guid (child2, "b585990a-003e-5270-89d5-3705a17f9a43");
		fu_device_add_flag (child2, FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_plugin_device_add (plugin, child2);

	}

	return TRUE;
}

void
fu_plugin_device_registered (FuPlugin *plugin, FuDevice *device)
{
	fu_device_set_metadata (device, "BestDevice", "/dev/urandom");
}

gboolean
fu_plugin_verify(FuPlugin *plugin,
		 FuDevice *device,
		 FuProgress *progress,
		 FuPluginVerifyFlags flags,
		 GError **error)
{
	if (g_strcmp0 (fu_device_get_version (device), "1.2.2") == 0) {
		fu_device_add_checksum (device, "90d0ad436d21e0687998cd2127b2411135e1f730");
		fu_device_add_checksum (device, "921631916a60b295605dbae6a0309f9b64e2401b3de8e8506e109fc82c586e3a");
		return TRUE;
	}
	if (g_strcmp0 (fu_device_get_version (device), "1.2.3") == 0) {
		fu_device_add_checksum (device, "7998cd212721e068b2411135e1f90d0ad436d730");
		fu_device_add_checksum (device, "dbae6a0309b3de8e850921631916a60b2956056e109fc82c586e3f9b64e2401a");
		return TRUE;
	}
	if (g_strcmp0 (fu_device_get_version (device), "1.2.4") == 0) {
		fu_device_add_checksum (device, "2b8546ba805ad10bf8a2e5ad539d53f303812ba5");
		fu_device_add_checksum (device, "b546c241029ce4e16c99eb6bfd77b86e4490aa3826ba71b8a4114e96a2d69bcd");
		return TRUE;
	}
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_SUPPORTED,
		     "no checksum for %s", fu_device_get_version (device));
	return FALSE;
}

static gchar *
fu_plugin_test_get_version (GBytes *blob_fw)
{
	const gchar *str = g_bytes_get_data (blob_fw, NULL);
	guint64 val = 0;
	if (str == NULL)
		return NULL;
	val = fu_common_strtoull (str);
	if (val == 0x0)
		return NULL;
	return fu_common_version_from_uint32 (val, FWUPD_VERSION_FORMAT_TRIPLET);
}

gboolean
fu_plugin_write_firmware(FuPlugin *plugin,
			 FuDevice *device,
			 GBytes *blob_fw,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuPluginData *data = fu_plugin_get_data(plugin);
	const gchar *test = g_getenv ("FWUPD_PLUGIN_TEST");
	gboolean requires_activation = g_strcmp0 (test, "requires-activation") == 0;
	gboolean requires_reboot = g_strcmp0 (test, "requires-reboot") == 0;
	if (g_strcmp0 (test, "fail") == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "device was not in supported mode");
		return FALSE;
	}
	fu_device_set_status (device, FWUPD_STATUS_DECOMPRESSING);
	for (guint i = 0; i <= data->delay_decompress_ms; i++) {
		g_usleep (1000);
		fu_progress_set_percentage_full(progress, i, data->delay_decompress_ms);
	}
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 0; i <= data->delay_write_ms; i++) {
		g_usleep (1000);
		fu_progress_set_percentage_full(progress, i, data->delay_write_ms);
	}
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_VERIFY);
	for (guint i = 0; i <= data->delay_verify_ms; i++) {
		g_usleep (1000);
		fu_progress_set_percentage_full(progress, i, data->delay_verify_ms);
	}

	/* composite test, upgrade composite devices */
	if (g_strcmp0 (test, "composite") == 0) {
		fu_device_set_version_format (device, FWUPD_VERSION_FORMAT_PLAIN);
		if (g_strcmp0 (fu_device_get_logical_id (device), "child1") == 0) {
			fu_device_set_version (device, "2");
			return TRUE;
		} else if (g_strcmp0 (fu_device_get_logical_id (device), "child2") == 0) {
			fu_device_set_version (device, "11");
			return TRUE;
		}
	}

	/* upgrade, or downgrade */
	if (requires_activation) {
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
	} else if (requires_reboot) {
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	} else {
		g_autofree gchar *ver = fu_plugin_test_get_version (blob_fw);
		fu_device_set_version_format (device, FWUPD_VERSION_FORMAT_TRIPLET);
		if (ver != NULL) {
			fu_device_set_version (device, ver);
		} else {
			if (flags & FWUPD_INSTALL_FLAG_ALLOW_OLDER) {
				fu_device_set_version (device, "1.2.2");
			} else {
				fu_device_set_version (device, "1.2.3");
			}
		}
	}

	/* do this all over again */
	if (g_strcmp0 (test, "another-write-required") == 0) {
		g_unsetenv ("FWUPD_PLUGIN_TEST");
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED);
	}

	/* for the self tests only */
	fu_device_set_metadata_integer (device, "nr-update",
					fu_device_get_metadata_integer (device, "nr-update") + 1);

	return TRUE;
}

gboolean
fu_plugin_activate (FuPlugin *plugin, FuDevice *device, GError **error)
{
	fu_device_set_version_format (device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version (device, "1.2.3");
	return TRUE;
}

gboolean
fu_plugin_get_results (FuPlugin *plugin, FuDevice *device, GError **error)
{
	fu_device_set_update_state (device, FWUPD_UPDATE_STATE_SUCCESS);
	fu_device_set_update_error (device, NULL);
	return TRUE;
}

gboolean
fu_plugin_composite_prepare (FuPlugin *plugin,
			     GPtrArray *devices,
			     GError **error)
{
	if (g_strcmp0 (g_getenv ("FWUPD_PLUGIN_TEST"), "composite") == 0) {
		for (guint i = 0; i < devices->len; i++) {
			FuDevice *device = g_ptr_array_index (devices, i);
			fu_device_set_metadata (device, "frimbulator", "1");
		}
	}
	return TRUE;
}

gboolean
fu_plugin_composite_cleanup (FuPlugin *plugin,
			     GPtrArray *devices,
			     GError **error)
{
	if (g_strcmp0 (g_getenv ("FWUPD_PLUGIN_TEST"), "composite") == 0) {
		for (guint i = 0; i < devices->len; i++) {
			FuDevice *device = g_ptr_array_index (devices, i);
			fu_device_set_metadata (device, "frombulator", "1");
		}
	}
	return TRUE;
}
