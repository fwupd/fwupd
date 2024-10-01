/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include <fcntl.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

#include "fwupd-remote-private.h"
#include "fwupd-security-attr-private.h"

#include "../plugins/test/fu-test-plugin.h"
#include "fu-bios-settings-private.h"
#include "fu-cabinet.h"
#include "fu-client-list.h"
#include "fu-config-private.h"
#include "fu-console.h"
#include "fu-context-private.h"
#include "fu-device-list.h"
#include "fu-device-private.h"
#include "fu-engine-config.h"
#include "fu-engine-helper.h"
#include "fu-engine-requirements.h"
#include "fu-engine.h"
#include "fu-history.h"
#include "fu-idle.h"
#include "fu-plugin-list.h"
#include "fu-plugin-private.h"
#include "fu-release-common.h"
#include "fu-remote-list.h"
#include "fu-remote.h"
#include "fu-security-attr-common.h"
#include "fu-smbios-private.h"
#include "fu-usb-backend.h"

#ifdef HAVE_GIO_UNIX
#include "fu-unix-seekable-input-stream.h"
#endif

typedef struct {
	FuPlugin *plugin;
	FuContext *ctx;
} FuTest;

static GMainLoop *_test_loop = NULL;
static guint _test_loop_timeout_id = 0;

static gboolean
fu_test_hang_check_cb(gpointer user_data)
{
	g_main_loop_quit(_test_loop);
	_test_loop_timeout_id = 0;
	return G_SOURCE_REMOVE;
}

static void
fu_test_loop_run_with_timeout(guint timeout_ms)
{
	g_assert_cmpint(_test_loop_timeout_id, ==, 0);
	g_assert_null(_test_loop);
	_test_loop = g_main_loop_new(NULL, FALSE);
	_test_loop_timeout_id = g_timeout_add(timeout_ms, fu_test_hang_check_cb, NULL);
	g_main_loop_run(_test_loop);
}

static void
fu_test_loop_quit(void)
{
	if (_test_loop_timeout_id > 0) {
		g_source_remove(_test_loop_timeout_id);
		_test_loop_timeout_id = 0;
	}
	if (_test_loop != NULL) {
		g_main_loop_quit(_test_loop);
		g_main_loop_unref(_test_loop);
		_test_loop = NULL;
	}
}

static void
fu_self_test_mkroot(void)
{
	if (g_file_test("/tmp/fwupd-self-test", G_FILE_TEST_EXISTS)) {
		g_autoptr(GError) error = NULL;
		if (!fu_path_rmtree("/tmp/fwupd-self-test", &error))
			g_warning("failed to mkroot: %s", error->message);
	}
	g_assert_cmpint(g_mkdir_with_parents("/tmp/fwupd-self-test/var/lib/fwupd", 0755), ==, 0);
}

static void
fu_test_copy_file(const gchar *source, const gchar *target)
{
	gboolean ret;
	g_autofree gchar *data = NULL;
	g_autoptr(GError) error = NULL;

	g_debug("copying %s to %s", source, target);
	ret = g_file_get_contents(source, &data, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = g_file_set_contents(target, data, -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static gboolean
fu_test_compare_lines(const gchar *txt1, const gchar *txt2, GError **error)
{
	g_autofree gchar *output = NULL;
	g_autofree gchar *diff = g_find_program_in_path("diff");
	g_autofree gchar *cmd = g_strdup_printf("%s -urNp /tmp/b /tmp/a", diff);
	if (g_strcmp0(txt1, txt2) == 0)
		return TRUE;
	if (g_pattern_match_simple(txt2, txt1))
		return TRUE;
	if (diff == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "does not match: %s vs %s",
			    txt1,
			    txt2);
		return FALSE;
	}
	if (!g_file_set_contents("/tmp/a", txt1, -1, error))
		return FALSE;
	if (!g_file_set_contents("/tmp/b", txt2, -1, error))
		return FALSE;
	if (!g_spawn_command_line_sync(cmd, &output, NULL, NULL, error))
		return FALSE;
	g_set_error_literal(error, 1, 0, output);
	return FALSE;
}

static void
fu_test_free(FuTest *self)
{
	if (self->ctx != NULL)
		g_object_unref(self->ctx);
	g_free(self);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuTest, fu_test_free)
#pragma clang diagnostic pop

static void
fu_client_list_func(void)
{
	g_autoptr(FuClient) client_find = NULL;
	g_autoptr(FuClient) client = NULL;
	g_autoptr(FuClient) client_orig = NULL;
	g_autoptr(FuClientList) client_list = fu_client_list_new(NULL);
	g_autoptr(GPtrArray) clients_empty = NULL;
	g_autoptr(GPtrArray) clients_full = NULL;

	/* ensure empty */
	clients_empty = fu_client_list_get_all(client_list);
	g_assert_cmpint(clients_empty->len, ==, 0);

	/* register a client, then find it */
	client_orig = fu_client_list_register(client_list, ":hello");
	g_assert_nonnull(client_orig);
	client_find = fu_client_list_get_by_sender(client_list, ":hello");
	g_assert_nonnull(client_find);
	g_assert_true(client_orig == client_find);
	clients_full = fu_client_list_get_all(client_list);
	g_assert_cmpint(clients_full->len, ==, 1);

	/* register a duplicate, check properties */
	client = fu_client_list_register(client_list, ":hello");
	g_assert_nonnull(client);
	g_assert_true(client_orig == client);
	g_assert_cmpstr(fu_client_get_sender(client), ==, ":hello");
	g_assert_cmpint(fu_client_get_feature_flags(client), ==, FWUPD_FEATURE_FLAG_NONE);
	g_assert_cmpstr(fu_client_lookup_hint(client, "key"), ==, NULL);
	g_assert_true(fu_client_has_flag(client, FU_CLIENT_FLAG_ACTIVE));
	fu_client_insert_hint(client, "key", "value");
	fu_client_set_feature_flags(client, FWUPD_FEATURE_FLAG_UPDATE_ACTION);
	g_assert_cmpstr(fu_client_lookup_hint(client, "key"), ==, "value");
	g_assert_cmpint(fu_client_get_feature_flags(client), ==, FWUPD_FEATURE_FLAG_UPDATE_ACTION);

	/* emulate disconnect */
	fu_client_remove_flag(client, FU_CLIENT_FLAG_ACTIVE);
	g_assert_false(fu_client_has_flag(client, FU_CLIENT_FLAG_ACTIVE));
}

static void
fu_idle_func(void)
{
	guint token;
	g_autoptr(FuIdle) idle = fu_idle_new();

	fu_idle_reset(idle);
	g_assert_false(fu_idle_has_inhibit(idle, FU_IDLE_INHIBIT_TIMEOUT));
	g_assert_false(fu_idle_has_inhibit(idle, FU_IDLE_INHIBIT_SIGNALS));

	token = fu_idle_inhibit(idle, FU_IDLE_INHIBIT_TIMEOUT | FU_IDLE_INHIBIT_SIGNALS, NULL);
	g_assert_true(fu_idle_has_inhibit(idle, FU_IDLE_INHIBIT_TIMEOUT));
	g_assert_true(fu_idle_has_inhibit(idle, FU_IDLE_INHIBIT_SIGNALS));

	/* wrong token */
	fu_idle_uninhibit(idle, token + 1);
	g_assert_true(fu_idle_has_inhibit(idle, FU_IDLE_INHIBIT_SIGNALS));

	/* correct token */
	fu_idle_uninhibit(idle, token);
	g_assert_false(fu_idle_has_inhibit(idle, FU_IDLE_INHIBIT_TIMEOUT));
	g_assert_false(fu_idle_has_inhibit(idle, FU_IDLE_INHIBIT_SIGNALS));

	/* locker section */
	{
		g_autoptr(FuIdleLocker) idle_locker1 =
		    fu_idle_locker_new(idle, FU_IDLE_INHIBIT_TIMEOUT, NULL);
		g_autoptr(FuIdleLocker) idle_locker2 =
		    fu_idle_locker_new(idle, FU_IDLE_INHIBIT_SIGNALS, NULL);
		g_assert_nonnull(idle_locker1);
		g_assert_nonnull(idle_locker2);
		g_assert_true(fu_idle_has_inhibit(idle, FU_IDLE_INHIBIT_TIMEOUT));
		g_assert_true(fu_idle_has_inhibit(idle, FU_IDLE_INHIBIT_SIGNALS));
	}
	g_assert_false(fu_idle_has_inhibit(idle, FU_IDLE_INHIBIT_TIMEOUT));
	g_assert_false(fu_idle_has_inhibit(idle, FU_IDLE_INHIBIT_SIGNALS));
}

static void
fu_engine_generate_md_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	const gchar *tmp;
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;

	/* put cab file somewhere we can parse it */
	filename =
	    g_test_build_filename(G_TEST_DIST, "tests", "colorhug", "colorhug-als-3.0.2.cab", NULL);
	data = fu_bytes_get_contents(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);
	ret = fu_bytes_set_contents("/tmp/fwupd-self-test/var/cache/fwupd/foo.cab", data, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* load engine and check the device was found */
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_REMOTES | FU_ENGINE_LOAD_FLAG_NO_CACHE,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_device_add_guid(device, "12345678-1234-1234-1234-123456789012");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.3");
	component = fu_engine_get_component_by_guids(engine, device);
	g_assert_nonnull(component);

	/* check remote ID set */
	tmp = xb_node_query_text(component, "../custom/value[@key='fwupd::RemoteId']", NULL);
	g_assert_cmpstr(tmp, ==, "directory");

	/* verify checksums */
	tmp = xb_node_query_text(component, "releases/release/checksum[@target='container']", NULL);
	g_assert_cmpstr(tmp, ==, "3da49ddd961144a79336b3ac3b0e469cb2531d0e");
	tmp = xb_node_query_text(component, "releases/release/checksum[@target='content']", NULL);
	g_assert_cmpstr(tmp, ==, NULL);
}

static void
fu_engine_requirements_missing_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	const gchar *xml = "<component>"
			   "  <requires>"
			   "    <id compare=\"ge\" version=\"1.2.3\">not.going.to.exist</id>"
			   "  </requires>"
			   "  <releases>"
			   "    <release version=\"1.2.3\"/>"
			   "  </releases>"
			   "</component>";

	/* set up a dummy version */
	fu_engine_add_runtime_version(engine, "org.test.dummy", "1.2.3");

	/* make the component require one thing */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this fails */
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);
}

static void
fu_engine_requirements_soft_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	const gchar *xml = "<component>"
			   "  <suggests>"
			   "    <id compare=\"ge\" version=\"1.2.3\">not.going.to.exist</id>"
			   "  </suggests>"
			   "  <releases>"
			   "    <release version=\"1.2.3\"/>"
			   "  </releases>"
			   "</component>";

	/* set up a dummy version */
	fu_engine_add_runtime_version(engine, "org.test.dummy", "1.2.3");

	/* make the component require one thing */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine,
					   release,
					   FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS,
					   &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_requirements_client_fail_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	const gchar *xml = "<component>"
			   "  <requires>"
			   "    <client>detach-action</client>"
			   "  </requires>"
			   "  <releases>"
			   "    <release version=\"1.2.3\"/>"
			   "  </releases>"
			   "</component>";

	/* make the component require one thing */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this fails */
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
}

static void
fu_engine_requirements_client_invalid_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	const gchar *xml = "<component>"
			   "  <requires>"
			   "    <client>hello-dave</client>"
			   "    <id compare=\"ge\" version=\"1.4.5\">org.freedesktop.fwupd</id>\n"
			   "  </requires>"
			   "  <releases>"
			   "    <release version=\"1.2.3\"/>"
			   "  </releases>"
			   "</component>";

	/* make the component require one thing */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this fails */
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);
}

static void
fu_engine_requirements_client_pass_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	const gchar *xml = "<component>"
			   "  <requires>"
			   "    <client>detach-action</client>"
			   "    <id compare=\"ge\" version=\"1.4.5\">org.freedesktop.fwupd</id>\n"
			   "  </requires>"
			   "  <releases>"
			   "    <release version=\"1.2.3\"/>"
			   "  </releases>"
			   "</component>";

	/* set up a dummy version */
	fu_engine_request_set_feature_flags(request, FWUPD_FEATURE_FLAG_DETACH_ACTION);

	/* make the component require one thing */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_requirements_not_hardware_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	const gchar *xml = "<component>"
			   "  <requires>"
			   "    <not_hardware>ffffffff-ffff-ffff-ffff-ffffffffffff</not_hardware>"
			   "    <id compare=\"ge\" version=\"1.9.10\">org.freedesktop.fwupd</id>\n"
			   "  </requires>"
			   "  <releases>"
			   "    <release version=\"1.2.3\"/>"
			   "  </releases>"
			   "</component>";

	/* set up a dummy version */
	fu_engine_request_set_feature_flags(request, FWUPD_FEATURE_FLAG_DETACH_ACTION);

	/* make the component require one thing */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_requirements_version_require_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml =
	    "<component>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"1.2.4\">"
	    "    </release>"
	    "  </releases>"
	    "</component>";

	/* set up a dummy device */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.3");
	fu_device_set_version_bootloader(device, "4.5.6");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_VERSION_CHECK_REQUIRED);
	fu_device_add_guid(device, "12345678-1234-1234-1234-123456789012");

	/* make the component require one thing */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this fails */
	fu_release_set_device(release, device);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_true(
	    g_str_has_prefix(error->message, "device requires firmware with a version check"));
	g_assert_false(ret);
}

static void
fu_engine_requirements_version_lowest_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml =
	    "<component>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"1.2.2\">"
	    "    </release>"
	    "  </releases>"
	    "</component>";

	/* set up a dummy device */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.3");
	fu_device_set_version_lowest(device, "1.2.3");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_guid(device, "12345678-1234-1234-1234-123456789012");

	/* make the component require one thing */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this fails */
	fu_release_set_device(release, device);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_true(
	    g_str_has_prefix(error->message, "Specified firmware is older than the minimum"));
	g_assert_false(ret);
}

static void
fu_engine_requirements_unsupported_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	const gchar *xml = "<component>"
			   "  <requires>"
			   "    <UNKNOWN compare=\"ge\" version=\"2.6.0\"/>"
			   "  </requires>"
			   "  <releases>"
			   "    <release version=\"1.2.3\"/>"
			   "  </releases>"
			   "</component>";

	/* set up a dummy version */
	fu_engine_add_runtime_version(engine, "org.test.dummy", "1.2.3");

	/* make the component require one thing that we don't support */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this fails */
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
}

static void
fu_engine_requirements_child_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuDevice) child = fu_device_new(NULL);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml =
	    "<component>"
	    "  <requires>"
	    "    <firmware compare=\"eq\" version=\"0.0.1\">not-child</firmware>"
	    "    <id compare=\"ge\" version=\"1.2.11\">org.freedesktop.fwupd</id>\n"
	    "  </requires>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"1.2.4\">"
	    "      <checksum type=\"sha1\" filename=\"bios.bin\" target=\"content\"/>"
	    "    </release>"
	    "  </releases>"
	    "</component>";

	/* set up a dummy device */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.3");
	fu_device_set_version_bootloader(device, "4.5.6");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_guid(device, "12345678-1234-1234-1234-123456789012");
	fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(child, "0.0.999");
	fu_device_set_physical_id(child, "dummy");
	fu_device_add_child(device, child);

	/* make the component require three things */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_device(release, device);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_requirements_child_fail_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuDevice) child = fu_device_new(NULL);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml =
	    "<component>"
	    "  <requires>"
	    "    <firmware compare=\"glob\" version=\"0.0.*\">not-child</firmware>"
	    "    <id compare=\"ge\" version=\"1.2.11\">org.freedesktop.fwupd</id>\n"
	    "  </requires>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"1.2.4\">"
	    "      <checksum type=\"sha1\" filename=\"bios.bin\" target=\"content\"/>"
	    "    </release>"
	    "  </releases>"
	    "</component>";

	/* set up a dummy device */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.3");
	fu_device_set_version_bootloader(device, "4.5.6");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_guid(device, "12345678-1234-1234-1234-123456789012");
	fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(child, "0.0.1");
	fu_device_set_physical_id(child, "dummy");
	fu_device_add_child(device, child);

	/* make the component require three things */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_device(release, device);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_nonnull(
	    g_strstr_len(error->message, -1, "Not compatible with child device version"));
	g_assert_false(ret);
}

static void
fu_engine_requirements_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml = "<component>"
			   "  <requires>"
			   "    <id compare=\"ge\" version=\"1.2.3\">org.test.dummy</id>"
			   "    <hardware>6ff95c9c-ae41-5f59-9d90-3ec1ea66091e</hardware>"
			   "    <id compare=\"ge\" version=\"1.0.1\">org.freedesktop.fwupd</id>\n"
			   "    <id compare=\"ge\" version=\"1.9.10\">org.freedesktop.fwupd</id>\n"
			   "  </requires>"
			   "  <releases>"
			   "    <release version=\"1.2.3\"/>"
			   "  </releases>"
			   "</component>";

	/* set up some dummy versions */
	fu_engine_add_runtime_version(engine, "org.test.dummy", "1.2.3");
	fu_engine_add_runtime_version(engine, "com.hughski.colorhug", "7.8.9");

	/* make the component require one thing */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_requirements_device_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml =
	    "<component>"
	    "  <requires>"
	    "    <firmware compare=\"ge\" version=\"1.2.3\"/>"
	    "    <firmware compare=\"eq\" version=\"4.5.6\">bootloader</firmware>"
	    "    <firmware compare=\"regex\" version=\"USB:0xFFFF|DMI:Lenovo\">vendor-id</firmware>"
#ifdef __linux__
	    "    <id compare=\"ge\" version=\"4.0.0\">org.kernel</id>"
#endif
	    "    <id compare=\"ge\" version=\"1.2.11\">org.freedesktop.fwupd</id>\n"
	    "  </requires>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"1.2.4\">"
	    "      <checksum type=\"sha1\" filename=\"bios.bin\" target=\"content\"/>"
	    "    </release>"
	    "  </releases>"
	    "</component>";

	/* set up a dummy device */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.3");
	fu_device_set_version_bootloader(device, "4.5.6");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_build_vendor_id_u16(device, "PCI", 0x0000);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_VERSION_CHECK_REQUIRED);
	fu_device_add_guid(device, "12345678-1234-1234-1234-123456789012");

	/* make the component require three things */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_device(release, device);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check this fails, as the wrong requirement is specified */
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_ENFORCE_REQUIRES);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_nonnull(g_strstr_len(error->message, -1, "child, parent or sibling requirement"));
	g_assert_false(ret);

#ifndef SUPPORTED_BUILD
	/* we can force this */
	g_clear_error(&error);
	ret = fu_engine_requirements_check(engine,
					   release,
					   FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS,
					   &error);
	g_assert_no_error(error);
	g_assert_true(ret);
#endif
}

static void
fu_engine_requirements_device_plain_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml =
	    "<component>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"51H0AALB\">"
	    "      <checksum type=\"sha1\" filename=\"bios.bin\" target=\"content\"/>"
	    "    </release>"
	    "  </releases>"
	    "</component>";

	/* set up a dummy device */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_version(device, "5101AALB");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_guid(device, "12345678-1234-1234-1234-123456789012");

	/* make the component require three things */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_device(release, device);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_requirements_version_format_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml =
	    "<component>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"1.2.4\">"
	    "      <checksum type=\"sha1\" filename=\"bios.bin\" target=\"content\"/>"
	    "    </release>"
	    "  </releases>"
	    "  <custom>"
	    "    <value key=\"LVFS::VersionFormat\">triplet</value>"
	    "  </custom>"
	    "</component>";

	/* set up a dummy device */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_version(device, "1.2.3.4");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_guid(device, "12345678-1234-1234-1234-123456789012");

	/* make the component require three things */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this fails */
	fu_release_set_device(release, device);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_nonnull(
	    g_strstr_len(error->message, -1, "Firmware version formats were different"));
	g_assert_false(ret);
}

static void
fu_engine_requirements_only_upgrade_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml =
	    "<component>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"1.2.3\"/>"
	    "  </releases>"
	    "</component>";

	/* set up a dummy device */
	fu_device_set_version(device, "1.2.4");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_ONLY_VERSION_UPGRADE);
	fu_device_add_guid(device, "12345678-1234-1234-1234-123456789012");

	/* make the component require three things */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this fails */
	fu_release_set_device(release, device);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_nonnull(g_strstr_len(error->message, -1, "Device only supports version upgrades"));
	g_assert_false(ret);
}

static void
fu_engine_requirements_sibling_device_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device1 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) unrelated_device3 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) parent = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release1 = fu_release_new();
	g_autoptr(FuRelease) release2 = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();
	const gchar *xml =
	    "<component>"
	    "  <requires>"
	    "    <firmware depth=\"0\">1ff60ab2-3905-06a1-b476-0371f00c9e9b</firmware>"
	    "    <id compare=\"ge\" version=\"1.6.1\">org.freedesktop.fwupd</id>\n"
	    "  </requires>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"1.2.4\">"
	    "      <checksum type=\"sha1\" filename=\"bios.bin\" target=\"content\"/>"
	    "    </release>"
	    "  </releases>"
	    "</component>";

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up a dummy device */
	fu_device_set_id(device1, "id1");
	fu_device_set_version_format(device1, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device1, "1.2.3");
	fu_device_build_vendor_id_u16(device1, "USB", 0xFFFF);
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_guid(device1, "12345678-1234-1234-1234-123456789012");
	fu_device_add_protocol(device1, "com.acme");
	fu_engine_add_device(engine, device1);

	/* setup the parent */
	fu_device_set_id(parent, "parent");
	fu_device_set_version_format(parent, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(parent, "1.0.0");
	fu_device_add_flag(parent, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(parent, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_guid(parent, "42f3d696-0b6f-4d69-908f-357f98ef115e");
	fu_device_add_protocol(parent, "com.acme");
	fu_device_add_child(parent, device1);
	fu_engine_add_device(engine, parent);

	/* set up a different device */
	fu_device_set_id(unrelated_device3, "id3");
	fu_device_build_vendor_id(unrelated_device3, "USB", "FFFF");
	fu_device_add_protocol(unrelated_device3, "com.acme");
	fu_device_set_name(unrelated_device3, "Foo bar device");
	fu_device_set_version_format(unrelated_device3, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(unrelated_device3, "1.5.3");
	fu_device_add_flag(unrelated_device3, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(unrelated_device3, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_guid(unrelated_device3, "3e455c08-352e-4a16-84d3-f04287289fa2");
	fu_engine_add_device(engine, unrelated_device3);

	/* import firmware metainfo */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this fails */
	fu_release_set_device(release1, device1);
	fu_release_set_request(release1, request);
	ret = fu_release_load(release1, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release1, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);

	/* set up a sibling device */
	fu_device_set_id(device2, "id2");
	fu_device_build_vendor_id_u16(device2, "USB", 0xFFFF);
	fu_device_add_protocol(device2, "com.acme");
	fu_device_set_name(device2, "Secondary firmware");
	fu_device_set_version_format(device2, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device2, "4.5.6");
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_guid(device2, "1ff60ab2-3905-06a1-b476-0371f00c9e9b");
	fu_device_add_child(parent, device2);
	fu_engine_add_device(engine, device2);

	/* check this passes */
	fu_release_set_device(release2, device1);
	fu_release_set_request(release2, request);
	ret = fu_release_load(release2, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release2, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check this still works, as a child requirement is specified */
	fu_device_add_private_flag(device1, FU_DEVICE_PRIVATE_FLAG_ENFORCE_REQUIRES);
	ret = fu_engine_requirements_check(engine, release2, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_requirements_other_device_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device1 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();
	const gchar *xml =
	    "<component>"
	    "  <requires>"
	    "    <firmware compare=\"gt\" "
	    "version=\"4.0.0\">1ff60ab2-3905-06a1-b476-0371f00c9e9b</firmware>"
	    "    <id compare=\"ge\" version=\"1.2.11\">org.freedesktop.fwupd</id>\n"
	    "  </requires>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"1.2.4\">"
	    "      <checksum type=\"sha1\" filename=\"bios.bin\" target=\"content\"/>"
	    "    </release>"
	    "  </releases>"
	    "</component>";

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up a dummy device */
	fu_device_set_version_format(device1, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device1, "1.2.3");
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_guid(device1, "12345678-1234-1234-1234-123456789012");

	/* set up a different device */
	fu_device_set_id(device2, "id2");
	fu_device_build_vendor_id_u16(device2, "USB", 0xFFFF);
	fu_device_add_protocol(device2, "com.acme");
	fu_device_set_name(device2, "Secondary firmware");
	fu_device_set_version_format(device2, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device2, "4.5.6");
	fu_device_add_guid(device2, "1ff60ab2-3905-06a1-b476-0371f00c9e9b");
	fu_engine_add_device(engine, device2);

	/* import firmware metainfo */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_device(release, device1);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_requirements_protocol_check_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	g_autoptr(FuDevice) device1 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(FuRelease) release1 = fu_release_new();
	g_autoptr(FuRelease) release2 = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();
	gboolean ret;

	const gchar *xml =
	    "<component>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"4.5.7\">"
	    "      <checksum type=\"sha1\" filename=\"bios.bin\" target=\"content\"/>"
	    "    </release>"
	    "  </releases>"
	    "  <custom>"
	    "    <value key=\"LVFS::UpdateProtocol\">org.bar</value>"
	    "  </custom>"

	    "</component>";

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	fu_device_set_id(device1, "NVME");
	fu_device_add_protocol(device1, "com.acme");
	fu_device_set_name(device1, "NVME device");
	fu_device_build_vendor_id(device1, "DMI", "ACME");
	fu_device_set_version_format(device1, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device1, "1.2.3");
	fu_device_add_guid(device1, "12345678-1234-1234-1234-123456789012");
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_engine_add_device(engine, device1);

	fu_device_set_id(device2, "UEFI");
	fu_device_add_protocol(device2, "org.bar");
	fu_device_set_name(device2, "UEFI device");
	fu_device_build_vendor_id(device2, "DMI", "ACME");
	fu_device_set_version_format(device2, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device2, "1.2.3");
	fu_device_add_guid(device2, "12345678-1234-1234-1234-123456789012");
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_engine_add_device(engine, device2);

	/* make sure both devices added */
	devices = fu_engine_get_devices(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 2);

	/* import firmware metainfo */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this fails */
	fu_release_set_device(release1, device1);
	fu_release_set_request(release1, request);
	ret = fu_release_load(release1, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);

	/* check this passes */
	fu_release_set_device(release2, device2);
	fu_release_set_request(release2, request);
	ret = fu_release_load(release2, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_requirements_parent_device_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device1 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();
	const gchar *xml =
	    "<component>"
	    "  <requires>"
	    "    <firmware depth=\"1\" compare=\"eq\" version=\"1.2.3\"/>"
	    "    <firmware depth=\"1\">12345678-1234-1234-1234-123456789012</firmware>"
	    "    <id compare=\"ge\" version=\"1.3.4\">org.freedesktop.fwupd</id>\n"
	    "  </requires>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">1ff60ab2-3905-06a1-b476-0371f00c9e9b</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"4.5.7\">"
	    "      <checksum type=\"sha1\" filename=\"bios.bin\" target=\"content\"/>"
	    "    </release>"
	    "  </releases>"
	    "</component>";

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up child device */
	fu_device_set_id(device2, "child");
	fu_device_set_name(device2, "child");
	fu_device_set_version_format(device2, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device2, "4.5.6");
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_guid(device2, "1ff60ab2-3905-06a1-b476-0371f00c9e9b");

	/* set up a parent device */
	fu_device_set_id(device1, "parent");
	fu_device_build_vendor_id_u16(device1, "USB", 0xFFFF);
	fu_device_add_protocol(device1, "com.acme");
	fu_device_set_name(device1, "parent");
	fu_device_set_version_format(device1, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device1, "1.2.3");
	fu_device_add_guid(device1, "12345678-1234-1234-1234-123456789012");
	fu_device_add_child(device1, device2);
	fu_engine_add_device(engine, device1);

	/* import firmware metainfo */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_device(release, device2);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_requirements_child_device_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device1 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();
	const gchar *xml =
	    "<component>"
	    "  <requires>"
	    "    <firmware depth=\"-1\">1ff60ab2-3905-06a1-b476-0371f00c9e9b</firmware>"
	    "    <id compare=\"ge\" version=\"1.9.7\">org.freedesktop.fwupd</id>\n"
	    "  </requires>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"4.5.7\">"
	    "      <checksum type=\"sha1\" filename=\"bios.bin\" target=\"content\"/>"
	    "    </release>"
	    "  </releases>"
	    "</component>";

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up a parent device */
	fu_device_set_id(device1, "parent");
	fu_device_build_vendor_id_u16(device1, "USB", 0xFFFF);
	fu_device_add_protocol(device1, "com.acme");
	fu_device_set_name(device1, "parent");
	fu_device_set_version_format(device1, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device1, "1.2.3");
	fu_device_add_guid(device1, "12345678-1234-1234-1234-123456789012");
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);

	/* set up child device */
	fu_device_set_id(device2, "child");
	fu_device_set_name(device2, "child");
	fu_device_set_version_format(device2, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device2, "4.5.6");
	fu_device_add_guid(device2, "1ff60ab2-3905-06a1-b476-0371f00c9e9b");
	fu_device_add_child(device1, device2);

	fu_engine_add_device(engine, device1);

	/* import firmware metainfo */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_device(release, device1);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_device_parent_guid_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	g_autoptr(FuDevice) device1 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device3 = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* add child */
	fu_device_set_id(device1, "child");
	fu_device_build_vendor_id_u16(device2, "USB", 0xFFFF);
	fu_device_add_protocol(device2, "com.acme");
	fu_device_add_instance_id(device1, "child-GUID-1");
	fu_device_add_parent_guid(device1, "parent-GUID");
	fu_device_convert_instance_ids(device1);
	fu_engine_add_device(engine, device1);

	/* parent */
	fu_device_set_id(device2, "parent");
	fu_device_build_vendor_id_u16(device2, "USB", 0xFFFF);
	fu_device_add_protocol(device2, "com.acme");
	fu_device_add_instance_id(device2, "parent-GUID");
	fu_device_set_vendor(device2, "oem");
	fu_device_convert_instance_ids(device2);

	/* add another child */
	fu_device_set_id(device3, "child2");
	fu_device_add_instance_id(device3, "child-GUID-2");
	fu_device_add_parent_guid(device3, "parent-GUID");
	fu_device_convert_instance_ids(device3);
	fu_device_add_child(device2, device3);

	/* add two together */
	fu_engine_add_device(engine, device2);

	/* this is normally done by fu_plugin_device_add() */
	fu_engine_add_device(engine, device3);

	/* verify both children were adopted */
	g_assert_true(fu_device_get_parent(device3) == device2);
	g_assert_true(fu_device_get_parent(device1) == device2);
	g_assert_cmpstr(fu_device_get_vendor(device3), ==, "oem");

	/* verify order */
	g_assert_cmpint(fu_device_get_order(device1), ==, -1);
	g_assert_cmpint(fu_device_get_order(device2), ==, 0);
	g_assert_cmpint(fu_device_get_order(device3), ==, -1);
}

static void
fu_engine_device_parent_id_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	g_autoptr(FuDevice) device1 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device3 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device4 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device5 = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* add child */
	fu_device_set_id(device1, "child1");
	fu_device_set_name(device1, "Child1");
	fu_device_set_physical_id(device1, "child-ID1");
	fu_device_build_vendor_id_u16(device1, "USB", 0xFFFF);
	fu_device_add_protocol(device1, "com.acme");
	fu_device_add_instance_id(device1, "child-GUID-1");
	fu_device_add_parent_physical_id(device1, "parent-ID-notfound");
	fu_device_add_parent_physical_id(device1, "parent-ID");
	fu_device_convert_instance_ids(device1);
	fu_engine_add_device(engine, device1);

	/* parent */
	fu_device_set_id(device2, "parent");
	fu_device_set_name(device2, "Parent");
	fu_device_set_backend_id(device2, "/sys/devices/foo/bar/baz");
	fu_device_set_physical_id(device2, "parent-ID");
	fu_device_build_vendor_id_u16(device2, "USB", 0xFFFF);
	fu_device_add_protocol(device2, "com.acme");
	fu_device_add_instance_id(device2, "parent-GUID");
	fu_device_set_vendor(device2, "oem");
	fu_device_add_private_flag(device2, FU_DEVICE_PRIVATE_FLAG_AUTO_PARENT_CHILDREN);
	fu_device_convert_instance_ids(device2);

	/* add another child */
	fu_device_set_id(device3, "child2");
	fu_device_set_name(device3, "Child2");
	fu_device_set_physical_id(device3, "child-ID2");
	fu_device_add_instance_id(device3, "child-GUID-2");
	fu_device_add_parent_physical_id(device3, "parent-ID");
	fu_device_convert_instance_ids(device3);
	fu_device_add_child(device2, device3);

	/* add two together */
	fu_engine_add_device(engine, device2);

	/* add non-child */
	fu_device_set_id(device4, "child4");
	fu_device_set_name(device4, "Child4");
	fu_device_set_physical_id(device4, "child-ID4");
	fu_device_build_vendor_id(device4, "USB", "FFFF");
	fu_device_add_protocol(device4, "com.acme");
	fu_device_add_instance_id(device4, "child-GUID-4");
	fu_device_add_parent_physical_id(device4, "parent-ID");
	fu_device_convert_instance_ids(device4);
	fu_engine_add_device(engine, device4);

	/* this is normally done by fu_plugin_device_add() */
	fu_engine_add_device(engine, device4);

	/* add child with the parent backend ID */
	fu_device_set_id(device5, "child5");
	fu_device_set_name(device5, "Child5");
	fu_device_set_physical_id(device5, "child-ID5");
	fu_device_build_vendor_id(device5, "USB", "FFFF");
	fu_device_add_protocol(device5, "com.acme");
	fu_device_add_instance_id(device5, "child-GUID-5");
	fu_device_add_parent_backend_id(device5, "/sys/devices/foo/bar/baz");
	fu_device_convert_instance_ids(device5);
	fu_engine_add_device(engine, device5);

	/* this is normally done by fu_plugin_device_add() */
	fu_engine_add_device(engine, device5);

	/* verify both children were adopted */
	g_assert_true(fu_device_get_parent(device3) == device2);
	g_assert_true(fu_device_get_parent(device4) == device2);
	g_assert_true(fu_device_get_parent(device5) == device2);
	g_assert_true(fu_device_get_parent(device1) == device2);
	g_assert_cmpstr(fu_device_get_vendor(device3), ==, "oem");
}

static void
fu_engine_partial_hash_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device1 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuPlugin) plugin = fu_plugin_new(NULL);
	g_autoptr(GError) error = NULL;
	g_autoptr(GError) error_none = NULL;
	g_autoptr(GError) error_both = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up dummy plugin */
	fu_plugin_set_name(plugin, "test");
	fu_engine_add_plugin(engine, plugin);

	/* add two dummy devices */
	fu_device_set_id(device1, "device1");
	fu_device_build_vendor_id_u16(device1, "USB", 0xFFFF);
	fu_device_add_protocol(device1, "com.acme");
	fu_device_set_plugin(device1, "test");
	fu_device_add_guid(device1, "12345678-1234-1234-1234-123456789012");
	fu_engine_add_device(engine, device1);
	fu_device_set_id(device2, "device21");
	fu_device_build_vendor_id_u16(device2, "USB", 0xFFFF);
	fu_device_add_protocol(device2, "com.acme");
	fu_device_set_plugin(device2, "test");
	fu_device_set_equivalent_id(device2, "b92f5b7560b84ca005a79f5a15de3c003ce494cf");
	fu_device_add_guid(device2, "87654321-1234-1234-1234-123456789012");
	fu_engine_add_device(engine, device2);

	/* match nothing */
	ret = fu_engine_unlock(engine, "deadbeef", &error_none);
	g_assert_error(error_none, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);

	/* match both */
	ret = fu_engine_unlock(engine, "9", &error_both);
	g_assert_error(error_both, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);

	/* match one exactly */
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_LOCKED);
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_LOCKED);
	ret = fu_engine_unlock(engine, "934b4162a6daa0b033d649c8d464529cec41d3de", &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* match one partially */
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_LOCKED);
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_LOCKED);
	ret = fu_engine_unlock(engine, "934b", &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* match equivalent ID */
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_LOCKED);
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_LOCKED);
	ret = fu_engine_unlock(engine, "b92f", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_device_unlock_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	FwupdRelease *rel;
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbSilo) silo = NULL;

	/* load engine to get FuConfig set up */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add the hardcoded 'fwupd' metadata */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "metadata.xml", NULL);
	file = g_file_new_for_path(filename);
	ret = xb_builder_source_load_file(source, file, XB_BUILDER_SOURCE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	fu_engine_set_silo(engine, silo);

	/* add a dummy device */
	fu_device_set_id(device, "UEFI-dummy-dev0");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_add_guid(device, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_LOCKED);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PLAIN);
	fu_engine_add_device(engine, device);

	/* ensure the metainfo was matched */
	rel = fwupd_device_get_release_default(FWUPD_DEVICE(device));
	g_assert_nonnull(rel);
	g_assert_false(fwupd_release_has_flag(rel, FWUPD_RELEASE_FLAG_TRUSTED_REPORT));
}

static void
fu_engine_device_equivalent_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device1 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device_best = NULL;
	g_autoptr(FuDevice) device_worst = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GError) error = NULL;

	/* load engine to get FuConfig set up */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a wireless (worse) device */
	fu_device_set_id(device1, "99249eb1bd9ef0b6e192b271a8cb6a3090cfec7a");
	fu_device_set_name(device1, "device1");
	fu_device_build_vendor_id_u16(device1, "USB", 0xFFFF);
	fu_device_add_protocol(device1, "com.acme");
	fu_device_add_guid(device1, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_engine_add_device(engine, device1);

	/* add a wired (better) device */
	fu_device_set_id(device2, "1a8d0d9a96ad3e67ba76cf3033623625dc6d6882");
	fu_device_set_name(device2, "device2");
	fu_device_set_equivalent_id(device2, "99249eb1bd9ef0b6e192b271a8cb6a3090cfec7a");
	fu_device_set_priority(device2, 999);
	fu_device_build_vendor_id_u16(device2, "USB", 0xFFFF);
	fu_device_add_protocol(device2, "com.acme");
	fu_device_add_guid(device2, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_engine_add_device(engine, device2);

	/* make sure the daemon chooses the best device */
	devices = fu_engine_get_devices(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 2);
	device_best = fu_engine_get_device(engine, "9924", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device_best);
	g_assert_cmpstr(fu_device_get_id(device_best),
			==,
			"1a8d0d9a96ad3e67ba76cf3033623625dc6d6882");
	g_assert_true(fu_device_has_flag(device_best, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_problem(device_best, FWUPD_DEVICE_PROBLEM_LOWER_PRIORITY));

	/* get the worst device and make sure it's not updatable */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device_tmp = g_ptr_array_index(devices, i);
		if (device_tmp != device_best) {
			device_worst = g_object_ref(device_tmp);
			break;
		}
	}
	g_assert_nonnull(device_worst);
	g_assert_false(fu_device_has_flag(device_worst, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_true(fu_device_has_problem(device_worst, FWUPD_DEVICE_PROBLEM_LOWER_PRIORITY));
}

static void
fu_engine_device_md_set_flags_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml =
	    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	    "<components version=\"0.9\">\n"
	    "  <component type=\"firmware\">\n"
	    "    <id>org.fwupd.8330a096d9f1af8567c7374cb8403e1ce9cf3163.device</id>\n"
	    "    <provides>\n"
	    "      <firmware type=\"flashed\">2d47f29b-83a2-4f31-a2e8-63474f4d4c2e</firmware>\n"
	    "    </provides>\n"
	    "    <releases>\n"
	    "      <release version=\"1\" />\n"
	    "    </releases>\n"
	    "    <custom>\n"
	    "      <value key=\"LVFS::DeviceFlags\">save-into-backup-remote</value>\n"
	    "    </custom>\n"
	    "  </component>\n"
	    "</components>\n";

	/* load engine to get FuConfig set up */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add the XML metadata */
	ret = xb_builder_source_load_xml(source, xml, XB_BUILDER_SOURCE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	fu_engine_set_silo(engine, silo);

	/* add a dummy device */
	fu_device_set_id(device, "UEFI-dummy-dev0");
	fu_device_set_version(device, "0");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_add_guid(device, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_FLAGS);
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PLAIN);
	fu_engine_add_device(engine, device);

	/* check the flag got set */
	g_assert_true(
	    fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_SAVE_INTO_BACKUP_REMOTE));
}

static void
fu_engine_require_hwid_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuCabinet) cabinet = NULL;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* load engine to get FuConfig set up */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* get generated file as a blob */
	filename =
	    g_test_build_filename(G_TEST_BUILT, "tests", "missing-hwid", "hwid-1.2.3.cab", NULL);
	stream = fu_input_stream_from_path(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	cabinet = fu_engine_build_cabinet_from_stream(engine, stream, &error);
	g_assert_no_error(error);
	g_assert_nonnull(cabinet);

	/* add a dummy device */
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.2");
	fu_device_add_guid(device, "12345678-1234-1234-1234-123456789012");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_engine_add_device(engine, device);

	/* get component */
	component = fu_cabinet_get_component(cabinet, "com.hughski.test.firmware", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check requirements */
	fu_release_set_device(release, device);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_nonnull(error);
	g_assert_cmpstr(error->message,
			==,
			"no HWIDs matched 9342d47a-1bab-5709-9869-c840b2eac501");
	g_assert_false(ret);
}

static void
fu_engine_get_details_added_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	FuDevice *device_tmp;
	FwupdRelease *release;
	gboolean ret;
	g_autofree gchar *checksum_sha256 = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* load engine to get FuConfig set up */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a dummy device */
	fu_device_set_id(device, "test_device");
	fu_device_set_name(device, "test device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.2");
	fu_device_add_guid(device, "12345678-1234-1234-1234-123456789012");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_engine_add_device(engine, device);

	/* get details */
	filename =
	    g_test_build_filename(G_TEST_BUILT, "tests", "missing-hwid", "hwid-1.2.3.cab", NULL);
	stream = fu_input_stream_from_path(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	checksum_sha256 = fu_input_stream_compute_checksum(stream, G_CHECKSUM_SHA256, &error);
	g_assert_no_error(error);
	g_assert_nonnull(checksum_sha256);
	devices = fu_engine_get_details(engine, request, stream, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	device_tmp = g_ptr_array_index(devices, 0);
	g_assert_cmpstr(fu_device_get_name(device_tmp), ==, "test device");
	release = fu_device_get_release_default(device_tmp);
	g_assert_nonnull(release);
	g_assert_cmpstr(fwupd_release_get_version(release), ==, "1.2.3");
	g_assert_true(fwupd_release_has_checksum(release, checksum_sha256));
}

static void
fu_engine_get_details_missing_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	FuDevice *device_tmp;
	FwupdRelease *release;
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* load engine to get FuConfig set up */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* get details */
	filename =
	    g_test_build_filename(G_TEST_BUILT, "tests", "missing-hwid", "hwid-1.2.3.cab", NULL);
	stream = fu_input_stream_from_path(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	devices = fu_engine_get_details(engine, request, stream, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	device_tmp = g_ptr_array_index(devices, 0);
	g_assert_cmpstr(fu_device_get_name(device_tmp), ==, NULL);
	release = fu_device_get_release_default(device_tmp);
	g_assert_nonnull(release);
	g_assert_cmpstr(fwupd_release_get_version(release), ==, "1.2.3");
}

static void
fu_engine_downgrade_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	FwupdRelease *rel;
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) devices_pre = NULL;
	g_autoptr(GPtrArray) releases_dg = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(GPtrArray) releases_up = NULL;
	g_autoptr(GPtrArray) releases_up2 = NULL;
	g_autoptr(GPtrArray) remotes = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* ensure empty tree */
	fu_self_test_mkroot();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* write a broken file */
	ret = g_file_set_contents("/tmp/fwupd-self-test/broken.xml.gz",
				  "this is not a valid",
				  -1,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* write the main file */
	ret = g_file_set_contents(
	    "/tmp/fwupd-self-test/stable.xml",
	    "<components>"
	    "  <component type=\"firmware\">"
	    "    <id>test</id>"
	    "    <name>Test Device</name>"
	    "    <provides>"
	    "      <firmware type=\"flashed\">aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee</firmware>"
	    "    </provides>"
	    "    <releases>"
	    "      <release version=\"1.2.3\" date=\"2017-09-15\">"
	    "        <size type=\"installed\">123</size>"
	    "        <size type=\"download\">456</size>"
	    "        <location>https://test.org/foo.cab</location>"
	    "        <checksum filename=\"foo.cab\" target=\"container\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdead1111</checksum>"
	    "        <checksum filename=\"firmware.bin\" target=\"content\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
	    "      </release>"
	    "      <release version=\"1.2.2\" date=\"2017-09-01\">"
	    "        <size type=\"installed\">123</size>"
	    "        <size type=\"download\">456</size>"
	    "        <location>https://test.org/foo.cab</location>"
	    "        <checksum filename=\"foo.cab\" target=\"container\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdead2222</checksum>"
	    "        <checksum filename=\"firmware.bin\" target=\"content\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
	    "      </release>"
	    "    </releases>"
	    "  </component>"
	    "</components>",
	    -1,
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* write the extra file */
	ret = g_file_set_contents(
	    "/tmp/fwupd-self-test/testing.xml",
	    "<components>"
	    "  <component type=\"firmware\">"
	    "    <id>test</id>"
	    "    <name>Test Device</name>"
	    "    <provides>"
	    "      <firmware type=\"flashed\">aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee</firmware>"
	    "    </provides>"
	    "    <releases>"
	    "      <release version=\"1.2.5\" date=\"2017-09-16\">"
	    "        <size type=\"installed\">123</size>"
	    "        <size type=\"download\">456</size>"
	    "        <location>https://test.org/foo.cab</location>"
	    "        <checksum filename=\"foo.cab\" target=\"container\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdead3333</checksum>"
	    "        <checksum filename=\"firmware.bin\" target=\"content\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
	    "      </release>"
	    "      <release version=\"1.2.4\" date=\"2017-09-15\">"
	    "        <size type=\"installed\">123</size>"
	    "        <size type=\"download\">456</size>"
	    "        <location>https://test.org/foo.cab</location>"
	    "        <checksum filename=\"foo.cab\" target=\"container\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdead4444</checksum>"
	    "        <checksum filename=\"firmware.bin\" target=\"content\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
	    "      </release>"
	    "    </releases>"
	    "  </component>"
	    "</components>",
	    -1,
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_REMOTES | FU_ENGINE_LOAD_FLAG_NO_CACHE,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_test_assert_expected_messages();

	/* return all the remotes, even the broken one */
	remotes = fu_engine_get_remotes(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(remotes);
	g_assert_cmpint(remotes->len, ==, 7);

	/* ensure there are no devices already */
	devices_pre = fu_engine_get_devices(engine, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO);
	g_assert_null(devices_pre);
	g_clear_error(&error);

	/* add a device so we can get upgrades and downgrades */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.3");
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_name(device, "Test Device");
	fu_device_add_guid(device, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_engine_add_device(engine, device);
	devices = fu_engine_get_devices(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_SUPPORTED));
	g_assert_true(fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_REGISTERED));

	/* get the releases for one device */
	releases = fu_engine_get_releases(engine, request, fu_device_get_id(device), &error);
	g_assert_no_error(error);
	g_assert_nonnull(releases);
	g_assert_cmpint(releases->len, ==, 4);

	/* no upgrades, as no firmware is approved */
	releases_up = fu_engine_get_upgrades(engine, request, fu_device_get_id(device), &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO);
	g_assert_null(releases_up);
	g_clear_error(&error);

	/* retry with approved firmware set */
	fu_engine_add_approved_firmware(engine, "deadbeefdeadbeefdeadbeefdead1111");
	fu_engine_add_approved_firmware(engine, "deadbeefdeadbeefdeadbeefdead2222");
	fu_engine_add_approved_firmware(engine, "deadbeefdeadbeefdeadbeefdead3333");
	fu_engine_add_approved_firmware(engine, "deadbeefdeadbeefdeadbeefdead4444");
	fu_engine_add_approved_firmware(engine, "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX");

	/* upgrades */
	releases_up = fu_engine_get_upgrades(engine, request, fu_device_get_id(device), &error);
	g_assert_no_error(error);
	g_assert_nonnull(releases_up);
	g_assert_cmpint(releases_up->len, ==, 2);

	/* ensure the list is sorted */
	rel = FWUPD_RELEASE(g_ptr_array_index(releases_up, 0));
	g_assert_cmpstr(fwupd_release_get_version(rel), ==, "1.2.5");
	rel = FWUPD_RELEASE(g_ptr_array_index(releases_up, 1));
	g_assert_cmpstr(fwupd_release_get_version(rel), ==, "1.2.4");

	/* downgrades */
	releases_dg = fu_engine_get_downgrades(engine, request, fu_device_get_id(device), &error);
	g_assert_no_error(error);
	g_assert_nonnull(releases_dg);
	g_assert_cmpint(releases_dg->len, ==, 1);
	rel = FWUPD_RELEASE(g_ptr_array_index(releases_dg, 0));
	g_assert_cmpstr(fwupd_release_get_version(rel), ==, "1.2.2");

	/* enforce that updates have to be explicit */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_ONLY_EXPLICIT_UPDATES);
	releases_up2 = fu_engine_get_upgrades(engine, request, fu_device_get_id(device), &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO);
	g_assert_null(releases_up2);
}

static void
fu_engine_md_verfmt_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	FwupdRemote *remote;
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* ensure empty tree */
	fu_self_test_mkroot();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* write the main file */
	ret = g_file_set_contents(
	    "/tmp/fwupd-self-test/stable.xml",
	    "<components>"
	    "  <component type=\"firmware\">"
	    "    <id>test</id>"
	    "    <name>Test Device</name>"
	    "    <icon>computer</icon>"
	    "    <developer_name>ACME</developer_name>"
	    "    <provides>"
	    "      <firmware type=\"flashed\">aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee</firmware>"
	    "    </provides>"
	    "    <categories>"
	    "      <category>X-GraphicsTablet</category>"
	    "    </categories>"
	    "    <releases>"
	    "      <release version=\"1.2.3\" date=\"2017-09-15\">"
	    "        <size type=\"installed\">123</size>"
	    "        <size type=\"download\">456</size>"
	    "        <location>https://test.org/foo.cab</location>"
	    "        <checksum filename=\"foo.cab\" target=\"container\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
	    "        <checksum filename=\"firmware.bin\" target=\"content\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
	    "      </release>"
	    "    </releases>"
	    "    <custom>"
	    "      <value key=\"LVFS::VersionFormat\">triplet</value>"
	    "      <value key=\"LVFS::DeviceIntegrity\">signed</value>"
	    "      <value key=\"LVFS::DeviceFlags\">host-cpu,needs-shutdown</value>"
	    "    </custom>"
	    "  </component>"
	    "</components>",
	    -1,
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_REMOTES | FU_ENGINE_LOAD_FLAG_NO_CACHE,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_test_assert_expected_messages();

	/* pretend this has a signature */
	remote = fu_engine_get_remote_by_id(engine, "stable", &error);
	g_assert_no_error(error);
	g_assert_nonnull(remote);

	/* add a device with no defined version format */
	fu_device_set_version(device, "16908291");
	fu_device_set_version_raw(device, 0x01020003);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_NAME_CATEGORY);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_ICON);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_VENDOR);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_SIGNED);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_VERFMT);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_FLAGS);
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_add_guid(device, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_engine_add_device(engine, device);

	/* ensure the version format was set from the metadata */
	g_assert_cmpint(fu_device_get_version_format(device), ==, FWUPD_VERSION_FORMAT_TRIPLET);
	g_assert_cmpstr(fu_device_get_version(device), ==, "1.2.3");
	g_assert_cmpstr(fu_device_get_name(device), ==, "Graphics Tablet");
	g_assert_cmpstr(fu_device_get_vendor(device), ==, "ACME");
	g_assert_true(fu_device_has_icon(device, "computer"));
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD));
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN));
	g_assert_true(fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_HOST_CPU));

	/* ensure the device was added */
	devices = fu_engine_get_devices(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_SUPPORTED));
	g_assert_true(fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_REGISTERED));

	/* ensure the releases are set */
	releases = fu_engine_get_releases(engine, request, fu_device_get_id(device), &error);
	g_assert_no_error(error);
	g_assert_nonnull(releases);
	g_assert_cmpint(releases->len, ==, 1);
}

static void
fu_engine_install_duration_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	FwupdRelease *rel;
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* ensure empty tree */
	fu_self_test_mkroot();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* write the main file */
	ret = g_file_set_contents(
	    "/tmp/fwupd-self-test/stable.xml",
	    "<components>"
	    "  <component type=\"firmware\">"
	    "    <id>test</id>"
	    "    <provides>"
	    "      <firmware type=\"flashed\">aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee</firmware>"
	    "    </provides>"
	    "    <releases>"
	    "      <release version=\"1.2.3\" date=\"2017-09-15\" install_duration=\"120\">"
	    "        <location>https://test.org/foo.cab</location>"
	    "        <checksum filename=\"foo.cab\" target=\"container\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
	    "        <checksum filename=\"firmware.bin\" target=\"content\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
	    "      </release>"
	    "    </releases>"
	    "  </component>"
	    "</components>",
	    -1,
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_REMOTES | FU_ENGINE_LOAD_FLAG_NO_CACHE,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a device so we can get the install duration */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.3");
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_add_guid(device, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
	fu_device_set_install_duration(device, 999);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_engine_add_device(engine, device);
	devices = fu_engine_get_devices(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_SUPPORTED));

	/* check the release install duration */
	releases = fu_engine_get_releases(engine, request, fu_device_get_id(device), &error);
	g_assert_no_error(error);
	g_assert_nonnull(releases);
	g_assert_cmpint(releases->len, ==, 1);
	rel = FWUPD_RELEASE(g_ptr_array_index(releases, 0));
	g_assert_cmpint(fwupd_release_get_install_duration(rel), ==, 120);
}

static void
fu_engine_release_dedupe_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* ensure empty tree */
	fu_self_test_mkroot();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* write the main file */
	ret = g_file_set_contents(
	    "/tmp/fwupd-self-test/stable.xml",
	    "<components>"
	    "  <component type=\"firmware\">"
	    "    <id>test</id>"
	    "    <provides>"
	    "      <firmware type=\"flashed\">aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee</firmware>"
	    "    </provides>"
	    "    <releases>"
	    "      <release version=\"1.2.3\" date=\"2017-09-15\" install_duration=\"120\">"
	    "        <location>https://test.org/foo.cab</location>"
	    "        <checksum filename=\"foo.cab\" target=\"container\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
	    "        <checksum filename=\"firmware.bin\" target=\"content\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
	    "      </release>"
	    "      <release version=\"1.2.3\" date=\"2017-09-15\" install_duration=\"120\">"
	    "        <location>https://test.org/foo.cab</location>"
	    "        <checksum filename=\"foo.cab\" target=\"container\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
	    "        <checksum filename=\"firmware.bin\" target=\"content\" "
	    "type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
	    "      </release>"
	    "    </releases>"
	    "  </component>"
	    "</components>",
	    -1,
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_REMOTES | FU_ENGINE_LOAD_FLAG_NO_CACHE,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a device so we can get the install duration */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.3");
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_add_guid(device, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
	fu_device_set_install_duration(device, 999);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_engine_add_device(engine, device);
	devices = fu_engine_get_devices(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_SUPPORTED));

	/* check the release install duration */
	releases = fu_engine_get_releases(engine, request, fu_device_get_id(device), &error);
	g_assert_no_error(error);
	g_assert_nonnull(releases);
	g_assert_cmpint(releases->len, ==, 1);
}

static void
fu_engine_history_modify_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuHistory) history = fu_history_new(self->ctx);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;

#ifndef HAVE_SQLITE
	g_test_skip("no sqlite support");
	return;
#endif

	/* add a new entry */
	fu_device_set_id(device, "foobarbaz");
	fu_history_remove_device(history, device, NULL);
	ret = fu_history_add_device(history, device, release, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* try to modify something that does exist */
	ret = fu_history_modify_device(history, device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* does not exist */
	fu_device_set_id(device, "DOES-NOT-EXIST");
	ret = fu_history_modify_device(history, device, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
}

static void
fu_engine_history_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autofree gchar *checksum = NULL;
	g_autofree gchar *device_str_expected = NULL;
	g_autofree gchar *device_str = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuCabinet) cabinet = NULL;
	g_autoptr(FuDevice) device2 = NULL;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuHistory) history = NULL;
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_test_plugin_get_type(), self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FwupdDevice) device3 = NULL;
	g_autoptr(FwupdDevice) device4 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* ensure empty tree */
	fu_self_test_mkroot();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up dummy plugin */
	ret = fu_plugin_reset_config_values(plugin, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_set_config_value(plugin, "AnotherWriteRequired", "true", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_engine_add_plugin(engine, plugin);

	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a device so we can get upgrade it */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.2");
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_name(device, "Test Device");
	fu_device_set_plugin(device, "test");
	fu_device_add_guid(device, "12345678-1234-1234-1234-123456789012");
	fu_device_add_checksum(device, "0123456789abcdef0123456789abcdef01234567");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_created_usec(device, 1515338000ull * G_USEC_PER_SEC);
	fu_engine_add_device(engine, device);
	devices = fu_engine_get_devices(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	g_assert_true(fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_REGISTERED));

	filename =
	    g_test_build_filename(G_TEST_BUILT, "tests", "missing-hwid", "noreqs-1.2.3.cab", NULL);
	stream = fu_input_stream_from_path(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	cabinet = fu_engine_build_cabinet_from_stream(engine, stream, &error);
	g_assert_no_error(error);
	g_assert_nonnull(cabinet);

	/* get component */
	component = fu_cabinet_get_component(cabinet, "com.hughski.test.firmware", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* set the counter */
	fu_device_set_metadata_integer(device, "nr-update", 0);

	/* install it */
	fu_release_set_device(release, device);
	ret = fu_release_load(release, cabinet, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_install_release(engine,
					release,
					stream,
					progress,
					FWUPD_INSTALL_FLAG_NONE,
					&error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check the write was done more than once */
	g_assert_cmpint(fu_device_get_metadata_integer(device, "nr-update"), ==, 2);

	/* check the history database */
	history = fu_history_new(self->ctx);
	device2 = fu_history_get_device_by_id(history, fu_device_get_id(device), &error);
	if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip("no sqlite support");
		return;
	}
	g_assert_no_error(error);
	g_assert_nonnull(device2);
	g_assert_cmpint(fu_device_get_update_state(device2), ==, FWUPD_UPDATE_STATE_SUCCESS);
	g_assert_cmpstr(fu_device_get_update_error(device2), ==, NULL);
	fu_device_set_modified_usec(device2, 1514338000ull * G_USEC_PER_SEC);
	g_hash_table_remove_all(fwupd_release_get_metadata(fu_device_get_release_default(device2)));
	device_str = fu_device_to_string(device2);
	checksum = fu_input_stream_compute_checksum(stream, G_CHECKSUM_SHA1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(checksum);
	device_str_expected =
	    g_strdup_printf("FuDevice:\n"
			    "  DeviceId:             894e8c17a29428b09d10cd90d1db74ea76fbcfe8\n"
			    "  Name:                 Test Device\n"
			    "  Guid:                 12345678-1234-1234-1234-123456789012\n"
			    "  Plugin:               test\n"
			    "  Flags:                updatable|historical|unsigned-payload\n"
			    "  Version:              1.2.2\n"
			    "  VersionFormat:        triplet\n"
			    "  Created:              2018-01-07\n"
			    "  Modified:             2017-12-27\n"
			    "  UpdateState:          success\n"
			    "  FuRelease:\n"
			    "    AppstreamId:        com.hughski.test.firmware\n"
			    "    Version:            1.2.3\n"
			    "    Checksum:           SHA1(%s)\n"
			    "    Flags:              trusted-payload|trusted-metadata\n"
			    "  AcquiesceDelay:       50\n",
			    checksum);
	ret = fu_test_compare_lines(device_str, device_str_expected, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* GetResults() */
	device3 = fu_engine_get_results(engine, FWUPD_DEVICE_ID_ANY, &error);
	g_assert_no_error(error);
	g_assert_nonnull(device3);
	g_assert_cmpstr(fu_device_get_id(device3), ==, "894e8c17a29428b09d10cd90d1db74ea76fbcfe8");
	g_assert_cmpint(fu_device_get_update_state(device3), ==, FWUPD_UPDATE_STATE_SUCCESS);
	g_assert_cmpstr(fu_device_get_update_error(device3), ==, NULL);

	/* ClearResults() */
	ret = fu_engine_clear_results(engine, FWUPD_DEVICE_ID_ANY, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* GetResults() */
	device4 = fu_engine_get_results(engine, FWUPD_DEVICE_ID_ANY, &error);
	g_assert_null(device4);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO);
}

static void
fu_engine_history_verfmt_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_DPAUX_DEVICE, "context", self->ctx, NULL);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_test_plugin_get_type(), self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up dummy plugin */
	fu_engine_add_plugin(engine, plugin);
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* absorb version format from the database */
	fu_device_set_version_raw(device, 65563);
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_NUMBER);
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_plugin(device, "test");
	fu_device_add_guid(device, "12345678-1234-1234-1234-123456789012");
	fu_device_add_checksum(device, "0123456789abcdef0123456789abcdef01234567");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_MD_SET_VERFMT);
	fu_device_set_created_usec(device, 1515338000ull * G_USEC_PER_SEC);
	fu_engine_add_device(engine, device);
	g_assert_cmpint(fu_device_get_version_format(device), ==, FWUPD_VERSION_FORMAT_TRIPLET);
	g_assert_cmpstr(fu_device_get_version(device), ==, "0.1.27");
}

static void
fu_engine_multiple_rels_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuCabinet) cabinet = NULL;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_test_plugin_get_type(), self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(GPtrArray) rels = NULL;
	g_autoptr(XbQuery) query = NULL;

#ifndef HAVE_LIBARCHIVE
	g_test_skip("no libarchive support");
	return;
#endif

	/* ensure empty tree */
	fu_self_test_mkroot();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up dummy plugin */
	ret = fu_plugin_reset_config_values(plugin, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_engine_add_plugin(engine, plugin);

	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a device so we can get upgrade it */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.2");
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_name(device, "Test Device");
	fu_device_set_plugin(device, "test");
	fu_device_add_guid(device, "12345678-1234-1234-1234-123456789012");
	fu_device_add_checksum(device, "0123456789abcdef0123456789abcdef01234567");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INSTALL_ALL_RELEASES);
	fu_device_set_created_usec(device, 1515338000ull * G_USEC_PER_SEC);
	fu_engine_add_device(engine, device);

	filename = g_test_build_filename(G_TEST_BUILT,
					 "tests",
					 "multiple-rels",
					 "multiple-rels-1.2.4.cab",
					 NULL);
	stream = fu_input_stream_from_path(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	cabinet = fu_engine_build_cabinet_from_stream(engine, stream, &error);
	g_assert_no_error(error);
	g_assert_nonnull(cabinet);

	/* get component */
	component = fu_cabinet_get_component(cabinet, "com.hughski.test.firmware", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* set up counter */
	fu_device_set_metadata_integer(device, "nr-update", 0);

	/* get all */
	query = xb_query_new_full(xb_node_get_silo(component),
				  "releases/release",
				  XB_QUERY_FLAG_FORCE_NODE_CACHE,
				  &error);
	g_assert_no_error(error);
	g_assert_nonnull(query);
	rels = xb_node_query_full(component, query, &error);
	g_assert_no_error(error);
	g_assert_nonnull(rels);

	releases = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (guint i = 0; i < rels->len; i++) {
		XbNode *rel = g_ptr_array_index(rels, i);
		g_autoptr(FuRelease) release = fu_release_new();
		fu_release_set_device(release, device);
		ret = fu_release_load(release,
				      cabinet,
				      component,
				      rel,
				      FWUPD_INSTALL_FLAG_NONE,
				      &error);
		g_assert_no_error(error);
		g_assert_true(ret);
		g_ptr_array_add(releases, g_object_ref(release));
	}

	/* install them */
	fu_progress_reset(progress);
	ret = fu_engine_install_releases(engine,
					 request,
					 releases,
					 cabinet,
					 progress,
					 FWUPD_INSTALL_FLAG_NONE,
					 &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check we did 1.2.2 -> 1.2.3 -> 1.2.4 */
	g_assert_cmpint(fu_device_get_metadata_integer(device, "nr-update"), ==, 2);
	g_assert_cmpstr(fu_device_get_version(device), ==, "1.2.4");

	/* reset the config back to defaults */
	ret = fu_engine_reset_config(engine, "test", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_history_inherit(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *localstatedir = NULL;
	g_autofree gchar *history_db = NULL;
	g_autoptr(FuCabinet) cabinet = NULL;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_test_plugin_get_type(), self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

#ifndef HAVE_SQLITE
	g_test_skip("no sqlite support");
	return;
#endif

	/* delete history */
	localstatedir = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);
	history_db = g_build_filename(localstatedir, "pending.db", NULL);
	(void)g_unlink(history_db);

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up dummy plugin */
	ret = fu_plugin_reset_config_values(plugin, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_set_config_value(plugin, "NeedsActivation", "true", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_engine_add_plugin(engine, plugin);
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a device so we can get upgrade it */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.2");
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_name(device, "Test Device");
	fu_device_set_plugin(device, "test");
	fu_device_add_guid(device, "12345678-1234-1234-1234-123456789012");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_created_usec(device, 1515338000ull * G_USEC_PER_SEC);
	fu_engine_add_device(engine, device);
	devices = fu_engine_get_devices(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	g_assert_true(fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_REGISTERED));

	filename =
	    g_test_build_filename(G_TEST_BUILT, "tests", "missing-hwid", "noreqs-1.2.3.cab", NULL);
	stream = fu_input_stream_from_path(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	cabinet = fu_engine_build_cabinet_from_stream(engine, stream, &error);
	g_assert_no_error(error);
	g_assert_nonnull(cabinet);

	/* get component */
	component = fu_cabinet_get_component(cabinet, "com.hughski.test.firmware", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* install it */
	fu_release_set_device(release, device);
	ret = fu_release_load(release, cabinet, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_install_release(engine,
					release,
					stream,
					progress,
					FWUPD_INSTALL_FLAG_NONE,
					&error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check the device requires an activation */
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION));
	g_assert_cmpstr(fu_device_get_version(device), ==, "1.2.2");

	/* activate the device */
	fu_progress_reset(progress);
	ret = fu_engine_activate(engine, fu_device_get_id(device), progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check the device no longer requires an activation */
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION));
	g_assert_cmpstr(fu_device_get_version(device), ==, "1.2.3");

	/* emulate getting the flag for a fresh boot on old firmware */
	fu_progress_reset(progress);
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.2");
	ret = fu_engine_install_release(engine,
					release,
					stream,
					progress,
					FWUPD_INSTALL_FLAG_NONE,
					&error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_object_unref(engine);
	g_object_unref(device);
	engine = fu_engine_new(self->ctx);
	fu_engine_set_silo(engine, silo_empty);
	fu_engine_add_plugin(engine, plugin);
	device = fu_device_new(self->ctx);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_INHERIT_ACTIVATION);
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_name(device, "Test Device");
	fu_device_add_guid(device, "12345678-1234-1234-1234-123456789012");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.2");
	fu_engine_add_device(engine, device);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION));

	/* emulate not getting the flag */
	g_object_unref(engine);
	g_object_unref(device);
	engine = fu_engine_new(self->ctx);
	fu_engine_set_silo(engine, silo_empty);
	fu_engine_add_plugin(engine, plugin);
	device = fu_device_new(self->ctx);
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_name(device, "Test Device");
	fu_device_add_guid(device, "12345678-1234-1234-1234-123456789012");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.2");
	fu_engine_add_device(engine, device);
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION));
}

static void
fu_engine_install_needs_reboot(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuCabinet) cabinet = NULL;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_test_plugin_get_type(), self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up dummy plugin */
	ret = fu_plugin_reset_config_values(plugin, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_set_config_value(plugin, "NeedsReboot", "true", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_engine_add_plugin(engine, plugin);
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a device so we can get upgrade it */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.2");
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_name(device, "Test Device");
	fu_device_set_plugin(device, "test");
	fu_device_add_guid(device, "12345678-1234-1234-1234-123456789012");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_created_usec(device, 1515338000ull * G_USEC_PER_SEC);
	fu_engine_add_device(engine, device);
	devices = fu_engine_get_devices(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	g_assert_true(fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_REGISTERED));

	filename =
	    g_test_build_filename(G_TEST_BUILT, "tests", "missing-hwid", "noreqs-1.2.3.cab", NULL);
	stream = fu_input_stream_from_path(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	cabinet = fu_engine_build_cabinet_from_stream(engine, stream, &error);
	g_assert_no_error(error);
	g_assert_nonnull(cabinet);

	/* get component */
	component = fu_cabinet_get_component(cabinet, "com.hughski.test.firmware", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* install it */
	fu_release_set_device(release, device);
	ret = fu_release_load(release, cabinet, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_install_release(engine,
					release,
					stream,
					progress,
					FWUPD_INSTALL_FLAG_NONE,
					&error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check the device requires reboot */
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT));
	g_assert_cmpint(fu_device_get_update_state(device), ==, FWUPD_UPDATE_STATE_NEEDS_REBOOT);
	g_assert_cmpstr(fu_device_get_version(device), ==, "1.2.2");
}

typedef struct {
	guint request_cnt;
	FwupdStatus last_status;
} FuTestRequestHelper;

static void
fu_test_engine_status_changed_cb(FuProgress *progress, FwupdStatus status, gpointer user_data)
{
	FuTestRequestHelper *helper = (FuTestRequestHelper *)user_data;
	g_debug("status now %s", fwupd_status_to_string(status));
	helper->last_status = status;
}

static void
fu_test_engine_request_cb(FuEngine *engine, FwupdRequest *request, gpointer user_data)
{
	FuTestRequestHelper *helper = (FuTestRequestHelper *)user_data;
	g_assert_cmpint(fwupd_request_get_kind(request), ==, FWUPD_REQUEST_KIND_IMMEDIATE);
	g_assert_cmpstr(fwupd_request_get_id(request), ==, FWUPD_REQUEST_ID_REMOVE_REPLUG);
	g_assert_true(fwupd_request_has_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE));
	g_assert_nonnull(fwupd_request_get_message(request));
	g_assert_cmpint(helper->last_status, ==, FWUPD_STATUS_WAITING_FOR_USER);
	helper->request_cnt++;
}

static void
fu_engine_install_request(gconstpointer user_data)
{
	FuTestRequestHelper helper = {.request_cnt = 0, .last_status = FWUPD_STATUS_UNKNOWN};
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuCabinet) cabinet = NULL;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_test_plugin_get_type(), self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up dummy plugin */
	ret = fu_plugin_set_config_value(plugin, "RequestSupported", "true", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_engine_add_plugin(engine, plugin);
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a device so we can get upgrade it */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.2");
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_name(device, "Test Device");
	fu_device_set_plugin(device, "test");
	fu_device_add_guid(device, "12345678-1234-1234-1234-123456789012");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_request_flag(device, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	fu_device_set_created_usec(device, 1515338000ull * G_USEC_PER_SEC);
	fu_engine_add_device(engine, device);
	devices = fu_engine_get_devices(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	g_assert_true(fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_REGISTERED));

	filename =
	    g_test_build_filename(G_TEST_BUILT, "tests", "missing-hwid", "noreqs-1.2.3.cab", NULL);
	stream = fu_input_stream_from_path(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	cabinet = fu_engine_build_cabinet_from_stream(engine, stream, &error);
	g_assert_no_error(error);
	g_assert_nonnull(cabinet);

	/* get component */
	component = fu_cabinet_get_component(cabinet, "com.hughski.test.firmware", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* install it */
	fu_release_set_device(release, device);
	ret = fu_release_load(release, cabinet, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_signal_connect(FU_ENGINE(engine),
			 "device-request",
			 G_CALLBACK(fu_test_engine_request_cb),
			 &helper);
	g_signal_connect(FU_PROGRESS(progress),
			 "status-changed",
			 G_CALLBACK(fu_test_engine_status_changed_cb),
			 &helper);

	ret = fu_engine_install_release(engine,
					release,
					stream,
					progress,
					FWUPD_INSTALL_FLAG_NONE,
					&error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(helper.request_cnt, ==, 1);
	g_assert_cmpint(helper.last_status, ==, FWUPD_STATUS_DEVICE_BUSY);
}

static void
fu_engine_history_error_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autofree gchar *checksum = NULL;
	g_autofree gchar *device_str_expected = NULL;
	g_autofree gchar *device_str = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuCabinet) cabinet = NULL;
	g_autoptr(FuDevice) device2 = NULL;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuHistory) history = NULL;
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_test_plugin_get_type(), self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error2 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up dummy plugin */
	ret = fu_plugin_set_config_value(plugin, "WriteSupported", "false", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_engine_add_plugin(engine, plugin);
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a device so we can get upgrade it */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.2");
	fu_device_set_id(device, "test_device");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_protocol(device, "com.acme");
	fu_device_set_name(device, "Test Device");
	fu_device_set_plugin(device, "test");
	fu_device_add_guid(device, "12345678-1234-1234-1234-123456789012");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_created_usec(device, 1515338000ull * G_USEC_PER_SEC);
	fu_engine_add_device(engine, device);
	devices = fu_engine_get_devices(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	g_assert_true(fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_REGISTERED));

	/* install the wrong thing */
	filename =
	    g_test_build_filename(G_TEST_BUILT, "tests", "missing-hwid", "noreqs-1.2.3.cab", NULL);
	stream = fu_input_stream_from_path(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	cabinet = fu_engine_build_cabinet_from_stream(engine, stream, &error);
	g_assert_no_error(error);
	g_assert_nonnull(cabinet);
	component = fu_cabinet_get_component(cabinet, "com.hughski.test.firmware", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);
	fu_release_set_device(release, device);
	ret = fu_release_load(release, cabinet, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_install_release(engine,
					release,
					stream,
					progress,
					FWUPD_INSTALL_FLAG_NONE,
					&error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_cmpstr(error->message,
			==,
			"failed to write-firmware: device was not in supported mode");
	g_assert_false(ret);

	/* check the history database */
	history = fu_history_new(self->ctx);
	device2 = fu_history_get_device_by_id(history, fu_device_get_id(device), &error2);
	if (g_error_matches(error2, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip("no sqlite support");
		return;
	}
	g_assert_no_error(error2);
	g_assert_nonnull(device2);
	g_assert_cmpint(fu_device_get_update_state(device2), ==, FWUPD_UPDATE_STATE_FAILED);
	g_assert_cmpstr(fu_device_get_update_error(device2), ==, error->message);
	g_clear_error(&error);
	fu_device_set_modified_usec(device2, 1514338000ull * G_USEC_PER_SEC);
	g_hash_table_remove_all(fwupd_release_get_metadata(fu_device_get_release_default(device2)));
	device_str = fu_device_to_string(device2);
	checksum = fu_input_stream_compute_checksum(stream, G_CHECKSUM_SHA1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(checksum);
	device_str_expected = g_strdup_printf(
	    "FuDevice:\n"
	    "  DeviceId:             894e8c17a29428b09d10cd90d1db74ea76fbcfe8\n"
	    "  Name:                 Test Device\n"
	    "  Guid:                 12345678-1234-1234-1234-123456789012\n"
	    "  Plugin:               test\n"
	    "  Flags:                updatable|historical|unsigned-payload\n"
	    "  Version:              1.2.2\n"
	    "  VersionFormat:        triplet\n"
	    "  Created:              2018-01-07\n"
	    "  Modified:             2017-12-27\n"
	    "  UpdateState:          failed\n"
	    "  UpdateError:          failed to write-firmware: device was not in supported mode\n"
	    "  FuRelease:\n"
	    "    AppstreamId:        com.hughski.test.firmware\n"
	    "    Version:            1.2.3\n"
	    "    Checksum:           SHA1(%s)\n"
	    "    Flags:              trusted-payload|trusted-metadata\n"
	    "  AcquiesceDelay:       50\n",
	    checksum);
	ret = fu_test_compare_lines(device_str, device_str_expected, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_test_device_list_count_cb(FuDeviceList *device_list, FuDevice *device, gpointer user_data)
{
	guint *cnt = (guint *)user_data;
	(*cnt)++;
}

static void
fu_device_list_no_auto_remove_children_func(gconstpointer user_data)
{
	g_autoptr(FuDevice) child = fu_device_new(NULL);
	g_autoptr(FuDevice) parent = fu_device_new(NULL);
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();
	g_autoptr(GPtrArray) active1 = NULL;
	g_autoptr(GPtrArray) active2 = NULL;
	g_autoptr(GPtrArray) active3 = NULL;

	/* normal behavior, remove child with parent */
	fu_device_set_id(parent, "parent");
	fu_device_set_id(child, "child");
	fu_device_add_child(parent, child);
	fu_device_list_add(device_list, parent);
	fu_device_list_add(device_list, child);
	fu_device_list_remove(device_list, parent);
	active1 = fu_device_list_get_active(device_list);
	g_assert_cmpint(active1->len, ==, 0);

	/* new-style behavior, do not remove child */
	fu_device_add_private_flag(parent, FU_DEVICE_PRIVATE_FLAG_NO_AUTO_REMOVE_CHILDREN);
	fu_device_list_add(device_list, parent);
	fu_device_list_add(device_list, child);
	fu_device_list_remove(device_list, parent);
	active2 = fu_device_list_get_active(device_list);
	g_assert_cmpint(active2->len, ==, 1);
	fu_device_list_remove(device_list, child);
	active3 = fu_device_list_get_active(device_list);
	g_assert_cmpint(active3->len, ==, 0);
}

static void
fu_device_list_delay_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	g_autoptr(FuDevice) device1 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(self->ctx);
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();
	guint added_cnt = 0;
	guint changed_cnt = 0;
	guint removed_cnt = 0;

	g_signal_connect(FU_DEVICE_LIST(device_list),
			 "added",
			 G_CALLBACK(fu_test_device_list_count_cb),
			 &added_cnt);
	g_signal_connect(FU_DEVICE_LIST(device_list),
			 "removed",
			 G_CALLBACK(fu_test_device_list_count_cb),
			 &removed_cnt);
	g_signal_connect(FU_DEVICE_LIST(device_list),
			 "changed",
			 G_CALLBACK(fu_test_device_list_count_cb),
			 &changed_cnt);

	/* add one device */
	fu_device_set_id(device1, "device1");
	fu_device_add_instance_id(device1, "foobar");
	fu_device_set_remove_delay(device1, 100);
	fu_device_convert_instance_ids(device1);
	fu_device_list_add(device_list, device1);
	g_assert_cmpint(added_cnt, ==, 1);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 0);

	/* add the same device again */
	fu_device_list_add(device_list, device1);
	g_assert_cmpint(added_cnt, ==, 1);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 1);

	/* add a device with the same ID */
	fu_device_set_id(device2, "device1");
	fu_device_list_add(device_list, device2);
	fu_device_set_remove_delay(device2, 100);
	g_assert_cmpint(added_cnt, ==, 1);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 2);

	/* spin a bit */
	fu_test_loop_run_with_timeout(10);
	fu_test_loop_quit();

	/* verify only a changed event was generated */
	added_cnt = removed_cnt = changed_cnt = 0;
	fu_device_list_remove(device_list, device1);
	fu_device_list_add(device_list, device1);
	g_assert_cmpint(added_cnt, ==, 0);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 1);
}

typedef struct {
	FuDevice *device_new;
	FuDevice *device_old;
	FuDeviceList *device_list;
} FuDeviceListReplugHelper;

static gboolean
fu_device_list_remove_cb(gpointer user_data)
{
	FuDeviceListReplugHelper *helper = (FuDeviceListReplugHelper *)user_data;
	fu_device_list_remove(helper->device_list, helper->device_old);
	return FALSE;
}

static gboolean
fu_device_list_add_cb(gpointer user_data)
{
	FuDeviceListReplugHelper *helper = (FuDeviceListReplugHelper *)user_data;
	fu_device_list_add(helper->device_list, helper->device_new);
	return FALSE;
}

static void
fu_device_list_replug_auto_func(gconstpointer user_data)
{
	gboolean ret;
	g_autoptr(FuDevice) device1 = fu_device_new(NULL);
	g_autoptr(FuDevice) device2 = fu_device_new(NULL);
	g_autoptr(FuDevice) parent = fu_device_new(NULL);
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();
	g_autoptr(GError) error = NULL;
	FuDeviceListReplugHelper helper;

	/* parent */
	fu_device_set_id(parent, "parent");

	/* fake child devices */
	fu_device_set_id(device1, "device1");
	fu_device_add_private_flag(device1, FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_set_physical_id(device1, "ID");
	fu_device_set_plugin(device1, "self-test");
	fu_device_set_remove_delay(device1, FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_child(parent, device1);
	fu_device_set_id(device2, "device2");
	fu_device_add_private_flag(device2, FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_set_physical_id(device2, "ID"); /* matches */
	fu_device_set_plugin(device2, "self-test");
	fu_device_set_remove_delay(device2, FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);

	/* not yet added */
	ret = fu_device_list_wait_for_replug(device_list, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add device */
	fu_device_list_add(device_list, device1);

	/* not waiting */
	ret = fu_device_list_wait_for_replug(device_list, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* waiting */
	helper.device_old = device1;
	helper.device_new = device2;
	helper.device_list = device_list;
	g_timeout_add(100, fu_device_list_remove_cb, &helper);
	g_timeout_add(200, fu_device_list_add_cb, &helper);
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	ret = fu_device_list_wait_for_replug(device_list, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(fu_device_has_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG));

	/* check device2 now has parent too */
	g_assert_true(fu_device_get_parent(device2) == parent);

	/* waiting, failed */
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	ret = fu_device_list_wait_for_replug(device_list, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);
	g_assert_false(fu_device_has_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG));
}

static void
fu_device_list_replug_user_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device1 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(self->ctx);
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();
	g_autoptr(GError) error = NULL;
	FuDeviceListReplugHelper helper;

	/* fake devices */
	fu_device_set_id(device1, "device1");
	fu_device_set_name(device1, "device1");
	fu_device_add_private_flag(device1, FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_instance_id(device1, "foo");
	fu_device_add_instance_id(device1, "bar");
	fu_device_set_plugin(device1, "self-test");
	fu_device_set_remove_delay(device1, FU_DEVICE_REMOVE_DELAY_USER_REPLUG);
	fu_device_convert_instance_ids(device1);
	fu_device_set_id(device2, "device2");
	fu_device_set_name(device2, "device2");
	fu_device_add_private_flag(device2, FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_instance_id(device2, "baz");
	fu_device_add_counterpart_guid(device2, "bar"); /* matches */
	fu_device_set_plugin(device2, "self-test");
	fu_device_set_remove_delay(device2, FU_DEVICE_REMOVE_DELAY_USER_REPLUG);
	fu_device_convert_instance_ids(device2);

	/* not yet added */
	ret = fu_device_list_wait_for_replug(device_list, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add device */
	fu_device_list_add(device_list, device1);

	/* add duplicate */
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	fu_device_list_add(device_list, device1);
	g_assert_false(fu_device_has_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG));

	/* not waiting */
	ret = fu_device_list_wait_for_replug(device_list, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* waiting */
	helper.device_old = device1;
	helper.device_new = device2;
	helper.device_list = device_list;
	g_timeout_add(100, fu_device_list_remove_cb, &helper);
	g_timeout_add(200, fu_device_list_add_cb, &helper);
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	ret = fu_device_list_wait_for_replug(device_list, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(fu_device_has_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG));

	/* should not be possible, but here we are */
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	fu_device_list_add(device_list, device1);
	g_assert_false(fu_device_has_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG));
	g_assert_false(fu_device_has_flag(device2, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG));

	/* add back the old device */
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	fu_device_list_remove(device_list, device2);
	fu_device_list_add(device_list, device1);
	g_assert_false(fu_device_has_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG));
	g_assert_false(fu_device_has_flag(device2, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG));
}

static void
fu_device_list_compatible_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	g_autoptr(FuDevice) device1 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device_old = NULL;
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();
	g_autoptr(GPtrArray) devices_all = NULL;
	g_autoptr(GPtrArray) devices_active = NULL;
	FuDevice *device;
	guint added_cnt = 0;
	guint changed_cnt = 0;
	guint removed_cnt = 0;

	g_signal_connect(FU_DEVICE_LIST(device_list),
			 "added",
			 G_CALLBACK(fu_test_device_list_count_cb),
			 &added_cnt);
	g_signal_connect(FU_DEVICE_LIST(device_list),
			 "removed",
			 G_CALLBACK(fu_test_device_list_count_cb),
			 &removed_cnt);
	g_signal_connect(FU_DEVICE_LIST(device_list),
			 "changed",
			 G_CALLBACK(fu_test_device_list_count_cb),
			 &changed_cnt);

	/* add one device in runtime mode */
	fu_device_set_id(device1, "device1");
	fu_device_set_plugin(device1, "plugin-for-runtime");
	fu_device_build_vendor_id(device1, "USB", "0x20A0");
	fu_device_set_version_format(device1, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device1, "1.2.3");
	fu_device_add_private_flag(device1, FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_instance_id(device1, "foobar");
	fu_device_add_counterpart_guid(device1, "bootloader");
	fu_device_set_remove_delay(device1, 100);
	fu_device_convert_instance_ids(device1);
	fu_device_list_add(device_list, device1);
	g_assert_cmpint(added_cnt, ==, 1);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 0);

	/* add another device in bootloader mode */
	fu_device_set_id(device2, "device2");
	fu_device_set_plugin(device2, "plugin-for-bootloader");
	fu_device_add_instance_id(device2, "bootloader");
	fu_device_add_private_flag(device2, FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_convert_instance_ids(device2);

	/* verify only a changed event was generated */
	added_cnt = removed_cnt = changed_cnt = 0;
	fu_device_list_remove(device_list, device1);
	fu_device_list_add(device_list, device2);
	g_assert_cmpint(added_cnt, ==, 0);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 1);

	/* device2 should inherit the vendor ID and version from device1 */
	g_assert_true(fu_device_has_vendor_id(device2, "USB:0x20A0"));
	g_assert_cmpstr(fu_device_get_version(device2), ==, "1.2.3");

	/* one device is active */
	devices_active = fu_device_list_get_active(device_list);
	g_assert_cmpint(devices_active->len, ==, 1);
	device = g_ptr_array_index(devices_active, 0);
	g_assert_cmpstr(fu_device_get_id(device), ==, "1a8d0d9a96ad3e67ba76cf3033623625dc6d6882");

	/* the list knows about both devices, list in order of active->old */
	devices_all = fu_device_list_get_all(device_list);
	g_assert_cmpint(devices_all->len, ==, 2);
	device = g_ptr_array_index(devices_all, 0);
	g_assert_cmpstr(fu_device_get_id(device), ==, "1a8d0d9a96ad3e67ba76cf3033623625dc6d6882");
	device = g_ptr_array_index(devices_all, 1);
	g_assert_cmpstr(fu_device_get_id(device), ==, "99249eb1bd9ef0b6e192b271a8cb6a3090cfec7a");

	/* verify we can get the old device from the new device */
	device_old = fu_device_list_get_old(device_list, device2);
	g_assert_true(device_old == device1);
}

static void
fu_device_list_remove_chain_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();
	g_autoptr(FuDevice) device_child = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device_parent = fu_device_new(self->ctx);

	guint added_cnt = 0;
	guint changed_cnt = 0;
	guint removed_cnt = 0;

	g_signal_connect(FU_DEVICE_LIST(device_list),
			 "added",
			 G_CALLBACK(fu_test_device_list_count_cb),
			 &added_cnt);
	g_signal_connect(FU_DEVICE_LIST(device_list),
			 "removed",
			 G_CALLBACK(fu_test_device_list_count_cb),
			 &removed_cnt);
	g_signal_connect(FU_DEVICE_LIST(device_list),
			 "changed",
			 G_CALLBACK(fu_test_device_list_count_cb),
			 &changed_cnt);

	/* add child */
	fu_device_set_id(device_child, "child");
	fu_device_add_instance_id(device_child, "child-GUID-1");
	fu_device_convert_instance_ids(device_child);
	fu_device_list_add(device_list, device_child);
	g_assert_cmpint(added_cnt, ==, 1);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 0);

	/* add parent */
	fu_device_set_id(device_parent, "parent");
	fu_device_add_instance_id(device_parent, "parent-GUID-1");
	fu_device_convert_instance_ids(device_parent);
	fu_device_add_child(device_parent, device_child);
	fu_device_list_add(device_list, device_parent);
	g_assert_cmpint(added_cnt, ==, 2);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 0);

	/* make sure that removing the parent causes both to go; but the child to go first */
	fu_device_list_remove(device_list, device_parent);
	g_assert_cmpint(added_cnt, ==, 2);
	g_assert_cmpint(removed_cnt, ==, 2);
	g_assert_cmpint(changed_cnt, ==, 0);
}

static void
fu_device_list_explicit_order_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	g_autoptr(FuDevice) device_child = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device_root = fu_device_new(self->ctx);
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();

	/* add both */
	fu_device_set_id(device_root, "device");
	fu_device_add_instance_id(device_root, "foobar");
	fu_device_convert_instance_ids(device_root);
	fu_device_set_id(device_child, "device-child");
	fu_device_add_instance_id(device_child, "baz");
	fu_device_convert_instance_ids(device_child);
	fu_device_add_child(device_root, device_child);
	fu_device_list_add(device_list, device_root);

	fu_device_add_private_flag(device_root, FU_DEVICE_PRIVATE_FLAG_EXPLICIT_ORDER);
	fu_device_list_depsolve_order(device_list, device_root);
	g_assert_cmpint(fu_device_get_order(device_root), ==, G_MAXINT);
	g_assert_cmpint(fu_device_get_order(device_child), ==, G_MAXINT);
}

static void
fu_device_list_explicit_order_post_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	g_autoptr(FuDevice) device_child = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device_root = fu_device_new(self->ctx);
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();

	/* add both */
	fu_device_set_id(device_root, "device");
	fu_device_add_instance_id(device_root, "foobar");
	fu_device_convert_instance_ids(device_root);
	fu_device_set_id(device_child, "device-child");
	fu_device_add_instance_id(device_child, "baz");
	fu_device_convert_instance_ids(device_child);
	fu_device_add_child(device_root, device_child);
	fu_device_list_add(device_list, device_root);
	fu_device_list_add(device_list, device_child);

	fu_device_list_depsolve_order(device_list, device_root);
	g_assert_cmpint(fu_device_get_order(device_root), ==, 0);
	g_assert_cmpint(fu_device_get_order(device_child), ==, -1);

	fu_device_add_private_flag(device_root, FU_DEVICE_PRIVATE_FLAG_EXPLICIT_ORDER);
	g_assert_cmpint(fu_device_get_order(device_root), ==, G_MAXINT);
	g_assert_cmpint(fu_device_get_order(device_child), ==, G_MAXINT);
}

static void
fu_device_list_counterpart_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();
	g_autoptr(FuDevice) device1 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(self->ctx);

	/* add and then remove runtime */
	fu_device_set_id(device1, "device-runtime");
	fu_device_add_instance_id(device1, "runtime"); /* 420dde7c-3102-5d8f-86bc-aaabd7920150 */
	fu_device_add_counterpart_guid(device1, "bootloader");
	fu_device_convert_instance_ids(device1);
	fu_device_add_private_flag(device1, FU_DEVICE_PRIVATE_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_set_remove_delay(device1, 100);
	fu_device_list_add(device_list, device1);
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	fu_device_list_remove(device_list, device1);
	g_assert_true(fu_device_has_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG));

	/* add bootloader */
	fu_device_set_id(device2, "device-bootloader");
	fu_device_add_instance_id(device2, "bootloader"); /* 015370aa-26f2-5daa-9661-a75bf4c1a913 */
	fu_device_add_private_flag(device2, FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_private_flag(device2, FU_DEVICE_PRIVATE_FLAG_ADD_COUNTERPART_GUIDS);
	fu_device_convert_instance_ids(device2);
	fu_device_list_add(device_list, device2);

	/* should have matched the runtime */
	g_assert_false(fu_device_has_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG));

	/* should not have *visible* GUID of runtime */
	g_assert_false(fu_device_has_guid(device2, "runtime"));
	g_assert_true(fu_device_has_counterpart_guid(device2, "runtime"));
}

static void
fu_device_list_equivalent_id_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	g_autoptr(FuDevice) device1 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();
	g_autoptr(GError) error = NULL;

	fu_device_set_id(device1, "8e9cb71aeca70d2faedb5b8aaa263f6175086b2e");
	fu_device_list_add(device_list, device1);

	fu_device_set_id(device2, "1a8d0d9a96ad3e67ba76cf3033623625dc6d6882");
	fu_device_set_equivalent_id(device2, "8e9cb71aeca70d2faedb5b8aaa263f6175086b2e");
	fu_device_set_priority(device2, 999);
	fu_device_list_add(device_list, device2);

	device = fu_device_list_get_by_id(device_list, "8e9c", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_device_get_id(device), ==, "1a8d0d9a96ad3e67ba76cf3033623625dc6d6882");
}

static void
fu_device_list_unconnected_no_delay_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();
	g_autoptr(FuDevice) device1 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(self->ctx);

	fu_device_set_id(device1, "device1");
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_instance_id(device1, "foobar");
	fu_device_convert_instance_ids(device1);
	fu_device_list_add(device_list, device1);
	g_assert_false(fu_device_has_private_flag(device1, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED));

	/* remove */
	fu_device_list_remove(device_list, device1);
	g_assert_true(fu_device_has_private_flag(device1, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED));

	/* add back exact same device, then remove */
	fu_device_list_add(device_list, device1);
	g_assert_false(fu_device_has_private_flag(device1, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED));
	fu_device_list_remove(device_list, device1);
	g_assert_true(fu_device_has_private_flag(device1, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED));

	/* add back device with same ID, then remove */
	fu_device_set_id(device2, "device1");
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_instance_id(device2, "foobar");
	fu_device_convert_instance_ids(device2);
	fu_device_list_add(device_list, device2);
	g_assert_false(fu_device_has_private_flag(device2, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED));
	fu_device_list_remove(device_list, device2);
	g_assert_true(fu_device_has_private_flag(device2, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED));
}

static void
fu_device_list_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();
	g_autoptr(FuDevice) device1 = fu_device_new(self->ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(self->ctx);
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) devices2 = NULL;
	g_autoptr(GError) error = NULL;
	FuDevice *device;
	guint added_cnt = 0;
	guint changed_cnt = 0;
	guint removed_cnt = 0;

	g_signal_connect(FU_DEVICE_LIST(device_list),
			 "added",
			 G_CALLBACK(fu_test_device_list_count_cb),
			 &added_cnt);
	g_signal_connect(FU_DEVICE_LIST(device_list),
			 "removed",
			 G_CALLBACK(fu_test_device_list_count_cb),
			 &removed_cnt);
	g_signal_connect(FU_DEVICE_LIST(device_list),
			 "changed",
			 G_CALLBACK(fu_test_device_list_count_cb),
			 &changed_cnt);

	/* add both */
	fu_device_set_id(device1, "device1");
	fu_device_add_instance_id(device1, "foobar");
	fu_device_convert_instance_ids(device1);
	fu_device_list_add(device_list, device1);
	fu_device_set_id(device2, "device2");
	fu_device_add_instance_id(device2, "baz");
	fu_device_convert_instance_ids(device2);
	fu_device_list_add(device_list, device2);
	g_assert_cmpint(added_cnt, ==, 2);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 0);

	/* get all */
	devices = fu_device_list_get_all(device_list);
	g_assert_cmpint(devices->len, ==, 2);
	device = g_ptr_array_index(devices, 0);
	g_assert_cmpstr(fu_device_get_id(device), ==, "99249eb1bd9ef0b6e192b271a8cb6a3090cfec7a");

	/* find by ID */
	device = fu_device_list_get_by_id(device_list,
					  "99249eb1bd9ef0b6e192b271a8cb6a3090cfec7a",
					  &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_device_get_id(device), ==, "99249eb1bd9ef0b6e192b271a8cb6a3090cfec7a");
	g_clear_object(&device);

	/* find by GUID */
	device =
	    fu_device_list_get_by_guid(device_list, "579a3b1c-d1db-5bdc-b6b9-e2c1b28d5b8a", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_device_get_id(device), ==, "1a8d0d9a96ad3e67ba76cf3033623625dc6d6882");
	g_clear_object(&device);

	/* find by missing GUID */
	device = fu_device_list_get_by_guid(device_list, "notfound", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(device);

	/* remove device */
	added_cnt = removed_cnt = changed_cnt = 0;
	fu_device_list_remove(device_list, device1);
	g_assert_cmpint(added_cnt, ==, 0);
	g_assert_cmpint(removed_cnt, ==, 1);
	g_assert_cmpint(changed_cnt, ==, 0);
	devices2 = fu_device_list_get_all(device_list);
	g_assert_cmpint(devices2->len, ==, 1);
	device = g_ptr_array_index(devices2, 0);
	g_assert_cmpstr(fu_device_get_id(device), ==, "1a8d0d9a96ad3e67ba76cf3033623625dc6d6882");
}

static void
fu_plugin_list_func(gconstpointer user_data)
{
	GPtrArray *plugins;
	FuPlugin *plugin;
	g_autoptr(FuPluginList) plugin_list = fu_plugin_list_new();
	g_autoptr(FuPlugin) plugin1 = fu_plugin_new(NULL);
	g_autoptr(FuPlugin) plugin2 = fu_plugin_new(NULL);
	g_autoptr(GError) error = NULL;

	fu_plugin_set_name(plugin1, "plugin1");
	fu_plugin_set_name(plugin2, "plugin2");

	/* get all the plugins */
	fu_plugin_list_add(plugin_list, plugin1);
	fu_plugin_list_add(plugin_list, plugin2);
	plugins = fu_plugin_list_get_all(plugin_list);
	g_assert_cmpint(plugins->len, ==, 2);

	/* get a single plugin */
	plugin = fu_plugin_list_find_by_name(plugin_list, "plugin1", &error);
	g_assert_no_error(error);
	g_assert_nonnull(plugin);
	g_assert_cmpstr(fu_plugin_get_name(plugin), ==, "plugin1");

	/* does not exist */
	plugin = fu_plugin_list_find_by_name(plugin_list, "nope", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(plugin);
}

static void
fu_plugin_list_depsolve_func(gconstpointer user_data)
{
	GPtrArray *plugins;
	FuPlugin *plugin;
	gboolean ret;
	g_autoptr(FuPluginList) plugin_list = fu_plugin_list_new();
	g_autoptr(FuPlugin) plugin1 = fu_plugin_new(NULL);
	g_autoptr(FuPlugin) plugin2 = fu_plugin_new(NULL);
	g_autoptr(GError) error = NULL;

	fu_plugin_set_name(plugin1, "plugin1");
	fu_plugin_set_name(plugin2, "plugin2");

	/* add rule then depsolve */
	fu_plugin_list_add(plugin_list, plugin1);
	fu_plugin_list_add(plugin_list, plugin2);
	fu_plugin_add_rule(plugin1, FU_PLUGIN_RULE_RUN_AFTER, "plugin2");
	ret = fu_plugin_list_depsolve(plugin_list, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	plugins = fu_plugin_list_get_all(plugin_list);
	g_assert_cmpint(plugins->len, ==, 2);
	plugin = g_ptr_array_index(plugins, 0);
	g_assert_cmpstr(fu_plugin_get_name(plugin), ==, "plugin2");
	g_assert_cmpint(fu_plugin_get_order(plugin), ==, 0);
	g_assert_false(fu_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED));

	/* add another rule, then re-depsolve */
	fu_plugin_add_rule(plugin1, FU_PLUGIN_RULE_CONFLICTS, "plugin2");
	ret = fu_plugin_list_depsolve(plugin_list, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	plugin = fu_plugin_list_find_by_name(plugin_list, "plugin1", &error);
	g_assert_no_error(error);
	g_assert_nonnull(plugin);
	g_assert_false(fu_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED));
	plugin = fu_plugin_list_find_by_name(plugin_list, "plugin2", &error);
	g_assert_no_error(error);
	g_assert_nonnull(plugin);
	g_assert_true(fu_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED));
}

static void
fu_history_migrate_v1_func(gconstpointer user_data)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file_dst = NULL;
	g_autoptr(GFile) file_src = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuHistory) history = NULL;
	g_autofree gchar *filename = NULL;

#ifndef HAVE_SQLITE
	g_test_skip("no sqlite support");
	return;
#endif

	/* load old version */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "history_v1.db", NULL);
	file_src = g_file_new_for_path(filename);
	file_dst = g_file_new_for_path("/tmp/fwupd-self-test/var/lib/fwupd/pending.db");
	ret = g_file_copy(file_src, file_dst, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create, migrating as required */
	history = fu_history_new(ctx);
	g_assert_nonnull(history);

	/* get device */
	device = fu_history_get_device_by_id(history,
					     "2ba16d10df45823dd4494ff10a0bfccfef512c9d",
					     &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_device_get_id(device), ==, "2ba16d10df45823dd4494ff10a0bfccfef512c9d");
}

static void
fu_history_migrate_v2_func(gconstpointer user_data)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file_dst = NULL;
	g_autoptr(GFile) file_src = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuHistory) history = NULL;
	g_autofree gchar *filename = NULL;

#ifndef HAVE_SQLITE
	g_test_skip("no sqlite support");
	return;
#endif

	/* load old version */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "history_v2.db", NULL);
	file_src = g_file_new_for_path(filename);
	file_dst = g_file_new_for_path("/tmp/fwupd-self-test/var/lib/fwupd/pending.db");
	ret = g_file_copy(file_src, file_dst, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create, migrating as required */
	history = fu_history_new(ctx);
	g_assert_nonnull(history);

	/* get device */
	device = fu_history_get_device_by_id(history,
					     "2ba16d10df45823dd4494ff10a0bfccfef512c9d",
					     &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_device_get_id(device), ==, "2ba16d10df45823dd4494ff10a0bfccfef512c9d");
}

static void
fu_test_plugin_device_added_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	FuDevice **dev = (FuDevice **)user_data;
	*dev = g_object_ref(device);
	fu_test_loop_quit();
}

static void
fu_test_plugin_device_register_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	/* fake being a daemon */
	fu_plugin_runner_device_register(plugin, device);
}

static void
fu_backend_usb_hotplug_cb(FuBackend *backend, FuDevice *device, gpointer user_data)
{
	guint *cnt = (guint *)user_data;
	(*cnt)++;
}

static void
fu_backend_usb_load_file(FuBackend *backend, const gchar *fn)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(JsonParser) parser = json_parser_new();

	ret = json_parser_load_from_file(parser, fn, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fwupd_codec_from_json(FWUPD_CODEC(backend), json_parser_get_root(parser), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

/*
 * To generate the fwupd DS20 descriptor in the usb-devices.json file save fw-ds20.builder.xml:
 *
 *    <firmware gtype="FuUsbDeviceFwDs20">
 *      <idx>42</idx>   <!-- bVendorCode -->
 *      <size>32</size> <!-- wLength -->
 *    </firmware>
 *
 * Then run:
 *
 *    fwupdtool firmware-build fw-ds20.builder.xml fw-ds20.bin
 *    base64 fw-ds20.bin
 *
 * To generate the fake control transfer response, save fw-ds20.quirk:
 *
 *    [USB\VID_273F&PID_1004]
 *    Plugin = dfu
 *    Icon = computer
 *
 * Then run:
 *
 *    contrib/generate-ds20.py fw-ds20.quirk --bufsz 32
 */
static void
fu_backend_usb_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	guint cnt_added = 0;
	guint cnt_removed = 0;
	FuDevice *device_tmp;
	g_autofree gchar *usb_emulate_fn = NULL;
	g_autofree gchar *usb_emulate_fn2 = NULL;
	g_autofree gchar *usb_emulate_fn3 = NULL;
	g_autofree gchar *devicestr = NULL;
	g_autoptr(FuBackend) backend = fu_usb_backend_new(self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) possible_plugins = NULL;

#if !GLIB_CHECK_VERSION(2, 80, 0)
	g_test_skip("GLib version too old");
	return;
#endif

	/* check there were events */
	g_object_set(backend, "device-gtype", FU_TYPE_USB_DEVICE, NULL);
	g_signal_connect(backend,
			 "device-added",
			 G_CALLBACK(fu_backend_usb_hotplug_cb),
			 &cnt_added);
	g_signal_connect(backend,
			 "device-removed",
			 G_CALLBACK(fu_backend_usb_hotplug_cb),
			 &cnt_removed);

	/* load the JSON into the backend */
	g_assert_cmpstr(fu_backend_get_name(backend), ==, "usb");
	g_assert_true(fu_backend_get_enabled(backend));
	ret = fu_backend_setup(backend, FU_BACKEND_SETUP_FLAG_NONE, progress, &error);
	g_assert_cmpint(cnt_added, ==, 0);
	g_assert_cmpint(cnt_removed, ==, 0);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(cnt_added, ==, 0);
	g_assert_cmpint(cnt_removed, ==, 0);
	ret = fu_backend_coldplug(backend, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(cnt_added, ==, 0);
	g_assert_cmpint(cnt_removed, ==, 0);
	usb_emulate_fn = g_test_build_filename(G_TEST_DIST, "tests", "usb-devices.json", NULL);
	g_assert_nonnull(usb_emulate_fn);
	fu_backend_usb_load_file(backend, usb_emulate_fn);
	g_assert_cmpint(cnt_added, ==, 1);
	g_assert_cmpint(cnt_removed, ==, 0);
	devices = fu_backend_get_devices(backend);
	g_assert_cmpint(devices->len, ==, 1);
	device_tmp = g_ptr_array_index(devices, 0);
	fu_device_set_context(device_tmp, self->ctx);
	ret = fu_device_probe(device_tmp, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_device_has_flag(device_tmp, FWUPD_DEVICE_FLAG_EMULATED));

	/* for debugging */
	devicestr = fu_device_to_string(device_tmp);
	g_debug("%s", devicestr);

	/* check the fwupd DS20 descriptor was parsed */
	g_assert_true(fu_device_has_icon(device_tmp, "computer"));
	possible_plugins = fu_device_get_possible_plugins(device_tmp);
	g_assert_cmpint(possible_plugins->len, ==, 1);
	g_assert_cmpstr(g_ptr_array_index(possible_plugins, 0), ==, "dfu");

	/* load another device with the same VID:PID, and check that we did not get a replug */
	usb_emulate_fn2 =
	    g_test_build_filename(G_TEST_DIST, "tests", "usb-devices-replace.json", NULL);
	g_assert_nonnull(usb_emulate_fn2);
	fu_backend_usb_load_file(backend, usb_emulate_fn2);
	g_assert_cmpint(cnt_added, ==, 1);
	g_assert_cmpint(cnt_removed, ==, 0);

	/* load another device with a different VID:PID, and check that we *did* get a replug */
	usb_emulate_fn3 =
	    g_test_build_filename(G_TEST_DIST, "tests", "usb-devices-bootloader.json", NULL);
	g_assert_nonnull(usb_emulate_fn3);
	fu_backend_usb_load_file(backend, usb_emulate_fn3);
	g_assert_cmpint(cnt_added, ==, 2);
	g_assert_cmpint(cnt_removed, ==, 1);
}

static void
fu_backend_usb_invalid_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	FuDevice *device_tmp;
	g_autofree gchar *usb_emulate_fn = NULL;
	g_autoptr(FuBackend) backend = fu_usb_backend_new(self->ctx);
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(JsonParser) parser = json_parser_new();

	/* load the JSON into the backend */
	g_object_set(backend, "device-gtype", FU_TYPE_USB_DEVICE, NULL);
	usb_emulate_fn =
	    g_test_build_filename(G_TEST_DIST, "tests", "usb-devices-invalid.json", NULL);
	ret = json_parser_load_from_file(parser, usb_emulate_fn, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_backend_setup(backend, FU_BACKEND_SETUP_FLAG_NONE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fwupd_codec_from_json(FWUPD_CODEC(backend), json_parser_get_root(parser), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_backend_coldplug(backend, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	devices = fu_backend_get_devices(backend);
	g_assert_cmpint(devices->len, ==, 1);
	device_tmp = g_ptr_array_index(devices, 0);
	fu_device_set_context(device_tmp, self->ctx);

	g_test_expect_message("FuUsbDevice",
			      G_LOG_LEVEL_WARNING,
			      "*invalid platform version 0x0000000a, expected >= 0x00010805*");
	g_test_expect_message("FuUsbDevice",
			      G_LOG_LEVEL_WARNING,
			      "failed to parse * BOS descriptor: *did not find magic*");

	locker = fu_device_locker_new(device_tmp, &error);
	g_assert_no_error(error);
	g_assert_nonnull(locker);

	/* check the device was processed correctly by FuUsbDevice */
	g_assert_cmpstr(fu_device_get_name(device_tmp), ==, "ColorHug2");
	g_assert_true(fu_device_has_instance_id(device_tmp, "USB\\VID_273F&PID_1004"));
	g_assert_true(fu_device_has_vendor_id(device_tmp, "USB:0x273F"));

	/* check the fwupd DS20 descriptor was *not* parsed */
	g_assert_false(fu_device_has_icon(device_tmp, "computer"));
}

static void
fu_plugin_module_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	GError *error = NULL;
	gboolean ret;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_test_plugin_get_type(), self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* create a fake device */
	ret = fu_plugin_set_config_value(plugin, "RegistrationSupported", "true", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_signal_connect(FU_PLUGIN(plugin),
			 "device-added",
			 G_CALLBACK(fu_test_plugin_device_added_cb),
			 &device);
	g_signal_connect(FU_PLUGIN(plugin),
			 "device-register",
			 G_CALLBACK(fu_test_plugin_device_register_cb),
			 &device);
	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check we did the right thing */
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_device_get_id(device), ==, "08d460be0f1f9f128413f816022a6439e0078018");
	g_assert_cmpstr(fu_device_get_version_lowest(device), ==, "1.2.0");
	g_assert_cmpstr(fu_device_get_version(device), ==, "1.2.2");
	g_assert_cmpstr(fu_device_get_version_bootloader(device), ==, "0.1.2");
	g_assert_cmpstr(fu_device_get_guid_default(device),
			==,
			"b585990a-003e-5270-89d5-3705a17f9a43");
	g_assert_cmpstr(fu_device_get_name(device), ==, "Integrated Webcam™");
	g_signal_handlers_disconnect_by_data(plugin, &device);
}

static void
fu_history_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	GError *error = NULL;
	GPtrArray *checksums;
	gboolean ret;
	FuDevice *device;
	FuRelease *release;
	g_autoptr(FuDevice) device_found = NULL;
	g_autoptr(FuHistory) history = NULL;
	g_autoptr(GPtrArray) approved_firmware = NULL;
	g_autofree gchar *dirname = NULL;
	g_autofree gchar *filename = NULL;

#ifndef HAVE_SQLITE
	g_test_skip("no sqlite support");
	return;
#endif

	/* create */
	history = fu_history_new(self->ctx);
	g_assert_nonnull(history);

	/* delete the database */
	dirname = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);
	if (!g_file_test(dirname, G_FILE_TEST_IS_DIR))
		return;
	filename = g_build_filename(dirname, "pending.db", NULL);
	(void)g_unlink(filename);

	/* add a device */
	device = fu_device_new(self->ctx);
	fu_device_set_id(device, "self-test");
	fu_device_set_name(device, "ColorHug"),
	    fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "3.0.1"),
	    fu_device_set_update_state(device, FWUPD_UPDATE_STATE_FAILED);
	fu_device_set_update_error(device, "word");
	fu_device_add_guid(device, "827edddd-9bb6-5632-889f-2c01255503da");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_set_created_usec(device, 1514338000ull * G_USEC_PER_SEC);
	fu_device_set_modified_usec(device, 1514338999ull * G_USEC_PER_SEC);
	release = fu_release_new();
	fu_release_set_filename(release, "/var/lib/dave.cap"),
	    fu_release_add_checksum(release, "abcdef");
	fu_release_set_version(release, "3.0.2");
	fu_release_add_metadata_item(release, "FwupdVersion", VERSION);
	ret = fu_history_add_device(history, device, release, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_object_unref(release);

	/* ensure database was created */
	g_assert_true(g_file_test(filename, G_FILE_TEST_EXISTS));

	g_object_unref(device);

	/* get device */
	device = fu_history_get_device_by_id(history,
					     "2ba16d10df45823dd4494ff10a0bfccfef512c9d",
					     &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_device_get_id(device), ==, "2ba16d10df45823dd4494ff10a0bfccfef512c9d");
	g_assert_cmpstr(fu_device_get_name(device), ==, "ColorHug");
	g_assert_cmpstr(fu_device_get_version(device), ==, "3.0.1");
	g_assert_cmpint(fu_device_get_update_state(device), ==, FWUPD_UPDATE_STATE_FAILED);
	g_assert_cmpstr(fu_device_get_update_error(device), ==, "word");
	g_assert_cmpstr(fu_device_get_guid_default(device),
			==,
			"827edddd-9bb6-5632-889f-2c01255503da");
	g_assert_cmpint(fu_device_get_flags(device),
			==,
			FWUPD_DEVICE_FLAG_INTERNAL | FWUPD_DEVICE_FLAG_HISTORICAL);
	g_assert_cmpint(fu_device_get_created_usec(device), ==, 1514338000ull * G_USEC_PER_SEC);
	g_assert_cmpint(fu_device_get_modified_usec(device), ==, 1514338999ull * G_USEC_PER_SEC);
	release = FU_RELEASE(fu_device_get_release_default(device));
	g_assert_nonnull(release);
	g_assert_cmpstr(fu_release_get_version(release), ==, "3.0.2");
	g_assert_cmpstr(fu_release_get_filename(release), ==, "/var/lib/dave.cap");
	g_assert_cmpstr(fu_release_get_metadata_item(release, "FwupdVersion"), ==, VERSION);
	checksums = fu_release_get_checksums(release);
	g_assert_nonnull(checksums);
	g_assert_cmpint(checksums->len, ==, 1);
	g_assert_cmpstr(fwupd_checksum_get_by_kind(checksums, G_CHECKSUM_SHA1), ==, "abcdef");
	ret = fu_history_add_device(history, device, release, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* get device that does not exist */
	device_found = fu_history_get_device_by_id(history, "XXXXXXXXXXXXX", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(device_found);
	g_clear_error(&error);

	/* get device that does exist */
	device_found = fu_history_get_device_by_id(history,
						   "2ba16d10df45823dd4494ff10a0bfccfef512c9d",
						   &error);
	g_assert_no_error(error);
	g_assert_nonnull(device_found);
	g_object_unref(device_found);

	/* remove device */
	ret = fu_history_remove_device(history, device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_object_unref(device);

	/* get device that does not exist */
	device_found = fu_history_get_device_by_id(history,
						   "2ba16d10df45823dd4494ff10a0bfccfef512c9d",
						   &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(device_found);
	g_clear_error(&error);

	/* approved firmware */
	ret = fu_history_clear_approved_firmware(history, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_history_add_approved_firmware(history, "foo", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_history_add_approved_firmware(history, "bar", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	approved_firmware = fu_history_get_approved_firmware(history, &error);
	g_assert_no_error(error);
	g_assert_nonnull(approved_firmware);
	g_assert_cmpint(approved_firmware->len, ==, 2);
	g_assert_cmpstr(g_ptr_array_index(approved_firmware, 0), ==, "foo");
	g_assert_cmpstr(g_ptr_array_index(approved_firmware, 1), ==, "bar");
}

static GBytes *
fu_test_build_cab(gboolean compressed, ...)
{
	va_list args;
	g_autoptr(FuCabFirmware) cabinet = fu_cab_firmware_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) cabinet_blob = NULL;

	fu_cab_firmware_set_compressed(cabinet, compressed);

	/* add each file */
	va_start(args, compressed);
	do {
		const gchar *fn;
		const gchar *text;
		g_autoptr(FuCabImage) img = fu_cab_image_new();
		g_autoptr(GBytes) blob = NULL;

		/* get filename */
		fn = va_arg(args, const gchar *);
		if (fn == NULL)
			break;

		/* get contents */
		text = va_arg(args, const gchar *);
		if (text == NULL)
			break;
		g_debug("creating %s with %s", fn, text);

		/* add a GCabFile to the cabinet */
		blob = g_bytes_new_static(text, strlen(text));
		fu_firmware_set_id(FU_FIRMWARE(img), fn);
		fu_firmware_set_bytes(FU_FIRMWARE(img), blob);
		fu_firmware_add_image(FU_FIRMWARE(cabinet), FU_FIRMWARE(img));
	} while (TRUE);
	va_end(args);

	/* write the archive to a blob */
	cabinet_blob = fu_firmware_write(FU_FIRMWARE(cabinet), &error);
	g_assert_no_error(error);
	g_assert_nonnull(cabinet_blob);
	return g_steal_pointer(&cabinet_blob);
}

static void
fu_test_plugin_composite_device_added_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	GPtrArray *devices = (GPtrArray *)user_data;
	g_ptr_array_add(devices, g_object_ref(device));
}

static gint
fu_plugin_composite_release_sort_cb(gconstpointer a, gconstpointer b)
{
	FuRelease *release1 = *((FuRelease **)a);
	FuRelease *release2 = *((FuRelease **)b);
	FuDevice *device1 = fu_release_get_device(release1);
	FuDevice *device2 = fu_release_get_device(release2);
	if (fu_device_get_order(device1) < fu_device_get_order(device2))
		return 1;
	if (fu_device_get_order(device1) > fu_device_get_order(device2))
		return -1;
	return 0;
}

static void
fu_plugin_composite_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	FuDevice *dev_tmp;
	GError *error = NULL;
	gboolean ret;
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_test_plugin_get_type(), self->ctx);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GPtrArray) components = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) releases =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(XbSilo) silo_empty = xb_silo_new();

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* create CAB file */
	blob = fu_test_build_cab(
	    FALSE,
	    "acme.metainfo.xml",
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.example.firmware</id>\n"
	    "  <provides>\n"
	    "    <firmware type=\"flashed\">b585990a-003e-5270-89d5-3705a17f9a43</firmware>\n"
	    "  </provides>\n"
	    "  <releases>\n"
	    "    <release version=\"1.2.3\"/>\n"
	    "  </releases>\n"
	    "</component>",
	    "acme.module1.metainfo.xml",
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.example.firmware.module1</id>\n"
	    "  <provides>\n"
	    "    <firmware type=\"flashed\">7fddead7-12b5-4fb9-9fa0-6d30305df755</firmware>\n"
	    "  </provides>\n"
	    "  <releases>\n"
	    "    <release version=\"2\"/>\n"
	    "  </releases>\n"
	    "  <custom>\n"
	    "    <value key=\"LVFS::VersionFormat\">plain</value>\n"
	    "  </custom>\n"
	    "</component>",
	    "acme.module2.metainfo.xml",
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.example.firmware.module2</id>\n"
	    "  <provides>\n"
	    "    <firmware type=\"flashed\">b8fe6b45-8702-4bcd-8120-ef236caac76f</firmware>\n"
	    "  </provides>\n"
	    "  <releases>\n"
	    "    <release version=\"11\"/>\n"
	    "  </releases>\n"
	    "  <custom>\n"
	    "    <value key=\"LVFS::VersionFormat\">plain</value>\n"
	    "  </custom>\n"
	    "</component>",
	    "firmware.bin",
	    "world",
	    NULL);
	ret = fu_firmware_parse(FU_FIRMWARE(cabinet), blob, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	components = fu_cabinet_get_components(cabinet, &error);
	g_assert_no_error(error);
	g_assert_nonnull(components);
	g_assert_cmpint(components->len, ==, 3);

	/* set up dummy plugin */
	ret = fu_plugin_reset_config_values(plugin, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_set_config_value(plugin, "CompositeChild", "true", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_engine_add_plugin(engine, plugin);

	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	g_signal_connect(FU_PLUGIN(plugin),
			 "device-added",
			 G_CALLBACK(fu_test_plugin_composite_device_added_cb),
			 devices);

	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check we found all composite devices  */
	g_assert_cmpint(devices->len, ==, 3);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		fu_engine_add_device(engine, device);
		if (g_strcmp0(fu_device_get_id(device),
			      "08d460be0f1f9f128413f816022a6439e0078018") == 0) {
			g_assert_cmpstr(fu_device_get_version(device), ==, "1.2.2");
		} else if (g_strcmp0(fu_device_get_id(device),
				     "c0a0a4aa6480ac28eea1ce164fbb466ca934e1ff") == 0) {
			g_assert_cmpstr(fu_device_get_version(device), ==, "1");
			g_assert_nonnull(fu_device_get_parent(device));
		} else if (g_strcmp0(fu_device_get_id(device),
				     "bf455e9f371d2608d1cb67660fd2b335d3f6ef73") == 0) {
			g_assert_cmpstr(fu_device_get_version(device), ==, "10");
			g_assert_nonnull(fu_device_get_parent(device));
		}
	}

	/* produce install tasks */
	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index(components, i);

		/* do any devices pass the requirements */
		for (guint j = 0; j < devices->len; j++) {
			FuDevice *device = g_ptr_array_index(devices, j);
			g_autoptr(FuRelease) release = fu_release_new();
			g_autoptr(GError) error_local = NULL;

			/* is this component valid for the device */
			fu_release_set_device(release, device);
			fu_release_set_request(release, request);
			if (!fu_release_load(release,
					     cabinet,
					     component,
					     NULL,
					     FWUPD_INSTALL_FLAG_NONE,
					     &error_local)) {
				g_debug("requirement on %s:%s failed: %s",
					fu_device_get_id(device),
					xb_node_query_text(component, "id", NULL),
					error_local->message);
				continue;
			}

			g_ptr_array_add(releases, g_steal_pointer(&release));
		}
	}
	g_assert_cmpint(releases->len, ==, 3);

	/* sort these by version, forcing fu_engine_install_releases() to sort by device order */
	g_ptr_array_sort(releases, fu_plugin_composite_release_sort_cb);
	dev_tmp = fu_release_get_device(FU_RELEASE(g_ptr_array_index(releases, 0)));
	g_assert_cmpstr(fu_device_get_logical_id(dev_tmp), ==, "child1");
	dev_tmp = fu_release_get_device(FU_RELEASE(g_ptr_array_index(releases, 1)));
	g_assert_cmpstr(fu_device_get_logical_id(dev_tmp), ==, "child2");
	dev_tmp = fu_release_get_device(FU_RELEASE(g_ptr_array_index(releases, 2)));
	g_assert_cmpstr(fu_device_get_logical_id(dev_tmp), ==, NULL);

	/* install the cab */
	ret = fu_engine_install_releases(engine,
					 request,
					 releases,
					 cabinet,
					 progress,
					 FWUPD_INSTALL_FLAG_NONE,
					 &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* verify we installed the parent first */
	dev_tmp = fu_release_get_device(FU_RELEASE(g_ptr_array_index(releases, 0)));
	g_assert_cmpstr(fu_device_get_logical_id(dev_tmp), ==, NULL);
	dev_tmp = fu_release_get_device(FU_RELEASE(g_ptr_array_index(releases, 1)));
	g_assert_cmpstr(fu_device_get_logical_id(dev_tmp), ==, "child2");
	dev_tmp = fu_release_get_device(FU_RELEASE(g_ptr_array_index(releases, 2)));
	g_assert_cmpstr(fu_device_get_logical_id(dev_tmp), ==, "child1");

	/* verify everything upgraded */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		const gchar *metadata;
		if (g_strcmp0(fu_device_get_id(device),
			      "08d460be0f1f9f128413f816022a6439e0078018") == 0) {
			g_assert_cmpstr(fu_device_get_version(device), ==, "1.2.3");
		} else if (g_strcmp0(fu_device_get_id(device),
				     "c0a0a4aa6480ac28eea1ce164fbb466ca934e1ff") == 0) {
			g_assert_cmpstr(fu_device_get_version(device), ==, "2");
		} else if (g_strcmp0(fu_device_get_id(device),
				     "bf455e9f371d2608d1cb67660fd2b335d3f6ef73") == 0) {
			g_assert_cmpstr(fu_device_get_version(device), ==, "11");
		}

		/* verify prepare and cleanup ran on all devices */
		metadata = fu_device_get_metadata(device, "frimbulator");
		g_assert_cmpstr(metadata, ==, "1");
		metadata = fu_device_get_metadata(device, "frombulator");
		g_assert_cmpstr(metadata, ==, "1");
	}
}

static void
fu_security_attr_func(gconstpointer user_data)
{
	gboolean ret;
	g_autofree gchar *json1 = NULL;
	g_autofree gchar *json2 = NULL;
	g_autoptr(FuSecurityAttrs) attrs1 = fu_security_attrs_new();
	g_autoptr(FuSecurityAttrs) attrs2 = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr1 = fwupd_security_attr_new("org.fwupd.hsi.foo");
	g_autoptr(FwupdSecurityAttr) attr2 = fwupd_security_attr_new("org.fwupd.hsi.bar");
	g_autoptr(GError) error = NULL;

	fwupd_security_attr_set_plugin(attr1, "foo");
	fwupd_security_attr_set_created(attr1, 0);
	fwupd_security_attr_set_plugin(attr2, "bar");
	fwupd_security_attr_set_created(attr2, 0);
	fu_security_attrs_append(attrs1, attr1);
	fu_security_attrs_append(attrs1, attr2);

	json1 = fwupd_codec_to_json_string(FWUPD_CODEC(attrs1), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json1);
	ret = fu_test_compare_lines(
	    json1,
	    "{\n"
	    "  \"SecurityAttributes\" : [\n"
	    "    {\n"
	    "      \"AppstreamId\" : \"org.fwupd.hsi.foo\",\n"
	    "      \"HsiLevel\" : 0,\n"
	    "      \"Plugin\" : \"foo\",\n"
	    "      \"Uri\" : "
	    "\"https://fwupd.github.io/libfwupdplugin/hsi.html#org.fwupd.hsi.foo\"\n"
	    "    },\n"
	    "    {\n"
	    "      \"AppstreamId\" : \"org.fwupd.hsi.bar\",\n"
	    "      \"HsiLevel\" : 0,\n"
	    "      \"Plugin\" : \"bar\",\n"
	    "      \"Uri\" : "
	    "\"https://fwupd.github.io/libfwupdplugin/hsi.html#org.fwupd.hsi.bar\"\n"
	    "    }\n"
	    "  ]\n"
	    "}",
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fwupd_codec_from_json_string(FWUPD_CODEC(attrs2), json1, &error);
	if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip(error->message);
		return;
	}
	g_assert_no_error(error);
	g_assert_true(ret);

	json2 = fwupd_codec_to_json_string(FWUPD_CODEC(attrs2), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json2);
	ret = fu_test_compare_lines(json2, json1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_common_cabinet_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(FuFirmware) img1 = NULL;
	g_autoptr(FuFirmware) img2 = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) jcat_blob1 = g_bytes_new_static("hello", 6);
	g_autoptr(GBytes) jcat_blob2 = g_bytes_new_static("hellX", 6);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;

	filename = g_test_build_filename(G_TEST_BUILT,
					 "tests",
					 "multiple-rels",
					 "multiple-rels-1.2.4.cab",
					 NULL);
	stream = fu_input_stream_from_path(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	ret = fu_firmware_parse_stream(FU_FIRMWARE(cabinet),
				       stream,
				       0x0,
				       FWUPD_INSTALL_FLAG_NONE,
				       &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add */
	fu_cabinet_add_file(cabinet, "firmware.jcat", jcat_blob1);

	/* replace */
	fu_cabinet_add_file(cabinet, "firmware.jcat", jcat_blob2);

	/* get data */
	img1 = fu_firmware_get_image_by_id(FU_FIRMWARE(cabinet), "firmware.jcat", &error);
	g_assert_no_error(error);
	g_assert_nonnull(img1);
	blob = fu_firmware_get_bytes(FU_FIRMWARE(img1), &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	g_assert_cmpstr(g_bytes_get_data(blob, NULL), ==, "hellX");

	/* get data that does not exist */
	img2 = fu_firmware_get_image_by_id(FU_FIRMWARE(cabinet), "foo.jcat", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(img2);
}

static void
fu_memcpy_func(gconstpointer user_data)
{
	const guint8 src[] = {'a', 'b', 'c', 'd', 'e'};
	gboolean ret;
	guint8 dst[4];
	g_autoptr(GError) error = NULL;

	/* copy entire buffer */
	ret = fu_memcpy_safe(dst, sizeof(dst), 0x0, src, sizeof(src), 0x0, 4, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(memcmp(src, dst, 4), ==, 0);

	/* copy first char */
	ret = fu_memcpy_safe(dst, sizeof(dst), 0x0, src, sizeof(src), 0x0, 1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(dst[0], ==, 'a');

	/* copy last char */
	ret = fu_memcpy_safe(dst, sizeof(dst), 0x0, src, sizeof(src), 0x4, 1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(dst[0], ==, 'e');

	/* copy nothing */
	ret = fu_memcpy_safe(dst, sizeof(dst), 0x0, src, sizeof(src), 0x0, 0, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* write past the end of dst */
	ret = fu_memcpy_safe(dst, sizeof(dst), 0x0, src, sizeof(src), 0x0, 5, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_WRITE);
	g_assert_false(ret);
	g_clear_error(&error);

	/* write past the end of dst with offset */
	ret = fu_memcpy_safe(dst, sizeof(dst), 0x1, src, sizeof(src), 0x0, 4, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_WRITE);
	g_assert_false(ret);
	g_clear_error(&error);

	/* read past the end of dst */
	ret = fu_memcpy_safe(dst, sizeof(dst), 0x0, src, sizeof(src), 0x0, 6, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_READ);
	g_assert_false(ret);
	g_clear_error(&error);

	/* read past the end of src with offset */
	ret = fu_memcpy_safe(dst, sizeof(dst), 0x0, src, sizeof(src), 0x4, 4, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_READ);
	g_assert_false(ret);
	g_clear_error(&error);
}

static void
fu_console_func(gconstpointer user_data)
{
	g_autoptr(FuConsole) console = fu_console_new();

	fu_console_set_status_length(console, 20);
	fu_console_set_percentage_length(console, 50);

	g_print("\n");
	for (guint i = 0; i < 100; i++) {
		fu_console_set_progress(console, FWUPD_STATUS_DECOMPRESSING, i);
		g_usleep(10000);
	}
	fu_console_set_progress(console, FWUPD_STATUS_IDLE, 0);
	for (guint i = 0; i < 100; i++) {
		guint pc = (i > 25 && i < 75) ? 0 : i;
		fu_console_set_progress(console, FWUPD_STATUS_LOADING, pc);
		g_usleep(10000);
	}
	fu_console_set_progress(console, FWUPD_STATUS_IDLE, 0);

	for (guint i = 0; i < 5000; i++) {
		fu_console_set_progress(console, FWUPD_STATUS_LOADING, 0);
		g_usleep(1000);
	}
	fu_console_set_progress(console, FWUPD_STATUS_IDLE, 0);
}

static gint
fu_release_compare_func_cb(gconstpointer a, gconstpointer b)
{
	FuRelease *release1 = *((FuRelease **)a);
	FuRelease *release2 = *((FuRelease **)b);
	return fu_release_compare(release1, release2);
}

static void
fu_release_compare_func(gconstpointer user_data)
{
	g_autoptr(GPtrArray) releases = g_ptr_array_new();
	g_autoptr(FuDevice) device1 = fu_device_new(NULL);
	g_autoptr(FuDevice) device2 = fu_device_new(NULL);
	g_autoptr(FuDevice) device3 = fu_device_new(NULL);
	g_autoptr(FuRelease) release1 = fu_release_new();
	g_autoptr(FuRelease) release2 = fu_release_new();
	g_autoptr(FuRelease) release3 = fu_release_new();

	fu_device_set_order(device1, 33);
	fu_release_set_device(release1, device1);
	fu_release_set_priority(release1, 0);
	fu_release_set_branch(release1, "1");

	fu_device_set_order(device2, 11);
	fu_release_set_device(release2, device2);
	fu_release_set_priority(release2, 0);
	fu_release_set_branch(release2, "2");

	fu_device_set_order(device3, 11);
	fu_release_set_device(release3, device3);
	fu_release_set_priority(release3, 99);
	fu_release_set_branch(release3, "3");

	g_ptr_array_add(releases, release1);
	g_ptr_array_add(releases, release2);
	g_ptr_array_add(releases, release3);

	/* order the install tasks */
	g_ptr_array_sort(releases, fu_release_compare_func_cb);
	g_assert_cmpint(releases->len, ==, 3);
	g_assert_cmpstr(fu_release_get_branch(g_ptr_array_index(releases, 0)), ==, "3");
	g_assert_cmpstr(fu_release_get_branch(g_ptr_array_index(releases, 1)), ==, "2");
	g_assert_cmpstr(fu_release_get_branch(g_ptr_array_index(releases, 2)), ==, "1");
}

static void
fu_release_uri_scheme_func(void)
{
	struct {
		const gchar *in;
		const gchar *op;
	} strs[] = {{"https://foo.bar/baz", "https"},
		    {"HTTP://FOO.BAR/BAZ", "http"},
		    {"ftp://", "ftp"},
		    {"ftp:", "ftp"},
		    {"foobarbaz", NULL},
		    {"", NULL},
		    {NULL, NULL}};
	for (guint i = 0; strs[i].in != NULL; i++) {
		g_autofree gchar *tmp = fu_release_uri_get_scheme(strs[i].in);
		g_assert_cmpstr(tmp, ==, strs[i].op);
	}
}

static void
fu_release_trusted_report_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	FwupdRelease *rel;
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FwupdRemote) remote = fwupd_remote_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderNode) custom = xb_builder_node_new("custom");
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);

	/* add fake LVFS remote */
	fwupd_remote_set_id(remote, "lvfs");
	fu_engine_add_remote(engine, remote);

	/* load engine to get FuConfig set up */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* metadata with vendor id=123 */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "metadata-report1.xml", NULL);
	file = g_file_new_for_path(filename);
	ret = xb_builder_source_load_file(source, file, XB_BUILDER_SOURCE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	xb_builder_node_insert_text(custom, "value", "lvfs", "key", "fwupd::RemoteId", NULL);
	xb_builder_source_set_info(source, custom);
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	fu_engine_set_silo(engine, silo);

	/* add a dummy device */
	fu_device_set_id(device, "dummy");
	fu_device_set_version(device, "1.2.2");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_protocol(device, "com.acme");
	fu_device_add_guid(device, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_engine_add_device(engine, device);

	/* ensure we set this as trusted */
	releases = fu_engine_get_releases_for_device(engine, request, device, &error);
	g_assert_no_error(error);
	g_assert_nonnull(releases);
	g_assert_cmpint(releases->len, ==, 1);
	rel = g_ptr_array_index(releases, 0);
	g_assert_true(fwupd_release_has_flag(rel, FWUPD_RELEASE_FLAG_TRUSTED_REPORT));
}

static void
fu_release_trusted_report_oem_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	FwupdRelease *rel;
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);

	/* load engine to get FuConfig set up */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* metadata with FromOEM */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "metadata-report2.xml", NULL);
	file = g_file_new_for_path(filename);
	ret = xb_builder_source_load_file(source, file, XB_BUILDER_SOURCE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	fu_engine_set_silo(engine, silo);

	/* add a dummy device */
	fu_device_set_id(device, "dummy");
	fu_device_set_version(device, "1.2.2");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_protocol(device, "com.acme");
	fu_device_add_guid(device, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_engine_add_device(engine, device);

	/* ensure we set this as trusted */
	releases = fu_engine_get_releases_for_device(engine, request, device, &error);
	g_assert_no_error(error);
	g_assert_nonnull(releases);
	g_assert_cmpint(releases->len, ==, 1);
	rel = g_ptr_array_index(releases, 0);
	g_assert_true(fwupd_release_has_flag(rel, FWUPD_RELEASE_FLAG_TRUSTED_REPORT));
}

static void
fu_release_no_trusted_report_upgrade_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	FwupdRelease *rel;
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);

	/* load engine to get FuConfig set up */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* metadata with FromOEM, but *NOT* an upgrade */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "metadata-report4.xml", NULL);
	file = g_file_new_for_path(filename);
	ret = xb_builder_source_load_file(source, file, XB_BUILDER_SOURCE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	fu_engine_set_silo(engine, silo);

	/* add a dummy device */
	fu_device_set_id(device, "dummy");
	fu_device_set_version(device, "1.2.3");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_protocol(device, "com.acme");
	fu_device_add_guid(device, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_engine_add_device(engine, device);

	/* ensure we set this as trusted */
	releases = fu_engine_get_releases_for_device(engine, request, device, &error);
	g_assert_no_error(error);
	g_assert_nonnull(releases);
	g_assert_cmpint(releases->len, ==, 1);
	rel = g_ptr_array_index(releases, 0);
	g_assert_false(fwupd_release_has_flag(rel, FWUPD_RELEASE_FLAG_TRUSTED_REPORT));
}

static void
fu_release_no_trusted_report_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	FwupdRelease *rel;
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuDevice) device = fu_device_new(self->ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);

	/* load engine to get FuConfig set up */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* metadata without OEM or valid VendorId as per tests/fwupd.conf */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "metadata-report3.xml", NULL);
	file = g_file_new_for_path(filename);
	ret = xb_builder_source_load_file(source, file, XB_BUILDER_SOURCE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	fu_engine_set_silo(engine, silo);

	/* add a dummy device */
	fu_device_set_id(device, "dummy");
	fu_device_set_version(device, "1.2.2");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_protocol(device, "com.acme");
	fu_device_add_guid(device, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_engine_add_device(engine, device);

	/* ensure trusted reports flag is not set */
	releases = fu_engine_get_releases_for_device(engine, request, device, &error);
	g_assert_no_error(error);
	g_assert_nonnull(releases);
	g_assert_cmpint(releases->len, ==, 1);
	rel = g_ptr_array_index(releases, 0);
	g_assert_false(fwupd_release_has_flag(rel, FWUPD_RELEASE_FLAG_TRUSTED_REPORT));
}

static void
fu_common_store_cab_func(void)
{
	gboolean ret;
	GBytes *blob_tmp;
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbNode) csum = NULL;
	g_autoptr(XbNode) rel = NULL;
	g_autoptr(XbNode) req = NULL;
	g_autoptr(XbQuery) query = NULL;

	/* create silo */
	blob = fu_test_build_cab(
	    FALSE,
	    "acme.metainfo.xml",
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.example.firmware</id>\n"
	    "  <name>ACME Firmware</name>\n"
	    "  <provides>\n"
	    "    <firmware type=\"flashed\">ae56e3fb-6528-5bc4-8b03-012f124075d7</firmware>\n"
	    "  </provides>\n"
	    "  <releases>\n"
	    "    <release version=\"1.2.3\" date=\"2017-09-06\">\n"
	    "      <size type=\"installed\">5</size>\n"
	    "      <checksum filename=\"firmware.dfu\" target=\"content\" "
	    "type=\"sha1\">7c211433f02071597741e6ff5a8ea34789abbf43</checksum>\n"
	    "      <description><p>We fixed things</p></description>\n"
	    "    </release>\n"
	    "  </releases>\n"
	    "  <requires>\n"
	    "    <id compare=\"ge\" version=\"1.0.1\">org.freedesktop.fwupd</id>\n"
	    "  </requires>\n"
	    "</component>",
	    "firmware.dfu",
	    "world",
	    "firmware.dfu.asc",
	    "signature",
	    NULL);
	ret = fu_firmware_parse(FU_FIRMWARE(cabinet), blob, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* verify */
	component = fu_cabinet_get_component(cabinet, "com.acme.example.firmware", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);
	query = xb_query_new_full(xb_node_get_silo(component),
				  "releases/release",
				  XB_QUERY_FLAG_FORCE_NODE_CACHE,
				  &error);
	g_assert_no_error(error);
	g_assert_nonnull(query);
	rel = xb_node_query_first_full(component, query, &error);
	g_assert_no_error(error);
	g_assert_nonnull(rel);
	g_assert_cmpstr(xb_node_get_attr(rel, "version"), ==, "1.2.3");
	csum = xb_node_query_first(rel, "checksum[@target='content']", &error);
	g_assert_nonnull(csum);
	g_assert_cmpstr(xb_node_get_text(csum), ==, "7c211433f02071597741e6ff5a8ea34789abbf43");
	blob_tmp = xb_node_get_data(rel, "fwupd::FirmwareBasename");
	g_assert_nonnull(blob_tmp);
	req = xb_node_query_first(component, "requires/id", &error);
	g_assert_no_error(error);
	g_assert_nonnull(req);
}

static void
fu_common_store_cab_artifact_func(void)
{
	gboolean ret;
	g_autoptr(GBytes) blob1 = NULL;
	g_autoptr(GBytes) blob2 = NULL;
	g_autoptr(GBytes) blob3 = NULL;
	g_autoptr(GBytes) blob4 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuCabinet) cabinet1 = fu_cabinet_new();
	g_autoptr(FuCabinet) cabinet2 = fu_cabinet_new();
	g_autoptr(FuCabinet) cabinet3 = fu_cabinet_new();
	g_autoptr(FuCabinet) cabinet4 = fu_cabinet_new();

	/* create silo (sha256, using artifacts object) */
	blob1 = fu_test_build_cab(
	    FALSE,
	    "acme.metainfo.xml",
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.example.firmware</id>\n"
	    "  <releases>\n"
	    "    <release version=\"1.2.3\" date=\"2017-09-06\">\n"
	    "      <artifacts>\n"
	    "        <artifact type=\"source\">\n"
	    "          <filename>firmware.dfu</filename>\n"
	    "          <checksum "
	    "type=\"sha256\">486EA46224D1BB4FB680F34F7C9AD96A8F24EC88BE73EA8E5A6C65260E9CB8A7</"
	    "checksum>\n"
	    "        </artifact>\n"
	    "      </artifacts>\n"
	    "    </release>\n"
	    "  </releases>\n"
	    "</component>",
	    "firmware.dfu",
	    "world",
	    "firmware.dfu.asc",
	    "signature",
	    NULL);
	ret = fu_firmware_parse(FU_FIRMWARE(cabinet1), blob1, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create silo (sha1, using artifacts object; mixed case) */
	blob2 = fu_test_build_cab(FALSE,
				  "acme.metainfo.xml",
				  "<component type=\"firmware\">\n"
				  "  <id>com.acme.example.firmware</id>\n"
				  "  <releases>\n"
				  "    <release version=\"1.2.3\" date=\"2017-09-06\">\n"
				  "      <artifacts>\n"
				  "        <artifact type=\"source\">\n"
				  "          <filename>firmware.dfu</filename>\n"
				  "          <checksum "
				  "type=\"sha1\">7c211433f02071597741e6ff5a8ea34789abbF43</"
				  "checksum>\n"
				  "        </artifact>\n"
				  "      </artifacts>\n"
				  "    </release>\n"
				  "  </releases>\n"
				  "</component>",
				  "firmware.dfu",
				  "world",
				  "firmware.dfu.asc",
				  "signature",
				  NULL);
	ret = fu_firmware_parse(FU_FIRMWARE(cabinet2), blob2, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create silo (sha512, using artifacts object; lower case) */
	blob3 = fu_test_build_cab(
	    FALSE,
	    "acme.metainfo.xml",
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.example.firmware</id>\n"
	    "  <releases>\n"
	    "    <release version=\"1.2.3\" date=\"2017-09-06\">\n"
	    "      <artifacts>\n"
	    "        <artifact type=\"source\">\n"
	    "          <filename>firmware.dfu</filename>\n"
	    "          <checksum "
	    "type=\"sha512\">"
	    "11853df40f4b2b919d3815f64792e58d08663767a494bcbb38c0b2389d9140bbb170281b"
	    "4a847be7757bde12c9cd0054ce3652d0ad3a1a0c92babb69798246ee</"
	    "checksum>\n"
	    "        </artifact>\n"
	    "      </artifacts>\n"
	    "    </release>\n"
	    "  </releases>\n"
	    "</component>",
	    "firmware.dfu",
	    "world",
	    "firmware.dfu.asc",
	    "signature",
	    NULL);
	ret = fu_firmware_parse(FU_FIRMWARE(cabinet3), blob3, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create silo (legacy release object) */
	blob4 =
	    fu_test_build_cab(FALSE,
			      "acme.metainfo.xml",
			      "<component type=\"firmware\">\n"
			      "  <id>com.acme.example.firmware</id>\n"
			      "  <releases>\n"
			      "    <release version=\"1.2.3\" date=\"2017-09-06\">\n"
			      "        <checksum "
			      "target=\"content\" "
			      "filename=\"firmware.dfu\">"
			      "486EA46224D1BB4FB680F34F7C9AD96A8F24EC88BE73EA8E5A6C65260E9CB8A7</"
			      "checksum>\n"
			      "    </release>\n"
			      "  </releases>\n"
			      "</component>",
			      "firmware.dfu",
			      "world",
			      "firmware.dfu.asc",
			      "signature",
			      NULL);
	ret = fu_firmware_parse(FU_FIRMWARE(cabinet4), blob4, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_common_store_cab_unsigned_func(void)
{
	GBytes *blob_tmp;
	gboolean ret;
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbNode) csum = NULL;
	g_autoptr(XbNode) rel = NULL;
	g_autoptr(XbQuery) query = NULL;

	/* create silo */
	blob = fu_test_build_cab(FALSE,
				 "acme.metainfo.xml",
				 "<component type=\"firmware\">\n"
				 "  <id>com.acme.example.firmware</id>\n"
				 "  <releases>\n"
				 "    <release version=\"1.2.3\"/>\n"
				 "  </releases>\n"
				 "</component>",
				 "firmware.bin",
				 "world",
				 NULL);
	ret = fu_firmware_parse(FU_FIRMWARE(cabinet), blob, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* verify */
	component = fu_cabinet_get_component(cabinet, "com.acme.example.firmware", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);
	query = xb_query_new_full(xb_node_get_silo(component),
				  "releases/release",
				  XB_QUERY_FLAG_FORCE_NODE_CACHE,
				  &error);
	g_assert_no_error(error);
	g_assert_nonnull(query);
	rel = xb_node_query_first_full(component, query, &error);
	g_assert_no_error(error);
	g_assert_nonnull(rel);
	g_assert_cmpstr(xb_node_get_attr(rel, "version"), ==, "1.2.3");
	csum = xb_node_query_first(rel, "checksum[@target='content']", &error);
	g_assert_null(csum);
	blob_tmp = xb_node_get_data(rel, "fwupd::FirmwareBasename");
	g_assert_nonnull(blob_tmp);
}

static void
fu_common_store_cab_sha256_func(void)
{
	gboolean ret;
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	/* create silo */
	blob = fu_test_build_cab(
	    FALSE,
	    "acme.metainfo.xml",
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.example.firmware</id>\n"
	    "  <releases>\n"
	    "    <release version=\"1.2.3\" date=\"2017-09-06\">\n"
	    "      <checksum target=\"content\" "
	    "type=\"sha256\">486ea46224d1bb4fb680f34f7c9ad96a8f24ec88be73ea8e5a6c65260e9cb8a7</"
	    "checksum>\n"
	    "    </release>\n"
	    "  </releases>\n"
	    "</component>",
	    "firmware.bin",
	    "world",
	    NULL);
	ret = fu_firmware_parse(FU_FIRMWARE(cabinet), blob, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_common_store_cab_folder_func(void)
{
	GBytes *blob_tmp;
	gboolean ret;
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbNode) rel = NULL;
	g_autoptr(XbQuery) query = NULL;

	/* create silo */
	blob = fu_test_build_cab(FALSE,
				 "lvfs\\acme.metainfo.xml",
				 "<component type=\"firmware\">\n"
				 "  <id>com.acme.example.firmware</id>\n"
				 "  <releases>\n"
				 "    <release version=\"1.2.3\"/>\n"
				 "  </releases>\n"
				 "</component>",
				 "lvfs\\firmware.bin",
				 "world",
				 NULL);
	ret = fu_firmware_parse(FU_FIRMWARE(cabinet), blob, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* verify */
	component = fu_cabinet_get_component(cabinet, "com.acme.example.firmware", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);
	query = xb_query_new_full(xb_node_get_silo(component),
				  "releases/release",
				  XB_QUERY_FLAG_FORCE_NODE_CACHE,
				  &error);
	g_assert_no_error(error);
	g_assert_nonnull(query);
	rel = xb_node_query_first_full(component, query, &error);
	g_assert_no_error(error);
	g_assert_nonnull(rel);
	g_assert_cmpstr(xb_node_get_attr(rel, "version"), ==, "1.2.3");
	blob_tmp = xb_node_get_data(rel, "fwupd::FirmwareBasename");
	g_assert_nonnull(blob_tmp);
}

static void
fu_common_store_cab_error_no_metadata_func(void)
{
	gboolean ret;
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	blob = fu_test_build_cab(FALSE, "foo.txt", "hello", "bar.txt", "world", NULL);
	ret = fu_firmware_parse(FU_FIRMWARE(cabinet), blob, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_false(ret);
}

static void
fu_common_store_cab_error_wrong_size_func(void)
{
	gboolean ret;
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	blob = fu_test_build_cab(FALSE,
				 "acme.metainfo.xml",
				 "<component type=\"firmware\">\n"
				 "  <id>com.acme.example.firmware</id>\n"
				 "  <releases>\n"
				 "    <release version=\"1.2.3\">\n"
				 "      <size type=\"installed\">7004701</size>\n"
				 "      <checksum filename=\"firmware.bin\" target=\"content\" "
				 "type=\"sha1\">deadbeef</checksum>\n"
				 "    </release>\n"
				 "  </releases>\n"
				 "</component>",
				 "firmware.bin",
				 "world",
				 NULL);
	ret = fu_firmware_parse(FU_FIRMWARE(cabinet), blob, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_false(ret);
}

static void
fu_common_store_cab_error_missing_file_func(void)
{
	gboolean ret;
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	blob = fu_test_build_cab(FALSE,
				 "acme.metainfo.xml",
				 "<component type=\"firmware\">\n"
				 "  <id>com.acme.example.firmware</id>\n"
				 "  <releases>\n"
				 "    <release version=\"1.2.3\">\n"
				 "      <checksum filename=\"firmware.dfu\" target=\"content\"/>\n"
				 "    </release>\n"
				 "  </releases>\n"
				 "</component>",
				 "firmware.bin",
				 "world",
				 NULL);
	ret = fu_firmware_parse(FU_FIRMWARE(cabinet), blob, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_false(ret);
}

static void
fu_common_store_cab_error_size_func(void)
{
	gboolean ret;
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	blob = fu_test_build_cab(FALSE,
				 "acme.metainfo.xml",
				 "<component type=\"firmware\">\n"
				 "  <id>com.acme.example.firmware</id>\n"
				 "  <releases>\n"
				 "    <release version=\"1.2.3\"/>\n"
				 "  </releases>\n"
				 "</component>",
				 "firmware.bin",
				 "world",
				 NULL);
	fu_firmware_set_size_max(FU_FIRMWARE(cabinet), 123);
	ret = fu_firmware_parse(FU_FIRMWARE(cabinet), blob, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_false(ret);
}

static void
fu_common_store_cab_error_wrong_checksum_func(void)
{
	gboolean ret;
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	blob = fu_test_build_cab(FALSE,
				 "acme.metainfo.xml",
				 "<component type=\"firmware\">\n"
				 "  <id>com.acme.example.firmware</id>\n"
				 "  <releases>\n"
				 "    <release version=\"1.2.3\">\n"
				 "      <checksum filename=\"firmware.bin\" target=\"content\" "
				 "type=\"sha1\">deadbeef</checksum>\n"
				 "    </release>\n"
				 "  </releases>\n"
				 "</component>",
				 "firmware.bin",
				 "world",
				 NULL);
	ret = fu_firmware_parse(FU_FIRMWARE(cabinet), blob, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_false(ret);
}

static void
fu_engine_modify_bios_settings_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	const gchar *current;
	FwupdBiosSetting *attr1;
	FwupdBiosSetting *attr2;
	FwupdBiosSetting *attr3;
	FwupdBiosSetting *attr4;
	g_autofree gchar *test_dir = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuBiosSettings) attrs = NULL;
	g_autoptr(GPtrArray) items = NULL;
	g_autoptr(GHashTable) bios_settings =
	    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

#ifdef _WIN32
	g_test_skip("BIOS settings not supported on Windows");
	return;
#endif

	/* Load contrived attributes */
	test_dir = g_test_build_filename(G_TEST_DIST, "tests", "bios-attrs", NULL);
	(void)g_setenv("FWUPD_SYSFSFWATTRIBDIR", test_dir, TRUE);

	ret = fu_context_reload_bios_settings(fu_engine_get_context(engine), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	attrs = fu_context_get_bios_settings(fu_engine_get_context(engine));
	items = fu_bios_settings_get_all(attrs);
	g_assert_cmpint(items->len, ==, 4);

	/* enumeration */
	attr1 = fu_context_get_bios_setting(fu_engine_get_context(engine),
					    "com.fwupd-internal.Absolute");
	g_assert_nonnull(attr1);

	current = fwupd_bios_setting_get_current_value(attr1);
	g_assert_nonnull(current);

	g_hash_table_insert(bios_settings, g_strdup("Absolute"), g_strdup("Disabled"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO);
	g_assert_false(ret);
	g_clear_error(&error);

	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings, g_strdup("Absolute"), g_strdup("Enabled"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings, g_strdup("Absolute"), g_strdup("off"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings, g_strdup("Absolute"), g_strdup("FOO"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	/* use BiosSettingId instead */
	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings, g_strdup("com.fwupd-internal.Absolute"), g_strdup("on"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings,
			    g_strdup("com.fwupd-internal.Absolute"),
			    g_strdup("off"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* string */
	attr2 =
	    fu_context_get_bios_setting(fu_engine_get_context(engine), "com.fwupd-internal.Asset");
	g_assert_nonnull(attr2);

	current = fwupd_bios_setting_get_current_value(attr2);
	g_assert_nonnull(current);

	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings, g_strdup("Asset"), g_strdup("0"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings, g_strdup("Asset"), g_strdup("1"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(
	    bios_settings,
	    g_strdup("Absolute"),
	    g_strdup("1234567891123456789112345678911234567891123456789112345678911111"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	/* integer */
	attr3 = fu_context_get_bios_setting(fu_engine_get_context(engine),
					    "com.fwupd-internal.CustomChargeStop");
	g_assert_nonnull(attr3);

	current = fwupd_bios_setting_get_current_value(attr3);
	g_assert_nonnull(current);

	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings, g_strdup("CustomChargeStop"), g_strdup("75"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_true(ret);

	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings, g_strdup("CustomChargeStop"), g_strdup("110"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings, g_strdup("CustomChargeStop"), g_strdup("1"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	/* force it to read only */
	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings, g_strdup("CustomChargeStop"), g_strdup("70"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, TRUE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* Read Only */
	attr4 = fu_context_get_bios_setting(fu_engine_get_context(engine),
					    "com.fwupd-internal.pending_reboot");
	g_assert_nonnull(attr4);

	current = fwupd_bios_setting_get_current_value(attr4);
	g_assert_nonnull(current);

	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings, g_strdup("pending_reboot"), g_strdup("foo"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	g_hash_table_remove_all(bios_settings);
	g_hash_table_insert(bios_settings, g_strdup("CustomChargeStop"), g_strdup("80"));
	ret = fu_engine_modify_bios_settings(engine, bios_settings, FALSE, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);
}

GFileInputStream *
_g_local_file_input_stream_new(int fd);

static void
fu_unix_seekable_input_stream_func(void)
{
#ifdef HAVE_GIO_UNIX
	gssize ret;
	gint fd;
	guint8 buf[6] = {0};
	g_autofree gchar *fn = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "metadata.xml", NULL);
	g_assert_nonnull(fn);
	fd = g_open(fn, O_RDONLY, 0);
	g_assert_cmpint(fd, >=, 0);

	stream = fu_unix_seekable_input_stream_new(fd, TRUE);
	g_assert_nonnull(stream);

	/* first chuck */
	ret = g_input_stream_read(stream, buf, sizeof(buf) - 1, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(ret, ==, 5);
	g_assert_cmpstr((const gchar *)buf, ==, "<?xml");

	/* second chuck */
	ret = g_input_stream_read(stream, buf, sizeof(buf) - 1, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(ret, ==, 5);
	g_assert_cmpstr((const gchar *)buf, ==, " vers");

	/* first chuck, again */
	ret = g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(ret, ==, 1);
	ret = g_input_stream_read(stream, buf, sizeof(buf) - 1, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(ret, ==, 5);
	g_assert_cmpstr((const gchar *)buf, ==, "<?xml");
#else
	g_test_skip("No gio-unix-2.0 support, skipping");
#endif
}

static void
fu_remote_download_func(void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *directory = NULL;
	g_autofree gchar *expected_metadata = NULL;
	g_autofree gchar *expected_signature = NULL;
	g_autoptr(FwupdRemote) remote = NULL;
	g_autoptr(GError) error = NULL;

	remote = fwupd_remote_new();
	directory = g_build_filename(FWUPD_LOCALSTATEDIR, "lib", "fwupd", "remotes2.d", NULL);
	expected_metadata = g_build_filename(FWUPD_LOCALSTATEDIR,
					     "lib",
					     "fwupd",
					     "remotes2.d",
					     "lvfs-testing",
					     "firmware.xml.gz",
					     NULL);
	expected_signature = g_strdup_printf("%s.jcat", expected_metadata);
	fwupd_remote_set_remotes_dir(remote, directory);
	fn = g_test_build_filename(G_TEST_DIST, "tests", "remotes2.d", "lvfs-testing.conf", NULL);
	ret = fu_remote_load_from_filename(remote, fn, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fwupd_remote_setup(remote, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fwupd_remote_get_kind(remote), ==, FWUPD_REMOTE_KIND_DOWNLOAD);
	g_assert_cmpint(fwupd_remote_get_priority(remote), ==, 0);
	g_assert_false(fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_ENABLED));
	g_assert_nonnull(fwupd_remote_get_metadata_uri(remote));
	g_assert_nonnull(fwupd_remote_get_metadata_uri_sig(remote));
	g_assert_cmpstr(fwupd_remote_get_title(remote),
			==,
			"Linux Vendor Firmware Service (testing)");
	g_assert_cmpstr(fwupd_remote_get_report_uri(remote),
			==,
			"https://fwupd.org/lvfs/firmware/report");
	g_assert_cmpstr(fwupd_remote_get_filename_cache(remote), ==, expected_metadata);
	g_assert_cmpstr(fwupd_remote_get_filename_cache_sig(remote), ==, expected_signature);
}

static void
fu_remote_auth_func(void)
{
	gboolean ret;
	gchar **order;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *remotes_dir = NULL;
	g_autofree gchar *json = NULL;
	g_autoptr(FwupdRemote) remote = fwupd_remote_new();
	g_autoptr(FwupdRemote) remote2 = fwupd_remote_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) data = NULL;

	remotes_dir = g_test_build_filename(G_TEST_BUILT, "tests", NULL);
	fwupd_remote_set_remotes_dir(remote, remotes_dir);

	fn = g_test_build_filename(G_TEST_DIST, "tests", "auth.conf", NULL);
	ret = fu_remote_load_from_filename(remote, fn, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fwupd_remote_get_username(remote), ==, "user");
	g_assert_cmpstr(fwupd_remote_get_password(remote), ==, "pass");
	g_assert_cmpstr(fwupd_remote_get_report_uri(remote),
			==,
			"https://fwupd.org/lvfs/firmware/report");
	g_assert_false(fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_APPROVAL_REQUIRED));
	g_assert_false(fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_AUTOMATIC_REPORTS));
	g_assert_true(fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_AUTOMATIC_SECURITY_REPORTS));

	g_assert_true(
	    g_str_has_suffix(fwupd_remote_get_filename_source(remote), "tests/auth.conf"));
	g_assert_true(g_str_has_suffix(fwupd_remote_get_remotes_dir(remote), "/src/tests"));
	g_assert_cmpint(fwupd_remote_get_age(remote), >, 1000000);

	ret = fwupd_remote_setup(remote, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	order = fwupd_remote_get_order_before(remote);
	g_assert_nonnull(order);
	g_assert_cmpint(g_strv_length(order), ==, 1);
	g_assert_cmpstr(order[0], ==, "before");
	order = fwupd_remote_get_order_after(remote);
	g_assert_nonnull(order);
	g_assert_cmpint(g_strv_length(order), ==, 1);
	g_assert_cmpstr(order[0], ==, "after");

	/* to/from GVariant */
	fwupd_remote_set_priority(remote, 999);
	data = fwupd_codec_to_variant(FWUPD_CODEC(remote), FWUPD_CODEC_FLAG_NONE);
	ret = fwupd_codec_from_variant(FWUPD_CODEC(remote2), data, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fwupd_remote_get_username(remote2), ==, "user");
	g_assert_cmpint(fwupd_remote_get_priority(remote2), ==, 999);

	/* jcat-tool is not a hard dep, and the tests create an empty file if unfound */
	ret = fwupd_remote_load_signature(remote,
					  fwupd_remote_get_filename_cache_sig(remote),
					  &error);
	if (!ret) {
		if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_READ)) {
			g_test_skip("no jcat-tool, so skipping test");
			return;
		}
	}
	g_assert_no_error(error);
	g_assert_true(ret);

	/* to JSON */
	fwupd_remote_set_filename_source(remote2, NULL);
	fwupd_remote_set_checksum_sig(
	    remote2,
	    "dd1b4fd2a59bb0e4d9ea760c658ac3cf9336c7b6729357bab443485b5cf071b2");
	fwupd_remote_set_filename_cache(remote2, "./libfwupd/tests/auth/firmware.xml.gz");
	json = fwupd_codec_to_json_string(FWUPD_CODEC(remote2), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json);
	ret = fu_test_compare_lines(
	    json,
	    "{\n"
	    "  \"Id\" : \"auth\",\n"
	    "  \"Kind\" : \"download\",\n"
	    "  \"ReportUri\" : \"https://fwupd.org/lvfs/firmware/report\",\n"
	    "  \"MetadataUri\" : \"https://cdn.fwupd.org/downloads/firmware.xml.gz\",\n"
	    "  \"MetadataUriSig\" : \"https://cdn.fwupd.org/downloads/firmware.xml.gz.jcat\",\n"
	    "  \"Username\" : \"user\",\n"
	    "  \"Password\" : \"pass\",\n"
	    "  \"ChecksumSig\" : "
	    "\"dd1b4fd2a59bb0e4d9ea760c658ac3cf9336c7b6729357bab443485b5cf071b2\",\n"
	    "  \"FilenameCache\" : \"./libfwupd/tests/auth/firmware.xml.gz\",\n"
	    "  \"FilenameCacheSig\" : \"./libfwupd/tests/auth/firmware.xml.gz.jcat\",\n"
	    "  \"Flags\" : 9,\n"
	    "  \"Enabled\" : true,\n"
	    "  \"ApprovalRequired\" : false,\n"
	    "  \"AutomaticReports\" : false,\n"
	    "  \"AutomaticSecurityReports\" : true,\n"
	    "  \"Priority\" : 999,\n"
	    "  \"Mtime\" : 0,\n"
	    "  \"RefreshInterval\" : 86400\n"
	    "}",
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_remote_duplicate_func(void)
{
	gboolean ret;
	g_autofree gchar *fn2 = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(FwupdRemote) remote = fwupd_remote_new();
	g_autoptr(GError) error = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "stable.conf", NULL);
	ret = fu_remote_load_from_filename(remote, fn, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fn2 = g_test_build_filename(G_TEST_DIST, "tests", "disabled.conf", NULL);
	ret = fu_remote_load_from_filename(remote, fn2, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fwupd_remote_setup(remote, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fwupd_remote_setup(remote, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_ENABLED));
	g_assert_cmpstr(fwupd_remote_get_username(remote), ==, NULL);
	g_assert_cmpstr(fwupd_remote_get_password(remote), ==, "");
	g_assert_cmpstr(fwupd_remote_get_filename_cache(remote),
			==,
			"/tmp/fwupd-self-test/stable.xml");
}

/* verify we used the metadata path for firmware */
static void
fu_remote_nopath_func(void)
{
	gboolean ret;
	g_autofree gchar *firmware_uri = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(FwupdRemote) remote = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *directory = NULL;

	remote = fwupd_remote_new();
	directory = g_build_filename(FWUPD_LOCALSTATEDIR, "lib", "fwupd", "remotes2.d", NULL);
	fwupd_remote_set_remotes_dir(remote, directory);
	fn = g_test_build_filename(G_TEST_DIST, "tests", "firmware-nopath.conf", NULL);
	ret = fu_remote_load_from_filename(remote, fn, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fwupd_remote_get_kind(remote), ==, FWUPD_REMOTE_KIND_DOWNLOAD);
	g_assert_cmpint(fwupd_remote_get_priority(remote), ==, 0);
	g_assert_true(fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_ENABLED));
	g_assert_cmpstr(fwupd_remote_get_checksum(remote), ==, NULL);
	g_assert_cmpstr(fwupd_remote_get_metadata_uri(remote),
			==,
			"https://s3.amazonaws.com/lvfsbucket/downloads/firmware.xml.gz");
	g_assert_cmpstr(fwupd_remote_get_metadata_uri_sig(remote),
			==,
			"https://s3.amazonaws.com/lvfsbucket/downloads/firmware.xml.gz.jcat");
	firmware_uri = fwupd_remote_build_firmware_uri(remote, "firmware.cab", &error);
	g_assert_no_error(error);
	g_assert_cmpstr(firmware_uri,
			==,
			"https://s3.amazonaws.com/lvfsbucket/downloads/firmware.cab");
}

static void
fu_remote_local_func(void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *json = NULL;
	g_autoptr(FwupdRemote) remote = NULL;
	g_autoptr(FwupdRemote) remote2 = fwupd_remote_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) data = NULL;

	remote = fwupd_remote_new();
	fn = g_test_build_filename(G_TEST_DIST, "tests", "dell-esrt.conf", NULL);
	ret = fu_remote_load_from_filename(remote, fn, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fwupd_remote_get_kind(remote), ==, FWUPD_REMOTE_KIND_LOCAL);
	g_assert_true(fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_ENABLED));
	g_assert_null(fwupd_remote_get_metadata_uri(remote));
	g_assert_null(fwupd_remote_get_metadata_uri_sig(remote));
	g_assert_null(fwupd_remote_get_report_uri(remote));
	g_assert_cmpstr(fwupd_remote_get_title(remote),
			==,
			"Enable UEFI capsule updates on Dell systems");
	g_assert_cmpstr(fwupd_remote_get_filename_cache(remote),
			==,
			"@datadir@/fwupd/remotes.d/dell-esrt/firmware.xml");
	g_assert_cmpstr(fwupd_remote_get_filename_cache_sig(remote), ==, NULL);
	g_assert_cmpstr(fwupd_remote_get_checksum(remote), ==, NULL);

	/* to/from GVariant */
	data = fwupd_codec_to_variant(FWUPD_CODEC(remote), FWUPD_CODEC_FLAG_NONE);
	ret = fwupd_codec_from_variant(FWUPD_CODEC(remote2), data, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_null(fwupd_remote_get_metadata_uri(remote));

	/* to JSON */
	fwupd_remote_set_filename_source(remote2, NULL);
	json = fwupd_codec_to_json_string(FWUPD_CODEC(remote2), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json);
	ret = fu_test_compare_lines(
	    json,
	    "{\n"
	    "  \"Id\" : \"dell-esrt\",\n"
	    "  \"Kind\" : \"local\",\n"
	    "  \"Title\" : \"Enable UEFI capsule updates on Dell systems\",\n"
	    "  \"FilenameCache\" : \"@datadir@/fwupd/remotes.d/dell-esrt/firmware.xml\",\n"
	    "  \"Flags\" : 1,\n"
	    "  \"Enabled\" : true,\n"
	    "  \"ApprovalRequired\" : false,\n"
	    "  \"AutomaticReports\" : false,\n"
	    "  \"AutomaticSecurityReports\" : false,\n"
	    "  \"Priority\" : 0,\n"
	    "  \"Mtime\" : 0,\n"
	    "  \"RefreshInterval\" : 0\n"
	    "}",
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_remote_list_repair_func(void)
{
	FwupdRemote *remote;
	gboolean ret;
	g_autoptr(FuRemoteList) remote_list = fu_remote_list_new();
	g_autoptr(GError) error = NULL;

	fu_remote_list_set_lvfs_metadata_format(remote_list, "zst");
	ret = fu_remote_list_load(remote_list, FU_REMOTE_LIST_LOAD_FLAG_FIX_METADATA_URI, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check .gz converted to .zst */
	remote = fu_remote_list_get_by_id(remote_list, "legacy-lvfs");
	g_assert_nonnull(remote);
	g_assert_cmpstr(fwupd_remote_get_metadata_uri(remote),
			==,
			"http://localhost/stable.xml.zst");

	/* check .xz converted to .zst */
	remote = fu_remote_list_get_by_id(remote_list, "legacy-lvfs-xz");
	g_assert_nonnull(remote);
	g_assert_cmpstr(fwupd_remote_get_metadata_uri(remote),
			==,
			"http://localhost/stable.xml.zst");

	/* check non-LVFS remote NOT .gz converted to .xz */
	remote = fu_remote_list_get_by_id(remote_list, "legacy");
	g_assert_nonnull(remote);
	g_assert_cmpstr(fwupd_remote_get_metadata_uri(remote),
			==,
			"http://localhost/stable.xml.gz");
}

static void
fu_config_migrate_1_9_func(void)
{
	const gchar *fake_localconf_fn = "/tmp/fwupd-self-test/var/etc/fwupd/fwupd.conf";
	const gchar *fake_sysconf_fn = "/tmp/fwupd-self-test/fwupd/fwupd.conf";
	gboolean ret;
	g_autofree gchar *localconf_data = NULL;
	g_autoptr(FuConfig) config = FU_CONFIG(fu_engine_config_new());
	g_autoptr(GError) error = NULL;

	/* ensure empty tree */
	fu_self_test_mkroot();

	(void)g_unsetenv("CONFIGURATION_DIRECTORY");
	(void)g_setenv("FWUPD_SYSCONFDIR", "/tmp/fwupd-self-test", TRUE);

	ret = fu_path_mkdir_parent(fake_sysconf_fn, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = g_file_set_contents(fake_sysconf_fn,
				  "# use `man 5 fwupd.conf` for documentation\n"
				  "[fwupd]\n"
				  "DisabledPlugins=test;test_ble\n"
				  "OnlyTrusted=true\n"
				  "AllowEmulation=false\n",
				  -1,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_config_load(config, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_config_set_value(config, "fwupd", "AllowEmulation", "true", &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* ensure that all keys except AllowEmulation migrated */
	ret = g_file_get_contents(fake_localconf_fn, &localconf_data, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(localconf_data,
			==,
			"[fwupd]\n"
			"AllowEmulation=true\n");
}

static void
fu_config_set_plugin_defaults(FuConfig *config)
{
	/* these are correct for v2.0.0 */
	fu_config_set_default(config, "msr", "MinimumSmeKernelVersion", "5.18.0");
	fu_config_set_default(config, "redfish", "CACheck", "false");
	fu_config_set_default(config, "redfish", "IpmiDisableCreateUser", "false");
	fu_config_set_default(config, "redfish", "ManagerResetTimeout", "1800"); /* seconds */
	fu_config_set_default(config, "redfish", "Password", NULL);
	fu_config_set_default(config, "redfish", "Uri", NULL);
	fu_config_set_default(config, "redfish", "Username", NULL);
	fu_config_set_default(config, "redfish", "UserUri", NULL);
	fu_config_set_default(config, "thunderbolt", "DelayedActivation", "false");
	fu_config_set_default(config, "thunderbolt", "MinimumKernelVersion", "4.13.0");
	fu_config_set_default(config, "uefi-capsule", "DisableCapsuleUpdateOnDisk", "false");
	fu_config_set_default(config, "uefi-capsule", "DisableShimForSecureBoot", "false");
	fu_config_set_default(config, "uefi-capsule", "EnableEfiDebugging", "false");
	fu_config_set_default(config, "uefi-capsule", "EnableGrubChainLoad", "false");
	fu_config_set_default(config, "uefi-capsule", "OverrideESPMountPoint", NULL);
	fu_config_set_default(config, "uefi-capsule", "RebootCleanup", "true");
	fu_config_set_default(config, "uefi-capsule", "RequireESPFreeSpace", "0");
	fu_config_set_default(config, "uefi-capsule", "ScreenWidth", "0");
	fu_config_set_default(config, "uefi-capsule", "ScreenHeight", "0");
}

static void
fu_config_migrate_1_7_func(void)
{
	const gchar *sysconfdir = "/tmp/fwupd-self-test/conf-migration-1.7/var/etc";
	gboolean ret;
	const gchar *fn_merge[] = {"daemon.conf",
				   "msr.conf",
				   "redfish.conf",
				   "thunderbolt.conf",
				   "uefi_capsule.conf",
				   NULL};
	g_autofree gchar *localconf_data = NULL;
	g_autofree gchar *fn_mut = NULL;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuConfig) config = FU_CONFIG(fu_engine_config_new());
	g_autoptr(GError) error = NULL;

	/* ensure empty tree */
	fu_self_test_mkroot();

	/* source directory and data */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", "conf-migration-1.7", NULL);
	if (!g_file_test(testdatadir, G_FILE_TEST_EXISTS)) {
		g_test_skip("missing fwupd 1.7.x migration test data");
		return;
	}

	/* working directory */
	(void)g_setenv("FWUPD_SYSCONFDIR", sysconfdir, TRUE);
	(void)g_unsetenv("CONFIGURATION_DIRECTORY");

	fn_mut = g_build_filename(sysconfdir, "fwupd", "fwupd.conf", NULL);
	g_assert_nonnull(fn_mut);
	ret = fu_path_mkdir_parent(fn_mut, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* copy all files to working directory */
	for (guint i = 0; fn_merge[i] != NULL; i++) {
		g_autofree gchar *source =
		    g_build_filename(testdatadir, "fwupd", fn_merge[i], NULL);
		g_autofree gchar *target = g_build_filename(sysconfdir, "fwupd", fn_merge[i], NULL);
		fu_test_copy_file(source, target);
	}

	/* we don't want to run all the plugins just to get the _init() defaults */
	fu_config_set_plugin_defaults(config);
	ret = fu_config_load(config, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* make sure all migrated files were renamed */
	for (guint i = 0; fn_merge[i] != NULL; i++) {
		g_autofree gchar *old = g_build_filename(sysconfdir, "fwupd", fn_merge[i], NULL);
		g_autofree gchar *new = g_strdup_printf("%s.old", old);
		ret = g_file_test(old, G_FILE_TEST_EXISTS);
		g_assert_false(ret);
		ret = g_file_test(new, G_FILE_TEST_EXISTS);
		g_assert_true(ret);
	}

	/* ensure all default keys migrated */
	ret = g_file_get_contents(fn_mut, &localconf_data, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(localconf_data, ==, "");
}

static void
fu_engine_machine_hash_func(void)
{
	gsize sz = 0;
	g_autofree gchar *buf = NULL;
	g_autofree gchar *mhash1 = NULL;
	g_autofree gchar *mhash2 = NULL;
	g_autoptr(GError) error = NULL;

	if (!g_file_test("/etc/machine-id", G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing /etc/machine-id");
		return;
	}
	if (!g_file_get_contents("/etc/machine-id", &buf, &sz, &error)) {
		g_test_skip("/etc/machine-id is unreadable");
		return;
	}

	if (sz == 0) {
		g_test_skip("Empty /etc/machine-id");
		return;
	}

	mhash1 = fu_engine_build_machine_id("salt1", &error);
	g_assert_no_error(error);
	g_assert_cmpstr(mhash1, !=, NULL);
	mhash2 = fu_engine_build_machine_id("salt2", &error);
	g_assert_no_error(error);
	g_assert_cmpstr(mhash2, !=, NULL);
	g_assert_cmpstr(mhash2, !=, mhash1);
}

static void
fu_test_engine_fake_hidraw(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autofree gchar *value2 = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuUdevDevice) udev_device2 = NULL;
	g_autoptr(FuUdevDevice) udev_device3 = NULL;
	g_autoptr(GError) error = NULL;

	/* load engine and check the device was found */
	fu_engine_add_plugin_filter(engine, "pixart_rf");
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS |
				 FU_ENGINE_LOAD_FLAG_NO_IDLE_SOURCES | FU_ENGINE_LOAD_FLAG_READONLY,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* hidraw -> pixart_rf */
	device = fu_engine_get_device(engine, "6acd27f1feb25ba3b604063de4c13b604776b2f5", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), ==, "hidraw");
	g_assert_cmpstr(fu_udev_device_get_devtype(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpstr(fu_udev_device_get_driver(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpint(fu_device_get_vid(device), ==, 0x093a);
	g_assert_cmpint(fu_device_get_pid(device), ==, 0x2862);
	g_assert_cmpstr(fu_device_get_plugin(device), ==, "pixart_rf");
	g_assert_cmpstr(fu_device_get_name(device), ==, "PIXART Pixart dual-mode mouse");
	g_assert_cmpstr(fu_device_get_physical_id(device), ==, "usb-0000:00:14.0-1/input1");
	g_assert_cmpstr(fu_device_get_logical_id(device), ==, NULL);

	/* check can read random files */
	value2 = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(device),
					   "dev",
					   FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					   &error);
	g_assert_no_error(error);
	g_assert_cmpstr(value2, ==, "241:1");

	/* get child, both specified */
	udev_device2 = FU_UDEV_DEVICE(
	    fu_device_get_backend_parent_with_subsystem(device, "usb:usb_interface", &error));
	g_assert_no_error(error);
	g_assert_nonnull(udev_device2);
	g_assert_cmpstr(fu_udev_device_get_subsystem(udev_device2), ==, "usb");

	/* get child, initially unprobed */
	udev_device3 =
	    FU_UDEV_DEVICE(fu_device_get_backend_parent_with_subsystem(device, "usb", &error));
	g_assert_no_error(error);
	g_assert_nonnull(udev_device3);
	g_assert_cmpstr(fu_udev_device_get_subsystem(udev_device3), ==, "usb");
	g_assert_cmpstr(fu_udev_device_get_driver(udev_device3), ==, "usb");
}

static void
fu_test_engine_fake_usb(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	/* load engine and check the device was found */
	fu_engine_add_plugin_filter(engine, "colorhug");
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS |
				 FU_ENGINE_LOAD_FLAG_NO_IDLE_SOURCES | FU_ENGINE_LOAD_FLAG_READONLY,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* USB -> colorhug */
	device = fu_engine_get_device(engine, "d787669ee4a103fe0b361fe31c10ea037c72f27c", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), ==, "usb");
	g_assert_cmpstr(fu_udev_device_get_devtype(FU_UDEV_DEVICE(device)), ==, "usb_device");
	g_assert_cmpstr(fu_udev_device_get_driver(FU_UDEV_DEVICE(device)), ==, "usb");
	g_assert_cmpint(fu_device_get_vid(device), ==, 0x093a);
	g_assert_cmpint(fu_device_get_pid(device), ==, 0x2862);
	g_assert_cmpstr(fu_device_get_plugin(device), ==, "colorhug");
	g_assert_cmpstr(fu_device_get_physical_id(device), ==, "1-1");
	g_assert_cmpstr(fu_device_get_logical_id(device), ==, NULL);
}

static void
fu_test_engine_fake_pci(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	/* load engine and check the device was found */
	fu_engine_add_plugin_filter(engine, "optionrom");
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS |
				 FU_ENGINE_LOAD_FLAG_NO_IDLE_SOURCES | FU_ENGINE_LOAD_FLAG_READONLY,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* PCI -> optionrom */
	device = fu_engine_get_device(engine, "20c947afbdc42deee9a7333290008cb384b10f74", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), ==, "pci");
	g_assert_cmpstr(fu_udev_device_get_devtype(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpstr(fu_udev_device_get_driver(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_true(
	    g_str_has_suffix(fu_udev_device_get_device_file(FU_UDEV_DEVICE(device)), "/rom"));
	g_assert_cmpint(fu_device_get_vid(device), ==, 0x8086);
	g_assert_cmpint(fu_device_get_pid(device), ==, 0x06ed);
	g_assert_cmpstr(fu_device_get_plugin(device), ==, "optionrom");
	g_assert_cmpstr(fu_device_get_physical_id(device), ==, "PCI_SLOT_NAME=0000:00:14.0");
	g_assert_cmpstr(fu_device_get_logical_id(device), ==, "rom");
}

static void
fu_test_engine_fake_v4l(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	/* load engine and check the device was found */
	fu_engine_add_plugin_filter(engine, "logitech_tap");
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS |
				 FU_ENGINE_LOAD_FLAG_NO_IDLE_SOURCES | FU_ENGINE_LOAD_FLAG_READONLY,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* v4l -> logitech_tap */
	device = fu_engine_get_device(engine, "d787669ee4a103fe0b361fe31c10ea037c72f27c", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), ==, "video4linux");
	g_assert_cmpstr(fu_udev_device_get_devtype(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpstr(fu_udev_device_get_driver(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpint(fu_device_get_vid(device), ==, 0x093A);
	g_assert_cmpint(fu_device_get_pid(device), ==, 0x2862);
	g_assert_cmpint(fu_v4l_device_get_index(FU_V4L_DEVICE(device)), ==, 0);
	g_assert_cmpint(fu_v4l_device_get_caps(FU_V4L_DEVICE(device)), ==, FU_V4L_CAP_NONE);
	g_assert_cmpstr(fu_device_get_name(device), ==, "Integrated Camera: Integrated C");
	g_assert_cmpstr(fu_device_get_plugin(device), ==, "logitech_tap");
}

static void
fu_test_engine_fake_nvme(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	/* load engine and check the device was found */
	fu_engine_add_plugin_filter(engine, "nvme");
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS |
				 FU_ENGINE_LOAD_FLAG_NO_IDLE_SOURCES | FU_ENGINE_LOAD_FLAG_READONLY,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* NVMe -> nvme */
	device = fu_engine_get_device(engine, "4c263c95f596030b430d65dc934f6722bcee5720", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), ==, "nvme");
	g_assert_cmpstr(fu_udev_device_get_devtype(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpint(fu_udev_device_get_number(FU_UDEV_DEVICE(device)), ==, 1);
	g_assert_cmpstr(fu_udev_device_get_driver(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpstr(fu_udev_device_get_device_file(FU_UDEV_DEVICE(device)), ==, "/dev/nvme1");
	g_assert_cmpint(fu_device_get_vid(device), ==, 0x1179);
	g_assert_cmpint(fu_device_get_pid(device), ==, 0x010F);
	g_assert_true(fu_device_has_vendor_id(device, "PCI:0x1179"));
	g_assert_cmpstr(fu_device_get_vendor(device), ==, "Toshiba Corporation");
	g_assert_cmpstr(fu_device_get_plugin(device), ==, "nvme");
	g_assert_cmpstr(fu_device_get_physical_id(device), ==, "PCI_SLOT_NAME=0000:00:1b.0");
	g_assert_cmpstr(fu_device_get_logical_id(device), ==, NULL);
}

static void
fu_test_engine_fake_serio(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	/* load engine and check the device was found */
	fu_engine_add_plugin_filter(engine, "synaptics_rmi");
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS |
				 FU_ENGINE_LOAD_FLAG_NO_IDLE_SOURCES | FU_ENGINE_LOAD_FLAG_READONLY,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* serio */
	device = fu_engine_get_device(engine, "d8419b7614e50c6fb6162b5dca34df5236a62a8d", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), ==, "serio");
	g_assert_cmpstr(fu_udev_device_get_devtype(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpstr(fu_udev_device_get_driver(FU_UDEV_DEVICE(device)), ==, "psmouse");
	g_assert_cmpstr(fu_udev_device_get_device_file(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpint(fu_device_get_vid(device), ==, 0x0);
	g_assert_cmpint(fu_device_get_pid(device), ==, 0x0);
	g_assert_cmpstr(fu_device_get_name(device), ==, "TouchStyk");
	g_assert_cmpstr(fu_device_get_plugin(device), ==, "synaptics_rmi");
	g_assert_cmpstr(fu_device_get_physical_id(device),
			==,
			"DEVPATH=/devices/platform/i8042/serio1");
	g_assert_cmpstr(fu_device_get_logical_id(device), ==, NULL);
	g_assert_true(fu_device_has_instance_id(device, "SERIO\\FWID_LEN0305-PNP0F13"));
}

static void
fu_test_engine_fake_tpm(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	/* load engine and check the device was found */
	fu_engine_add_plugin_filter(engine, "tpm");
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS |
				 FU_ENGINE_LOAD_FLAG_NO_IDLE_SOURCES | FU_ENGINE_LOAD_FLAG_READONLY,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* no tss2-esys */
	if (fu_engine_get_plugin_by_name(engine, "tpm", &error) == NULL) {
		g_test_skip(error->message);
		return;
	}

	/* tpm */
	device = fu_engine_get_device(engine, "1d8d50a4dbc65618f5c399c2ae827b632b3ccc11", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), ==, "tpm");
	g_assert_cmpstr(fu_udev_device_get_devtype(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpstr(fu_udev_device_get_driver(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpstr(fu_udev_device_get_device_file(FU_UDEV_DEVICE(device)), ==, "/dev/tpm0");
	g_assert_cmpint(fu_device_get_vid(device), ==, 0x0);
	g_assert_cmpint(fu_device_get_pid(device), ==, 0x0);
	g_assert_cmpstr(fu_device_get_plugin(device), ==, "tpm");
	g_assert_cmpstr(fu_device_get_physical_id(device), ==, "DEVNAME=tpm0");
	g_assert_cmpstr(fu_device_get_logical_id(device), ==, NULL);
}

static void
fu_test_engine_fake_mei(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	/* load engine and check the device was found */
	fu_engine_add_plugin_filter(engine, "intel_me");
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS |
				 FU_ENGINE_LOAD_FLAG_NO_IDLE_SOURCES | FU_ENGINE_LOAD_FLAG_READONLY,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* mei */
	device = fu_engine_get_device(engine, "8d5470e73fd9a31eaa460b2b6aea95483fe3f14c", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), ==, "mei");
	g_assert_cmpstr(fu_udev_device_get_devtype(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpstr(fu_udev_device_get_driver(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpstr(fu_udev_device_get_device_file(FU_UDEV_DEVICE(device)), ==, "/dev/mei0");
	g_assert_cmpint(fu_device_get_vid(device), ==, 0x8086);
	g_assert_cmpint(fu_device_get_pid(device), ==, 0x06E0);
	g_assert_cmpstr(fu_device_get_plugin(device), ==, "intel_me");
	g_assert_cmpstr(fu_device_get_physical_id(device), ==, "PCI_SLOT_NAME=0000:00:16.0");
	g_assert_cmpstr(fu_device_get_logical_id(device), ==, "AMT");
}

static void
fu_test_engine_fake_block(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(self->ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	/* load engine and check the device was found */
	fu_engine_add_plugin_filter(engine, "scsi");
	ret = fu_engine_load(engine,
			     FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS |
				 FU_ENGINE_LOAD_FLAG_NO_IDLE_SOURCES | FU_ENGINE_LOAD_FLAG_READONLY,
			     progress,
			     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* block */
	device = fu_engine_get_device(engine, "7772d9fe9419e3ea564216e12913a16e233378a6", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), ==, "block");
	g_assert_cmpstr(fu_udev_device_get_devtype(FU_UDEV_DEVICE(device)), ==, "disk");
	g_assert_cmpstr(fu_udev_device_get_driver(FU_UDEV_DEVICE(device)), ==, NULL);
	g_assert_cmpstr(fu_udev_device_get_device_file(FU_UDEV_DEVICE(device)), ==, "/dev/sde");
	g_assert_cmpstr(fu_device_get_plugin(device), ==, "scsi");
	g_assert_cmpstr(fu_device_get_vendor(device), ==, "IBM-ESXS");
}

int
main(int argc, char **argv)
{
	gboolean ret;
	g_autofree gchar *sysfsdir = NULL;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTest) self = g_new0(FuTest, 1);

	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_DATADIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_LIBDIR_PKG", testdatadir, TRUE);
	(void)g_setenv("FWUPD_SYSCONFDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_SYSFSFWDIR", testdatadir, TRUE);
	(void)g_setenv("CONFIGURATION_DIRECTORY", testdatadir, TRUE);
	(void)g_setenv("FWUPD_LOCALSTATEDIR", "/tmp/fwupd-self-test/var", TRUE);
	(void)g_setenv("FWUPD_SYSFSFWATTRIBDIR", testdatadir, TRUE);
	sysfsdir = g_test_build_filename(G_TEST_DIST, "tests", "sys", NULL);
	(void)g_setenv("FWUPD_SYSFSDIR", sysfsdir, TRUE);
	(void)g_setenv("FWUPD_SELF_TEST", "1", TRUE);
	(void)g_setenv("FWUPD_MACHINE_ID", "test", TRUE);

	/* ensure empty tree */
	fu_self_test_mkroot();

	/* do not save silo */
	self->ctx = fu_context_new();
	ret = fu_context_load_quirks(self->ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* load dummy hwids */
	ret = fu_context_load_hwinfo(self->ctx, progress, FU_CONTEXT_HWID_FLAG_LOAD_CONFIG, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* tests go here */
	if (g_test_slow()) {
		g_test_add_data_func("/fwupd/console", self, fu_console_func);
	}
	g_test_add_func("/fwupd/idle", fu_idle_func);
	g_test_add_func("/fwupd/client-list", fu_client_list_func);
	g_test_add_func("/fwupd/remote{download}", fu_remote_download_func);
	g_test_add_func("/fwupd/remote{no-path}", fu_remote_nopath_func);
	g_test_add_func("/fwupd/remote{local}", fu_remote_local_func);
	g_test_add_func("/fwupd/remote{duplicate}", fu_remote_duplicate_func);
	g_test_add_func("/fwupd/remote{auth}", fu_remote_auth_func);
	g_test_add_func("/fwupd/remote-list{repair}", fu_remote_list_repair_func);
	g_test_add_func("/fwupd/unix-seekable-input-stream", fu_unix_seekable_input_stream_func);
	g_test_add_data_func("/fwupd/backend{usb}", self, fu_backend_usb_func);
	g_test_add_data_func("/fwupd/backend{usb-invalid}", self, fu_backend_usb_invalid_func);
	g_test_add_data_func("/fwupd/plugin{module}", self, fu_plugin_module_func);
	g_test_add_data_func("/fwupd/memcpy", self, fu_memcpy_func);
	g_test_add_func("/fwupd/cabinet", fu_common_cabinet_func);
	g_test_add_data_func("/fwupd/security-attr", self, fu_security_attr_func);
	g_test_add_data_func("/fwupd/device-list", self, fu_device_list_func);
	g_test_add_data_func("/fwupd/device-list{unconnected-no-delay}",
			     self,
			     fu_device_list_unconnected_no_delay_func);
	g_test_add_data_func("/fwupd/device-list{equivalent-id}",
			     self,
			     fu_device_list_equivalent_id_func);
	g_test_add_data_func("/fwupd/device-list{delay}", self, fu_device_list_delay_func);
	g_test_add_data_func("/fwupd/device-list{explicit-order}",
			     self,
			     fu_device_list_explicit_order_func);
	g_test_add_data_func("/fwupd/device-list{explicit-order-post}",
			     self,
			     fu_device_list_explicit_order_post_func);
	g_test_add_data_func("/fwupd/device-list{no-auto-remove-children}",
			     self,
			     fu_device_list_no_auto_remove_children_func);
	g_test_add_data_func("/fwupd/device-list{compatible}",
			     self,
			     fu_device_list_compatible_func);
	g_test_add_data_func("/fwupd/device-list{remove-chain}",
			     self,
			     fu_device_list_remove_chain_func);
	g_test_add_data_func("/fwupd/device-list{counterpart}",
			     self,
			     fu_device_list_counterpart_func);
	g_test_add_data_func("/fwupd/release{compare}", self, fu_release_compare_func);
	g_test_add_func("/fwupd/release{uri-scheme}", fu_release_uri_scheme_func);
	g_test_add_data_func("/fwupd/release{trusted-report}",
			     self,
			     fu_release_trusted_report_func);
	g_test_add_data_func("/fwupd/release{trusted-report-oem}",
			     self,
			     fu_release_trusted_report_oem_func);
	g_test_add_data_func("/fwupd/release{no-trusted-report-upgrade}",
			     self,
			     fu_release_no_trusted_report_upgrade_func);
	g_test_add_data_func("/fwupd/release{no-trusted-report}",
			     self,
			     fu_release_no_trusted_report_func);
	g_test_add_data_func("/fwupd/engine{get-details-added}",
			     self,
			     fu_engine_get_details_added_func);
	g_test_add_data_func("/fwupd/engine{get-details-missing}",
			     self,
			     fu_engine_get_details_missing_func);
	g_test_add_data_func("/fwupd/engine{device-unlock}", self, fu_engine_device_unlock_func);
	g_test_add_data_func("/fwupd/engine{device-equivalent}",
			     self,
			     fu_engine_device_equivalent_func);
	g_test_add_data_func("/fwupd/engine{device-md-set-flags}",
			     self,
			     fu_engine_device_md_set_flags_func);
	g_test_add_data_func("/fwupd/engine{multiple-releases}",
			     self,
			     fu_engine_multiple_rels_func);
	g_test_add_data_func("/fwupd/engine{install-request}", self, fu_engine_install_request);
	g_test_add_data_func("/fwupd/engine{history-success}", self, fu_engine_history_func);
	g_test_add_data_func("/fwupd/engine{history-verfmt}", self, fu_engine_history_verfmt_func);
	g_test_add_data_func("/fwupd/engine{history-modify}", self, fu_engine_history_modify_func);
	g_test_add_data_func("/fwupd/engine{history-error}", self, fu_engine_history_error_func);
	g_test_add_data_func("/fwupd/engine{fake-hidraw}", self, fu_test_engine_fake_hidraw);
	g_test_add_data_func("/fwupd/engine{fake-usb}", self, fu_test_engine_fake_usb);
	g_test_add_data_func("/fwupd/engine{fake-serio}", self, fu_test_engine_fake_serio);
	g_test_add_data_func("/fwupd/engine{fake-nvme}", self, fu_test_engine_fake_nvme);
	g_test_add_data_func("/fwupd/engine{fake-block}", self, fu_test_engine_fake_block);
	g_test_add_data_func("/fwupd/engine{fake-mei}", self, fu_test_engine_fake_mei);
	g_test_add_data_func("/fwupd/engine{fake-tpm}", self, fu_test_engine_fake_tpm);
	g_test_add_data_func("/fwupd/engine{fake-pci}", self, fu_test_engine_fake_pci);
	g_test_add_data_func("/fwupd/engine{fake-v4l}", self, fu_test_engine_fake_v4l);
	if (g_test_slow()) {
		g_test_add_data_func("/fwupd/device-list{replug-auto}",
				     self,
				     fu_device_list_replug_auto_func);
	}
	g_test_add_data_func("/fwupd/device-list{replug-user}",
			     self,
			     fu_device_list_replug_user_func);
	g_test_add_func("/fwupd/engine{machine-hash}", fu_engine_machine_hash_func);
	g_test_add_data_func("/fwupd/engine{require-hwid}", self, fu_engine_require_hwid_func);
	g_test_add_data_func("/fwupd/engine{requires-reboot}",
			     self,
			     fu_engine_install_needs_reboot);
	g_test_add_data_func("/fwupd/engine{history-inherit}", self, fu_engine_history_inherit);
	g_test_add_data_func("/fwupd/engine{partial-hash}", self, fu_engine_partial_hash_func);
	g_test_add_data_func("/fwupd/engine{downgrade}", self, fu_engine_downgrade_func);
	g_test_add_data_func("/fwupd/engine{md-verfmt}", self, fu_engine_md_verfmt_func);
	g_test_add_data_func("/fwupd/engine{requirements-success}",
			     self,
			     fu_engine_requirements_func);
	g_test_add_data_func("/fwupd/engine{requirements-soft}",
			     self,
			     fu_engine_requirements_soft_func);
	g_test_add_data_func("/fwupd/engine{requirements-missing}",
			     self,
			     fu_engine_requirements_missing_func);
	g_test_add_data_func("/fwupd/engine{requirements-client-fail}",
			     self,
			     fu_engine_requirements_client_fail_func);
	g_test_add_data_func("/fwupd/engine{requirements-client-invalid}",
			     self,
			     fu_engine_requirements_client_invalid_func);
	g_test_add_data_func("/fwupd/engine{requirements-client-pass}",
			     self,
			     fu_engine_requirements_client_pass_func);
	g_test_add_data_func("/fwupd/engine{requirements-not-hardware}",
			     self,
			     fu_engine_requirements_not_hardware_func);
	g_test_add_data_func("/fwupd/engine{requirements-version-require}",
			     self,
			     fu_engine_requirements_version_require_func);
	g_test_add_data_func("/fwupd/engine{requirements-version-lowest}",
			     self,
			     fu_engine_requirements_version_lowest_func);
	g_test_add_data_func("/fwupd/engine{requirements-parent-device}",
			     self,
			     fu_engine_requirements_parent_device_func);
	g_test_add_data_func("/fwupd/engine{requirements-child-device}",
			     self,
			     fu_engine_requirements_child_device_func);
	g_test_add_data_func("/fwupd/engine{requirements_protocol_check_func}",
			     self,
			     fu_engine_requirements_protocol_check_func);
	g_test_add_data_func("/fwupd/engine{requirements-not-child}",
			     self,
			     fu_engine_requirements_child_func);
	g_test_add_data_func("/fwupd/engine{requirements-not-child-fail}",
			     self,
			     fu_engine_requirements_child_fail_func);
	g_test_add_data_func("/fwupd/engine{requirements-unsupported}",
			     self,
			     fu_engine_requirements_unsupported_func);
	g_test_add_data_func("/fwupd/engine{requirements-device}",
			     self,
			     fu_engine_requirements_device_func);
	g_test_add_data_func("/fwupd/engine{requirements-device-plain}",
			     self,
			     fu_engine_requirements_device_plain_func);
	g_test_add_data_func("/fwupd/engine{requirements-version-format}",
			     self,
			     fu_engine_requirements_version_format_func);
	g_test_add_data_func("/fwupd/engine{requirements-only-upgrade}",
			     self,
			     fu_engine_requirements_only_upgrade_func);
	g_test_add_data_func("/fwupd/engine{device-auto-parent-id}",
			     self,
			     fu_engine_device_parent_id_func);
	g_test_add_data_func("/fwupd/engine{device-auto-parent-guid}",
			     self,
			     fu_engine_device_parent_guid_func);
	g_test_add_data_func("/fwupd/engine{install-duration}",
			     self,
			     fu_engine_install_duration_func);
	g_test_add_data_func("/fwupd/engine{release-dedupe}", self, fu_engine_release_dedupe_func);
	g_test_add_data_func("/fwupd/engine{generate-md}", self, fu_engine_generate_md_func);
	g_test_add_data_func("/fwupd/engine{requirements-other-device}",
			     self,
			     fu_engine_requirements_other_device_func);
	g_test_add_data_func("/fwupd/engine{fu_engine_requirements_sibling_device_func}",
			     self,
			     fu_engine_requirements_sibling_device_func);
	g_test_add_data_func("/fwupd/plugin{composite}", self, fu_plugin_composite_func);
	g_test_add_data_func("/fwupd/history", self, fu_history_func);
	g_test_add_data_func("/fwupd/history{migrate-v1}", self, fu_history_migrate_v1_func);
	g_test_add_data_func("/fwupd/history{migrate-v2}", self, fu_history_migrate_v2_func);
	g_test_add_data_func("/fwupd/plugin-list", self, fu_plugin_list_func);
	g_test_add_data_func("/fwupd/plugin-list{depsolve}", self, fu_plugin_list_depsolve_func);
	g_test_add_func("/fwupd/common{cab-success}", fu_common_store_cab_func);
	g_test_add_func("/fwupd/common{cab-success-artifact}", fu_common_store_cab_artifact_func);
	g_test_add_func("/fwupd/common{cab-success-unsigned}", fu_common_store_cab_unsigned_func);
	g_test_add_func("/fwupd/common{cab-success-folder}", fu_common_store_cab_folder_func);
	g_test_add_func("/fwupd/common{cab-success-sha256}", fu_common_store_cab_sha256_func);
	g_test_add_func("/fwupd/common{cab-error-no-metadata}",
			fu_common_store_cab_error_no_metadata_func);
	g_test_add_func("/fwupd/common{cab-error-wrong-size}",
			fu_common_store_cab_error_wrong_size_func);
	g_test_add_func("/fwupd/common{cab-error-wrong-checksum}",
			fu_common_store_cab_error_wrong_checksum_func);
	g_test_add_func("/fwupd/common{cab-error-missing-file}",
			fu_common_store_cab_error_missing_file_func);
	g_test_add_func("/fwupd/common{cab-error-size}", fu_common_store_cab_error_size_func);
	g_test_add_data_func("/fwupd/write-bios-attrs", self, fu_engine_modify_bios_settings_func);

	/* these need to be last as they overwrite stuff in the mkroot */
	g_test_add_func("/fwupd/config_migrate_1_7", fu_config_migrate_1_7_func);
	g_test_add_func("/fwupd/config_migrate_1_9", fu_config_migrate_1_9_func);
	return g_test_run();
}
