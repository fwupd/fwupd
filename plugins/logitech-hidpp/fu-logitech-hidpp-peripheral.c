/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-logitech-hidpp-common.h"
#include "fu-logitech-hidpp-peripheral.h"

struct _FuLogitechHidPpPeripheral
{
	FuLogitechHidPpDevice		parent_instance;
};

G_DEFINE_TYPE (FuLogitechHidPpPeripheral, fu_logitech_hidpp_peripheral, FU_TYPE_HIDPP_DEVICE)

static void
fu_logitech_hidpp_peripheral_class_init (FuLogitechHidPpPeripheralClass *klass)
{
}

static void
fu_logitech_hidpp_peripheral_init (FuLogitechHidPpPeripheral *self)
{
	fu_device_add_parent_guid (FU_DEVICE (self), "HIDRAW\\VEN_046D&DEV_C52B");
	/* there are a lot of unifying peripherals, but not all respond
	 * well to opening -- so limit to ones with issued updates */
	fu_device_add_internal_flag (FU_DEVICE (self),
				     FU_DEVICE_INTERNAL_FLAG_ONLY_SUPPORTED);
}
