/*
 * Copyright 2024 Mike Chang <Mike.chang@telink-semi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <stdio.h>

#include "fu-telink-dfu-common.h"

guint32
fu_telink_dfu_parse_image_version(const gchar *version)
{
	gint rc;
	guint32 v_major = 0;
	guint32 v_minor = 0;
	guint32 v_patch = 0;

	/* revision not available; forced update */
	if (version == NULL)
		return 0;

	/* version format: aa.bb.cc */
	rc = sscanf(version, "%u.%u.%u", &v_major, &v_minor, &v_patch);
	if (rc != 3 || v_major > 999 || v_minor > 999 || v_patch > 999) {
		g_warning("invalid version string: %s", version);
		return 0;
	}

	return (v_major << 24) | (v_minor << 16) | v_patch;
}
