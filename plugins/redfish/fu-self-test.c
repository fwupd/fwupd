/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>

#include "fu-redfish-common.h"
#include "fu-redfish-network.h"

static void
fu_test_redfish_common_func (void)
{
	const guint8 buf[16] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
				 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f  };
	g_autofree gchar *ipv4 = NULL;
	g_autofree gchar *ipv6 = NULL;
	g_autofree gchar *maca = NULL;

	ipv4 = fu_redfish_common_buffer_to_ipv4 (buf);
	g_assert_cmpstr (ipv4, ==, "0.1.2.3");
	ipv6 = fu_redfish_common_buffer_to_ipv6 (buf);
	g_assert_cmpstr (ipv6, ==, "00010203:04050607:08090a0b:0c0d0e0f");
	maca = fu_redfish_common_buffer_to_mac (buf);
	g_assert_cmpstr (maca, ==, "00:01:02:03:04:05");
}

static void
fu_test_redfish_network_mac_addr_func (void)
{
	g_autofree gchar *ip_addr = NULL;
	g_autoptr(GError) error = NULL;

	ip_addr = fu_redfish_network_ip_for_mac_addr ("00:13:F7:29:C2:D8", &error);
	if (ip_addr == NULL &&
	    g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
		g_test_skip ("no hardware");
		return;
	}
	g_assert_no_error (error);
	g_assert_nonnull (ip_addr);
}

static void
fu_test_redfish_network_vid_pid_func (void)
{
	g_autofree gchar *ip_addr = NULL;
	g_autoptr(GError) error = NULL;

	ip_addr = fu_redfish_network_ip_for_vid_pid (0x0707, 0x0201, &error);
	if (ip_addr == NULL &&
	    g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
		g_test_skip ("no hardware");
		return;
	}
	g_assert_no_error (error);
	g_assert_nonnull (ip_addr);
}

int
main (int argc, char **argv)
{
	g_autoptr(GError) error = NULL;
	g_setenv ("FWUPD_REDFISH_VERBOSE", "1", TRUE);
	g_test_init (&argc, &argv, NULL);
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	g_test_add_func ("/redfish/common", fu_test_redfish_common_func);
	g_test_add_func ("/redfish/network{mac_addr}", fu_test_redfish_network_mac_addr_func);
	g_test_add_func ("/redfish/network{vid_pid}", fu_test_redfish_network_vid_pid_func);
	return g_test_run ();
}
