/*
 * Copyright 2023 GN Audio A/S
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

struct _FuJabraGnpChildDevice {
	FuDevice parent_instance;
	guint8 fwu_protocol;
	guint8 sequence_number;
	guint8 address;
	guint dfu_pid;
};

#define FU_TYPE_JABRA_GNP_CHILD_DEVICE (fu_jabra_gnp_child_device_get_type())
G_DECLARE_FINAL_TYPE(FuJabraGnpChildDevice, fu_jabra_gnp_child_device, FU, JABRA_GNP_CHILD_DEVICE, FuDevice)
