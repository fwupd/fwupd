/*
 * Copyright (C) 2018 Dell Inc.
 * All rights reserved.
 *
 * This software and associated documentation (if any) is furnished
 * under a license and may only be used or copied in accordance
 * with the terms of the license.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * Dell Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-dell-dock-common.h"
#include "fu-dell-dock-i2c-ec.h"

gboolean
fu_dell_dock_set_power(FuDevice *device, guint8 target, gboolean enabled, GError **error)
{
	FuDevice *parent;
	g_autoptr(FuDeviceLocker) locker = NULL;

	g_return_val_if_fail(device != NULL, FALSE);

	parent = FU_IS_DELL_DOCK_EC(device) ? device : fu_device_get_parent(device);

	if (parent == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "Couldn't find parent for %s",
			    fu_device_get_name(device));
		return FALSE;
	}

	locker = fu_device_locker_new(parent, error);
	if (locker == NULL)
		return FALSE;

	return fu_dell_dock_ec_modify_lock(parent, target, enabled, error);
}

void
fu_dell_dock_will_replug(FuDevice *device)
{
	guint64 timeout = fu_device_get_install_duration(device);

	g_return_if_fail(FU_IS_DEVICE(device));

	g_debug("Activated %" G_GUINT64_FORMAT "s replug delay for %s",
		timeout,
		fu_device_get_name(device));
	fu_device_set_remove_delay(device, timeout * 1000);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
}
