/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-vli-common.h"

#define FU_TYPE_VLI_DEVICE (fu_vli_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuVliDevice, fu_vli_device, FU, VLI_DEVICE, FuUsbDevice)

struct _FuVliDeviceClass {
	FuUsbDeviceClass parent_class;
	gboolean (*spi_chip_erase)(FuVliDevice *self, GError **error);
	gboolean (*spi_sector_erase)(FuVliDevice *self, guint32 addr, GError **error);
	gboolean (*spi_read_data)(FuVliDevice *self,
				  guint32 addr,
				  guint8 *buf,
				  gsize bufsz,
				  GError **error);
	gboolean (*spi_read_status)(FuVliDevice *self, guint8 *status, GError **error);
	gboolean (*spi_write_enable)(FuVliDevice *self, GError **error);
	gboolean (*spi_write_data)(FuVliDevice *self,
				   guint32 addr,
				   const guint8 *buf,
				   gsize bufsz,
				   GError **error);
	gboolean (*spi_write_status)(FuVliDevice *self, guint8 status, GError **error);
};

#define FU_VLI_DEVICE_TIMEOUT 3000 /* ms */
#define FU_VLI_DEVICE_TXSIZE  0x20 /* bytes */

void
fu_vli_device_set_kind(FuVliDevice *self, FuVliDeviceKind device_kind);
void
fu_vli_device_set_spi_auto_detect(FuVliDevice *self, gboolean spi_auto_detect);
FuVliDeviceKind
fu_vli_device_get_kind(FuVliDevice *self);
guint32
fu_vli_device_get_offset(FuVliDevice *self);
FuCfiDevice *
fu_vli_device_get_cfi_device(FuVliDevice *self);
gboolean
fu_vli_device_spi_erase_sector(FuVliDevice *self, guint32 addr, GError **error);
gboolean
fu_vli_device_spi_erase_all(FuVliDevice *self, FuProgress *progress, GError **error);
gboolean
fu_vli_device_spi_erase(FuVliDevice *self,
			guint32 addr,
			gsize sz,
			FuProgress *progress,
			GError **error);
gboolean
fu_vli_device_spi_read_block(FuVliDevice *self,
			     guint32 addr,
			     guint8 *buf,
			     gsize bufsz,
			     GError **error);
GBytes *
fu_vli_device_spi_read(FuVliDevice *self,
		       guint32 address,
		       gsize bufsz,
		       FuProgress *progress,
		       GError **error);
gboolean
fu_vli_device_spi_write_block(FuVliDevice *self,
			      guint32 address,
			      const guint8 *buf,
			      gsize bufsz,
			      FuProgress *progress,
			      GError **error);
gboolean
fu_vli_device_spi_write(FuVliDevice *self,
			guint32 address,
			const guint8 *buf,
			gsize bufsz,
			FuProgress *progress,
			GError **error);
