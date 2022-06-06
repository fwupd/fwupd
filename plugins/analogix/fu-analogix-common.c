/*
 * Copyright (C) 2021 Xiaotian Cui <xtcui@analogixsemi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-analogix-common.h"

const gchar *
fu_analogix_update_status_to_string(AnxUpdateStatus status)
{
	if (status == UPDATE_STATUS_INVALID)
		return "invalid";
	if (status == UPDATE_STATUS_START)
		return "start";
	if (status == UPDATE_STATUS_FINISH)
		return "finish";
	if (status == UPDATE_STATUS_ERROR)
		return "error";
	return NULL;
}
