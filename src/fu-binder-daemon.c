/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 * Copyright 2024 Colin Kinloch <colin.kinloch@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "fu-engine-request.h"
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

#define EVENT_LISTENER_IFACE "org.freedesktop.fwupd.IFwupdEventListener"

#define BINDER_TRANSACTION(c2, c3, c4) GBINDER_FOURCC('_', c2, c3, c4)
#define BINDER_DUMP_TRANSACTION	       BINDER_TRANSACTION('D', 'M', 'P')

enum event_listener_transactions {
	EVENT_LISTENER_ON_CHANGED = GBINDER_FIRST_CALL_TRANSACTION,
	EVENT_LISTENER_ON_DEVICE_ADDED,
	EVENT_LISTENER_ON_DEVICE_REMOVED,
	EVENT_LISTENER_ON_DEVICE_CHANGED,
	EVENT_LISTENER_ON_DEVICE_REQUEST,
};

struct _FuBinderDaemon {
	FuDaemon parent_instance;
	gboolean async;
	gulong presence_id;
	GBinderServiceManager *sm;
	GBinderLocalObject *obj;
	GPtrArray *event_listener_remote_objects;
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

// TODO: I have so far failed to receive an fd over AIDL
static GBinderLocalReply *
fu_binder_daemon_method_install(FuBinderDaemon *self,
				GBinderRemoteRequest *remote_request,
				FuEngineRequest *request,
				int *status)
{
	GBinderLocalReply *local_reply = gbinder_local_object_new_reply(self->obj);
	binder_status_t nstatus = STATUS_OK;
	GBinderReader reader;
	gsize size = 0;
	char *guid = NULL;
	gint fd = -1;

	g_warning("install firmware fd");

	gbinder_remote_request_init_reader(remote_request, &reader);
	guid = gbinder_reader_read_string16(&reader);
	g_warning("guid is %s", guid);
	fd = gbinder_reader_read_fd(&reader);
	// gint fd = gbinder_reader_read_dup_fd(&reader);
	g_warning("fd is %d", fd);

	// TODO: AParcel_unmarshal and read fd
	gbinder_remote_request_init_reader(remote_request, &reader);

	// gbinder reader data returns full buffer, to unmarshal we need to derive the offset
	const void *buffer = gbinder_reader_get_data(&reader, &size);
	const gsize offset = size - gbinder_reader_bytes_remaining(&reader);
#if 0
	// Debug print parcel
	for (gsize i = 0; i < size ; i++) {
		const guint8 value = ((const guint8 *)buffer + offset)[i];
		g_warning("value is (%u) (%#0x) %c", value, value, value);
	}
#endif
	g_autoptr(AParcel) parcel = AParcel_create();
	nstatus = AParcel_unmarshal(parcel, (const guint8 *)buffer + offset, size - offset);
	if (nstatus != STATUS_OK) {
		g_warning("Failed to unmarshal parcel %d", nstatus);
	}

	// Reset cursor position to start of parcel after unmarshal
	nstatus = AParcel_setDataPosition(parcel, 0);
	if (nstatus != STATUS_OK) {
		g_warning("Failed to set parcel position to zero %d: %s",
			  nstatus,
			  g_strerror(-nstatus));
	}

	nstatus = AParcel_readString(parcel, (void *)(&guid), parcel_string_allocator);
	if (nstatus != STATUS_OK) {
		g_warning("failed to read guid string %d (%s)", nstatus, g_strerror(-nstatus));
	}
	g_warning("guid is %s", guid);

	// Debug print the rest of the parcel
	gint pos = AParcel_getDataPosition(parcel);
	for (gint32 i = pos; i < AParcel_getDataSize(parcel); i++) {
		gint8 value;
		// WARNING: AParcel_readByte increments 4 per byte
		nstatus = AParcel_setDataPosition(parcel, i);
		AParcel_readByte(parcel, &value);
		g_warning("value is (%u) (%#0x) %c", value, value, value);
	}
	nstatus = AParcel_setDataPosition(parcel, pos);

	// TODO: This returns failed to read parcel fd Status(-129, EX_TRANSACTION_FAILED):
	// 'BAD_TYPE: ' And logcat  W Parcel  : Attempt to read object from Parcel
	// 0xb400007b1bf0c570 at offset 156 that is not in the object list Printing the bytes the
	// BINDER_TYPE_FD header is present in reverse:
	//  0x85, '*', 'd', 'f'
	//  https://cs.android.com/android/platform/superproject/main/+/main:bionic/libc/kernel/uapi/linux/android/binder.h;l=18;drc=b4d6320e2ae398b36f0aaafb2ecd83609d2d99af
	// What does this mean? "fd (-arrays) must always appear in the meta-data list (eg touched
	// by the kernel)"
	//  https://cs.android.com/android/platform/superproject/main/+/main:system/libhwbinder/Parcel.cpp;l=1225?q=%22that%20is%20not%20in%20the%20object%20list%22&ss=android%2Fplatform%2Fsuperproject%2Fmain
	// The value seems to be binder_fd_object
	//  https://cs.android.com/android/platform/superproject/main/+/main:bionic/libc/kernel/uapi/linux/android/binder.h;l=51;drc=b4d6320e2ae398b36f0aaafb2ecd83609d2d99af
	// header, pad to 64 bit, union {fd, pad_binder (64 bit)}, cookie (64 bit)
	// hdr: {0x85, '*', 'd', 'f'}, fd: 0xf, cookie: 0x1
	// Maybe I should test whether libbinder_ndk.so session management works better
	nstatus = AParcel_readParcelFileDescriptor(parcel, &fd);
	if (nstatus != STATUS_OK) {
		g_autoptr(AStatus) ystatus = AStatus_fromStatus(nstatus);
		g_warning("failed to read parcel fd %s \n%s",
			  AStatus_getDescription(ystatus),
			  AStatus_getMessage(ystatus));
	}

	g_warning("fd is %d", fd);

	// TODO: read options Bundle
	gint32 bundle_not_null = 0;
	nstatus = AParcel_readInt32(parcel, &bundle_not_null);
	if (bundle_not_null) {
		gint32 bundle_value = 0;
		g_autoptr(APersistableBundle) bundle = APersistableBundle_new();
		nstatus = APersistableBundle_readFromParcel(parcel, &bundle);
		if (nstatus != STATUS_OK) {
			g_autoptr(AStatus) ystatus = AStatus_fromStatus(nstatus);
			g_warning("failed to read parcel bundle %s \n%s",
				  AStatus_getDescription(ystatus),
				  AStatus_getMessage(ystatus));
		}

		g_warning("options bundle size %d", APersistableBundle_size(bundle));
		APersistableBundle_getInt(bundle, "value", &bundle_value);
		g_warning("options bundle value = %d", bundle_value);
	}

	return local_reply;
}

static void
event_listener_death_handler(GBinderRemoteObject *event_listener_remote_object, void *user_data)
{
	FuBinderDaemon *self = user_data;

	g_warning("remove dead event listener");

	g_ptr_array_remove(self->event_listener_remote_objects, event_listener_remote_object);
}

static GBinderLocalReply *
fu_binder_daemon_method_add_event_listener(FuBinderDaemon *self,
					   GBinderRemoteRequest *remote_request,
					   FuEngineRequest *request,
					   int *status)
{
	GBinderLocalReply *local_reply = gbinder_local_object_new_reply(self->obj);

	GBinderRemoteObject *event_listener_remote_object =
	    gbinder_remote_request_read_object(remote_request);
	g_ptr_array_add(self->event_listener_remote_objects, event_listener_remote_object);
	gbinder_remote_object_add_death_handler(event_listener_remote_object,
						event_listener_death_handler,
						self);

	// TODO: Initial retransmit status events (dbus properties)?

	g_warning("add event listener");

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
	    fu_binder_daemon_method_install,		/* install */
	    fu_binder_daemon_method_add_event_listener, /* addEventListener */
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

static void
fu_binder_daemon_send_codec_event(FuBinderDaemon *self, FwupdCodec *codec, guint32 transaction_id)
{
	GBinderRemoteObject *event_listener_remote_object;
	GBinderClient *client;
	GBinderLocalRequest *event_listener_req;
	GBinderWriter event_params_writer;
	gsize size;
	g_autofree uint8_t *buffer = NULL;
	// TODO: Reuse parcel????
	g_autoptr(GError) error = NULL;

	GVariant *val;
	AParcel *parcel;
	binder_status_t nstatus = -1;

	if (self->event_listener_remote_objects->len == 0)
		return;

	val = fwupd_codec_to_variant(codec, FWUPD_CODEC_FLAG_NONE);

	parcel = AParcel_create();

	gp_parcel_write_variant(parcel, val, &error);

	size = AParcel_getDataSize(parcel);
	buffer = calloc(1, size);

	nstatus = AParcel_marshal(parcel, buffer, 0, size);
	if (nstatus != GBINDER_STATUS_OK) {
		g_warning("Failed to marshal parcel %d", nstatus);
	}

	for (guint i = 0; i < self->event_listener_remote_objects->len; i++) {
		event_listener_remote_object =
		    g_ptr_array_index(self->event_listener_remote_objects, i);
		client = gbinder_client_new(event_listener_remote_object, EVENT_LISTENER_IFACE);
		event_listener_req = gbinder_client_new_request(client);
		gbinder_local_request_init_writer(event_listener_req, &event_params_writer);
		gbinder_writer_append_bytes(&event_params_writer, buffer, size);

		gbinder_client_transact_sync_oneway(client, transaction_id, event_listener_req);

		gbinder_local_request_unref(event_listener_req);
		gbinder_client_unref(client);
	}
}

static void
fu_binder_daemon_engine_changed_cb(FuEngine *engine, FuBinderDaemon *self)
{
	GBinderRemoteObject *event_listener_remote_object;
	GBinderClient *client;

	if (self->event_listener_remote_objects->len == 0)
		return;

	for (guint i = 0; i < self->event_listener_remote_objects->len; i++) {
		event_listener_remote_object =
		    g_ptr_array_index(self->event_listener_remote_objects, i);
		client = gbinder_client_new(event_listener_remote_object, EVENT_LISTENER_IFACE);
		gbinder_client_transact_sync_oneway(client, EVENT_LISTENER_ON_CHANGED, NULL);

		gbinder_client_unref(client);
	}

	fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void
fu_binder_daemon_engine_device_added_cb(FuEngine *engine, FuDevice *device, FuBinderDaemon *self)
{
	g_debug("added cb");
	fu_binder_daemon_send_codec_event(self,
					  FWUPD_CODEC(device),
					  EVENT_LISTENER_ON_DEVICE_ADDED);
	fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void
fu_binder_daemon_engine_device_removed_cb(FuEngine *engine, FuDevice *device, FuBinderDaemon *self)
{
	g_debug("removed cb");
	fu_binder_daemon_send_codec_event(self,
					  FWUPD_CODEC(device),
					  EVENT_LISTENER_ON_DEVICE_REMOVED);
	fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void
fu_binder_daemon_engine_device_changed_cb(FuEngine *engine, FuDevice *device, FuBinderDaemon *self)
{
	g_debug("changed cb");
	fu_binder_daemon_send_codec_event(self,
					  FWUPD_CODEC(device),
					  EVENT_LISTENER_ON_DEVICE_CHANGED);
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
					  EVENT_LISTENER_ON_DEVICE_REQUEST);
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
	self->event_listener_remote_objects =
	    g_ptr_array_new_with_free_func((GDestroyNotify)gbinder_remote_object_unref);
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
