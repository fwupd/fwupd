/*
 * Copyright (C) 2015-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <xmlb.h>

#include "fu-device.h"

#define FU_TYPE_TASK (fu_install_task_get_type ())
G_DECLARE_FINAL_TYPE (FuInstallTask, fu_install_task, FU, INSTALL_TASK, GObject)

FuInstallTask	*fu_install_task_new			(FuDevice	*device,
							 XbNode		*component);
FuDevice	*fu_install_task_get_device		(FuInstallTask	*self);
XbNode		*fu_install_task_get_component		(FuInstallTask	*self);
FwupdReleaseFlags fu_install_task_get_trust_flags	(FuInstallTask	*self);
gboolean	 fu_install_task_get_is_downgrade	(FuInstallTask	*self);
gboolean	 fu_install_task_check_requirements	(FuInstallTask	*self,
							 FwupdInstallFlags flags,
							 GError		**error);
const gchar	*fu_install_task_get_action_id		(FuInstallTask	*self);
gint		 fu_install_task_compare		(FuInstallTask	*task1,
							 FuInstallTask	*task2);
