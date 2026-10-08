/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-test-bluetooth-proxy.h"

static FuContext *
fu_bluetooth_device_test_ctx_new(void)
{
	gboolean ret;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", "quirks.d", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_DATADIR_QUIRKS, testdatadir);
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	return g_steal_pointer(&ctx);
}

static void
fu_bluetooth_device_modalias_func(void)
{
	g_autofree gchar *str = NULL;
	g_autoptr(FuContext) ctx = fu_bluetooth_device_test_ctx_new();
	g_autoptr(FuBluetoothDevice) dev_usb =
	    g_object_new(FU_TYPE_BLUETOOTH_DEVICE, "context", ctx, NULL);
	g_autoptr(FuBluetoothDevice) dev_bt =
	    g_object_new(FU_TYPE_BLUETOOTH_DEVICE, "context", ctx, NULL);

	/* usb-style modalias */
	fu_bluetooth_device_set_modalias(dev_usb, "usb:v0461p4EEFd0001");
	g_assert_cmpint(fu_device_get_vid(FU_DEVICE(dev_usb)), ==, 0x0461);
	g_assert_cmpint(fu_device_get_pid(FU_DEVICE(dev_usb)), ==, 0x4EEF);
	g_assert_true(fu_device_has_vendor_id(FU_DEVICE(dev_usb), "BLUETOOTH:0x0461"));
	g_assert_cmpstr(fu_device_get_version(FU_DEVICE(dev_usb)), ==, "0.1");

	/* the modalias is serialized to the daemon string */
	str = fu_device_to_string(FU_DEVICE(dev_usb));
	g_assert_nonnull(g_strstr_len(str, -1, "usb:v0461p4EEFd0001"));

	/* bluetooth-style modalias */
	fu_bluetooth_device_set_modalias(dev_bt, "bluetooth:v000ApFFFFdFFFF");
	g_assert_cmpint(fu_device_get_vid(FU_DEVICE(dev_bt)), ==, 0x000A);
	g_assert_cmpint(fu_device_get_pid(FU_DEVICE(dev_bt)), ==, 0xFFFF);
}

static void
fu_bluetooth_device_proxy_func(void)
{
	gboolean ret;
	gint32 mtu = 0;
	g_autofree gchar *str = NULL;
	g_autoptr(FuContext) ctx = fu_bluetooth_device_test_ctx_new();
	g_autoptr(FuBluetoothDevice) dev =
	    g_object_new(FU_TYPE_BLUETOOTH_DEVICE, "context", ctx, NULL);
	g_autoptr(FuTestBluetoothProxy) proxy = fu_test_bluetooth_proxy_new();
	g_autoptr(FuIOChannel) io_notify = NULL;
	g_autoptr(FuIOChannel) io_write = NULL;
	g_autoptr(GByteArray) buf_rd = NULL;
	g_autoptr(GByteArray) buf_wr = g_byte_array_new();
	g_autoptr(GError) error = NULL;

	fu_device_set_proxy(FU_DEVICE(dev), FU_DEVICE(proxy));

	/* read / read-string go via the proxy */
	buf_rd = fu_bluetooth_device_read(dev, FU_BLUETOOTH_DEVICE_UUID_DI_MODEL_NUMBER, &error);
	g_assert_no_error(error);
	g_assert_nonnull(buf_rd);
	g_assert_cmpint(buf_rd->len, ==, 4);
	str =
	    fu_bluetooth_device_read_string(dev, FU_BLUETOOTH_DEVICE_UUID_DI_MODEL_NUMBER, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(str, ==, "test");

	/* write */
	fu_byte_array_append_uint8(buf_wr, 0xFE);
	ret = fu_bluetooth_device_write(dev,
					FU_BLUETOOTH_DEVICE_UUID_DI_MODEL_NUMBER,
					buf_wr,
					&error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* notify start and stop */
	ret =
	    fu_bluetooth_device_notify_start(dev, FU_BLUETOOTH_DEVICE_UUID_DI_MODEL_NUMBER, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret =
	    fu_bluetooth_device_notify_stop(dev, FU_BLUETOOTH_DEVICE_UUID_DI_MODEL_NUMBER, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* acquire notify / write channels */
	io_notify = fu_bluetooth_device_notify_acquire(dev,
						       FU_BLUETOOTH_DEVICE_UUID_DI_MODEL_NUMBER,
						       &mtu,
						       &error);
	if (io_notify == NULL && g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip("no memfd support");
		return;
	}
	g_assert_no_error(error);
	g_assert_nonnull(io_notify);
	g_assert_cmpint(mtu, ==, 512);
	io_write = fu_bluetooth_device_write_acquire(dev,
						     FU_BLUETOOTH_DEVICE_UUID_DI_MODEL_NUMBER,
						     NULL,
						     &error);
	g_assert_no_error(error);
	g_assert_nonnull(io_write);

	/* a full probe reads the Device Information service via the proxy */
	ret = fu_device_probe(FU_DEVICE(dev), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_device_get_serial(FU_DEVICE(dev)), ==, "test");
}

static void
fu_bluetooth_device_emulated_func(void)
{
	g_autofree gchar *str = NULL;
	g_autoptr(FuContext) ctx = fu_bluetooth_device_test_ctx_new();
	g_autoptr(FuBluetoothDevice) dev =
	    g_object_new(FU_TYPE_BLUETOOTH_DEVICE, "context", ctx, NULL);
	g_autoptr(FuDeviceEvent) event = NULL;
	g_autoptr(GError) error = NULL;
	const guint8 data[] = {'h', 'i'};

	/* record a read event, then replay it */
	fu_device_add_flag(FU_DEVICE(dev), FWUPD_DEVICE_FLAG_EMULATED);
	event = fu_device_event_new("Read:Uuid=" FU_BLUETOOTH_DEVICE_UUID_DI_MODEL_NUMBER);
	fu_device_event_set_data(event, "Data", data, sizeof(data));
	fu_device_add_event(FU_DEVICE(dev), event);

	str =
	    fu_bluetooth_device_read_string(dev, FU_BLUETOOTH_DEVICE_UUID_DI_MODEL_NUMBER, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(str, ==, "hi");
}

static void
fu_bluetooth_device_json_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_bluetooth_device_test_ctx_new();
	g_autoptr(FuBluetoothDevice) dev =
	    g_object_new(FU_TYPE_BLUETOOTH_DEVICE, "context", ctx, NULL);
	g_autoptr(FuBluetoothDevice) dev2 =
	    g_object_new(FU_TYPE_BLUETOOTH_DEVICE, "context", ctx, NULL);
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GString) json = NULL;

	fu_device_set_name(FU_DEVICE(dev), "Mouse");
	fu_bluetooth_device_set_modalias(dev, "usb:v0461p4EEFd0001");
	fu_device_set_battery_level(FU_DEVICE(dev), 50);

	/* serialize via the device add_json vfunc */
	fu_device_add_json(FU_DEVICE(dev), json_obj, FWUPD_CODEC_FLAG_NONE);
	json = fwupd_json_object_to_string(json_obj, FWUPD_JSON_EXPORT_FLAG_NONE);
	g_assert_nonnull(json);
	g_assert_nonnull(g_strstr_len(json->str, -1, "FuBluetoothDevice"));
	g_assert_nonnull(g_strstr_len(json->str, -1, "Mouse"));

	/* parse back via the device from_json vfunc */
	ret = fu_device_from_json(FU_DEVICE(dev2), json_obj, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_device_get_name(FU_DEVICE(dev2)), ==, "Mouse");
	g_assert_cmpint(fu_device_get_vid(FU_DEVICE(dev2)), ==, 0x0461);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/bluetooth-device/modalias", fu_bluetooth_device_modalias_func);
	g_test_add_func("/fwupd/bluetooth-device/proxy", fu_bluetooth_device_proxy_func);
	g_test_add_func("/fwupd/bluetooth-device/emulated", fu_bluetooth_device_emulated_func);
	g_test_add_func("/fwupd/bluetooth-device/json", fu_bluetooth_device_json_func);
	return g_test_run();
}
