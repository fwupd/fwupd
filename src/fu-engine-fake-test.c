/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-device.h"
#include "fu-engine.h"

static void
fu_engine_fake_hidraw(gconstpointer user_data)
{
	FuContext *ctx = (FuContext *)user_data;
	gboolean ret;
	g_autofree gchar *value2 = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuUdevDevice) udev_device2 = NULL;
	g_autoptr(FuUdevDevice) udev_device3 = NULL;
	g_autoptr(GError) error = NULL;

	/* non-linux */
	if (!fu_context_has_backend(ctx, "udev")) {
		g_test_skip("no Udev backend");
		return;
	}

	/* load engine and check the device was found */
	fu_engine_add_plugin_filter(engine, "pixart_rf");
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS |
				 FU_ENGINE_LOAD_FLAG_READONLY,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* hidraw -> pixart_rf */
	device = fu_engine_get_device(engine, "ab6b164573f0782ee23e38740d0e0934ee352090", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), ==, "hidraw");
	g_assert_cmpstr(fu_udev_device_get_devtype(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpstr(fu_udev_device_get_driver(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpint(fu_device_get_vid(device), ==, 0x093a);
	g_assert_cmpint(fu_device_get_pid(device), ==, 0x2862);
	g_assert_cmpstr(fu_device_get_plugin(device), ==, "pixart_rf");
	g_assert_cmpstr(fu_device_get_name(device), ==, "PIXART Pixart dual-mode mouse");
	g_assert_cmpstr(fu_device_get_physical_id(device), ==, "usb-0000_00_14.0-1/input1");
	g_assert_cmpstr(fu_device_get_logical_id(device), ==, NULL);

	/* check can read random files */
	value2 = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(device),
					   "dev",
					   FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					   &error);
	g_assert_no_error(error);
	g_assert_cmpstr(value2, ==, "241:1");

	/* get child, both specified */
	udev_device2 = FU_UDEV_DEVICE(
	    fu_device_get_backend_parent_with_subsystem(device, "usb:usb_interface", &error));
	g_assert_no_error(error);
	g_assert_nonnull(udev_device2);
	g_assert_cmpstr(fu_udev_device_get_subsystem(udev_device2), ==, "usb");

	/* get child, initially unprobed */
	udev_device3 =
	    FU_UDEV_DEVICE(fu_device_get_backend_parent_with_subsystem(device, "usb", &error));
	g_assert_no_error(error);
	g_assert_nonnull(udev_device3);
	g_assert_cmpstr(fu_udev_device_get_subsystem(udev_device3), ==, "usb");
	g_assert_cmpstr(fu_udev_device_get_driver(udev_device3), ==, "usb");
}

static void
fu_engine_fake_usb(gconstpointer user_data)
{
	FuContext *ctx = (FuContext *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	/* non-linux */
	if (!fu_context_has_backend(ctx, "udev")) {
		g_test_skip("no Udev backend");
		return;
	}

	/* load engine and check the device was found */
	fu_engine_add_plugin_filter(engine, "hughski_colorhug");
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS |
				 FU_ENGINE_LOAD_FLAG_READONLY,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* USB -> colorhug */
	device = fu_engine_get_device(engine, "d787669ee4a103fe0b361fe31c10ea037c72f27c", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), ==, "usb");
	g_assert_cmpstr(fu_udev_device_get_devtype(FU_UDEV_DEVICE(device)), ==, "usb_device");
	g_assert_cmpstr(fu_udev_device_get_driver(FU_UDEV_DEVICE(device)), ==, "usb");
	g_assert_cmpint(fu_device_get_vid(device), ==, 0x093a);
	g_assert_cmpint(fu_device_get_pid(device), ==, 0x2862);
	g_assert_cmpstr(fu_device_get_plugin(device), ==, "hughski_colorhug");
	g_assert_cmpstr(fu_device_get_physical_id(device), ==, "1-1");
	g_assert_cmpstr(fu_device_get_logical_id(device), ==, NULL);
}

static void
fu_engine_fake_v4l(gconstpointer user_data)
{
	FuContext *ctx = (FuContext *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	/* non-linux */
	if (!fu_context_has_backend(ctx, "udev")) {
		g_test_skip("no Udev backend");
		return;
	}

	/* load engine and check the device was found */
	fu_engine_add_plugin_filter(engine, "logitech_tap");
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS |
				 FU_ENGINE_LOAD_FLAG_READONLY,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* no -Dplugin_logitech_tap=enabled */
	if (fu_engine_get_plugin_by_name(engine, "logitech_tap", &error) == NULL) {
		g_test_skip(error->message);
		return;
	}

	/* v4l -> logitech_tap */
	device = fu_engine_get_device(engine, "d787669ee4a103fe0b361fe31c10ea037c72f27c", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), ==, "video4linux");
	g_assert_cmpstr(fu_udev_device_get_devtype(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpstr(fu_udev_device_get_driver(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpint(fu_device_get_vid(device), ==, 0x093A);
	g_assert_cmpint(fu_device_get_pid(device), ==, 0x2862);
	g_assert_cmpint(fu_v4l_device_get_index(FU_V4L_DEVICE(device)), ==, 0);
	g_assert_cmpint(fu_v4l_device_get_caps(FU_V4L_DEVICE(device)), ==, FU_V4L_CAP_NONE);
	g_assert_cmpstr(fu_device_get_name(device), ==, "Integrated Camera: Integrated C");
	g_assert_cmpstr(fu_device_get_plugin(device), ==, "logitech_tap");
}

static void
fu_engine_fake_nvme(gconstpointer user_data)
{
	FuContext *ctx = (FuContext *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	/* non-linux */
	if (!fu_context_has_backend(ctx, "udev")) {
		g_test_skip("no Udev backend");
		return;
	}

	/* load engine and check the device was found */
	fu_engine_add_plugin_filter(engine, "nvme");
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS |
				 FU_ENGINE_LOAD_FLAG_READONLY | FU_ENGINE_LOAD_FLAG_NO_CACHE,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* no linux/nvme_ioctl.h */
	if (fu_engine_get_plugin_by_name(engine, "nvme", &error) == NULL) {
		g_test_skip(error->message);
		return;
	}

	/* NVMe -> nvme */
	device = fu_engine_get_device(engine, "4c263c95f596030b430d65dc934f6722bcee5720", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), ==, "nvme");
	g_assert_cmpstr(fu_udev_device_get_devtype(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpint(fu_udev_device_get_number(FU_UDEV_DEVICE(device)), ==, 1);
	g_assert_cmpstr(fu_udev_device_get_driver(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpstr(fu_udev_device_get_device_file(FU_UDEV_DEVICE(device)), ==, "/dev/nvme1");
	g_assert_cmpint(fu_device_get_vid(device), ==, 0x1179);
	g_assert_cmpint(fu_device_get_pid(device), ==, 0x010F);
	g_assert_true(fu_device_has_vendor_id(device, "PCI:0x1179"));
	g_assert_cmpstr(fu_device_get_vendor(device), ==, NULL);
	g_assert_cmpstr(fu_device_get_plugin(device), ==, "nvme");
	g_assert_cmpstr(fu_device_get_physical_id(device), ==, "PCI_SLOT_NAME=0000:00:1b.0");
	g_assert_cmpstr(fu_device_get_logical_id(device), ==, NULL);
}

static void
fu_engine_fake_serio(gconstpointer user_data)
{
	FuContext *ctx = (FuContext *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	/* non-linux */
	if (!fu_context_has_backend(ctx, "udev")) {
		g_test_skip("no Udev backend");
		return;
	}

	/* load engine and check the device was found */
	fu_engine_add_plugin_filter(engine, "synaptics_rmi");
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS |
				 FU_ENGINE_LOAD_FLAG_READONLY,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* no gnutls */
	if (fu_engine_get_plugin_by_name(engine, "synaptics_rmi", &error) == NULL) {
		g_test_skip(error->message);
		return;
	}

	/* serio */
	device = fu_engine_get_device(engine, "d8419b7614e50c6fb6162b5dca34df5236a62a8d", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), ==, "serio");
	g_assert_cmpstr(fu_udev_device_get_devtype(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpstr(fu_udev_device_get_driver(FU_UDEV_DEVICE(device)), ==, "psmouse");
	g_assert_cmpstr(fu_udev_device_get_device_file(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpint(fu_device_get_vid(device), ==, 0x0);
	g_assert_cmpint(fu_device_get_pid(device), ==, 0x0);
	g_assert_cmpstr(fu_device_get_name(device), ==, "TouchStyk");
	g_assert_cmpstr(fu_device_get_plugin(device), ==, "synaptics_rmi");
	g_assert_cmpstr(fu_device_get_physical_id(device),
			==,
			"DEVPATH=/devices/platform/i8042/serio1");
	g_assert_cmpstr(fu_device_get_logical_id(device), ==, NULL);
	g_assert_true(fu_device_has_instance_id(device,
						"SERIO\\FWID_LEN0305-PNP0F13",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE));
}

static void
fu_engine_fake_tpm(gconstpointer user_data)
{
	FuContext *ctx = (FuContext *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	/* non-linux */
	if (!fu_context_has_backend(ctx, "udev")) {
		g_test_skip("no Udev backend");
		return;
	}

	if (g_getenv("TPM2TOOLS_TCTI") != NULL) {
		g_test_skip("Using software TPM, skipping fake TPM test");
		return;
	}

	/* load engine and check the device was found */
	fu_engine_add_plugin_filter(engine, "tpm");
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS |
				 FU_ENGINE_LOAD_FLAG_READONLY,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* no tss2-esys */
	if (fu_engine_get_plugin_by_name(engine, "tpm", &error) == NULL) {
		g_test_skip(error->message);
		return;
	}

	/* tpm */
	device = fu_engine_get_device(engine, "1d8d50a4dbc65618f5c399c2ae827b632b3ccc11", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), ==, "tpm");
	g_assert_cmpstr(fu_udev_device_get_devtype(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpstr(fu_udev_device_get_driver(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpstr(fu_udev_device_get_device_file(FU_UDEV_DEVICE(device)), ==, "/dev/tpm0");
	g_assert_cmpint(fu_device_get_vid(device), ==, 0x0);
	g_assert_cmpint(fu_device_get_pid(device), ==, 0x0);
	g_assert_cmpstr(fu_device_get_plugin(device), ==, "tpm");
	g_assert_cmpstr(fu_device_get_physical_id(device), ==, "DEVNAME=tpm0");
	g_assert_cmpstr(fu_device_get_logical_id(device), ==, NULL);
}

static void
fu_engine_fake_block(gconstpointer user_data)
{
	FuContext *ctx = (FuContext *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	/* non-linux */
	if (!fu_context_has_backend(ctx, "udev")) {
		g_test_skip("no Udev backend");
		return;
	}

	/* load engine and check the device was found */
	fu_engine_add_plugin_filter(engine, "scsi");
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS |
				 FU_ENGINE_LOAD_FLAG_READONLY,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* no Udev */
	if (fu_engine_get_plugin_by_name(engine, "scsi", &error) == NULL) {
		g_test_skip(error->message);
		return;
	}

	/* block */
	device = fu_engine_get_device(engine, "82063150bef0a76856b9ab79cbf88e4f6ef2f93d", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), ==, "block");
	g_assert_cmpstr(fu_udev_device_get_devtype(FU_UDEV_DEVICE(device)), ==, "disk");
	g_assert_cmpstr(fu_udev_device_get_driver(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpstr(fu_udev_device_get_device_file(FU_UDEV_DEVICE(device)), ==, "/dev/sde");
	g_assert_cmpstr(fu_device_get_plugin(device), ==, "scsi");
	g_assert_cmpstr(fu_device_get_vendor(device), ==, "IBM-ESXS");
}

int
main(int argc, char **argv)
{
	gboolean ret;
	g_autofree gchar *sysfsdir = NULL;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);

	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_DATADIR", testdatadir, TRUE);
	sysfsdir = g_test_build_filename(G_TEST_DIST, "tests", "sys", NULL);
	(void)g_setenv("FWUPD_SYSFSDIR", sysfsdir, TRUE);
	(void)g_setenv("FWUPD_SELF_TEST", "1", TRUE);

	/* do not save silo */
	ctx = fu_context_new();
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_IDLE_SOURCES);
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_test_add_data_func("/fwupd/engine/fake/hidraw", ctx, fu_engine_fake_hidraw);
	g_test_add_data_func("/fwupd/engine/fake/usb", ctx, fu_engine_fake_usb);
	g_test_add_data_func("/fwupd/engine/fake/serio", ctx, fu_engine_fake_serio);
	g_test_add_data_func("/fwupd/engine/fake/nvme", ctx, fu_engine_fake_nvme);
	g_test_add_data_func("/fwupd/engine/fake/block", ctx, fu_engine_fake_block);
	g_test_add_data_func("/fwupd/engine/fake/tpm", ctx, fu_engine_fake_tpm);
	g_test_add_data_func("/fwupd/engine/fake/v4l", ctx, fu_engine_fake_v4l);
	return g_test_run();
}
