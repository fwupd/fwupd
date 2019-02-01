/*
 * Copyright (C) 2018-2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 Aleksander Morgado <aleksander@aleksander.es>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_MM_DEVICE_H
#define __FU_MM_DEVICE_H

#include <libmm-glib.h>

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_MM_DEVICE (fu_mm_device_get_type ())
G_DECLARE_FINAL_TYPE (FuMmDevice, fu_mm_device, FU, MM_DEVICE, FuDevice)

FuMmDevice			*fu_mm_device_new			(MMManager	*manager,
									 MMObject	*omodem);
const gchar			*fu_mm_device_get_inhibition_uid	(FuMmDevice	*device);
const gchar			*fu_mm_device_get_detach_fastboot_at	(FuMmDevice	*device);
gint				 fu_mm_device_get_port_at_ifnum		(FuMmDevice	*device);
MMModemFirmwareUpdateMethod	 fu_mm_device_get_update_methods	(FuMmDevice	*device);

FuMmDevice			*fu_mm_device_udev_new			(MMManager	*manager,
									 const gchar	*physical_id,
									 const gchar	*vendor,
									 const gchar	*name,
									 const gchar	*version,
									 const gchar	**device_ids,
									 MMModemFirmwareUpdateMethod update_methods,
									 const gchar	*detach_fastboot_at,
									 gint		 port_at_ifnum);
void			       fu_mm_device_udev_add_port		(FuMmDevice	*device,
									 const gchar	*subsystem,
									 const gchar	*path,
									 gint		 ifnum);

G_END_DECLS

#endif /* __FU_MM_DEVICE_H */
