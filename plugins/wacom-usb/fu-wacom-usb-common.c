/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include <string.h>

#include "fu-wacom-usb-common.h"
#include "fu-wacom-usb-struct.h"

void
fu_wacom_usb_buffer_dump(const gchar *title, guint8 cmd, const guint8 *buf, gsize sz)
{
	g_autofree gchar *tmp = NULL;
	tmp = g_strdup_printf("%s %s (%" G_GSIZE_FORMAT ")",
			      title,
			      fu_wacom_usb_report_id_to_string(cmd),
			      sz);
	fu_dump_raw(G_LOG_DOMAIN, tmp, buf, sz);
}
