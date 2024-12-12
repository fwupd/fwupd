/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 * Copyright 2024 Colin Kinloch <colin.kinloch@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "fu-engine-request.h"
#include "gbinder_types.h"
#include "glib.h"
#include "glibconfig.h"
#include "gparcelable.h"
#define G_LOG_DOMAIN "FuMain"

#include "config.h"

#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <android/persistable_bundle.h>
#include <gbinder.h>

#include "fu-binder-aidl.h"
#include "fu-binder-daemon.h"

/* this can be tested using:
 * ./build/release/binder-client -v -d /dev/hwbinder -n devices@1.0/org.freedesktop.fwupd
 */

G_DEFINE_AUTOPTR_CLEANUP_FUNC(AStatus, AStatus_delete)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(AParcel, AParcel_delete)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(APersistableBundle, APersistableBundle_delete)

#define DEFAULT_DEVICE "/dev/binder"
#define DEFAULT_IFACE  "org.freedesktop.fwupd.IFwupd"
#define DEFAULT_NAME   "fwupd"

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

static void
fu_binder_daemon_device_array_to_persistable_bundle(FuBinderDaemon *self,
						    FuEngineRequest *request,
						    GPtrArray *devices,
						    AParcel *parcel,
						    GError **error)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	FwupdCodecFlags flags = fu_engine_request_get_converter_flags(request);
	g_autoptr(GVariant) device_array = NULL;
	GVariantIter iter;

	// TODO: Clean up intermediate variants
	if (fu_engine_config_get_show_device_private(fu_engine_get_config(engine)))
		flags |= FWUPD_CODEC_FLAG_TRUSTED;
	if (devices != NULL) {
		g_autoptr(GVariant) variant = fwupd_codec_array_to_variant(devices, flags);

		g_variant_iter_init(&iter, variant);
		device_array = g_variant_iter_next_value(&iter);
	}
	GVariant *variant = g_variant_new_maybe(G_VARIANT_TYPE("aa{sv}"), device_array);

	gp_parcel_write_variant(parcel, variant, error);
}

static GBinderLocalReply *
fu_binder_daemon_method_get_devices(FuBinderDaemon *self,
				    GBinderRemoteRequest *remote_request,
				    FuEngineRequest *request,
				    int *status)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	// g_autoptr(AParcel) val = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	GBinderLocalReply *local_reply = gbinder_local_object_new_reply(self->obj);
	GBinderWriter packet_writer;
	gsize size;
	binder_status_t nstatus = STATUS_OK;

	devices = fu_engine_get_devices(engine, &error);
	if (devices == NULL) {
		// TODO: How do we return meaningful aidl errors
		// fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		//*status = GBINDER_STATUS_FAILED;
		// TODO: This is received by the client as an empty array, should we do something
		// else?
		// return local_reply;
	}

	g_autoptr(AStatus) status_header = AStatus_newOk();
	AParcel *packet_parcel = AParcel_create();

	AParcel_writeStatusHeader(packet_parcel, status_header);
	fu_binder_daemon_device_array_to_persistable_bundle(self,
							    request,
							    devices,
							    packet_parcel,
							    &error);

	gbinder_local_reply_init_writer(local_reply, &packet_writer);

	size = AParcel_getDataSize(packet_parcel);
	g_autofree uint8_t *buffer = calloc(1, size);
	nstatus = AParcel_marshal(packet_parcel, buffer, 0, size);
	if (nstatus != STATUS_OK) {
		g_warning("Failed to marshal parcel %d", nstatus);
	}

	gbinder_writer_append_bytes(&packet_writer, buffer, size);

	return local_reply;
}

static GBinderLocalReply *
fu_binder_daemon_method_add_event_listener(FuBinderDaemon *self,
					   GBinderRemoteRequest *remote_request,
					   FuEngineRequest *request,
					   int *status)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	GBinderLocalReply *local_reply = gbinder_local_object_new_reply(self->obj);

	g_warning("unimplemented method");

	return local_reply;
}

static GBinderLocalReply *
fu_binder_daemon_method_unimplemented(FuBinderDaemon *self,
				      GBinderRemoteRequest *remote_request,
				      FuEngineRequest *request,
				      int *status)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	GBinderLocalReply *local_reply = gbinder_local_object_new_reply(self->obj);

	g_warning("unimplemented method");

	return local_reply;
}

static FuEngineRequest *
fu_binder_daemon_create_request(FuBinderDaemon *self, const gchar *sender, GError **error)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(sender);

	// TODO: Check if sender is trusted
	fu_engine_request_set_converter_flags(request, FWUPD_CODEC_FLAG_TRUSTED);

	return g_object_ref(request);
}

typedef GBinderLocalReply *(*FuBinderDaemonMethodFunc)(FuBinderDaemon *self,
						       GBinderRemoteRequest *remote_request,
						       FuEngineRequest *request,
						       int *status);

static GBinderLocalReply *
fu_binder_daemon_method_call(GBinderLocalObject *daemon_object,
			     GBinderRemoteRequest *remote_request,
			     guint code,
			     guint flags,
			     int *status,
			     gpointer user_data)
{
	FuBinderDaemon *self = FU_BINDER_DAEMON(user_data);
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	g_autoptr(FuEngineRequest) request = NULL;
	g_autoptr(GError) error = NULL;

	GBinderReader reader;
	const gchar *iface = gbinder_remote_request_interface(remote_request);
	gbinder_remote_request_init_reader(remote_request, &reader);

	/* Keep these in the same order as in the aidl file */
	FuBinderDaemonMethodFunc method_funcs[] = {
	    NULL,
	    fu_binder_daemon_method_get_devices,	/* getDevices */
	    fu_binder_daemon_method_add_event_listener, /* addEventListener */
	    fu_binder_daemon_method_unimplemented,	/* removeEventListener */
	};

	/* build request */
	request = fu_binder_daemon_create_request(self, "sender?", &error);
	if (request == NULL) {
		// fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		*status = GBINDER_STATUS_FAILED;
		return NULL;
	}

	/* activity */
	fu_engine_idle_reset(engine);

	if (g_strcmp0(iface, DEFAULT_IFACE) == 0) {
		if (code > 0 && code <= G_N_ELEMENTS(method_funcs) && method_funcs[code]) {
			return method_funcs[code](self, remote_request, request, status);
		}

		g_debug("unexpected interface %s", iface);
	} else if (code == BINDER_DUMP_TRANSACTION) {
		int fd = gbinder_reader_read_fd(&reader);
		const gchar *dump = "Sorry, I've got nothing to dump...\n";
		const gssize dump_len = strlen(dump);

		g_debug("dump request from %d", gbinder_remote_request_sender_pid(remote_request));
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
		g_info("added %s", DEFAULT_IFACE "/" DEFAULT_NAME);
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
			    FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_HWINFO |
				FU_ENGINE_LOAD_FLAG_REMOTES | FU_ENGINE_LOAD_FLAG_EXTERNAL_PLUGINS |
				FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS |
				FU_ENGINE_LOAD_FLAG_ENSURE_CLIENT_CERT |
				FU_ENGINE_LOAD_FLAG_DEVICE_HOTPLUG,
			    fu_progress_get_child(progress),
			    error)) {
		g_prefix_error(error, "failed to load engine: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	g_info("waiting for SM");
	if (!gbinder_servicemanager_wait(self->sm, 0)) {
		g_warning("we didn't connect, and that's okay. Lets just be a dumb fwupd daemon");
		//g_set_error_literal(error,
		//		    FWUPD_ERROR,
		//		    FWUPD_ERROR_BROKEN_SYSTEM,
		//		    "failed to wait for service manager");
		//return FALSE;
	} else {
		g_debug("waited for SM, creating local object");
		self->obj = gbinder_servicemanager_new_local_object(self->sm,
								    DEFAULT_IFACE,
								    fu_binder_daemon_method_call,
								    self);
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_binder_daemon_init(FuBinderDaemon *self)
{
	self->sm = gbinder_servicemanager_new2(DEFAULT_DEVICE, "aidl3", "aidl3");
	// self->sm = gbinder_servicemanager_new(DEFAULT_DEVICE);
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
