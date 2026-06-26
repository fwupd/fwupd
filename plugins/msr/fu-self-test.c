/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-msr-plugin.h"
#include "fu-plugin-private.h"
#include "fu-security-attrs-private.h"
#include "fu-udev-device-private.h"

#define PCI_MSR_IA32_DEBUG_INTERFACE 0xc80
#define PCI_MSR_IA32_TME_ACTIVATION  0x982
#define PCI_MSR_AMD64_SYSCFG	     0xC0010010
#define PCI_MSR_AMD64_HWCFG	     0xc0010015

static void
fu_test_msr_device_add_pread(FuDevice *device, /* nocheck:name */
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
fu_test_msr_new_emulated_device(FuContext *ctx)
{
	FuDevice *device;
	device = g_object_new(FU_TYPE_UDEV_DEVICE, "context", ctx, NULL);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_EMULATED);
	fu_device_set_backend_id(device, "/sys/devices/virtual/msr/msr0");
	fu_udev_device_set_subsystem(FU_UDEV_DEVICE(device), "msr");
	return device;
}

static void
fu_msr_plugin_intel_dci_not_supported_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_INTEL);
	plugin = fu_plugin_new_from_gtype(fu_msr_plugin_get_type(), ctx);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_ENABLED,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
	g_assert_true(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_msr_plugin_intel_dci_enabled_func(void)
{
	gboolean ret;
	guint8 buf[8] = {0x0};
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_INTEL);
	plugin = fu_plugin_new_from_gtype(fu_msr_plugin_get_type(), ctx);
	fu_plugin_add_private_flag(plugin, "ia32-debug");

	/* enabled=1 (bit 0) */
	fu_memwrite_uint32(buf, 0x1, G_LITTLE_ENDIAN);
	device = fu_test_msr_new_emulated_device(ctx);
	fu_test_msr_device_add_pread(device, PCI_MSR_IA32_DEBUG_INTERFACE, buf, sizeof(buf));
	ret = fu_plugin_runner_backend_device_added(plugin, device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_ENABLED,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	g_assert_false(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_msr_plugin_intel_dci_not_enabled_func(void)
{
	gboolean ret;
	guint8 buf[8] = {0x0};
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_INTEL);
	plugin = fu_plugin_new_from_gtype(fu_msr_plugin_get_type(), ctx);
	fu_plugin_add_private_flag(plugin, "ia32-debug");

	/* enabled=0, locked=0 */
	device = fu_test_msr_new_emulated_device(ctx);
	fu_test_msr_device_add_pread(device, PCI_MSR_IA32_DEBUG_INTERFACE, buf, sizeof(buf));
	ret = fu_plugin_runner_backend_device_added(plugin, device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_ENABLED,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
	g_assert_true(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_msr_plugin_intel_dci_locked_func(void)
{
	gboolean ret;
	guint8 buf[8] = {0x0};
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_INTEL);
	plugin = fu_plugin_new_from_gtype(fu_msr_plugin_get_type(), ctx);
	fu_plugin_add_private_flag(plugin, "ia32-debug");

	/* locked=1 (bit 30) */
	fu_memwrite_uint32(buf, 1U << 30, G_LITTLE_ENDIAN);
	device = fu_test_msr_new_emulated_device(ctx);
	fu_test_msr_device_add_pread(device, PCI_MSR_IA32_DEBUG_INTERFACE, buf, sizeof(buf));
	ret = fu_plugin_runner_backend_device_added(plugin, device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_LOCKED,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_LOCKED);
	g_assert_true(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_msr_plugin_intel_dci_not_locked_func(void)
{
	gboolean ret;
	guint8 buf[8] = {0x0};
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_INTEL);
	plugin = fu_plugin_new_from_gtype(fu_msr_plugin_get_type(), ctx);
	fu_plugin_add_private_flag(plugin, "ia32-debug");

	/* locked=0 */
	device = fu_test_msr_new_emulated_device(ctx);
	fu_test_msr_device_add_pread(device, PCI_MSR_IA32_DEBUG_INTERFACE, buf, sizeof(buf));
	ret = fu_plugin_runner_backend_device_added(plugin, device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_LOCKED,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
	g_assert_false(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_msr_plugin_intel_tme_not_supported_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_INTEL);
	plugin = fu_plugin_new_from_gtype(fu_msr_plugin_get_type(), ctx);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
}

static void
fu_msr_plugin_intel_tme_not_enabled_func(void)
{
	gboolean ret;
	guint8 buf[8] = {0x0};
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_INTEL);
	plugin = fu_plugin_new_from_gtype(fu_msr_plugin_get_type(), ctx);
	fu_plugin_add_private_flag(plugin, "ia32-tme");

	/* enable=0 (bit 1) */
	device = fu_test_msr_new_emulated_device(ctx);
	fu_test_msr_device_add_pread(device, PCI_MSR_IA32_TME_ACTIVATION, buf, sizeof(buf));
	ret = fu_plugin_runner_backend_device_added(plugin, device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
}

static void
fu_msr_plugin_intel_tme_bypass_enabled_func(void)
{
	gboolean ret;
	guint8 buf[8] = {0x0};
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_INTEL);
	plugin = fu_plugin_new_from_gtype(fu_msr_plugin_get_type(), ctx);
	fu_plugin_add_private_flag(plugin, "ia32-tme");

	/* enable=1 (bit 1), bypass_enable=1 (bit 31) */
	fu_memwrite_uint64(buf, (1ULL << 1) | (1ULL << 31), G_LITTLE_ENDIAN);
	device = fu_test_msr_new_emulated_device(ctx);
	fu_test_msr_device_add_pread(device, PCI_MSR_IA32_TME_ACTIVATION, buf, sizeof(buf));
	ret = fu_plugin_runner_backend_device_added(plugin, device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
	g_assert_false(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_msr_plugin_intel_tme_not_locked_func(void)
{
	gboolean ret;
	guint8 buf[8] = {0x0};
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_INTEL);
	plugin = fu_plugin_new_from_gtype(fu_msr_plugin_get_type(), ctx);
	fu_plugin_add_private_flag(plugin, "ia32-tme");

	/* enable=1 (bit 1), bypass_enable=0, lock_ro=0 (bit 0) */
	fu_memwrite_uint64(buf, 1ULL << 1, G_LITTLE_ENDIAN);
	device = fu_test_msr_new_emulated_device(ctx);
	fu_test_msr_device_add_pread(device, PCI_MSR_IA32_TME_ACTIVATION, buf, sizeof(buf));
	ret = fu_plugin_runner_backend_device_added(plugin, device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
}

static void
fu_msr_plugin_amd_no_intel_attrs_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr_dci_en = NULL;
	g_autoptr(FwupdSecurityAttr) attr_dci_lk = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_AMD);
	plugin = fu_plugin_new_from_gtype(fu_msr_plugin_get_type(), ctx);
	fu_plugin_runner_add_security_attrs(plugin, attrs);

	attr_dci_en =
	    fu_security_attrs_get_by_appstream_id(attrs,
						  FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_ENABLED,
						  NULL);
	g_assert_null(attr_dci_en);

	attr_dci_lk =
	    fu_security_attrs_get_by_appstream_id(attrs,
						  FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_LOCKED,
						  NULL);
	g_assert_null(attr_dci_lk);
}

static void
fu_msr_plugin_amd_sme_not_supported_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_AMD);
	plugin = fu_plugin_new_from_gtype(fu_msr_plugin_get_type(), ctx);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
}

static void
fu_msr_plugin_amd_sme_not_encrypted_func(void)
{
	gboolean ret;
	guint8 buf[8] = {0x0};
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_AMD);
	plugin = fu_plugin_new_from_gtype(fu_msr_plugin_get_type(), ctx);
	fu_plugin_add_private_flag(plugin, "amd64-syscfg");

	/* sme_is_enabled=0 (bit 23) */
	device = fu_test_msr_new_emulated_device(ctx);
	fu_test_msr_device_add_pread(device, PCI_MSR_AMD64_SYSCFG, buf, sizeof(buf));
	ret = fu_plugin_runner_backend_device_added(plugin, device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
	g_assert_false(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_msr_plugin_amd_hwcr_not_supported_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_AMD);
	plugin = fu_plugin_new_from_gtype(fu_msr_plugin_get_type(), ctx);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_AMD_SMM_LOCKED,
						     NULL);
	g_assert_null(attr);
}

static void
fu_msr_plugin_amd_hwcr_locked_func(void)
{
	gboolean ret;
	guint8 buf[8] = {0x0};
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_AMD);
	plugin = fu_plugin_new_from_gtype(fu_msr_plugin_get_type(), ctx);
	fu_plugin_add_private_flag(plugin, "amd64-hwcfg");

	/* smm_locked=1 (bit 0), smm_base_lock=1 (bit 31) */
	fu_memwrite_uint64(buf, (1ULL << 0) | (1ULL << 31), G_LITTLE_ENDIAN);
	device = fu_test_msr_new_emulated_device(ctx);
	fu_test_msr_device_add_pread(device, PCI_MSR_AMD64_HWCFG, buf, sizeof(buf));
	ret = fu_plugin_runner_backend_device_added(plugin, device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_AMD_SMM_LOCKED,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_LOCKED);
	g_assert_true(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_msr_plugin_amd_hwcr_not_locked_func(void)
{
	gboolean ret;
	guint8 buf[8] = {0x0};
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_AMD);
	plugin = fu_plugin_new_from_gtype(fu_msr_plugin_get_type(), ctx);
	fu_plugin_add_private_flag(plugin, "amd64-hwcfg");

	/* smm_locked=0 (bit 0), smm_base_lock=1 (bit 31) */
	fu_memwrite_uint64(buf, 1ULL << 31, G_LITTLE_ENDIAN);
	device = fu_test_msr_new_emulated_device(ctx);
	fu_test_msr_device_add_pread(device, PCI_MSR_AMD64_HWCFG, buf, sizeof(buf));
	ret = fu_plugin_runner_backend_device_added(plugin, device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_AMD_SMM_LOCKED,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
	g_assert_false(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_msr_plugin_amd_hwcr_base_not_locked_func(void)
{
	gboolean ret;
	guint8 buf[8] = {0x0};
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	fu_context_set_cpu_vendor(ctx, FU_CPU_VENDOR_AMD);
	plugin = fu_plugin_new_from_gtype(fu_msr_plugin_get_type(), ctx);
	fu_plugin_add_private_flag(plugin, "amd64-hwcfg");

	/* smm_locked=1 (bit 0), smm_base_lock=0 (bit 31) */
	fu_memwrite_uint64(buf, 1ULL << 0, G_LITTLE_ENDIAN);
	device = fu_test_msr_new_emulated_device(ctx);
	fu_test_msr_device_add_pread(device, PCI_MSR_AMD64_HWCFG, buf, sizeof(buf));
	ret = fu_plugin_runner_backend_device_added(plugin, device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_AMD_SMM_LOCKED,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
	g_assert_false(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);

	/* intel DCI tests */
	g_test_add_func("/msr/intel/dci/not-supported", fu_msr_plugin_intel_dci_not_supported_func);
	g_test_add_func("/msr/intel/dci/enabled", fu_msr_plugin_intel_dci_enabled_func);
	g_test_add_func("/msr/intel/dci/not-enabled", fu_msr_plugin_intel_dci_not_enabled_func);
	g_test_add_func("/msr/intel/dci/locked", fu_msr_plugin_intel_dci_locked_func);
	g_test_add_func("/msr/intel/dci/not-locked", fu_msr_plugin_intel_dci_not_locked_func);

	/* intel TME tests */
	g_test_add_func("/msr/intel/tme/not-supported", fu_msr_plugin_intel_tme_not_supported_func);
	g_test_add_func("/msr/intel/tme/not-enabled", fu_msr_plugin_intel_tme_not_enabled_func);
	g_test_add_func("/msr/intel/tme/bypass-enabled",
			fu_msr_plugin_intel_tme_bypass_enabled_func);
	g_test_add_func("/msr/intel/tme/not-locked", fu_msr_plugin_intel_tme_not_locked_func);

	/* AMD/Intel vendor filtering */
	g_test_add_func("/msr/amd/no-intel-attrs", fu_msr_plugin_amd_no_intel_attrs_func);

	/* AMD SME tests */
	g_test_add_func("/msr/amd/sme/not-supported", fu_msr_plugin_amd_sme_not_supported_func);
	g_test_add_func("/msr/amd/sme/not-encrypted", fu_msr_plugin_amd_sme_not_encrypted_func);

	/* AMD HWCR tests */
	g_test_add_func("/msr/amd/hwcr/not-supported", fu_msr_plugin_amd_hwcr_not_supported_func);
	g_test_add_func("/msr/amd/hwcr/locked", fu_msr_plugin_amd_hwcr_locked_func);
	g_test_add_func("/msr/amd/hwcr/not-locked", fu_msr_plugin_amd_hwcr_not_locked_func);
	g_test_add_func("/msr/amd/hwcr/base-not-locked",
			fu_msr_plugin_amd_hwcr_base_not_locked_func);

	return g_test_run();
}
