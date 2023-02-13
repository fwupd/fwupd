/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuDeviceProgress"

#include "config.h"

#include <gio/gio.h>
#ifdef HAVE_GUSB
#include <gusb.h>
#endif

#include "fu-device-progress.h"

struct _FuDeviceProgress {
	GObject parent_instance;
	FuDevice *device;
	FuProgress *progress;
	guint percentage_changed_id;
	guint status_changed_id;
};

G_DEFINE_TYPE(FuDeviceProgress, fu_device_progress, G_TYPE_OBJECT)

static void
fu_device_progress_percentage_changed_cb(FuProgress *progress, guint percentage, gpointer user_data)
{
	FuDeviceProgress *self = FU_DEVICE_PROGRESS(user_data);
	fu_device_set_percentage(self->device, percentage);
}

static void
fu_device_progress_status_changed_cb(FuProgress *progress, FwupdStatus status, gpointer user_data)
{
	FuDeviceProgress *self = FU_DEVICE_PROGRESS(user_data);
	fu_device_set_status(self->device, status);
}

static void
fu_device_progress_finalize(GObject *obj)
{
	FuDeviceProgress *self = FU_DEVICE_PROGRESS(obj);
	g_signal_handler_disconnect(self->progress, self->percentage_changed_id);
	g_signal_handler_disconnect(self->progress, self->status_changed_id);
	fu_device_set_status(self->device, FWUPD_STATUS_IDLE);
	fu_device_set_percentage(self->device, 0);
	g_object_unref(self->device);
	g_object_unref(self->progress);
	G_OBJECT_CLASS(fu_device_progress_parent_class)->finalize(obj);
}

static void
fu_device_progress_class_init(FuDeviceProgressClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_device_progress_finalize;
}

static void
fu_device_progress_init(FuDeviceProgress *self)
{
}

/**
 * fu_device_progress_new:
 * @device: a #FuDevice
 * @progress: a #FuProgress
 *
 * Binds the device to the progress object so that the status and percentage will be coped from
 * the progress all the time this object is alive.
 *
 * When this object is finalized the *device* status will be set to `idle` and the percentage reset
 * back to 0%.
 *
 * Returns: a #FuDeviceProgress
 *
 * Since: 1.8.11
 **/
FuDeviceProgress *
fu_device_progress_new(FuDevice *device, FuProgress *progress)
{
	g_autoptr(FuDeviceProgress) self = g_object_new(FU_TYPE_DEVICE_PROGRESS, NULL);

	g_return_val_if_fail(FU_IS_DEVICE(device), NULL);
	g_return_val_if_fail(FU_IS_PROGRESS(progress), NULL);

	/* connect up to device */
	self->percentage_changed_id =
	    g_signal_connect(FU_PROGRESS(progress),
			     "percentage-changed",
			     G_CALLBACK(fu_device_progress_percentage_changed_cb),
			     self);
	self->status_changed_id = g_signal_connect(FU_PROGRESS(progress),
						   "status-changed",
						   G_CALLBACK(fu_device_progress_status_changed_cb),
						   self);
	self->device = g_object_ref(device);
	self->progress = g_object_ref(progress);

	/* success */
	return g_steal_pointer(&self);
}
