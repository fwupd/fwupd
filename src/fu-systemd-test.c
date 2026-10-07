/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <gio/gio.h>

#include "fu-systemd.h"

#define MOCK_SYSTEMD_SERVICE	 "org.freedesktop.systemd1"
#define MOCK_SYSTEMD_OBJECT_PATH "/org/freedesktop/systemd1"
#define MOCK_SYSTEMD_UNIT_PATH	 "/org/freedesktop/systemd1/unit/fwupd_2eservice"
#define MOCK_SYSTEMD_KNOWN_UNIT	 "fwupd.service"

typedef struct {
	GTestDBus *dbus;
	GDBusConnection *conn;
	GMainContext *mock_ctx;
	GMainLoop *mock_loop;
	GThread *mock_thread;
	guint reg_manager;
	guint reg_unit;
	guint owner_id;
	gboolean stop_called;
} FuSystemdTestFixture;

static const gchar introspection_manager_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.systemd1.Manager'>"
    "    <method name='GetUnit'>"
    "      <arg type='s' name='name' direction='in'/>"
    "      <arg type='o' name='unit' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

static const gchar introspection_unit_xml[] = "<node>"
					      "  <interface name='org.freedesktop.systemd1.Unit'>"
					      "    <method name='Stop'>"
					      "      <arg type='s' name='mode' direction='in'/>"
					      "      <arg type='o' name='job' direction='out'/>"
					      "    </method>"
					      "  </interface>"
					      "</node>";

static void
fu_systemd_test_manager_method_call(GDBusConnection *connection,
				    const gchar *sender,
				    const gchar *object_path,
				    const gchar *interface_name,
				    const gchar *method_name,
				    GVariant *parameters,
				    GDBusMethodInvocation *invocation,
				    gpointer user_data)
{
	if (g_strcmp0(method_name, "GetUnit") == 0) {
		const gchar *name = NULL;
		g_variant_get(parameters, "(&s)", &name);
		if (g_strcmp0(name, MOCK_SYSTEMD_KNOWN_UNIT) != 0) {
			g_dbus_method_invocation_return_dbus_error(
			    invocation,
			    "org.freedesktop.systemd1.NoSuchUnit",
			    "Unit not loaded.");
			return;
		}
		g_dbus_method_invocation_return_value(invocation,
						      g_variant_new("(o)", MOCK_SYSTEMD_UNIT_PATH));
		return;
	}
	g_dbus_method_invocation_return_dbus_error(invocation,
						   "org.freedesktop.DBus.Error.UnknownMethod",
						   "no such method");
}

static void
fu_systemd_test_unit_method_call(GDBusConnection *connection,
				 const gchar *sender,
				 const gchar *object_path,
				 const gchar *interface_name,
				 const gchar *method_name,
				 GVariant *parameters,
				 GDBusMethodInvocation *invocation,
				 gpointer user_data)
{
	FuSystemdTestFixture *fix = user_data;
	if (g_strcmp0(method_name, "Stop") == 0) {
		fix->stop_called = TRUE;
		/* systemd returns the object path of the enqueued job */
		g_dbus_method_invocation_return_value(
		    invocation,
		    g_variant_new("(o)", "/org/freedesktop/systemd1/job/1"));
		return;
	}
	g_dbus_method_invocation_return_dbus_error(invocation,
						   "org.freedesktop.DBus.Error.UnknownMethod",
						   "no such method");
}

static const GDBusInterfaceVTable manager_vtable = {
    fu_systemd_test_manager_method_call,
    NULL,
    NULL,
};

static const GDBusInterfaceVTable unit_vtable = {
    fu_systemd_test_unit_method_call,
    NULL,
    NULL,
};

static gpointer
fu_systemd_test_mock_thread_cb(gpointer data)
{
	FuSystemdTestFixture *fix = data;
	g_main_loop_run(fix->mock_loop);
	return NULL;
}

static void
fu_systemd_test_setup(FuSystemdTestFixture *fix, gconstpointer user_data)
{
	g_autoptr(GDBusNodeInfo) node_manager = NULL;
	g_autoptr(GDBusNodeInfo) node_unit = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) result = NULL;

	fix->dbus = g_test_dbus_new(G_TEST_DBUS_NONE);
	g_test_dbus_up(fix->dbus);
	(void)g_setenv("DBUS_SYSTEM_BUS_ADDRESS", g_test_dbus_get_bus_address(fix->dbus), TRUE);

	fix->mock_ctx = g_main_context_new();
	fix->mock_loop = g_main_loop_new(fix->mock_ctx, FALSE);

	/* register objects on the mock context so that callbacks are dispatched
	 * there (iterated by the mock thread) */
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

	node_manager = g_dbus_node_info_new_for_xml(introspection_manager_xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(node_manager);
	fix->reg_manager = g_dbus_connection_register_object(fix->conn,
							     MOCK_SYSTEMD_OBJECT_PATH,
							     node_manager->interfaces[0],
							     &manager_vtable,
							     fix,
							     NULL,
							     &error);
	g_assert_no_error(error);
	g_assert_cmpuint(fix->reg_manager, !=, 0);

	node_unit = g_dbus_node_info_new_for_xml(introspection_unit_xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(node_unit);
	fix->reg_unit = g_dbus_connection_register_object(fix->conn,
							  MOCK_SYSTEMD_UNIT_PATH,
							  node_unit->interfaces[0],
							  &unit_vtable,
							  fix,
							  NULL,
							  &error);
	g_assert_no_error(error);
	g_assert_cmpuint(fix->reg_unit, !=, 0);

	g_main_context_pop_thread_default(fix->mock_ctx);

	/* own the systemd bus name */
	result = g_dbus_connection_call_sync(fix->conn,
					     "org.freedesktop.DBus",
					     "/org/freedesktop/DBus",
					     "org.freedesktop.DBus",
					     "RequestName",
					     g_variant_new("(su)", MOCK_SYSTEMD_SERVICE, 0u),
					     G_VARIANT_TYPE("(u)"),
					     G_DBUS_CALL_FLAGS_NONE,
					     -1,
					     NULL,
					     &error);
	g_assert_no_error(error);
	g_assert_nonnull(result);

	/* start the mock service thread */
	fix->mock_thread = g_thread_new("mock-systemd", fu_systemd_test_mock_thread_cb, fix);
}

static void
fu_systemd_test_teardown(FuSystemdTestFixture *fix, gconstpointer user_data)
{
	g_main_loop_quit(fix->mock_loop);
	g_thread_join(fix->mock_thread);
	g_dbus_connection_unregister_object(fix->conn, fix->reg_manager);
	g_dbus_connection_unregister_object(fix->conn, fix->reg_unit);
	g_object_unref(fix->conn);
	g_main_loop_unref(fix->mock_loop);
	g_main_context_unref(fix->mock_ctx);
	g_test_dbus_down(fix->dbus);
	g_object_unref(fix->dbus);
}

static void
fu_systemd_unit_stop_success_func(FuSystemdTestFixture *fix, gconstpointer user_data)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;

	ret = fu_systemd_unit_stop(MOCK_SYSTEMD_KNOWN_UNIT, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fix->stop_called);
}

static void
fu_systemd_unit_stop_not_found_func(FuSystemdTestFixture *fix, gconstpointer user_data)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;

	ret = fu_systemd_unit_stop("does-not-exist.service", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_assert_false(fix->stop_called);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add("/fwupd/systemd/unit-stop/success",
		   FuSystemdTestFixture,
		   NULL,
		   fu_systemd_test_setup,
		   fu_systemd_unit_stop_success_func,
		   fu_systemd_test_teardown);
	g_test_add("/fwupd/systemd/unit-stop/not-found",
		   FuSystemdTestFixture,
		   NULL,
		   fu_systemd_test_setup,
		   fu_systemd_unit_stop_not_found_func,
		   fu_systemd_test_teardown);
	return g_test_run();
}
