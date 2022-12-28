/*
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
 * Copyright (C) 2022 Hai Su <hsu@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-kinetic-dp-common.h"

const gchar *
fu_kinetic_dp_mode_to_string(FuKineticDpMode mode)
{
	if (mode == FU_KINETIC_DP_MODE_DIRECT)
		return "DIRECT";
	if (mode == FU_KINETIC_DP_MODE_REMOTE)
		return "REMOTE";
	return NULL;
}

const gchar *
fu_kinetic_dp_family_to_string(FuKineticDpFamily family)
{
	if (family == FU_KINETIC_DP_FAMILY_MUSTANG)
		return "MUSTANG";
	if (family == FU_KINETIC_DP_FAMILY_JAGUAR)
		return "JAGUAR";
	if (family == FU_KINETIC_DP_FAMILY_PUMA)
		return "PUMA";
	return NULL;
}

const gchar *
fu_kinetic_dp_chip_id_to_string(KtChipId chip_id)
{
	if (chip_id == KT_CHIP_BOBCAT_2800 || chip_id == KT_CHIP_BOBCAT_2850)
		return "BOBCAT";
	if (chip_id == KT_CHIP_PEGASUS)
		return "PEGASUS";
	if (chip_id == KT_CHIP_MYSTIQUE)
		return "MYSTIQUE";
	if (chip_id == KT_CHIP_DP2VGA)
		return "DP2VGS";
	if (chip_id == KT_CHIP_PUMA_2900 || chip_id == KT_CHIP_PUMA_2920)
		return "PUMA";
	if (chip_id == KT_CHIP_MUSTANG_5200)
		return "MUSTANG";
	if (chip_id == KT_CHIP_JAGUAR_5000)
		return "JAGUAR";
	return "UNKNOWN";
}
FuKineticDpFamily
fu_kinetic_dp_chip_id_to_family(KtChipId chip_id)
{
	if (chip_id == KT_CHIP_PUMA_2900 || chip_id == KT_CHIP_PUMA_2920)
		return FU_KINETIC_DP_FAMILY_PUMA;
	if (chip_id == KT_CHIP_MUSTANG_5200)
		return FU_KINETIC_DP_FAMILY_MUSTANG;
	if (chip_id == KT_CHIP_JAGUAR_5000)
		return FU_KINETIC_DP_FAMILY_JAGUAR;
	return FU_KINETIC_DP_FAMILY_UNKNOWN;
}

gchar *
fu_kinetic_dp_version_to_string(guint32 fw_version)
{
	return g_strdup_printf("%1u.%03u.%02u",
			       (fw_version >> 16) & 0xff,
			       (fw_version >> 8) & 0xff,
			       fw_version & 0xff);
}
