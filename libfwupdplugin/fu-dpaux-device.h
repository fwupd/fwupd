/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-udev-device.h"

#define FU_TYPE_DPAUX_DEVICE (fu_dpaux_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuDpauxDevice, fu_dpaux_device, FU, DPAUX_DEVICE, FuUdevDevice)

struct _FuDpauxDeviceClass {
	FuUdevDeviceClass parent_class;
};

#define FU_DPAUX_DEVICE_DPCD_OFFSET_RECEIVER_CAPABILITY 0x0
#define FU_DPAUX_DEVICE_DPCD_OFFSET_LINK_CONFIGURATION	0x100
#define FU_DPAUX_DEVICE_DPCD_OFFSET_LINK_SINK_STATUS	0x200
#define FU_DPAUX_DEVICE_DPCD_OFFSET_SOURCE_DEVICE	0x300
#define FU_DPAUX_DEVICE_DPCD_OFFSET_SINK_DEVICE		0x400
#define FU_DPAUX_DEVICE_DPCD_OFFSET_BRANCH_DEVICE	0x500

guint32
fu_dpaux_device_get_dpcd_ieee_oui(FuDpauxDevice *self) G_GNUC_NON_NULL(1);
void
fu_dpaux_device_set_dpcd_ieee_oui(FuDpauxDevice *self, guint32 dpcd_ieee_oui) G_GNUC_NON_NULL(1);
guint8
fu_dpaux_device_get_dpcd_hw_rev(FuDpauxDevice *self) G_GNUC_NON_NULL(1);
void
fu_dpaux_device_set_dpcd_hw_rev(FuDpauxDevice *self, guint8 dpcd_hw_rev) G_GNUC_NON_NULL(1);
const gchar *
fu_dpaux_device_get_dpcd_dev_id(FuDpauxDevice *self) G_GNUC_NON_NULL(1);
void
fu_dpaux_device_set_dpcd_dev_id(FuDpauxDevice *self, const gchar *dpcd_dev_id) G_GNUC_NON_NULL(1);

gboolean
fu_dpaux_device_read(FuDpauxDevice *self,
		     goffset offset,
		     guint8 *buf,
		     gsize bufsz,
		     guint timeout_ms,
		     GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_dpaux_device_write(FuDpauxDevice *self,
		      goffset offset,
		      const guint8 *buf,
		      gsize bufsz,
		      guint timeout_ms,
		      GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
