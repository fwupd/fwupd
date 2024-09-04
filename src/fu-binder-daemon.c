/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuMain"

#include "config.h"

#include <gbinder.h>

#include "fu-binder-daemon.h"

/* this can be tested using:
 * ./build/release/binder-client -v -d /dev/hwbinder -n devices@1.0/org.freedesktop.fwupd
 */

#define DEFAULT_DEVICE "/dev/hwbinder"
#define DEFAULT_NAME   "org.freedesktop.fwupd"
#define DEFAULT_IFACE  "devices@1.0"

#define BINDER_TRANSACTION(c2, c3, c4) GBINDER_FOURCC('_', c2, c3, c4)
#define BINDER_DUMP_TRANSACTION	       BINDER_TRANSACTION('D', 'M', 'P')

struct _FuBinderDaemon {
	FuDaemon parent_instance;
	gboolean async;
	gulong presence_id;
	GBinderServiceManager *sm;
	GBinderLocalObject *obj;
};

G_DEFINE_TYPE(FuBinderDaemon, fu_binder_daemon, FU_TYPE_DAEMON)

static GBinderLocalReply *
fu_binder_daemon_app_reply(GBinderLocalObject *obj,
			   GBinderRemoteRequest *req,
			   guint code,
			   guint flags,
			   int *status,
			   gpointer user_data)
{
	GBinderReader reader;

	gbinder_remote_request_init_reader(req, &reader);
	if (code == GBINDER_FIRST_CALL_TRANSACTION) {
		const gchar *iface = gbinder_remote_request_interface(req);
		if (g_strcmp0(iface, DEFAULT_IFACE) == 0) {
			GBinderLocalReply *reply = gbinder_local_object_new_reply(obj);
			g_autofree gchar *str = gbinder_reader_read_string16(&reader);
			g_autofree gchar *str2 = g_strdup_printf("I think you said '%s'", str);

			g_debug("%s %u", iface, code);
			g_debug("%s", str);
			*status = 0;
			gbinder_local_reply_append_string16(reply, str2);
			return reply;
		}
		g_debug("unexpected interface %s", iface);
	} else if (code == BINDER_DUMP_TRANSACTION) {
		int fd = gbinder_reader_read_fd(&reader);
		const gchar *dump = "Sorry, I've got nothing to dump...\n";
		const gssize dump_len = strlen(dump);

		g_debug("dump request from %d", gbinder_remote_request_sender_pid(req));
		if (write(fd, dump, dump_len) != dump_len) {
			g_warning("failed to write dump: %s", strerror(errno));
		}
		*status = 0;
		return NULL;
	}
	*status = -1;
	return NULL;
}

static void
fu_binder_daemon_app_add_service_done(GBinderServiceManager *sm, int status, gpointer user_data)
{
	FuBinderDaemon *self = FU_BINDER_DAEMON(user_data);
	if (status == GBINDER_STATUS_OK) {
		g_info("added %s", DEFAULT_NAME);
	} else {
		g_warning("failed to add %s (%d)", DEFAULT_NAME, status);
		fu_daemon_stop(FU_DAEMON(self), NULL);
	}
}

static void
fu_binder_daemon_app_sm_presence_handler_cb(GBinderServiceManager *sm, gpointer user_data)
{
	FuBinderDaemon *self = FU_BINDER_DAEMON(user_data);

	if (gbinder_servicemanager_is_present(self->sm)) {
		g_info("SM has reappeared");
		gbinder_servicemanager_add_service(self->sm,
						   DEFAULT_NAME,
						   self->obj,
						   fu_binder_daemon_app_add_service_done,
						   self);
	} else {
		g_info("SM has died");
	}
}

static gboolean
fu_binder_daemon_stop(FuDaemon *daemon, GError **error)
{
	FuBinderDaemon *self = FU_BINDER_DAEMON(daemon);
	if (self->presence_id != 0) {
		gbinder_servicemanager_remove_handler(self->sm, self->presence_id);
		self->presence_id = 0;
	}
	return TRUE;
}

static gboolean
fu_binder_daemon_start(FuDaemon *daemon, GError **error)
{
	FuBinderDaemon *self = FU_BINDER_DAEMON(daemon);
	self->presence_id =
	    gbinder_servicemanager_add_presence_handler(self->sm,
							fu_binder_daemon_app_sm_presence_handler_cb,
							self);
	gbinder_servicemanager_add_service(self->sm,
					   DEFAULT_NAME,
					   self->obj,
					   fu_binder_daemon_app_add_service_done,
					   self);
	return TRUE;
}

static gboolean
fu_binder_daemon_setup(FuDaemon *daemon,
		       const gchar *socket_address,
		       FuProgress *progress,
		       GError **error)
{
	FuBinderDaemon *self = FU_BINDER_DAEMON(daemon);
	FuEngine *engine = fu_daemon_get_engine(daemon);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_profile(progress, g_getenv("FWUPD_VERBOSE") != NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 99, "load-engine");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "create-sm");

	/* load engine */
	if (!fu_engine_load(engine,
			    FU_ENGINE_LOAD_FLAG_NONE,
			    fu_progress_get_child(progress),
			    error)) {
		g_prefix_error(error, "failed to load engine: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	g_info("waiting for SM");
	if (!gbinder_servicemanager_wait(self->sm, -1)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_BROKEN_SYSTEM,
				    "failed to wait for service manager");
		return FALSE;
	}
	g_debug("waited for SM, creating local object");
	self->obj = gbinder_servicemanager_new_local_object(self->sm,
							    DEFAULT_IFACE,
							    fu_binder_daemon_app_reply,
							    self);
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_binder_daemon_init(FuBinderDaemon *self)
{
	self->sm = gbinder_servicemanager_new(DEFAULT_DEVICE);
}

static void
fu_binder_daemon_finalize(GObject *obj)
{
	FuBinderDaemon *self = FU_BINDER_DAEMON(obj);

	if (self->obj != NULL)
		gbinder_local_object_unref(self->obj);
	gbinder_servicemanager_unref(self->sm);

	G_OBJECT_CLASS(fu_binder_daemon_parent_class)->finalize(obj);
}

static void
fu_binder_daemon_class_init(FuBinderDaemonClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDaemonClass *daemon_class = FU_DAEMON_CLASS(klass);
	object_class->finalize = fu_binder_daemon_finalize;
	daemon_class->setup = fu_binder_daemon_setup;
	daemon_class->start = fu_binder_daemon_start;
	daemon_class->stop = fu_binder_daemon_stop;
}

FuDaemon *
fu_daemon_new(void)
{
	return FU_DAEMON(g_object_new(FU_TYPE_BINDER_DAEMON, NULL));
}
