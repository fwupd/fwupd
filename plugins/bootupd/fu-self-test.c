/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <systemd/sd-event.h>
#include <systemd/sd-json.h>
#include <systemd/sd-varlink.h>

#include "fu-bootupd-plugin.h"
#include "fu-context-private.h"
#include "fu-plugin-private.h"
#include "fu-volume-private.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC(sd_event, sd_event_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(sd_varlink_server, sd_varlink_server_unref)

#define FU_BOOTUPD_TEST_PARTUUID "2e126947-2730-49df-af3f-012de73bccfd"

typedef struct {
	gchar *address;	    /* the socket the fake bootupd listens on */
	sd_event *event;    /* no-ref, valid while the server thread runs */
	GMutex mutex;	    /* protects @ready */
	GCond cond;	    /* signaled once the server is listening */
	gboolean ready;	    /* the server is listening */
	gchar *partuuid;    /* captured from the SyncFwupdUpdates call */
	gchar *capsule_dir; /* captured from the SyncFwupdUpdates call */
} FuBootupdTestServer;

static void
fu_bootupd_test_server_free(FuBootupdTestServer *server)
{
	g_free(server->address);
	g_free(server->partuuid);
	g_free(server->capsule_dir);
	g_mutex_clear(&server->mutex);
	g_cond_clear(&server->cond);
	g_free(server);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuBootupdTestServer, fu_bootupd_test_server_free)

/* handle the org.coreos.bootupd1.SyncFwupdUpdates method by recording the
 * arguments the plugin sent us and replying with success */
static int
fu_bootupd_test_server_method_cb(sd_varlink *link,
				 sd_json_variant *parameters,
				 sd_varlink_method_flags_t flags,
				 void *userdata)
{
	FuBootupdTestServer *server = (FuBootupdTestServer *)userdata;
	server->partuuid =
	    g_strdup(sd_json_variant_string(sd_json_variant_by_key(parameters, "partuuid")));
	server->capsule_dir =
	    g_strdup(sd_json_variant_string(sd_json_variant_by_key(parameters, "capsule_dir")));
	return sd_varlink_reply(link, NULL);
}

/* the plugin unrefs its connection once the call returns, so exit the loop */
static void
fu_bootupd_test_server_disconnect_cb(sd_varlink_server *s, sd_varlink *link, void *userdata)
{
	FuBootupdTestServer *server = (FuBootupdTestServer *)userdata;
	sd_event_exit(server->event, 0);
}

/* defensive timeout so a broken test fails rather than hanging forever */
static int
fu_bootupd_test_server_timeout_cb(sd_event_source *s, uint64_t usec, void *userdata)
{
	return sd_event_exit((sd_event *)userdata, -ETIMEDOUT);
}

static gpointer
fu_bootupd_test_server_thread_cb(gpointer user_data)
{
	FuBootupdTestServer *server = (FuBootupdTestServer *)user_data;
	uint64_t now = 0;
	g_autoptr(sd_event) event = NULL;
	g_autoptr(sd_varlink_server) vls = NULL;

	/* set up a fake bootupd service that exits once the client disconnects */
	g_assert_cmpint(sd_event_new(&event), >=, 0);
	server->event = event;
	g_assert_cmpint(sd_varlink_server_new(&vls, SD_VARLINK_SERVER_INHERIT_USERDATA), >=, 0);
	sd_varlink_server_set_userdata(vls, server);
	g_assert_cmpint(sd_varlink_server_bind_method(vls,
						      "org.coreos.bootupd1.SyncFwupdUpdates",
						      fu_bootupd_test_server_method_cb),
			>=,
			0);
	g_assert_cmpint(
	    sd_varlink_server_bind_disconnect(vls, fu_bootupd_test_server_disconnect_cb),
	    >=,
	    0);
	g_assert_cmpint(sd_varlink_server_listen_address(vls, server->address, 0600), >=, 0);
	g_assert_cmpint(sd_varlink_server_attach_event(vls, event, 0), >=, 0);

	g_assert_cmpint(sd_event_now(event, CLOCK_MONOTONIC, &now), >=, 0);
	g_assert_cmpint(sd_event_add_time(event,
					  NULL,
					  CLOCK_MONOTONIC,
					  now + (10 * G_USEC_PER_SEC),
					  0,
					  fu_bootupd_test_server_timeout_cb,
					  event),
			>=,
			0);

	/* the socket now exists, so let the main thread connect */
	g_mutex_lock(&server->mutex);
	server->ready = TRUE;
	g_cond_signal(&server->cond);
	g_mutex_unlock(&server->mutex);

	sd_event_loop(event);
	return NULL;
}

/* write @filename to an ESP mounted at @mount_point and return what the fake
 * bootupd service was told; the returned server has a NULL partuuid if the
 * plugin never made the call */
static FuBootupdTestServer *
fu_bootupd_test_write_esp(const gchar *mount_point, const gchar *filename)
{
	gboolean ret;
	FuBootupdTestServer *server = g_new0(FuBootupdTestServer, 1);
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(FU_TYPE_BOOTUPD_PLUGIN, ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTemporaryDirectory) sockdir = NULL;
	g_autoptr(FuVolume) volume = NULL;
	g_autoptr(GBytes) bytes = g_bytes_new_static("hello", 5);
	g_autoptr(GError) error = NULL;
	g_autoptr(GThread) thread = NULL;

	/* start a fake bootupd varlink service */
	sockdir = fu_temporary_directory_new("bootupd", &error);
	g_assert_no_error(error);
	g_assert_nonnull(sockdir);
	server->address = fu_temporary_directory_build(sockdir, "bootupd.sock", NULL);
	g_mutex_init(&server->mutex);
	g_cond_init(&server->cond);
	fu_bootupd_plugin_set_varlink_address(FU_BOOTUPD_PLUGIN(plugin), server->address);

	thread = g_thread_new("bootupd-test-server", fu_bootupd_test_server_thread_cb, server);
	g_mutex_lock(&server->mutex);
	while (!server->ready)
		g_cond_wait(&server->cond, &server->mutex);
	g_mutex_unlock(&server->mutex);

	/* start up the plugin, which connects to the esp-write signal */
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create an ESP volume with a partition UUID and register it */
	volume = fu_volume_new_from_mount_path(mount_point);
	fu_volume_set_partition_uuid(volume, FU_BOOTUPD_TEST_PARTUUID);
	fu_context_add_esp_volume(ctx, volume);

	/* writing a capsule to the ESP should notify bootupd over varlink */
	ret = fu_volume_write_file(volume, filename, bytes, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* the client has disconnected (or was never created), so the loop exits */
	g_thread_join(g_steal_pointer(&thread));
	return server;
}

/* a capsule written into a nested directory is reported relative to the ESP root */
static void
fu_bootupd_plugin_esp_write_func(void)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(FuBootupdTestServer) server = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;

	tmpdir = fu_temporary_directory_new("bootupd-esp", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	filename = g_build_filename(fu_temporary_directory_get_path(tmpdir),
				    "EFI",
				    "fedora",
				    "fw",
				    "firmware.cap",
				    NULL);
	server = fu_bootupd_test_write_esp(fu_temporary_directory_get_path(tmpdir), filename);
	g_assert_cmpstr(server->partuuid, ==, FU_BOOTUPD_TEST_PARTUUID);
	g_assert_cmpstr(server->capsule_dir, ==, "EFI/fedora/fw");
}

/* a capsule written directly into the ESP root has an empty capsule directory */
static void
fu_bootupd_plugin_esp_write_root_func(void)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(FuBootupdTestServer) server = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;

	tmpdir = fu_temporary_directory_new("bootupd-esp", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	filename = g_build_filename(fu_temporary_directory_get_path(tmpdir), "firmware.cap", NULL);
	server = fu_bootupd_test_write_esp(fu_temporary_directory_get_path(tmpdir), filename);
	g_assert_cmpstr(server->partuuid, ==, FU_BOOTUPD_TEST_PARTUUID);
	g_assert_cmpstr(server->capsule_dir, ==, "");
}

/* a write to a path that is not within the ESP mount point is ignored */
static void
fu_bootupd_plugin_esp_write_outside_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(FU_TYPE_BOOTUPD_PLUGIN, ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTemporaryDirectory) outside = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(FuVolume) volume = NULL;
	g_autoptr(GBytes) bytes = g_bytes_new_static("hello", 5);
	g_autoptr(GError) error = NULL;

	fu_bootupd_plugin_set_varlink_address(FU_BOOTUPD_PLUGIN(plugin), g_get_tmp_dir());
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* register an ESP, then write a file that lives outside its mount point */
	tmpdir = fu_temporary_directory_new("bootupd-esp", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	outside = fu_temporary_directory_new("bootupd-out", &error);
	g_assert_no_error(error);
	g_assert_nonnull(outside);
	volume = fu_volume_new_from_mount_path(fu_temporary_directory_get_path(tmpdir));
	fu_volume_set_partition_uuid(volume, FU_BOOTUPD_TEST_PARTUUID);
	fu_context_add_esp_volume(ctx, volume);

	filename = g_build_filename(fu_temporary_directory_get_path(outside), "firmware.cap", NULL);
	g_test_expect_message(G_LOG_DOMAIN,
			      G_LOG_LEVEL_WARNING,
			      "failed to notify: */firmware.cap is not within *, ignoring");
	ret = fu_volume_write_file(volume, filename, bytes, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_test_assert_expected_messages();
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/bootupd/esp-write", fu_bootupd_plugin_esp_write_func);
	g_test_add_func("/fwupd/bootupd/esp-write-root", fu_bootupd_plugin_esp_write_root_func);
	g_test_add_func("/fwupd/bootupd/esp-write-outside",
			fu_bootupd_plugin_esp_write_outside_func);
	return g_test_run();
}
