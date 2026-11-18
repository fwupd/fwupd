/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-dpaux-struct.h"
#include "fu-udev-device-private.h"

static FuContext *
fu_dpaux_device_test_ctx_new(void)
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

static FuDpauxDevice *
fu_dpaux_device_test_new(FuContext *ctx)
{
	FuDpauxDevice *device;
	g_autoptr(FuDeviceEvent) ev_devname = NULL;

	device = g_object_new(FU_TYPE_DPAUX_DEVICE, "context", ctx, NULL);
	fu_device_add_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_EMULATED);
	fu_device_set_fwupd_version(FU_DEVICE(device), PACKAGE_VERSION);
	fu_device_set_backend_id(FU_DEVICE(device),
				 "/sys/devices/pci0000:00/0000:00:02.0/drm_dp_aux0");
	fu_udev_device_set_subsystem(FU_UDEV_DEVICE(device), "drm_dp_aux_dev");
	fu_udev_device_set_devtype(FU_UDEV_DEVICE(device), "drm_dp_aux_dev");
	fu_udev_device_set_device_file(FU_UDEV_DEVICE(device), "/dev/drm_dp_aux0");

	/* ReadProp:Key=DEVNAME */
	ev_devname = fu_device_event_new("ReadProp:Key=DEVNAME");
	fu_device_event_set_str(ev_devname, "Data", "drm_dp_aux0");
	fu_device_add_event(FU_DEVICE(device), ev_devname);

	return device;
}

static void
fu_dpaux_device_test_add_dpcd(FuDpauxDevice *device, goffset offset, const guint8 *buf, gsize bufsz)
{
	g_autofree gchar *event_id = NULL;
	g_autoptr(FuDeviceEvent) ev = NULL;

	event_id = g_strdup_printf("Read:Length=0x%x,Offset=0x%x", (guint)bufsz, (guint)offset);
	ev = fu_device_event_new(event_id);
	fu_device_event_set_data(ev, "Data", buf, bufsz);
	fu_device_add_event(FU_DEVICE(device), ev);
}

static void
fu_dpaux_device_default_func(void)
{
	g_autoptr(FuContext) ctx = fu_dpaux_device_test_ctx_new();
	g_autoptr(FuDpauxDevice) device = fu_dpaux_device_test_new(ctx);

	/* branch device is the default, as that is what we normally want to update */
	g_assert_cmpint(fu_dpaux_device_get_dpcd_offset(device),
			==,
			FU_DPAUX_DEVICE_DPCD_OFFSET_BRANCH_DEVICE);
	g_assert_cmpint(fu_dpaux_device_get_dpcd_ieee_oui(device), ==, 0x0);
	g_assert_cmpint(fu_dpaux_device_get_dpcd_hw_rev(device), ==, 0x0);
	g_assert_null(fu_dpaux_device_get_dpcd_dev_id(device));
	g_assert_cmpint(fu_device_get_version_format(FU_DEVICE(device)),
			==,
			FWUPD_VERSION_FORMAT_TRIPLET);
}

static void
fu_dpaux_device_setters_func(void)
{
	g_autoptr(FuContext) ctx = fu_dpaux_device_test_ctx_new();
	g_autoptr(FuDpauxDevice) device = fu_dpaux_device_test_new(ctx);

	fu_dpaux_device_set_dpcd_offset(device, FU_DPAUX_DEVICE_DPCD_OFFSET_SOURCE_DEVICE);
	g_assert_cmpint(fu_dpaux_device_get_dpcd_offset(device),
			==,
			FU_DPAUX_DEVICE_DPCD_OFFSET_SOURCE_DEVICE);
	fu_dpaux_device_set_dpcd_offset(device, FU_DPAUX_DEVICE_DPCD_OFFSET_SINK_DEVICE);
	g_assert_cmpint(fu_dpaux_device_get_dpcd_offset(device),
			==,
			FU_DPAUX_DEVICE_DPCD_OFFSET_SINK_DEVICE);

	fu_dpaux_device_set_dpcd_ieee_oui(device, 0x00D046);
	g_assert_cmpint(fu_dpaux_device_get_dpcd_ieee_oui(device), ==, 0x00D046);
	fu_dpaux_device_set_dpcd_hw_rev(device, 0x12);
	g_assert_cmpint(fu_dpaux_device_get_dpcd_hw_rev(device), ==, 0x12);
	fu_dpaux_device_set_dpcd_dev_id(device, "ABCDEF");
	g_assert_cmpstr(fu_dpaux_device_get_dpcd_dev_id(device), ==, "ABCDEF");

	/* setting the same value again is a no-op, not a leak or a reset */
	fu_dpaux_device_set_dpcd_dev_id(device, "ABCDEF");
	g_assert_cmpstr(fu_dpaux_device_get_dpcd_dev_id(device), ==, "ABCDEF");
}

static void
fu_dpaux_device_setup_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_dpaux_device_test_ctx_new();
	g_autoptr(FuDpauxDevice) device = NULL;
	g_autoptr(FuStructDpauxDpcd) st = fu_struct_dpaux_dpcd_new();
	g_autoptr(GError) error = NULL;

	fu_struct_dpaux_dpcd_set_ieee_oui(st, 0x00D046);
	ret = fu_struct_dpaux_dpcd_set_dev_id(st, "ABCDEF", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_struct_dpaux_dpcd_set_hw_rev(st, 0x12);
	fu_struct_dpaux_dpcd_set_fw_ver(st, 0x010203);

	device = fu_dpaux_device_test_new(ctx);
	fu_dpaux_device_test_add_dpcd(device,
				      FU_DPAUX_DEVICE_DPCD_OFFSET_BRANCH_DEVICE,
				      st->buf->data,
				      st->buf->len);

	ret = fu_device_probe(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_device_get_logical_id(FU_DEVICE(device)), ==, "drm_dp_aux0");
	g_assert_cmpstr(fu_device_get_physical_id(FU_DEVICE(device)), ==, "DEVNAME=drm_dp_aux0");

	ret = fu_device_setup(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_assert_cmpint(fu_dpaux_device_get_dpcd_ieee_oui(device), ==, 0x00D046);
	g_assert_cmpint(fu_dpaux_device_get_dpcd_hw_rev(device), ==, 0x12);
	g_assert_cmpstr(fu_dpaux_device_get_dpcd_dev_id(device), ==, "ABCDEF");
	g_assert_cmpint(fu_device_get_version_raw(FU_DEVICE(device)), ==, 0x010203);
	g_assert_cmpstr(fu_device_get_version(FU_DEVICE(device)), ==, "1.2.3");
}

/*
 * The DPCD read happens at the offset set by the plugin, so a device told to
 * look at the sink must not read the branch block.
 */
static void
fu_dpaux_device_setup_offset_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_dpaux_device_test_ctx_new();
	g_autoptr(FuDpauxDevice) device = NULL;
	g_autoptr(FuStructDpauxDpcd) st = fu_struct_dpaux_dpcd_new();
	g_autoptr(GError) error = NULL;

	fu_struct_dpaux_dpcd_set_ieee_oui(st, 0x123456);
	ret = fu_struct_dpaux_dpcd_set_dev_id(st, "SINK01", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_struct_dpaux_dpcd_set_hw_rev(st, 0x07);
	fu_struct_dpaux_dpcd_set_fw_ver(st, 0x000009);

	device = fu_dpaux_device_test_new(ctx);
	fu_dpaux_device_set_dpcd_offset(device, FU_DPAUX_DEVICE_DPCD_OFFSET_SINK_DEVICE);
	fu_dpaux_device_test_add_dpcd(device,
				      FU_DPAUX_DEVICE_DPCD_OFFSET_SINK_DEVICE,
				      st->buf->data,
				      st->buf->len);

	ret = fu_device_probe(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_setup(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_assert_cmpint(fu_dpaux_device_get_dpcd_ieee_oui(device), ==, 0x123456);
	g_assert_cmpstr(fu_dpaux_device_get_dpcd_dev_id(device), ==, "SINK01");
	g_assert_cmpint(fu_dpaux_device_get_dpcd_hw_rev(device), ==, 0x07);
}

/* a DPCD read that returns nothing must fail rather than report a null device */
static void
fu_dpaux_device_setup_no_data_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_dpaux_device_test_ctx_new();
	g_autoptr(FuDpauxDevice) device = NULL;
	g_autoptr(GError) error = NULL;

	device = fu_dpaux_device_test_new(ctx);
	ret = fu_device_probe(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_device_setup(FU_DEVICE(device), &error);
	g_assert_nonnull(error);
	g_assert_false(ret);
}

static void
fu_dpaux_device_read_write_func(void)
{
	gboolean ret;
	guint8 buf[4] = {0x0};
	const guint8 buf_rd[] = {0xDE, 0xAD, 0xBE, 0xEF};
	const guint8 buf_wr[] = {0x01, 0x02};
	g_autofree gchar *data_base64 = fu_base64_encode(buf_wr, sizeof(buf_wr));
	g_autofree gchar *event_id = NULL;
	g_autoptr(FuContext) ctx = fu_dpaux_device_test_ctx_new();
	g_autoptr(FuDeviceEvent) ev_wr = NULL;
	g_autoptr(FuDpauxDevice) device = NULL;
	g_autoptr(GError) error = NULL;

	device = fu_dpaux_device_test_new(ctx);
	fu_dpaux_device_test_add_dpcd(device,
				      FU_DPAUX_DEVICE_DPCD_OFFSET_LINK_CONFIGURATION,
				      buf_rd,
				      sizeof(buf_rd));

	/* the event ID encodes the data the device is expected to be sent */
	event_id = g_strdup_printf("Write:Data=%s,Length=0x%x", data_base64, (guint)sizeof(buf_wr));
	ev_wr = fu_device_event_new(event_id);
	fu_device_add_event(FU_DEVICE(device), ev_wr);

	ret = fu_dpaux_device_read(device,
				   FU_DPAUX_DEVICE_DPCD_OFFSET_LINK_CONFIGURATION,
				   buf,
				   sizeof(buf),
				   100,
				   &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(buf[0], ==, 0xDE);
	g_assert_cmpint(buf[3], ==, 0xEF);

	ret = fu_dpaux_device_write(device,
				    FU_DPAUX_DEVICE_DPCD_OFFSET_LINK_CONFIGURATION,
				    buf_wr,
				    sizeof(buf_wr),
				    100,
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_dpaux_device_incorporate_func(void)
{
	g_autoptr(FuContext) ctx = fu_dpaux_device_test_ctx_new();
	g_autoptr(FuDpauxDevice) device = fu_dpaux_device_test_new(ctx);
	g_autoptr(FuDpauxDevice) donor = fu_dpaux_device_test_new(ctx);

	fu_dpaux_device_set_dpcd_offset(donor, FU_DPAUX_DEVICE_DPCD_OFFSET_SOURCE_DEVICE);
	fu_dpaux_device_set_dpcd_ieee_oui(donor, 0x00D046);
	fu_dpaux_device_set_dpcd_hw_rev(donor, 0x12);
	fu_dpaux_device_set_dpcd_dev_id(donor, "ABCDEF");

	fu_device_incorporate(FU_DEVICE(device), FU_DEVICE(donor), FU_DEVICE_INCORPORATE_FLAG_ALL);
	g_assert_cmpint(fu_dpaux_device_get_dpcd_offset(device),
			==,
			FU_DPAUX_DEVICE_DPCD_OFFSET_SOURCE_DEVICE);
	g_assert_cmpint(fu_dpaux_device_get_dpcd_ieee_oui(device), ==, 0x00D046);
	g_assert_cmpint(fu_dpaux_device_get_dpcd_hw_rev(device), ==, 0x12);
	g_assert_cmpstr(fu_dpaux_device_get_dpcd_dev_id(device), ==, "ABCDEF");
}

/*
 * Invalidating the device drops the cached DPCD so that the next setup
 * re-reads it -- a stale dev_id would otherwise survive a hotplug.
 */
static void
fu_dpaux_device_invalidate_func(void)
{
	g_autoptr(FuContext) ctx = fu_dpaux_device_test_ctx_new();
	g_autoptr(FuDpauxDevice) device = fu_dpaux_device_test_new(ctx);

	fu_dpaux_device_set_dpcd_offset(device, FU_DPAUX_DEVICE_DPCD_OFFSET_SINK_DEVICE);
	fu_dpaux_device_set_dpcd_ieee_oui(device, 0x00D046);
	fu_dpaux_device_set_dpcd_hw_rev(device, 0x12);
	fu_dpaux_device_set_dpcd_dev_id(device, "ABCDEF");
	fu_device_probe_invalidate(FU_DEVICE(device));

	g_assert_cmpint(fu_dpaux_device_get_dpcd_offset(device),
			==,
			FU_DPAUX_DEVICE_DPCD_OFFSET_BRANCH_DEVICE);
	g_assert_cmpint(fu_dpaux_device_get_dpcd_ieee_oui(device), ==, 0x0);
	g_assert_cmpint(fu_dpaux_device_get_dpcd_hw_rev(device), ==, 0x0);
	g_assert_null(fu_dpaux_device_get_dpcd_dev_id(device));
}

static void
fu_dpaux_device_to_string_func(void)
{
	g_autofree gchar *str = NULL;
	g_autoptr(FuContext) ctx = fu_dpaux_device_test_ctx_new();
	g_autoptr(FuDpauxDevice) device = fu_dpaux_device_test_new(ctx);

	fu_dpaux_device_set_dpcd_ieee_oui(device, 0x00D046);
	fu_dpaux_device_set_dpcd_hw_rev(device, 0x12);
	fu_dpaux_device_set_dpcd_dev_id(device, "ABCDEF");

	str = fu_device_to_string(FU_DEVICE(device));
	g_assert_nonnull(str);
	g_assert_nonnull(g_strstr_len(str, -1, "DpcdIeeeOui"));
	g_assert_nonnull(g_strstr_len(str, -1, "DpcdHwRev"));
	g_assert_nonnull(g_strstr_len(str, -1, "ABCDEF"));
}

/*
 * Reading the DPCD hangs some Framework laptops with an old system firmware,
 * so setup refuses to run there.
 */
static void
fu_dpaux_device_setup_broken_hwid_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_dpaux_device_test_ctx_new();
	g_autoptr(FuDpauxDevice) device = NULL;
	g_autoptr(GError) error = NULL;

	fu_hwids_add_guid(fu_context_get_hwids(ctx), "32d49d99-414b-55d5-813b-12aaf0335b58");
	device = fu_dpaux_device_test_new(ctx);
	fu_device_set_name(FU_DEVICE(device), "AMDGPU DM aux hw bus 0");

	ret = fu_device_setup(FU_DEVICE(device), &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/dpaux-device/default", fu_dpaux_device_default_func);
	g_test_add_func("/fwupd/dpaux-device/setters", fu_dpaux_device_setters_func);
	g_test_add_func("/fwupd/dpaux-device/setup", fu_dpaux_device_setup_func);
	g_test_add_func("/fwupd/dpaux-device/setup-offset", fu_dpaux_device_setup_offset_func);
	g_test_add_func("/fwupd/dpaux-device/setup-no-data", fu_dpaux_device_setup_no_data_func);
	g_test_add_func("/fwupd/dpaux-device/read-write", fu_dpaux_device_read_write_func);
	g_test_add_func("/fwupd/dpaux-device/incorporate", fu_dpaux_device_incorporate_func);
	g_test_add_func("/fwupd/dpaux-device/invalidate", fu_dpaux_device_invalidate_func);
	g_test_add_func("/fwupd/dpaux-device/to-string", fu_dpaux_device_to_string_func);
	g_test_add_func("/fwupd/dpaux-device/setup-broken-hwid",
			fu_dpaux_device_setup_broken_hwid_func);
	return g_test_run();
}
