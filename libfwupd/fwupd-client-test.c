/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-client-sync.h"
#include "fwupd-error.h"
#include "fwupd-test.h"

static void
fwupd_client_api_undefined_setter(void)
{
#if GLIB_CHECK_VERSION(2, 74, 0)
	if (g_test_subprocess()) {
		g_autoptr(FwupdClient) client = fwupd_client_new();
		GValue value_bool = G_VALUE_INIT;

		g_value_init(&value_bool, G_TYPE_BOOLEAN);
		g_object_set_property(G_OBJECT(client), "battery-adapter", &value_bool);
	} else {
		g_test_trap_subprocess("/fwupd/client/api/undefined_setter",
				       0,
				       G_TEST_SUBPROCESS_DEFAULT);
		g_test_trap_assert_failed();
		g_test_trap_assert_stderr(
		    "*GLib-GObject-CRITICAL*has no property named 'battery-adapter'*");
	}
#else
	g_test_skip("Trap handling requires glib 2.74");
#endif
}

static void
fwupd_client_api_undefined_getter(void)
{
#if GLIB_CHECK_VERSION(2, 74, 0)
	if (g_test_subprocess()) {
		g_autoptr(FwupdClient) client = fwupd_client_new();
		GValue value_bool = G_VALUE_INIT;

		g_value_init(&value_bool, G_TYPE_BOOLEAN);
		g_object_get_property(G_OBJECT(client), "battery-adapter", &value_bool);
	} else {
		g_test_trap_subprocess("/fwupd/client/api/undefined_getter",
				       0,
				       G_TEST_SUBPROCESS_DEFAULT);
		g_test_trap_assert_failed();
		g_test_trap_assert_stderr(
		    "*GLib-GObject-CRITICAL*has no property named 'battery-adapter'*");
	}
#else
	g_test_skip("Trap handling requires glib 2.74");
#endif
}

static void
fwupd_client_api_ro_props(void)
{
#if GLIB_CHECK_VERSION(2, 74, 0)
	const gchar *props[] = {"daemon-version", "tainted", "interactive", "only-trusted", NULL};

	for (guint i = 0; i < G_N_ELEMENTS(props); i++) {
		if (g_test_subprocess()) {
			g_autoptr(FwupdClient) client = fwupd_client_new();
			GValue value_bool = G_VALUE_INIT;

			g_value_init(&value_bool, G_TYPE_BOOLEAN);
			g_object_set_property(G_OBJECT(client), props[i], &value_bool);
		} else {
			g_test_trap_subprocess("/fwupd/client/api/ro_props",
					       0,
					       G_TEST_SUBPROCESS_DEFAULT);
			g_test_trap_assert_failed();
			g_test_trap_assert_stderr(
			    "*GLib-GObject-CRITICAL*property*is not writable*");
		}
	}
#else
	g_test_skip("Trap handling requires glib 2.74");
#endif
}

static void
fwupd_client_api(void)
{
	gboolean ret;
	const gchar *tmp = "1234567890abcdef";
	GValue value_str = G_VALUE_INIT;
	GValue value_int = G_VALUE_INIT;
	GValue value_bool = G_VALUE_INIT;
	g_autoptr(FwupdClient) client = fwupd_client_new();
	g_autoptr(GError) error = NULL;

	g_value_init(&value_str, G_TYPE_STRING);
	g_value_init(&value_int, G_TYPE_INT);
	g_value_init(&value_bool, G_TYPE_BOOLEAN);
	g_value_set_string(&value_str, tmp);

	ret = fwupd_client_get_only_trusted(client);
	g_assert_false(ret);
	ret = fwupd_client_get_daemon_interactive(client);
	g_assert_false(ret);
	ret = fwupd_client_get_tainted(client);
	g_assert_false(ret);

	/* set the version multiple times */
	fwupd_client_set_daemon_version(client, "1.2.3");
	g_assert_cmpstr(fwupd_client_get_daemon_version(client), ==, "1.2.3");
	fwupd_client_set_daemon_version(client, "1.2.4");
	g_assert_cmpstr(fwupd_client_get_daemon_version(client), ==, "1.2.4");
	fwupd_client_set_daemon_version(client, "1.2.4");

	/* set host security ID multiple times */
	g_object_set_property(G_OBJECT(client), "host-security-id", &value_str);
	g_assert_cmpstr(fwupd_client_get_host_security_id(client), ==, tmp);
	g_object_set_property(G_OBJECT(client), "host-security-id", &value_str);

	/* set host machine ID multiple times */
	g_object_set_property(G_OBJECT(client), "host-machine-id", &value_str);
	g_assert_cmpstr(fwupd_client_get_host_machine_id(client), ==, tmp);
	g_object_set_property(G_OBJECT(client), "host-machine-id", &value_str);

	/* set host product ID and product vendor multiple times */
	tmp = "Acme";
	g_value_set_string(&value_str, tmp);
	g_object_set_property(G_OBJECT(client), "host-vendor", &value_str);
	g_assert_cmpstr(fwupd_client_get_host_vendor(client), ==, tmp);
	g_object_set_property(G_OBJECT(client), "host-vendor", &value_str);

	tmp = "Anvil";
	g_value_set_string(&value_str, tmp);
	g_object_set_property(G_OBJECT(client), "host-product", &value_str);
	g_assert_cmpstr(fwupd_client_get_host_product(client), ==, tmp);
	g_object_set_property(G_OBJECT(client), "host-product", &value_str);

	/* set BKC */
	tmp = "BKC-123";
	g_value_set_string(&value_str, tmp);
	g_object_set_property(G_OBJECT(client), "host-bkc", &value_str);
	g_assert_cmpstr(fwupd_client_get_host_bkc(client), ==, tmp);
	g_object_set_property(G_OBJECT(client), "host-bkc", &value_str);

	/* verify experience with no user agent explicitly */
	ret = fwupd_client_ensure_networking(client, &error);
	g_assert_true(ret);
	g_assert_no_error(error);

	/* verify experience with a good user agent*/
	fwupd_client_set_user_agent_for_package(client, "fwupd", "2.0.0");
	ret = fwupd_client_ensure_networking(client, &error);
	g_assert_true(ret);
	g_assert_no_error(error);

	/* set same battery level multiple times */
	g_value_set_int(&value_int, 50);
	g_object_set_property(G_OBJECT(client), "battery-level", &value_int);
	g_assert_cmpint(fwupd_client_get_battery_level(client), ==, 50);
	g_object_set_property(G_OBJECT(client), "battery-level", &value_int);

	/* set same battery threshold multiple times */
	g_value_set_int(&value_int, 20);
	g_object_set_property(G_OBJECT(client), "battery-threshold", &value_int);
	g_assert_cmpint(fwupd_client_get_battery_threshold(client), ==, 20);
	g_object_set_property(G_OBJECT(client), "battery-threshold", &value_int);

	/* set same status multiple times */
	g_value_set_int(&value_int, FWUPD_STATUS_IDLE);
	g_object_set_property(G_OBJECT(client), "status", &value_int);
	g_assert_cmpint(fwupd_client_get_status(client), ==, FWUPD_STATUS_IDLE);
	g_object_set_property(G_OBJECT(client), "status", &value_int);

	/* set same percentage multiple times */
	g_value_set_int(&value_int, 50);
	g_object_set_property(G_OBJECT(client), "percentage", &value_int);
	g_assert_cmpint(fwupd_client_get_percentage(client), ==, 50);
	g_object_set_property(G_OBJECT(client), "percentage", &value_int);

	/* set all properties */
	g_value_set_int(&value_int, 0);
	g_object_set_property(G_OBJECT(client), "status", &value_int);
	g_object_set_property(G_OBJECT(client), "percentage", &value_int);
	g_object_set_property(G_OBJECT(client), "host-bkc", &value_str);
	g_object_set_property(G_OBJECT(client), "host-vendor", &value_str);
	g_object_set_property(G_OBJECT(client), "host-product", &value_str);
	g_object_set_property(G_OBJECT(client), "host-machine-id", &value_str);
	g_object_set_property(G_OBJECT(client), "host-security-id", &value_str);
	g_object_set_property(G_OBJECT(client), "battery-level", &value_int);
	g_object_set_property(G_OBJECT(client), "battery-threshold", &value_int);

	/* read all properties */
	g_object_get_property(G_OBJECT(client), "status", &value_int);
	g_object_get_property(G_OBJECT(client), "tainted", &value_bool);
	g_object_get_property(G_OBJECT(client), "interactive", &value_bool);
	g_object_get_property(G_OBJECT(client), "percentage", &value_int);
	g_object_get_property(G_OBJECT(client), "daemon-version", &value_str);
	g_object_get_property(G_OBJECT(client), "host-bkc", &value_str);
	g_object_get_property(G_OBJECT(client), "host-vendor", &value_str);
	g_object_get_property(G_OBJECT(client), "host-product", &value_str);
	g_object_get_property(G_OBJECT(client), "host-machine-id", &value_str);
	g_object_get_property(G_OBJECT(client), "host-security-id", &value_str);
	g_object_get_property(G_OBJECT(client), "only-trusted", &value_bool);
	g_object_get_property(G_OBJECT(client), "battery-level", &value_int);
	g_object_get_property(G_OBJECT(client), "battery-threshold", &value_int);

	g_value_unset(&value_str);
	g_value_unset(&value_int);
	g_value_unset(&value_bool);
}

static void
fwupd_client_devices_func(void)
{
	FwupdDevice *dev;
	gboolean ret;
	g_autoptr(FwupdClient) client = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GError) error = NULL;

	client = fwupd_client_new();

	/* only run if running fwupd is new enough */
	ret = fwupd_client_connect(client, NULL, &error);
	if (ret == FALSE && (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_TIMED_OUT) ||
			     g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER) ||
			     g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN))) {
		g_debug("%s", error->message);
		g_test_skip("timeout connecting to daemon");
		return;
	}
	g_assert_no_error(error);
	g_assert_true(ret);
	if (fwupd_client_get_daemon_version(client) == NULL) {
		g_test_skip("no enabled fwupd daemon");
		return;
	}
	if (!g_str_has_prefix(fwupd_client_get_daemon_version(client), "1.")) {
		g_test_skip("running fwupd is too old");
		return;
	}

	array = fwupd_client_get_devices(client, NULL, &error);
	if (array == NULL && g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO)) {
		g_test_skip("no available fwupd devices");
		return;
	}
	if (array == NULL && g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip("no available fwupd daemon");
		return;
	}
	g_assert_no_error(error);
	g_assert_nonnull(array);
	g_assert_cmpint(array->len, >, 0);

	/* check device */
	dev = g_ptr_array_index(array, 0);
	g_assert_true(FWUPD_IS_DEVICE(dev));
	g_assert_cmpstr(fwupd_device_get_guid_default(dev), !=, NULL);
	g_assert_cmpstr(fwupd_device_get_id(dev), !=, NULL);
}

static void
fwupd_client_remotes_func(void)
{
	gboolean ret;
	g_autofree gchar *remotesdir = NULL;
	g_autoptr(FwupdClient) client = NULL;
	g_autoptr(FwupdRemote) remote2 = NULL;
	g_autoptr(FwupdRemote) remote3 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = NULL;

	remotesdir = g_test_build_filename(G_TEST_DIST, "tests", "remotes.d", NULL);
	(void)g_setenv("FU_SELF_TEST_REMOTES_DIR", remotesdir, TRUE);

	client = fwupd_client_new();

	/* only run if running fwupd is new enough */
	ret = fwupd_client_connect(client, NULL, &error);
	if (ret == FALSE && (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_TIMED_OUT) ||
			     g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER) ||
			     g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN))) {
		g_debug("%s", error->message);
		g_test_skip("timeout connecting to daemon");
		return;
	}
	g_assert_no_error(error);
	g_assert_true(ret);
	if (fwupd_client_get_daemon_version(client) == NULL) {
		g_test_skip("no enabled fwupd daemon");
		return;
	}
	if (!g_str_has_prefix(fwupd_client_get_daemon_version(client), "1.")) {
		g_test_skip("running fwupd is too old");
		return;
	}

	array = fwupd_client_get_remotes(client, NULL, &error);
	if (array == NULL && g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO)) {
		g_test_skip("no available fwupd remotes");
		return;
	}
	if (array == NULL && g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip("no available fwupd daemon");
		return;
	}
	g_assert_no_error(error);
	g_assert_nonnull(array);
	g_assert_cmpint(array->len, >, 0);

	/* check we can find the right thing */
	remote2 = fwupd_client_get_remote_by_id(client, "lvfs", NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(remote2);
	g_assert_cmpstr(fwupd_remote_get_id(remote2), ==, "lvfs");
	g_assert_nonnull(fwupd_remote_get_metadata_uri(remote2));

	/* check we set an error when unfound */
	remote3 = fwupd_client_get_remote_by_id(client, "XXXX", NULL, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(remote3);
}

static gboolean
fwupd_has_system_bus(void)
{
	g_autoptr(GDBusConnection) conn = NULL;
	conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
	if (conn != NULL)
		return TRUE;
	g_debug("D-Bus system bus unavailable, skipping tests");
	return FALSE;
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/client/api", fwupd_client_api);
	if (g_test_undefined()) {
		g_test_add_func("/fwupd/client/api/undefined_setter",
				fwupd_client_api_undefined_setter);
		g_test_add_func("/fwupd/client/api/undefined_getter",
				fwupd_client_api_undefined_getter);
		g_test_add_func("/fwupd/client/api/ro_props", fwupd_client_api_ro_props);
	}
	if (fwupd_has_system_bus()) {
		g_test_add_func("/fwupd/client/remotes", fwupd_client_remotes_func);
		g_test_add_func("/fwupd/client/devices", fwupd_client_devices_func);
	}
	return g_test_run();
}
