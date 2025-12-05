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
