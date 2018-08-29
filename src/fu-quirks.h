/*
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_QUIRKS_H
#define __FU_QUIRKS_H

G_BEGIN_DECLS

#include <glib-object.h>

#define FU_TYPE_QUIRKS (fu_quirks_get_type ())
G_DECLARE_FINAL_TYPE (FuQuirks, fu_quirks, FU, QUIRKS, GObject)

FuQuirks	*fu_quirks_new				(void);
gboolean	 fu_quirks_load				(FuQuirks	*self,
							 GError		**error);
const gchar	*fu_quirks_lookup_by_id			(FuQuirks	*self,
							 const gchar	*group,
							 const gchar	*key);
const gchar	*fu_quirks_lookup_by_guid		(FuQuirks	*self,
							 const gchar	*guid,
							 const gchar	*key);
gchar		*fu_quirks_lookup_by_guids		(FuQuirks	*self,
							 GPtrArray	*guids,
							 const gchar	*key);
void		 fu_quirks_add_value			(FuQuirks	*self,
							 const gchar	*group,
							 const gchar	*key,
							 const gchar	*value);
gboolean	 fu_quirks_get_kvs_for_guid		(FuQuirks	*self,
							 const gchar	*guid,
							 GHashTableIter *iter);

/**
 * FU_QUIRKS_PLUGIN:
 * @key: the USB device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
 * @value: the plugin name, e.g. `csr`
 *
 * Sets the plugin to use for a specific hardware device.
 *
 * Since: 1.1.0
 */
#define	FU_QUIRKS_PLUGIN			"Plugin"

/**
 * FU_QUIRKS_UEFI_VERSION_FORMAT:
 * @key: a %FU_HWIDS_KEY_MANUFACTURER, e.g. `Alienware`
 * @value: the version format, e.g. `none`
 *
 * Assigns the version format to use for a specific manufacturer.
 * A specific version format is sometimes chosen to match the appearance of
 * other systems or specifications.
 *
 * Default value: `use-triplet`
 *
 * Since: 1.0.1
 */
#define	FU_QUIRKS_UEFI_VERSION_FORMAT		"UefiVersionFormat"

/**
 * FU_QUIRKS_DAEMON_VERSION_FORMAT:
 * @key: the optionally wildcarded AppStream ID e.g. `com.dell.uefi*.firmware`
 * @value: the version format, e.g. `none`
 *
 * Assigns the version format to use for a specific AppStream component.
 * A specific version format is sometimes chosen to match the appearance of
 * other systems or specifications.
 *
 * Default value: `use-triplet`
 *
 * Since: 1.0.1
 */
#define	FU_QUIRKS_DAEMON_VERSION_FORMAT		"ComponentIDs"

/**
 * FU_QUIRKS_FLAGS:
 * @key: the USB device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
 * @value: the quirk, e.g. `is-bootloader`
 *
 * Assigns optional quirks to use for a 8Bitdo device. The list of supported
 * quirks is thus:
 *
 * * `none`:			No device quirks
 * * `is-bootloader`:		Device is in bootloader mode
 *
 * Since: 1.0.3
 */
#define	FU_QUIRKS_FLAGS				"Flags"

/**
 * FU_QUIRKS_SUMMARY:
 * @key: the USB device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
 * @value: the USB device summary, e.g. `An open source display colorimeter`
 *
 * Sets a name for a specific hardware device.
 *
 * Since: 1.0.2
 */
#define	FU_QUIRKS_SUMMARY			"Summary"

/**
 * FU_QUIRKS_ICON:
 * @key: the USB device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
 * @value: the USB device icon name, e.g. `media-removable`
 *
 * Adds an icon name for a specific hardware device.
 *
 * Since: 1.0.2
 */
#define	FU_QUIRKS_ICON				"Icon"

/**
 * FU_QUIRKS_NAME:
 * @key: the USB device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
 * @value: the USB device name, e.g. `ColorHug`
 *
 * Sets a name for a specific hardware device.
 *
 * Since: 1.0.2
 */
#define	FU_QUIRKS_NAME				"Name"

/**
 * FU_QUIRKS_GUID:
 * @key: the USB device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
 * @value: the GUID, e.g. `537f7800-8529-5656-b2fa-b0901fe91696`
 *
 * Adds an extra GUID for a specific hardware device. If the value provided is
 * not already a suitable GUID, it will be converted to one.
 *
 * Since: 1.0.3
 */
#define	FU_QUIRKS_GUID				"Guid"

/**
 * FU_QUIRKS_PARENT_GUID:
 * @key: the USB device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
 * @value: the GUID, e.g. `537f7800-8529-5656-b2fa-b0901fe91696`
 *
 * Adds an extra GUID to mark as the parent device. If the value provided is
 * not already a suitable GUID, it will be converted to one.
 *
 * Since: 1.1.2
 */
#define	FU_QUIRKS_PARENT_GUID			"ParentGuid"

/**
 * FU_QUIRKS_CHILDREN:
 * @key: the USB device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
 * @value: the GUID, e.g. `USB\VID_0763&PID_2806&I2C_01`
 *
 * Adds one or more virtual devices to a physical device, delimited by comma.
 *
 * To set the object type of the child device use a pipe before the object type,
 * for instance: `FuRts54xxDevice|USB\VID_0763&PID_2806&I2C_01`
 * If the type of device is not specified the parent device type is used.
 *
 * If the values provided are not already suitable GUIDs, they will be
 * converted.
 *
 * Since: 1.1.2
 */
#define	FU_QUIRKS_CHILDREN			"Children"

/**
 * FU_QUIRKS_VERSION:
 * @key: the USB device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806&REV_0001`
 * @value: the version number, e.g. `1.2`
 *
 * Sets a version for a specific hardware device.
 *
 * Since: 1.0.3
 */
#define	FU_QUIRKS_VERSION			"Version"

/**
 * FU_QUIRKS_VENDOR:
 * @key: the USB device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
 * @value: the vendor, e.g. `Hughski Limited`
 *
 * Sets a vendor name for a specific hardware device.
 *
 * Since: 1.0.3
 */
#define	FU_QUIRKS_VENDOR			"Vendor"

/**
 * FU_QUIRKS_FIRMWARE_SIZE_MIN:
 * @key: the USB device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
 * @value: the vendor, e.g. `0x10000`
 *
 * Sets the minimum allowed firmware size.
 *
 * Since: 1.1.2
 */
#define	FU_QUIRKS_FIRMWARE_SIZE_MIN		"FirmwareSizeMin"

/**
 * FU_QUIRKS_FIRMWARE_SIZE_MAX:
 * @key: the USB device ID, e.g. `DeviceInstanceId=USB\VID_0763&PID_2806`
 * @value: the vendor, e.g. `0x10000`
 *
 * Sets the maximum allowed firmware size.
 *
 * Since: 1.1.2
 */
#define	FU_QUIRKS_FIRMWARE_SIZE_MAX		"FirmwareSizeMax"

G_END_DECLS

#endif /* __FU_QUIRKS_H */
