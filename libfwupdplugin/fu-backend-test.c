/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-test.h"

static void
fu_backend_emulate_count_cb(FuBackend *backend, FuDevice *device, gpointer user_data)
{
	guint *cnt = (guint *)user_data;
	(*cnt)++;
}

static void
fu_backend_emulate_func(void)
{
	gboolean ret;
	guint8 buf[] = {0x00, 0x00};
	guint added_cnt = 0;
	guint changed_cnt = 0;
	guint removed_cnt = 0;
	FuDevice *device;
	g_autofree gchar *json3 = NULL;
	g_autoptr(FuBackend) backend = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device2 = NULL;
	g_autoptr(FuIoctl) ioctl = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *json1 = "{\n"
			     "  \"UsbDevices\": [\n"
			     "    {\n"
			     "      \"Created\": \"2023-02-01T16:35:03.302027Z\",\n"
			     "      \"GType\": \"FuUdevDevice\",\n"
			     "      \"BackendId\": \"foo:bar:baz\",\n"
			     "      \"Events\": [\n"
			     "        {\n"
			     "          \"Id\": \"Ioctl:Request=0x007b,Data=AAA=,Length=0x2\",\n"
			     "          \"Data\": \"Aw==\",\n"
			     "          \"DataOut\": \"Aw==\"\n"
			     "        },\n"
			     "        {\n"
			     "          \"Id\": \"Ioctl:Request=0x007b,Data=AAA=,Length=0x2\",\n"
			     "          \"Data\": \"Aw==\",\n"
			     "          \"DataOut\": \"Aw==\"\n"
			     "        }\n"
			     "      ]\n"
			     "    }\n"
			     "  ]\n"
			     "}";
	const gchar *json2 = "{\n"
			     "  \"FwupdVersion\": \"" PACKAGE_VERSION "\",\n"
			     "  \"UsbDevices\": [\n"
			     "    {\n"
			     "      \"Created\": \"2099-02-01T16:35:03Z\",\n"
			     "      \"GType\": \"FuUdevDevice\",\n"
			     "      \"BackendId\": \"usb:FF:FF:06\"\n"
			     "    }\n"
			     "  ]\n"
			     "}";

	/* watch events */
	backend = g_object_new(FU_TYPE_BACKEND,
			       "context",
			       ctx,
			       "name",
			       "udev",
			       "device-gtype",
			       FU_TYPE_UDEV_DEVICE,
			       NULL);
	g_signal_connect(FU_BACKEND(backend),
			 "device-added",
			 G_CALLBACK(fu_backend_emulate_count_cb),
			 &added_cnt);
	g_signal_connect(FU_BACKEND(backend),
			 "device-removed",
			 G_CALLBACK(fu_backend_emulate_count_cb),
			 &removed_cnt);
	g_signal_connect(FU_BACKEND(backend),
			 "device-changed",
			 G_CALLBACK(fu_backend_emulate_count_cb),
			 &changed_cnt);

	/* parse */
	ret = fwupd_codec_from_json_string(FWUPD_CODEC(backend), json1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(added_cnt, ==, 1);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 0);

	/* get device */
	device = fu_backend_lookup_by_id(backend, "foo:bar:baz");
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED));

#ifndef HAVE_IOCTL_H
	g_test_skip("no <ioctl.h> support");
	return;
#endif

	/* in-order */
	ioctl = fu_udev_device_ioctl_new(FU_UDEV_DEVICE(device));
	g_assert_nonnull(ioctl);
	ret = fu_ioctl_execute(ioctl, 123, buf, sizeof(buf), NULL, 0, FU_IOCTL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* in-order, repeat */
	buf[0] = 0x00;
	buf[1] = 0x00;
	ret = fu_ioctl_execute(ioctl, 123, buf, sizeof(buf), NULL, 0, FU_IOCTL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* out-of-order */
	buf[0] = 0x00;
	buf[1] = 0x00;
	ret = fu_ioctl_execute(ioctl, 123, buf, sizeof(buf), NULL, 0, FU_IOCTL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* load the same data */
	ret = fwupd_codec_from_json_string(FWUPD_CODEC(backend), json1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(added_cnt, ==, 1);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 1);
	device = fu_backend_lookup_by_id(backend, "foo:bar:baz");
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED));

	/* load a different device */
	ret = fwupd_codec_from_json_string(FWUPD_CODEC(backend), json2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(added_cnt, ==, 2);
	g_assert_cmpint(changed_cnt, ==, 1);
	g_assert_cmpint(removed_cnt, ==, 1);
	device = fu_backend_lookup_by_id(backend, "usb:FF:FF:06");
	g_assert_no_error(error);
	g_assert_nonnull(device);

	/* save to string */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_EMULATION_TAG);
	json3 = fwupd_codec_to_json_string(FWUPD_CODEC(backend), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json3);
	g_debug("%s", json3);
	ret = fu_test_compare_lines(json3, json2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* missing event, new path */
	fu_device_set_fwupd_version(device, PACKAGE_VERSION);
	device2 = fu_device_get_backend_parent_with_subsystem(device, "usb", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(device2);

	/* check version */
	g_assert_false(fu_device_check_fwupd_version(device, "5.0.0"));
	g_assert_true(fu_device_check_fwupd_version(device, "1.9.19"));
}

static void
fu_backend_func(void)
{
	FuDevice *dev;
	gboolean ret;
	g_autoptr(FuBackend) backend = g_object_new(FU_TYPE_BACKEND, NULL);
	g_autoptr(FuDevice) dev1 = fu_device_new(NULL);
	g_autoptr(FuDevice) dev2 = fu_device_new(NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	/* defaults */
	g_assert_null(fu_backend_get_name(backend));
	g_assert_true(fu_backend_get_enabled(backend));

	/* load */
	ret = fu_backend_setup(backend, FU_BACKEND_SETUP_FLAG_NONE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_backend_coldplug(backend, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add two devices, then remove one of them */
	fu_device_set_physical_id(dev1, "dev1");
	fu_backend_device_added(backend, dev1);
	fu_device_set_physical_id(dev2, "dev2");
	fu_backend_device_added(backend, dev2);
	fu_backend_device_changed(backend, dev2);
	fu_backend_device_removed(backend, dev2);

	dev = fu_backend_lookup_by_id(backend, "dev1");
	g_assert_nonnull(dev);
	g_assert_true(dev == dev1);

	/* should have been removed */
	dev = fu_backend_lookup_by_id(backend, "dev2");
	g_assert_null(dev);

	/* get linear array */
	devices = fu_backend_get_devices(backend);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	dev = g_ptr_array_index(devices, 0);
	g_assert_nonnull(dev);
	g_assert_true(dev == dev1);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/backend", fu_backend_func);
	g_test_add_func("/fwupd/backend/emulate", fu_backend_emulate_func);
	return g_test_run();
}
