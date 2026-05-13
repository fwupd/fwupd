/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 * Copyright 2019 Synaptics Inc
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_SYNAPTICS_PROMETHEUS_FIRMWARE (fu_synaptics_prometheus_firmware_get_type())

#define FU_SYNAPTICS_PROMETHEUS_FIRMWARE_PROMETHEUS_SIGSIZE 0x100
#define FU_SYNAPTICS_PROMETHEUS_FIRMWARE_TRITON_SIGSIZE	    0x180

G_DECLARE_FINAL_TYPE(FuSynapticsPrometheusFirmware,
		     fu_synaptics_prometheus_firmware,
		     FU,
		     SYNAPTICS_PROMETHEUS_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_synaptics_prometheus_firmware_new(void);
guint32
fu_synaptics_prometheus_firmware_get_product_id(FuSynapticsPrometheusFirmware *self);
gboolean
fu_synaptics_prometheus_firmware_set_signature_size(FuSynapticsPrometheusFirmware *self,
						    guint32 signature_size);
