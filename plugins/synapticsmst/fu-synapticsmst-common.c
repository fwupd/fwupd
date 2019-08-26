/*
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-synapticsmst-common.h"

const gchar *
fu_synapticsmst_mode_to_string (FuSynapticsmstMode mode)
{
	if (mode == FU_SYNAPTICSMST_MODE_DIRECT)
		return "DIRECT";
	if (mode == FU_SYNAPTICSMST_MODE_REMOTE)
		return "REMOTE";
	return NULL;
}

const gchar *
fu_synapticsmst_family_to_string (FuSynapticsmstFamily family)
{
	if (family == FU_SYNAPTICSMST_FAMILY_TESLA)
		return "tesla";
	if (family == FU_SYNAPTICSMST_FAMILY_LEAF)
		return "leaf";
	if (family == FU_SYNAPTICSMST_FAMILY_PANAMERA)
		return "panamera";
	return NULL;
}

FuSynapticsmstFamily
fu_synapticsmst_family_from_chip_id (guint16 chip_id)
{
	if (chip_id >= 0x5000 && chip_id < 0x6000)
		return FU_SYNAPTICSMST_FAMILY_PANAMERA;
	if (chip_id >= 0x3000 && chip_id < 0x4000)
		return FU_SYNAPTICSMST_FAMILY_LEAF;
	if (chip_id >= 0x2000 && chip_id < 0x3000)
		return FU_SYNAPTICSMST_FAMILY_TESLA;
	return FU_SYNAPTICSMST_FAMILY_UNKNOWN;
}
