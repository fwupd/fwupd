/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-test-plugin.h"

struct _FuTestPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuTestPlugin, fu_test_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_test_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(FuDevice) device = NULL;
	device = fu_device_new(ctx);
	fu_device_set_id(device, "FakeDevice");
	fu_device_add_instance_id(device, "b585990a-003e-5270-89d5-3705a17f9a43");
	fu_device_set_name(device, "Integrated_Webcam(TM)");
	fu_device_add_icon(device, FU_DEVICE_ICON_WEB_CAMERA);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_CAN_EMULATION_TAG);
	fu_device_add_request_flag(device, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	fu_device_add_protocol(device, "com.acme.test");
	fu_device_set_summary(device, "Fake webcam");
	fu_device_set_vendor(device, "ACME Corp.");
	fu_device_build_vendor_id_u16(device, "USB", 0x046D);
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version_bootloader(device, "0.1.2");
	fu_device_set_version(device, "1.2.2");
	fu_device_set_version_lowest(device, "1.2.0");

	if (fu_plugin_get_config_value_boolean(plugin, "RegistrationSupported")) {
		fu_plugin_device_register(plugin, device);
		if (fu_device_get_metadata(device, "BestDevice") == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_FOUND,
					    "Device not set by another plugin");
			return FALSE;
		}
	}
	fu_plugin_device_add(plugin, device);

	if (fu_plugin_get_config_value_boolean(plugin, "CompositeChild")) {
		g_autoptr(FuDevice) child1 = NULL;
		g_autoptr(FuDevice) child2 = NULL;

		child1 = fu_device_new(ctx);
		fu_device_build_vendor_id_u16(child1, "USB", 0xFFFF);
		fu_device_add_protocol(child1, "com.acme");
		fu_device_set_physical_id(child1, "fake");
		fu_device_set_logical_id(child1, "child1");
		fu_device_add_instance_id(child1, "7fddead7-12b5-4fb9-9fa0-6d30305df755");
		fu_device_set_name(child1, "Module1");
		fu_device_set_version_format(child1, FWUPD_VERSION_FORMAT_PLAIN);
		fu_device_set_version(child1, "1");
		fu_device_add_parent_guid(child1, "b585990a-003e-5270-89d5-3705a17f9a43");
		fu_device_add_flag(child1, FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_device_add_flag(child1, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
		fu_device_add_private_flag(child1, FU_DEVICE_PRIVATE_FLAG_INSTALL_PARENT_FIRST);
		fu_plugin_device_add(plugin, child1);

		child2 = fu_device_new(ctx);
		fu_device_build_vendor_id_u16(child2, "USB", 0xFFFF);
		fu_device_add_protocol(child2, "com.acme");
		fu_device_set_physical_id(child2, "fake");
		fu_device_set_logical_id(child2, "child2");
		fu_device_add_instance_id(child2, "b8fe6b45-8702-4bcd-8120-ef236caac76f");
		fu_device_set_name(child2, "Module2");
		fu_device_set_version_format(child2, FWUPD_VERSION_FORMAT_PLAIN);
		fu_device_set_version(child2, "10");
		fu_device_add_parent_guid(child2, "b585990a-003e-5270-89d5-3705a17f9a43");
		fu_device_add_flag(child2, FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_device_add_flag(child2, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
		fu_device_add_private_flag(child2, FU_DEVICE_PRIVATE_FLAG_INSTALL_PARENT_FIRST);
		fu_plugin_device_add(plugin, child2);
	}

	return TRUE;
}

static gboolean
fu_test_plugin_modify_config(FuPlugin *plugin, const gchar *key, const gchar *value, GError **error)
{
	const gchar *keys[] = {"AnotherWriteRequired",
			       "CompositeChild",
			       "DecompressDelay",
			       "NeedsActivation",
			       "NeedsReboot",
			       "RegistrationSupported",
			       "RequestDelay",
			       "RequestSupported",
			       "VerifyDelay",
			       "WriteDelay",
			       "WriteSupported",
			       NULL};
	if (!g_strv_contains(keys, key)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "config key %s not supported",
			    key);
		return FALSE;
	}
	return fu_plugin_set_config_value(plugin, key, value, error);
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
	guint64 val = 0;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *str_safe = fu_strsafe_bytes(blob_fw, G_MAXSIZE);

	if (str_safe == NULL)
		return NULL;
	if (!fu_strtoull(str_safe, &val, 0, G_MAXSIZE, FU_INTEGER_BASE_AUTO, &error_local)) {
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
			      FuFirmware *firmware,
			      FuProgress *progress,
			      FwupdInstallFlags flags,
			      GError **error)
{
	g_autofree gchar *decompress_delay_str = NULL;
	g_autofree gchar *write_delay_str = NULL;
	g_autofree gchar *verify_delay_str = NULL;
	guint64 delay_decompress_ms = 0;
	guint64 delay_write_ms = 0;
	guint64 delay_verify_ms = 0;
	guint64 delay_request_ms = 0;

	if (!fu_plugin_get_config_value_boolean(plugin, "WriteSupported")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device was not in supported mode");
		return FALSE;
	}

	fu_progress_set_status(progress, FWUPD_STATUS_DECOMPRESSING);
	decompress_delay_str = fu_plugin_get_config_value(plugin, "DecompressDelay");
	if (decompress_delay_str != NULL) {
		if (!fu_strtoull(decompress_delay_str,
				 &delay_decompress_ms,
				 0,
				 10000,
				 FU_INTEGER_BASE_AUTO,
				 error)) {
			g_prefix_error_literal(error, "failed to parse DecompressDelay: ");
			return FALSE;
		}
	}
	for (guint i = 0; i <= delay_decompress_ms; i++) {
		fu_device_sleep(device, 1);
		fu_progress_set_percentage_full(progress, i, delay_decompress_ms);
	}

	/* send an interactive request, and wait some time */
	if (fu_plugin_get_config_value_boolean(plugin, "RequestSupported")) {
		g_autofree gchar *request_delay_str = NULL;
		g_autoptr(FwupdRequest) request = fwupd_request_new();

		fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
		fwupd_request_set_id(request, FWUPD_REQUEST_ID_REMOVE_REPLUG);
		fwupd_request_add_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
		fwupd_request_set_message(request,
					  "Please pretend to remove the device you cannot see or "
					  "touch and please re-insert it.");
		if (!fu_device_emit_request(device, request, progress, error))
			return FALSE;
		request_delay_str = fu_plugin_get_config_value(plugin, "RequestDelay");
		if (request_delay_str != NULL) {
			if (!fu_strtoull(request_delay_str,
					 &delay_request_ms,
					 0,
					 10000,
					 FU_INTEGER_BASE_AUTO,
					 error)) {
				g_prefix_error_literal(error, "failed to parse RequestDelay: ");
				return FALSE;
			}
		}
		fu_device_sleep(device, delay_request_ms);
	}

	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	write_delay_str = fu_plugin_get_config_value(plugin, "WriteDelay");
	if (write_delay_str != NULL) {
		if (!fu_strtoull(write_delay_str,
				 &delay_write_ms,
				 0,
				 10000,
				 FU_INTEGER_BASE_AUTO,
				 error)) {
			g_prefix_error_literal(error, "failed to parse WriteDelay: ");
			return FALSE;
		}
	}
	for (guint i = 0; i <= delay_write_ms; i++) {
		fu_device_sleep(device, 1);
		fu_progress_set_percentage_full(progress, i, delay_write_ms);
	}
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_VERIFY);
	verify_delay_str = fu_plugin_get_config_value(plugin, "VerifyDelay");
	if (verify_delay_str != NULL) {
		if (!fu_strtoull(verify_delay_str,
				 &delay_verify_ms,
				 0,
				 10000,
				 FU_INTEGER_BASE_AUTO,
				 error)) {
			g_prefix_error_literal(error, "failed to parse VerifyDelay: ");
			return FALSE;
		}
	}
	for (guint i = 0; i <= delay_verify_ms; i++) {
		fu_device_sleep(device, 1);
		fu_progress_set_percentage_full(progress, i, delay_verify_ms);
	}

	/* composite test, upgrade composite devices */
	if (fu_plugin_get_config_value_boolean(plugin, "CompositeChild")) {
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
	if (fu_plugin_get_config_value_boolean(plugin, "NeedsActivation")) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
	} else if (fu_plugin_get_config_value_boolean(plugin, "NeedsReboot")) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	} else {
		g_autofree gchar *ver = NULL;
		g_autoptr(GBytes) blob_fw = NULL;
		g_autoptr(GInputStream) stream = NULL;

		stream = fu_firmware_get_stream(firmware, error);
		if (stream == NULL)
			return FALSE;
		blob_fw = fu_input_stream_read_bytes(stream, 0x0, 9, NULL, error);
		if (blob_fw == NULL)
			return FALSE;
		ver = fu_test_plugin_get_version(blob_fw);
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
	if (fu_plugin_get_config_value_boolean(plugin, "AnotherWriteRequired") &&
	    !fu_device_get_metadata_boolean(device, "DoneAnotherWriteRequired")) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED);
		fu_device_set_metadata_boolean(device, "DoneAnotherWriteRequired", TRUE);
	}

	/* do this all over again */
	if (fu_plugin_get_config_value_boolean(plugin, "InstallLoopRestart") &&
	    !fu_device_get_metadata_boolean(device, "DoneInstallLoopRestart")) {
		fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_INSTALL_LOOP_RESTART);
		fu_device_set_metadata_boolean(device, "DoneInstallLoopRestart", TRUE);
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
	return TRUE;
}

static gboolean
fu_test_plugin_composite_prepare(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	if (fu_plugin_get_config_value_boolean(plugin, "CompositeChild")) {
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
	if (fu_plugin_get_config_value_boolean(plugin, "CompositeChild")) {
		for (guint i = 0; i < devices->len; i++) {
			FuDevice *device = g_ptr_array_index(devices, i);
			fu_device_set_metadata(device, "frombulator", "1");
		}
	}
	return TRUE;
}

static gboolean
fu_test_plugin_attach(FuPlugin *plugin, FuDevice *device, FuProgress *progress, GError **error)
{
	fu_device_set_metadata_integer(device,
				       "nr-attach",
				       fu_device_get_metadata_integer(device, "nr-attach") + 1);
	return TRUE;
}

static void
fu_test_plugin_init(FuTestPlugin *self)
{
	fu_plugin_add_flag(FU_PLUGIN(self), FWUPD_PLUGIN_FLAG_TEST_ONLY);
}

static void
fu_test_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_set_config_default(plugin, "AnotherWriteRequired", "false");
	fu_plugin_set_config_default(plugin, "InstallLoopRestart", "false");
	fu_plugin_set_config_default(plugin, "CompositeChild", "false");
	fu_plugin_set_config_default(plugin, "DecompressDelay", "0");
	fu_plugin_set_config_default(plugin, "NeedsActivation", "false");
	fu_plugin_set_config_default(plugin, "NeedsReboot", "false");
	fu_plugin_set_config_default(plugin, "RegistrationSupported", "false");
	fu_plugin_set_config_default(plugin, "RequestDelay", "10"); /* ms */
	fu_plugin_set_config_default(plugin, "RequestSupported", "false");
	fu_plugin_set_config_default(plugin, "VerifyDelay", "0");
	fu_plugin_set_config_default(plugin, "WriteDelay", "0");
	fu_plugin_set_config_default(plugin, "WriteSupported", "true");
}

static void
fu_test_plugin_finalize(GObject *obj)
{
	g_debug("destroy");
	G_OBJECT_CLASS(fu_test_plugin_parent_class)->finalize(obj);
}

static void
fu_test_plugin_class_init(FuTestPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_test_plugin_finalize;
	plugin_class->constructed = fu_test_plugin_constructed;
	plugin_class->composite_cleanup = fu_test_plugin_composite_cleanup;
	plugin_class->composite_prepare = fu_test_plugin_composite_prepare;
	plugin_class->get_results = fu_test_plugin_get_results;
	plugin_class->activate = fu_test_plugin_activate;
	plugin_class->write_firmware = fu_test_plugin_write_firmware;
	plugin_class->verify = fu_test_plugin_verify;
	plugin_class->attach = fu_test_plugin_attach;
	plugin_class->coldplug = fu_test_plugin_coldplug;
	plugin_class->device_registered = fu_test_plugin_device_registered;
	plugin_class->modify_config = fu_test_plugin_modify_config;
}
