/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_DEVICE_PRIVATE_H
#define __FU_DEVICE_PRIVATE_H

#include <fu-device.h>

G_BEGIN_DECLS

GPtrArray	*fu_device_get_parent_guids		(FuDevice	*device);
gboolean	 fu_device_has_parent_guid		(FuDevice	*device,
							 const gchar	*guid);
guint		 fu_device_get_order			(FuDevice	*device);
void		 fu_device_set_order			(FuDevice	*device,
							 guint		 order);

G_END_DECLS

#endif /* __FU_DEVICE_PRIVATE_H */

