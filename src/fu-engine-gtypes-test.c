/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-drm-device-private.h"
#include "fu-engine.h"
#include "fu-plugin-private.h"
#include "fu-security-attrs-private.h"

static void
fu_engine_plugin_device_gtype(GType gtype)
{
	GType proxy_gtype;
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuProgress) progress_tmp = fu_progress_new(G_STRLOC);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GHashTable) metadata_post = NULL;
	g_autoptr(GHashTable) metadata_pre = NULL;
	g_autoptr(GInputStream) stream = g_memory_input_stream_new();

	g_debug("loading %s", g_type_name(gtype));
	device = g_object_new(gtype, "context", ctx, "physical-id", "/sys", NULL);
	g_assert_nonnull(device);
	fu_device_set_plugin(device, "test");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_EMULATED);

	/* version convert */
	if (fu_device_get_version_format(device) != FWUPD_VERSION_FORMAT_UNKNOWN)
		fu_device_set_version_raw(device, 0);

	/* progress steps */
	fu_device_set_progress(device, progress_tmp);

	/* report metadata */
	metadata_pre = fu_device_report_metadata_pre(device);
	if (metadata_pre != NULL)
		g_debug("got %u metadata items", g_hash_table_size(metadata_pre));
	metadata_post = fu_device_report_metadata_post(device);
	if (metadata_post != NULL)
		g_debug("got %u metadata items", g_hash_table_size(metadata_post));

	/* security attrs */
	fu_device_add_security_attrs(device, attrs);

	/* quirk kvs */
	ret = fu_device_set_quirk_kv(device,
				     "NoGoingTo",
				     "Exist",
				     FU_CONTEXT_QUIRK_SOURCE_FALLBACK,
				     &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);

	/* to string */
	str = fu_device_to_string(device);
	g_assert_nonnull(str);

	/* proxy required */
	proxy_gtype = fu_device_get_proxy_gtype(device);
	if (proxy_gtype != G_TYPE_INVALID && G_TYPE_FUNDAMENTAL(proxy_gtype) == G_TYPE_OBJECT) {
		g_autoptr(FuDevice) proxy =
		    g_object_new(proxy_gtype, "context", ctx, "physical-id", "/sys", NULL);
		fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_REFCOUNTED_PROXY);
		fu_device_set_proxy(device, proxy);
	}

	/* ->probe() and ->setup */
	locker = fu_device_locker_new(device, NULL);
	if (locker != NULL)
		g_debug("did ->probe() and ->setup()!");

	/* ->prepare(), ->attach(), ->detach, ->cleanup */
	if (fu_device_prepare(device, progress_tmp, FWUPD_INSTALL_FLAG_FORCE, NULL))
		g_debug("did ->prepare()");
	if (fu_device_attach_full(device, progress_tmp, NULL))
		g_debug("did ->attach()");
	if (fu_device_poll(device, NULL))
		g_debug("did ->poll()");
	if (fu_device_detach_full(device, progress_tmp, NULL))
		g_debug("did ->detach()");
	if (fu_device_cleanup(device, progress_tmp, FWUPD_INSTALL_FLAG_FORCE, NULL))
		g_debug("did ->cleanup()");

	/* ->prepare_firmware() */
	firmware = fu_device_prepare_firmware(device,
					      stream,
					      progress_tmp,
					      FU_FIRMWARE_PARSE_FLAG_NONE,
					      NULL);
	if (firmware == NULL) {
		GType firmware_gtype = fu_device_get_firmware_gtype(device);
		if (firmware_gtype != G_TYPE_INVALID)
			firmware = g_object_new(firmware_gtype, NULL);
	}
	if (firmware != NULL) {
		if (fu_device_write_firmware(device,
					     firmware,
					     progress_tmp,
					     FWUPD_INSTALL_FLAG_FORCE,
					     NULL)) {
			g_debug("did ->write_firmware()!");
		}
	}
}

static void
fu_engine_plugin_firmware_gtype(GType gtype)
{
	gboolean ret;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) fw = g_bytes_new_static((const guint8 *)"x", 1);
	g_autoptr(GError) error = NULL;
	const gchar *noxml[] = {
	    "FuFirmware",
	    "FuGenesysUsbhubFirmware",
	    "FuIntelThunderboltFirmware",
	    "FuIntelThunderboltNvm",
	    "FuJsonFirmware",
	    "FuUefiUpdateInfo",
	    NULL,
	};

	g_debug("loading %s", g_type_name(gtype));
	firmware = g_object_new(gtype, NULL);
	g_assert_nonnull(firmware);

	/* ensure we have data set even if parsing fails */
	fu_firmware_set_bytes(firmware, fw);

	/* version convert */
	if (fu_firmware_get_version_format(firmware) != FWUPD_VERSION_FORMAT_UNKNOWN)
		fu_firmware_set_version_raw(firmware, 0);

	/* parse nonsense */
	if (gtype != FU_TYPE_FIRMWARE &&
	    !fu_firmware_has_flag(FU_FIRMWARE(firmware), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION)) {
		ret = fu_firmware_parse_bytes(firmware,
					      fw,
					      0x0,
					      FU_FIRMWARE_PARSE_FLAG_NO_SEARCH |
						  FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM,
					      NULL);
		g_assert_false(ret);
	}

	/* write */
	blob = fu_firmware_write(firmware, NULL);
	if (blob != NULL && g_bytes_get_size(blob) > 0)
		g_debug("saved 0x%x bytes", (guint)g_bytes_get_size(blob));

	/* export -> build */
	if (!g_strv_contains(noxml, g_type_name(gtype))) {
		g_autofree gchar *xml = fu_firmware_export_to_xml(
		    firmware,
		    FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG | FU_FIRMWARE_EXPORT_FLAG_ASCII_DATA,
		    NULL);
		if (xml != NULL) {
			g_autoptr(FuFirmware) firmware2 = fu_firmware_new_from_xml(xml, &error);
			g_assert_no_error(error);
			g_assert_nonnull(firmware2);
		}
	}
}

static void
fu_engine_gtypes_func(void)
{
	GPtrArray *plugins;
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDrmDevice) drm_device = g_object_new(FU_TYPE_DRM_DEVICE, NULL);
	g_autoptr(FuEdid) edid = fu_edid_new();
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(GArray) firmware_gtypes = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();
	const gchar *external_plugins[] = {
	    "flashrom",
	    "modem-manager",
	};

	/* set up test harness */
	g_autofree gchar *testdatadir = NULL;
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSCONFDIR_PKG, testdatadir);

	/* load dummy hwids */
	ret = fu_context_load_hwinfo(ctx, progress, FU_CONTEXT_HWID_FLAG_LOAD_CONFIG, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* load these from the build directory, not the install directory */
	if (g_getenv("G_TEST_BUILDDIR") != NULL) {
		g_autoptr(GPtrArray) external_plugin_dirs = g_ptr_array_new_with_free_func(g_free);
		g_autofree gchar *external_plugindir = NULL;
		for (guint i = 0; i < G_N_ELEMENTS(external_plugins); i++) {
			g_ptr_array_add(external_plugin_dirs,
					g_test_build_filename(G_TEST_BUILT,
							      "..",
							      "plugins",
							      external_plugins[i],
							      NULL));
		}
		external_plugindir = fu_strjoin(",", external_plugin_dirs);
		fu_context_set_path(ctx, FU_PATH_KIND_LIBDIR_PKG, external_plugindir);
	} else {
		fu_context_set_path(ctx, FU_PATH_KIND_LIBDIR_PKG, FWUPD_LIBDIR_PKG);
	}

	/* load all plugins */
	ret =
	    fu_engine_load(engine,
			   FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS |
			       FU_ENGINE_LOAD_FLAG_EXTERNAL_PLUGINS | FU_ENGINE_LOAD_FLAG_NO_CACHE,
			   progress,
			   &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	plugins = fu_engine_get_plugins(engine);
	g_assert_nonnull(plugins);
	g_assert_cmpint(plugins->len, >, 5);

	/* start up plugins */
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		g_autoptr(GError) error_local = NULL;
		if (!fu_plugin_runner_startup(plugin, progress, &error_local))
			g_debug("ignoring: %s", error_local->message);
	}

	/* add security attrs where possible */
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		fu_plugin_runner_add_security_attrs(plugin, attrs);
	}

	/* run the post-reboot action */
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		g_autoptr(FuDevice) device_nop = fu_device_new(ctx);
		g_autoptr(GError) error_local = NULL;
		if (!fu_plugin_runner_reboot_cleanup(plugin, device_nop, &error_local))
			g_debug("ignoring: %s", error_local->message);
	}

	/* run the composite-prepare action */
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		g_autoptr(FuDevice) device_nop = fu_device_new(ctx);
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GPtrArray) devices = g_ptr_array_new();
		g_ptr_array_add(devices, device_nop);
		if (!fu_plugin_runner_composite_prepare(plugin, devices, &error_local))
			g_debug("ignoring: %s", error_local->message);
	}

	/* run the composite-cleanup action */
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		g_autoptr(FuDevice) device_nop = fu_device_new(ctx);
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GPtrArray) devices = g_ptr_array_new();
		g_ptr_array_add(devices, device_nop);
		if (!fu_plugin_runner_composite_cleanup(plugin, devices, &error_local))
			g_debug("ignoring: %s", error_local->message);
	}

	/* run the composite-peek-firmware action */
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		g_autoptr(GBytes) blob = g_bytes_new_static("xxx", 3);
		g_autoptr(FuDevice) device_nop = fu_device_new(ctx);
		g_autoptr(FuFirmware) firmware = fu_firmware_new_from_bytes(blob);
		g_autoptr(GError) error_local = NULL;

		fu_device_set_plugin(device_nop, "uefi_dbx");
		if (!fu_plugin_runner_composite_peek_firmware(plugin,
							      device_nop,
							      firmware,
							      progress,
							      FWUPD_INSTALL_FLAG_NONE,
							      &error_local))
			g_debug("ignoring: %s", error_local->message);
	}

	/* run the device unlock action */
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		g_autoptr(FuDevice) device_nop = fu_device_new(ctx);
		g_autoptr(GError) error_local = NULL;
		if (!fu_plugin_runner_unlock(plugin, device_nop, &error_local))
			g_debug("ignoring: %s", error_local->message);
	}

	/* run the backend-device-added action */
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		GArray *device_gtypes = fu_plugin_get_device_gtypes(plugin);
		if (device_gtypes == NULL || device_gtypes->len == 0) {
			g_autoptr(FuDevice) device_nop = fu_device_new(ctx);
			g_autoptr(GError) error_local = NULL;
			if (!fu_plugin_runner_backend_device_added(plugin,
								   device_nop,
								   progress,
								   &error_local))
				g_debug("ignoring: %s", error_local->message);
		}
	}

	/* register a DRM device as some plugins use these for quirks */
	fu_edid_set_pnp_id(edid, "PNP");
	fu_edid_set_eisa_id(edid, "IBM");
	fu_edid_set_product_name(edid, "Display");
	fu_edid_set_serial_number(edid, "123456");
	fu_edid_set_product_code(edid, 0x1234);
	fu_drm_device_set_edid(drm_device, edid);
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		fu_plugin_runner_device_register(plugin, FU_DEVICE(drm_device));
	}

	/* create each custom device with a context only */
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		GArray *device_gtypes = fu_plugin_get_device_gtypes(plugin);
		for (guint j = 0; device_gtypes != NULL && j < device_gtypes->len; j++) {
			GType gtype = g_array_index(device_gtypes, GType, j);
			fu_engine_plugin_device_gtype(gtype);
		}
	}

	/* create each firmware */
	firmware_gtypes = fu_context_get_firmware_gtypes(ctx);
	for (guint j = 0; j < firmware_gtypes->len; j++) {
		GType gtype = g_array_index(firmware_gtypes, GType, j);
		fu_engine_plugin_firmware_gtype(gtype);
	}
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/engine/gtypes", fu_engine_gtypes_func);
	return g_test_run();
}
