/*
 * Copyright 2023 GN Audio A/S
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_JABRA_GNP_CHILD_DEVICE (fu_jabra_gnp_child_device_get_type())
G_DECLARE_FINAL_TYPE(FuJabraGnpChildDevice, fu_jabra_gnp_child_device, FU, JABRA_GNP_CHILD_DEVICE, FuDevice)

void
fu_jabra_gnp_child_device_set_dfu_pid(FuJabraGnpChildDevice *self, guint16 dfu_pid)
    G_GNUC_NON_NULL(1);
