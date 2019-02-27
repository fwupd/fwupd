/*
 * Copyright (C) 2015-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define FWUPD_DBUS_PATH			"/"
#define FWUPD_DBUS_SERVICE		"org.freedesktop.fwupd"
#define FWUPD_DBUS_INTERFACE		"org.freedesktop.fwupd"

#define FWUPD_DEVICE_ID_ANY		"*"

/**
 * FwupdGuidFlags:
 * @FWUPD_GUID_FLAG_NONE:			No trust
 * @FWUPD_GUID_FLAG_NAMESPACE_MICROSOFT:	Use the Microsoft-compatible namespace
 * @FWUPD_GUID_FLAG_MIXED_ENDIAN:		Use EFI mixed endian representation
 *
 * The flags to show how the data should be converted.
 **/
typedef enum {
	FWUPD_GUID_FLAG_NONE			= 0,		/* Since: 1.2.5 */
	FWUPD_GUID_FLAG_NAMESPACE_MICROSOFT	= 1 << 0,	/* Since: 1.2.5 */
	FWUPD_GUID_FLAG_MIXED_ENDIAN		= 1 << 1,	/* Since: 1.2.5 */
	/*< private >*/
	FWUPD_GUID_FLAG_LAST
} FwupdGuidFlags;

/* GObject Introspection does not understand typedefs with sizes */
#ifndef __GI_SCANNER__
typedef guint8 fwupd_guid_t[16];
#endif

const gchar	*fwupd_checksum_get_best		(GPtrArray	*checksums);
const gchar	*fwupd_checksum_get_by_kind		(GPtrArray	*checksums,
							 GChecksumType	 kind);
GChecksumType	 fwupd_checksum_guess_kind		(const gchar	*checksum);
gchar		*fwupd_build_user_agent			(const gchar	*package_name,
							 const gchar	*package_version);
gchar		*fwupd_build_machine_id			(const gchar 	*salt,
							 GError		**error);
GHashTable	*fwupd_get_os_release			(GError		**error);
gchar		*fwupd_build_history_report_json	(GPtrArray	*devices,
							 GError		**error);
#ifndef __GI_SCANNER__
gchar		*fwupd_guid_to_string			(const fwupd_guid_t *guid,
							 FwupdGuidFlags	 flags);
gboolean	 fwupd_guid_from_string			(const gchar	*guidstr,
							 fwupd_guid_t	*guid,
							 FwupdGuidFlags	 flags,
							 GError		**error);
#else
gchar		*fwupd_guid_to_string			(const guint8	 guid[16],
							 FwupdGuidFlags	 flags);
gboolean	 fwupd_guid_from_string			(const gchar	*guidstr,
							 guint8		 guid[16],
							 FwupdGuidFlags	 flags,
							 GError		**error);
#endif
gboolean	 fwupd_guid_is_valid			(const gchar	*guid);
gchar		*fwupd_guid_hash_string			(const gchar	*str);
gchar		*fwupd_guid_hash_data			(const guint8	*data,
							 gsize		 datasz,
							 FwupdGuidFlags	 flags);

G_END_DECLS
