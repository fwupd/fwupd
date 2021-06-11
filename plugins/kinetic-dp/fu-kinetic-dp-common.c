/*
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
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
	return NULL;
}

