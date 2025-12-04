/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <stdarg.h>

#include "fu-pxi-tp-common.h"

gboolean
fu_pxi_tp_common_fail(GError **dest, GQuark domain, gint code, const char *fmt, ...)
{
	g_autofree gchar *msg = NULL;
	va_list ap;

	if (dest == NULL)
		return FALSE;

	/* 如果 caller 已經有錯誤，就不要覆蓋（防止 double-set） */
	if (*dest != NULL)
		return FALSE;

	va_start(ap, fmt);
	msg = g_strdup_vprintf(fmt, ap);
	va_end(ap);

	g_set_error(dest, domain, code, "%s", msg);
	return FALSE;
}

gboolean
fu_pxi_tp_common_send_feature(FuPxiTpDevice *self, const guint8 *buf, gsize len, GError **error)
{
	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					    buf,
					    len,
					    FU_IOCTL_FLAG_NONE,
					    error);
}

gboolean
fu_pxi_tp_common_get_feature(FuPxiTpDevice *self,
			     guint8 report_id,
			     guint8 *buf,
			     gsize len,
			     GError **error)
{
	buf[0] = report_id;
	return fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					    buf,
					    len,
					    FU_IOCTL_FLAG_NONE,
					    error);
}

gboolean
fu_pxi_tp_common_propagate_with_log(GError **error, GError **error_local, const char *ctx)
{
	if (error_local != NULL && *error_local != NULL) {
		/* add context to the existing error text */
		if (ctx != NULL && *ctx != '\0')
			g_prefix_error(*error_local, "%s: ", ctx); /* nocheck:error */

		/* propagate or clear without dereferencing the GError fields */
		if (error != NULL)
			g_propagate_error(error, g_steal_pointer(error_local));
		else
			g_clear_error(error_local);

		return FALSE;
	}
	return TRUE;
}
