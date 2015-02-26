/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib-object.h>
#include <gio/gio.h>

#include "fu-cleanup.h"
#include "fu-common.h"
#include "fu-device.h"
#include "fu-provider-uefi.h"

static void     fu_provider_finalize	(GObject	*object);

#define FU_PROVIDER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), FU_TYPE_PROVIDER, FuProviderPrivate))

enum {
	SIGNAL_DEVICE_ADDED,
	SIGNAL_DEVICE_REMOVED,
	SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (FuProvider, fu_provider, G_TYPE_OBJECT)

/**
 * fu_provider_coldplug:
 **/
gboolean
fu_provider_coldplug (FuProvider *provider, GError **error)
{
	FuProviderClass *klass = FU_PROVIDER_GET_CLASS (provider);
	if (klass->coldplug != NULL)
		return klass->coldplug (provider, error);
	return TRUE;
}

/**
 * fu_provider_update_offline:
 **/
gboolean
fu_provider_update_offline (FuProvider *provider,
			    FuDevice *device,
			    gint fd,
			    GError **error)
{
	FuProviderClass *klass = FU_PROVIDER_GET_CLASS (provider);
	if (klass->update_offline == NULL) {
		g_set_error_literal (error,
				     FU_ERROR,
				     FU_ERROR_INTERNAL,
				     "No offline update functionality");
		return FALSE;
	}
	return klass->update_offline (provider, device, fd, error);
}

/**
 * fu_provider_emit_added:
 **/
void
fu_provider_emit_added (FuProvider *provider, FuDevice *device)
{
	g_debug ("emit added: %s", fu_device_get_id (device));
	g_signal_emit (provider, signals[SIGNAL_DEVICE_ADDED], 0, device);
}

/**
 * fu_provider_emit_removed:
 **/
void
fu_provider_emit_removed (FuProvider *provider, FuDevice *device)
{
	g_debug ("emit removed: %s", fu_device_get_id (device));
	g_signal_emit (provider, signals[SIGNAL_DEVICE_REMOVED], 0, device);
}

/**
 * fu_provider_class_init:
 **/
static void
fu_provider_class_init (FuProviderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_provider_finalize;
	signals[SIGNAL_DEVICE_ADDED] =
		g_signal_new ("device-added",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FuProviderClass, device_added),
			      NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, FU_TYPE_DEVICE);
	signals[SIGNAL_DEVICE_REMOVED] =
		g_signal_new ("device-removed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FuProviderClass, device_removed),
			      NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, FU_TYPE_DEVICE);
}

/**
 * fu_provider_init:
 **/
static void
fu_provider_init (FuProvider *provider)
{
}

/**
 * fu_provider_finalize:
 **/
static void
fu_provider_finalize (GObject *object)
{
	G_OBJECT_CLASS (fu_provider_parent_class)->finalize (object);
}
