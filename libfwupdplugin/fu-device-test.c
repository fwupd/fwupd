/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-device-progress.h"
#include "fu-self-test-device.h"
#include "fu-test.h"
#include "fu-udev-device-private.h"

static void
fu_device_version_format_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_ENSURE_SEMVER);
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	/* nocheck:set-version */
	fu_device_set_version(device, "Ver1.2.3 RELEASE");
	g_assert_cmpstr(fu_device_get_version(device), ==, "1.2.3");
}

static void
fu_device_version_format_raw_func(void)
{
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_USB_DEVICE, NULL);

	/* like normal */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_BCD);
	fu_device_set_version_raw(device, 256);
	fu_device_set_version_lowest_raw(device, 257);

	g_assert_cmpstr(fu_device_get_version(device), ==, "1.0");
	g_assert_cmpstr(fu_device_get_version_lowest(device), ==, "1.1");

	/* ensure both are changed */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PLAIN);
	g_assert_cmpstr(fu_device_get_version(device), ==, "256");
	g_assert_cmpstr(fu_device_get_version_lowest(device), ==, "257");
}

static void
fu_device_open_refcount_func(void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(GError) error = NULL;
	fu_device_set_id(device, "test_device");
	ret = fu_device_open(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_open(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_close(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_close(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_close(device, &error);
	g_assert_true(ret);
}

static void
fu_device_rescan_func(void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(GError) error = NULL;

	/* no GUIDs! */
	ret = fu_device_rescan(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_device_name_func(void)
{
	g_autoptr(FuDevice) device1 = fu_device_new(NULL);
	g_autoptr(FuDevice) device2 = fu_device_new(NULL);

	/* vendor then name */
	fu_device_set_vendor(device1, "  Hughski  ");
	fu_device_set_name(device1, "HUGHSKI  ColorHug(TM)__Pro  ");
	g_assert_cmpstr(fu_device_get_vendor(device1), ==, "Hughski");
	g_assert_cmpstr(fu_device_get_name(device1), ==, "ColorHug™ Pro");

	/* name then vendor */
	fu_device_set_name(device2, "Hughski ColorHug(TM)_Pro");
	fu_device_set_vendor(device2, "Hughski");
	g_assert_cmpstr(fu_device_get_vendor(device2), ==, "Hughski");
	g_assert_cmpstr(fu_device_get_name(device2), ==, "ColorHug™ Pro");

	/* a real example */
	fu_device_set_name(device2, "Intel(R) Core(TM) i7-10850H CPU @ 2.70GHz");
	fu_device_set_vendor(device2, "Intel");
	g_assert_cmpstr(fu_device_get_name(device2), ==, "Core™ i7-10850H CPU @ 2.70GHz");

	/* name and vendor are the same */
#ifndef SUPPORTED_BUILD
	g_test_expect_message("FuDevice", G_LOG_LEVEL_WARNING, "name and vendor are the same*");
#endif
	fu_device_set_name(device2, "example");
	fu_device_set_vendor(device2, "EXAMPLE");
	g_assert_cmpstr(fu_device_get_name(device2), ==, "example");
	g_assert_cmpstr(fu_device_get_vendor(device2), ==, "EXAMPLE");
}

static void
fu_device_cfi_device_func(void)
{
	gboolean ret;
	guint8 cmd = 0;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuCfiDevice) cfi_device = NULL;
	g_autoptr(GError) error = NULL;

	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	cfi_device = fu_cfi_device_new(ctx, "3730");
	ret = fu_device_setup(FU_DEVICE(cfi_device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* fallback */
	ret = fu_cfi_device_get_cmd(cfi_device, FU_CFI_DEVICE_CMD_READ_DATA, &cmd, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(cmd, ==, 0x03);

	/* from quirk */
	ret = fu_cfi_device_get_cmd(cfi_device, FU_CFI_DEVICE_CMD_CHIP_ERASE, &cmd, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(cmd, ==, 0xC7);
	g_assert_cmpint(fu_cfi_device_get_size(cfi_device), ==, 0x10000);
	g_assert_cmpint(fu_cfi_device_get_page_size(cfi_device), ==, 0x200);
	g_assert_cmpint(fu_cfi_device_get_sector_size(cfi_device), ==, 0x2000);
	g_assert_cmpint(fu_cfi_device_get_block_size(cfi_device), ==, 0x8000);
}

static void
fu_device_metadata_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);

	/* string */
	fu_device_set_metadata(device, "foo", "bar");
	g_assert_cmpstr(fu_device_get_metadata(device, "foo"), ==, "bar");
	fu_device_set_metadata(device, "foo", "baz");
	g_assert_cmpstr(fu_device_get_metadata(device, "foo"), ==, "baz");
	g_assert_null(fu_device_get_metadata(device, "unknown"));

	/* boolean */
	fu_device_set_metadata_boolean(device, "baz", TRUE);
	g_assert_cmpstr(fu_device_get_metadata(device, "baz"), ==, "true");
	g_assert_true(fu_device_get_metadata_boolean(device, "baz"));
	g_assert_false(fu_device_get_metadata_boolean(device, "unknown"));

	/* integer */
	fu_device_set_metadata_integer(device, "bam", 12345);
	g_assert_cmpstr(fu_device_get_metadata(device, "bam"), ==, "12345");
	g_assert_cmpint(fu_device_get_metadata_integer(device, "bam"), ==, 12345);
	g_assert_cmpint(fu_device_get_metadata_integer(device, "unknown"), ==, G_MAXUINT);
}

static void
fu_device_strsafe_func(void)
{
	struct {
		const gchar *in;
		const gchar *op;
	} strs[] = {{"dave123", "dave123"},
		    {"dave123XXX", "dave123"},
		    {"dave\x03XXX", "dave.XX"},
		    {"dave\x03\x04XXX", "dave..X"},
		    {"\x03\x03", NULL},
		    {NULL, NULL}};
	GPtrArray *instance_ids;
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) dev = fu_device_new(ctx);
	g_autoptr(GError) error = NULL;

	/* check bespoke legacy instance ID behavior */
	fu_device_add_instance_strsafe(dev, "KEY", "_ _LEN&VO&\\&");
	ret = fu_device_build_instance_id(dev, &error, "SUB", "KEY", NULL);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_device_convert_instance_ids(dev);
	instance_ids = fu_device_get_instance_ids(dev);
	g_assert_cmpint(instance_ids->len, ==, 1);
	g_assert_cmpstr(g_ptr_array_index(instance_ids, 0), ==, "SUB\\KEY_LEN-VO");

	for (guint i = 0; strs[i].in != NULL; i++) {
		g_autofree gchar *tmp = fu_strsafe(strs[i].in, 7);
		g_assert_cmpstr(tmp, ==, strs[i].op);
	}
}

static void
fu_device_progress_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuDeviceProgress) device_progress = fu_device_progress_new(device, progress);

	/* proxy */
	fu_progress_set_percentage(progress, 50);
	fu_progress_set_status(progress, FWUPD_STATUS_SHUTDOWN);
	g_assert_cmpint(fu_device_get_percentage(device), ==, 50);
	g_assert_cmpint(fu_device_get_status(device), ==, FWUPD_STATUS_SHUTDOWN);

	/* clear */
	g_clear_object(&device_progress);
	g_assert_cmpint(fu_device_get_percentage(device), ==, 0);
	g_assert_cmpint(fu_device_get_status(device), ==, FWUPD_STATUS_IDLE);

	/* do not proxy */
	fu_progress_set_percentage(progress, 100);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_BUSY);
	g_assert_cmpint(fu_device_get_percentage(device), ==, 0);
	g_assert_cmpint(fu_device_get_status(device), ==, FWUPD_STATUS_IDLE);
}

static gboolean
fu_device_poll_cb(FuDevice *device, GError **error)
{
	guint64 cnt = fu_device_get_metadata_integer(device, "cnt");
	g_debug("poll cnt=%" G_GUINT64_FORMAT, cnt);
	fu_device_set_metadata_integer(device, "cnt", cnt + 1);
	return TRUE;
}

static void
fu_device_poll_func(void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error = NULL;
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS(device);
	guint cnt;

	klass->poll = fu_device_poll_cb;
	fu_device_set_metadata_integer(device, "cnt", 0);

	/* manual poll */
	ret = fu_device_poll(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	cnt = fu_device_get_metadata_integer(device, "cnt");
	g_assert_cmpint(cnt, ==, 1);

	/* set up a 10ms poll */
	fu_device_set_poll_interval(device, 5);
	fu_test_loop_run_with_timeout(50);
	fu_test_loop_quit();
	cnt = fu_device_get_metadata_integer(device, "cnt");
	g_assert_cmpint(cnt, >=, 5);
	fu_test_loop_quit();

	/* auto pause */
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_AUTO_PAUSE_POLLING);
	locker = fu_device_poll_locker_new(device, &error);
	g_assert_no_error(error);
	g_assert_nonnull(locker);
	fu_test_loop_run_with_timeout(25);
	g_clear_object(&locker);
	g_assert_cmpint(fu_device_get_metadata_integer(device, "cnt"), ==, cnt);
	fu_test_loop_quit();

	/* disable the poll manually */
	fu_device_set_poll_interval(device, 0);
	fu_test_loop_run_with_timeout(25);
	fu_test_loop_quit();
	g_assert_cmpint(fu_device_get_metadata_integer(device, "cnt"), ==, cnt);
	fu_test_loop_quit();
}

static void
fu_device_func(void)
{
	g_autofree gchar *fn = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) possible_plugins = NULL;

	/* only add one plugin name of the same type */
	fu_device_add_possible_plugin(device, "test");
	fu_device_add_possible_plugin(device, "test");
	possible_plugins = fu_device_get_possible_plugins(device);
	g_assert_cmpint(possible_plugins->len, ==, 1);

	fn = g_test_build_filename(G_TEST_DIST, "tests", "sys_vendor", NULL);
	str = fu_device_get_contents(device, fn, G_MAXSIZE, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(str, ==, "FwupdTest\n");

	blob = fu_device_get_contents_bytes(device, fn, 5, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(g_bytes_get_size(blob), ==, 5);
}

static void
fu_device_vfuncs_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuFirmware) firmware_dummy = fu_firmware_new();
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	/* nop: error */
	ret = fu_device_get_results(device, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);

	ret = fu_device_write_firmware(device,
				       firmware_dummy,
				       progress,
				       FWUPD_INSTALL_FLAG_NONE,
				       &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);

	firmware = fu_device_read_firmware(device, progress, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_null(firmware);
	g_clear_error(&error);

	blob = fu_device_dump_firmware(device, progress, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_null(blob);
	g_clear_error(&error);

	ret = fu_device_unbind_driver(device, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);
	ret = fu_device_bind_driver(device, "subsystem", "driver", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);

	/* nop: ignore */
	ret = fu_device_detach(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_attach(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_activate(device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* no-probe */
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_NO_PROBE);
	ret = fu_device_probe(device, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);
}

static void
fu_device_instance_ids_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(GError) error = NULL;

	/* do not save silo */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* sanity check */
	g_assert_false(fu_device_has_guid(device, "c0a26214-223b-572a-9477-cde897fe8619"));

	/* add a deferred instance ID that only gets converted on ->setup */
	fu_device_add_instance_id(device, "foobarbaz");
	g_assert_false(fu_device_has_guid(device, "c0a26214-223b-572a-9477-cde897fe8619"));

	ret = fu_device_setup(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_device_has_guid(device, "c0a26214-223b-572a-9477-cde897fe8619"));

	/* this gets added immediately */
	fu_device_add_instance_id(device, "bazbarfoo");
	g_assert_true(fu_device_has_guid(device, "77e49bb0-2cd6-5faf-bcee-5b7fbe6e944d"));
}

static void
fu_device_composite_id_func(void)
{
	g_autoptr(FuDevice) dev1 = fu_device_new(NULL);
	g_autoptr(FuDevice) dev2 = fu_device_new(NULL);
	g_autoptr(FuDevice) dev3 = fu_device_new(NULL);
	g_autoptr(FuDevice) dev4 = fu_device_new(NULL);

	/* single device */
	fu_device_set_id(dev1, "dev1");
	g_assert_cmpstr(fu_device_get_composite_id(dev1),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");
	fu_device_set_id(dev2, "dev2");

	/* one child */
	fu_device_add_child(dev1, dev2);
	g_assert_cmpstr(fu_device_get_composite_id(dev1),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");
	g_assert_cmpstr(fu_device_get_composite_id(dev2),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");

	/* add a different "family" */
	fu_device_set_id(dev3, "dev3");
	fu_device_set_id(dev4, "dev4");
	fu_device_add_child(dev3, dev4);
	fu_device_add_child(dev2, dev3);
	g_assert_cmpstr(fu_device_get_composite_id(dev1),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");
	g_assert_cmpstr(fu_device_get_composite_id(dev2),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");
	g_assert_cmpstr(fu_device_get_composite_id(dev3),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");
	g_assert_cmpstr(fu_device_get_composite_id(dev4),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");

	/* change the parent ID */
	fu_device_set_id(dev1, "dev1-NEW");
	g_assert_cmpstr(fu_device_get_composite_id(dev1),
			==,
			"a4c8efc6a0a58c2dc14c05fd33186703f7352997");
	g_assert_cmpstr(fu_device_get_composite_id(dev2),
			==,
			"a4c8efc6a0a58c2dc14c05fd33186703f7352997");
}

static void
fu_device_inhibit_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_battery_threshold(device, 25);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));

	/* does not exist -> fine */
	fu_device_uninhibit(device, "NOTGOINGTOEXIST");
	g_assert_false(fu_device_has_inhibit(device, "NOTGOINGTOEXIST"));

	/* first one */
	fu_device_inhibit(device, "needs-activation", "Device is pending activation");
	g_assert_true(fu_device_has_inhibit(device, "needs-activation"));
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* another */
	fu_device_set_battery_level(device, 5);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* activated, power still too low */
	fu_device_uninhibit(device, "needs-activation");
	g_assert_false(fu_device_has_inhibit(device, "needs-activation"));
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* we got some more power -> fine */
	fu_device_set_battery_level(device, 95);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
}

static void
fu_device_inhibit_updateable_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);

	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
	g_assert_cmpstr(fu_device_get_update_error(device), ==, NULL);

	/* first one */
	fu_device_inhibit(device, "needs-activation", "Device is pending activation");
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_cmpstr(fu_device_get_update_error(device), ==, "Device is pending activation");

	/* activated, but still not updatable */
	fu_device_uninhibit(device, "needs-activation");
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
	g_assert_cmpstr(fu_device_get_update_error(device), ==, NULL);
}

static void
fu_device_custom_flags_func(void)
{
	g_autofree gchar *tmp = NULL;
	g_autoptr(FuDevice) device = fu_device_new(NULL);

	fu_device_register_private_flag(device, "foo");
	fu_device_register_private_flag(device, "bar");

	fu_device_set_custom_flags(device, "foo");
	g_assert_true(fu_device_has_private_flag(device, "foo"));
	fu_device_set_custom_flags(device, "bar");
	g_assert_true(fu_device_has_private_flag(device, "foo"));
	g_assert_true(fu_device_has_private_flag(device, "bar"));
	fu_device_set_custom_flags(device, "~bar");
	g_assert_true(fu_device_has_private_flag(device, "foo"));
	g_assert_false(fu_device_has_private_flag(device, "bar"));
	fu_device_set_custom_flags(device, "baz");
	g_assert_true(fu_device_has_private_flag(device, "foo"));
	g_assert_false(fu_device_has_private_flag(device, "bar"));

	tmp = fu_device_to_string(device);
	g_assert_cmpstr(tmp,
			==,
			"FuDevice:\n"
			"  Flags:                none\n"
			"  AcquiesceDelay:       50\n"
			"  CustomFlags:          baz\n"
			"  PrivateFlags:         foo\n");
}

static void
fu_device_flags_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(FuDevice) proxy = fu_device_new(NULL);

	g_assert_cmpint(fu_device_get_flags(device), ==, FWUPD_DEVICE_FLAG_NONE);

	/* remove IS_BOOTLOADER if is a BOOTLOADER */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);
	g_assert_cmpint(fu_device_get_flags(device), ==, FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);
	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);

	/* check implication */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	g_assert_cmpint(fu_device_get_flags(device),
			==,
			FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE | FWUPD_DEVICE_FLAG_CAN_VERIFY);
	fu_device_remove_flag(device,
			      FWUPD_DEVICE_FLAG_CAN_VERIFY | FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);

	/* negation */
	fu_device_set_custom_flags(device, "is-bootloader,updatable");
	g_assert_cmpint(fu_device_get_flags(device),
			==,
			FWUPD_DEVICE_FLAG_IS_BOOTLOADER | FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_custom_flags(device, "~is-bootloader");
	g_assert_cmpint(fu_device_get_flags(device), ==, FWUPD_DEVICE_FLAG_UPDATABLE);

	/* setting flags on the proxy should propagate to the device that *uses* the proxy */
	fu_device_set_proxy(device, proxy);
	fu_device_add_flag(proxy, FWUPD_DEVICE_FLAG_EMULATED);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED));

	/* unsetting flags on the proxy should unpropagate to the device that *uses* the proxy */
	fu_device_remove_flag(proxy, FWUPD_DEVICE_FLAG_EMULATED);
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED));
}

static void
fu_device_children_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) child = fu_device_new(NULL);
	g_autoptr(FuDevice) parent = fu_device_new(ctx);
	g_autoptr(GError) error = NULL;

	/* do not save silo */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_device_set_physical_id(child, "dummy");
	fu_device_set_physical_id(parent, "dummy");

	/* set up family */
	fu_device_add_child(parent, child);

	/* set an instance ID that will be converted to a GUID when the parent
	 * calls ->setup */
	fu_device_add_instance_id(child, "foo");
	g_assert_false(fu_device_has_guid(child, "b84ed8ed-a7b1-502f-83f6-90132e68adef"));

	/* setup parent, which also calls setup on child too (and thus also
	 * converts the instance ID to a GUID) */
	ret = fu_device_setup(parent, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_device_has_guid(child, "b84ed8ed-a7b1-502f-83f6-90132e68adef"));
}

static void
fu_device_parent_func(void)
{
	g_autoptr(FuDevice) child = fu_device_new(NULL);
	g_autoptr(FuDevice) child_root = NULL;
	g_autoptr(FuDevice) grandparent = fu_device_new(NULL);
	g_autoptr(FuDevice) grandparent_root = NULL;
	g_autoptr(FuDevice) parent = fu_device_new(NULL);
	g_autoptr(FuDevice) parent_root = NULL;

	fu_device_set_physical_id(child, "dummy");
	fu_device_set_physical_id(grandparent, "dummy");
	fu_device_set_physical_id(parent, "dummy");

	/* set up three layer family */
	fu_device_add_child(grandparent, parent);
	fu_device_add_child(parent, child);

	/* check parents */
	g_assert_true(fu_device_get_parent_internal(child) == parent);
	g_assert_true(fu_device_get_parent_internal(parent) == grandparent);
	g_assert_true(fu_device_get_parent_internal(grandparent) == NULL);

	/* check root */
	child_root = fu_device_get_root(child);
	g_assert_true(child_root == grandparent);
	parent_root = fu_device_get_root(parent);
	g_assert_true(parent_root == grandparent);
	grandparent_root = fu_device_get_root(child);
	g_assert_true(grandparent_root == grandparent);
}

static void
fu_device_incorporate_descendant_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuSelfTestDevice) test_device = g_object_new(FU_TYPE_SELF_TEST_DEVICE, NULL);

	fu_device_set_name(device, "FuDevice");
	fu_device_set_summary(FU_DEVICE(test_device), "FuSelfTestDevice");

	fu_device_incorporate(FU_DEVICE(test_device), device, FU_DEVICE_INCORPORATE_FLAG_ALL);
	g_assert_cmpstr(fu_device_get_name(FU_DEVICE(test_device)), ==, "FuDevice");

	/* this won't explode as device_class->incorporate is checking types */
	fu_device_incorporate(device, FU_DEVICE(test_device), FU_DEVICE_INCORPORATE_FLAG_ALL);
	g_assert_cmpstr(fu_device_get_summary(device), ==, "FuSelfTestDevice");
}

static void
fu_device_incorporate_non_generic_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuDevice) donor = fu_device_new(ctx);

	fu_device_add_instance_id_full(donor,
				       "USB\\VID_273F&PID_1004",
				       FU_DEVICE_INSTANCE_FLAG_GENERIC |
					   FU_DEVICE_INSTANCE_FLAG_VISIBLE);
	fu_device_add_instance_id_full(donor,
				       "USB\\VID_273F&PID_1004&CID_1234",
				       FU_DEVICE_INSTANCE_FLAG_VISIBLE);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_NO_GENERIC_GUIDS);
	fu_device_incorporate(device, donor, FU_DEVICE_INCORPORATE_FLAG_INSTANCE_IDS);
	g_assert_false(fu_device_has_instance_id(device,
						 "USB\\VID_273F&PID_1004",
						 FU_DEVICE_INSTANCE_FLAG_VISIBLE));
	g_assert_true(fu_device_has_instance_id(device,
						"USB\\VID_273F&PID_1004&CID_1234",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE));
	fu_device_convert_instance_ids(device);
	g_assert_false(fu_device_has_instance_id(device,
						 "USB\\VID_273F&PID_1004",
						 FU_DEVICE_INSTANCE_FLAG_VISIBLE));
	g_assert_true(fu_device_has_instance_id(device,
						"USB\\VID_273F&PID_1004&CID_1234",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE));
	g_assert_false(
	    fwupd_device_has_instance_id(FWUPD_DEVICE(device), "USB\\VID_273F&PID_1004"));
	g_assert_true(
	    fwupd_device_has_instance_id(FWUPD_DEVICE(device), "USB\\VID_273F&PID_1004&CID_1234"));
}

static void
fu_device_incorporate_flag_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuDevice) donor = fu_device_new(ctx);

	fu_device_set_logical_id(donor, "logi");
	fu_device_set_physical_id(donor, "phys");
	fu_device_add_vendor_id(donor, "PCI:0x1234");

	fu_device_incorporate(device,
			      donor,
			      FU_DEVICE_INCORPORATE_FLAG_VENDOR_IDS |
				  FU_DEVICE_INCORPORATE_FLAG_PHYSICAL_ID);
	g_assert_cmpstr(fu_device_get_physical_id(device), ==, "phys");
	g_assert_cmpstr(fu_device_get_logical_id(device), ==, NULL);
	g_assert_true(fu_device_has_vendor_id(device, "PCI:0x1234"));

	fu_device_incorporate(device, donor, FU_DEVICE_INCORPORATE_FLAG_ALL);
	g_assert_cmpstr(fu_device_get_logical_id(device), ==, "logi");
}

static void
fu_device_incorporate_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuDevice) donor = fu_device_new(ctx);
	g_autoptr(GError) error = NULL;

	/* load quirks */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* set up donor device */
	fu_device_set_equivalent_id(donor, "0000000000000000000000000000000000000000");
	fu_device_set_metadata(donor, "test", "me");
	fu_device_set_metadata(donor, "test2", "me");
	fu_device_add_instance_str(donor, "VID", "0A5C");
	fu_device_add_instance_u16(donor, "PID", 0x6412);
	fu_device_add_instance_u32(donor, "BOARD_ID", 0x12345678);
	fu_device_register_private_flag(donor, "self-test");
	fu_device_add_private_flag(donor, "self-test");

	/* match a quirk entry, and then clear to ensure incorporate uses the quirk instance ID */
	ret = fu_device_build_instance_id_full(donor,
					       FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					       &error,
					       "USB",
					       "VID",
					       "PID",
					       NULL);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_device_get_custom_flags(donor), ==, "ignore-runtime");
	fu_device_set_custom_flags(donor, "SHOULD_BE_REPLACED_WITH_QUIRK_VALUE");

	/* base properties */
	fu_device_add_flag(donor, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_set_created_usec(donor, 1514338000ull * G_USEC_PER_SEC);
	fu_device_set_modified_usec(donor, 1514338999ull * G_USEC_PER_SEC);
	fu_device_add_icon(donor, "computer");

	/* existing properties */
	fu_device_set_equivalent_id(device, "ffffffffffffffffffffffffffffffffffffffff");
	fu_device_set_metadata(device, "test2", "DO_NOT_OVERWRITE");
	fu_device_set_modified_usec(device, 1514340000ull * G_USEC_PER_SEC);

	/* incorporate properties from donor to device */
	fu_device_incorporate(device, donor, FU_DEVICE_INCORPORATE_FLAG_ALL);
	g_assert_cmpstr(fu_device_get_equivalent_id(device),
			==,
			"ffffffffffffffffffffffffffffffffffffffff");
	g_assert_cmpstr(fu_device_get_metadata(device, "test"), ==, "me");
	g_assert_cmpstr(fu_device_get_metadata(device, "test2"), ==, "DO_NOT_OVERWRITE");
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_REQUIRE_AC));
	g_assert_cmpint(fu_device_get_created_usec(device), ==, 1514338000ull * G_USEC_PER_SEC);
	g_assert_cmpint(fu_device_get_modified_usec(device), ==, 1514340000ull * G_USEC_PER_SEC);
	g_assert_cmpint(fu_device_get_icons(device)->len, ==, 1);
	ret = fu_device_build_instance_id(device, &error, "USB", "VID", NULL);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(
	    fu_device_has_instance_id(device, "USB\\VID_0A5C", FU_DEVICE_INSTANCE_FLAG_VISIBLE));
	g_assert_cmpstr(fu_device_get_custom_flags(device), ==, "ignore-runtime");
}

typedef struct {
	guint cnt_success;
	guint cnt_failed;
} FuDeviceRetryHelper;

static gboolean
fu_device_retry_success_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuDeviceRetryHelper *helper = (FuDeviceRetryHelper *)user_data;
	helper->cnt_success++;
	return TRUE;
}

static gboolean
fu_device_retry_failed_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuDeviceRetryHelper *helper = (FuDeviceRetryHelper *)user_data;
	helper->cnt_failed++;
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "failed");
	return FALSE;
}

static gboolean
fu_device_retry_success_3rd_try_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuDeviceRetryHelper *helper = (FuDeviceRetryHelper *)user_data;
	if (helper->cnt_failed == 2) {
		helper->cnt_success++;
		return TRUE;
	}
	helper->cnt_failed++;
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "failed");
	return FALSE;
}

static void
fu_device_retry_success_func(void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(GError) error = NULL;
	FuDeviceRetryHelper helper = {
	    .cnt_success = 0,
	    .cnt_failed = 0,
	};
	fu_device_retry_add_recovery(device,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     fu_device_retry_failed_cb);
	ret = fu_device_retry(device, fu_device_retry_success_cb, 3, &helper, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(helper.cnt_success, ==, 1);
	g_assert_cmpint(helper.cnt_failed, ==, 0);
}

static void
fu_device_retry_failed_func(void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(GError) error = NULL;
	FuDeviceRetryHelper helper = {
	    .cnt_success = 0,
	    .cnt_failed = 0,
	};
	fu_device_retry_add_recovery(device,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     fu_device_retry_success_cb);
	ret = fu_device_retry(device, fu_device_retry_failed_cb, 3, &helper, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
	g_assert_true(!ret);
	g_assert_cmpint(helper.cnt_success, ==, 2); /* do not reset for the last failure */
	g_assert_cmpint(helper.cnt_failed, ==, 3);
}

static void
fu_device_retry_hardware_func(void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(GError) error = NULL;
	FuDeviceRetryHelper helper = {
	    .cnt_success = 0,
	    .cnt_failed = 0,
	};
	ret = fu_device_retry(device, fu_device_retry_success_3rd_try_cb, 3, &helper, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(helper.cnt_success, ==, 1);
	g_assert_cmpint(helper.cnt_failed, ==, 2);
}

static void
fu_device_possible_plugin_func(void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) possible_plugins = NULL;

	ret = fu_device_set_quirk_kv(device, "Plugin", "dfu", FU_CONTEXT_QUIRK_SOURCE_FILE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* duplicate */
	ret = fu_device_set_quirk_kv(device, "Plugin", "dfu", FU_CONTEXT_QUIRK_SOURCE_FILE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* something else */
	ret = fu_device_set_quirk_kv(device, "Plugin", "abc", FU_CONTEXT_QUIRK_SOURCE_FILE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* remove the other thing */
	ret =
	    fu_device_set_quirk_kv(device, "Plugin", "~dfu", FU_CONTEXT_QUIRK_SOURCE_FILE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* verify */
	possible_plugins = fu_device_get_possible_plugins(device);
	g_assert_cmpint(possible_plugins->len, ==, 1);
	g_assert_cmpstr(g_ptr_array_index(possible_plugins, 0), ==, "abc");
}

static void
fu_device_parent_name_prefix_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(FuDevice) parent = fu_device_new(NULL);

	fu_device_set_id(parent, "0000000000000000000000000000000000000000");
	fu_device_set_name(parent, "Parent1");

	fu_device_set_id(device, "1111111111111111111111111111111111111111");
	fu_device_set_name(device, "Child1");
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_PARENT_NAME_PREFIX);
	fu_device_set_parent(device, parent);

	g_assert_cmpstr(fu_device_get_name(parent), ==, "Parent1");
	g_assert_cmpstr(fu_device_get_name(device), ==, "Parent1 (Child1)");

	/* still set, change child */
	g_assert_true(
	    fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_PARENT_NAME_PREFIX));
	fu_device_set_name(device, "Child2");
	g_assert_cmpstr(fu_device_get_name(device), ==, "Parent1 (Child2)");
}

static void
fu_device_id_display_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autofree gchar *id1 = NULL;
	g_autofree gchar *id2 = NULL;
	g_autofree gchar *id3 = NULL;
	g_autofree gchar *id4 = NULL;

	id1 = fu_device_get_id_display(device);
	g_assert_cmpstr(id1, ==, NULL);

	fu_device_set_id(device, "362301da643102b9f38477387e2193e57abaa590");
	id2 = fu_device_get_id_display(device);
	g_assert_cmpstr(id2, ==, "362301da643102b9f38477387e2193e57abaa590");

	fu_device_set_plugin(device, "uefi_dbx");
	id3 = fu_device_get_id_display(device);
	g_assert_cmpstr(id3, ==, "362301da643102b9f38477387e2193e57abaa590 {uefi_dbx}");

	fu_device_set_name(device, "UEFI dbx");
	id4 = fu_device_get_id_display(device);
	g_assert_cmpstr(id4, ==, "362301da643102b9f38477387e2193e57abaa590 [UEFI dbx]");
}

static void
fu_device_udev_func(void)
{
	g_autofree gchar *prop = NULL;
	g_autofree gchar *sysfs_path = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuUdevDevice) udev_device = fu_udev_device_new(ctx, sysfs_path);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) attrs = NULL;

	prop = fu_udev_device_read_property(udev_device, "MODALIAS", &error);
	g_assert_no_error(error);
	g_assert_cmpstr(prop, ==, "hdaudio:v10EC0298r00100103a01");

	/* list all the files in the directory */
	attrs = fu_udev_device_list_sysfs(udev_device, &error);
	g_assert_no_error(error);
	g_assert_nonnull(attrs);
	g_assert_cmpint(attrs->len, >, 10);
}

int
main(int argc, char **argv)
{
	g_autofree gchar *testdatadir = NULL;
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);

	g_test_init(&argc, &argv, NULL);
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_DATADIR", testdatadir, TRUE);
	g_test_add_func("/fwupd/device", fu_device_func);
	g_test_add_func("/fwupd/device/parent-name-prefix", fu_device_parent_name_prefix_func);
	g_test_add_func("/fwupd/device/id-for-display", fu_device_id_display_func);
	g_test_add_func("/fwupd/device/possible-plugin", fu_device_possible_plugin_func);
	g_test_add_func("/fwupd/device/udev", fu_device_udev_func);
	g_test_add_func("/fwupd/device/vfuncs", fu_device_vfuncs_func);
	g_test_add_func("/fwupd/device/instance-ids", fu_device_instance_ids_func);
	g_test_add_func("/fwupd/device/composite-id", fu_device_composite_id_func);
	g_test_add_func("/fwupd/device/flags", fu_device_flags_func);
	g_test_add_func("/fwupd/device/strsafe", fu_device_strsafe_func);
	g_test_add_func("/fwupd/device/private-flags", fu_device_custom_flags_func);
	g_test_add_func("/fwupd/device/inhibit", fu_device_inhibit_func);
	g_test_add_func("/fwupd/device/inhibit/updateable", fu_device_inhibit_updateable_func);
	g_test_add_func("/fwupd/device/parent", fu_device_parent_func);
	g_test_add_func("/fwupd/device/children", fu_device_children_func);
	g_test_add_func("/fwupd/device/incorporate", fu_device_incorporate_func);
	g_test_add_func("/fwupd/device/incorporate/flag", fu_device_incorporate_flag_func);
	g_test_add_func("/fwupd/device/incorporate/non-generic",
			fu_device_incorporate_non_generic_func);
	g_test_add_func("/fwupd/device/incorporate/descendant",
			fu_device_incorporate_descendant_func);
	g_test_add_func("/fwupd/device/name", fu_device_name_func);
	g_test_add_func("/fwupd/device/rescan", fu_device_rescan_func);
	g_test_add_func("/fwupd/device/metadata", fu_device_metadata_func);
	g_test_add_func("/fwupd/device/open-refcount", fu_device_open_refcount_func);
	g_test_add_func("/fwupd/device/version-format", fu_device_version_format_func);
	g_test_add_func("/fwupd/device/version-format/raw", fu_device_version_format_raw_func);
	g_test_add_func("/fwupd/device/retry/success", fu_device_retry_success_func);
	g_test_add_func("/fwupd/device/retry/failed", fu_device_retry_failed_func);
	g_test_add_func("/fwupd/device/retry/hardware", fu_device_retry_hardware_func);
	g_test_add_func("/fwupd/device/cfi-device", fu_device_cfi_device_func);
	g_test_add_func("/fwupd/device/progress", fu_device_progress_func);
	if (g_test_slow())
		g_test_add_func("/fwupd/device/poll", fu_device_poll_func);
	return g_test_run();
}
