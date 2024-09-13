/*
 * Copyright 2011 Richard Hughes <richard@hughsie.com>
 * Copyright 2011 Hans de Goede <hdegoede@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuBackend"

#include "config.h"

#include "fu-context-private.h"
#include "fu-usb-backend.h"
#include "fu-usb-device-private.h"

struct _FuUsbBackend {
	FuBackend parent_instance;
	libusb_context *ctx;
#ifndef HAVE_GUDEV
	libusb_hotplug_callback_handle hotplug_id;
	GThread *thread_event;
	volatile gint thread_event_run;
	GPtrArray *idle_events;
	GMutex idle_events_mutex;
	guint idle_events_id;
	guint hotplug_poll_id;
	guint hotplug_poll_interval;
#endif
};

G_DEFINE_TYPE(FuUsbBackend, fu_usb_backend, FU_TYPE_BACKEND)

#define FU_USB_BACKEND_POLL_INTERVAL_DEFAULT	 1000 /* ms */
#define FU_USB_BACKEND_POLL_INTERVAL_WAIT_REPLUG 5    /* ms */

#ifndef HAVE_GUDEV
static gchar *
fu_usb_backend_get_usb_device_backend_id(libusb_device *usb_device)
{
	return g_strdup_printf("%02x:%02x",
			       libusb_get_bus_number(usb_device),
			       libusb_get_device_address(usb_device));
}

static FuUsbDevice *
fu_usb_backend_create_device(FuUsbBackend *self, libusb_device *usb_device)
{
	g_autofree gchar *backend_id = fu_usb_backend_get_usb_device_backend_id(usb_device);
	return g_object_new(FU_TYPE_USB_DEVICE,
			    "backend",
			    FU_BACKEND(self),
			    "backend-id",
			    backend_id,
			    "libusb-device",
			    usb_device,
			    NULL);
}

static void
fu_usb_backend_add_device(FuUsbBackend *self, libusb_device *usb_device)
{
	FuDevice *device_old;
	g_autofree gchar *backend_id = fu_usb_backend_get_usb_device_backend_id(usb_device);
	g_autoptr(FuUsbDevice) device = NULL;

	device_old = fu_backend_lookup_by_id(FU_BACKEND(self), backend_id);
	if (device_old != NULL)
		return;
	device = fu_usb_backend_create_device(self, usb_device);
	fu_backend_device_added(FU_BACKEND(self), FU_DEVICE(device));
}

static void
fu_usb_backend_remove_device(FuUsbBackend *self, libusb_device *usb_device)
{
	g_autofree gchar *backend_id = fu_usb_backend_get_usb_device_backend_id(usb_device);
	FuDevice *device = fu_backend_lookup_by_id(FU_BACKEND(self), backend_id);
	if (device != NULL)
		fu_backend_device_removed(FU_BACKEND(self), device);
}

static void
fu_usb_backend_rescan(FuUsbBackend *self)
{
	libusb_device **dev_list = NULL;
	g_autoptr(GList) existing_devices = NULL;
	g_autoptr(GPtrArray) devices = fu_backend_get_devices(FU_BACKEND(self));

	/* skip actual enumeration */
	if (g_getenv("FWUPD_SELF_TEST") != NULL)
		return;

	/* copy to a context so we can remove from the array */
	for (guint i = 0; i < devices->len; i++) {
		FuUsbDevice *device = g_ptr_array_index(devices, i);
		existing_devices = g_list_prepend(existing_devices, device);
	}

	/* look for any removed devices */
	libusb_get_device_list(self->ctx, &dev_list);
	for (GList *l = existing_devices; l != NULL; l = l->next) {
		FuUsbDevice *device = FU_USB_DEVICE(l->data);
		gboolean found = FALSE;
		for (guint i = 0; dev_list != NULL && dev_list[i] != NULL; i++) {
			if (libusb_get_bus_number(dev_list[i]) == fu_usb_device_get_bus(device) &&
			    libusb_get_device_address(dev_list[i]) ==
				fu_usb_device_get_address(device)) {
				found = TRUE;
				break;
			}
		}
		if (!found)
			fu_backend_device_removed(FU_BACKEND(self), FU_DEVICE(device));
	}

	/* add any devices not yet added (duplicates will be filtered */
	for (guint i = 0; dev_list != NULL && dev_list[i] != NULL; i++)
		fu_usb_backend_add_device(self, dev_list[i]);

	libusb_free_device_list(dev_list, 1);
}

static gboolean
fu_usb_backend_rescan_cb(gpointer user_data)
{
	FuUsbBackend *self = FU_USB_BACKEND(user_data);
	fu_usb_backend_rescan(self);
	return TRUE;
}

static void
fu_usb_backend_ensure_rescan_timeout(FuUsbBackend *self)
{
	if (self->hotplug_poll_id > 0) {
		g_source_remove(self->hotplug_poll_id);
		self->hotplug_poll_id = 0;
	}
	if (self->hotplug_poll_interval > 0) {
		self->hotplug_poll_id =
		    g_timeout_add(self->hotplug_poll_interval, fu_usb_backend_rescan_cb, self);
	}
}

static void
fu_usb_backend_set_hotplug_poll_interval(FuUsbBackend *self, guint hotplug_poll_interval)
{
	/* same */
	if (self->hotplug_poll_interval == hotplug_poll_interval)
		return;

	self->hotplug_poll_interval = hotplug_poll_interval;

	/* if already running then change the existing timeout */
	if (self->hotplug_poll_id > 0)
		fu_usb_backend_ensure_rescan_timeout(self);
}

#ifdef _WIN32
static void
fu_usb_backend_device_notify_flags_cb(FuDevice *device, GParamSpec *pspec, FuBackend *backend)
{
	FuUsbBackend *self = FU_USB_BACKEND(backend);

	/* if waiting for a disconnect, set win32 to poll insanely fast -- and set it
	 * back to the default when the device removal was detected */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG)) {
		g_debug("setting USB poll interval to %ums to detect replug",
			(guint)FU_USB_BACKEND_POLL_INTERVAL_WAIT_REPLUG);
		fu_usb_backend_set_hotplug_poll_interval(self,
							 FU_USB_BACKEND_POLL_INTERVAL_WAIT_REPLUG);
	} else {
		fu_usb_backend_set_hotplug_poll_interval(self,
							 FU_USB_BACKEND_POLL_INTERVAL_DEFAULT);
	}
}
#endif

typedef struct {
	FuUsbBackend *self;
	libusb_device *dev;
	libusb_hotplug_event event;
} FuUsbBackendIdleHelper;

static gpointer
fu_usb_backend_idle_helper_copy(gconstpointer src, gpointer user_data)
{
	FuUsbBackendIdleHelper *helper_src = (FuUsbBackendIdleHelper *)src;
	FuUsbBackendIdleHelper *helper_dst = g_new0(FuUsbBackendIdleHelper, 1);
	helper_dst->self = g_object_ref(helper_src->self);
	helper_dst->dev = libusb_ref_device(helper_src->dev);
	helper_dst->event = helper_src->event;
	return helper_dst;
}

/* always in the main thread */
static gboolean
fu_usb_backend_idle_hotplug_cb(gpointer user_data)
{
	FuUsbBackend *self = FU_USB_BACKEND(user_data);
	g_autoptr(GPtrArray) idle_events = NULL;

	/* drain the idle events with the lock held */
	g_mutex_lock(&self->idle_events_mutex);
	idle_events = g_ptr_array_copy(self->idle_events, fu_usb_backend_idle_helper_copy, NULL);
	g_ptr_array_set_size(self->idle_events, 0);
	self->idle_events_id = 0;
	g_mutex_unlock(&self->idle_events_mutex);

	/* run the callbacks when not locked */
	for (guint i = 0; i < idle_events->len; i++) {
		FuUsbBackendIdleHelper *helper = g_ptr_array_index(idle_events, i);
		switch (helper->event) {
		case LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED:
			fu_usb_backend_add_device(helper->self, helper->dev);
			break;
		case LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT:
			fu_usb_backend_remove_device(helper->self, helper->dev);
			break;
		default:
			break;
		}
	}

	/* all done */
	return FALSE;
}

/* this is run in the libusb thread */
static int LIBUSB_CALL
fu_usb_backend_hotplug_cb(struct libusb_context *ctx,
			  struct libusb_device *dev,
			  libusb_hotplug_event event,
			  void *user_data)
{
	FuUsbBackend *self = FU_USB_BACKEND(user_data);
	FuUsbBackendIdleHelper *helper;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->idle_events_mutex);

	g_assert(locker != NULL); /* nocheck:blocked */

	helper = g_new0(FuUsbBackendIdleHelper, 1);
	helper->self = g_object_ref(self);
	helper->dev = libusb_ref_device(dev);
	helper->event = event;

	g_ptr_array_add(self->idle_events, helper);
	if (self->idle_events_id == 0)
		self->idle_events_id = g_idle_add(fu_usb_backend_idle_hotplug_cb, self);

	return 0;
}

static gpointer
fu_usb_backend_event_thread_cb(gpointer data)
{
	FuUsbBackend *self = FU_USB_BACKEND(data);
	struct timeval tv = {
	    .tv_usec = 0,
	    .tv_sec = 2,
	};
	while (g_atomic_int_get(&self->thread_event_run) > 0)
		libusb_handle_events_timeout_completed(self->ctx, &tv, NULL);
	return NULL;
}
#endif

static gboolean
fu_usb_backend_setup(FuBackend *backend,
		     FuBackendSetupFlags flags,
		     FuProgress *progress,
		     GError **error)
{
	FuUsbBackend *self = FU_USB_BACKEND(backend);
	FuContext *ctx = fu_backend_get_context(FU_BACKEND(self));
	gint log_level = g_getenv("FWUPD_VERBOSE") != NULL ? 3 : 0;
	gint rc;

#ifdef HAVE_LIBUSB_INIT_CONTEXT
	const struct libusb_init_option options[] = {{.option = LIBUSB_OPTION_NO_DEVICE_DISCOVERY,
						      .value = {
							  .ival = 1,
						      }}};
	rc = libusb_init_context(&self->ctx, options, G_N_ELEMENTS(options));
#else
	rc = libusb_init(&self->ctx);
#endif
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to init libusb: %s [%i]",
			    libusb_strerror(rc),
			    rc);
		return FALSE;
	}
#ifdef HAVE_LIBUSB_SET_OPTION
	libusb_set_option(self->ctx, LIBUSB_OPTION_LOG_LEVEL, log_level);
#else
	libusb_set_debug(self->ctx, log_level);
#endif
	fu_context_set_data(ctx, "libusb_context", self->ctx);
	fu_context_add_udev_subsystem(ctx, "usb", NULL);

	/* no hotplug required, probably in tests */
	if ((flags & FU_BACKEND_SETUP_FLAG_USE_HOTPLUG) == 0)
		return TRUE;

#ifndef HAVE_GUDEV
	self->thread_event_run = 1;
	self->thread_event = g_thread_new("FuUsbBackendEvt", fu_usb_backend_event_thread_cb, self);

	/* watch for add/remove */
	if (libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
		rc = libusb_hotplug_register_callback(self->ctx,
						      LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |
							  LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
						      0,
						      LIBUSB_HOTPLUG_MATCH_ANY,
						      LIBUSB_HOTPLUG_MATCH_ANY,
						      LIBUSB_HOTPLUG_MATCH_ANY,
						      fu_usb_backend_hotplug_cb,
						      self,
						      &self->hotplug_id);
		if (rc != LIBUSB_SUCCESS) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "error creating a hotplug callback: %s [%i]",
				    libusb_strerror(rc),
				    rc);
			return FALSE;
		}
	} else {
		fu_usb_backend_set_hotplug_poll_interval(self,
							 FU_USB_BACKEND_POLL_INTERVAL_DEFAULT);
	}
#endif

	/* success */
	return TRUE;
}

#ifndef HAVE_GUDEV
static gboolean
fu_usb_backend_coldplug(FuBackend *backend, FuProgress *progress, GError **error)
{
	FuUsbBackend *self = FU_USB_BACKEND(backend);
	fu_usb_backend_rescan(self);
	return TRUE;
}

static void
fu_usb_backend_registered(FuBackend *backend, FuDevice *device)
{
#ifdef _WIN32
	/* not required */
	if (!FU_IS_USB_DEVICE(device))
		return;

	/* on win32 we need to poll the context faster */
	g_signal_connect(FU_DEVICE(device),
			 "notify::flags",
			 G_CALLBACK(fu_usb_backend_device_notify_flags_cb),
			 backend);
#endif
}

static FuDevice *
fu_usb_backend_get_device_parent(FuBackend *backend,
				 FuDevice *device,
				 const gchar *subsystem,
				 GError **error)
{
	FuUsbBackend *self = FU_USB_BACKEND(backend);
	libusb_device *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));
	libusb_device *usb_parent;

	/* libusb or kernel */
	if (usb_device == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no device");
		return NULL;
	}
	usb_parent = libusb_get_parent(usb_device);
	if (usb_parent == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no parent");
		return NULL;
	}
	return FU_DEVICE(fu_usb_backend_create_device(self, usb_parent));
}
#endif

static void
fu_usb_backend_finalize(GObject *object)
{
#ifndef HAVE_GUDEV
	FuUsbBackend *self = FU_USB_BACKEND(object);

	/* this is safe to call even when self->hotplug_id is unset */
	if (g_atomic_int_dec_and_test(&self->thread_event_run)) {
		libusb_hotplug_deregister_callback(self->ctx, self->hotplug_id);
		g_thread_join(self->thread_event);
	}
	if (self->idle_events_id > 0)
		g_source_remove(self->idle_events_id);
	if (self->hotplug_poll_id > 0)
		g_source_remove(self->hotplug_poll_id);
	if (self->ctx != NULL)
		libusb_exit(self->ctx);
	g_clear_pointer(&self->idle_events, g_ptr_array_unref);
	g_mutex_clear(&self->idle_events_mutex);
#endif

	G_OBJECT_CLASS(fu_usb_backend_parent_class)->finalize(object);
}

#ifndef HAVE_GUDEV
static void
fu_usb_backend_idle_helper_free(FuUsbBackendIdleHelper *helper)
{
	g_object_unref(helper->self);
	libusb_unref_device(helper->dev);
	g_free(helper);
}
#endif

static void
fu_usb_backend_init(FuUsbBackend *self)
{
#ifndef HAVE_GUDEV
	/* to escape the thread into the mainloop */
	g_mutex_init(&self->idle_events_mutex);
	self->idle_events =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_usb_backend_idle_helper_free);
#endif
}

static void
fu_usb_backend_class_init(FuUsbBackendClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuBackendClass *backend_class = FU_BACKEND_CLASS(klass);
	object_class->finalize = fu_usb_backend_finalize;
	backend_class->setup = fu_usb_backend_setup;
#ifndef HAVE_GUDEV
	backend_class->coldplug = fu_usb_backend_coldplug;
	backend_class->registered = fu_usb_backend_registered;
	backend_class->get_device_parent = fu_usb_backend_get_device_parent;
#endif
}

FuBackend *
fu_usb_backend_new(FuContext *ctx)
{
	return FU_BACKEND(g_object_new(FU_TYPE_USB_BACKEND, "name", "usb", "context", ctx, NULL));
}
