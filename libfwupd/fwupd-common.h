/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
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

#endif /* __FWUPD_COMMON_H */
