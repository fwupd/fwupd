/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-udev-device-private.h"

static FuPciDevice *
fu_pci_device_test_new(FuBackend *backend, const gchar *class_str)
{
	FuPciDevice *device;
	FuContext *ctx = fu_backend_get_context(backend);
	g_autoptr(FuDeviceEvent) ev_class = NULL;
	g_autoptr(FuDeviceEvent) ev_class2 = NULL;
	g_autoptr(FuDeviceEvent) ev_device = NULL;
	g_autoptr(FuDeviceEvent) ev_revision = NULL;
	g_autoptr(FuDeviceEvent) ev_slot = NULL;
	g_autoptr(FuDeviceEvent) ev_subsystem_device = NULL;
	g_autoptr(FuDeviceEvent) ev_subsystem_vendor = NULL;
	g_autoptr(FuDeviceEvent) ev_vendor = NULL;

	device = g_object_new(FU_TYPE_PCI_DEVICE, "context", ctx, NULL);
	fu_device_add_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_EMULATED);
	fu_device_set_backend(FU_DEVICE(device), backend);
	fu_device_set_backend_id(FU_DEVICE(device), "/sys/devices/pci0000:00/0000:00:1f.0");
	fu_udev_device_set_subsystem(FU_UDEV_DEVICE(device), "pci");

	/* ReadAttr:Attr=vendor */
	ev_vendor = fu_device_event_new("ReadAttr:Attr=vendor");
	fu_device_event_set_str(ev_vendor, "Data", "0x8086");
	fu_device_add_event(FU_DEVICE(device), ev_vendor);

	/* ReadAttr:Attr=device */
	ev_device = fu_device_event_new("ReadAttr:Attr=device");
	fu_device_event_set_str(ev_device, "Data", "0x1234");
	fu_device_add_event(FU_DEVICE(device), ev_device);

	/* ReadAttr:Attr=class, read once by FuUdevDevice->probe and once here */
	ev_class = fu_device_event_new("ReadAttr:Attr=class");
	fu_device_event_set_str(ev_class, "Data", class_str);
	fu_device_add_event(FU_DEVICE(device), ev_class);
	ev_class2 = fu_device_event_new("ReadAttr:Attr=class");
	fu_device_event_set_str(ev_class2, "Data", class_str);
	fu_device_add_event(FU_DEVICE(device), ev_class2);

	/* ReadAttr:Attr=revision */
	ev_revision = fu_device_event_new("ReadAttr:Attr=revision");
	fu_device_event_set_str(ev_revision, "Data", "0x05");
	fu_device_add_event(FU_DEVICE(device), ev_revision);

	/* ReadAttr:Attr=subsystem_vendor */
	ev_subsystem_vendor = fu_device_event_new("ReadAttr:Attr=subsystem_vendor");
	fu_device_event_set_str(ev_subsystem_vendor, "Data", "0x17aa");
	fu_device_add_event(FU_DEVICE(device), ev_subsystem_vendor);

	/* ReadAttr:Attr=subsystem_device */
	ev_subsystem_device = fu_device_event_new("ReadAttr:Attr=subsystem_device");
	fu_device_event_set_str(ev_subsystem_device, "Data", "0x225e");
	fu_device_add_event(FU_DEVICE(device), ev_subsystem_device);

	/* ReadProp:Key=PCI_SLOT_NAME */
	ev_slot = fu_device_event_new("ReadProp:Key=PCI_SLOT_NAME");
	fu_device_event_set_str(ev_slot, "Data", "0000:00:1f.0");
	fu_device_add_event(FU_DEVICE(device), ev_slot);

	return device;
}

static void
fu_pci_device_test_ctx_load(FuContext *ctx)
{
	gboolean ret;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", "quirks.d", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_DATADIR_QUIRKS, testdatadir);
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static FuBackend *
fu_pci_device_test_backend_new(FuContext *ctx)
{
	return g_object_new(FU_TYPE_BACKEND,
			    "context",
			    ctx,
			    "name",
			    "udev",
			    "device-gtype",
			    FU_TYPE_UDEV_DEVICE,
			    NULL);
}

static void
fu_pci_device_probe_func(void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FuBackend) backend = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPciDevice) device = NULL;
	g_autoptr(GError) error = NULL;

	fu_pci_device_test_ctx_load(ctx);
	backend = fu_pci_device_test_backend_new(ctx);
	device = fu_pci_device_test_new(backend, "0x010802"); /* NVMe */

	ret = fu_device_probe(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* parsed from sysfs */
	g_assert_cmpint(fu_device_get_vid(FU_DEVICE(device)), ==, 0x8086);
	g_assert_cmpint(fu_device_get_pid(FU_DEVICE(device)), ==, 0x1234);
	g_assert_cmpint(fu_pci_device_get_revision(device), ==, 0x05);
	g_assert_cmpint(fu_pci_device_get_subsystem_vid(device), ==, 0x17aa);
	g_assert_cmpint(fu_pci_device_get_subsystem_pid(device), ==, 0x225e);
	g_assert_cmpstr(fu_device_get_physical_id(FU_DEVICE(device)),
			==,
			"PCI_SLOT_NAME=0000:00:1f.0");
	g_assert_cmpstr(fu_device_get_version(FU_DEVICE(device)), ==, "05");

	/* exercise the to_string vfunc */
	str = fu_device_to_string(FU_DEVICE(device));
	g_assert_nonnull(str);
}

static void
fu_pci_device_fallback_name_func(void)
{
	struct {
		const gchar *class_str;
		const gchar *name;
	} map[] = {
	    {"0x010000", "Mass Storage Device"},
	    {"0x020000", "Network Device"},
	    {"0x030000", "Display Device"},
	    {"0x040000", "Multimedia Device"},
	    {"0x050000", "Memory Device"},
	    {"0x060000", "Bridge Device"},
	    {"0x070000", "Simple Communication Device"},
	    {"0x080000", "Base Device"},
	    {"0x090000", "Input Device"},
	    {"0x0a0000", "Docking Device"},
	    {"0x0b0000", "Processor Device"},
	    {"0x0c0000", "Serial Bus Device"},
	    {"0x0d0000", "Wireless Device"},
	    {"0x0e0000", "Intelligent I/O Device"},
	    {"0x0f0000", "Satellite Device"},
	    {"0x100000", "Encryption Device"},
	    {"0x110000", "Signal Processing Device"},
	    {"0x120000", "Accelerator Device"},
	    {"0x130000", "Non-essential Device"},
	};
	g_autoptr(FuContext) ctx = fu_context_new();

	fu_pci_device_test_ctx_load(ctx);
	for (guint i = 0; i < G_N_ELEMENTS(map); i++) {
		gboolean ret;
		g_autoptr(FuBackend) backend = fu_pci_device_test_backend_new(ctx);
		g_autoptr(FuPciDevice) device = fu_pci_device_test_new(backend, map[i].class_str);
		g_autoptr(GError) error = NULL;

		ret = fu_device_probe(FU_DEVICE(device), &error);
		g_assert_no_error(error);
		g_assert_true(ret);
		fu_device_probe_complete(FU_DEVICE(device));
		g_assert_cmpstr(fu_device_get_name(FU_DEVICE(device)), ==, map[i].name);
	}
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/pci-device/probe", fu_pci_device_probe_func);
	g_test_add_func("/fwupd/pci-device/fallback-name", fu_pci_device_fallback_name_func);
	return g_test_run();
}
