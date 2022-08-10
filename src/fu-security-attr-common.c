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
		return g_strdup(_("Platform Debugging"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SUPPORTED_CPU) == 0) {
		/* TRANSLATORS: Title: if fwupd supports HSI on this chip */
		return g_strdup(_("Supported CPU"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_AMD_ROLLBACK_PROTECTION) == 0) {
		/* TRANSLATORS: Title: if firmware enforces rollback protection */
		return g_strdup(_("Rollback protection"));
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
		return _("AMD Rollback Protection");
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
		return _("Pre-boot DMA protection prevents devices from being connected to the "
			 "computer and accessing system memory.");
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
	    g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_AMD_SPI_REPLAY_PROTECTION) == 0) {
		/* TRANSLATORS: longer description */
		return _("AMD Rollback Protection prevents device software from being downgraded "
			 "to an older version that has security problems.");
	}
	return NULL;
}

const gchar *
fu_security_attr_result_to_string(FwupdSecurityAttrResult result)
{
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
	return NULL;
}

const gchar *
fu_security_attr_get_result(FwupdSecurityAttr *attr)
{
	const gchar *tmp;

	/* common case */
	tmp = fu_security_attr_result_to_string(fwupd_security_attr_get_result(attr));
	if (tmp != NULL)
		return tmp;

	/* fallback */
	if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS)) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("OK");
	}

	/* TRANSLATORS: Suffix: the fallback HSI result */
	return _("Failed");
}

/**
 * fu_security_attrs_to_json_string：
 * Convert security attribute to JSON string.
 * @attrs: a pointer for a FuSecurityAttrs data structure.
 * @error: return location for an error
 *
 * fu_security_attrs_to_json_string() converts FuSecurityAttrs and return the
 * string pointer. The converted JSON format is shown as follows:
 * {
 *     "SecurityAttributes": [
 *         {
 *              "name": "aaa",
 *              "value": "bbb"
 *         }
 *     ]
 *  }
 *
 * Returns: A string and NULL on fail.
 *
 * Since: 1.7.0
 *
 */
gchar *
fu_security_attrs_to_json_string(FuSecurityAttrs *attrs, GError **error)
{
	g_autofree gchar *data = NULL;
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonBuilder) builder = json_builder_new();
	g_autoptr(JsonNode) json_root = NULL;
	fu_security_attrs_to_json(attrs, builder);
	json_root = json_builder_get_root(builder);
	json_generator = json_generator_new();
	json_generator_set_pretty(json_generator, TRUE);
	json_generator_set_root(json_generator, json_root);
	data = json_generator_to_data(json_generator, NULL);
	if (data == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to convert security attribute to json.");
		return NULL;
	}
	return g_steal_pointer(&data);
}

void
fu_security_attrs_to_json(FuSecurityAttrs *attrs, JsonBuilder *builder)
{
	g_autoptr(GPtrArray) items = NULL;

	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "SecurityAttributes");
	json_builder_begin_array(builder);
	items = fu_security_attrs_get_all(attrs);
	for (guint i = 0; i < items->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index(items, i);
		guint64 created = fwupd_security_attr_get_created(attr);
		json_builder_begin_object(builder);
		fwupd_security_attr_set_created(attr, 0);
		fwupd_security_attr_to_json(attr, builder);
		fwupd_security_attr_set_created(attr, created);
		json_builder_end_object(builder);
	}
	json_builder_end_array(builder);
	json_builder_end_object(builder);
}

gboolean
fu_security_attrs_from_json(FuSecurityAttrs *attrs, JsonNode *json_node, GError **error)
{
	JsonArray *array;
	JsonObject *obj;

	/* sanity check */
	if (!JSON_NODE_HOLDS_OBJECT(json_node)) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "not JSON object");
		return FALSE;
	}
	obj = json_node_get_object(json_node);

	/* this has to exist */
	if (!json_object_has_member(obj, "SecurityAttributes")) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "no SecurityAttributes property in object");
		return FALSE;
	}
	array = json_object_get_array_member(obj, "SecurityAttributes");
	for (guint i = 0; i < json_array_get_length(array); i++) {
		JsonNode *node_tmp = json_array_get_element(array, i);
		g_autoptr(FwupdSecurityAttr) attr = fwupd_security_attr_new(NULL);
		if (!fwupd_security_attr_from_json(attr, node_tmp, error))
			return FALSE;
		fu_security_attrs_append(attrs, attr);
	}

	/* success */
	return TRUE;
}

/**
 * fu_security_attrs_compare：
 * @attrs1: a #FuSecurityAttrs
 * @attrs2: another #FuSecurityAttrs, perhaps newer in some way
 *
 * Compares the two objects, returning the differences.
 *
 * If the two sets of attrs are considered the same then an empty array is returned.
 * Only the AppStream ID results are compared, extra metadata is ignored.
 *
 * Returns: (element-type FwupdSecurityAttr) (transfer container): differences
 */
GPtrArray *
fu_security_attrs_compare(FuSecurityAttrs *attrs1, FuSecurityAttrs *attrs2)
{
	g_autoptr(GHashTable) hash1 = g_hash_table_new(g_str_hash, g_str_equal);
	g_autoptr(GHashTable) hash2 = g_hash_table_new(g_str_hash, g_str_equal);
	g_autoptr(GPtrArray) array1 = fu_security_attrs_get_all(attrs1);
	g_autoptr(GPtrArray) array2 = fu_security_attrs_get_all(attrs2);
	g_autoptr(GPtrArray) results =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

	/* create hash tables of appstream-id -> FwupdSecurityAttr */
	for (guint i = 0; i < array1->len; i++) {
		FwupdSecurityAttr *attr1 = g_ptr_array_index(array1, i);
		g_hash_table_insert(hash1,
				    (gpointer)fwupd_security_attr_get_appstream_id(attr1),
				    (gpointer)attr1);
	}
	for (guint i = 0; i < array2->len; i++) {
		FwupdSecurityAttr *attr2 = g_ptr_array_index(array2, i);
		g_hash_table_insert(hash2,
				    (gpointer)fwupd_security_attr_get_appstream_id(attr2),
				    (gpointer)attr2);
	}

	/* present in attrs2, not present in attrs1 */
	for (guint i = 0; i < array2->len; i++) {
		FwupdSecurityAttr *attr1;
		FwupdSecurityAttr *attr2 = g_ptr_array_index(array2, i);
		attr1 = g_hash_table_lookup(hash1, fwupd_security_attr_get_appstream_id(attr2));
		if (attr1 == NULL) {
			g_autoptr(FwupdSecurityAttr) attr = fwupd_security_attr_copy(attr2);
			g_ptr_array_add(results, g_steal_pointer(&attr));
			continue;
		}
	}

	/* present in attrs1, not present in attrs2 */
	for (guint i = 0; i < array1->len; i++) {
		FwupdSecurityAttr *attr1 = g_ptr_array_index(array1, i);
		FwupdSecurityAttr *attr2;
		attr2 = g_hash_table_lookup(hash2, fwupd_security_attr_get_appstream_id(attr1));
		if (attr2 == NULL) {
			g_autoptr(FwupdSecurityAttr) attr = fwupd_security_attr_copy(attr1);
			fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_UNKNOWN);
			fwupd_security_attr_set_result_fallback(
			    attr, /* flip these around */
			    fwupd_security_attr_get_result(attr1));
			g_ptr_array_add(results, g_steal_pointer(&attr));
			continue;
		}
	}

	/* find any attributes that differ */
	for (guint i = 0; i < array2->len; i++) {
		FwupdSecurityAttr *attr1;
		FwupdSecurityAttr *attr2 = g_ptr_array_index(array2, i);
		attr1 = g_hash_table_lookup(hash1, fwupd_security_attr_get_appstream_id(attr2));
		if (attr1 == NULL)
			continue;

		/* result of specific attr differed */
		if (fwupd_security_attr_get_result(attr1) !=
		    fwupd_security_attr_get_result(attr2)) {
			g_autoptr(FwupdSecurityAttr) attr = fwupd_security_attr_copy(attr1);
			fwupd_security_attr_set_result(attr, fwupd_security_attr_get_result(attr2));
			fwupd_security_attr_set_result_fallback(
			    attr,
			    fwupd_security_attr_get_result(attr1));
			fwupd_security_attr_set_flags(attr, fwupd_security_attr_get_flags(attr2));
			g_ptr_array_add(results, g_steal_pointer(&attr));
		}
	}

	/* success */
	return g_steal_pointer(&results);
}

/**
 * fu_security_attrs_equal：
 * @attrs1: a #FuSecurityAttrs
 * @attrs2: another #FuSecurityAttrs
 *
 * Tests the objects for equality. Only the AppStream ID results are compared, extra metadata
 * is ignored.
 *
 * Returns: %TRUE if the set of attrs can be considered equal
 */
gboolean
fu_security_attrs_equal(FuSecurityAttrs *attrs1, FuSecurityAttrs *attrs2)
{
	g_autoptr(GPtrArray) compare = fu_security_attrs_compare(attrs1, attrs2);
	return compare->len == 0;
}
