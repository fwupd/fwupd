/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-jabra-gnp-child-device.h"

static void
fu_jabra_gnp_child_device_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuJabraGnpChildDevice) device =
	    g_object_new(FU_TYPE_JABRA_GNP_CHILD_DEVICE, "context", ctx, NULL);

	g_assert_nonnull(device);

	/* resets the DFU PID and the transmit sequence number */
	fu_jabra_gnp_child_device_set_dfu_pid_and_seq(device, 0x2496);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/jabra-gnp/child-device", fu_jabra_gnp_child_device_func);
	return g_test_run();
}
