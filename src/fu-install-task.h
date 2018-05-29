/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
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

