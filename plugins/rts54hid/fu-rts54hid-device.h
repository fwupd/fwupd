/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_RTS54HID_DEVICE_H
#define __FU_RTS54HID_DEVICE_H

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_RTS54HID_DEVICE (fu_rts54hid_device_get_type ())
G_DECLARE_FINAL_TYPE (FuRts54HidDevice, fu_rts54hid_device, FU, RTS54HID_DEVICE, FuUsbDevice)

FuRts54HidDevice	*fu_rts54hid_device_new		(FuUsbDevice		*device);
gboolean	 	fu_rts54hid_device_set_report	(FuRts54HidDevice	*self,
							 guint8			*buf,
							 gsize			 buf_sz,
							 GError			**error);
gboolean		 fu_rts54hid_device_get_report	(FuRts54HidDevice		*self,
							 guint8			*buf,
							 gsize			 buf_sz,
							 GError			**error);

G_END_DECLS

#endif /* __FU_RTS54HID_DEVICE_H */
