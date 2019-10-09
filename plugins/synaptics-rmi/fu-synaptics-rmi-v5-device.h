/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#include "fu-synaptics-rmi-device.h"

gboolean	 fu_synaptics_rmi_v5_device_detach		(FuDevice	*device,
								 GError		**error);
gboolean	 fu_synaptics_rmi_v5_device_write_firmware	(FuDevice	*device,
								 FuFirmware	*firmware,
								 FwupdInstallFlags flags,
								 GError		**error);
gboolean	 fu_synaptics_rmi_v5_device_setup		(FuSynapticsRmiDevice	*self,
								 GError			**error);
gboolean	 fu_synaptics_rmi_v5_device_query_status	(FuSynapticsRmiDevice	*self,
								 GError			**error);
