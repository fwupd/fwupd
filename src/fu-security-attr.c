/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include <config.h>
#include <glib/gi18n.h>

#include "fu-security-attr.h"

gchar *
fu_security_attr_get_name (FwupdSecurityAttr *attr)
{
	const gchar *appstream_id = fwupd_security_attr_get_appstream_id (attr);
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE) == 0) {
		/* TRANSLATORS: Title: SPI refers to the flash chip in the computer */
		return g_strdup (_("SPI write"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_SPI_BLE) == 0) {
		/* TRANSLATORS: Title: SPI refers to the flash chip in the computer */
		return g_strdup (_("SPI lock"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_SPI_SMM_BWP) == 0) {
		/* TRANSLATORS: Title: SPI refers to the flash chip in the computer */
		return g_strdup (_("SPI BIOS region"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_ACPI_DMAR) == 0) {
		/* TRANSLATORS: Title: DMA as in https://en.wikipedia.org/wiki/DMA_attack  */
		return g_strdup (_("Pre-boot DMA protection"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ENABLED) == 0) {
		/* TRANSLATORS: Title: BootGuard is a trademark from Intel */
		return g_strdup (_("Intel BootGuard"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_VERIFIED) == 0) {
		/* TRANSLATORS: Title: BootGuard is a trademark from Intel,
		 * verified boot refers to the way the boot process is verified */
		return g_strdup (_("Intel BootGuard verified boot"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ACM) == 0) {
		/* TRANSLATORS: Title: BootGuard is a trademark from Intel,
		 * ACM means to verify the integrity of Initial Boot Block */
		return g_strdup (_("Intel BootGuard ACM protected"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_POLICY) == 0) {
		/* TRANSLATORS: Title: BootGuard is a trademark from Intel,
		 * error policy is what to do on failure */
		return g_strdup (_("Intel BootGuard error policy"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_OTP) == 0) {
		/* TRANSLATORS: Title: BootGuard is a trademark from Intel,
		 * OTP = one time programmable */
		return g_strdup (_("Intel BootGuard OTP fuse"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_CET_ENABLED) == 0) {
		/* TRANSLATORS: Title: CET = Control-flow Enforcement Technology,
		 * enabled means supported by the processor */
		return g_strdup (_("Intel CET Enabled"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_CET_ACTIVE) == 0) {
		/* TRANSLATORS: Title: CET = Control-flow Enforcement Technology,
		 * active means being used by the OS */
		return g_strdup (_("Intel CET Active"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_SMAP) == 0) {
		/* TRANSLATORS: Title: SMAP = Supervisor Mode Access Prevention */
		return g_strdup (_("Intel SMAP"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM) == 0) {
		/* TRANSLATORS: Title: Memory contents are encrypted, e.g. Intel TME */
		return g_strdup (_("Encrypted RAM"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_IOMMU) == 0) {
		/* TRANSLATORS: Title: https://en.wikipedia.org/wiki/Input%E2%80%93output_memory_management_unit */
		return g_strdup (_("IOMMU"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_KERNEL_LOCKDOWN) == 0) {
		/* TRANSLATORS: Title: lockdown is a security mode of the kernel */
		return g_strdup (_("Linux kernel lockdown"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_KERNEL_TAINTED) == 0) {
		/* TRANSLATORS: Title: if it's tainted or not */
		return g_strdup (_("Linux kernel"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_KERNEL_SWAP) == 0) {
		/* TRANSLATORS: Title: swap space or swap partition */
		return g_strdup (_("Linux swap"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM) == 0) {
		/* TRANSLATORS: Title: sleep state */
		return g_strdup (_("Suspend-to-ram"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_IDLE) == 0) {
		/* TRANSLATORS: Title: a better sleep state */
		return g_strdup (_("Suspend-to-idle"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_UEFI_DBX) == 0) {
		/* TRANSLATORS: Title: dbx is the database with revoked hashes */
		return g_strdup (_("UEFI dbx update"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_UEFI_DBX_ESP) == 0) {
		/* TRANSLATORS: Title: ESP is the name of the EFI System Partition
		 * and dbx is the database with revoked hashes */
		return g_strdup (_("UEFI dbx in ESP"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT) == 0) {
		/* TRANSLATORS: Title: SB is a way of locking down UEFI */
		return g_strdup (_("UEFI secure boot"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_TPM_RECONSTRUCTION_PCR0) == 0) {
		/* TRANSLATORS: Title: the PCR is rebuilt from the TPM event log */
		return g_strdup (_("TPM PCR0 reconstruction"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_TPM_VERSION_20) == 0) {
		/* TRANSLATORS: Title: TPM = Trusted Platform Module */
		return g_strdup (_("TPM v2.0"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE) == 0) {
		const gchar *kind = fwupd_security_attr_get_metadata (attr, "kind");
		if (kind != NULL) {
			/* TRANSLATORS: Title: %s is ME kind, e.g. CSME/TXT */
			return g_strdup_printf (_("%s manufacturing mode"), kind);
		}
		/* TRANSLATORS: Title: MEI = Intel Management Engine */
		return g_strdup (_("MEI manufacturing mode"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_MEI_OVERRIDE_STRAP) == 0) {
		const gchar *kind = fwupd_security_attr_get_metadata (attr, "kind");
		if (kind != NULL) {
			/* TRANSLATORS: Title: %s is ME kind, e.g. CSME/TXT */
			return g_strdup_printf (_("%s override"), kind);
		}
		/* TRANSLATORS: Title: MEI = Intel Management Engine, and the
		 * "override" is the physical PIN that can be driven to
		 * logic high -- luckily it is probably not accessible to
		 * end users on consumer boards */
		return g_strdup (_("MEI override"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_MEI_VERSION) == 0) {
		/* TRANSLATORS: Title: MEI = Intel Management Engine */
		const gchar *kind = fwupd_security_attr_get_metadata (attr, "kind");
		const gchar *version = fwupd_security_attr_get_metadata (attr, "version");
		if (kind != NULL && version != NULL) {
			/* TRANSLATORS: Title: %1 is ME kind, e.g. CSME/TXT, %2 is a version number */
			return g_strdup_printf (_("%s v%s"), kind, version);
		}
		if (kind != NULL) {
			/* TRANSLATORS: Title: %s is ME kind, e.g. CSME/TXT */
			return g_strdup_printf (_("%s version"), kind);
		}
		return g_strdup (_("MEI version"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_FWUPD_UPDATES) == 0) {
		/* TRANSLATORS: Title: if firmware updates are available */
		return g_strdup (_("Firmware updates"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_FWUPD_ATTESTATION) == 0) {
		/* TRANSLATORS: Title: if we can verify the firmware checksums */
		return g_strdup (_("Firmware attestation"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_FWUPD_PLUGINS) == 0) {
		/* TRANSLATORS: Title: if the fwupd plugins are all present and correct */
		return g_strdup (_("fwupd plugins"));
	}
	if (g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_DCI_ENABLED) == 0 ||
	    g_strcmp0 (appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_DCI_LOCKED) == 0) {
		/* TRANSLATORS: Title: Direct Connect Interface (DCI) allows
		 * debugging of Intel processors using the USB3 port */
		return g_strdup (_("Intel DCI debugger"));
	}

	/* we should not get here */
	return g_strdup (fwupd_security_attr_get_name (attr));
}

const gchar *
fu_security_attr_get_result (FwupdSecurityAttr *attr)
{
	FwupdSecurityAttrResult result = fwupd_security_attr_get_result (attr);
	if (result == FWUPD_SECURITY_ATTR_RESULT_VALID) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Valid");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_VALID) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Invalid");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_ENABLED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Enabled");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Disabled");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_LOCKED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Locked");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Unlocked");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_ENCRYPTED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Encrypted");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_ENCRYPTED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Unencrypted");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_TAINTED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Tainted");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_TAINTED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Untainted");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_FOUND) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Found");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Not found");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_SUPPORTED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Supported");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Not supported");
	}

	/* fallback */
	if (fwupd_security_attr_has_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS)) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("OK");
	}

	/* TRANSLATORS: Suffix: the fallback HSI result */
	return _("Failed");
}
