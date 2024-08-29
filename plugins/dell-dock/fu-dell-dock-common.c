/*
 * Copyright 2018 Dell Inc.
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
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

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

const gchar *
fu_dell_dock_get_instance_id(guint8 type, DellDockComponent *dev_list, guint16 vid, guint16 pid)
{
	/* The last instance_id must be NULL */
	for (guint i = 0; dev_list[i].instance_id != NULL; i++) {
		if (dev_list[i].dock_type != type)
			continue;
		if (dev_list[i].vid != 0 && vid != 0 && dev_list[i].vid != vid)
			continue;
		if (dev_list[i].pid != 0 && pid != 0 && dev_list[i].pid != pid)
			continue;
		return dev_list[i].instance_id;
	}

	return NULL;
}
