/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>
#include <fwupd.h>

G_BEGIN_DECLS

gint		 fu_common_vercmp		(const gchar	*version_a,
						 const gchar	*version_b);
gchar		*fu_common_version_from_uint32	(guint32	 val,
						 FwupdVersionFormat kind);
gchar		*fu_common_version_from_uint16	(guint16	 val,
						 FwupdVersionFormat kind);
gchar		*fu_common_version_parse	(const gchar	*version);
gchar		*fu_common_version_parse_from_format	(const gchar	*version,
							 FwupdVersionFormat	fmt);
gchar		*fu_common_version_ensure_semver (const gchar	*version);
FwupdVersionFormat	 fu_common_version_guess_format	(const gchar	*version);
gboolean	 fu_common_version_verify_format	(const gchar	*version,
							 FwupdVersionFormat fmt,
							 GError		**error);
G_END_DECLS
