/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 * Copyright 2024 Colin Kinloch <colin.kinloch@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <glib.h>

#include "fu-engine-request.h"
#include "glibconfig.h"
#include "gparcelable.h"
#define G_LOG_DOMAIN "FuMain"

#include "config.h"

#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <android/persistable_bundle.h>

// AOSP binder service management
#include <android/binder_ibinder.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#ifdef HAVE_GIO_UNIX
#include "fu-unix-seekable-input-stream.h"
#endif

#include "fu-binder-aidl.h"
#include "fu-binder-daemon.h"

/* this can be tested using:
 * ./build/release/binder-client -v -d /dev/hwbinder -n devices@1.0/org.freedesktop.fwupd
 */

G_DEFINE_AUTOPTR_CLEANUP_FUNC(AStatus, AStatus_delete)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(AParcel, AParcel_delete)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(APersistableBundle, APersistableBundle_delete)

#define DEFAULT_DEVICE "/dev/binder"

#define EVENT_LISTENER_IFACE "org.freedesktop.fwupd.IFwupdEventListener"

struct _FuBinderDaemon {
	FuDaemon parent_instance;
	gboolean async;
	gulong presence_id;
	AIBinder_Class *binder_class;
	AIBinder *binder;
	gint binder_fd;
	GPtrArray *event_listener_binders;
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
	g_autoptr(GVariant) maybe_device_array = NULL;
	GVariantIter iter;

	if (fu_engine_config_get_show_device_private(fu_engine_get_config(engine)))
		flags |= FWUPD_CODEC_FLAG_TRUSTED;
	if (devices != NULL) {
		g_autoptr(GVariant) tuple_device_array =
		    fwupd_codec_array_to_variant(devices, flags);

		g_variant_iter_init(&iter, tuple_device_array);
		device_array = g_variant_iter_next_value(&iter);
	}
	maybe_device_array = g_variant_new_maybe(G_VARIANT_TYPE("aa{sv}"), device_array);

	gp_parcel_write_variant(parcel, maybe_device_array, error);
}

static binder_status_t
fu_binder_daemon_method_get_devices(FuBinderDaemon *self,
				    FuEngineRequest *request,
				    const AParcel *in,
				    AParcel *out)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	g_autoptr(AStatus) status_header = AStatus_newOk();

	devices = fu_engine_get_devices(engine, &error);
	if (devices == NULL) {
		// TODO: How do we return meaningful aidl errors
		// fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		//*status = STATUS_FAILED_TRANSACTION;
		// TODO: This is received by the client as an empty array, should we do something
		// else?
		// return local_reply;
	}

	AParcel *packet_parcel = out;
	// TODO: Should I be writing success after success? (does it reset cursor position)
	AParcel_writeStatusHeader(out, status_header);
	fu_binder_daemon_device_array_to_persistable_bundle(self, request, devices, out, &error);

	return STATUS_OK;
}

static bool
parcel_string_allocator(void *stringData, int32_t length, char **buffer)
{
	char **outString = (char **)stringData;
	g_warning("string length is %d", length);

	if (length == 0)
		return false;

	if (length == -1) {
		*outString = NULL;
		return true;
	}

	*buffer = calloc(1, length);
	*outString = *buffer;

	return true;
}

static binder_status_t
fu_binder_daemon_method_install(FuBinderDaemon *self,
				FuEngineRequest *request,
				const AParcel *in,
				AParcel *out)
{
#ifdef HAVE_GIO_UNIX
	binder_status_t nstatus = STATUS_OK;
	gint firmware_fd = 0;
	g_autoptr(GInputStream) stream = NULL;
	// g_autoptr(FuMainAuthHelper) helper = NULL;
	const char *device_id = NULL;
	g_autoptr(GError) err = NULL;

	nstatus = AParcel_readString(in, &device_id, parcel_string_allocator);
	if (nstatus != STATUS_OK) {
		AStatus *status = AStatus_fromStatus(nstatus);
		g_warning("read parcel string status is %s", AStatus_getDescription(status));
	}

	g_warning("string is %s", device_id);
	device_id = "2082b5e0-7a64-478a-b1b2-e3404fab6dad";
	g_warning("pretend string is %s", device_id);

	nstatus = AParcel_readParcelFileDescriptor(in, &firmware_fd);
	if (nstatus != STATUS_OK) {
		AStatus *status = AStatus_fromStatus(nstatus);
		g_warning("read parcel fd status is %s", AStatus_getDescription(status));
	}
	g_warning("fd is %d", firmware_fd);

	/* create helper object */
	// TODO: Create helper object

	// TODO: Helper
	stream = fu_unix_seekable_input_stream_new(firmware_fd, TRUE);

	gint size = 0;
	GBytes *bytes = NULL;
	do {
		bytes = g_input_stream_read_bytes(stream, 512, NULL, &err);
		size += g_bytes_get_size(bytes);

		g_warning("firmware is of size %d error is %p", size, err);
	} while (g_bytes_get_size(bytes) > 0);

	// if (!fu_dbus_daemon_install_with_helper(g_steal_pointer(&helper), &error)) {
	//	fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
	//	return;
	// }

	return STATUS_OK;
#endif
}

static void *
listener_on_create(void *arg)
{
	return arg;
}
static void
listener_on_destroy(void *arg)
{
	// TODO: Clean up ???
}

static binder_status_t
listener_on_transact(AIBinder *binder, transaction_code_t code, const AParcel *in, AParcel *out)
{
	// TODO: Do we need to transact??
}

static AIBinder_Class *
get_listener_class()
{
	static AIBinder_Class *listener_class = NULL;
	if (!listener_class) {
		listener_class = AIBinder_Class_define(EVENT_LISTENER_IFACE,
						       listener_on_create,
						       listener_on_destroy,
						       listener_on_transact);
	}

	return listener_class;
}

typedef struct ListenerDeathCookie {
	FuBinderDaemon *daemon;
	AIBinder *listener_binder;
} ListenerDeathCookie;

static void
listener_on_binder_died(void *cookie)
{
	ListenerDeathCookie *death_cookie = cookie;
	// TODO: The cookie should have access to daemon->listeners list and the listener we're
	// deleting
	g_warning("listener is dead %p %d",
		  cookie,
		  death_cookie->daemon->event_listener_binders->len);

	g_ptr_array_remove(death_cookie->daemon->event_listener_binders,
			   death_cookie->listener_binder);
}

static void
listener_death_recipient_on_unlinked(void *cookie)
{
	g_warning("put the cookie dawn %p", cookie);
	g_free(cookie);
}

static binder_status_t
fu_binder_daemon_method_add_event_listener(FuBinderDaemon *self,
					   FuEngineRequest *request,
					   const AParcel *in,
					   AParcel *out)
{
	// TODO: Get binder service from `in`
	AIBinder *event_listener_remote_object = NULL;
	binder_status_t nstatus = STATUS_OK;
	nstatus = AParcel_readStrongBinder(in, &event_listener_remote_object);
	if (nstatus != STATUS_OK) {
		AStatus *status = AStatus_fromStatus(nstatus);
		g_warning("failed to read strong binder %s", AStatus_getDescription(status));
	}
	g_info("strong binder  %p\n", event_listener_remote_object);

	// BtHfpAgCallbacks_associateClass
	const AIBinder_Class *listener_class = get_listener_class();
	AIBinder_associateClass(event_listener_remote_object, listener_class);

	g_ptr_array_add(self->event_listener_binders, event_listener_remote_object);

	// TODO: events seem slow, do I need to pump an fd??? ABinderProcess_setupPolling
	//   or is our pumping blocking the thread?

	ListenerDeathCookie *death_cookie = g_malloc0(sizeof(ListenerDeathCookie));
	death_cookie->listener_binder = event_listener_remote_object;
	death_cookie->daemon = self;

	AIBinder_DeathRecipient *dr = AIBinder_DeathRecipient_new(listener_on_binder_died);
	AIBinder_DeathRecipient_setOnUnlinked(dr, listener_death_recipient_on_unlinked);
	AIBinder_linkToDeath(event_listener_remote_object, dr, death_cookie);

	// TODO: die
	// AIBinder_linkToDeath(AIBinder *binder, AIBinder_DeathRecipient *recipient, void *cookie)
	return STATUS_OK;
}

static FuEngineRequest *
fu_binder_daemon_create_request(FuBinderDaemon *self, const gchar *sender, GError **error)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(sender);

	// TODO: Check if sender is trusted
	fu_engine_request_set_converter_flags(request, FWUPD_CODEC_FLAG_TRUSTED);

	return g_steal_pointer(&request);
}

typedef binder_status_t (*FuBinderDaemonMethodFunc)(FuBinderDaemon *self,
						    FuEngineRequest *request,
						    const AParcel *in,
						    AParcel *out);

static void
fu_binder_daemon_send_codec_event(FuBinderDaemon *self, FwupdCodec *codec, guint32 transaction_id)
{
	AIBinder *event_listener_binder;
	AParcel *in = NULL;
	gsize size;
	g_autofree uint8_t *buffer = NULL;
	// TODO: Reuse parcel????
	g_autoptr(GError) error = NULL;

	GVariant *val;
	binder_status_t nstatus = -1;

	g_info("send event %d", transaction_id);

	if (self->event_listener_binders->len == 0)
		return;

	val = fwupd_codec_to_variant(codec, FWUPD_CODEC_FLAG_NONE);

	AIBinder_prepareTransaction(event_listener_binder, &in);
	gp_parcel_write_variant(in, val, &error);

	for (guint i = 0; i < self->event_listener_binders->len; i++) {
		AParcel *out = NULL;
		event_listener_binder = g_ptr_array_index(self->event_listener_binders, i);
		AIBinder_transact(event_listener_binder, transaction_id, &in, &out, 0);
	}
}

static void
fu_binder_daemon_engine_changed_cb(FuEngine *engine, FuBinderDaemon *self)
{
	AIBinder *event_listener_binder;
	AParcel *in = NULL;

	if (self->event_listener_binders->len == 0)
		return;

	AIBinder_prepareTransaction(event_listener_binder, &in);

	for (guint i = 0; i < self->event_listener_binders->len; i++) {
		AParcel *out = NULL;
		event_listener_binder = g_ptr_array_index(self->event_listener_binders, i);
		AIBinder_transact(event_listener_binder,
				  FWUPD_BINDER_LISTENER_CALL_ON_CHANGED,
				  &in,
				  &out,
				  0);
	}

	fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void
fu_binder_daemon_engine_device_added_cb(FuEngine *engine, FuDevice *device, FuBinderDaemon *self)
{
	g_debug("added cb");
	fu_binder_daemon_send_codec_event(self,
					  FWUPD_CODEC(device),
					  FWUPD_BINDER_LISTENER_CALL_ON_DEVICE_ADDED);
	fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void
fu_binder_daemon_engine_device_removed_cb(FuEngine *engine, FuDevice *device, FuBinderDaemon *self)
{
	g_debug("removed cb");
	fu_binder_daemon_send_codec_event(self,
					  FWUPD_CODEC(device),
					  FWUPD_BINDER_LISTENER_CALL_ON_DEVICE_REMOVED);
	fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void
fu_binder_daemon_engine_device_changed_cb(FuEngine *engine, FuDevice *device, FuBinderDaemon *self)
{
	g_debug("changed cb");
	fu_binder_daemon_send_codec_event(self,
					  FWUPD_CODEC(device),
					  FWUPD_BINDER_LISTENER_CALL_ON_DEVICE_CHANGED);
	fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void
fu_binder_daemon_engine_device_request_cb(FuEngine *engine,
					  FwupdRequest *request,
					  FuBinderDaemon *self)
{
	g_debug("request cb");
	fu_binder_daemon_send_codec_event(self,
					  FWUPD_CODEC(request),
					  FWUPD_BINDER_LISTENER_CALL_ON_DEVICE_REQUEST);
	fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void
fu_binder_daemon_engine_status_changed_cb(FuEngine *engine,
					  FwupdStatus status,
					  FuBinderDaemon *self)
{
	// fu_dbus_daemon_set_status(self, status);
	//  TODO: fu_binder_set_status

	/* engine has gone idle */
	if (status == FWUPD_STATUS_SHUTDOWN)
		fu_daemon_stop(FU_DAEMON(self), NULL);
}

static int
poll_binder_process(void *user_data)
{
	// Daemon *daemon = user_data;
	FuBinderDaemon *daemon = user_data;
	binder_status_t nstatus = STATUS_OK;
	if (daemon->binder_fd < 0)
		return G_SOURCE_CONTINUE;

	nstatus = ABinderProcess_handlePolledCommands();

	if (nstatus != STATUS_OK) {
		AStatus *status = AStatus_fromStatus(nstatus);
		g_warning("failed to handle polled commands %s", AStatus_getDescription(status));
	}

	return G_SOURCE_CONTINUE;
}

static gboolean
fu_binder_daemon_stop(FuDaemon *daemon, GError **error)
{
	FuBinderDaemon *self = FU_BINDER_DAEMON(daemon);

	// TODO: Clean up self->event_listener_binders

	return TRUE;
}

static gboolean
fu_binder_daemon_start(FuDaemon *daemon, GError **error)
{
	FuBinderDaemon *self = FU_BINDER_DAEMON(daemon);
	// TODO: Add service here

	// TODO: die
	// AIBinder_linkToDeath(AIBinder *binder, AIBinder_DeathRecipient *recipient, void *cookie)

	// Daemon *daemon = AIBinder_getUserData(self->binder);

	// TODO: Handle reconnecting service daemon

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
	binder_status_t nstatus = STATUS_OK;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_profile(progress, g_getenv("FWUPD_VERBOSE") != NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 99, "load-engine");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "create-sm");

	/* load engine */
	g_signal_connect(FU_ENGINE(engine),
			 "changed",
			 G_CALLBACK(fu_binder_daemon_engine_changed_cb),
			 self);
	g_signal_connect(FU_ENGINE(engine),
			 "device-added",
			 G_CALLBACK(fu_binder_daemon_engine_device_added_cb),
			 self);
	g_signal_connect(FU_ENGINE(engine),
			 "device-removed",
			 G_CALLBACK(fu_binder_daemon_engine_device_removed_cb),
			 self);
	g_signal_connect(FU_ENGINE(engine),
			 "device-changed",
			 G_CALLBACK(fu_binder_daemon_engine_device_changed_cb),
			 self);
	g_signal_connect(FU_ENGINE(engine),
			 "device-request",
			 G_CALLBACK(fu_binder_daemon_engine_device_request_cb),
			 self);
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
	// Do we need service started here???
	// TODO: Wait for service manager?
	self->binder = AIBinder_new(self->binder_class, self);

	nstatus = AServiceManager_addService(self->binder, DEFAULT_NAME);
	if (nstatus != STATUS_OK) {
		AStatus *status = AStatus_fromStatus(nstatus);
		g_warning("failed to add service %s", AStatus_getDescription(status));
	}

	AIBinder *binder_check = AServiceManager_checkService(DEFAULT_NAME);
	g_warning("service check %p", (void *)binder_check);

	ABinderProcess_setupPolling(&self->binder_fd);
	g_idle_add(poll_binder_process, self);

	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static int
method_send_fd(AIBinder *binder, transaction_code_t code, const AParcel *in, AParcel *out)
{
}

static void *
binder_class_on_create(void *user_data)
{
	// Daemon *daemon = calloc(1, sizeof(Daemon));
	// return daemon;
	return user_data;
}

static void
binder_class_on_destroy(void *user_data)
{
	// Daemon *daemon = user_data;
	// free(daemon);
	g_warning("binder on_destroy %p", user_data);
}

static int
binder_class_on_transact(AIBinder *binder, transaction_code_t code, const AParcel *in, AParcel *out)
{
	FuBinderDaemon *daemon = AIBinder_getUserData(binder);
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(daemon));
	g_autoptr(FuEngineRequest) request = NULL;
	g_autoptr(GError) error = NULL;

	g_warning("transaction is %u", code);
	static const FuBinderDaemonMethodFunc method_funcs[] = {
	    NULL,
	    fu_binder_daemon_method_get_devices,	/* getDevices */
	    fu_binder_daemon_method_install,		/* install */
	    fu_binder_daemon_method_add_event_listener, /* addEventListener */
	};

	/* build request */
	request = fu_binder_daemon_create_request(daemon, "sender?", &error);

	if (request == NULL) {
		// fu_dbus_daemon_method_invocation_return_gerror(invocation, error);
		//*status = STATUS_FAILED_TRANSACTION;
		// return NULL;
		// TODO: set out to NULL?
		return STATUS_FAILED_TRANSACTION;
	}

	/* activity */
	fu_engine_idle_reset(engine);

	// TODO: surely `code <= 0`???
	if (code < 0 && code > G_N_ELEMENTS(method_funcs)) {
		return STATUS_INVALID_OPERATION;
	}

	return method_funcs[code](daemon, request, in, out);
}

static void
fu_binder_daemon_init(FuBinderDaemon *self)
{
	self->event_listener_binders =
	    g_ptr_array_new_with_free_func((GDestroyNotify)AIBinder_decStrong);
	// TODO: lifetime? and reconnection
	self->binder_class = AIBinder_Class_define(DEFAULT_IFACE,
						   binder_class_on_create,
						   binder_class_on_destroy,
						   binder_class_on_transact);
}

static void
fu_binder_daemon_finalize(GObject *obj)
{
	FuBinderDaemon *self = FU_BINDER_DAEMON(obj);

	if (self->binder != NULL)
		AIBinder_decStrong(self->binder);

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
