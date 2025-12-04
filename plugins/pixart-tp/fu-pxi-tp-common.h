#pragma once
#include <fwupdplugin.h>
#include "fu-pxi-tp-device.h"

// #define g_debug g_message

/**
 * fu_pxi_tp_common_fail:
 * @dest:   Target error pointer (can be NULL)
 * @domain: Error domain (GQuark; only used when creating a new error)
 * @code:   Error code (only used when creating a new error)
 * @fmt:    printf-style message (can also be a plain string without %)
 */

gboolean
fu_pxi_tp_common_fail(GError **dest, GQuark domain, gint code, const char *fmt, ...);

gboolean
fu_pxi_tp_common_send_feature(FuPxiTpDevice *self, const guint8 *buf, gsize len, GError **error);

gboolean
fu_pxi_tp_common_get_feature(FuPxiTpDevice *self,
			     guint8 report_id,
			     guint8 *buf,
			     gsize len,
			     GError **error);
