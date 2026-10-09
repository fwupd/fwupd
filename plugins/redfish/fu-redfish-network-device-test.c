/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-redfish-network-device.h"
#include "fu-redfish-struct.h"

typedef struct {
	GTestDBus *dbus;
} FuRedfishNetworkFixture;

static void
fu_redfish_network_fixture_set_up(FuRedfishNetworkFixture *fix, gconstpointer user_data)
{
	/* an isolated, empty private bus stands in for the system bus, so the
	 * NetworkManager calls fail deterministically without real hardware */
	fix->dbus = g_test_dbus_new(G_TEST_DBUS_NONE);
	g_test_dbus_up(fix->dbus);
	(void)g_setenv("DBUS_SYSTEM_BUS_ADDRESS", g_test_dbus_get_bus_address(fix->dbus), TRUE);
}

static void
fu_redfish_network_fixture_tear_down(FuRedfishNetworkFixture *fix, gconstpointer user_data)
{
	g_test_dbus_down(fix->dbus);
	g_object_unref(fix->dbus);
}

static void
fu_redfish_network_device_func(FuRedfishNetworkFixture *fix, gconstpointer user_data)
{
	FuRedfishNetworkDeviceState state = FU_REDFISH_NETWORK_DEVICE_STATE_UNKNOWN;
	gboolean ret;
	g_autofree gchar *ip_addr = NULL;
	g_autoptr(FuRedfishNetworkDevice) device =
	    fu_redfish_network_device_new("/org/freedesktop/NetworkManager/Devices/0");
	g_autoptr(GError) error_addr = NULL;
	g_autoptr(GError) error_connect = NULL;
	g_autoptr(GError) error_state = NULL;

	g_assert_nonnull(device);

	/* the device has no State property on the empty bus */
	ret = fu_redfish_network_device_get_state(device, &state, &error_state);
	g_assert_error(error_state, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);

	/* activating the connection fails as NetworkManager is not present */
	ret = fu_redfish_network_device_connect(device, &error_connect);
	g_assert_error(error_connect, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN);
	g_assert_false(ret);

	/* no IPv4 config on the empty bus */
	ip_addr = fu_redfish_network_device_get_address(device, &error_addr);
	g_assert_error(error_addr, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(ip_addr);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add("/redfish/network-device",
		   FuRedfishNetworkFixture,
		   NULL,
		   fu_redfish_network_fixture_set_up,
		   fu_redfish_network_device_func,
		   fu_redfish_network_fixture_tear_down);
	return g_test_run();
}
