/*
 * Copyright (C) 2019 Aleksander Morgado <aleksander@aleksander.es>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_MM_UTILS_H
#define __FU_MM_UTILS_H

#include <gudev/gudev.h>

gboolean
fu_mm_utils_get_udev_port_info(GUdevDevice *dev,
			       gchar **device_bus,
			       gchar **device_sysfs_path,
			       gint *port_usb_ifnum,
			       GError **error);
gboolean
fu_mm_utils_get_port_info(const gchar *path,
			  gchar **device_bus,
			  gchar **device_sysfs_path,
			  gint *port_usb_ifnum,
			  GError **error);
gboolean
fu_mm_utils_find_device_file(const gchar *device_sysfs_path,
			     const gchar *subsystem,
			     gchar **out_device_file,
			     GError **error);

#endif /* __FU_MM_UTILS_H */
