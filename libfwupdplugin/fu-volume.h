/*
 * Copyright (C) 2018-2020 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#include "fu-device-locker.h"

#define FU_TYPE_VOLUME (fu_volume_get_type ())

G_DECLARE_FINAL_TYPE (FuVolume, fu_volume, FU, VOLUME, GObject)

#define FU_VOLUME_KIND_ESP		"c12a7328-f81f-11d2-ba4b-00a0c93ec93b"
#define FU_VOLUME_KIND_BDP		"ebd0a0a2-b9e5-4433-87c0-68b6b72699c7"

const gchar	*fu_volume_get_id		(FuVolume	*self);
gboolean	 fu_volume_check_free_space	(FuVolume	*self,
						 guint64	 required,
						 GError		**error);
gboolean	 fu_volume_is_mounted		(FuVolume	*self);
gboolean	 fu_volume_is_encrypted		(FuVolume	*self);
gchar		*fu_volume_get_mount_point	(FuVolume	*self);
gboolean	 fu_volume_mount		(FuVolume	*self,
						 GError		**error);
gboolean	 fu_volume_unmount		(FuVolume	*self,
						 GError		**error);
FuDeviceLocker	*fu_volume_locker		(FuVolume	*self,
						 GError		**error);
gboolean	 fu_volume_is_internal		(FuVolume	*self);
gchar		*fu_volume_get_id_type		(FuVolume	*self);
