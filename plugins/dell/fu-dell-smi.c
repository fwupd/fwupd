/*
 * Copyright (C) 2017 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-dell-smi.h"

static void
_dell_smi_obj_free(FuDellSmiObj *obj)
{
	dell_smi_obj_free(obj->smi);
	g_free(obj);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuDellSmiObj, _dell_smi_obj_free);
#pragma clang diagnostic pop

/* don't actually clear if we're testing */
gboolean
fu_dell_clear_smi(FuDellSmiObj *obj)
{
	if (obj->fake_smbios)
		return TRUE;

	for (gint i = 0; i < 4; i++) {
		obj->input[i] = 0;
		obj->output[i] = 0;
	}
	return TRUE;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overread"
gboolean
fu_dell_execute_simple_smi(FuDellSmiObj *obj, guint16 class, guint16 select)
{
	/* test suite will mean don't actually call */
	if (obj->fake_smbios)
		return TRUE;

	if (dell_simple_ci_smi(class, select, obj->input, obj->output)) {
		g_debug("failed to run query %u/%u", class, select);
		return FALSE;
	}
	return TRUE;
}
#pragma GCC diagnostic pop
