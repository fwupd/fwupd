/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_RTS54HID_MODULE_H
#define __FU_RTS54HID_MODULE_H

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_RTS54HID_MODULE (fu_rts54hid_module_get_type ())
G_DECLARE_FINAL_TYPE (FuRts54HidModule, fu_rts54hid_module, FU, RTS54HID_MODULE, FuDevice)

FuRts54HidModule	*fu_rts54hid_module_new		(void);

G_END_DECLS

#endif /* __FU_RTS54HID_MODULE_H */
