/*
 * Copyright 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 * Copyright 2021 Apollo Ling <apollo.ling@synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-synaptics-mst-common.h"

/*
 * NOTE: DO NOT ALLOW ANY MORE MAGIC CONSTANTS IN THIS FILE
 * nocheck:magic-inlines=550
 */

FuSynapticsMstFamily
fu_synaptics_mst_family_from_chip_id(guint16 chip_id)
{
	if (chip_id >= 0x8000 && chip_id < 0xA000)
		return FU_SYNAPTICS_MST_FAMILY_CARRERA;
	if (chip_id >= 0x7000 && chip_id < 0x8000)
		return FU_SYNAPTICS_MST_FAMILY_SPYDER;
	if ((chip_id >= 0x6000 && chip_id < 0x7000) || (chip_id >= 0x8000 && chip_id < 0x9000))
		return FU_SYNAPTICS_MST_FAMILY_CAYENNE;
	if (chip_id >= 0x5000 && chip_id < 0x6000)
		return FU_SYNAPTICS_MST_FAMILY_PANAMERA;
	if (chip_id >= 0x3000 && chip_id < 0x4000)
		return FU_SYNAPTICS_MST_FAMILY_LEAF;
	if (chip_id >= 0x2000 && chip_id < 0x3000)
		return FU_SYNAPTICS_MST_FAMILY_TESLA;
	return FU_SYNAPTICS_MST_FAMILY_UNKNOWN;
}
