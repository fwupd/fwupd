/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuMain"

#include "config.h"

#include <gbinder.h>

#include "fu-debug.h"

#define DEFAULT_DEVICE "/dev/hwbinder"
#define DEFAULT_NAME   "org.freedesktop.fwupd"
#define DEFAULT_IFACE  "devices@1.0"

typedef struct self {
	gchar *fqname;
	GMainLoop *loop;
	GBinderServiceManager *sm;
	GBinderLocalObject *local;
	GBinderRemoteObject *remote;
	gulong wait_id;
	gulong death_id;
	GBinderClient *client;
	guint poll_id;
} FuUtil;

static void
fu_self_free(FuUtil *self)
{
	if (self->sm != NULL)
		gbinder_servicemanager_unref(self->sm);
	g_main_loop_unref(self->loop);
	if (self->poll_id != 0)
		g_source_remove(self->poll_id);
	gbinder_remote_object_remove_handler(self->remote, self->death_id);
	gbinder_remote_object_unref(self->remote);
	gbinder_local_object_drop(self->local);
	gbinder_client_unref(self->client);
	g_free(self->fqname);
	g_free(self);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuUtil, fu_self_free)

static void
fu_binder_client_app_remote_died(GBinderRemoteObject *obj, gpointer user_data)
{
	FuUtil *self = (FuUtil *)user_data;
	g_info("remote has died, exiting...");
	g_main_loop_quit(self->loop);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GBinderRemoteReply, gbinder_remote_reply_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GBinderLocalRequest, gbinder_local_request_unref)

static void
fu_binder_client_app_call(FuUtil *self, const gchar *str)
{
	int status;
	g_autoptr(GBinderLocalRequest) req = gbinder_client_new_request(self->client);
	g_autoptr(GBinderRemoteReply) reply = NULL;

	gbinder_local_request_append_string16(req, str);
	reply = gbinder_client_transact_sync_reply(self->client,
						   GBINDER_FIRST_CALL_TRANSACTION,
						   req,
						   &status);
	if (status == GBINDER_STATUS_OK) {
		GBinderReader reader;
		g_autofree gchar *ret = NULL;

		gbinder_remote_reply_init_reader(reply, &reader);
		ret = gbinder_reader_read_string16(&reader);
		g_debug("Reply: %s", ret);
	} else {
		g_warning("status %d", status);
	}
}

static gboolean
fu_binder_client_poll_cb(gpointer user_data)
{
	FuUtil *self = (FuUtil *)user_data;
	fu_binder_client_app_call(self, "hello dave");
	return G_SOURCE_CONTINUE;
}

static gboolean
fu_binder_client_app_connect_remote(FuUtil *self)
{
	self->remote = gbinder_servicemanager_get_service_sync(self->sm,
							       self->fqname,
							       NULL); /* autoreleased pointer */
	if (self->remote != NULL) {
		g_info("connected to %s", self->fqname);
		gbinder_remote_object_ref(self->remote);
		self->client = gbinder_client_new(self->remote, DEFAULT_IFACE);
		self->death_id =
		    gbinder_remote_object_add_death_handler(self->remote,
							    fu_binder_client_app_remote_died,
							    self);
		self->poll_id = g_timeout_add_seconds(3, fu_binder_client_poll_cb, self);
		return TRUE;
	}
	return FALSE;
}

static void
fu_binder_client_app_registration_handler(GBinderServiceManager *sm,
					  const gchar *name,
					  gpointer user_data)
{
	FuUtil *self = (FuUtil *)user_data;
	g_debug("%s appeared", name);
	if (!strcmp(name, self->fqname) && fu_binder_client_app_connect_remote(self)) {
		gbinder_servicemanager_remove_handler(self->sm, self->wait_id);
		self->wait_id = 0;
	}
}

int
main(int argc, char *argv[])
{
	g_autoptr(FuUtil) self = g_new0(FuUtil, 1);
	g_autoptr(GOptionContext) context = g_option_context_new(NULL);
	g_autoptr(GError) error = NULL;

	g_option_context_add_group(context, fu_debug_get_option_group());
	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_printerr("Failed to parse arguments: %s\n", error->message);
		return EXIT_FAILURE;
	}

	self->sm = gbinder_servicemanager_new(DEFAULT_DEVICE);
	if (self->sm == NULL) {
		g_printerr("failed to get service manager\n");
		return EXIT_FAILURE;
	}
	self->local = gbinder_servicemanager_new_local_object(self->sm, NULL, NULL, NULL);
	self->fqname = g_strconcat(DEFAULT_IFACE, "/", DEFAULT_NAME, NULL);
	if (!fu_binder_client_app_connect_remote(self)) {
		g_info("waiting for %s", self->fqname);
		self->wait_id = gbinder_servicemanager_add_registration_handler(
		    self->sm,
		    self->fqname,
		    fu_binder_client_app_registration_handler,
		    self);
	}

	self->loop = g_main_loop_new(NULL, TRUE);
	g_main_loop_run(self->loop);
	return 0;
}
