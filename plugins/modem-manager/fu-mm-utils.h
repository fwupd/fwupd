/*
 * Copyright (C) 2019 Aleksander Morgado <aleksander@aleksander.es>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_MM_UTILS_H
#define __FU_MM_UTILS_H

#include "config.h"
#include <gudev/gudev.h>

G_BEGIN_DECLS

#ifndef HAVE_GUDEV_232
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GUdevClient, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GUdevDevice, g_object_unref)
#pragma clang diagnostic pop
#endif

gboolean	fu_mm_utils_get_udev_port_info	(GUdevDevice  *dev,
						 gchar	     **device_sysfs_path,
						 gint	      *port_ifnum,
						 GError	     **error);
gboolean	fu_mm_utils_get_port_info	(const gchar  *path,
						 gchar	     **device_sysfs_path,
						 gint	      *port_ifnum,
						 GError	     **error);

G_END_DECLS

#endif /* __FU_MM_UTILS_H */
