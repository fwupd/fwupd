/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-cfi-struct.h"
#include "fu-device-locker.h"

#define FU_TYPE_CFI_DEVICE (fu_cfi_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuCfiDevice, fu_cfi_device, FU, CFI_DEVICE, FuDevice)

struct _FuCfiDeviceClass {
	FuDeviceClass parent_class;
	gboolean (*chip_select)(FuCfiDevice *self, gboolean value, GError **error);
	gboolean (*send_command)(FuCfiDevice *self,
				 const guint8 *wbuf,
				 gsize wbufsz,
				 guint8 *rbuf,
				 gsize rbufsz,
				 FuProgress *progress,
				 GError **error);
	gboolean (*read_jedec)(FuCfiDevice *self, GError **error);
};

FuCfiDevice *
fu_cfi_device_new(FuContext *ctx, const gchar *flash_id) G_GNUC_NON_NULL(1);
const gchar *
fu_cfi_device_get_flash_id(FuCfiDevice *self) G_GNUC_NON_NULL(1);
void
fu_cfi_device_set_flash_id(FuCfiDevice *self, const gchar *flash_id) G_GNUC_NON_NULL(1);
guint64
fu_cfi_device_get_size(FuCfiDevice *self) G_GNUC_NON_NULL(1);
void
fu_cfi_device_set_size(FuCfiDevice *self, guint64 size) G_GNUC_NON_NULL(1);
guint32
fu_cfi_device_get_page_size(FuCfiDevice *self) G_GNUC_NON_NULL(1);
void
fu_cfi_device_set_page_size(FuCfiDevice *self, guint32 page_size) G_GNUC_NON_NULL(1);
guint32
fu_cfi_device_get_sector_size(FuCfiDevice *self) G_GNUC_NON_NULL(1);
void
fu_cfi_device_set_sector_size(FuCfiDevice *self, guint32 sector_size) G_GNUC_NON_NULL(1);
guint32
fu_cfi_device_get_block_size(FuCfiDevice *self) G_GNUC_NON_NULL(1);
void
fu_cfi_device_set_block_size(FuCfiDevice *self, guint32 block_size) G_GNUC_NON_NULL(1);
gboolean
fu_cfi_device_get_cmd(FuCfiDevice *self, FuCfiDeviceCmd cmd, guint8 *value, GError **error)
    G_GNUC_NON_NULL(1);

gboolean
fu_cfi_device_send_command(FuCfiDevice *self,
			   const guint8 *wbuf,
			   gsize wbufsz,
			   guint8 *rbuf,
			   gsize rbufsz,
			   FuProgress *progress,
			   GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_cfi_device_chip_select(FuCfiDevice *self, gboolean value, GError **error) G_GNUC_NON_NULL(1);
FuDeviceLocker *
fu_cfi_device_chip_select_locker_new(FuCfiDevice *self, GError **error) G_GNUC_NON_NULL(1);
