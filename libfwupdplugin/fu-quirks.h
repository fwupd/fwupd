/*
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
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
 * @user_data: user data
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
	       GError **error) G_GNUC_WARN_UNUSED_RESULT;
const gchar *
fu_quirks_lookup_by_id(FuQuirks *self, const gchar *guid, const gchar *key);
gboolean
fu_quirks_lookup_by_id_iter(FuQuirks *self,
			    const gchar *guid,
			    FuQuirksIter iter_cb,
			    gpointer user_data);
void
fu_quirks_add_possible_key(FuQuirks *self, const gchar *possible_key);

/**
 * FU_QUIRKS_PLUGIN:
 *
 * The quirk key for the plugin name.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_PLUGIN "Plugin"
/**
 * FU_QUIRKS_FLAGS:
 *
 * The quirk key for the public flags.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_FLAGS "Flags"
/**
 * FU_QUIRKS_SUMMARY:
 *
 * The quirk key for the summary.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_SUMMARY "Summary"
/**
 * FU_QUIRKS_ICON:
 *
 * The quirk key for the icon.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_ICON "Icon"
/**
 * FU_QUIRKS_NAME:
 *
 * The quirk key for the name.
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
 * The quirk key for the GUID.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_GUID "Guid"
/**
 * FU_QUIRKS_COUNTERPART_GUID:
 *
 * The quirk key for the counterpart GUID.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_COUNTERPART_GUID "CounterpartGuid"
/**
 * FU_QUIRKS_PARENT_GUID:
 *
 * The quirk key for the parent GUID.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_PARENT_GUID "ParentGuid"
/**
 * FU_QUIRKS_PROXY_GUID:
 *
 * The quirk key for the proxy GUID.
 *
 * Since: 1.4.1
 **/
#define FU_QUIRKS_PROXY_GUID "ProxyGuid"
/**
 * FU_QUIRKS_CHILDREN:
 *
 * The quirk key for the children. This should contain the custom GType.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_CHILDREN "Children"
/**
 * FU_QUIRKS_VERSION:
 *
 * The quirk key for the version.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_VERSION "Version"
/**
 * FU_QUIRKS_VENDOR:
 *
 * The quirk key for the vendor name.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_VENDOR "Vendor"
/**
 * FU_QUIRKS_VENDOR_ID:
 *
 * The quirk key for the vendor ID.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_VENDOR_ID "VendorId"
/**
 * FU_QUIRKS_FIRMWARE_SIZE_MIN:
 *
 * The quirk key for the minimum firmware size in bytes.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_FIRMWARE_SIZE_MIN "FirmwareSizeMin"
/**
 * FU_QUIRKS_FIRMWARE_SIZE_MAX:
 *
 * The quirk key for the maximum firmware size in bytes.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_FIRMWARE_SIZE_MAX "FirmwareSizeMax"
/**
 * FU_QUIRKS_FIRMWARE_SIZE:
 *
 * The quirk key for the exact required firmware size in bytes.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_FIRMWARE_SIZE "FirmwareSize"
/**
 * FU_QUIRKS_INSTALL_DURATION:
 *
 * The quirk key for the install duration in seconds.
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
 * The quirk key for the custom GType.
 *
 * Since: 1.3.7
 **/
#define FU_QUIRKS_GTYPE "GType"
/**
 * FU_QUIRKS_FIRMWARE_GTYPE:
 *
 * The quirk key for the custom firmware GType.
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
 * The quirk key for the device priority.
 *
 * Since: 1.4.1
 **/
#define FU_QUIRKS_PRIORITY "Priority"
/**
 * FU_QUIRKS_BATTERY_THRESHOLD:
 *
 * The quirk key for the battery threshold in percent.
 *
 * Since: 1.6.0
 **/
#define FU_QUIRKS_BATTERY_THRESHOLD "BatteryThreshold"
/**
 * FU_QUIRKS_REMOVE_DELAY:
 *
 * The quirk key for the device removal delay in milliseconds.
 *
 * Since: 1.5.0
 **/
#define FU_QUIRKS_REMOVE_DELAY "RemoveDelay"
/**
 * FU_QUIRKS_INHIBIT:
 *
 * The quirk key to inhibit the UPDATABLE flag and to set an update error.
 *
 * Since: 1.6.2
 **/
#define FU_QUIRKS_INHIBIT "Inhibit"
/**
 * FU_QUIRKS_ISSUE:
 *
 * The quirk key to add security issues affecting a specific device.
 *
 * Since: 1.7.6
 **/
#define FU_QUIRKS_ISSUE "Issue"
