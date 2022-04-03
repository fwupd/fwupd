/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <json-glib/json-glib.h>

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
 * Host Security ID attribute for attesation
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
 * FWUPD_SECURITY_ATTR_ID_INTEL_CET_ENABLED:
 *
 * Host Security ID attribute for Intel CET enabled
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_INTEL_CET_ENABLED "org.fwupd.hsi.IntelCet.Enabled"
/**
 * FWUPD_SECURITY_ATTR_ID_INTEL_CET_ACTIVE:
 *
 * Host Security ID attribute for Intel CET active
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_INTEL_CET_ACTIVE "org.fwupd.hsi.IntelCet.Active"
/**
 * FWUPD_SECURITY_ATTR_ID_INTEL_SMAP:
 *
 * Host Security ID attribute for Intel SMAP
 *
 * Since: 1.5.0
 **/
#define FWUPD_SECURITY_ATTR_ID_INTEL_SMAP "org.fwupd.hsi.IntelSmap"
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
 * FWUPD_SECURITY_ATTR_ID_SUPPORTED_CPU
 *
 * Host Security ID attribute for Supported CPU
 *
 * Since: 1.8.0
 **/
#define FWUPD_SECURITY_ATTR_ID_SUPPORTED_CPU "org.fwupd.hsi.SupportedCpu"
/**
 * FWUPD_SECURITY_ATTR_ID_AMD_ROLLBACK_PROTECTION
 *
 * Host Security ID attribute for Rollback protection of AMD platform
 * firmware
 *
 * Since: 1.8.0
 **/
#define FWUPD_SECURITY_ATTR_ID_AMD_ROLLBACK_PROTECTION "org.fwupd.hsi.Amd.RollbackProtection"
/**
 * FWUPD_SECURITY_ATTR_ID_AMD_SPI_WRITE_PROTECTION
 *
 * Host Security ID attribute for SPI Write protection
 *
 * Since: 1.8.0
 **/
#define FWUPD_SECURITY_ATTR_ID_AMD_SPI_WRITE_PROTECTION "org.fwupd.hsi.Amd.SpiWriteProtection"
/**
 * FWUPD_SECURITY_ATTR_ID_AMD_SPI_REPLAY_PROTECTION
 *
 * Host Security ID attribute for SPI replay protection
 *
 * Since: 1.8.0
 **/
#define FWUPD_SECURITY_ATTR_ID_AMD_SPI_REPLAY_PROTECTION "org.fwupd.hsi.Amd.SpiReplayProtection"

GVariant *
fwupd_security_attr_to_variant(FwupdSecurityAttr *self);
void
fwupd_security_attr_to_json(FwupdSecurityAttr *self, JsonBuilder *builder);
gboolean
fwupd_security_attr_from_json(FwupdSecurityAttr *self, JsonNode *json_node, GError **error);
FwupdSecurityAttr *
fwupd_security_attr_copy(FwupdSecurityAttr *self);
FwupdSecurityAttrResult
fwupd_security_attr_get_result_fallback(FwupdSecurityAttr *self);
void
fwupd_security_attr_set_result_fallback(FwupdSecurityAttr *self, FwupdSecurityAttrResult result);

G_END_DECLS
