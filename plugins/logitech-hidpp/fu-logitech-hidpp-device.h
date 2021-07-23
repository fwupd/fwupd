/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_HIDPP_DEVICE (fu_logitech_hidpp_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuLogitechHidPpDevice, fu_logitech_hidpp_device, FU, HIDPP_DEVICE, FuUdevDevice)

struct _FuLogitechHidPpDeviceClass
{
	FuUdevDeviceClass	parent_class;
	/* TODO: overridable methods */
};


void		fu_logitech_hidpp_device_set_hidpp_id		(FuLogitechHidPpDevice	*self,
								 guint8			 hidpp_id);
