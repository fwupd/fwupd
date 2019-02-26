/*
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"
#include "fu-dell-smi.h"

struct FuPluginData {
	FuDellSmiObj		*smi_obj;
	guint16			fake_vid;
	guint16			fake_pid;
	gboolean		can_switch_modes;
	gboolean		capsule_supported;
};

void
fu_plugin_dell_inject_fake_data (FuPlugin *plugin,
				 guint32 *output, guint16 vid, guint16 pid,
				 guint8 *buf, gboolean can_switch_modes);

gboolean
fu_plugin_dell_detect_tpm (FuPlugin *plugin, GError **error);

/* These are nodes that will indicate information about
 * the TPM status
 */
struct tpm_status {
	guint32 ret;
	guint32 fw_version;
	guint32 status;
	guint32 flashes_left;
};
#define TPM_EN_MASK	0x0001
#define TPM_OWN_MASK	0x0004
#define TPM_TYPE_MASK	0x0F00
#define TPM_1_2_MODE	0x0001
#define TPM_2_0_MODE	0x0002
