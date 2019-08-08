/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#include "fu-synaptics-rmi-device.h"

G_BEGIN_DECLS

gboolean	 fu_synaptics_rmi_v6_device_setup		(FuSynapticsRmiDevice	*self,
								 GError			**error);

G_END_DECLS
