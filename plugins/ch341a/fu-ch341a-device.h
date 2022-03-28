/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_CH341A_DEVICE (fu_ch341a_device_get_type())
G_DECLARE_FINAL_TYPE(FuCh341aDevice, fu_ch341a_device, FU, CH341A_DEVICE, FuUsbDevice)

gboolean
fu_ch341a_device_chip_select(FuCh341aDevice *self, gboolean val, GError **error);
gboolean
fu_ch341a_device_spi_transfer(FuCh341aDevice *self, guint8 *buf, gsize bufsz, GError **error);
