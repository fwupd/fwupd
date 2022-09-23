/*
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-dell-smi.h"

void
fu_dell_plugin_inject_fake_data(FuPlugin *plugin,
				guint32 *output,
				guint16 vid,
				guint16 pid,
				guint8 *buf,
				gboolean can_switch_modes);

gboolean
fu_dell_plugin_detect_tpm(FuPlugin *plugin, GError **error);
gboolean
fu_dell_plugin_backend_device_added(FuPlugin *plugin, FuDevice *device, GError **error);

/* These are nodes that will indicate information about
 * the TPM status
 */
struct tpm_status {
	guint32 ret;
	guint32 fw_version;
	guint32 status;
	guint32 flashes_left;
};
#define TPM_EN_MASK   0x0001
#define TPM_OWN_MASK  0x0004
#define TPM_TYPE_MASK 0x0F00
#define TPM_1_2_MODE  0x0001
#define TPM_2_0_MODE  0x0002
