/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

/**
 * FuVersionFormat:
 * @FU_VERSION_FORMAT_UNKNOWN:		Unknown version format
 * @FU_VERSION_FORMAT_PLAIN:		Use plain integer version numbers
 * @FU_VERSION_FORMAT_QUAD:		Use Dell-style AA.BB.CC.DD version numbers
 * @FU_VERSION_FORMAT_TRIPLET:		Use Microsoft-style AA.BB.CCDD version numbers
 * @FU_VERSION_FORMAT_PAIR:		Use two AABB.CCDD version numbers
 * @FU_VERSION_FORMAT_BCD:		Use binary coded decimal notation
 * @FU_VERSION_FORMAT_INTEL_ME:		Use Intel ME-style bitshifted notation
 * @FU_VERSION_FORMAT_INTEL_ME2:	Use Intel ME-style A.B.CC.DDDD notation notation
 *
 * The flags used when parsing version numbers.
 **/
typedef enum {
	FU_VERSION_FORMAT_UNKNOWN,	/* Since: 1.2.0 */
	FU_VERSION_FORMAT_PLAIN,	/* Since: 1.2.0 */
	FU_VERSION_FORMAT_QUAD,		/* Since: 1.2.0 */
	FU_VERSION_FORMAT_TRIPLET,	/* Since: 1.2.0 */
	FU_VERSION_FORMAT_PAIR,		/* Since: 1.2.0 */
	FU_VERSION_FORMAT_BCD,		/* Since: 1.2.0 */
	FU_VERSION_FORMAT_INTEL_ME,	/* Since: 1.2.0 */
	FU_VERSION_FORMAT_INTEL_ME2,	/* Since: 1.2.0 */
	/*< private >*/
	FU_VERSION_FORMAT_LAST
} FuVersionFormat;

FuVersionFormat  fu_common_version_format_from_string	(const gchar	*str);
const gchar	*fu_common_version_format_to_string	(FuVersionFormat kind);

gint		 fu_common_vercmp		(const gchar	*version_a,
						 const gchar	*version_b);
gchar		*fu_common_version_from_uint32	(guint32	 val,
						 FuVersionFormat flags);
gchar		*fu_common_version_from_uint16	(guint16	 val,
						 FuVersionFormat flags);
gchar		*fu_common_version_parse	(const gchar	*version);
FuVersionFormat	 fu_common_version_guess_format	(const gchar	*version);

G_END_DECLS
