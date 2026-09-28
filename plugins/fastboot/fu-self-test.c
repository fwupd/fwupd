/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-fastboot-rolling-device.h"

static void
fu_fastboot_rolling_device_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device =
	    g_object_new(FU_TYPE_FASTBOOT_ROLLING_DEVICE, "context", ctx, NULL);
	g_assert_true(fu_device_has_protocol(device, "com.google.fastboot"));
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER));
	g_assert_true(fu_device_get_firmware_gtype(device) == FU_TYPE_ZIP_FIRMWARE);
}

static void
fu_test_fastboot_rolling_add_reboot_events(FuFastbootRollingDevice *self)
{
	FuDevice *device = FU_DEVICE(self);
	guint8 rbuf[64] = {0x0};
	g_autofree gchar *data_out = fu_base64_encode((const guint8 *)"reboot", 6);
	g_autofree gchar *data_in = fu_base64_encode(rbuf, sizeof(rbuf));
	g_autofree gchar *event_id_out = NULL;
	g_autofree gchar *event_id_in = NULL;
	g_autoptr(FuDeviceEvent) event1 = NULL;
	g_autoptr(FuDeviceEvent) event2 = NULL;

	/* the OUT event key is built from the outgoing "reboot" buffer; the
	 * recorded Data just needs to be six bytes so the write reports the full
	 * length was transferred */
	event_id_out = g_strdup_printf("BulkTransfer:Endpoint=0x01,Data=%s,Length=0x6", data_out);
	event1 = fu_device_event_new(event_id_out);
	fu_device_event_set_data(event1, "Data", (const guint8 *)"reboot", 6);
	fu_device_add_event(device, event1);

	/* the IN event key is built from the zeroed read buffer; the recorded
	 * "OKAY" is copied back so the fastboot read parses it as success */
	event_id_in = g_strdup_printf("BulkTransfer:Endpoint=0x81,Data=%s,Length=0x40", data_in);
	event2 = fu_device_event_new(event_id_in);
	fu_device_event_set_data(event2, "Data", (const guint8 *)"OKAY", 4);
	fu_device_add_event(device, event2);
}

/* the parent "reboot" succeeds so the rolling attach() enters the rebind, but
 * with no sysfs path the rebind fails; attach() must swallow that failure with
 * a warning and still return success */
static void
fu_fastboot_rolling_device_attach_rebind_fail_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device =
	    g_object_new(FU_TYPE_FASTBOOT_ROLLING_DEVICE, "context", ctx, NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_EMULATED);
	fu_test_fastboot_rolling_add_reboot_events(FU_FASTBOOT_ROLLING_DEVICE(device));

	g_test_expect_message("FuPluginFastboot",
			      G_LOG_LEVEL_WARNING,
			      "failed to rebind cdc_mbim: *");
	ret = fu_device_attach_full(device, progress, &error);
	g_test_assert_expected_messages();
	g_assert_no_error(error);
	g_assert_true(ret);
}

/* the parent "reboot" succeeds, the interface directory is
 * present, and the bind file is writable, so the cdc_mbim rebind completes and
 * attach() returns success without a warning */
static void
fu_fastboot_rolling_device_attach_rebind_func(void)
{
	gboolean ret;
	g_autofree gchar *sysfs_path = NULL;
	g_autofree gchar *iface_path = NULL;
	g_autofree gchar *sysfsdir = NULL;
	g_autofree gchar *bind_path = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device =
	    g_object_new(FU_TYPE_FASTBOOT_ROLLING_DEVICE, "context", ctx, NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;

	tmpdir = fu_temporary_directory_new("fastboot-rolling", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	/* the device sysfs path is taken from the backend ID; create the
	 * "<bind_id>:1.0" interface directory the rebind waits for */
	sysfs_path = fu_temporary_directory_build(tmpdir, "sys", NULL);
	iface_path = g_build_filename(sysfs_path, "1-1:1.0", NULL);
	ret = fu_path_mkdir(iface_path, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_device_set_backend_id(device, sysfs_path);
	fu_udev_device_set_bind_id(FU_UDEV_DEVICE(device), "1-1");

	/* the rebind writes to <sysfsdir>/bus/usb/drivers/cdc_mbim/bind */
	sysfsdir = fu_temporary_directory_build(tmpdir, "sysfs", NULL);
	bind_path = g_build_filename(sysfsdir, "bus", "usb", "drivers", "cdc_mbim", "bind", NULL);
	ret = fu_path_mkdir_parent(bind_path, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(g_file_set_contents(bind_path, "", 0, &error));
	g_assert_no_error(error);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR, sysfsdir);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_EMULATED);
	fu_test_fastboot_rolling_add_reboot_events(FU_FASTBOOT_ROLLING_DEVICE(device));

	ret = fu_device_attach_full(device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fastboot/rolling-device", fu_fastboot_rolling_device_func);
	g_test_add_func("/fastboot/rolling-device/attach{rebind-fail}",
			fu_fastboot_rolling_device_attach_rebind_fail_func);
	g_test_add_func("/fastboot/rolling-device/attach{rebind}",
			fu_fastboot_rolling_device_attach_rebind_func);
	return g_test_run();
}
