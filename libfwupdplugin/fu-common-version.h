/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>
#include <fwupd.h>

gint		 fu_common_vercmp		(const gchar	*version_a,
						 const gchar	*version_b)
G_DEPRECATED_FOR(fu_common_vercmp_full);
gint		 fu_common_vercmp_full		(const gchar	*version_a,
						 const gchar	*version_b,
						 FwupdVersionFormat fmt);
gchar		*fu_common_version_from_uint64	(guint64	 val,
						 FwupdVersionFormat kind);
gchar		*fu_common_version_from_uint32	(guint32	 val,
						 FwupdVersionFormat kind);
gchar		*fu_common_version_from_uint16	(guint16	 val,
						 FwupdVersionFormat kind);
gchar		*fu_common_version_parse	(const gchar	*version)
G_DEPRECATED_FOR(fu_common_version_parse_from_format);
gchar		*fu_common_version_parse_from_format	(const gchar	*version,
							 FwupdVersionFormat	fmt);
gchar		*fu_common_version_ensure_semver (const gchar	*version);
FwupdVersionFormat	 fu_common_version_guess_format	(const gchar	*version);
gboolean	 fu_common_version_verify_format	(const gchar	*version,
							 FwupdVersionFormat fmt,
							 GError		**error);
