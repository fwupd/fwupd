/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_SYNAPTICS_CXAUDIO_DEVICE (fu_synaptics_cxaudio_device_get_type ())
G_DECLARE_FINAL_TYPE (FuSynapticsCxaudioDevice, fu_synaptics_cxaudio_device, FU, SYNAPTICS_CXAUDIO_DEVICE, FuUsbDevice)

struct _FuSynapticsCxaudioDeviceClass
{
	FuUsbDeviceClass	parent_class;
};
