/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-smbios.h"

#define FU_TYPE_HWIDS (fu_hwids_get_type())

G_DECLARE_FINAL_TYPE(FuHwids, fu_hwids, FU, HWIDS, GObject)

/**
 * FU_HWIDS_KEY_BASEBOARD_MANUFACTURER:
 *
 * The HwID key for the baseboard (motherboard) vendor.
 *
 * Since: 1.3.7
 **/
#define FU_HWIDS_KEY_BASEBOARD_MANUFACTURER "BaseboardManufacturer"
/**
 * FU_HWIDS_KEY_BASEBOARD_PRODUCT:
 *
 * The HwID key for baseboard (motherboard) product.
 *
 * Since: 1.3.7
 **/
#define FU_HWIDS_KEY_BASEBOARD_PRODUCT "BaseboardProduct"
/**
 * FU_HWIDS_KEY_BIOS_MAJOR_RELEASE:
 *
 * The HwID key for the BIOS major version.
 *
 * Since: 1.3.7
 **/
#define FU_HWIDS_KEY_BIOS_MAJOR_RELEASE "BiosMajorRelease"
/**
 * FU_HWIDS_KEY_BIOS_MINOR_RELEASE:
 *
 * The HwID key for the BIOS minor version.
 *
 * Since: 1.3.7
 **/
#define FU_HWIDS_KEY_BIOS_MINOR_RELEASE "BiosMinorRelease"
/**
 * FU_HWIDS_KEY_BIOS_VENDOR:
 *
 * The HwID key for the BIOS vendor.
 *
 * Since: 1.3.7
 **/
#define FU_HWIDS_KEY_BIOS_VENDOR "BiosVendor"
/**
 * FU_HWIDS_KEY_BIOS_VERSION:
 *
 * The HwID key for the BIOS version.
 *
 * Since: 1.3.7
 **/
#define FU_HWIDS_KEY_BIOS_VERSION "BiosVersion"
/**
 * FU_HWIDS_KEY_FIRMWARE_MAJOR_RELEASE:
 *
 * The HwID key for the firmware major version.
 *
 * Since: 1.6.1
 **/
#define FU_HWIDS_KEY_FIRMWARE_MAJOR_RELEASE "FirmwareMajorRelease"
/**
 * FU_HWIDS_KEY_FIRMWARE_MINOR_RELEASE:
 *
 * The HwID key for the firmware minor version.
 *
 * Since: 1.6.1
 **/
#define FU_HWIDS_KEY_FIRMWARE_MINOR_RELEASE "FirmwareMinorRelease"
/**
 * FU_HWIDS_KEY_ENCLOSURE_KIND:
 *
 * The HwID key for the enclosure kind.
 *
 * Since: 1.3.7
 **/
#define FU_HWIDS_KEY_ENCLOSURE_KIND "EnclosureKind"
/**
 * FU_HWIDS_KEY_FAMILY:
 *
 * The HwID key for the deice family.
 *
 * Since: 1.3.7
 **/
#define FU_HWIDS_KEY_FAMILY "Family"
/**
 * FU_HWIDS_KEY_MANUFACTURER:
 *
 * The HwID key for the top-level product vendor.
 *
 * Since: 1.3.7
 **/
#define FU_HWIDS_KEY_MANUFACTURER "Manufacturer"
/**
 * FU_HWIDS_KEY_PRODUCT_NAME:
 *
 * The HwID key for the top-level product product name.
 *
 * Since: 1.3.7
 **/
#define FU_HWIDS_KEY_PRODUCT_NAME "ProductName"
/**
 * FU_HWIDS_KEY_PRODUCT_SKU:
 *
 * The HwID key for the top-level product SKU.
 *
 * Since: 1.3.7
 **/
#define FU_HWIDS_KEY_PRODUCT_SKU "ProductSku"

FuHwids *
fu_hwids_new(void);
GPtrArray *
fu_hwids_get_keys(FuHwids *self);
const gchar *
fu_hwids_get_value(FuHwids *self, const gchar *key);
void
fu_hwids_add_smbios_override(FuHwids *self, const gchar *key, const gchar *value);
const gchar *
fu_hwids_get_replace_keys(FuHwids *self, const gchar *key);
gchar *
fu_hwids_get_replace_values(FuHwids *self,
			    const gchar *keys,
			    GError **error) G_GNUC_WARN_UNUSED_RESULT;
gchar *
fu_hwids_get_guid(FuHwids *self, const gchar *keys, GError **error) G_GNUC_WARN_UNUSED_RESULT;
GPtrArray *
fu_hwids_get_guids(FuHwids *self);
gboolean
fu_hwids_has_guid(FuHwids *self, const gchar *guid);
gboolean
fu_hwids_setup(FuHwids *self, FuSmbios *smbios, GError **error) G_GNUC_WARN_UNUSED_RESULT;
