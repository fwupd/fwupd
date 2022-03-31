/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ccgx-dmc-common.h"

const gchar *
fu_ccgx_dmc_update_model_type_to_string(DmcUpdateModel val)
{
	if (val == DMC_UPDATE_MODEL_NONE)
		return "None";
	if (val == DMC_UPDATE_MODEL_DOWNLOAD_TRIGGER)
		return "Download Trigger";
	if (val == DMC_UPDATE_MODEL_PENDING_RESET)
		return "Pending Reset";
	return NULL;
}
