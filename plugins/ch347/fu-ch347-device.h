/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_CH347_DEVICE (fu_ch347_device_get_type())
G_DECLARE_FINAL_TYPE(FuCh347Device, fu_ch347_device, FU, CH347_DEVICE, FuUsbDevice)

gboolean
fu_ch347_device_chip_select(FuCh347Device *self, gboolean val, GError **error);
gboolean
fu_ch347_device_send_command(FuCh347Device *self,
			     const guint8 *wbuf,
			     gsize wbufsz,
			     guint8 *rbuf,
			     gsize rbufsz,
			     FuProgress *progress,
			     GError **error);
