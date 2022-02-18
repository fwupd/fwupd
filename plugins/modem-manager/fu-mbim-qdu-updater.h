/*
 * Copyright (C) 2021 Jarvis Jiang <jarvis.w.jiang@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <libmbim-glib.h>

#include "fu-mm-device.h"

#if MBIM_CHECK_VERSION(1, 25, 3)
#define FU_TYPE_MBIM_QDU_UPDATER (fu_mbim_qdu_updater_get_type())
G_DECLARE_FINAL_TYPE(FuMbimQduUpdater, fu_mbim_qdu_updater, FU, MBIM_QDU_UPDATER, GObject)

FuMbimQduUpdater *
fu_mbim_qdu_updater_new(const gchar *mbim_port);
gboolean
fu_mbim_qdu_updater_open(FuMbimQduUpdater *self, GError **error);
GArray *
fu_mbim_qdu_updater_write(FuMbimQduUpdater *self,
			  const gchar *filename,
			  GBytes *blob,
			  FuDevice *device,
			  FuProgress *progress,
			  GError **error);
gchar *
fu_mbim_qdu_updater_check_ready(FuMbimQduUpdater *self, GError **error);
gboolean
fu_mbim_qdu_updater_close(FuMbimQduUpdater *self, GError **error);
MbimDevice *
fu_mbim_qdu_updater_get_mbim_device(FuMbimQduUpdater *self);

#endif /* MBIM_CHECK_VERSION(1,25,3) */
