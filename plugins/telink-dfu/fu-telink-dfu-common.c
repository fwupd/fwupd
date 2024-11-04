/*
 * Copyright 2024 Mike Chang <Mike.chang@telink-semi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <stdio.h>

#include "fu-telink-dfu-common.h"

guint32
fu_telink_dfu_parse_image_version(const gchar *version, FwupdVersionFormat ver_format)
{
	gint rc;
	guint32 v_major = 0;
	guint32 v_minor = 0;
	guint32 v_patch = 0;
	guint32 ver_hex = 0;

	/* revision not available; forced update */
	if (version == NULL)
		return 0;

	if (ver_format == FWUPD_VERSION_FORMAT_TRIPLET) {
		/* version format: aa.bb.cc */
		rc = sscanf(version, "%u.%u.%u", &v_major, &v_minor, &v_patch);
		if (rc != 3 || v_major > 999 || v_minor > 999 || v_patch > 999) {
			/* invalid version format; forced update */
			g_warning("invalid version string(FORMAT_TRIPLET): %s", version);
		} else {
			ver_hex = (v_major << 24) | (v_minor << 16) | v_patch;
		}
	} else if (ver_format == FWUPD_VERSION_FORMAT_PAIR) {
		/* version format: aaaa.bbbb */
		rc = sscanf(version, "%u.%u", &v_major, &v_minor);
		if (rc != 2 || v_major > 99 || v_minor > 99) {
			/* invalid version format; forced update */
			g_warning("invalid version string(FORMAT_PAIR): %s", version);
		} else {
			ver_hex = (v_major << 16) | v_minor;
		}
	} else {
		g_warning("unsupported version format: %u", ver_format);
	}

	return ver_hex;
}
