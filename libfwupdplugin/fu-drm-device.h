/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-edid.h"
#include "fu-udev-device.h"

#define FU_TYPE_DRM_DEVICE (fu_drm_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuDrmDevice, fu_drm_device, FU, DRM_DEVICE, FuUdevDevice)

struct _FuDrmDeviceClass {
	FuUdevDeviceClass parent_class;
};

gboolean
fu_drm_device_get_enabled(FuDrmDevice *self) G_GNUC_NON_NULL(1);
FuDisplayState
fu_drm_device_get_state(FuDrmDevice *self) G_GNUC_NON_NULL(1);
const gchar *
fu_drm_device_get_connector_id(FuDrmDevice *self) G_GNUC_NON_NULL(1);
guint32
fu_drm_device_get_crtc_x(FuDrmDevice *self) G_GNUC_NON_NULL(1);
guint32
fu_drm_device_get_crtc_y(FuDrmDevice *self) G_GNUC_NON_NULL(1);
guint32
fu_drm_device_get_crtc_width(FuDrmDevice *self) G_GNUC_NON_NULL(1);
guint32
fu_drm_device_get_crtc_height(FuDrmDevice *self) G_GNUC_NON_NULL(1);
FuEdid *
fu_drm_device_get_edid(FuDrmDevice *self) G_GNUC_NON_NULL(1);
