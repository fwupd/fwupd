/*
 * Copyright 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib-object.h>

#define FU_TYPE_QUIRKS (fu_quirks_get_type())
G_DECLARE_FINAL_TYPE(FuQuirks, fu_quirks, FU, QUIRKS, GObject)

/**
 * FuQuirksLoadFlags:
 * @FU_QUIRKS_LOAD_FLAG_NONE:		No flags set
 * @FU_QUIRKS_LOAD_FLAG_READONLY_FS:	Ignore readonly filesystem errors
 * @FU_QUIRKS_LOAD_FLAG_NO_CACHE:	Do not save to a persistent cache
 * @FU_QUIRKS_LOAD_FLAG_NO_VERIFY:	Do not check the key files for errors
 *
 * The flags to use when loading quirks.
 **/
typedef enum {
	FU_QUIRKS_LOAD_FLAG_NONE = 0,
	FU_QUIRKS_LOAD_FLAG_READONLY_FS = 1 << 0,
	FU_QUIRKS_LOAD_FLAG_NO_CACHE = 1 << 1,
	FU_QUIRKS_LOAD_FLAG_NO_VERIFY = 1 << 2,
	/*< private >*/
	FU_QUIRKS_LOAD_FLAG_LAST
} FuQuirksLoadFlags;

/**
 * FuQuirksIter:
 * @self: a #FuQuirks
 * @key: a key
 * @value: a value
 * @user_data: (closure): user data
 *
 * The quirks iteration callback.
 */
typedef void (*FuQuirksIter)(FuQuirks *self,
			     const gchar *key,
			     const gchar *value,
			     gpointer user_data);

FuQuirks *
fu_quirks_new(void);
gboolean
fu_quirks_load(FuQuirks *self,
	       FuQuirksLoadFlags load_flags,
	       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
const gchar *
fu_quirks_lookup_by_id(FuQuirks *self, const gchar *guid, const gchar *key)
    G_GNUC_NON_NULL(1, 2, 3);
gboolean
fu_quirks_lookup_by_id_iter(FuQuirks *self,
			    const gchar *guid,
			    const gchar *key,
			    FuQuirksIter iter_cb,
			    gpointer user_data) G_GNUC_NON_NULL(1, 2);
void
fu_quirks_add_possible_key(FuQuirks *self, const gchar *possible_key) G_GNUC_NON_NULL(1, 2);

/**
 * FU_QUIRKS_PLUGIN:
 *
 * The quirk key for the plugin name, e.g. `csr`.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_PLUGIN "Plugin"
/**
 * FU_QUIRKS_FLAGS:
 *
 * The quirk key for either for public, internal or private flags, e.g. `is-bootloader`.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_FLAGS "Flags"
/**
 * FU_QUIRKS_SUMMARY:
 *
 * The quirk key for the summary, e.g. `An open source display colorimeter`.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_SUMMARY "Summary"
/**
 * FU_QUIRKS_ICON:
 *
 * The quirk key for the icon, e.g. `media-removable`.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_ICON "Icon"
/**
 * FU_QUIRKS_NAME:
 *
 * The quirk key for the name, e.g. `ColorHug`.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_NAME "Name"
/**
 * FU_QUIRKS_BRANCH:
 *
 * The quirk key for the firmware branch.
 *
 * Since: 1.5.0
 **/
#define FU_QUIRKS_BRANCH "Branch"
/**
 * FU_QUIRKS_GUID:
 *
 * The quirk key for the GUID, e.g. `537f7800-8529-5656-b2fa-b0901fe91696`.
 *
 * If the value provided is not already a suitable GUID, it will be converted to one.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_GUID "Guid"
/**
 * FU_QUIRKS_GUID_QUIRK:
 *
 * The quirk key for the GUID, only used for quirk matching, e.g. `SYNAPTICS_CAPE\CX31993`.
 *
 * If the value provided is not already a suitable GUID, it will be converted to one.
 *
 * Since: 1.9.6
 **/
#define FU_QUIRKS_GUID_QUIRK "Guid[quirk]"
/**
 * FU_QUIRKS_COUNTERPART_GUID:
 *
 * The quirk key for the counterpart GUID, e.g. `537f7800-8529-5656-b2fa-b0901fe91696`.
 *
 * A counterpart GUID is typically the GUID of the same device in bootloader or runtime mode,
 * if they have a different device PCI or USB ID.
 * Adding this type of GUID does not cause a "cascade" by matching using the quirk database.
 *
 * If the value provided is not already a suitable GUID, it will be converted to one.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_COUNTERPART_GUID "CounterpartGuid"
/**
 * FU_QUIRKS_PARENT_GUID:
 *
 * The quirk key for the parent GUID, e.g. `537f7800-8529-5656-b2fa-b0901fe91696`.
 *
 * If the value provided is not already a suitable GUID, it will be converted to one.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_PARENT_GUID "ParentGuid"
/**
 * FU_QUIRKS_PROXY_GUID:
 *
 * The quirk key for the proxy GUID, e.g. `537f7800-8529-5656-b2fa-b0901fe91696`.
 *
 * Since: 1.4.1
 **/
#define FU_QUIRKS_PROXY_GUID "ProxyGuid"
/**
 * FU_QUIRKS_CHILDREN:
 *
 * The quirk key for the children. This should contain the custom GType, e.g.
 * `FuRts54xxDeviceUSB\VID_0763&PID_2806&I2C_01`.
 *
 * This allows the quirk entry to adds one or more virtual devices to a physical device.
 * If the type of device is not specified the parent device type is used.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_CHILDREN "Children"
/**
 * FU_QUIRKS_VERSION:
 *
 * The quirk key for the version, e.g. `1.2.3`.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_VERSION "Version"
/**
 * FU_QUIRKS_VENDOR:
 *
 * The quirk key for the vendor name, e.g. `Hughski Limited`.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_VENDOR "Vendor"
/**
 * FU_QUIRKS_VENDOR_ID:
 *
 * The quirk key for the vendor ID, e.g. `USB:0x123A`.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_VENDOR_ID "VendorId"
/**
 * FU_QUIRKS_FIRMWARE_SIZE_MIN:
 *
 * The quirk key for the minimum firmware size in bytes, e.g. `512`.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_FIRMWARE_SIZE_MIN "FirmwareSizeMin"
/**
 * FU_QUIRKS_FIRMWARE_SIZE_MAX:
 *
 * The quirk key for the maximum firmware size in bytes, e.g. `1024`.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_FIRMWARE_SIZE_MAX "FirmwareSizeMax"
/**
 * FU_QUIRKS_FIRMWARE_SIZE:
 *
 * The quirk key for the exact required firmware size in bytes, e.g. `1024`.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_FIRMWARE_SIZE "FirmwareSize"
/**
 * FU_QUIRKS_INSTALL_DURATION:
 *
 * The quirk key for the install duration in seconds, e.g. `60`.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_INSTALL_DURATION "InstallDuration"
/**
 * FU_QUIRKS_VERSION_FORMAT:
 *
 * The quirk key for the version format, e.g. `quad`.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_VERSION_FORMAT "VersionFormat"
/**
 * FU_QUIRKS_GTYPE:
 *
 * The quirk key for the custom GType, e.g. `FuCcgxHidDevice`.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_GTYPE "GType"
/**
 * FU_QUIRKS_PROXY_GTYPE:
 *
 * The quirk key for the custom proxy GType, e.g. `FuCcgxHidDevice`.
 *
 * Since: 1.9.15
 **/
#define FU_QUIRKS_PROXY_GTYPE "ProxyGType"
/**
 * FU_QUIRKS_FIRMWARE_GTYPE:
 *
 * The quirk key for the custom firmware GType, e.g. `FuUswidFirmware`.
 *
 * Since: 1.7.2
 **/
#define FU_QUIRKS_FIRMWARE_GTYPE "FirmwareGType"
/**
 * FU_QUIRKS_PROTOCOL:
 *
 * The quirk key for the protocol, e.g. `org.usb.dfu`.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_PROTOCOL "Protocol"
/**
 * FU_QUIRKS_UPDATE_MESSAGE:
 *
 * The quirk key for the update message shown after the transaction has completed.
 *
 * Since: 1.4.0
 **/
#define FU_QUIRKS_UPDATE_MESSAGE "UpdateMessage"
/**
 * FU_QUIRKS_UPDATE_IMAGE:
 *
 * The quirk key for the update image shown before the update is performed.
 *
 * Since: 1.5.0
 **/
#define FU_QUIRKS_UPDATE_IMAGE "UpdateImage"
/**
 * FU_QUIRKS_PRIORITY:
 *
 * The quirk key for the device priority, e.g. `2`.
 *
 * Since: 1.4.1
 **/
#define FU_QUIRKS_PRIORITY "Priority"
/**
 * FU_QUIRKS_BATTERY_THRESHOLD:
 *
 * The quirk key for the battery threshold in percent, e.g. `80`.
 *
 * Since: 1.6.0
 **/
#define FU_QUIRKS_BATTERY_THRESHOLD "BatteryThreshold"
/**
 * FU_QUIRKS_REMOVE_DELAY:
 *
 * The quirk key for the device removal delay in milliseconds, e.g. `2500`.
 *
 * Since: 1.5.0
 **/
#define FU_QUIRKS_REMOVE_DELAY "RemoveDelay"
/**
 * FU_QUIRKS_ACQUIESCE_DELAY:
 *
 * The quirk key for the device removal delay in milliseconds, e.g. `2500`.
 *
 * Since: 1.8.3
 **/
#define FU_QUIRKS_ACQUIESCE_DELAY "AcquiesceDelay"
/**
 * FU_QUIRKS_INHIBIT:
 *
 * The quirk key to inhibit the UPDATABLE flag and to set an update error, e.g. `In safe mode`.
 *
 * Since: 1.6.2
 **/
#define FU_QUIRKS_INHIBIT "Inhibit"
/**
 * FU_QUIRKS_ISSUE:
 *
 * The quirk key to add security issues affecting a specific device, e.g.
 * `https://www.pugetsystems.com/support/guides/critical-samsung-ssd-firmware-update/`.
 *
 * Since: 1.7.6
 **/
#define FU_QUIRKS_ISSUE "Issue"
/**
 * FU_QUIRKS_CFI_DEVICE_CMD_READ_ID
 *
 * The quirk key to set the CFI read ID command, e.g. `0xF8`.
 *
 * Since: 1.8.2
 **/
#define FU_QUIRKS_CFI_DEVICE_CMD_READ_ID "CfiDeviceCmdReadId"
/**
 * FU_QUIRKS_CFI_DEVICE_CMD_READ_ID_SZ
 *
 * The quirk key to set the CFI read ID size, e.g. `0xF8`.
 *
 * Since: 1.8.2
 **/
#define FU_QUIRKS_CFI_DEVICE_CMD_READ_ID_SZ "CfiDeviceCmdReadIdSz"
/**
 * FU_QUIRKS_CFI_DEVICE_CMD_CHIP_ERASE
 *
 * The quirk key to set the CFI chip erase command, e.g. `0xF8`.
 *
 * Since: 1.8.2
 **/
#define FU_QUIRKS_CFI_DEVICE_CMD_CHIP_ERASE "CfiDeviceCmdChipErase"
/**
 * FU_QUIRKS_CFI_DEVICE_CMD_BLOCK_ERASE
 *
 * The quirk key to set the CFI block erase command, e.g. `0xF8`.
 *
 * Since: 1.8.2
 **/
#define FU_QUIRKS_CFI_DEVICE_CMD_BLOCK_ERASE "CfiDeviceCmdBlockErase"
/**
 * FU_QUIRKS_CFI_DEVICE_CMD_SECTOR_ERASE
 *
 * The quirk key to set the CFI sector erase command, e.g. `0xF8`.
 *
 * Since: 1.8.2
 **/
#define FU_QUIRKS_CFI_DEVICE_CMD_SECTOR_ERASE "CfiDeviceCmdSectorErase"
/**
 * FU_QUIRKS_CFI_DEVICE_CMD_WRITE_STATUS
 *
 * The quirk key to set the CFI write status command, e.g. `0xF8`.
 *
 * Since: 1.8.2
 **/
#define FU_QUIRKS_CFI_DEVICE_CMD_WRITE_STATUS "CfiDeviceCmdWriteStatus"
/**
 * FU_QUIRKS_CFI_DEVICE_CMD_PAGE_PROG
 *
 * The quirk key to set the CFI page program command, e.g. `0xF8`.
 *
 * Since: 1.8.2
 **/
#define FU_QUIRKS_CFI_DEVICE_CMD_PAGE_PROG "CfiDeviceCmdPageProg"
/**
 * FU_QUIRKS_CFI_DEVICE_CMD_READ_DATA
 *
 * The quirk key to set the CFI read data command, e.g. `0xF8`.
 *
 * Since: 1.8.2
 **/
#define FU_QUIRKS_CFI_DEVICE_CMD_READ_DATA "CfiDeviceCmdReadData"
/**
 * FU_QUIRKS_CFI_DEVICE_CMD_READ_STATUS
 *
 * The quirk key to set the CFI read status command, e.g. `0xF8`.
 *
 * Since: 1.8.2
 **/
#define FU_QUIRKS_CFI_DEVICE_CMD_READ_STATUS "CfiDeviceCmdReadStatus"
/**
 * FU_QUIRKS_CFI_DEVICE_CMD_WRITE_EN
 *
 * The quirk key to set the CFI write en command, e.g. `0xF8`.
 *
 * Since: 1.8.2
 **/
#define FU_QUIRKS_CFI_DEVICE_CMD_WRITE_EN "CfiDeviceCmdWriteEn"
/**
 * FU_QUIRKS_CFI_DEVICE_PAGE_SIZE
 *
 * The quirk key to set the CFI page size, e.g. `0xF8`.
 *
 * Since: 1.8.2
 **/
#define FU_QUIRKS_CFI_DEVICE_PAGE_SIZE "CfiDevicePageSize"
/**
 * FU_QUIRKS_CFI_DEVICE_SECTOR_SIZE
 *
 * The quirk key to set the CFI sector size, e.g. `0x100`.
 *
 * Since: 1.8.2
 **/
#define FU_QUIRKS_CFI_DEVICE_SECTOR_SIZE "CfiDeviceSectorSize"
/**
 * FU_QUIRKS_CFI_DEVICE_BLOCK_SIZE
 *
 * The quirk key to set the CFI block size, e.g. `0x100`.
 *
 * Since: 1.8.2
 **/
#define FU_QUIRKS_CFI_DEVICE_BLOCK_SIZE "CfiDeviceBlockSize"
/**
 * FU_QUIRKS_PLUGIN_INHIBIT:
 *
 * The quirk key for the list of plugin inhibits, split by comma if required.
 *
 * Since: 1.9.22
 **/
#define FU_QUIRKS_PLUGIN_INHIBIT "PluginInhibit"
