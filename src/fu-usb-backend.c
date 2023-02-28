/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuBackend"

#include "config.h"

#include <gusb.h>

#include "fu-usb-backend.h"
#include "fu-usb-device-private.h"

struct _FuUsbBackend {
	FuBackend parent_instance;
	GUsbContext *usb_ctx;
};

G_DEFINE_TYPE(FuUsbBackend, fu_usb_backend, FU_TYPE_BACKEND)

#define FU_USB_BACKEND_POLL_INTERVAL_DEFAULT	 1000 /* ms */
#define FU_USB_BACKEND_POLL_INTERVAL_WAIT_REPLUG 5    /* ms */

#ifdef _WIN32
static void
fu_usb_backend_device_notify_flags_cb(FuDevice *device, GParamSpec *pspec, FuBackend *backend)
{
#if G_USB_CHECK_VERSION(0, 3, 10)
	FuUsbBackend *self = FU_USB_BACKEND(backend);

	/* if waiting for a disconnect, set win32 to poll insanely fast -- and set it
	 * back to the default when the device removal was detected */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG)) {
		g_debug("setting USB poll interval to %ums to detect replug",
			(guint)FU_USB_BACKEND_POLL_INTERVAL_WAIT_REPLUG);
		g_usb_context_set_hotplug_poll_interval(self->usb_ctx,
							FU_USB_BACKEND_POLL_INTERVAL_WAIT_REPLUG);
	} else {
		g_usb_context_set_hotplug_poll_interval(self->usb_ctx,
							FU_USB_BACKEND_POLL_INTERVAL_DEFAULT);
	}
#else
	g_warning("GUsb >= 0.3.10 may be needed to notice device enumeration");
#endif
}
#endif

static void
fu_usb_backend_device_added_cb(GUsbContext *ctx, GUsbDevice *usb_device, FuBackend *backend)
{
	g_autoptr(FuUsbDevice) device =
	    fu_usb_device_new(fu_backend_get_context(backend), usb_device);
	fu_backend_device_added(backend, FU_DEVICE(device));
}

static void
fu_usb_backend_device_removed_cb(GUsbContext *ctx, GUsbDevice *usb_device, FuBackend *backend)
{
	FuDevice *device =
	    fu_backend_lookup_by_id(backend, g_usb_device_get_platform_id(usb_device));
	if (device != NULL)
		fu_backend_device_removed(backend, device);
}

static void
fu_usb_backend_context_finalized_cb(gpointer data, GObject *where_the_object_was)
{
	g_critical("GUsbContext %p was finalized from under our feet!", where_the_object_was);
}

static void
fu_usb_backend_context_flags_check(FuUsbBackend *self)
{
#if G_USB_CHECK_VERSION(0, 4, 5)
	FuContext *ctx = fu_backend_get_context(FU_BACKEND(self));
	GUsbContextFlags usb_flags = G_USB_CONTEXT_FLAGS_DEBUG;
	if (fu_context_has_flag(ctx, FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		g_debug("saving FuUsbBackend events");
		usb_flags |= G_USB_CONTEXT_FLAGS_SAVE_EVENTS;
	}
	g_usb_context_set_flags(self->usb_ctx, usb_flags);
#endif
}

static void
fu_usb_backend_context_flags_notify_cb(FuContext *ctx, GParamSpec *pspec, FuUsbBackend *self)
{
	fu_usb_backend_context_flags_check(self);
}

static gboolean
fu_usb_backend_setup(FuBackend *backend, FuProgress *progress, GError **error)
{
	FuUsbBackend *self = FU_USB_BACKEND(backend);

	self->usb_ctx = g_usb_context_new(error);
	if (self->usb_ctx == NULL) {
		g_prefix_error(error, "failed to get USB context: ");
		return FALSE;
	}
	g_object_weak_ref(G_OBJECT(self->usb_ctx), fu_usb_backend_context_finalized_cb, self);
	g_signal_connect(fu_backend_get_context(backend),
			 "notify::flags",
			 G_CALLBACK(fu_usb_backend_context_flags_notify_cb),
			 self);
	fu_usb_backend_context_flags_check(self);
	return TRUE;
}

static void
fu_usb_backend_coldplug_device(FuUsbBackend *self, GUsbDevice *usb_device, FuProgress *progress)
{
	g_autoptr(FuUsbDevice) device = NULL;
	g_autofree gchar *name = NULL;

	name = g_strdup_printf("%04X:%04X",
			       g_usb_device_get_vid(usb_device),
			       g_usb_device_get_pid(usb_device));
	fu_progress_set_name(progress, name);
	device = fu_usb_device_new(fu_backend_get_context(FU_BACKEND(self)), usb_device);
	fu_backend_device_added(FU_BACKEND(self), FU_DEVICE(device));
}

static void
fu_usb_backend_coldplug_devices(FuUsbBackend *self, GPtrArray *usb_devices, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, usb_devices->len);
	for (guint i = 0; i < usb_devices->len; i++) {
		GUsbDevice *usb_device = g_ptr_array_index(usb_devices, i);
		fu_usb_backend_coldplug_device(self, usb_device, fu_progress_get_child(progress));
		fu_progress_step_done(progress);
	}
}

static gboolean
fu_usb_backend_coldplug(FuBackend *backend, FuProgress *progress, GError **error)
{
	FuUsbBackend *self = FU_USB_BACKEND(backend);

	g_autoptr(GPtrArray) usb_devices = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "enumerate");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 99, "add-devices");

	/* no insight */
	g_usb_context_enumerate(self->usb_ctx);
	fu_progress_step_done(progress);

	/* add each device */
	usb_devices = g_usb_context_get_devices(self->usb_ctx);
	fu_usb_backend_coldplug_devices(self, usb_devices, fu_progress_get_child(progress));
	fu_progress_step_done(progress);

	/* watch for future changes */
	g_signal_connect(G_USB_CONTEXT(self->usb_ctx),
			 "device-added",
			 G_CALLBACK(fu_usb_backend_device_added_cb),
			 self);
	g_signal_connect(G_USB_CONTEXT(self->usb_ctx),
			 "device-removed",
			 G_CALLBACK(fu_usb_backend_device_removed_cb),
			 self);
	return TRUE;
}

static gboolean
fu_usb_backend_load(FuBackend *backend,
		    JsonObject *json_object,
		    const gchar *tag,
		    FuBackendLoadFlags flags,
		    GError **error)
{
#if G_USB_CHECK_VERSION(0, 4, 5)
	FuUsbBackend *self = FU_USB_BACKEND(backend);
	return g_usb_context_load_with_tag(self->usb_ctx, json_object, tag, error);
#else
	g_set_error_literal(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "GUsb version too old to load backends");
	return FALSE;
#endif
}

static gboolean
fu_usb_backend_save(FuBackend *backend,
		    JsonBuilder *json_builder,
		    const gchar *tag,
		    FuBackendSaveFlags flags,
		    GError **error)
{
#if G_USB_CHECK_VERSION(0, 4, 5)
	FuUsbBackend *self = FU_USB_BACKEND(backend);
	guint usb_events_cnt = 0;
	g_autoptr(GPtrArray) devices = g_usb_context_get_devices(self->usb_ctx);

	for (guint i = 0; i < devices->len; i++) {
		GUsbDevice *usb_device = g_ptr_array_index(devices, i);
		g_autoptr(GPtrArray) usb_events = g_usb_device_get_events(usb_device);
		if (usb_events->len > 0 || g_usb_device_has_tag(usb_device, tag)) {
			g_info("%u USB events to save for %s",
			       usb_events->len,
			       g_usb_device_get_platform_id(usb_device));
		}
		usb_events_cnt += usb_events->len;
	}
	if (usb_events_cnt == 0)
		return TRUE;
	if (!g_usb_context_save_with_tag(self->usb_ctx, json_builder, tag, error))
		return FALSE;
	for (guint i = 0; i < devices->len; i++) {
		GUsbDevice *usb_device = g_ptr_array_index(devices, i);
		g_usb_device_clear_events(usb_device);
	}
	return TRUE;
#else
	g_set_error_literal(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "GUsb version too old to save backends");
	return FALSE;
#endif
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

static void
fu_usb_backend_finalize(GObject *object)
{
	FuUsbBackend *self = FU_USB_BACKEND(object);

	if (self->usb_ctx != NULL) {
		g_signal_handlers_disconnect_by_data(G_USB_CONTEXT(self->usb_ctx), self);
		g_object_weak_unref(G_OBJECT(self->usb_ctx),
				    fu_usb_backend_context_finalized_cb,
				    self);
		g_object_unref(self->usb_ctx);
	}
	G_OBJECT_CLASS(fu_usb_backend_parent_class)->finalize(object);
}

static void
fu_usb_backend_init(FuUsbBackend *self)
{
}

static void
fu_usb_backend_class_init(FuUsbBackendClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuBackendClass *klass_backend = FU_BACKEND_CLASS(klass);
	object_class->finalize = fu_usb_backend_finalize;
	klass_backend->setup = fu_usb_backend_setup;
	klass_backend->coldplug = fu_usb_backend_coldplug;
	klass_backend->load = fu_usb_backend_load;
	klass_backend->save = fu_usb_backend_save;
	klass_backend->registered = fu_usb_backend_registered;
}

FuBackend *
fu_usb_backend_new(FuContext *ctx)
{
	return FU_BACKEND(g_object_new(FU_TYPE_USB_BACKEND, "name", "usb", "context", ctx, NULL));
}
