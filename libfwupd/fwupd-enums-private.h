/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/**
 * FWUPD_RESULT_KEY_APPSTREAM_ID:
 *
 * Result key to represent AppstreamId
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_APPSTREAM_ID "AppstreamId"
/**
 * FWUPD_RESULT_KEY_RELEASE_ID:
 *
 * Result key to represent the release ID.
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_RELEASE_ID "ReleaseId"
/**
 * FWUPD_RESULT_KEY_CHECKSUM:
 *
 * Result key to represent Checksum
 *
 * The D-Bus type signature string is 'as' i.e. an array of strings.
 **/
#define FWUPD_RESULT_KEY_CHECKSUM "Checksum"
/**
 * FWUPD_RESULT_KEY_TAGS:
 *
 * Result key to represent release tags
 *
 * The D-Bus type signature string is 'as' i.e. an array of strings.
 **/
#define FWUPD_RESULT_KEY_TAGS "Tags"
/**
 * FWUPD_RESULT_KEY_CREATED:
 *
 * Result key to represent Created
 *
 * The D-Bus type signature string is 't' i.e. a unsigned 64 bit integer.
 **/
#define FWUPD_RESULT_KEY_CREATED "Created"
/**
 * FWUPD_RESULT_KEY_DESCRIPTION:
 *
 * Result key to represent Description
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_DESCRIPTION "Description"
/**
 * FWUPD_RESULT_KEY_DETACH_CAPTION:
 *
 * Result key to represent DetachCaption
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_DETACH_CAPTION "DetachCaption"
/**
 * FWUPD_RESULT_KEY_DETACH_IMAGE:
 *
 * Result key to represent DetachImage
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_DETACH_IMAGE "DetachImage"
/**
 * FWUPD_RESULT_KEY_DEVICE_ID:
 *
 * Result key to represent DeviceId
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_DEVICE_ID "DeviceId"
/**
 * FWUPD_RESULT_KEY_PARENT_DEVICE_ID:
 *
 * Result key to represent ParentDeviceId
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_PARENT_DEVICE_ID "ParentDeviceId"
/**
 * FWUPD_RESULT_KEY_COMPOSITE_ID:
 *
 * Result key to represent CompositeId
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_COMPOSITE_ID "CompositeId"
/**
 * FWUPD_RESULT_KEY_FILENAME:
 *
 * Result key to represent Filename
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_FILENAME "Filename"
/**
 * FWUPD_RESULT_KEY_PROTOCOL:
 *
 * Result key to represent Protocol
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_PROTOCOL "Protocol"
/**
 * FWUPD_RESULT_KEY_CATEGORIES:
 *
 * Result key to represent Categories
 *
 * The D-Bus type signature string is 'as' i.e. an array of strings.
 **/
#define FWUPD_RESULT_KEY_CATEGORIES "Categories"
/**
 * FWUPD_RESULT_KEY_ISSUES:
 *
 * Result key to represent Issues
 *
 * The D-Bus type signature string is 'as' i.e. an array of strings.
 **/
#define FWUPD_RESULT_KEY_ISSUES "Issues"
/**
 * FWUPD_RESULT_KEY_FLAGS:
 *
 * Result key to represent Flags
 *
 * The D-Bus type signature string is 't' i.e. a unsigned 64 bit integer.
 **/
#define FWUPD_RESULT_KEY_FLAGS "Flags"
/**
 * FWUPD_RESULT_KEY_FLASHES_LEFT:
 *
 * Result key to represent FlashesLeft
 *
 * The D-Bus type signature string is 'u' i.e. a unsigned 32 bit integer.
 **/
#define FWUPD_RESULT_KEY_FLASHES_LEFT "FlashesLeft"
/**
 * FWUPD_RESULT_KEY_URGENCY:
 *
 * Result key to represent Urgency
 *
 * The D-Bus type signature string is 'u' i.e. a unsigned 32 bit integer.
 **/
#define FWUPD_RESULT_KEY_URGENCY "Urgency"
/**
 * FWUPD_RESULT_KEY_REQUEST_KIND:
 *
 * Result key to represent RequestKind
 *
 * The D-Bus type signature string is 'u' i.e. a unsigned 32 bit integer.
 **/
#define FWUPD_RESULT_KEY_REQUEST_KIND "RequestKind"
/**
 * FWUPD_RESULT_KEY_HSI_LEVEL:
 *
 * Result key to represent HsiLevel
 *
 * The D-Bus type signature string is 'u' i.e. a unsigned 32 bit integer.
 **/
#define FWUPD_RESULT_KEY_HSI_LEVEL "HsiLevel"
/**
 * FWUPD_RESULT_KEY_HSI_RESULT:
 *
 * Result key to represent HsiResult
 *
 * The D-Bus type signature string is 'u' i.e. a unsigned 32 bit integer.
 **/
#define FWUPD_RESULT_KEY_HSI_RESULT "HsiResult"
/**
 * FWUPD_RESULT_KEY_HSI_RESULT_FALLBACK:
 *
 * Result key to represent the fallback HsiResult
 *
 * The D-Bus type signature string is 'u' i.e. a unsigned 32 bit integer.
 **/
#define FWUPD_RESULT_KEY_HSI_RESULT_FALLBACK "HsiResultFallback"
/**
 * FWUPD_RESULT_KEY_INSTALL_DURATION:
 *
 * Result key to represent InstallDuration
 *
 * The D-Bus type signature string is 'u' i.e. a unsigned 32 bit integer.
 **/
#define FWUPD_RESULT_KEY_INSTALL_DURATION "InstallDuration"
/**
 * FWUPD_RESULT_KEY_GUID:
 *
 * Result key to represent Guid
 *
 * The D-Bus type signature string is 'as' i.e. an array of strings.
 **/
#define FWUPD_RESULT_KEY_GUID "Guid"
/**
 * FWUPD_RESULT_KEY_INSTANCE_IDS:
 *
 * Result key to represent InstanceIds
 *
 * The D-Bus type signature string is 'as' i.e. an array of strings.
 **/
#define FWUPD_RESULT_KEY_INSTANCE_IDS "InstanceIds"
/**
 * FWUPD_RESULT_KEY_HOMEPAGE:
 *
 * Result key to represent Homepage
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_HOMEPAGE "Homepage"
/**
 * FWUPD_RESULT_KEY_DETAILS_URL:
 *
 * Result key to represent DetailsUrl
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_DETAILS_URL "DetailsUrl"
/**
 * FWUPD_RESULT_KEY_SOURCE_URL:
 *
 * Result key to represent SourceUrl
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_SOURCE_URL "SourceUrl"
/**
 * FWUPD_RESULT_KEY_ICON:
 *
 * Result key to represent Icon
 *
 * The D-Bus type signature string is 'as' i.e. an array of strings.
 **/
#define FWUPD_RESULT_KEY_ICON "Icon"
/**
 * FWUPD_RESULT_KEY_LICENSE:
 *
 * Result key to represent License
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_LICENSE "License"
/**
 * FWUPD_RESULT_KEY_MODIFIED:
 *
 * Result key to represent Modified
 *
 * The D-Bus type signature string is 't' i.e. a unsigned 64 bit integer.
 **/
#define FWUPD_RESULT_KEY_MODIFIED "Modified"
/**
 * FWUPD_RESULT_KEY_VERSION_BUILD_DATE:
 *
 * Result key to represent VersionBuildDate
 *
 * The D-Bus type signature string is 't' i.e. a unsigned 64 bit integer.
 **/
#define FWUPD_RESULT_KEY_VERSION_BUILD_DATE "VersionBuildDate"
/**
 * FWUPD_RESULT_KEY_METADATA:
 *
 * Result key to represent Metadata
 *
 * The D-Bus type signature string is 'a{ss}' i.e. a string dictionary.
 **/
#define FWUPD_RESULT_KEY_METADATA "Metadata"
/**
 * FWUPD_RESULT_KEY_NAME:
 *
 * Result key to represent Name
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_NAME "Name"
/**
 * FWUPD_RESULT_KEY_NAME_VARIANT_SUFFIX:
 *
 * Result key to represent NameVariantSuffix
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_NAME_VARIANT_SUFFIX "NameVariantSuffix"
/**
 * FWUPD_RESULT_KEY_PLUGIN:
 *
 * Result key to represent Plugin
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_PLUGIN "Plugin"
/**
 * FWUPD_RESULT_KEY_RELEASE:
 *
 * Result key to represent Release
 *
 * The D-Bus type signature string is 'a{sv}' i.e. a variant dictionary.
 **/
#define FWUPD_RESULT_KEY_RELEASE "Release"
/**
 * FWUPD_RESULT_KEY_REMOTE_ID:
 *
 * Result key to represent RemoteId
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_REMOTE_ID "RemoteId"
/**
 * FWUPD_RESULT_KEY_SERIAL:
 *
 * Result key to represent Serial
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_SERIAL "Serial"
/**
 * FWUPD_RESULT_KEY_SIZE:
 *
 * Result key to represent Size
 *
 * The D-Bus type signature string is 't' i.e. a unsigned 64 bit integer.
 **/
#define FWUPD_RESULT_KEY_SIZE "Size"
/**
 * FWUPD_RESULT_KEY_STATUS:
 *
 * Result key to represent Status
 *
 * The D-Bus type signature string is 'u' i.e. a unsigned 32 bit integer.
 **/
#define FWUPD_RESULT_KEY_STATUS "Status"
/**
 * FWUPD_RESULT_KEY_SUMMARY:
 *
 * Result key to represent Summary
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_SUMMARY "Summary"
/**
 * FWUPD_RESULT_KEY_BRANCH:
 *
 * Result key to represent Branch
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_BRANCH "Branch"
/**
 * FWUPD_RESULT_KEY_TRUST_FLAGS:
 *
 * Result key to represent TrustFlags
 *
 * The D-Bus type signature string is 't' i.e. a unsigned 64 bit integer.
 **/
#define FWUPD_RESULT_KEY_TRUST_FLAGS "TrustFlags"
/**
 * FWUPD_RESULT_KEY_PROBLEMS:
 *
 * Result key to represent problems
 *
 * The D-Bus type signature string is 't' i.e. a unsigned 64 bit integer.
 **/
#define FWUPD_RESULT_KEY_PROBLEMS "Problems"
/**
 * FWUPD_RESULT_KEY_UPDATE_MESSAGE:
 *
 * Result key to represent UpdateMessage
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_UPDATE_MESSAGE "UpdateMessage"
/**
 * FWUPD_RESULT_KEY_UPDATE_IMAGE:
 *
 * Result key to represent UpdateImage
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_UPDATE_IMAGE "UpdateImage"
/**
 * FWUPD_RESULT_KEY_UPDATE_ERROR:
 *
 * Result key to represent UpdateError
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_UPDATE_ERROR "UpdateError"
/**
 * FWUPD_RESULT_KEY_UPDATE_STATE:
 *
 * Result key to represent UpdateState
 *
 * The D-Bus type signature string is 'u' i.e. a unsigned 32 bit integer.
 **/
#define FWUPD_RESULT_KEY_UPDATE_STATE "UpdateState"
/**
 * FWUPD_RESULT_KEY_URI:
 *
 * Result key to represent Uri
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_URI "Uri"
/**
 * FWUPD_RESULT_KEY_LOCATIONS:
 *
 * Result key to represent Locations
 *
 * The D-Bus type signature string is 'as' i.e. an array of strings.
 **/
#define FWUPD_RESULT_KEY_LOCATIONS "Locations"
/**
 * FWUPD_RESULT_KEY_VENDOR_ID:
 *
 * Result key to represent VendorId
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_VENDOR_ID "VendorId"
/**
 * FWUPD_RESULT_KEY_VENDOR:
 *
 * Result key to represent Vendor
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_VENDOR "Vendor"
/**
 * FWUPD_RESULT_KEY_VERSION_BOOTLOADER:
 *
 * Result key to represent VersionBootloader
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_VERSION_BOOTLOADER "VersionBootloader"
/**
 * FWUPD_RESULT_KEY_VERSION_BOOTLOADER_RAW:
 *
 * Result key to represent VersionBootloaderRaw
 *
 * The D-Bus type signature string is 't' i.e. a unsigned 64 bit integer.
 **/
#define FWUPD_RESULT_KEY_VERSION_BOOTLOADER_RAW "VersionBootloaderRaw"
/**
 * FWUPD_RESULT_KEY_VERSION_FORMAT:
 *
 * Result key to represent VersionFormat
 *
 * The D-Bus type signature string is 'u' i.e. a unsigned 32 bit integer.
 **/
#define FWUPD_RESULT_KEY_VERSION_FORMAT "VersionFormat"
/**
 * FWUPD_RESULT_KEY_VERSION_RAW:
 *
 * Result key to represent VersionRaw
 *
 * The D-Bus type signature string is 't' i.e. a unsigned 64 bit integer.
 **/
#define FWUPD_RESULT_KEY_VERSION_RAW "VersionRaw"
/**
 * FWUPD_RESULT_KEY_VERSION_LOWEST:
 *
 * Result key to represent VersionLowest
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_VERSION_LOWEST "VersionLowest"
/**
 * FWUPD_RESULT_KEY_VERSION_LOWEST_RAW:
 *
 * Result key to represent VersionLowestRaw
 *
 * The D-Bus type signature string is 't' i.e. a unsigned 64 bit integer.
 **/
#define FWUPD_RESULT_KEY_VERSION_LOWEST_RAW "VersionLowestRaw"
/**
 * FWUPD_RESULT_KEY_VERSION:
 *
 * Result key to represent Version
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_VERSION "Version"
/**
 * FWUPD_RESULT_KEY_BATTERY_LEVEL:
 *
 * Result key to represent the current battery level in percent.
 * Expressed from 0-100%, or 101 for invalid or unset.
 *
 * The D-Bus type signature string is 'u' i.e. a unsigned 32 bit integer.
 **/
#define FWUPD_RESULT_KEY_BATTERY_LEVEL "BatteryLevel"
/**
 * FWUPD_RESULT_KEY_BATTERY_THRESHOLD:
 *
 * Result key to represent the minimum battery level required to perform an update.
 * Expressed from 0-100%, or 101 for invalid or unset.
 *
 * The D-Bus type signature string is 'u' i.e. a unsigned 32 bit integer.
 **/
#define FWUPD_RESULT_KEY_BATTERY_THRESHOLD "BatteryThreshold"
/**
 * FWUPD_RESULT_KEY_BIOS_SETTING_ID:
 *
 * Result key to represent the unique identifier of the BIOS setting.
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_BIOS_SETTING_ID "BiosSettingId"
/**
 * FWUPD_RESULT_KEY_BIOS_SETTING_TARGET_VALUE:
 *
 * Result key to represent the value that would enable this attribute.
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_BIOS_SETTING_TARGET_VALUE "BiosSettingTargetValue"
/**
 * FWUPD_RESULT_KEY_BIOS_SETTING_CURRENT_VALUE:
 *
 * Result key to represent the current value of BIOS setting.
 *
 * The D-Bus type signature string is 's' i.e. a string.
 **/
#define FWUPD_RESULT_KEY_BIOS_SETTING_CURRENT_VALUE "BiosSettingCurrentValue"
/**
 * FWUPD_RESULT_KEY_BIOS_SETTING_TYPE:
 *
 * Result key to represent the type of BIOS setting.
 * 0 is invalid, 1+ represent an attribute type
 *
 * The D-Bus type signature string is 't' i.e. a unsigned 64 bit integer.
 **/
#define FWUPD_RESULT_KEY_BIOS_SETTING_TYPE "BiosSettingType"
/**
 * FWUPD_RESULT_KEY_BIOS_SETTING_POSSIBLE_VALUES:
 *
 * Result key to represent possible values
 *
 * The D-Bus type signature string is 'as' i.e. an array of strings.
 **/
#define FWUPD_RESULT_KEY_BIOS_SETTING_POSSIBLE_VALUES "BiosSettingPossibleValues"
/**
 * FWUPD_RESULT_KEY_BIOS_SETTING_LOWER_BOUND:
 *
 * Result key to represent the upper bound for an integer BIOS setting.
 * or minimum length for string BIOS setting.
 * The D-Bus type signature string is 't' i.e. a unsigned 64 bit integer.
 **/
#define FWUPD_RESULT_KEY_BIOS_SETTING_LOWER_BOUND "BiosSettingLowerBound"
/**
 * FWUPD_RESULT_KEY_BIOS_SETTING_UPPER_BOUND:
 *
 * Result key to represent the lower bound for an integer BIOS setting
 * or maximum length for string BIOS setting.
 *
 * The D-Bus type signature string is 't' i.e. a unsigned 64 bit integer.
 **/
#define FWUPD_RESULT_KEY_BIOS_SETTING_UPPER_BOUND "BiosSettingUpperBound"
/**
 * FWUPD_RESULT_KEY_BIOS_SETTING_SCALAR_INCREMENT:
 *
 * Result key to represent the scalar increment for an integer BIOS setting.
 *
 * The D-Bus type signature string is 't' i.e. a unsigned 64 bit integer.
 **/
#define FWUPD_RESULT_KEY_BIOS_SETTING_SCALAR_INCREMENT "BiosSettingScalarIncrement"
/**
 * FWUPD_RESULT_KEY_BIOS_SETTING_READ_ONLY:
 *
 * Result key to represent whether BIOS setting is read only
 *
 * The D-Bus type signature string is 'b' i.e. a boolean.
 **/
#define FWUPD_RESULT_KEY_BIOS_SETTING_READ_ONLY "BiosSettingReadOnly"

G_END_DECLS
