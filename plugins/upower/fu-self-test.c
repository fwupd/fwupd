/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-plugin-private.h"
#include "fu-upower-plugin.h"

typedef struct {
	GTestDBus *dbus;
	GDBusConnection *conn;
	GMainContext *mock_ctx;
	GMainLoop *mock_loop;
	GThread *mock_thread;
	guint reg_manager;
	guint reg_device;
	gboolean on_battery;
	gboolean lid_is_present;
	gboolean lid_is_closed;
	guint32 type;
	gdouble percentage;
	guint32 state;
} FuUpowerTestFixture;

static const gchar introspection_manager_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.UPower'>"
    "    <property name='OnBattery' type='b' access='read'/>"
    "    <property name='LidIsPresent' type='b' access='read'/>"
    "    <property name='LidIsClosed' type='b' access='read'/>"
    "  </interface>"
    "</node>";

static const gchar introspection_device_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.UPower.Device'>"
    "    <property name='Type' type='u' access='read'/>"
    "    <property name='Percentage' type='d' access='read'/>"
    "    <property name='State' type='u' access='read'/>"
    "  </interface>"
    "</node>";

static GVariant *
fu_upower_test_manager_get_property(GDBusConnection *connection,
				    const gchar *sender,
				    const gchar *object_path,
				    const gchar *interface_name,
				    const gchar *property_name,
				    GError **error,
				    gpointer user_data)
{
	FuUpowerTestFixture *fix = user_data;
	if (g_strcmp0(property_name, "OnBattery") == 0)
		return g_variant_new_boolean(fix->on_battery);
	if (g_strcmp0(property_name, "LidIsPresent") == 0)
		return g_variant_new_boolean(fix->lid_is_present);
	if (g_strcmp0(property_name, "LidIsClosed") == 0)
		return g_variant_new_boolean(fix->lid_is_closed);
	return NULL;
}

static GVariant *
fu_upower_test_device_get_property(GDBusConnection *connection,
				   const gchar *sender,
				   const gchar *object_path,
				   const gchar *interface_name,
				   const gchar *property_name,
				   GError **error,
				   gpointer user_data)
{
	FuUpowerTestFixture *fix = user_data;
	if (g_strcmp0(property_name, "Type") == 0)
		return g_variant_new_uint32(fix->type);
	if (g_strcmp0(property_name, "Percentage") == 0)
		return g_variant_new_double(fix->percentage);
	if (g_strcmp0(property_name, "State") == 0)
		return g_variant_new_uint32(fix->state);
	return NULL;
}

static const GDBusInterfaceVTable manager_vtable = {
    NULL,
    fu_upower_test_manager_get_property,
    NULL,
};

static const GDBusInterfaceVTable device_vtable = {
    NULL,
    fu_upower_test_device_get_property,
    NULL,
};

static gpointer
fu_upower_test_mock_thread_cb(gpointer data)
{
	FuUpowerTestFixture *fix = data;
	g_main_loop_run(fix->mock_loop);
	return NULL;
}

static void
fu_upower_test_setup(FuUpowerTestFixture *fix, gconstpointer user_data)
{
	g_autoptr(GDBusNodeInfo) node_device = NULL;
	g_autoptr(GDBusNodeInfo) node_manager = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) result = NULL;

	/* sensible defaults */
	fix->type = 2;
	fix->percentage = 50.0;
	fix->state = 2;
	fix->on_battery = FALSE;
	fix->lid_is_present = FALSE;
	fix->lid_is_closed = FALSE;

	fix->dbus = g_test_dbus_new(G_TEST_DBUS_NONE);
	g_test_dbus_up(fix->dbus);
	(void)g_setenv("DBUS_SYSTEM_BUS_ADDRESS", g_test_dbus_get_bus_address(fix->dbus), TRUE);

	fix->mock_ctx = g_main_context_new();
	fix->mock_loop = g_main_loop_new(fix->mock_ctx, FALSE);

	/* create the mock connection and register objects on the mock context so that
	 * callbacks are dispatched there (iterated by the mock thread) */
	g_main_context_push_thread_default(fix->mock_ctx);

	fix->conn = g_dbus_connection_new_for_address_sync(
	    g_test_dbus_get_bus_address(fix->dbus),
	    G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
		G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
	    NULL,
	    NULL,
	    &error);
	g_assert_no_error(error);
	g_assert_nonnull(fix->conn);

	/* register mock UPower manager */
	node_manager = g_dbus_node_info_new_for_xml(introspection_manager_xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(node_manager);
	fix->reg_manager = g_dbus_connection_register_object(fix->conn,
							     "/org/freedesktop/UPower",
							     node_manager->interfaces[0],
							     &manager_vtable,
							     fix,
							     NULL,
							     &error);
	g_assert_no_error(error);

	/* register mock UPower display device */
	node_device = g_dbus_node_info_new_for_xml(introspection_device_xml, &error);
	g_assert_no_error(error);
	fix->reg_device =
	    g_dbus_connection_register_object(fix->conn,
					      "/org/freedesktop/UPower/devices/DisplayDevice",
					      node_device->interfaces[0],
					      &device_vtable,
					      fix,
					      NULL,
					      &error);
	g_assert_no_error(error);

	g_main_context_pop_thread_default(fix->mock_ctx);

	/* own the UPower bus name */
	result = g_dbus_connection_call_sync(fix->conn,
					     "org.freedesktop.DBus",
					     "/org/freedesktop/DBus",
					     "org.freedesktop.DBus",
					     "RequestName",
					     g_variant_new("(su)", "org.freedesktop.UPower", 0u),
					     G_VARIANT_TYPE("(u)"),
					     G_DBUS_CALL_FLAGS_NONE,
					     -1,
					     NULL,
					     &error);
	g_assert_no_error(error);
	g_assert_nonnull(result);

	/* start the mock service thread */
	fix->mock_thread = g_thread_new("mock-upower", fu_upower_test_mock_thread_cb, fix);
}

static void
fu_upower_test_teardown(FuUpowerTestFixture *fix, gconstpointer user_data)
{
	g_main_loop_quit(fix->mock_loop);
	g_thread_join(fix->mock_thread);
	g_dbus_connection_unregister_object(fix->conn, fix->reg_manager);
	g_dbus_connection_unregister_object(fix->conn, fix->reg_device);
	g_object_unref(fix->conn);
	g_main_loop_unref(fix->mock_loop);
	g_main_context_unref(fix->mock_ctx);
	g_test_dbus_down(fix->dbus);
	g_object_unref(fix->dbus);
}

static void
fu_upower_battery_level_func(FuUpowerTestFixture *fix, gconstpointer user_data)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_upower_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	fix->percentage = 75.0;

	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_assert_cmpuint(fu_context_get_battery_level(ctx), ==, 75);
}

static void
fu_upower_no_battery_func(FuUpowerTestFixture *fix, gconstpointer user_data)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_upower_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	fix->type = 0;

	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_assert_cmpuint(fu_context_get_battery_level(ctx), ==, FWUPD_BATTERY_LEVEL_INVALID);
}

static void
fu_upower_on_battery_func(FuUpowerTestFixture *fix, gconstpointer user_data)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_upower_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	fix->on_battery = TRUE;

	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_assert_cmpint(fu_context_get_power_state(ctx), ==, FU_POWER_STATE_BATTERY);
}

static void
fu_upower_ac_power_func(FuUpowerTestFixture *fix, gconstpointer user_data)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_upower_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_assert_cmpint(fu_context_get_power_state(ctx), ==, FU_POWER_STATE_AC);
}

static void
fu_upower_lid_open_func(FuUpowerTestFixture *fix, gconstpointer user_data)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_upower_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	fix->lid_is_present = TRUE;

	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_assert_cmpint(fu_context_get_lid_state(ctx), ==, FU_LID_STATE_OPEN);
}

static void
fu_upower_lid_closed_func(FuUpowerTestFixture *fix, gconstpointer user_data)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_upower_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	fix->lid_is_present = TRUE;
	fix->lid_is_closed = TRUE;

	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_assert_cmpint(fu_context_get_lid_state(ctx), ==, FU_LID_STATE_CLOSED);
}

static void
fu_upower_lid_not_present_func(FuUpowerTestFixture *fix, gconstpointer user_data)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_upower_plugin_get_type(), ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_assert_cmpint(fu_context_get_lid_state(ctx), ==, FU_LID_STATE_UNKNOWN);
}

int
main(int argc, char **argv)
{
	g_autofree gchar *argv0 = NULL;
	g_test_init(&argc, &argv, NULL);

	/* nocheck:blocked -- while building packages this may not be available */
	argv0 = g_find_program_in_path("dbus-daemon");
	if (argv0 == NULL)
		return 0;

	g_test_add("/upower/battery-level",
		   FuUpowerTestFixture,
		   NULL,
		   fu_upower_test_setup,
		   fu_upower_battery_level_func,
		   fu_upower_test_teardown);
	g_test_add("/upower/no-battery",
		   FuUpowerTestFixture,
		   NULL,
		   fu_upower_test_setup,
		   fu_upower_no_battery_func,
		   fu_upower_test_teardown);
	g_test_add("/upower/on-battery",
		   FuUpowerTestFixture,
		   NULL,
		   fu_upower_test_setup,
		   fu_upower_on_battery_func,
		   fu_upower_test_teardown);
	g_test_add("/upower/ac-power",
		   FuUpowerTestFixture,
		   NULL,
		   fu_upower_test_setup,
		   fu_upower_ac_power_func,
		   fu_upower_test_teardown);
	g_test_add("/upower/lid-open",
		   FuUpowerTestFixture,
		   NULL,
		   fu_upower_test_setup,
		   fu_upower_lid_open_func,
		   fu_upower_test_teardown);
	g_test_add("/upower/lid-closed",
		   FuUpowerTestFixture,
		   NULL,
		   fu_upower_test_setup,
		   fu_upower_lid_closed_func,
		   fu_upower_test_teardown);
	g_test_add("/upower/lid-not-present",
		   FuUpowerTestFixture,
		   NULL,
		   fu_upower_test_setup,
		   fu_upower_lid_not_present_func,
		   fu_upower_test_teardown);
	return g_test_run();
}
