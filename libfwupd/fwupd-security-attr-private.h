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

#define FWUPD_SECURITY_ATTR_ID_ACPI_DMAR		"org.fwupd.hsi.AcpiDmar"		/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM		"org.fwupd.hsi.EncryptedRam"		/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_FWUPD_ATTESTATION	"org.fwupd.hsi.Fwupd.Attestation"	/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_FWUPD_PLUGINS		"org.fwupd.hsi.Fwupd.Plugins"		/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_FWUPD_UPDATES		"org.fwupd.hsi.Fwupd.Updates"		/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ENABLED	"org.fwupd.hsi.IntelBootguard.Enabled"	/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_VERIFIED	"org.fwupd.hsi.IntelBootguard.Verified"	/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ACM	"org.fwupd.hsi.IntelBootguard.Acm"	/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_POLICY	"org.fwupd.hsi.IntelBootguard.Policy"	/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_OTP	"org.fwupd.hsi.IntelBootguard.Otp"	/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_INTEL_CET_ENABLED	"org.fwupd.hsi.IntelCet.Enabled"	/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_INTEL_CET_ACTIVE		"org.fwupd.hsi.IntelCet.Active"		/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_INTEL_SMAP		"org.fwupd.hsi.IntelSmap"		/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_IOMMU			"org.fwupd.hsi.Iommu"			/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_KERNEL_LOCKDOWN		"org.fwupd.hsi.Kernel.Lockdown"		/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_KERNEL_SWAP		"org.fwupd.hsi.Kernel.Swap"		/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_KERNEL_TAINTED		"org.fwupd.hsi.Kernel.Tainted"		/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE	"org.fwupd.hsi.Mei.ManufacturingMode"	/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_MEI_OVERRIDE_STRAP	"org.fwupd.hsi.Mei.OverrideStrap"	/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_MEI_VERSION		"org.fwupd.hsi.Mei.Version"		/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE		"org.fwupd.hsi.Spi.Bioswe"		/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_SPI_BLE			"org.fwupd.hsi.Spi.Ble"			/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_SPI_SMM_BWP		"org.fwupd.hsi.Spi.SmmBwp"		/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_IDLE		"org.fwupd.hsi.SuspendToIdle"		/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM		"org.fwupd.hsi.SuspendToRam"		/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_TPM_RECONSTRUCTION_PCR0	"org.fwupd.hsi.Tpm.ReconstructionPcr0"	/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_TPM_VERSION_20		"org.fwupd.hsi.Tpm.Version20"		/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT		"org.fwupd.hsi.Uefi.SecureBoot"		/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_INTEL_DCI_ENABLED	"org.fwupd.hsi.IntelDci.Enabled"	/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_INTEL_DCI_LOCKED		"org.fwupd.hsi.IntelDci.Locked"		/* Since: 1.5.0 */
#define FWUPD_SECURITY_ATTR_ID_UEFI_PK			"org.fwupd.hsi.Uefi.Pk"			/* Since: 1.5.5 */

GVariant	*fwupd_security_attr_to_variant		(FwupdSecurityAttr	*self);
void		 fwupd_security_attr_to_json		(FwupdSecurityAttr	*self,
							 JsonBuilder		*builder);

G_END_DECLS

