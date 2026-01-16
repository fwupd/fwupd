/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuEngine"

#include "config.h"

#include <libfwupdplugin/fu-acpi-table.h>
#include <libfwupdplugin/fu-cab-firmware.h>
#include <libfwupdplugin/fu-cfu-offer.h>
#include <libfwupdplugin/fu-cfu-payload.h>
#include <libfwupdplugin/fu-context-private.h>
#include <libfwupdplugin/fu-coswid-firmware.h>
#include <libfwupdplugin/fu-csv-firmware.h>
#include <libfwupdplugin/fu-dfuse-firmware.h>
#include <libfwupdplugin/fu-edid.h>
#include <libfwupdplugin/fu-efi-device-path-list.h>
#include <libfwupdplugin/fu-efi-file.h>
#include <libfwupdplugin/fu-efi-filesystem.h>
#include <libfwupdplugin/fu-efi-ftw-store.h>
#include <libfwupdplugin/fu-efi-section.h>
#include <libfwupdplugin/fu-efi-signature-list.h>
#include <libfwupdplugin/fu-efi-signature.h>
#include <libfwupdplugin/fu-efi-variable-authentication2.h>
#include <libfwupdplugin/fu-efi-volume.h>
#include <libfwupdplugin/fu-efi-vss-auth-variable.h>
#include <libfwupdplugin/fu-efi-vss2-variable-store.h>
#include <libfwupdplugin/fu-elf-firmware.h>
#include <libfwupdplugin/fu-fdt-firmware.h>
#include <libfwupdplugin/fu-fit-firmware.h>
#include <libfwupdplugin/fu-fmap-firmware.h>
#include <libfwupdplugin/fu-hid-descriptor.h>
#include <libfwupdplugin/fu-ifd-bios.h>
#include <libfwupdplugin/fu-ifd-firmware.h>
#include <libfwupdplugin/fu-ifwi-cpd-firmware.h>
#include <libfwupdplugin/fu-ifwi-fpt-firmware.h>
#include <libfwupdplugin/fu-ihex-firmware.h>
#include <libfwupdplugin/fu-intel-thunderbolt-firmware.h>
#include <libfwupdplugin/fu-json-firmware.h>
#include <libfwupdplugin/fu-linear-firmware.h>
#include <libfwupdplugin/fu-oprom-firmware.h>
#include <libfwupdplugin/fu-pefile-firmware.h>
#include <libfwupdplugin/fu-sbatlevel-section.h>
#include <libfwupdplugin/fu-srec-firmware.h>
#include <libfwupdplugin/fu-tpm-eventlog-v1.h>
#include <libfwupdplugin/fu-tpm-eventlog-v2.h>
#include <libfwupdplugin/fu-usb-device-fw-ds20.h>
#include <libfwupdplugin/fu-usb-device-ms-ds20.h>
#include <libfwupdplugin/fu-uswid-firmware.h>
#include <libfwupdplugin/fu-x509-certificate.h>
#include <libfwupdplugin/fu-zip-firmware.h>

#include "fu-context-helper.h"

void
fu_context_add_firmware_gtypes(FuContext *self)
{
	fu_context_add_firmware_gtype(self, FU_TYPE_FIRMWARE);
	fu_context_add_firmware_gtype(self, FU_TYPE_CAB_FIRMWARE);
	fu_context_add_firmware_gtype(self, FU_TYPE_DFU_FIRMWARE);
	fu_context_add_firmware_gtype(self, FU_TYPE_FDT_FIRMWARE);
	fu_context_add_firmware_gtype(self, FU_TYPE_CSV_FIRMWARE);
	fu_context_add_firmware_gtype(self, FU_TYPE_FIT_FIRMWARE);
	fu_context_add_firmware_gtype(self, FU_TYPE_DFUSE_FIRMWARE);
	fu_context_add_firmware_gtype(self, FU_TYPE_IFWI_CPD_FIRMWARE);
	fu_context_add_firmware_gtype(self, FU_TYPE_IFWI_FPT_FIRMWARE);
	fu_context_add_firmware_gtype(self, FU_TYPE_OPROM_FIRMWARE);
	fu_context_add_firmware_gtype(self, FU_TYPE_FMAP_FIRMWARE);
	fu_context_add_firmware_gtype(self, FU_TYPE_IHEX_FIRMWARE);
	fu_context_add_firmware_gtype(self, FU_TYPE_LINEAR_FIRMWARE);
	fu_context_add_firmware_gtype(self, FU_TYPE_SREC_FIRMWARE);
	fu_context_add_firmware_gtype(self, FU_TYPE_HID_DESCRIPTOR);
	fu_context_add_firmware_gtype(self, FU_TYPE_SMBIOS);
	fu_context_add_firmware_gtype(self, FU_TYPE_ACPI_TABLE);
	fu_context_add_firmware_gtype(self, FU_TYPE_SBATLEVEL_SECTION);
	fu_context_add_firmware_gtype(self, FU_TYPE_EDID);
	fu_context_add_firmware_gtype(self, FU_TYPE_EFI_FILE);
	fu_context_add_firmware_gtype(self, FU_TYPE_EFI_SIGNATURE);
	fu_context_add_firmware_gtype(self, FU_TYPE_EFI_SIGNATURE_LIST);
	fu_context_add_firmware_gtype(self, FU_TYPE_EFI_VARIABLE_AUTHENTICATION2);
	fu_context_add_firmware_gtype(self, FU_TYPE_EFI_LOAD_OPTION);
	fu_context_add_firmware_gtype(self, FU_TYPE_EFI_DEVICE_PATH_LIST);
	fu_context_add_firmware_gtype(self, FU_TYPE_EFI_FILESYSTEM);
	fu_context_add_firmware_gtype(self, FU_TYPE_EFI_SECTION);
	fu_context_add_firmware_gtype(self, FU_TYPE_EFI_VOLUME);
	fu_context_add_firmware_gtype(self, FU_TYPE_EFI_FTW_STORE);
	fu_context_add_firmware_gtype(self, FU_TYPE_EFI_VSS2_VARIABLE_STORE);
	fu_context_add_firmware_gtype(self, FU_TYPE_EFI_VSS_AUTH_VARIABLE);
	fu_context_add_firmware_gtype(self, FU_TYPE_JSON_FIRMWARE);
	fu_context_add_firmware_gtype(self, FU_TYPE_IFD_BIOS);
	fu_context_add_firmware_gtype(self, FU_TYPE_IFD_FIRMWARE);
	fu_context_add_firmware_gtype(self, FU_TYPE_CFU_OFFER);
	fu_context_add_firmware_gtype(self, FU_TYPE_CFU_PAYLOAD);
	fu_context_add_firmware_gtype(self, FU_TYPE_USWID_FIRMWARE);
	fu_context_add_firmware_gtype(self, FU_TYPE_COSWID_FIRMWARE);
	fu_context_add_firmware_gtype(self, FU_TYPE_PEFILE_FIRMWARE);
	fu_context_add_firmware_gtype(self, FU_TYPE_ELF_FIRMWARE);
	fu_context_add_firmware_gtype(self, FU_TYPE_X509_CERTIFICATE);
	fu_context_add_firmware_gtype(self, FU_TYPE_INTEL_THUNDERBOLT_FIRMWARE);
	fu_context_add_firmware_gtype(self, FU_TYPE_INTEL_THUNDERBOLT_NVM);
	fu_context_add_firmware_gtype(self, FU_TYPE_USB_DEVICE_FW_DS20);
	fu_context_add_firmware_gtype(self, FU_TYPE_USB_DEVICE_MS_DS20);
	fu_context_add_firmware_gtype(self, FU_TYPE_TPM_EVENTLOG_V1);
	fu_context_add_firmware_gtype(self, FU_TYPE_TPM_EVENTLOG_V2);
	fu_context_add_firmware_gtype(self, FU_TYPE_ZIP_FIRMWARE);
}
