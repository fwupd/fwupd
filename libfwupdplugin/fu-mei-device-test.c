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

static FuContext *
fu_mei_device_test_ctx_new(void)
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
fu_mei_device_fw_ver_func(void)
{
	g_autofree gchar *fw_status = NULL;
	g_autofree gchar *fw_ver0 = NULL;
	g_autofree gchar *fw_ver1 = NULL;
	g_autofree gchar *fw_ver_bad = NULL;
	g_autoptr(FuContext) ctx = fu_mei_device_test_ctx_new();
	g_autoptr(FuDevice) dev = g_object_new(FU_TYPE_MEI_DEVICE, "context", ctx, NULL);
	g_autoptr(FuDeviceEvent) ev_status = fu_device_event_new("ReadAttr:Attr=fw_status");
	g_autoptr(GError) error = NULL;
	const gchar *ver_data = "11.0.0.1\n12.0.0.2";
	const gchar *status_data = "0x1234\n0x5678";

	fu_device_add_flag(dev, FWUPD_DEVICE_FLAG_EMULATED);

	/* each get_fw_ver()/get_fw_status() re-reads the whole attr, so add one
	 * event per call */
	for (guint i = 0; i < 3; i++) {
		g_autoptr(FuDeviceEvent) ev_ver = fu_device_event_new("ReadAttr:Attr=fw_ver");
		fu_device_event_set_data(ev_ver,
					 "Data",
					 (const guint8 *)ver_data,
					 strlen(ver_data));
		fu_device_add_event(dev, ev_ver);
	}
	fu_device_event_set_data(ev_status,
				 "Data",
				 (const guint8 *)status_data,
				 strlen(status_data));
	fu_device_add_event(dev, ev_status);

	fw_ver0 = fu_mei_device_get_fw_ver(FU_MEI_DEVICE(dev), 0, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(fw_ver0, ==, "11.0.0.1");
	fw_ver1 = fu_mei_device_get_fw_ver(FU_MEI_DEVICE(dev), 1, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(fw_ver1, ==, "12.0.0.2");

	/* an out-of-range index is an error */
	fw_ver_bad = fu_mei_device_get_fw_ver(FU_MEI_DEVICE(dev), 5, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null(fw_ver_bad);
	g_clear_error(&error);

	fw_status = fu_mei_device_get_fw_status(FU_MEI_DEVICE(dev), 0, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(fw_status, ==, "0x1234");
}

static void
fu_mei_device_getters_func(void)
{
	g_autoptr(FuContext) ctx = fu_mei_device_test_ctx_new();
	g_autoptr(FuDevice) dev = g_object_new(FU_TYPE_MEI_DEVICE, "context", ctx, NULL);

	/* unset defaults */
	g_assert_cmpint(fu_mei_device_get_max_msg_length(FU_MEI_DEVICE(dev)), ==, 0);
	g_assert_cmpint(fu_mei_device_get_protocol_version(FU_MEI_DEVICE(dev)), ==, 0);
}

static void
fu_mei_device_read_write_func(void)
{
	gboolean ret;
	gsize bytes_read = 0;
	guint8 buf[4] = {0};
	g_autoptr(FuContext) ctx = fu_mei_device_test_ctx_new();
	g_autoptr(FuDevice) dev = g_object_new(FU_TYPE_MEI_DEVICE, "context", ctx, NULL);
	g_autoptr(FuDeviceEvent) ev_read = NULL;
	g_autoptr(FuDeviceEvent) ev_write = NULL;
	g_autoptr(GError) error = NULL;
	const guint8 rddata[] = {0x11, 0x22, 0x33, 0x44};
	const guint8 wrdata[] = {0xAB, 0xCD};

	fu_device_add_flag(dev, FWUPD_DEVICE_FLAG_EMULATED);
	fu_device_set_fwupd_version(dev, PACKAGE_VERSION);

	/* a single-shot read */
	ev_read = fu_device_event_new("Read:Length=0x4,Offset=0x0");
	fu_device_event_set_data(ev_read, "Data", rddata, sizeof(rddata));
	fu_device_add_event(dev, ev_read);
	ret = fu_mei_device_read(FU_MEI_DEVICE(dev), buf, sizeof(buf), &bytes_read, 100, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(bytes_read, ==, 4);
	g_assert_cmpmem(buf, bytes_read, rddata, sizeof(rddata));

	/* a write, keyed by its base64-encoded data */
	ev_write = fu_device_event_new("Write:Data=q80=,Length=0x2");
	fu_device_add_event(dev, ev_write);
	ret = fu_mei_device_write(FU_MEI_DEVICE(dev), wrdata, sizeof(wrdata), 100, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_mei_device_probe_func(void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FuContext) ctx = fu_mei_device_test_ctx_new();
	g_autoptr(FuBackend) backend = NULL;
	g_autoptr(FuDevice) dev = g_object_new(FU_TYPE_MEI_DEVICE, "context", ctx, NULL);
	g_autoptr(FuDeviceEvent) ev_list = NULL;
	g_autoptr(FuDeviceEvent) ev_parent = NULL;
	g_autoptr(FuDeviceEvent) ev_pci = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *list_data = "mei0:aa-bb\nmei0:cc-dd\nunrelated";

	backend = g_object_new(FU_TYPE_BACKEND,
			       "context",
			       ctx,
			       "name",
			       "udev",
			       "device-gtype",
			       FU_TYPE_UDEV_DEVICE,
			       NULL);

	fu_device_add_flag(dev, FWUPD_DEVICE_FLAG_EMULATED);
	fu_device_set_fwupd_version(dev, PACKAGE_VERSION);
	fu_device_set_backend(dev, backend);
	fu_device_set_backend_id(dev, "/sys/devices/pci0000:00/0000:00:16.0/mei0");
	fu_udev_device_set_subsystem(FU_UDEV_DEVICE(dev), "mei");
	fu_udev_device_set_device_file(FU_UDEV_DEVICE(dev), "/dev/mei0");

	/* GetBackendParent:Subsystem=pci -> a PCI donor to copy the vendor from;
	 * a plain FuDevice has a trivial probe and the VID/PID come from the event */
	ev_pci = fu_device_event_new("GetBackendParent:Subsystem=pci");
	fu_device_event_set_str(ev_pci, "GType", "FuDevice");
	fu_device_event_set_str(ev_pci, "BackendId", "/sys/devices/pci0000:00/0000:00:16.0");
	fu_device_event_set_i64(ev_pci, "Vid", 0x8086);
	fu_device_event_set_i64(ev_pci, "Pid", 0x1234);
	fu_device_add_event(dev, ev_pci);

	/* GetBackendParent (no subsystem) -> the immediate parent for interfaces */
	ev_parent = fu_device_event_new("GetBackendParent:Subsystem=(null)");
	fu_device_event_set_str(ev_parent, "GType", "FuUdevDevice");
	fu_device_event_set_str(ev_parent,
				"BackendId",
				"/sys/devices/pci0000:00/0000:00:16.0/mei0");
	fu_device_add_event(dev, ev_parent);

	/* ListAttr -> the interfaces exposed by the parent */
	ev_list = fu_device_event_new("ListAttr");
	fu_device_event_set_str(ev_list, "Data", list_data);
	fu_device_add_event(dev, ev_list);

	ret = fu_device_probe(dev, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* the vendor was copied from the PCI donor */
	g_assert_cmpint(fu_device_get_vid(dev), ==, 0x8086);

	/* the matching interfaces became instance IDs */
	g_assert_true(fu_device_has_instance_id(dev, "aa-bb", FU_DEVICE_INSTANCE_FLAG_QUIRKS));
	g_assert_true(fu_device_has_instance_id(dev, "cc-dd", FU_DEVICE_INSTANCE_FLAG_QUIRKS));

	str = fu_device_to_string(dev);
	g_assert_nonnull(str);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/mei-device/getters", fu_mei_device_getters_func);
	g_test_add_func("/fwupd/mei-device/fw-ver", fu_mei_device_fw_ver_func);
	g_test_add_func("/fwupd/mei-device/read-write", fu_mei_device_read_write_func);
	g_test_add_func("/fwupd/mei-device/probe", fu_mei_device_probe_func);
	return g_test_run();
}
