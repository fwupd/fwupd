/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "fu-wch-ch347-cfi-device.h"

#include "config.h"
#include "fu-wch-ch347-device.h"

struct _FuWchCh347CfiDevice {
	FuCfiDevice parent_instance;
};

G_DEFINE_TYPE(FuWchCh347CfiDevice, fu_wch_ch347_cfi_device, FU_TYPE_CFI_DEVICE)

static gboolean
fu_wch_ch347_cfi_device_chip_select(FuCfiDevice *self, gboolean value, GError **error)
{
	FuWchCh347Device *proxy = FU_WCH_CH347_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));
	return fu_wch_ch347_device_chip_select(proxy, value, error);
}

static gboolean
fu_wch_ch347_cfi_device_send_command(FuCfiDevice *self,
				     const guint8 *wbuf,
				     gsize wbufsz,
				     guint8 *rbuf,
				     gsize rbufsz,
				     FuProgress *progress,
				     GError **error)
{
	FuWchCh347Device *proxy = FU_WCH_CH347_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));
	return fu_wch_ch347_device_send_command(proxy, wbuf, wbufsz, rbuf, rbufsz, progress, error);
}

static void
fu_wch_ch347_cfi_device_init(FuWchCh347CfiDevice *self)
{
}

static void
fu_wch_ch347_cfi_device_class_init(FuWchCh347CfiDeviceClass *klass)
{
	FuCfiDeviceClass *cfi_class = FU_CFI_DEVICE_CLASS(klass);
	cfi_class->chip_select = fu_wch_ch347_cfi_device_chip_select;
	cfi_class->send_command = fu_wch_ch347_cfi_device_send_command;
}
