/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-pxi-tp-common.h"
#include "fu-pxi-tp-device.h"
#include "fu-pxi-tp-struct.h"

gboolean
fu_pxi_tp_register_write(FuPxiTpDevice *self,
			 FuPxiTpSystemBank bank,
			 guint8 addr,
			 guint8 val,
			 GError **error);

gboolean
fu_pxi_tp_register_read(FuPxiTpDevice *self,
			FuPxiTpSystemBank bank,
			guint8 addr,
			guint8 *out_val,
			GError **error);

gboolean
fu_pxi_tp_register_user_write(FuPxiTpDevice *self,
			      FuPxiTpUserBank bank,
			      guint8 addr,
			      guint8 val,
			      GError **error);

gboolean
fu_pxi_tp_register_user_read(FuPxiTpDevice *self,
			     FuPxiTpUserBank bank,
			     guint8 addr,
			     guint8 *out_val,
			     GError **error);

gboolean
fu_pxi_tp_register_burst_write(FuPxiTpDevice *self, const guint8 *buf, gsize bufsz, GError **error);

gboolean
fu_pxi_tp_register_burst_read(FuPxiTpDevice *self, guint8 *buf, gsize bufsz, GError **error);
