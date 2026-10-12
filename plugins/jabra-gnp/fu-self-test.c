/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-jabra-gnp-child-device.h"
#include "fu-jabra-gnp-struct.h"

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

static void
fu_jabra_gnp_child_device_battery_report_func(void)
{
	/* captured from a Jabra Link 390 updating an Evolve2 65 Flex */
	const guint8 report[] = {0x05, 0x00, 0x01, 0x00, 0x0A, 0x12, 0x02, 0x15, 0x44};
	const guint8 response[] = {0x05, 0x00, 0x04, 0x03, 0xCA, 0x02, 0x13, 0x15, 0x41};
	g_autoptr(FuStructJabraGnpPacket) st1 = NULL;
	g_autoptr(FuStructJabraGnpPacket) st2 = NULL;
	g_autoptr(GError) error = NULL;

	st1 = fu_struct_jabra_gnp_packet_parse(report, sizeof(report), 0x0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(st1);
	g_assert_cmpint(fu_struct_jabra_gnp_packet_get_cmd(st1),
			==,
			FU_JABRA_GNP_PACKET_CMD_GET_BATTERY_LEVEL);

	st2 = fu_struct_jabra_gnp_packet_parse(response, sizeof(response), 0x0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(st2);
	g_assert_cmpint(fu_struct_jabra_gnp_packet_get_cmd(st2),
			!=,
			FU_JABRA_GNP_PACKET_CMD_GET_BATTERY_LEVEL);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/jabra-gnp/child-device", fu_jabra_gnp_child_device_func);
	g_test_add_func("/jabra-gnp/child-device/battery-report",
			fu_jabra_gnp_child_device_battery_report_func);
	return g_test_run();
}
