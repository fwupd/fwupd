/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-udev-device-private.h"

static FuContext *
fu_i2c_device_test_ctx_new(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	/* the quirk store has to be loaded before any instance IDs are built */
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	return g_steal_pointer(&ctx);
}

static FuI2cDevice *
fu_i2c_device_test_new(FuContext *ctx, const gchar *sysfs_path, const gchar *name)
{
	FuI2cDevice *device;
	g_autoptr(FuDeviceEvent) ev_name = NULL;

	device = g_object_new(FU_TYPE_I2C_DEVICE, "context", ctx, NULL);
	fu_device_add_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_EMULATED);
	fu_device_set_fwupd_version(FU_DEVICE(device), PACKAGE_VERSION);
	fu_device_set_backend_id(FU_DEVICE(device), sysfs_path);
	fu_udev_device_set_subsystem(FU_UDEV_DEVICE(device), "i2c");
	fu_udev_device_set_devtype(FU_UDEV_DEVICE(device), "i2c_device");

	/* ReadAttr:Attr=name */
	ev_name = fu_device_event_new("ReadAttr:Attr=name");
	fu_device_event_set_str(ev_name, "Data", name);
	fu_device_add_event(FU_DEVICE(device), ev_name);

	return device;
}

/*
 * The bus number and the /dev node are both derived from the `i2c-N` component
 * of the sysfs path, as the kernel does not expose either as an attribute.
 */
static void
fu_i2c_device_probe_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_i2c_device_test_ctx_new();
	g_autoptr(FuI2cDevice) device = NULL;
	g_autoptr(GError) error = NULL;

	device = fu_i2c_device_test_new(ctx,
					"/sys/devices/pci0000:00/i2c-7/7-0050",
					"SMBus I801 adapter");
	ret = fu_device_probe(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_assert_cmpint(fu_udev_device_get_number(FU_UDEV_DEVICE(device)), ==, 7);
	g_assert_cmpstr(fu_udev_device_get_device_file(FU_UDEV_DEVICE(device)), ==, "/dev/i2c-7");
	g_assert_cmpstr(fu_device_get_physical_id(FU_DEVICE(device)),
			==,
			"DEVPATH=/devices/pci0000:00/i2c-7/7-0050");
	g_assert_true(fu_device_has_instance_id(FU_DEVICE(device),
						"I2C\\NAME_SMBus-I801-adapter",
						FU_DEVICE_INSTANCE_FLAG_QUIRKS));
}

/*
 * A sysfs path with no `i2c-N` component cannot be turned into a /dev node,
 * and that has to be an error rather than a silently unusable device.
 */
static void
fu_i2c_device_probe_no_bus_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_i2c_device_test_ctx_new();
	g_autoptr(FuI2cDevice) device = NULL;
	g_autoptr(GError) error = NULL;

	device = fu_i2c_device_test_new(ctx, "/sys/devices/pci0000:00/0000:00:1f.4", "nobus");
	ret = fu_device_probe(FU_DEVICE(device), &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
}

/*
 * I²C devices are usually tied to the platform and have very generic names, so
 * the HWID GUIDs are mixed into the instance IDs -- unless the device opts out.
 */
static void
fu_i2c_device_probe_hwid_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_i2c_device_test_ctx_new();
	g_autoptr(FuI2cDevice) device = NULL;
	g_autoptr(GError) error = NULL;

	fu_hwids_add_guid(fu_context_get_hwids(ctx), "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
	device = fu_i2c_device_test_new(ctx, "/sys/devices/pci0000:00/i2c-3/3-0010", "touchpad");
	ret = fu_device_probe(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_assert_true(fu_device_has_instance_id(FU_DEVICE(device),
						"I2C\\NAME_touchpad",
						FU_DEVICE_INSTANCE_FLAG_QUIRKS));
	g_assert_true(fu_device_has_instance_id(
	    FU_DEVICE(device),
	    "I2C\\NAME_touchpad&HWID_aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
	    FU_DEVICE_INSTANCE_FLAG_QUIRKS));
}

static void
fu_i2c_device_probe_no_hwid_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_i2c_device_test_ctx_new();
	g_autoptr(FuI2cDevice) device = NULL;
	g_autoptr(GError) error = NULL;

	fu_hwids_add_guid(fu_context_get_hwids(ctx), "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
	device = fu_i2c_device_test_new(ctx, "/sys/devices/pci0000:00/i2c-3/3-0010", "touchpad");
	fu_device_add_private_flag(FU_DEVICE(device), FU_I2C_DEVICE_PRIVATE_FLAG_NO_HWID_GUIDS);
	ret = fu_device_probe(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_assert_true(fu_device_has_instance_id(FU_DEVICE(device),
						"I2C\\NAME_touchpad",
						FU_DEVICE_INSTANCE_FLAG_QUIRKS));
	g_assert_false(fu_device_has_instance_id(
	    FU_DEVICE(device),
	    "I2C\\NAME_touchpad&HWID_aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
	    FU_DEVICE_INSTANCE_FLAG_QUIRKS));
}

static void
fu_i2c_device_read_write_func(void)
{
	gboolean ret;
	guint8 buf[4] = {0x0};
	const guint8 buf_wr[] = {0xDE, 0xAD, 0xBE, 0xEF};
	const guint8 buf_rd[] = {0x01, 0x02, 0x03, 0x04};
	g_autoptr(FuContext) ctx = fu_i2c_device_test_ctx_new();
	g_autoptr(FuDeviceEvent) ev_rd = NULL;
	g_autoptr(FuDeviceEvent) ev_wr = NULL;
	g_autoptr(FuI2cDevice) device = NULL;
	g_autoptr(GError) error = NULL;

	device = fu_i2c_device_test_new(ctx, "/sys/devices/pci0000:00/i2c-7/7-0050", "eeprom");

	/* Pwrite:Port=0x0,Data=3q2+7w==,Length=0x4 */
	ev_wr = fu_device_event_new("Pwrite:Port=0x0,Data=3q2+7w==,Length=0x4");
	fu_device_add_event(FU_DEVICE(device), ev_wr);

	/* Pread:Port=0x0,Length=0x4 */
	ev_rd = fu_device_event_new("Pread:Port=0x0,Length=0x4");
	fu_device_event_set_data(ev_rd, "Data", buf_rd, sizeof(buf_rd));
	fu_device_add_event(FU_DEVICE(device), ev_rd);

	ret = fu_i2c_device_write(device, buf_wr, sizeof(buf_wr), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_i2c_device_read(device, buf, sizeof(buf), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(buf[0], ==, 0x01);
	g_assert_cmpint(buf[1], ==, 0x02);
	g_assert_cmpint(buf[2], ==, 0x03);
	g_assert_cmpint(buf[3], ==, 0x04);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/i2c-device/probe", fu_i2c_device_probe_func);
	g_test_add_func("/fwupd/i2c-device/probe-no-bus", fu_i2c_device_probe_no_bus_func);
	g_test_add_func("/fwupd/i2c-device/probe-hwid", fu_i2c_device_probe_hwid_func);
	g_test_add_func("/fwupd/i2c-device/probe-no-hwid", fu_i2c_device_probe_no_hwid_func);
	g_test_add_func("/fwupd/i2c-device/read-write", fu_i2c_device_read_write_func);
	return g_test_run();
}
