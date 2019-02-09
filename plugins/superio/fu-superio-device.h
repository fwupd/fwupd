/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_SUPERIO_DEVICE (fu_superio_device_get_type ())
G_DECLARE_FINAL_TYPE (FuSuperioDevice, fu_superio_device, FU, SUPERIO_DEVICE, FuDevice)

FuSuperioDevice	*fu_superio_device_new		(const gchar		*chipset,
						 guint16		 id,
						 guint16		 port);

G_END_DECLS
