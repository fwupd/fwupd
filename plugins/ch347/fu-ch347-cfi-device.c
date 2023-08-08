/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ch347-cfi-device.h"
#include "fu-ch347-device.h"

struct _FuCh347CfiDevice {
	FuCfiDevice parent_instance;
};

G_DEFINE_TYPE(FuCh347CfiDevice, fu_ch347_cfi_device, FU_TYPE_CFI_DEVICE)

static gboolean
fu_ch347_cfi_device_chip_select(FuCfiDevice *self, gboolean value, GError **error)
{
	FuCh347Device *proxy = FU_CH347_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));
	return fu_ch347_device_chip_select(proxy, value, error);
}

static gboolean
fu_ch347_cfi_device_send_command(FuCfiDevice *self,
				 const guint8 *wbuf,
				 gsize wbufsz,
				 guint8 *rbuf,
				 gsize rbufsz,
				 FuProgress *progress,
				 GError **error)
{
	FuCh347Device *proxy = FU_CH347_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));
	return fu_ch347_device_send_command(proxy, wbuf, wbufsz, rbuf, rbufsz, progress, error);
}

static void
fu_ch347_cfi_device_init(FuCh347CfiDevice *self)
{
}

static void
fu_ch347_cfi_device_class_init(FuCh347CfiDeviceClass *klass)
{
	FuCfiDeviceClass *klass_cfi = FU_CFI_DEVICE_CLASS(klass);
	klass_cfi->chip_select = fu_ch347_cfi_device_chip_select;
	klass_cfi->send_command = fu_ch347_cfi_device_send_command;
}
