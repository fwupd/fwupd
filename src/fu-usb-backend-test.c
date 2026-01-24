/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-bytes.h"
#include "fu-context-private.h"
#include "fu-device-locker.h"
#include "fu-device-private.h"
#include "fu-usb-backend.h"
#include "fu-usb-device.h"

static void
fu_usb_backend_hotplug_cb(FuBackend *backend, FuDevice *device, gpointer user_data)
{
	guint *cnt = (guint *)user_data;
	(*cnt)++;
}

static void
fu_usb_backend_load_file(FuBackend *backend, const gchar *fn)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(FwupdJsonParser) json_parser = fwupd_json_parser_new();
	g_autoptr(FwupdJsonNode) json_node = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* set appropriate limits */
	fwupd_json_parser_set_max_depth(json_parser, 10);
	fwupd_json_parser_set_max_items(json_parser, 1000);
	fwupd_json_parser_set_max_quoted(json_parser, 100000);

	g_debug("loading %s", fn);
	stream = fu_input_stream_from_path(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	json_node = fwupd_json_parser_load_from_stream(json_parser,
						       stream,
						       FWUPD_JSON_LOAD_FLAG_NONE,
						       &error);
	g_assert_no_error(error);
	g_assert_nonnull(json_node);
	json_obj = fwupd_json_node_get_object(json_node, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json_obj);
	ret = fwupd_codec_from_json(FWUPD_CODEC(backend), json_obj, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

/*
 * To generate the fwupd DS20 descriptor in the usb-devices.json file save fw-ds20.builder.xml:
 *
 *    <firmware gtype="FuUsbDeviceFwDs20">
 *      <idx>42</idx>   <!-- bVendorCode -->
 *      <size>32</size> <!-- wLength -->
 *    </firmware>
 *
 * Then run:
 *
 *    fwupdtool firmware-build fw-ds20.builder.xml fw-ds20.bin
 *    base64 fw-ds20.bin
 *
 * To generate the fake control transfer response, save fw-ds20.quirk:
 *
 *    [USB\VID_273F&PID_1004]
 *    Plugin = dfu
 *    Icon = computer
 *
 * Then run:
 *
 *    contrib/generate-ds20.py fw-ds20.quirk --bufsz 32
 */
static void
fu_usb_backend(gconstpointer user_data)
{
	FuContext *ctx = (FuContext *)user_data;
	gboolean ret;
	guint cnt_added = 0;
	guint cnt_removed = 0;
	FuDevice *device_tmp;
	g_autofree gchar *usb_emulate_fn = NULL;
	g_autofree gchar *usb_emulate_fn2 = NULL;
	g_autofree gchar *usb_emulate_fn3 = NULL;
	g_autofree gchar *devicestr = NULL;
	g_autoptr(FuBackend) backend = fu_usb_backend_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) possible_plugins = NULL;

	/* check there were events */
	g_object_set(backend, "device-gtype", FU_TYPE_USB_DEVICE, NULL);
	g_signal_connect(backend,
			 "device-added",
			 G_CALLBACK(fu_usb_backend_hotplug_cb),
			 &cnt_added);
	g_signal_connect(backend,
			 "device-removed",
			 G_CALLBACK(fu_usb_backend_hotplug_cb),
			 &cnt_removed);

	/* load the JSON into the backend */
	g_assert_cmpstr(fu_backend_get_name(backend), ==, "usb");
	g_assert_true(fu_backend_get_enabled(backend));
	ret = fu_backend_setup(backend, FU_BACKEND_SETUP_FLAG_NONE, progress, &error);
	g_assert_cmpint(cnt_added, ==, 0);
	g_assert_cmpint(cnt_removed, ==, 0);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(cnt_added, ==, 0);
	g_assert_cmpint(cnt_removed, ==, 0);
	ret = fu_backend_coldplug(backend, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(cnt_added, ==, 0);
	g_assert_cmpint(cnt_removed, ==, 0);
	usb_emulate_fn = g_test_build_filename(G_TEST_DIST, "tests", "usb-devices.json", NULL);
	g_assert_nonnull(usb_emulate_fn);
	fu_usb_backend_load_file(backend, usb_emulate_fn);
	g_assert_cmpint(cnt_added, ==, 1);
	g_assert_cmpint(cnt_removed, ==, 0);
	devices = fu_backend_get_devices(backend);
	g_assert_cmpint(devices->len, ==, 1);
	device_tmp = g_ptr_array_index(devices, 0);
	fu_device_set_context(device_tmp, ctx);
	ret = fu_device_probe(device_tmp, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_device_has_flag(device_tmp, FWUPD_DEVICE_FLAG_EMULATED));

	/* for debugging */
	devicestr = fu_device_to_string(device_tmp);
	g_debug("%s", devicestr);

	/* check the fwupd DS20 descriptor was parsed */
	g_assert_true(fu_device_has_icon(device_tmp, "computer"));
	possible_plugins = fu_device_get_possible_plugins(device_tmp);
	g_assert_cmpint(possible_plugins->len, ==, 1);
	g_assert_cmpstr(g_ptr_array_index(possible_plugins, 0), ==, "dfu");

	/* load another device with the same VID:PID, and check that we did not get a replug */
	usb_emulate_fn2 =
	    g_test_build_filename(G_TEST_DIST, "tests", "usb-devices-replace.json", NULL);
	g_assert_nonnull(usb_emulate_fn2);
	fu_usb_backend_load_file(backend, usb_emulate_fn2);
	g_assert_cmpint(cnt_added, ==, 1);
	g_assert_cmpint(cnt_removed, ==, 0);

	/* load another device with a different VID:PID, and check that we *did* get a replug */
	usb_emulate_fn3 =
	    g_test_build_filename(G_TEST_DIST, "tests", "usb-devices-bootloader.json", NULL);
	g_assert_nonnull(usb_emulate_fn3);
	fu_usb_backend_load_file(backend, usb_emulate_fn3);
	g_assert_cmpint(cnt_added, ==, 2);
	g_assert_cmpint(cnt_removed, ==, 1);
}

static void
fu_usb_backend_invalid_func(gconstpointer user_data)
{
	FuContext *ctx = (FuContext *)user_data;
	gboolean ret;
	FuDevice *device_tmp;
	g_autofree gchar *usb_emulate_fn = NULL;
	g_autoptr(FuBackend) backend = fu_usb_backend_new(ctx);
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GBytes) usb_emulate_blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(FwupdJsonParser) json_parser = fwupd_json_parser_new();
	g_autoptr(FwupdJsonNode) json_node = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;

#ifndef SUPPORTED_BUILD
	g_test_expect_message("FuUsbDevice",
			      G_LOG_LEVEL_WARNING,
			      "*invalid platform version 0x0000000a, expected >= 0x00010805*");
	g_test_expect_message("FuUsbDevice",
			      G_LOG_LEVEL_WARNING,
			      "failed to parse * BOS descriptor: *invalid UUID*");
#endif

	/* set appropriate limits */
	fwupd_json_parser_set_max_depth(json_parser, 10);
	fwupd_json_parser_set_max_items(json_parser, 100);
	fwupd_json_parser_set_max_quoted(json_parser, 10000);

	/* load the JSON into the backend */
	g_object_set(backend, "device-gtype", FU_TYPE_USB_DEVICE, NULL);
	usb_emulate_fn =
	    g_test_build_filename(G_TEST_DIST, "tests", "usb-devices-invalid.json", NULL);
	usb_emulate_blob = fu_bytes_get_contents(usb_emulate_fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(usb_emulate_blob);
	json_node = fwupd_json_parser_load_from_bytes(json_parser,
						      usb_emulate_blob,
						      FWUPD_JSON_LOAD_FLAG_NONE,
						      &error);
	g_assert_no_error(error);
	g_assert_nonnull(json_node);
	ret = fu_backend_setup(backend, FU_BACKEND_SETUP_FLAG_NONE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	json_obj = fwupd_json_node_get_object(json_node, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json_obj);
	ret = fwupd_codec_from_json(FWUPD_CODEC(backend), json_obj, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_backend_coldplug(backend, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	devices = fu_backend_get_devices(backend);
	g_assert_cmpint(devices->len, ==, 1);
	device_tmp = g_ptr_array_index(devices, 0);
	fu_device_set_context(device_tmp, ctx);

	locker = fu_device_locker_new(device_tmp, &error);
	g_assert_no_error(error);
	g_assert_nonnull(locker);

	/* check the device was processed correctly by FuUsbDevice */
	g_assert_cmpstr(fu_device_get_name(device_tmp), ==, "ColorHug2");
	g_assert_true(fu_device_has_instance_id(device_tmp,
						"USB\\VID_273F&PID_1004",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE));
	g_assert_true(fu_device_has_vendor_id(device_tmp, "USB:0x273F"));

	/* check the fwupd DS20 descriptor was *not* parsed */
	g_assert_false(fu_device_has_icon(device_tmp, "computer"));
}

int
main(int argc, char **argv)
{
	gboolean ret;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);

	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_DATADIR", testdatadir, TRUE);

	/* do not save silo */
	ctx = fu_context_new();
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_IDLE_SOURCES);
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_test_add_data_func("/fwupd/usb-backend", ctx, fu_usb_backend);
	g_test_add_data_func("/fwupd/usb-backend/invalid", ctx, fu_usb_backend_invalid_func);
	return g_test_run();
}
