/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <glib/gi18n.h>
#include <json-glib/json-glib.h>

#include "fwupd-security-attr-private.h"

#include "fu-security-attr-common.h"
#include "fu-security-attrs-private.h"

gchar *
fu_security_attr_get_name(FwupdSecurityAttr *attr)
{
	const gchar *appstream_id = fwupd_security_attr_get_appstream_id(attr);
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE) == 0) {
		/* TRANSLATORS: Title: SPI refers to the flash chip in the computer */
		return g_strdup(_("SPI write"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SPI_BLE) == 0) {
		/* TRANSLATORS: Title: SPI refers to the flash chip in the computer */
		return g_strdup(_("SPI lock"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SPI_SMM_BWP) == 0) {
		/* TRANSLATORS: Title: SPI refers to the flash chip in the computer */
		return g_strdup(_("SPI BIOS region"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SPI_DESCRIPTOR) == 0) {
		/* TRANSLATORS: Title: SPI refers to the flash chip in the computer */
		return g_strdup(_("SPI BIOS Descriptor"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_PREBOOT_DMA_PROTECTION) == 0) {
		/* TRANSLATORS: Title: DMA as in https://en.wikipedia.org/wiki/DMA_attack  */
		return g_strdup(_("Pre-boot DMA protection"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ENABLED) == 0) {
		/* TRANSLATORS: Title: BootGuard is a trademark from Intel */
		return g_strdup(_("Intel BootGuard"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_VERIFIED) == 0) {
		/* TRANSLATORS: Title: BootGuard is a trademark from Intel,
		 * verified boot refers to the way the boot process is verified */
		return g_strdup(_("Intel BootGuard verified boot"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ACM) == 0) {
		/* TRANSLATORS: Title: BootGuard is a trademark from Intel,
		 * ACM means to verify the integrity of Initial Boot Block */
		return g_strdup(_("Intel BootGuard ACM protected"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_POLICY) == 0) {
		/* TRANSLATORS: Title: BootGuard is a trademark from Intel,
		 * error policy is what to do on failure */
		return g_strdup(_("Intel BootGuard error policy"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_OTP) == 0) {
		/* TRANSLATORS: Title: BootGuard is a trademark from Intel,
		 * OTP = one time programmable */
		return g_strdup(_("Intel BootGuard OTP fuse"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_CET_ENABLED) == 0) {
		/* TRANSLATORS: Title: CET = Control-flow Enforcement Technology,
		 * enabled means supported by the processor */
		return g_strdup(_("Intel CET Enabled"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_CET_ACTIVE) == 0) {
		/* TRANSLATORS: Title: CET = Control-flow Enforcement Technology,
		 * active means being used by the OS */
		return g_strdup(_("Intel CET Active"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_SMAP) == 0) {
		/* TRANSLATORS: Title: SMAP = Supervisor Mode Access Prevention */
		return g_strdup(_("Intel SMAP"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM) == 0) {
		/* TRANSLATORS: Title: Memory contents are encrypted, e.g. Intel TME */
		return g_strdup(_("Encrypted RAM"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_IOMMU) == 0) {
		/* TRANSLATORS: Title:
		 * https://en.wikipedia.org/wiki/Input%E2%80%93output_memory_management_unit */
		return g_strdup(_("IOMMU"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_KERNEL_LOCKDOWN) == 0) {
		/* TRANSLATORS: Title: lockdown is a security mode of the kernel */
		return g_strdup(_("Linux kernel lockdown"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_KERNEL_TAINTED) == 0) {
		/* TRANSLATORS: Title: if it's tainted or not */
		return g_strdup(_("Linux kernel"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_KERNEL_SWAP) == 0) {
		/* TRANSLATORS: Title: swap space or swap partition */
		return g_strdup(_("Linux swap"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM) == 0) {
		/* TRANSLATORS: Title: sleep state */
		return g_strdup(_("Suspend-to-ram"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_IDLE) == 0) {
		/* TRANSLATORS: Title: a better sleep state */
		return g_strdup(_("Suspend-to-idle"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_UEFI_PK) == 0) {
		/* TRANSLATORS: Title: PK is the 'platform key' for the machine */
		return g_strdup(_("UEFI platform key"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT) == 0) {
		/* TRANSLATORS: Title: SB is a way of locking down UEFI */
		return g_strdup(_("UEFI secure boot"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_UEFI_BOOTSERVICE_VARS) == 0) {
		/* TRANSLATORS: Title: Bootservice is when only readable from early-boot */
		return g_strdup(_("UEFI bootservice variables"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_TPM_EMPTY_PCR) == 0) {
		/* TRANSLATORS: Title: PCRs (Platform Configuration Registers) shouldn't be empty */
		return g_strdup(_("TPM empty PCRs"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_TPM_RECONSTRUCTION_PCR0) == 0) {
		/* TRANSLATORS: Title: the PCR is rebuilt from the TPM event log */
		return g_strdup(_("TPM PCR0 reconstruction"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_TPM_VERSION_20) == 0) {
		/* TRANSLATORS: Title: TPM = Trusted Platform Module */
		return g_strdup(_("TPM v2.0"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE) == 0) {
		const gchar *kind = fwupd_security_attr_get_metadata(attr, "kind");
		if (kind != NULL) {
			/* TRANSLATORS: Title: %s is ME kind, e.g. CSME/TXT */
			return g_strdup_printf(_("%s manufacturing mode"), kind);
		}
		/* TRANSLATORS: Title: MEI = Intel Management Engine */
		return g_strdup(_("MEI manufacturing mode"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_MEI_OVERRIDE_STRAP) == 0) {
		const gchar *kind = fwupd_security_attr_get_metadata(attr, "kind");
		if (kind != NULL) {
			/* TRANSLATORS: Title: %s is ME kind, e.g. CSME/TXT */
			return g_strdup_printf(_("%s override"), kind);
		}
		/* TRANSLATORS: Title: MEI = Intel Management Engine, and the
		 * "override" is the physical PIN that can be driven to
		 * logic high -- luckily it is probably not accessible to
		 * end users on consumer boards */
		return g_strdup(_("MEI override"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_MEI_KEY_MANIFEST) == 0) {
		/* TRANSLATORS: Title: MEI = Intel Management Engine, and key refer
		 * to the private/public key used to secure loading of firmware */
		return g_strdup(_("MEI key manifest"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_MEI_VERSION) == 0) {
		/* TRANSLATORS: Title: MEI = Intel Management Engine */
		const gchar *kind = fwupd_security_attr_get_metadata(attr, "kind");
		const gchar *version = fwupd_security_attr_get_metadata(attr, "version");
		if (kind != NULL && version != NULL) {
			/* TRANSLATORS: Title: %1 is ME kind, e.g. CSME/TXT, %2 is a version number
			 */
			return g_strdup_printf(_("%s v%s"), kind, version);
		}
		if (kind != NULL) {
			/* TRANSLATORS: Title: %s is ME kind, e.g. CSME/TXT */
			return g_strdup_printf(_("%s version"), kind);
		}
		return g_strdup(_("MEI version"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_FWUPD_UPDATES) == 0) {
		/* TRANSLATORS: Title: if firmware updates are available */
		return g_strdup(_("Firmware updates"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_FWUPD_ATTESTATION) == 0) {
		/* TRANSLATORS: Title: if we can verify the firmware checksums */
		return g_strdup(_("Firmware attestation"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_FWUPD_PLUGINS) == 0) {
		/* TRANSLATORS: Title: if the fwupd plugins are all present and correct */
		return g_strdup(_("fwupd plugins"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_ENABLED) == 0 ||
	    g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_LOCKED) == 0) {
		/* TRANSLATORS: Title: Allows debugging of parts using proprietary hardware */
		return g_strdup(_("Platform debugging"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SUPPORTED_CPU) == 0) {
		/* TRANSLATORS: Title: if fwupd supports HSI on this chip */
		return g_strdup(_("Supported CPU"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_AMD_ROLLBACK_PROTECTION) == 0) {
		/* TRANSLATORS: Title: if firmware enforces rollback protection */
		return g_strdup(_("Processor rollback protection"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_AMD_SPI_REPLAY_PROTECTION) == 0) {
		/* TRANSLATORS: Title: if hardware enforces control of SPI replays */
		return g_strdup(_("SPI replay protection"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_AMD_SPI_WRITE_PROTECTION) == 0) {
		/* TRANSLATORS: Title: if hardware enforces control of SPI writes */
		return g_strdup(_("SPI write protection"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_PLATFORM_FUSED) == 0) {
		/* TRANSLATORS: Title: if the part has been fused */
		return g_strdup(_("Fused platform"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_HOST_EMULATION) == 0) {
		/* TRANSLATORS: Title: if we are emulating a different host */
		return g_strdup(_("Emulated host"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_BIOS_ROLLBACK_PROTECTION) == 0) {
		/* TRANSLATORS: Title: if firmware enforces rollback protection */
		return g_strdup(_("BIOS rollback protection"));
	}
	/* we should not get here */
	return g_strdup(fwupd_security_attr_get_name(attr));
}

const gchar *
fu_security_attr_get_title(FwupdSecurityAttr *attr)
{
	const gchar *appstream_id = fwupd_security_attr_get_appstream_id(attr);
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE) == 0) {
		/* TRANSLATORS: Title: firmware refers to the flash chip in the computer */
		return _("Firmware Write Protection");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SPI_BLE) == 0) {
		/* TRANSLATORS: Title: firmware refers to the flash chip in the computer */
		return _("Firmware Write Protection Lock");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SPI_SMM_BWP) == 0) {
		/* TRANSLATORS: Title: SPI refers to the flash chip in the computer */
		return _("Firmware BIOS Region");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SPI_DESCRIPTOR) == 0) {
		/* TRANSLATORS: Title: firmware refers to the flash chip in the computer */
		return _("Firmware BIOS Descriptor");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_PREBOOT_DMA_PROTECTION) == 0) {
		/* TRANSLATORS: Title: DMA as in https://en.wikipedia.org/wiki/DMA_attack  */
		return _("Pre-boot DMA Protection");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ENABLED) == 0) {
		/* TRANSLATORS: Title: BootGuard is a trademark from Intel */
		return _("Intel BootGuard");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_VERIFIED) == 0) {
		/* TRANSLATORS: Title: BootGuard is a trademark from Intel,
		 * verified boot refers to the way the boot process is verified */
		return _("Intel BootGuard Verified Boot");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ACM) == 0) {
		/* TRANSLATORS: Title: BootGuard is a trademark from Intel,
		 * ACM means to verify the integrity of Initial Boot Block */
		return _("Intel BootGuard ACM Protected");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_POLICY) == 0) {
		/* TRANSLATORS: Title: BootGuard is a trademark from Intel,
		 * error policy is what to do on failure */
		return _("Intel BootGuard Error Policy");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_OTP) == 0) {
		/* TRANSLATORS: Title: BootGuard is a trademark from Intel */
		return _("Intel BootGuard Fuse");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_CET_ENABLED) == 0 ||
	    g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_CET_ACTIVE) == 0) {
		/* TRANSLATORS: Title: CET = Control-flow Enforcement Technology */
		return _("Intel CET");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_SMAP) == 0) {
		/* TRANSLATORS: Title: SMAP = Supervisor Mode Access Prevention */
		return _("Intel SMAP");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM) == 0) {
		/* TRANSLATORS: Title: Memory contents are encrypted, e.g. Intel TME */
		return _("Encrypted RAM");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_IOMMU) == 0) {
		/* TRANSLATORS: Title:
		 * https://en.wikipedia.org/wiki/Input%E2%80%93output_memory_management_unit */
		return _("IOMMU Protection");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_KERNEL_LOCKDOWN) == 0) {
		/* TRANSLATORS: Title: lockdown is a security mode of the kernel */
		return _("Linux Kernel Lockdown");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_KERNEL_TAINTED) == 0) {
		/* TRANSLATORS: Title: if it's tainted or not */
		return _("Linux Kernel Verification");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_KERNEL_SWAP) == 0) {
		/* TRANSLATORS: Title: swap space or swap partition */
		return _("Linux Swap");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM) == 0) {
		/* TRANSLATORS: Title: sleep state */
		return _("Suspend To RAM");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_IDLE) == 0) {
		/* TRANSLATORS: Title: a better sleep state */
		return _("Suspend To Idle");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_UEFI_PK) == 0) {
		/* TRANSLATORS: Title: PK is the 'platform key' for the machine */
		return _("UEFI Platform Key");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT) == 0) {
		/* TRANSLATORS: Title: SB is a way of locking down UEFI */
		return _("UEFI Secure Boot");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_UEFI_BOOTSERVICE_VARS) == 0) {
		/* TRANSLATORS: Title: Bootservice is when only readable from early-boot */
		return _("UEFI Bootservice Variables");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_TPM_EMPTY_PCR) == 0) {
		/* TRANSLATORS: Title: PCRs (Platform Configuration Registers) shouldn't be empty */
		return _("TPM Platform Configuration");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_TPM_RECONSTRUCTION_PCR0) == 0) {
		/* TRANSLATORS: Title: the PCR is rebuilt from the TPM event log */
		return _("TPM Reconstruction");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_TPM_VERSION_20) == 0) {
		/* TRANSLATORS: Title: TPM = Trusted Platform Module */
		return _("TPM v2.0");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE) == 0) {
		/* TRANSLATORS: Title: MEI = Intel Management Engine */
		return _("Intel Management Engine Manufacturing Mode");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_MEI_OVERRIDE_STRAP) == 0) {
		/* TRANSLATORS: Title: MEI = Intel Management Engine, and the "override" is enabled
		 * with a jumper -- luckily it is probably not accessible to end users on consumer
		 * boards */
		return _("Intel Management Engine Override");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_MEI_KEY_MANIFEST) == 0) {
		/* TRANSLATORS: Title: MEI = Intel Management Engine, and key refers
		 * to the private/public key used to secure loading of firmware */
		return _("MEI Key Manifest");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_MEI_VERSION) == 0) {
		/* TRANSLATORS: Title: MEI = Intel Management Engine */
		return _("Intel Management Engine Version");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_FWUPD_UPDATES) == 0) {
		/* TRANSLATORS: Title: if firmware updates are available */
		return _("Firmware Updates");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_FWUPD_ATTESTATION) == 0) {
		/* TRANSLATORS: Title: if we can verify the firmware checksums */
		return _("Firmware Attestation");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_FWUPD_PLUGINS) == 0) {
		/* TRANSLATORS: Title: if the fwupd plugins are all present and correct */
		return _("Firmware Updater Verification");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_ENABLED) == 0 ||
	    g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_LOCKED) == 0) {
		/* TRANSLATORS: Title: Allows debugging of parts using proprietary hardware */
		return _("Platform Debugging");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SUPPORTED_CPU) == 0) {
		/* TRANSLATORS: Title: if fwupd supports HSI on this chip */
		return _("Processor Security Checks");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_AMD_ROLLBACK_PROTECTION) == 0) {
		/* TRANSLATORS: Title: if firmware enforces rollback protection */
		return _("AMD Secure Processor Rollback Protection");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_AMD_SPI_REPLAY_PROTECTION) == 0) {
		/* TRANSLATORS: Title: if hardware enforces control of SPI replays */
		return _("AMD Firmware Replay Protection");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_AMD_SPI_WRITE_PROTECTION) == 0) {
		/* TRANSLATORS: Title: if hardware enforces control of SPI writes */
		return _("AMD Firmware Write Protection");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_PLATFORM_FUSED) == 0) {
		/* TRANSLATORS: Title: if the part has been fused */
		return _("Fused Platform");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_BIOS_ROLLBACK_PROTECTION) == 0) {
		/* TRANSLATORS: Title: if firmware enforces rollback protection */
		return _("BIOS Rollback Protection");
	}
	return NULL;
}

/* one line describing the attribute */
const gchar *
fu_security_attr_get_description(FwupdSecurityAttr *attr)
{
	const gchar *appstream_id = fwupd_security_attr_get_appstream_id(attr);
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE) == 0 ||
	    g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SPI_BLE) == 0 ||
	    g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_AMD_SPI_WRITE_PROTECTION) == 0) {
		/* TRANSLATORS: longer description */
		return _("Firmware Write Protection protects device firmware memory from being "
			 "tampered with.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SPI_SMM_BWP) == 0) {
		/* TRANSLATORS: longer description */
		return _("Firmware BIOS Region protects device firmware memory from being "
			 "tampered with.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SPI_DESCRIPTOR) == 0) {
		/* TRANSLATORS: longer description */
		return _("Firmware BIOS Descriptor protects device firmware memory from being "
			 "tampered with.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_PREBOOT_DMA_PROTECTION) == 0) {
		/* TRANSLATORS: longer description */
		return _("Pre-boot DMA protection prevents devices from accessing system memory "
			 "after being connected to the computer.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ENABLED) == 0 ||
	    g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ACM) == 0 ||
	    g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_OTP) == 0 ||
	    g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_VERIFIED) == 0) {
		/* TRANSLATORS: longer description */
		return _("Intel BootGuard prevents unauthorized device software from operating "
			 "when the device is started.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_POLICY) == 0) {
		/* TRANSLATORS: longer description */
		return _(
		    "Intel BootGuard Error Policy ensures the device does not continue to start if "
		    "its device software has been tampered with.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_CET_ENABLED) == 0 ||
	    g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_CET_ACTIVE) == 0) {
		/* TRANSLATORS: longer description */
		return _("Intel Control-Flow Enforcement Technology detects and prevents certain "
			 "methods for running malicious software on the device.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_SMAP) == 0) {
		/* TRANSLATORS: longer description */
		return _("Intel Supervisor Mode Access Prevention ensures critical parts of "
			 "device memory are not accessed by less secure programs.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM) == 0) {
		/* TRANSLATORS: longer description */
		return _(
		    "Encrypted RAM makes it impossible for information that is stored in device "
		    "memory to be read if the memory chip is removed and accessed.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_IOMMU) == 0) {
		/* TRANSLATORS: longer description */
		return _("IOMMU Protection prevents connected devices from accessing unauthorized "
			 "parts of system memory.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_KERNEL_LOCKDOWN) == 0) {
		/* TRANSLATORS: longer description */
		return _("Linux Kernel Lockdown mode prevents administrator (root) accounts from "
			 "accessing and changing critical parts of system software.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_KERNEL_TAINTED) == 0) {
		/* TRANSLATORS: longer description */
		return _(
		    "Linux Kernel Verification makes sure that critical system software has "
		    "not been tampered with. Using device drivers which are not provided with the "
		    "system can prevent this security feature from working correctly.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_KERNEL_SWAP) == 0) {
		/* TRANSLATORS: longer description */
		return _(
		    "Linux Kernel Swap temporarily saves information to disk as you work. If the "
		    "information is not protected, it could be accessed by someone if they "
		    "obtained the disk.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM) == 0) {
		/* TRANSLATORS: longer description */
		return _("Suspend to RAM allows the device to quickly go to sleep in order to save "
			 "power. While the device has been suspended, its memory could be "
			 "physically removed and its information accessed.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_IDLE) == 0) {
		/* TRANSLATORS: longer description */
		return _("Suspend to Idle allows the device to quickly go to sleep in order to "
			 "save power. While the device has been suspended, its memory could be "
			 "physically removed and its information accessed.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_UEFI_PK) == 0) {
		/* TRANSLATORS: longer description */
		return _("The UEFI Platform Key is used to determine if device software comes from "
			 "a trusted source.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT) == 0) {
		/* TRANSLATORS: longer description */
		return _("UEFI Secure Boot prevents malicious software from being loaded when the "
			 "device starts.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_UEFI_BOOTSERVICE_VARS) == 0) {
		/* TRANSLATORS: longer description */
		return _("UEFI boot service variables should not be readable from runtime mode.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_TPM_EMPTY_PCR) == 0) {
		/* TRANSLATORS: longer description */
		return _("The TPM (Trusted Platform Module) Platform Configuration is used to "
			 "check whether the device start process has been modified.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_TPM_RECONSTRUCTION_PCR0) == 0) {
		/* TRANSLATORS: longer description */
		return _("The TPM (Trusted Platform Module) Reconstruction is used to check "
			 "whether the device start process has been modified.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_TPM_VERSION_20) == 0) {
		/* TRANSLATORS: longer description */
		return _("TPM (Trusted Platform Module) is a computer chip that detects when "
			 "hardware components have been tampered with.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE) == 0 ||
	    g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_PLATFORM_FUSED) == 0) {
		/* TRANSLATORS: longer description */
		return _("Manufacturing Mode is used when the device is manufactured and "
			 "security features are not yet enabled.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_MEI_OVERRIDE_STRAP) == 0) {
		/* TRANSLATORS: longer description */
		return _("Intel Management Engine Override disables checks for device software "
			 "tampering.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_MEI_KEY_MANIFEST) == 0) {
		/* TRANSLATORS: longer description */
		return _("The Intel Management Engine Key Manifest must be valid so "
			 "that the device firmware can be trusted by the CPU.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_MEI_VERSION) == 0) {
		/* TRANSLATORS: longer description */
		return _("The Intel Management Engine controls device components and needs "
			 "to have a recent version to avoid security issues.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_FWUPD_UPDATES) == 0) {
		/* TRANSLATORS: longer description */
		return _("Device software updates are provided for this device.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_FWUPD_ATTESTATION) == 0) {
		/* TRANSLATORS: longer description */
		return _("Firmware Attestation checks device software using a reference copy, to "
			 "make sure that it has not been changed.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_FWUPD_PLUGINS) == 0) {
		/* TRANSLATORS: longer description */
		return _(
		    "Firmware Updater Verification checks that software used for updating has not "
		    "been tampered with.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_ENABLED) == 0 ||
	    g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_LOCKED) == 0) {
		/* TRANSLATORS: longer description */
		return _("Platform Debugging allows device security features to be disabled. "
			 "This should only be used by hardware manufacturers.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SUPPORTED_CPU) == 0) {
		/* TRANSLATORS: longer description */
		return _("Each system should have tests to ensure firmware security.");
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_AMD_ROLLBACK_PROTECTION) == 0 ||
	    g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_AMD_SPI_REPLAY_PROTECTION) == 0 ||
	    g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_BIOS_ROLLBACK_PROTECTION) == 0) {
		/* TRANSLATORS: longer description */
		return _("Rollback Protection prevents device software from being downgraded "
			 "to an older version that has security problems.");
	}

	return NULL;
}
