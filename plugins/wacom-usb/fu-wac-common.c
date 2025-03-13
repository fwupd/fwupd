/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include <string.h>

#include "fu-wac-common.h"
#include "fu-wac-struct.h"

void
fu_wac_buffer_dump(const gchar *title, guint8 cmd, const guint8 *buf, gsize sz)
{
	g_autofree gchar *tmp = NULL;
	tmp = g_strdup_printf("%s %s (%" G_GSIZE_FORMAT ")",
			      title,
			      fu_wac_report_id_to_string(cmd),
			      sz);
	fu_dump_raw(G_LOG_DOMAIN, tmp, buf, sz);
}

#define FU_WACOM_USB_VERSION_DECODE_BCD(val) ((((val) >> 4) & 0x0f) * 10 + ((val) & 0x0f))

gchar *
fu_wac_version_u32_to_quad_bcd(guint32 value)
{
	/* AA.BB.CC.DD, but BCD */
	return g_strdup_printf("%u.%u.%u.%u",
			       FU_WACOM_USB_VERSION_DECODE_BCD(value >> 24),
			       FU_WACOM_USB_VERSION_DECODE_BCD(value >> 16),
			       FU_WACOM_USB_VERSION_DECODE_BCD(value >> 8),
			       FU_WACOM_USB_VERSION_DECODE_BCD(value));
}
