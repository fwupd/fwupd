/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuBackend"

#include "config.h"

#include <gusb.h>

#include "fu-usb-backend.h"
#include "fu-usb-device.h"

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
	g_autoptr(FuUsbDevice) device = NULL;

	/* success */
	device = fu_usb_device_new_with_context(fu_backend_get_context(backend), usb_device);
	fu_backend_device_added(backend, FU_DEVICE(device));
}

static void
fu_usb_backend_device_removed_cb(GUsbContext *ctx, GUsbDevice *usb_device, FuBackend *backend)
{
	FuUsbBackend *self = FU_USB_BACKEND(backend);
	FuDevice *device_tmp;

	/* find the device we enumerated */
	device_tmp =
	    fu_backend_lookup_by_id(FU_BACKEND(self), g_usb_device_get_platform_id(usb_device));
	if (device_tmp != NULL)
		fu_backend_device_removed(backend, device_tmp);
}

static void
fu_usb_backend_context_finalized_cb(gpointer data, GObject *where_the_object_was)
{
	g_critical("GUsbContext %p was finalized from under our feet!", where_the_object_was);
}

static gboolean
fu_usb_backend_setup(FuBackend *backend, GError **error)
{
	FuUsbBackend *self = FU_USB_BACKEND(backend);

	self->usb_ctx = g_usb_context_new(error);
	if (self->usb_ctx == NULL) {
		g_prefix_error(error, "failed to get USB context: ");
		return FALSE;
	}
	g_object_weak_ref(G_OBJECT(self->usb_ctx), fu_usb_backend_context_finalized_cb, self);
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
fu_usb_backend_coldplug(FuBackend *backend, GError **error)
{
	FuUsbBackend *self = FU_USB_BACKEND(backend);
	g_usb_context_enumerate(self->usb_ctx);
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

static void
fu_usb_backend_finalize(GObject *object)
{
	FuUsbBackend *self = FU_USB_BACKEND(object);

	if (self->usb_ctx != NULL) {
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
	klass_backend->registered = fu_usb_backend_registered;
}

FuBackend *
fu_usb_backend_new(void)
{
	return FU_BACKEND(g_object_new(FU_TYPE_USB_BACKEND, "name", "usb", NULL));
}
