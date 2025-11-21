/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_WCH_CH341A_DEVICE (fu_wch_ch341a_device_get_type())
G_DECLARE_FINAL_TYPE(FuWchCh341aDevice, fu_wch_ch341a_device, FU, WCH_CH341A_DEVICE, FuUsbDevice)

gboolean
fu_wch_ch341a_device_chip_select(FuWchCh341aDevice *self, gboolean val, GError **error);
gboolean
fu_wch_ch341a_device_spi_transfer(FuWchCh341aDevice *self,
				  guint8 *buf,
				  gsize bufsz,
				  GError **error);
