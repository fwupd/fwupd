/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_HIDPP_RUNTIME (fu_logitech_hidpp_runtime_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuLogitechHidPpRuntime, fu_logitech_hidpp_runtime, FU, HIDPP_RUNTIME, FuUdevDevice)

struct _FuLogitechHidPpRuntimeClass
{
	FuUdevDeviceClass	 parent_class;
};

gboolean	fu_logitech_hidpp_runtime_enable_notifications		(FuLogitechHidPpRuntime	*self,
									 GError			**error);
gboolean	fu_logitech_hidpp_runtime_get_signed_firmware		(FuLogitechHidPpRuntime	*self);
void		fu_logitech_hidpp_runtime_set_signed_firmware		(FuLogitechHidPpRuntime	*self,
									 gboolean 		 signed_firmware);
FuIOChannel	*fu_logitech_hidpp_runtime_get_io_channel		(FuLogitechHidPpRuntime	*self);
void		fu_logitech_hidpp_runtime_set_io_channel		(FuLogitechHidPpRuntime	*self,
									 FuIOChannel		*io_channel);
guint8		fu_logitech_hidpp_runtime_get_version_bl_major		(FuLogitechHidPpRuntime	*self);
void		fu_logitech_hidpp_runtime_set_version_bl_major		(FuLogitechHidPpRuntime	*self,
									 guint8			 version_bl_major);
