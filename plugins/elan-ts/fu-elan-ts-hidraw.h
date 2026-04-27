/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

gboolean
fu_elan_ts_hidraw_write_with_retry(FuHidrawDevice *device,
				   const guint8 *p_buf,
				   gsize buf_len,
				   GError **error);

gboolean
fu_elan_ts_hidraw_read_with_retry(FuHidrawDevice *device,
				  guint8 *p_buf,
				  gsize buf_len,
				  GError **error);
