/*
 * Copyright 2024 TDT AG <development@tdt.de>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DFOTA_UPDATER (fu_dfota_updater_get_type())
G_DECLARE_FINAL_TYPE(FuDfotaUpdater, fu_dfota_updater, FU, DFOTA_UPDATER, GObject)

#define FU_DFOTA_UPDATER_FILENAME		   "dfota_update.bin"
#define FU_DFOTA_UPDATER_FOTA_RESTART_TIMEOUT_SECS 15

FuDfotaUpdater *
fu_dfota_updater_new(FuIOChannel *io_channel);
gboolean
fu_dfota_updater_open(FuDfotaUpdater *self, GError **error);
gboolean
fu_dfota_updater_write(FuDfotaUpdater *self,
		       FuProgress *progress,
		       FuDevice *device,
		       GError **error);
gboolean
fu_dfota_updater_close(FuDfotaUpdater *self, GError **error);
gboolean
fu_dfota_updater_upload_firmware(FuDfotaUpdater *self, GBytes *bytes, GError **error);
