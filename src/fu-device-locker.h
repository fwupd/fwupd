/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_DEVICE_LOCKER_H
#define __FU_DEVICE_LOCKER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define FU_TYPE_DEVICE_LOCKER (fu_device_locker_get_type ())

G_DECLARE_FINAL_TYPE (FuDeviceLocker, fu_device_locker, FU, DEVICE_LOCKER, GObject)

typedef gboolean (*FuDeviceLockerFunc)		(GObject		*device,
						 GError			**error);

FuDeviceLocker	*fu_device_locker_new		(gpointer		 device,
						 GError			**error);
FuDeviceLocker	*fu_device_locker_new_full	(gpointer		 device,
						 FuDeviceLockerFunc	 open_func,
						 FuDeviceLockerFunc	 close_func,
						 GError			**error);

G_END_DECLS

#endif /* __FU_DEVICE_LOCKER_H */
