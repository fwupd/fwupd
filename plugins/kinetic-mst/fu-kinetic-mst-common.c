/*
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-kinetic-mst-common.h"

const gchar *
fu_kinetic_mst_mode_to_string (FuKineticMstMode mode)
{
	if (mode == FU_KINETIC_MST_MODE_DIRECT)
		return "DIRECT";
	if (mode == FU_KINETIC_MST_MODE_REMOTE)
		return "REMOTE";
	return NULL;
}

const gchar *
fu_kinetic_mst_family_to_string (FuKineticMstFamily family)
{
	if (family == FU_KINETIC_MST_FAMILY_TESLA)
		return "tesla";
	if (family == FU_KINETIC_MST_FAMILY_LEAF)
		return "leaf";
	if (family == FU_KINETIC_MST_FAMILY_PANAMERA)
		return "panamera";
	return NULL;
}

FuKineticMstFamily
fu_kinetic_mst_family_from_chip_id (guint16 chip_id)
{
	if (chip_id >= 0x5000 && chip_id < 0x6000)
		return FU_KINETIC_MST_FAMILY_PANAMERA;
	if (chip_id >= 0x3000 && chip_id < 0x4000)
		return FU_KINETIC_MST_FAMILY_LEAF;
	if (chip_id >= 0x2000 && chip_id < 0x3000)
		return FU_KINETIC_MST_FAMILY_TESLA;
	return FU_KINETIC_MST_FAMILY_UNKNOWN;
}
