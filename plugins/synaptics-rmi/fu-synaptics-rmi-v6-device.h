/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-synaptics-rmi-device.h"

gboolean
fu_synaptics_rmi_v6_device_setup(FuSynapticsRmiDevice *self, GError **error);
