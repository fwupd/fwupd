/*
 * Copyright (C) 2015-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#include "fu-device.h"

#define FU_TYPE_PENDING (fu_history_get_type ())
G_DECLARE_FINAL_TYPE (FuHistory, fu_history, FU, HISTORY, GObject)

FuHistory	*fu_history_new				(void);

gboolean	 fu_history_add_device			(FuHistory	*self,
							 FuDevice	*device,
							 FwupdRelease	*release,
							 GError		**error);
gboolean	 fu_history_modify_device		(FuHistory	*self,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_history_remove_device		(FuHistory	*self,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_history_remove_all			(FuHistory	*self,
							 GError		**error);
gboolean	 fu_history_remove_all_with_state	(FuHistory	*self,
							 FwupdUpdateState update_state,
							 GError		**error);
FuDevice	*fu_history_get_device_by_id		(FuHistory	*self,
							 const gchar	*device_id,
							 GError		**error);
GPtrArray	*fu_history_get_devices			(FuHistory	*self,
							 GError		**error);

gboolean	 fu_history_clear_approved_firmware	(FuHistory	*self,
							 GError		**error);
gboolean	 fu_history_add_approved_firmware	(FuHistory	*self,
							 const gchar	*checksum,
							 GError		**error);
GPtrArray	*fu_history_get_approved_firmware	(FuHistory	*self,
							 GError		**error);
