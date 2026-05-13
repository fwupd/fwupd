/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_WCH_CH347_DEVICE (fu_wch_ch347_device_get_type())
G_DECLARE_FINAL_TYPE(FuWchCh347Device, fu_wch_ch347_device, FU, WCH_CH347_DEVICE, FuUsbDevice)

gboolean
fu_wch_ch347_device_chip_select(FuWchCh347Device *self, gboolean val, GError **error);
gboolean
fu_wch_ch347_device_send_command(FuWchCh347Device *self,
				 const guint8 *wbuf,
				 gsize wbufsz,
				 guint8 *rbuf,
				 gsize rbufsz,
				 FuProgress *progress,
				 GError **error);
