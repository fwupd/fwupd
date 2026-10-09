/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-dell-dock-common.h"
#include "fu-dell-dock-hub.h"

static void
fu_dell_dock_hub_instance_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_NO_CACHE);
	g_autoptr(FuDellDockHub) hub = g_object_new(FU_TYPE_DELL_DOCK_HUB, "context", ctx, NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_device_set_vid(FU_DEVICE(hub), 0x413C);
	fu_device_set_pid(FU_DEVICE(hub), 0xB06E);

	/* a regular hub and an atomic hub get different instance IDs */
	fu_dell_dock_hub_add_instance(hub, 0x0);
	g_assert_true(fu_device_has_instance_id(FU_DEVICE(hub),
						"USB\\VID_413C&PID_B06E&hub",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE));

	fu_dell_dock_hub_add_instance(hub, DOCK_BASE_TYPE_ATOMIC);
	g_assert_true(fu_device_has_instance_id(FU_DEVICE(hub),
						"USB\\VID_413C&PID_B06E&atomic_hub",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/dell-dock/hub{instance}", fu_dell_dock_hub_instance_func);
	return g_test_run();
}
