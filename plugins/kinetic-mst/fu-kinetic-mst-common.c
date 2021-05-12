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
fu_kinetic_mst_family_to_string(FuKineticMstFamily family)
{
	if (family == FU_KINETIC_MST_FAMILY_MUSTANG)
		return "mustang";
	if (family == FU_KINETIC_MST_FAMILY_JAGUAR)
		return "jaguar";
	return NULL;
}

