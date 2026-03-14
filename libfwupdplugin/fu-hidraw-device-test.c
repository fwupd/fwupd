/*
 * Copyright 2026 Daniel Schaefer <git@danielschaefer.me>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-udev-device-private.h"

static FuHidrawDevice *
fu_hidraw_device_test_new(FuBackend *backend,
			  const gchar *hid_phys)
{
	FuHidrawDevice *device;
	FuContext *ctx = fu_backend_get_context(backend);
	g_autoptr(FuDeviceEvent) ev_parent = NULL;
	g_autoptr(FuDeviceEvent) ev_hid_id = NULL;
	g_autoptr(FuDeviceEvent) ev_hid_phys = NULL;

	device = g_object_new(FU_TYPE_HIDRAW_DEVICE, "context", ctx, NULL);
	fu_device_add_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_EMULATED);
	fu_device_set_fwupd_version(FU_DEVICE(device), PACKAGE_VERSION);
	fu_device_set_backend(FU_DEVICE(device), backend);
	fu_device_set_backend_id(FU_DEVICE(device), "/sys/devices/pci/foo1/0003:093A:0274.0001/hidraw/hidraw0");
	fu_udev_device_set_subsystem(FU_UDEV_DEVICE(device), "hidraw");
	fu_udev_device_set_device_file(FU_UDEV_DEVICE(device), "/dev/hidraw0");

	/* GetBackendParent:Subsystem=hid */
	ev_parent = fu_device_event_new("GetBackendParent:Subsystem=hid");
	fu_device_event_set_str(ev_parent, "GType", "FuUdevDevice");
	fu_device_event_set_str(ev_parent, "BackendId", "/sys/devices/pci/foo1/0003:093A:0274.0001");
	fu_device_add_event(FU_DEVICE(device), ev_parent);

	/* ReadProp:Key=HID_ID */
	ev_hid_id = fu_device_event_new("ReadProp:Key=HID_ID");
	fu_device_event_set_str(ev_hid_id, "Data", "0003:0000093A:00000274");
	fu_device_add_event(FU_DEVICE(device), ev_hid_id);

	/* ReadProp:Key=HID_PHYS */
	ev_hid_phys = fu_device_event_new("ReadProp:Key=HID_PHYS");
	fu_device_event_set_str(ev_hid_phys, "Data", hid_phys);
	fu_device_add_event(FU_DEVICE(device), ev_hid_phys);

	return device;
}

/*
 * Test that when HID_PHYS is present and non-empty, it is used as the physical ID.
 */
static void
fu_hidraw_device_probe_hid_phys_func(void)
{
	gboolean ret;
	g_autoptr(FuBackend) backend = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuHidrawDevice) device = NULL;
	g_autoptr(GError) error = NULL;

	/* load quirks */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	backend = g_object_new(FU_TYPE_BACKEND,
			       "context",
			       ctx,
			       "name",
			       "udev",
			       "device-gtype",
			       FU_TYPE_UDEV_DEVICE,
			       NULL);
	device = fu_hidraw_device_test_new(backend, "i2c-HXTP0001:00");

	ret = fu_device_probe(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* physical ID should be the HID_PHYS value */
	g_assert_cmpstr(fu_device_get_physical_id(FU_DEVICE(device)),
			==,
			"i2c-HXTP0001:00");
}

/*
 * Test that when HID_PHYS is empty, the probe falls back to the HID
 * parent's sysfs path as the physical ID.
 *
 * Without this fallback, all devices with empty HID_PHYS would get
 * SHA1("") = da39a3ee... as their device ID, causing collisions.
 */
static void
fu_hidraw_device_probe_hid_phys_empty_func(void)
{
	gboolean ret;
	g_autoptr(FuBackend) backend = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuHidrawDevice) device = NULL;
	g_autoptr(GError) error = NULL;

	/* load quirks */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	backend = g_object_new(FU_TYPE_BACKEND,
			       "context",
			       ctx,
			       "name",
			       "udev",
			       "device-gtype",
			       FU_TYPE_UDEV_DEVICE,
			       NULL);

	/* hidraw device, with empty HID_PHYS */
	device = fu_hidraw_device_test_new(backend, "");

	/* probe */
	ret = fu_device_probe(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* should have fallen back to the HID parent sysfs path */
	g_assert_cmpstr(fu_device_get_physical_id(FU_DEVICE(device)),
			==,
			"/sys/devices/pci/foo1/0003:093A:0274.0001");

	/* verify VID/PID were correctly parsed from sysfs or HID_ID */
	g_assert_cmpint(fu_device_get_vid(FU_DEVICE(device)), ==, 0x093A);
	g_assert_cmpint(fu_device_get_pid(FU_DEVICE(device)), ==, 0x0274);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/hidraw-device{probe-hid-phys}",
			fu_hidraw_device_probe_hid_phys_func);
	g_test_add_func("/fwupd/hidraw-device{probe-hid-phys-empty}",
			fu_hidraw_device_probe_hid_phys_empty_func);
	return g_test_run();
}
