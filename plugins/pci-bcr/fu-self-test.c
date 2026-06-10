/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-pci-bcr-plugin.h"
#include "fu-plugin-private.h"
#include "fu-security-attrs-private.h"
#include "fu-udev-device-private.h"

#define BCR_WPD	    (1 << 0)
#define BCR_BLE	    (1 << 1)
#define BCR_SMM_BWP (1 << 5)

static void
fu_test_pci_bcr_device_add_sysfs(FuDevice *device, /* nocheck:name */
				 const gchar *attr,
				 const gchar *value)
{
	g_autofree gchar *event_id = g_strdup_printf("ReadAttr:Attr=%s", attr);
	g_autoptr(FuDeviceEvent) event = fu_device_event_new(event_id);
	fu_device_event_set_str(event, "Data", value);
	fu_device_add_event(device, event);
}

static void
fu_test_pci_bcr_device_add_prop(FuDevice *device, /* nocheck:name */
				const gchar *key,
				const gchar *value)
{
	g_autofree gchar *event_id = g_strdup_printf("ReadProp:Key=%s", key);
	g_autoptr(FuDeviceEvent) event = fu_device_event_new(event_id);
	fu_device_event_set_str(event, "Data", value);
	fu_device_add_event(device, event);
}

static void
fu_test_pci_bcr_device_add_pread(FuDevice *device, /* nocheck:name */
				 goffset port,
				 const guint8 *buf,
				 gsize bufsz)
{
	g_autofree gchar *event_id =
	    g_strdup_printf("Pread:Port=0x%x,Length=0x%x", (guint)port, (guint)bufsz);
	g_autoptr(FuDeviceEvent) event = fu_device_event_new(event_id);
	fu_device_event_set_data(event, "Data", buf, bufsz);
	fu_device_add_event(device, event);
}

static FuDevice *
fu_test_pci_bcr_new_emulated_device(FuContext *ctx, guint8 bcr)
{
	FuDevice *device;
	device = g_object_new(FU_TYPE_PCI_DEVICE, "context", ctx, NULL);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_EMULATED);
	fu_device_set_backend_id(device, "/sys/devices/pci0000:00/0000:00:1f.5");
	fu_udev_device_set_subsystem(FU_UDEV_DEVICE(device), "pci");
	fu_test_pci_bcr_device_add_sysfs(device, "vendor", "0x8086");
	fu_test_pci_bcr_device_add_sysfs(device, "class", "0x0c8000");
	fu_test_pci_bcr_device_add_prop(device, "PCI_SLOT_NAME", "0000:00:1f.5");
	fu_test_pci_bcr_device_add_pread(device, 0xdc, &bcr, 1);
	return device;
}

static void
fu_pci_bcr_plugin_no_device_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr_bioswe = NULL;
	g_autoptr(FwupdSecurityAttr) attr_ble = NULL;
	g_autoptr(FwupdSecurityAttr) attr_smm = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_INTEL);
	plugin = fu_plugin_new_from_gtype(fu_pci_bcr_plugin_get_type(), ctx);

	fu_plugin_runner_add_security_attrs(plugin, attrs);

	attr_bioswe =
	    fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE, NULL);
	g_assert_nonnull(attr_bioswe);
	g_assert_cmpint(fwupd_security_attr_get_result(attr_bioswe),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);

	attr_ble =
	    fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_SPI_BLE, NULL);
	g_assert_nonnull(attr_ble);
	g_assert_cmpint(fwupd_security_attr_get_result(attr_ble),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);

	attr_smm =
	    fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_SPI_SMM_BWP, NULL);
	g_assert_nonnull(attr_smm);
	g_assert_cmpint(fwupd_security_attr_get_result(attr_smm),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
}

static void
fu_pci_bcr_plugin_not_intel_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr_bioswe = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_AMD);
	plugin = fu_plugin_new_from_gtype(fu_pci_bcr_plugin_get_type(), ctx);

	fu_plugin_runner_add_security_attrs(plugin, attrs);

	attr_bioswe =
	    fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE, NULL);
	g_assert_null(attr_bioswe);
}

static void
fu_pci_bcr_plugin_bioswe_enabled_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_INTEL);
	plugin = fu_plugin_new_from_gtype(fu_pci_bcr_plugin_get_type(), ctx);

	device = fu_test_pci_bcr_new_emulated_device(ctx, BCR_WPD);
	ret = fu_plugin_runner_backend_device_added(plugin, device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr =
	    fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE, NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	g_assert_false(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_pci_bcr_plugin_bioswe_not_enabled_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_INTEL);
	plugin = fu_plugin_new_from_gtype(fu_pci_bcr_plugin_get_type(), ctx);

	device = fu_test_pci_bcr_new_emulated_device(ctx, 0);
	ret = fu_plugin_runner_backend_device_added(plugin, device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr =
	    fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE, NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
	g_assert_true(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_pci_bcr_plugin_ble_enabled_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_INTEL);
	plugin = fu_plugin_new_from_gtype(fu_pci_bcr_plugin_get_type(), ctx);

	device = fu_test_pci_bcr_new_emulated_device(ctx, BCR_BLE);
	ret = fu_plugin_runner_backend_device_added(plugin, device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_SPI_BLE, NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	g_assert_true(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_pci_bcr_plugin_ble_not_enabled_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_INTEL);
	plugin = fu_plugin_new_from_gtype(fu_pci_bcr_plugin_get_type(), ctx);

	device = fu_test_pci_bcr_new_emulated_device(ctx, 0);
	ret = fu_plugin_runner_backend_device_added(plugin, device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_SPI_BLE, NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
	g_assert_false(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_pci_bcr_plugin_smm_bwp_locked_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_INTEL);
	plugin = fu_plugin_new_from_gtype(fu_pci_bcr_plugin_get_type(), ctx);

	device = fu_test_pci_bcr_new_emulated_device(ctx, BCR_SMM_BWP);
	ret = fu_plugin_runner_backend_device_added(plugin, device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr =
	    fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_SPI_SMM_BWP, NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_LOCKED);
	g_assert_true(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_pci_bcr_plugin_smm_bwp_not_locked_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_INTEL);
	plugin = fu_plugin_new_from_gtype(fu_pci_bcr_plugin_get_type(), ctx);

	device = fu_test_pci_bcr_new_emulated_device(ctx, 0);
	ret = fu_plugin_runner_backend_device_added(plugin, device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr =
	    fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_SPI_SMM_BWP, NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
	g_assert_false(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_pci_bcr_plugin_all_secure_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr_bioswe = NULL;
	g_autoptr(FwupdSecurityAttr) attr_ble = NULL;
	g_autoptr(FwupdSecurityAttr) attr_smm = NULL;
	g_autoptr(GError) error = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_INTEL);
	plugin = fu_plugin_new_from_gtype(fu_pci_bcr_plugin_get_type(), ctx);

	/* BLE + SMM_BWP set, WPD clear */
	device = fu_test_pci_bcr_new_emulated_device(ctx, BCR_BLE | BCR_SMM_BWP);
	ret = fu_plugin_runner_backend_device_added(plugin, device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);

	attr_bioswe =
	    fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE, NULL);
	g_assert_nonnull(attr_bioswe);
	g_assert_cmpint(fwupd_security_attr_get_result(attr_bioswe),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
	g_assert_true(fwupd_security_attr_has_flag(attr_bioswe, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));

	attr_ble =
	    fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_SPI_BLE, NULL);
	g_assert_nonnull(attr_ble);
	g_assert_cmpint(fwupd_security_attr_get_result(attr_ble),
			==,
			FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	g_assert_true(fwupd_security_attr_has_flag(attr_ble, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));

	attr_smm =
	    fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_SPI_SMM_BWP, NULL);
	g_assert_nonnull(attr_smm);
	g_assert_cmpint(fwupd_security_attr_get_result(attr_smm),
			==,
			FWUPD_SECURITY_ATTR_RESULT_LOCKED);
	g_assert_true(fwupd_security_attr_has_flag(attr_smm, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);

	/* no device tests */
	g_test_add_func("/pci-bcr/no-device", fu_pci_bcr_plugin_no_device_func);
	g_test_add_func("/pci-bcr/not-intel", fu_pci_bcr_plugin_not_intel_func);

	/* BIOSWE tests */
	g_test_add_func("/pci-bcr/bioswe/enabled", fu_pci_bcr_plugin_bioswe_enabled_func);
	g_test_add_func("/pci-bcr/bioswe/not-enabled", fu_pci_bcr_plugin_bioswe_not_enabled_func);

	/* BLE tests */
	g_test_add_func("/pci-bcr/ble/enabled", fu_pci_bcr_plugin_ble_enabled_func);
	g_test_add_func("/pci-bcr/ble/not-enabled", fu_pci_bcr_plugin_ble_not_enabled_func);

	/* SMM_BWP tests */
	g_test_add_func("/pci-bcr/smm-bwp/locked", fu_pci_bcr_plugin_smm_bwp_locked_func);
	g_test_add_func("/pci-bcr/smm-bwp/not-locked", fu_pci_bcr_plugin_smm_bwp_not_locked_func);

	/* combined test */
	g_test_add_func("/pci-bcr/all-secure", fu_pci_bcr_plugin_all_secure_func);

	return g_test_run();
}
