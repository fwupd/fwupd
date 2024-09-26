/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-build.h"

G_BEGIN_DECLS

/**
 * FWUPD_DBUS_PATH:
 *
 * The dbus path
 **/
#define FWUPD_DBUS_PATH "/"
/**
 * FWUPD_DBUS_SERVICE:
 *
 * The dbus service
 **/
#define FWUPD_DBUS_SERVICE "org.freedesktop.fwupd"
/**
 * FWUPD_DBUS_INTERFACE:
 *
 * The dbus interface
 **/
#define FWUPD_DBUS_INTERFACE "org.freedesktop.fwupd"

/**
 * FWUPD_DEVICE_ID_ANY:
 *
 * Wildcard used for matching all device ids in fwupd
 **/
#define FWUPD_DEVICE_ID_ANY "*"

/**
 * FwupdGuidFlags:
 *
 * The flags to show how the data should be converted.
 **/
typedef enum {
	/**
	 * FWUPD_GUID_FLAG_NONE:
	 *
	 * No endian swapping.
	 *
	 * Since: 1.2.5
	 */
	FWUPD_GUID_FLAG_NONE = 0,
	/**
	 * FWUPD_GUID_FLAG_NAMESPACE_MICROSOFT:
	 *
	 * Use the Microsoft-compatible namespace.
	 *
	 * Since: 1.2.5
	 */
	FWUPD_GUID_FLAG_NAMESPACE_MICROSOFT = 1 << 0,
	/**
	 * FWUPD_GUID_FLAG_MIXED_ENDIAN:
	 *
	 * Use EFI mixed endian representation, as used in EFI.
	 *
	 * Since: 1.2.5
	 */
	FWUPD_GUID_FLAG_MIXED_ENDIAN = 1 << 1,
	/*< private >*/
	FWUPD_GUID_FLAG_LAST
} FwupdGuidFlags;

/* GObject Introspection does not understand typedefs with sizes */
#ifdef __GI_SCANNER__
typedef guint8 *fwupd_guid_t;
#else
typedef guint8 fwupd_guid_t[16];
#endif

const gchar *
fwupd_checksum_get_best(GPtrArray *checksums) G_GNUC_NON_NULL(1);
const gchar *
fwupd_checksum_get_by_kind(GPtrArray *checksums, GChecksumType kind) G_GNUC_NON_NULL(1);
GChecksumType
fwupd_checksum_guess_kind(const gchar *checksum) G_GNUC_NON_NULL(1);
const gchar *
fwupd_checksum_type_to_string_display(GChecksumType checksum_type);
gchar *
fwupd_checksum_format_for_display(const gchar *checksum) G_GNUC_NON_NULL(1);

gboolean
fwupd_device_id_is_valid(const gchar *device_id);
#ifndef __GI_SCANNER__
gchar *
fwupd_guid_to_string(const fwupd_guid_t *guid, FwupdGuidFlags flags) G_GNUC_NON_NULL(1);
gboolean
fwupd_guid_from_string(const gchar *guidstr,
		       fwupd_guid_t *guid,
		       FwupdGuidFlags flags,
		       GError **error) G_GNUC_NON_NULL(1);
#else
gchar *
fwupd_guid_to_string(const guint8 guid[16], FwupdGuidFlags flags);
gboolean
fwupd_guid_from_string(const gchar *guidstr, guint8 guid[16], FwupdGuidFlags flags, GError **error);
#endif
gboolean
fwupd_guid_is_valid(const gchar *guid);
gchar *
fwupd_guid_hash_string(const gchar *str);
gchar *
fwupd_guid_hash_data(const guint8 *data, gsize datasz, FwupdGuidFlags flags) G_GNUC_NON_NULL(1);

G_END_DECLS
