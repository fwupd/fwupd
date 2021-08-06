/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_INTEL_SPI_DEVICE (fu_intel_spi_device_get_type ())
G_DECLARE_FINAL_TYPE (FuIntelSpiDevice, fu_intel_spi_device, FU, INTEL_SPI_DEVICE, FuDevice)

GBytes *
fu_intel_spi_device_dump(FuIntelSpiDevice *self,
			 FuDevice *device,
			 guint32 offset,
			 guint32 length,
			 FuProgress *progress,
			 GError **error);
