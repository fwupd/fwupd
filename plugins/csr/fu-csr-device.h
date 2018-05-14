/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef __FU_CSR_DEVICE_H
#define __FU_CSR_DEVICE_H

#include <glib-object.h>
#include <gusb.h>

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_CSR_DEVICE (fu_csr_device_get_type ())
G_DECLARE_FINAL_TYPE (FuCsrDevice, fu_csr_device, FU, CSR_DEVICE, FuUsbDevice)

typedef enum {
	FU_CSR_DEVICE_QUIRK_NONE		= 0,
	FU_CSR_DEVICE_QUIRK_REQUIRE_DELAY	= (1 << 0),
	FU_CSR_DEVICE_QUIRK_LAST
} FuCsrDeviceQuirks;

FuCsrDevice	*fu_csr_device_new		(GUsbDevice		*usb_device);
gboolean	 fu_csr_device_attach		(FuCsrDevice		*self,
						 GError			**error);
void		 fu_csr_device_set_quirks	(FuCsrDevice		*self,
						 FuCsrDeviceQuirks	 quirks);

G_END_DECLS

#endif /* __FU_CSR_DEVICE_H */
