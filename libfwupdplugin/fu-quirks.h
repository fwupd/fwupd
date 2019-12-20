/*
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#define FU_TYPE_QUIRKS (fu_quirks_get_type ())
G_DECLARE_FINAL_TYPE (FuQuirks, fu_quirks, FU, QUIRKS, GObject)

/**
 * FuQuirksLoadFlags:
 * @FU_QUIRKS_LOAD_FLAG_NONE:		No flags set
 * @FU_QUIRKS_LOAD_FLAG_READONLY_FS:	Ignore readonly filesystem errors
 *
 * The flags to use when loading quirks.
 **/
typedef enum {
	FU_QUIRKS_LOAD_FLAG_NONE		= 0,
	FU_QUIRKS_LOAD_FLAG_READONLY_FS		= 1 << 0,
	/*< private >*/
	FU_QUIRKS_LOAD_FLAG_LAST
} FuQuirksLoadFlags;

typedef void	(*FuQuirksIter)				(FuQuirks	*self,
							 const gchar	*key,
							 const gchar	*value,
							 gpointer	 user_data);

FuQuirks	*fu_quirks_new				(void);
gboolean	 fu_quirks_load				(FuQuirks	*self,
							 FuQuirksLoadFlags load_flags,
							 GError		**error);
const gchar	*fu_quirks_lookup_by_id			(FuQuirks	*self,
							 const gchar	*group,
							 const gchar	*key);
gboolean	 fu_quirks_lookup_by_id_iter		(FuQuirks	*self,
							 const gchar	*group,
							 FuQuirksIter	 iter_cb,
							 gpointer	 user_data);

#define	FU_QUIRKS_PLUGIN			"Plugin"
#define	FU_QUIRKS_UEFI_VERSION_FORMAT		"UefiVersionFormat"
#define	FU_QUIRKS_DAEMON_VERSION_FORMAT		"ComponentIDs"
#define	FU_QUIRKS_FLAGS				"Flags"
#define	FU_QUIRKS_SUMMARY			"Summary"
#define	FU_QUIRKS_ICON				"Icon"
#define	FU_QUIRKS_NAME				"Name"
#define	FU_QUIRKS_GUID				"Guid"
#define	FU_QUIRKS_COUNTERPART_GUID		"CounterpartGuid"
#define	FU_QUIRKS_PARENT_GUID			"ParentGuid"
#define	FU_QUIRKS_CHILDREN			"Children"
#define	FU_QUIRKS_VERSION			"Version"
#define	FU_QUIRKS_VENDOR			"Vendor"
#define	FU_QUIRKS_VENDOR_ID			"VendorId"
#define	FU_QUIRKS_FIRMWARE_SIZE_MIN		"FirmwareSizeMin"
#define	FU_QUIRKS_FIRMWARE_SIZE_MAX		"FirmwareSizeMax"
#define	FU_QUIRKS_FIRMWARE_SIZE			"FirmwareSize"
#define	FU_QUIRKS_INSTALL_DURATION		"InstallDuration"
#define	FU_QUIRKS_VERSION_FORMAT		"VersionFormat"
#define	FU_QUIRKS_GTYPE				"GType"
#define	FU_QUIRKS_PROTOCOL			"Protocol"
