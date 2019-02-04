/*
 * Copyright (C) 2015-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FWUPD_COMMON_H
#define __FWUPD_COMMON_H

#include <glib.h>

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
gboolean	 fwupd_guid_is_valid			(const gchar	*guid);
gchar		*fwupd_guid_from_buf			(const guint8	*buf,
							 FwupdGuidFlags	 flags);
gchar		*fwupd_guid_from_string			(const gchar	*str);
gchar		*fwupd_guid_from_data			(const guint8	*data,
							 gsize		 datasz,
							 FwupdGuidFlags	 flags);

#endif /* __FWUPD_COMMON_H */
