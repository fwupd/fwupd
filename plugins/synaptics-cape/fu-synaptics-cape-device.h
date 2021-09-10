/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2021 Synaptics Incorporated <simon.ho@synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_SYNAPTICS_CAPE_DEVICE (fu_synaptics_cape_device_get_type())
G_DECLARE_FINAL_TYPE(FuSynapticsCapeDevice,
		     fu_synaptics_cape_device,
		     FU,
		     SYNAPTICS_CAPE_DEVICE,
		     FuHidDevice)

struct _FuSynapticsCapeDeviceClass {
	FuHidDeviceClass parent_class;
};