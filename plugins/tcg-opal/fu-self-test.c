/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#ifdef HAVE_LINUX_SED_OPAL_H
#include <linux/sed-opal.h>
#endif
#include <sys/ioctl.h>

#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-plugin-private.h"
#include "fu-security-attrs-private.h"
#include "fu-tcg-opal-device.h"
#include "fu-tcg-opal-plugin.h"

static guint fu_test_tcg_opal_device_cnt;

static FuDevice *
fu_test_tcg_opal_new_device(FuContext *ctx, FuTcgOpalFlags opal_flags)
{
	g_autofree gchar *id = g_strdup_printf("tcg_opal%u", fu_test_tcg_opal_device_cnt++);
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_TCG_OPAL_DEVICE, "context", ctx, NULL);
	fu_device_set_id(device, id);
	fu_tcg_opal_device_set_opal_flags(FU_TCG_OPAL_DEVICE(device), opal_flags);
	return g_steal_pointer(&device);
}

static void
fu_tcg_opal_plugin_opal_no_devices_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuSecurityAttr) attr = NULL;

	plugin = fu_plugin_new_from_gtype(fu_tcg_opal_plugin_get_type(), ctx);
	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_HW_DISK_ENCRYPTION,
						     NULL);
	g_assert_null(attr);
}

static void
fu_tcg_opal_plugin_opal_one_without_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuSecurityAttr) attr = NULL;

	plugin = fu_plugin_new_from_gtype(fu_tcg_opal_plugin_get_type(), ctx);
	dev = fu_test_tcg_opal_new_device(ctx, FU_TCG_OPAL_FLAG_UNKNOWN);
	fu_plugin_add_device(plugin, dev);
	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_HW_DISK_ENCRYPTION,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fu_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
	g_assert_false(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_tcg_opal_plugin_opal_one_with_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuSecurityAttr) attr = NULL;

	plugin = fu_plugin_new_from_gtype(fu_tcg_opal_plugin_get_type(), ctx);
	dev = fu_test_tcg_opal_new_device(ctx,
					  FU_TCG_OPAL_FLAG_SUPPORTED |
					      FU_TCG_OPAL_FLAG_LOCKING_SUPPORTED |
					      FU_TCG_OPAL_FLAG_LOCKING_ENABLED);
	fu_plugin_add_device(plugin, dev);
	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_HW_DISK_ENCRYPTION,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fu_security_attr_get_result(attr), ==, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	g_assert_true(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_tcg_opal_plugin_opal_two_without_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) dev1 = NULL;
	g_autoptr(FuDevice) dev2 = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuSecurityAttr) attr = NULL;

	plugin = fu_plugin_new_from_gtype(fu_tcg_opal_plugin_get_type(), ctx);
	dev1 = fu_test_tcg_opal_new_device(ctx, FU_TCG_OPAL_FLAG_UNKNOWN);
	fu_plugin_add_device(plugin, dev1);
	dev2 = fu_test_tcg_opal_new_device(ctx, FU_TCG_OPAL_FLAG_UNKNOWN);
	fu_plugin_add_device(plugin, dev2);
	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_HW_DISK_ENCRYPTION,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fu_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
	g_assert_false(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_tcg_opal_plugin_opal_two_mixed_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) dev1 = NULL;
	g_autoptr(FuDevice) dev2 = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuSecurityAttr) attr = NULL;

	plugin = fu_plugin_new_from_gtype(fu_tcg_opal_plugin_get_type(), ctx);
	dev1 = fu_test_tcg_opal_new_device(ctx,
					   FU_TCG_OPAL_FLAG_SUPPORTED |
					       FU_TCG_OPAL_FLAG_LOCKING_SUPPORTED |
					       FU_TCG_OPAL_FLAG_LOCKING_ENABLED);
	fu_plugin_add_device(plugin, dev1);
	dev2 = fu_test_tcg_opal_new_device(ctx, FU_TCG_OPAL_FLAG_UNKNOWN);
	fu_plugin_add_device(plugin, dev2);
	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_HW_DISK_ENCRYPTION,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fu_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
	g_assert_false(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_tcg_opal_plugin_opal_two_with_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) dev1 = NULL;
	g_autoptr(FuDevice) dev2 = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuSecurityAttr) attr = NULL;

	plugin = fu_plugin_new_from_gtype(fu_tcg_opal_plugin_get_type(), ctx);
	dev1 = fu_test_tcg_opal_new_device(ctx,
					   FU_TCG_OPAL_FLAG_SUPPORTED |
					       FU_TCG_OPAL_FLAG_LOCKING_SUPPORTED |
					       FU_TCG_OPAL_FLAG_LOCKING_ENABLED);
	fu_plugin_add_device(plugin, dev1);
	dev2 = fu_test_tcg_opal_new_device(ctx,
					   FU_TCG_OPAL_FLAG_SUPPORTED |
					       FU_TCG_OPAL_FLAG_LOCKING_SUPPORTED |
					       FU_TCG_OPAL_FLAG_LOCKING_ENABLED);
	fu_plugin_add_device(plugin, dev2);
	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_HW_DISK_ENCRYPTION,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fu_security_attr_get_result(attr), ==, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	g_assert_true(fu_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_tcg_opal_device_setup_func(void)
{
#ifdef HAVE_LINUX_SED_OPAL_H
	gboolean ret;
	struct opal_status status = {0};
	g_autofree gchar *data = NULL;
	g_autofree gchar *event_id = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuBackend) backend = g_object_new(FU_TYPE_BACKEND, "context", ctx, NULL);
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDeviceEvent) event_ioctl = NULL;
	g_autoptr(FuDeviceEvent) event_parent = NULL;
	g_autoptr(GError) error = NULL;

	/* emulated namespace block device */
	device = g_object_new(FU_TYPE_TCG_OPAL_DEVICE, "context", ctx, NULL);
	fu_device_set_backend(device, backend);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_EMULATED);
	fu_device_set_fwupd_version(device, PACKAGE_VERSION);

	/* emulate the NVMe parent */
	event_parent = fu_device_event_new("GetBackendParent:Subsystem=nvme");
	fu_device_event_set_str(event_parent, "GType", "FuDevice");
	fu_device_event_set_str(event_parent, "BackendId", "/sys/devices/nvme0");
	fu_device_add_event(device, event_parent);

	/* emulate the IOC_OPAL_GET_STATUS ioctl */
	data = fu_base64_encode((const guint8 *)&status, sizeof(status));
	event_id = g_strdup_printf("Ioctl:Request=0x%04x,Data=%s,Length=0x%x",
				   (guint)IOC_OPAL_GET_STATUS,
				   data,
				   (guint)sizeof(status));
	status.flags = FU_TCG_OPAL_FLAG_SUPPORTED | FU_TCG_OPAL_FLAG_LOCKING_SUPPORTED |
		       FU_TCG_OPAL_FLAG_LOCKING_ENABLED;
	event_ioctl = fu_device_event_new(event_id);
	fu_device_event_set_data(event_ioctl, "DataOut", (const guint8 *)&status, sizeof(status));
	fu_device_event_set_i64(event_ioctl, "Rc", 0);
	fu_device_add_event(device, event_ioctl);

	/* probe and read the flags */
	ret = fu_device_setup(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_device_get_physical_id(device), ==, "/sys/devices/nvme0");
	g_assert_cmpint(fu_tcg_opal_device_get_opal_flags(FU_TCG_OPAL_DEVICE(device)),
			==,
			FU_TCG_OPAL_FLAG_SUPPORTED | FU_TCG_OPAL_FLAG_LOCKING_SUPPORTED |
			    FU_TCG_OPAL_FLAG_LOCKING_ENABLED);
#endif
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/opal/no-devices", fu_tcg_opal_plugin_opal_no_devices_func);
	g_test_add_func("/fwupd/opal/one-without", fu_tcg_opal_plugin_opal_one_without_func);
	g_test_add_func("/fwupd/opal/one-with", fu_tcg_opal_plugin_opal_one_with_func);
	g_test_add_func("/fwupd/opal/two-without", fu_tcg_opal_plugin_opal_two_without_func);
	g_test_add_func("/fwupd/opal/two-mixed", fu_tcg_opal_plugin_opal_two_mixed_func);
	g_test_add_func("/fwupd/opal/two-with", fu_tcg_opal_plugin_opal_two_with_func);
	g_test_add_func("/fwupd/opal/device-setup", fu_tcg_opal_device_setup_func);
	return g_test_run();
}
