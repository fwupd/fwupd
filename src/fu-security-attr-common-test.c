/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-security-attr-common.h"

static void
fu_security_attr_common_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	const gchar *appstream_ids[] = {
	    FWUPD_SECURITY_ATTR_ID_AMD_ENTRY_SIGN,
	    FWUPD_SECURITY_ATTR_ID_AMD_PLATFORM_SECURE_BOOT,
	    FWUPD_SECURITY_ATTR_ID_AMD_ROLLBACK_PROTECTION,
	    FWUPD_SECURITY_ATTR_ID_AMD_SMM_LOCKED,
	    FWUPD_SECURITY_ATTR_ID_AMD_SPI_REPLAY_PROTECTION,
	    FWUPD_SECURITY_ATTR_ID_AMD_SPI_WRITE_PROTECTION,
	    FWUPD_SECURITY_ATTR_ID_BIOS_CAPSULE_UPDATES,
	    FWUPD_SECURITY_ATTR_ID_BIOS_ROLLBACK_PROTECTION,
	    FWUPD_SECURITY_ATTR_ID_CET_ACTIVE,
	    FWUPD_SECURITY_ATTR_ID_CET_ENABLED,
	    FWUPD_SECURITY_ATTR_ID_COREBOOT_VBOOT,
	    FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM,
	    FWUPD_SECURITY_ATTR_ID_FWUPD_ATTESTATION,
	    FWUPD_SECURITY_ATTR_ID_FWUPD_PLUGINS,
	    FWUPD_SECURITY_ATTR_ID_FWUPD_UPDATES,
	    FWUPD_SECURITY_ATTR_ID_HOST_EMULATION,
	    FWUPD_SECURITY_ATTR_ID_HP_SURESTART,
	    FWUPD_SECURITY_ATTR_ID_HW_DISK_ENCRYPTION,
	    FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ACM,
	    FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ENABLED,
	    FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_OTP,
	    FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_POLICY,
	    FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_VERIFIED,
	    FWUPD_SECURITY_ATTR_ID_INTEL_GDS,
	    FWUPD_SECURITY_ATTR_ID_IOMMU,
	    FWUPD_SECURITY_ATTR_ID_KERNEL_LOCKDOWN,
	    FWUPD_SECURITY_ATTR_ID_KERNEL_SWAP,
	    FWUPD_SECURITY_ATTR_ID_KERNEL_TAINTED,
	    FWUPD_SECURITY_ATTR_ID_MEI_KEY_MANIFEST,
	    FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE,
	    FWUPD_SECURITY_ATTR_ID_MEI_OVERRIDE_STRAP,
	    FWUPD_SECURITY_ATTR_ID_MEI_VERSION,
	    FWUPD_SECURITY_ATTR_ID_MTD_LOCKED,
	    FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_ENABLED,
	    FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_LOCKED,
	    FWUPD_SECURITY_ATTR_ID_PLATFORM_FUSED,
	    FWUPD_SECURITY_ATTR_ID_PREBOOT_DMA_PROTECTION,
	    FWUPD_SECURITY_ATTR_ID_SMAP,
	    FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE,
	    FWUPD_SECURITY_ATTR_ID_SPI_BLE,
	    FWUPD_SECURITY_ATTR_ID_SPI_DESCRIPTOR,
	    FWUPD_SECURITY_ATTR_ID_SPI_SMM_BWP,
	    FWUPD_SECURITY_ATTR_ID_SUPPORTED_CPU,
	    FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_IDLE,
	    FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM,
	    FWUPD_SECURITY_ATTR_ID_TPM_EMPTY_PCR,
	    FWUPD_SECURITY_ATTR_ID_TPM_RECONSTRUCTION_PCR0,
	    FWUPD_SECURITY_ATTR_ID_TPM_VERSION_20,
	    FWUPD_SECURITY_ATTR_ID_UEFI_BOOTSERVICE_VARS,
	    FWUPD_SECURITY_ATTR_ID_UEFI_DB_MS_UEFI,
	    FWUPD_SECURITY_ATTR_ID_UEFI_DB_PRODUCTION,
	    FWUPD_SECURITY_ATTR_ID_UEFI_MEMORY_PROTECTION,
	    FWUPD_SECURITY_ATTR_ID_UEFI_NX_COMPAT,
	    FWUPD_SECURITY_ATTR_ID_UEFI_PK,
	    FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT,
	};

	/* every known appstream-id has a translated name; most also have a title
	 * and description (exercise those branches too) */
	for (guint i = 0; i < G_N_ELEMENTS(appstream_ids); i++) {
		g_autofree gchar *name = NULL;
		g_autoptr(FuSecurityAttr) attr = fu_security_attr_new(ctx, appstream_ids[i]);

		name = fu_security_attr_get_name_translated(attr);
		g_assert_nonnull(name);
		(void)fu_security_attr_get_title_translated(attr);
		(void)fu_security_attr_get_description_translated(attr);
	}
}

static void
fu_security_attr_common_fallback_func(void)
{
	g_autofree gchar *name = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuSecurityAttr) attr = fu_security_attr_new(ctx, "org.fwupd.hsi.UnknownAttr");

	/* an unknown appstream-id falls back to the raw name, and no title or
	 * description */
	fu_security_attr_set_name(attr, "Custom Attr");
	name = fu_security_attr_get_name_translated(attr);
	g_assert_cmpstr(name, ==, "Custom Attr");
	g_assert_null(fu_security_attr_get_title_translated(attr));
	g_assert_null(fu_security_attr_get_description_translated(attr));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/security-attr-common", fu_security_attr_common_func);
	g_test_add_func("/fwupd/security-attr-common/fallback",
			fu_security_attr_common_fallback_func);
	return g_test_run();
}
