/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2018 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __FU_INSTALL_TASK_H
#define __FU_INSTALL_TASK_H

#include <glib-object.h>
#include <appstream-glib.h>

#include "fu-device.h"

G_BEGIN_DECLS

#define FU_TYPE_TASK (fu_install_task_get_type ())
G_DECLARE_FINAL_TYPE (FuInstallTask, fu_install_task, FU, INSTALL_TASK, GObject)

FuInstallTask	*fu_install_task_new			(FuDevice	*device,
							 AsApp		*app);
FuDevice	*fu_install_task_get_device		(FuInstallTask	*self);
AsApp		*fu_install_task_get_app		(FuInstallTask	*self);
FwupdTrustFlags	 fu_install_task_get_trust_flags	(FuInstallTask	*self);
gboolean	 fu_install_task_get_is_downgrade	(FuInstallTask	*self);
gboolean	 fu_install_task_check_requirements	(FuInstallTask	*self,
							 FwupdInstallFlags flags,
							 GError		**error);
const gchar	*fu_install_task_get_action_id		(FuInstallTask	*self);
gint		 fu_install_task_compare		(FuInstallTask	*task1,
							 FuInstallTask	*task2);

G_END_DECLS

#endif /* __FU_INSTALL_TASK_H */

