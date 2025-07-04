/*
 * Copyright 1999-2023 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LOGITECH_RALLYSYSTEM_AUDIO_DEVICE (fu_logitech_rallysystem_audio_device_get_type())
G_DECLARE_FINAL_TYPE(FuLogitechRallysystemAudioDevice,
		     fu_logitech_rallysystem_audio_device,
		     FU,
		     LOGITECH_RALLYSYSTEM_AUDIO_DEVICE,
		     FuHidrawDevice)
