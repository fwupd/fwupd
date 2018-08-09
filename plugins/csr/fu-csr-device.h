/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_CSR_DEVICE_H
#define __FU_CSR_DEVICE_H

#include <glib-object.h>
#include <gusb.h>

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_CSR_DEVICE (fu_csr_device_get_type ())
G_DECLARE_FINAL_TYPE (FuCsrDevice, fu_csr_device, FU, CSR_DEVICE, FuUsbDevice)

FuCsrDevice	*fu_csr_device_new		(GUsbDevice		*usb_device);

G_END_DECLS

#endif /* __FU_CSR_DEVICE_H */
