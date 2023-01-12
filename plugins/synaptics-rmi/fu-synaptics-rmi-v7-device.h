/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-synaptics-rmi-device.h"

gboolean
fu_synaptics_rmi_v7_device_detach(FuDevice *device, FuProgress *progress, GError **error);
gboolean
fu_synaptics_rmi_v7_device_write_firmware(FuDevice *device,
					  FuFirmware *firmware,
					  FuProgress *progress,
					  FwupdInstallFlags flags,
					  GError **error);
gboolean
fu_synaptics_rmi_v7_device_setup(FuSynapticsRmiDevice *self, GError **error);
gboolean
fu_synaptics_rmi_v7_device_query_status(FuSynapticsRmiDevice *self, GError **error);
gboolean
fu_synaptics_rmi_v7_device_secure_check(FuSynapticsRmiDevice *self,
					FuFirmware *firmware,
					GError **error);
GBytes *
fu_synaptics_rmi_v7_device_get_pubkey(FuSynapticsRmiDevice *self, GError **error);
