/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <json-glib/json-glib.h>

#include "fwupd-build.h"
#include "fwupd-security-attr.h"

G_BEGIN_DECLS

/**
 * FWUPD_SECURITY_ATTR_ID_PREBOOT_DMA_PROTECTION:
 *
 * Host Security ID attribute for Pre-boot DMA protection
 *
 * This was previously known as org.fwupd.hsi.AcpiDmar for Intel from 1.5.0+.
 *
 * Since: 1.8.0
 **/
#define FWUPD_SECURITY_ATTR_ID_PREBOOT_DMA_PROTECTION "org.fwupd.hsi.PrebootDma"
/**
 * FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM:
 *
 * Host Security ID attribute indicating encrypted RAM available
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM "org.fwupd.hsi.EncryptedRam"
/**
 * FWUPD_SECURITY_ATTR_ID_FWUPD_ATTESTATION:
 *
 * Host Security ID attribute for attestation
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_FWUPD_ATTESTATION "org.fwupd.hsi.Fwupd.Attestation"
/**
 * FWUPD_SECURITY_ATTR_ID_FWUPD_PLUGINS:
 *
 * Host Security ID attribute for plugins
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_FWUPD_PLUGINS "org.fwupd.hsi.Fwupd.Plugins"
/**
 * FWUPD_SECURITY_ATTR_ID_FWUPD_UPDATES:
 *
 * Host Security ID attribute for updates
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_FWUPD_UPDATES "org.fwupd.hsi.Fwupd.Updates"
/**
 * FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ENABLED:
 *
 * Host Security ID attribute for Intel Bootguard enabled
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ENABLED "org.fwupd.hsi.IntelBootguard.Enabled"
/**
 * FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_VERIFIED:
 *
 * Host Security ID attribute for Intel Bootguard verified
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_VERIFIED "org.fwupd.hsi.IntelBootguard.Verified"
/**
 * FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ACM:
 *
 * Host Security ID attribute for Intel Bootguard ACM
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ACM "org.fwupd.hsi.IntelBootguard.Acm"
/**
 * FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_POLICY:
 *
 * Host Security ID attribute for Intel Bootguard policy
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_POLICY "org.fwupd.hsi.IntelBootguard.Policy"
/**
 * FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_OTP:
 *
 * Host Security ID attribute for Intel Bootguard OTP fuse
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_OTP "org.fwupd.hsi.IntelBootguard.Otp"
/**
 * FWUPD_SECURITY_ATTR_ID_IOMMU:
 *
 * Host Security ID attribute for IOMMU
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_IOMMU "org.fwupd.hsi.Iommu"
/**
 * FWUPD_SECURITY_ATTR_ID_KERNEL_LOCKDOWN:
 *
 * Host Security ID attribute for kernel lockdown
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_KERNEL_LOCKDOWN "org.fwupd.hsi.Kernel.Lockdown"
/**
 * FWUPD_SECURITY_ATTR_ID_KERNEL_SWAP:
 *
 * Host Security ID attribute for kernel swap
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_KERNEL_SWAP "org.fwupd.hsi.Kernel.Swap"
/**
 * FWUPD_SECURITY_ATTR_ID_KERNEL_TAINTED:
 *
 * Host Security ID attribute for kernel taint
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_KERNEL_TAINTED "org.fwupd.hsi.Kernel.Tainted"
/**
 * FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE:
 *
 * Host Security ID attribute for Intel ME manufacturing mode
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE "org.fwupd.hsi.Mei.ManufacturingMode"
/**
 * FWUPD_SECURITY_ATTR_ID_MEI_OVERRIDE_STRAP:
 *
 * Host Security ID attribute for Intel ME override strap
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_MEI_OVERRIDE_STRAP "org.fwupd.hsi.Mei.OverrideStrap"
/**
 * FWUPD_SECURITY_ATTR_ID_MEI_KEY_MANIFEST:
 *
 * Host Security ID attribute for Intel ME Key Manifest
 *
 * Since: 1.8.7
 **/
#define FWUPD_SECURITY_ATTR_ID_MEI_KEY_MANIFEST "org.fwupd.hsi.Mei.KeyManifest"
/**
 * FWUPD_SECURITY_ATTR_ID_MEI_VERSION:
 *
 * Host Security ID attribute for Intel ME version
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_MEI_VERSION "org.fwupd.hsi.Mei.Version"
/**
 * FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE:
 *
 * Host Security ID attribute for Intel SPI BIOSWE configuration
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE "org.fwupd.hsi.Spi.Bioswe"
/**
 * FWUPD_SECURITY_ATTR_ID_SPI_BLE:
 *
 * Host Security ID attribute for Intel SPI BLE configuration
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_SPI_BLE "org.fwupd.hsi.Spi.Ble"
/**
 * FWUPD_SECURITY_ATTR_ID_SPI_SMM_BWP:
 *
 * Host Security ID attribute for Intel SPI SMM BWP
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_SPI_SMM_BWP "org.fwupd.hsi.Spi.SmmBwp"
/**
 * FWUPD_SECURITY_ATTR_ID_SPI_DESCRIPTOR:
 *
 * Host Security ID attribute for Intel SPI descriptor
 *
 * Since: 1.6.0
 **/
#define FWUPD_SECURITY_ATTR_ID_SPI_DESCRIPTOR "org.fwupd.hsi.Spi.Descriptor"
/**
 * FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_IDLE:
 *
 * Host Security ID attribute for Suspend to Idle
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_IDLE "org.fwupd.hsi.SuspendToIdle"
/**
 * FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM:
 *
 * Host Security ID attribute for Suspend to RAM
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM "org.fwupd.hsi.SuspendToRam"
/**
 * FWUPD_SECURITY_ATTR_ID_TPM_EMPTY_PCR:
 *
 * Host Security ID attribute for empty PCR
 *
 * Since: 1.7.2
 **/
#define FWUPD_SECURITY_ATTR_ID_TPM_EMPTY_PCR "org.fwupd.hsi.Tpm.EmptyPcr"
/**
 * FWUPD_SECURITY_ATTR_ID_TPM_RECONSTRUCTION_PCR0:
 *
 * Host Security ID attribute for TPM PCR0 reconstruction
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_TPM_RECONSTRUCTION_PCR0 "org.fwupd.hsi.Tpm.ReconstructionPcr0"
/**
 * FWUPD_SECURITY_ATTR_ID_TPM_VERSION_20:
 *
 * Host Security ID attribute for TPM 2.0
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_TPM_VERSION_20 "org.fwupd.hsi.Tpm.Version20"
/**
 * FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT:
 *
 * Host Security ID attribute for UEFI secure boot
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT "org.fwupd.hsi.Uefi.SecureBoot"
/**
 * FWUPD_SECURITY_ATTR_ID_UEFI_BOOTSERVICE_VARS:
 *
 * Host Security ID attribute indicating if Bootservice-only variables are hidden.
 *
 * Since: 1.9.3
 **/
#define FWUPD_SECURITY_ATTR_ID_UEFI_BOOTSERVICE_VARS "org.fwupd.hsi.Uefi.BootserviceVars"
/**
 * FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_ENABLED:
 *
 * Host Security ID attribute for parts with debugging capabilities enabled
 *
 * This was previously known as org.fwupd.hsi.PlatformDebugEnabled for Intel 1.5.0+
 * It was renamed for all vendor support in 1.8.0. *
 *
 * Since: 1.8.0
 **/
#define FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_ENABLED "org.fwupd.hsi.PlatformDebugEnabled"
/**
 * FWUPD_SECURITY_ATTR_ID_PLATFORM_FUSED:
 *
 * Host Security ID attribute for fused parts
 *
 * Since: 1.8.0
 **/
#define FWUPD_SECURITY_ATTR_ID_PLATFORM_FUSED "org.fwupd.hsi.PlatformFused"
/**
 * FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_LOCKED:
 *
 * Host Security ID attribute for parts locked from debugging
 *
 * This was previously known as org.fwupd.hsi.IntelDci.Locked for Intel 1.5.0+
 * It was renamed for all vendor support in 1.8.0.
 *
 * Since: 1.8.0
 **/
#define FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_LOCKED "org.fwupd.hsi.PlatformDebugLocked"
/**
 * FWUPD_SECURITY_ATTR_ID_UEFI_PK:
 *
 * Host Security ID attribute for UEFI PK
 *
 * Since: 1.5.5
 **/
#define FWUPD_SECURITY_ATTR_ID_UEFI_PK "org.fwupd.hsi.Uefi.Pk"
/**
 * FWUPD_SECURITY_ATTR_ID_SUPPORTED_CPU:
 *
 * Host Security ID attribute for Supported CPU
 *
 * Since: 1.8.0
 **/
#define FWUPD_SECURITY_ATTR_ID_SUPPORTED_CPU "org.fwupd.hsi.SupportedCpu"
/**
 * FWUPD_SECURITY_ATTR_ID_AMD_ROLLBACK_PROTECTION:
 *
 * Host Security ID attribute for Rollback protection of AMD platform
 * firmware
 *
 * Since: 1.8.0
 **/
#define FWUPD_SECURITY_ATTR_ID_AMD_ROLLBACK_PROTECTION "org.fwupd.hsi.Amd.RollbackProtection"
/**
 * FWUPD_SECURITY_ATTR_ID_AMD_SPI_WRITE_PROTECTION:
 *
 * Host Security ID attribute for SPI Write protection
 *
 * Since: 1.8.0
 **/
#define FWUPD_SECURITY_ATTR_ID_AMD_SPI_WRITE_PROTECTION "org.fwupd.hsi.Amd.SpiWriteProtection"
/**
 * FWUPD_SECURITY_ATTR_ID_AMD_SPI_REPLAY_PROTECTION:
 *
 * Host Security ID attribute for SPI replay protection
 *
 * Since: 1.8.0
 **/
#define FWUPD_SECURITY_ATTR_ID_AMD_SPI_REPLAY_PROTECTION "org.fwupd.hsi.Amd.SpiReplayProtection"
/**
 * FWUPD_SECURITY_ATTR_ID_HOST_EMULATION:
 *
 * Host Security ID attribute for host emulation
 *
 * Since: 1.8.3
 **/
#define FWUPD_SECURITY_ATTR_ID_HOST_EMULATION "org.fwupd.hsi.HostEmulation"
/**
 * FWUPD_SECURITY_ATTR_ID_BIOS_ROLLBACK_PROTECTION:
 *
 * Host Security ID attribute for Rollback protection of BIOS firmware
 *
 * Since: 1.8.8
 **/
#define FWUPD_SECURITY_ATTR_ID_BIOS_ROLLBACK_PROTECTION "org.fwupd.hsi.Bios.RollbackProtection"
/**
 * FWUPD_SECURITY_ATTR_ID_INTEL_GDS:
 *
 * Host Security ID attribute indicating the processor is safe against Gather Data Sampling.
 *
 * Since: 1.9.4
 **/
#define FWUPD_SECURITY_ATTR_ID_INTEL_GDS "org.fwupd.hsi.IntelGds"
/**
 * FWUPD_SECURITY_ATTR_ID_BIOS_CAPSULE_UPDATES:
 *
 * Host Security ID attribute indicating Capsule updates are supported by the BIOS.
 *
 * Since: 1.9.6
 **/
#define FWUPD_SECURITY_ATTR_ID_BIOS_CAPSULE_UPDATES "org.fwupd.hsi.Bios.CapsuleUpdates"
/**
 * FWUPD_SECURITY_ATTR_ID_SMAP:
 *
 * Host Security ID attribute for SMAP
 *
 * NOTE: This attribute use to be known as org.fwupd.hsi.IntelSmap before fwupd 2.0.0
 *
 * Since: 2.0.0
 **/
#define FWUPD_SECURITY_ATTR_ID_SMAP "org.fwupd.hsi.Smap"
/**
 * FWUPD_SECURITY_ATTR_ID_CET_ENABLED:
 *
 * Host Security ID attribute for Intel CET enabled
 *
 * NOTE: This used to be known as org.fwupd.hsi.IntelCet.Enabled before fwupd 2.0.0
 *
 * Since: 2.0.0
 **/
#define FWUPD_SECURITY_ATTR_ID_CET_ENABLED "org.fwupd.hsi.Cet.Enabled"
/**
 * FWUPD_SECURITY_ATTR_ID_CET_ACTIVE:
 *
 * Host Security ID attribute for Intel CET active
 *
 * NOTE: This used to be known as org.fwupd.hsi.IntelCet.Active before fwupd 2.0.0
 *
 * Since: 2.0.0
 **/
#define FWUPD_SECURITY_ATTR_ID_CET_ACTIVE "org.fwupd.hsi.Cet.Active"
/**
 * FWUPD_SECURITY_ATTR_ID_AMD_SMM_LOCKED:
 *
 * Host Security ID attribute for AMD SMM locked
 *
 * Since: 2.0.2
 **/
#define FWUPD_SECURITY_ATTR_ID_AMD_SMM_LOCKED "org.fwupd.hsi.Amd.SmmLocked"
/**
 * FWUPD_SECURITY_ATTR_ID_UEFI_MEMORY_PROTECTION:
 *
 * Host Security ID attribute for UEFI memory protection
 *
 * Since: 2.0.7
 **/
#define FWUPD_SECURITY_ATTR_ID_UEFI_MEMORY_PROTECTION "org.fwupd.hsi.Uefi.MemoryProtection"
/**
 * FWUPD_SECURITY_ATTR_ID_UEFI_DB:
 *
 * Host Security ID attribute for UEFI db certificate store
 *
 * Since: 2.0.8
 **/
#define FWUPD_SECURITY_ATTR_ID_UEFI_DB "org.fwupd.hsi.Uefi.Db"

FwupdSecurityAttr *
fwupd_security_attr_copy(FwupdSecurityAttr *self) G_GNUC_NON_NULL(1);

G_END_DECLS
