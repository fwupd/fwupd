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
fu_serio_device_test_ctx_new(void)
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

static FuSerioDevice *
fu_serio_device_test_new(FuContext *ctx, const gchar *firmware_id, const gchar *description)
{
	FuSerioDevice *device;

	device = g_object_new(FU_TYPE_SERIO_DEVICE, "context", ctx, NULL);
	fu_device_add_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_EMULATED);
	fu_device_set_fwupd_version(FU_DEVICE(device), PACKAGE_VERSION);
	fu_device_set_backend_id(FU_DEVICE(device), "/sys/devices/platform/i8042/serio1");
	fu_udev_device_set_subsystem(FU_UDEV_DEVICE(device), "serio");
	fu_udev_device_set_devtype(FU_UDEV_DEVICE(device), "serio");

	/* ReadAttr:Attr=firmware_id */
	if (firmware_id != NULL) {
		g_autoptr(FuDeviceEvent) ev = fu_device_event_new("ReadAttr:Attr=firmware_id");
		fu_device_event_set_str(ev, "Data", firmware_id);
		fu_device_add_event(FU_DEVICE(device), ev);
	}

	/* ReadAttr:Attr=description */
	if (description != NULL) {
		g_autoptr(FuDeviceEvent) ev = fu_device_event_new("ReadAttr:Attr=description");
		fu_device_event_set_str(ev, "Data", description);
		fu_device_add_event(FU_DEVICE(device), ev);
	}

	return device;
}

/*
 * The kernel reports the firmware ID with a `PNP: ` prefix that is not part of
 * the ID itself, so it has to be stripped before building the instance ID.
 */
static void
fu_serio_device_probe_pnp_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_serio_device_test_ctx_new();
	g_autoptr(FuSerioDevice) device = NULL;
	g_autoptr(GError) error = NULL;

	device = fu_serio_device_test_new(ctx, "PNP: SYN1e5b", "i8042 KBD port");
	ret = fu_device_probe(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_assert_true(fu_device_has_instance_id(FU_DEVICE(device),
						"SERIO\\FWID_SYN1E5B",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE));
	g_assert_cmpstr(fu_device_get_summary(FU_DEVICE(device)), ==, "i8042 KBD port");
	g_assert_cmpstr(fu_device_get_physical_id(FU_DEVICE(device)),
			==,
			"DEVPATH=/devices/platform/i8042/serio1");
}

/* not every kernel driver adds the prefix */
static void
fu_serio_device_probe_no_pnp_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_serio_device_test_ctx_new();
	g_autoptr(FuSerioDevice) device = NULL;
	g_autoptr(GError) error = NULL;

	device = fu_serio_device_test_new(ctx, "syn1e5b", NULL);
	ret = fu_device_probe(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_assert_true(fu_device_has_instance_id(FU_DEVICE(device),
						"SERIO\\FWID_SYN1E5B",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE));
	g_assert_null(fu_device_get_summary(FU_DEVICE(device)));
}

/*
 * An empty firmware_id is reported by devices with no PNP ID at all, and must
 * not create a `SERIO\FWID_` instance ID that every such device would share.
 */
static void
fu_serio_device_probe_empty_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_serio_device_test_ctx_new();
	g_autoptr(FuSerioDevice) device = NULL;
	g_autoptr(GError) error = NULL;

	device = fu_serio_device_test_new(ctx, "", NULL);
	ret = fu_device_probe(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_assert_false(fu_device_has_instance_id(FU_DEVICE(device),
						 "SERIO\\FWID_",
						 FU_DEVICE_INSTANCE_FLAG_VISIBLE));
	g_assert_cmpstr(fu_device_get_physical_id(FU_DEVICE(device)),
			==,
			"DEVPATH=/devices/platform/i8042/serio1");
}

/* no firmware_id attribute at all, e.g. a non-PNP serio port */
static void
fu_serio_device_probe_missing_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_serio_device_test_ctx_new();
	g_autoptr(FuSerioDevice) device = NULL;
	g_autoptr(GError) error = NULL;

	device = fu_serio_device_test_new(ctx, NULL, NULL);
	ret = fu_device_probe(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_device_get_physical_id(FU_DEVICE(device)),
			==,
			"DEVPATH=/devices/platform/i8042/serio1");
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/serio-device/probe-pnp", fu_serio_device_probe_pnp_func);
	g_test_add_func("/fwupd/serio-device/probe-no-pnp", fu_serio_device_probe_no_pnp_func);
	g_test_add_func("/fwupd/serio-device/probe-empty", fu_serio_device_probe_empty_func);
	g_test_add_func("/fwupd/serio-device/probe-missing", fu_serio_device_probe_missing_func);
	return g_test_run();
}
