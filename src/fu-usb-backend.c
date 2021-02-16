/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuBackend"

#include "config.h"

#include <gusb.h>

#include "fu-usb-device.h"
#include "fu-usb-backend.h"

struct _FuUsbBackend {
	FuBackend		 parent_instance;
	GUsbContext		*usb_ctx;
	GHashTable		*devices;	/* platform_id : FuDevice */
};

G_DEFINE_TYPE (FuUsbBackend, fu_usb_backend, FU_TYPE_BACKEND)

static void
fu_usb_backend_device_added_cb (GUsbContext *ctx,
			       GUsbDevice *usb_device,
			       FuBackend *backend)
{
	FuUsbBackend *self = FU_USB_BACKEND (backend);
	g_autoptr(FuUsbDevice) device = fu_usb_device_new (usb_device);

	/* success */
	g_hash_table_insert (self->devices,
			     g_strdup (g_usb_device_get_platform_id (usb_device)),
			     g_object_ref (device));
	fu_backend_device_added (backend, FU_DEVICE (device));
}

static void
fu_usb_backend_device_removed_cb (GUsbContext *ctx,
				  GUsbDevice *usb_device,
				  FuBackend *backend)
{
	FuUsbBackend *self = FU_USB_BACKEND (backend);
	FuDevice *device_tmp;

	/* find the device we enumerated */
	device_tmp = g_hash_table_lookup (self->devices,
					  g_usb_device_get_platform_id (usb_device));
	if (device_tmp != NULL) {
		fu_backend_device_removed (backend, device_tmp);
		g_hash_table_remove (self->devices,
				     g_usb_device_get_platform_id (usb_device));
	}
}

static gboolean
fu_usb_backend_setup (FuBackend *backend, GError **error)
{
	FuUsbBackend *self = FU_USB_BACKEND (backend);

	self->usb_ctx = g_usb_context_new (error);
	if (self->usb_ctx == NULL) {
		g_prefix_error (error, "failed to get USB context: ");
		return FALSE;
	}
	g_signal_connect (self->usb_ctx, "device-added",
			  G_CALLBACK (fu_usb_backend_device_added_cb),
			  self);
	g_signal_connect (self->usb_ctx, "device-removed",
			  G_CALLBACK (fu_usb_backend_device_removed_cb),
			  self);

	return TRUE;
}

static gboolean
fu_usb_backend_coldplug (FuBackend *backend, GError **error)
{
	FuUsbBackend *self = FU_USB_BACKEND (backend);
	g_usb_context_enumerate (self->usb_ctx);
	return TRUE;
}

static void
fu_usb_backend_finalize (GObject *object)
{
	FuUsbBackend *self = FU_USB_BACKEND (object);
	if (self->usb_ctx != NULL)
		g_object_unref (self->usb_ctx);
	g_hash_table_unref (self->devices);
	G_OBJECT_CLASS (fu_usb_backend_parent_class)->finalize (object);
}

static void
fu_usb_backend_init (FuUsbBackend *self)
{
	self->devices = g_hash_table_new_full (g_str_hash, g_str_equal,
					       g_free, (GDestroyNotify) g_object_unref);
}

static void
fu_usb_backend_class_init (FuUsbBackendClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuBackendClass *klass_backend = FU_BACKEND_CLASS (klass);
	object_class->finalize = fu_usb_backend_finalize;
	klass_backend->setup = fu_usb_backend_setup;
	klass_backend->coldplug = fu_usb_backend_coldplug;
}

FuBackend *
fu_usb_backend_new (void)
{
	return FU_BACKEND (g_object_new (FU_TYPE_USB_BACKEND, "name", "usb", NULL));
}
