/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once
#include <fwupdplugin.h>

#include "fu-pxi-tp-device.h"

gboolean
fu_pxi_tp_common_send_feature(FuPxiTpDevice *self, const guint8 *buf, gsize len, GError **error);

gboolean
fu_pxi_tp_common_get_feature(FuPxiTpDevice *self,
			     guint8 report_id,
			     guint8 *buf,
			     gsize len,
			     GError **error);
