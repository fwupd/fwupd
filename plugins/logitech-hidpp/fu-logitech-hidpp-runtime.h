/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_HIDPP_RUNTIME (fu_logitech_hidpp_runtime_get_type())
G_DECLARE_DERIVABLE_TYPE(FuLogitechHidppRuntime,
			 fu_logitech_hidpp_runtime,
			 FU,
			 HIDPP_RUNTIME,
			 FuHidrawDevice)

struct _FuLogitechHidppRuntimeClass {
	FuHidrawDeviceClass parent_class;
};

gboolean
fu_logitech_hidpp_runtime_enable_notifications(FuLogitechHidppRuntime *self, GError **error);
guint8
fu_logitech_hidpp_runtime_get_version_bl_major(FuLogitechHidppRuntime *self);
