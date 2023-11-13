/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-test-plugin.h"

struct _FuTestPlugin {
	FuPlugin parent_instance;
	guint delay_decompress_ms;
	guint delay_write_ms;
	guint delay_verify_ms;
	guint delay_request_ms;
};

G_DEFINE_TYPE(FuTestPlugin, fu_test_plugin, FU_TYPE_PLUGIN)

static void
fu_test_plugin_to_string(FuPlugin *plugin, guint idt, GString *str)
{
	FuTestPlugin *self = FU_TEST_PLUGIN(plugin);
	fu_string_append_ku(str, idt, "DelayDecompressMs", self->delay_decompress_ms);
	fu_string_append_ku(str, idt, "DelayWriteMs", self->delay_write_ms);
	fu_string_append_ku(str, idt, "DelayVerifyMs", self->delay_verify_ms);
	fu_string_append_ku(str, idt, "DelayRequestMs", self->delay_request_ms);
}

static gboolean
fu_test_plugin_load_xml(FuPlugin *plugin, const gchar *xml, GError **error)
{
	FuTestPlugin *self = FU_TEST_PLUGIN(plugin);
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbNode) delay_decompress_ms = NULL;
	g_autoptr(XbNode) delay_request_ms = NULL;
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
		self->delay_decompress_ms = xb_node_get_text_as_uint(delay_decompress_ms);
	delay_write_ms = xb_silo_query_first(silo, "config/delay_write_ms", NULL);
	if (delay_write_ms != NULL)
		self->delay_write_ms = xb_node_get_text_as_uint(delay_write_ms);
	delay_verify_ms = xb_silo_query_first(silo, "config/delay_verify_ms", NULL);
	if (delay_verify_ms != NULL)
		self->delay_verify_ms = xb_node_get_text_as_uint(delay_verify_ms);
	delay_request_ms = xb_silo_query_first(silo, "config/delay_request_ms", NULL);
	if (delay_request_ms != NULL)
		self->delay_request_ms = xb_node_get_text_as_uint(delay_request_ms);

	/* success */
	return TRUE;
}

static gboolean
fu_test_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	const gchar *xml = g_getenv("FWUPD_TEST_PLUGIN_XML");
	if (xml != NULL) {
		if (!fu_test_plugin_load_xml(plugin, xml, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_test_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(FuDevice) device = NULL;
	device = fu_device_new(ctx);
	fu_device_set_id(device, "FakeDevice");
	fu_device_add_guid(device, "b585990a-003e-5270-89d5-3705a17f9a43");
	fu_device_set_name(device, "Integrated_Webcam(TM)");
	fu_device_add_icon(device, "preferences-desktop-keyboard");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_protocol(device, "com.acme.test");
	fu_device_set_summary(device, "Fake webcam");
	fu_device_set_vendor(device, "ACME Corp.");
	fu_device_add_vendor_id(device, "USB:0x046D");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version_bootloader(device, "0.1.2");
	fu_device_set_version(device, "1.2.2");
	fu_device_set_version_lowest(device, "1.2.0");
	if (g_strcmp0(g_getenv("FWUPD_PLUGIN_TEST"), "registration") == 0) {
		fu_plugin_device_register(plugin, device);
		if (fu_device_get_metadata(device, "BestDevice") == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "Device not set by another plugin");
			return FALSE;
		}
	}
	fu_plugin_device_add(plugin, device);

	if (g_strcmp0(g_getenv("FWUPD_PLUGIN_TEST"), "composite") == 0) {
		g_autoptr(FuDevice) child1 = NULL;
		g_autoptr(FuDevice) child2 = NULL;

		child1 = fu_device_new(ctx);
		fu_device_add_vendor_id(child1, "USB:FFFF");
		fu_device_add_protocol(child1, "com.acme");
		fu_device_set_physical_id(child1, "fake");
		fu_device_set_logical_id(child1, "child1");
		fu_device_add_guid(child1, "7fddead7-12b5-4fb9-9fa0-6d30305df755");
		fu_device_set_name(child1, "Module1");
		fu_device_set_version_format(child1, FWUPD_VERSION_FORMAT_PLAIN);
		fu_device_set_version(child1, "1");
		fu_device_add_parent_guid(child1, "b585990a-003e-5270-89d5-3705a17f9a43");
		fu_device_add_flag(child1, FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_device_add_flag(child1, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
		fu_device_add_flag(child1, FWUPD_DEVICE_FLAG_INSTALL_PARENT_FIRST);
		fu_plugin_device_add(plugin, child1);

		child2 = fu_device_new(ctx);
		fu_device_add_vendor_id(child2, "USB:FFFF");
		fu_device_add_protocol(child2, "com.acme");
		fu_device_set_physical_id(child2, "fake");
		fu_device_set_logical_id(child2, "child2");
		fu_device_add_guid(child2, "b8fe6b45-8702-4bcd-8120-ef236caac76f");
		fu_device_set_name(child2, "Module2");
		fu_device_set_version_format(child2, FWUPD_VERSION_FORMAT_PLAIN);
		fu_device_set_version(child2, "10");
		fu_device_add_parent_guid(child2, "b585990a-003e-5270-89d5-3705a17f9a43");
		fu_device_add_flag(child2, FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_device_add_flag(child2, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
		fu_device_add_flag(child2, FWUPD_DEVICE_FLAG_INSTALL_PARENT_FIRST);
		fu_plugin_device_add(plugin, child2);
	}

	return TRUE;
}

static void
fu_test_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	fu_device_set_metadata(device, "BestDevice", "/dev/urandom");
}

static gboolean
fu_test_plugin_verify(FuPlugin *plugin,
		      FuDevice *device,
		      FuProgress *progress,
		      FuPluginVerifyFlags flags,
		      GError **error)
{
	if (g_strcmp0(fu_device_get_version(device), "1.2.2") == 0) {
		fu_device_add_checksum(device, "90d0ad436d21e0687998cd2127b2411135e1f730");
		fu_device_add_checksum(
		    device,
		    "921631916a60b295605dbae6a0309f9b64e2401b3de8e8506e109fc82c586e3a");
		return TRUE;
	}
	if (g_strcmp0(fu_device_get_version(device), "1.2.3") == 0) {
		fu_device_add_checksum(device, "7998cd212721e068b2411135e1f90d0ad436d730");
		fu_device_add_checksum(
		    device,
		    "dbae6a0309b3de8e850921631916a60b2956056e109fc82c586e3f9b64e2401a");
		return TRUE;
	}
	if (g_strcmp0(fu_device_get_version(device), "1.2.4") == 0) {
		fu_device_add_checksum(device, "2b8546ba805ad10bf8a2e5ad539d53f303812ba5");
		fu_device_add_checksum(
		    device,
		    "b546c241029ce4e16c99eb6bfd77b86e4490aa3826ba71b8a4114e96a2d69bcd");
		return TRUE;
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "no checksum for %s",
		    fu_device_get_version(device));
	return FALSE;
}

static gchar *
fu_test_plugin_get_version(GBytes *blob_fw)
{
	gsize bufsz = 0;
	const gchar *buf = g_bytes_get_data(blob_fw, &bufsz);
	guint64 val = 0;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *str_safe = fu_strsafe(buf, bufsz);

	if (str_safe == NULL)
		return NULL;
	if (!fu_strtoull(str_safe, &val, 0, G_MAXUINT32, &error_local)) {
		g_debug("invalid version specified: %s", error_local->message);
		return NULL;
	}
	if (val == 0x0)
		return NULL;
	return fu_version_from_uint32(val, FWUPD_VERSION_FORMAT_TRIPLET);
}

static gboolean
fu_test_plugin_write_firmware(FuPlugin *plugin,
			      FuDevice *device,
			      GBytes *blob_fw,
			      FuProgress *progress,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuTestPlugin *self = FU_TEST_PLUGIN(plugin);
	const gchar *test = g_getenv("FWUPD_PLUGIN_TEST");
	gboolean requires_activation = g_strcmp0(test, "requires-activation") == 0;
	gboolean requires_reboot = g_strcmp0(test, "requires-reboot") == 0;
	if (g_strcmp0(test, "fail") == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device was not in supported mode");
		return FALSE;
	}
	fu_progress_set_status(progress, FWUPD_STATUS_DECOMPRESSING);
	for (guint i = 0; i <= self->delay_decompress_ms; i++) {
		fu_device_sleep(device, 1);
		fu_progress_set_percentage_full(progress, i, self->delay_decompress_ms);
	}

	/* send an interactive request, and wait some time */
	if (g_strcmp0(test, "request") == 0 && self->delay_request_ms > 0) {
		g_autoptr(FwupdRequest) request = fwupd_request_new();
		fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
		fwupd_request_set_id(request, FWUPD_REQUEST_ID_REMOVE_REPLUG);
		fwupd_request_add_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
		fwupd_request_set_message(request,
					  "Please pretend to remove the device you cannot see or "
					  "touch and please re-insert it.");
		if (!fu_device_emit_request(device, request, progress, error))
			return FALSE;
		g_usleep(self->delay_request_ms * 1000);
	}

	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 0; i <= self->delay_write_ms; i++) {
		fu_device_sleep(device, 1);
		fu_progress_set_percentage_full(progress, i, self->delay_write_ms);
	}
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_VERIFY);
	for (guint i = 0; i <= self->delay_verify_ms; i++) {
		fu_device_sleep(device, 1);
		fu_progress_set_percentage_full(progress, i, self->delay_verify_ms);
	}

	/* composite test, upgrade composite devices */
	if (g_strcmp0(test, "composite") == 0) {
		fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PLAIN);
		if (g_strcmp0(fu_device_get_logical_id(device), "child1") == 0) {
			fu_device_set_version(device, "2");
			return TRUE;
		}
		if (g_strcmp0(fu_device_get_logical_id(device), "child2") == 0) {
			fu_device_set_version(device, "11");
			return TRUE;
		}
	}

	/* upgrade, or downgrade */
	if (requires_activation) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
	} else if (requires_reboot) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	} else {
		g_autofree gchar *ver = fu_test_plugin_get_version(blob_fw);
		fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
		if (ver != NULL) {
			fu_device_set_version(device, ver);
		} else {
			if (flags & FWUPD_INSTALL_FLAG_ALLOW_OLDER) {
				fu_device_set_version(device, "1.2.2");
			} else {
				fu_device_set_version(device, "1.2.3");
			}
		}
	}

	/* do this all over again */
	if (g_strcmp0(test, "another-write-required") == 0) {
		g_unsetenv("FWUPD_PLUGIN_TEST");
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED);
	}

	/* for the self tests only */
	fu_device_set_metadata_integer(device,
				       "nr-update",
				       fu_device_get_metadata_integer(device, "nr-update") + 1);

	return TRUE;
}

static gboolean
fu_test_plugin_activate(FuPlugin *plugin, FuDevice *device, FuProgress *process, GError **error)
{
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.3");
	return TRUE;
}

static gboolean
fu_test_plugin_get_results(FuPlugin *plugin, FuDevice *device, GError **error)
{
	fu_device_set_update_state(device, FWUPD_UPDATE_STATE_SUCCESS);
	fu_device_set_update_error(device, NULL);
	return TRUE;
}

static gboolean
fu_test_plugin_composite_prepare(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	if (g_strcmp0(g_getenv("FWUPD_PLUGIN_TEST"), "composite") == 0) {
		for (guint i = 0; i < devices->len; i++) {
			FuDevice *device = g_ptr_array_index(devices, i);
			fu_device_set_metadata(device, "frimbulator", "1");
		}
	}
	return TRUE;
}

static gboolean
fu_test_plugin_composite_cleanup(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	if (g_strcmp0(g_getenv("FWUPD_PLUGIN_TEST"), "composite") == 0) {
		for (guint i = 0; i < devices->len; i++) {
			FuDevice *device = g_ptr_array_index(devices, i);
			fu_device_set_metadata(device, "frombulator", "1");
		}
	}
	return TRUE;
}

static void
fu_test_plugin_init(FuTestPlugin *self)
{
	g_debug("init");
	self->delay_request_ms = 10;
}

static void
fu_test_finalize(GObject *obj)
{
	g_debug("destroy");
	G_OBJECT_CLASS(fu_test_plugin_parent_class)->finalize(obj);
}

static void
fu_test_plugin_class_init(FuTestPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_test_finalize;
	plugin_class->to_string = fu_test_plugin_to_string;
	plugin_class->composite_cleanup = fu_test_plugin_composite_cleanup;
	plugin_class->composite_prepare = fu_test_plugin_composite_prepare;
	plugin_class->get_results = fu_test_plugin_get_results;
	plugin_class->activate = fu_test_plugin_activate;
	plugin_class->write_firmware = fu_test_plugin_write_firmware;
	plugin_class->verify = fu_test_plugin_verify;
	plugin_class->startup = fu_test_plugin_startup;
	plugin_class->coldplug = fu_test_plugin_coldplug;
	plugin_class->device_registered = fu_test_plugin_device_registered;
}
