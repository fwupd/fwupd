/*
 * Copyright 2024 TDT AG <development@tdt.de>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_CINTERION_FDL_UPDATER (fu_cinterion_fdl_updater_get_type())
G_DECLARE_FINAL_TYPE(FuCinterionFdlUpdater,
		     fu_cinterion_fdl_updater,
		     FU,
		     CINTERION_FDL_UPDATER,
		     GObject)

FuCinterionFdlUpdater *
fu_cinterion_fdl_updater_new(const gchar *port);
gboolean
fu_cinterion_fdl_updater_open(FuCinterionFdlUpdater *self, GError **error);
gboolean
fu_cinterion_fdl_updater_write(FuCinterionFdlUpdater *self,
			       FuProgress *progress,
			       FuDevice *device,
			       GBytes *fw,
			       GError **error);
gboolean
fu_cinterion_fdl_updater_close(FuCinterionFdlUpdater *self, GError **error);
gboolean
fu_cinterion_fdl_updater_wait_ready(FuCinterionFdlUpdater *self, FuDevice *device, GError **error);
