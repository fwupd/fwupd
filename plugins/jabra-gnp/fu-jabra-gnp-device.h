/*
 * Copyright 2023 GN Audio A/S
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_JABRA_GNP_DEVICE (fu_jabra_gnp_device_get_type())
G_DECLARE_FINAL_TYPE(FuJabraGnpDevice, fu_jabra_gnp_device, FU, JABRA_GNP_DEVICE, FuUsbDevice)

guint8
fu_jabra_gnp_device_get_iface_hid(FuJabraGnpDevice *self) G_GNUC_NON_NULL(1);
guint8
fu_jabra_gnp_device_get_epin(FuJabraGnpDevice *self) G_GNUC_NON_NULL(1);
