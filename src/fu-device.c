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

static void fu_device_finalize			 (GObject *object);

#define FU_DEVICE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), FU_TYPE_DEVICE, FuDevicePrivate))

/**
 * FuDevicePrivate:
 *
 * Private #FuDevice data
 **/
struct _FuDevicePrivate
{
	gchar				*id;
};

enum {
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_ID,
	PROP_LAST
};

G_DEFINE_TYPE (FuDevice, fu_device, G_TYPE_OBJECT)

/**
 * fu_device_get_id:
 **/
const gchar *
fu_device_get_id (FuDevice *device)
{
	g_return_val_if_fail (FU_IS_DEVICE (device), NULL);
	return device->priv->id;
}

/**
 * fu_device_set_id:
 **/
void
fu_device_set_id (FuDevice *device, const gchar *id)
{
	g_return_if_fail (FU_IS_DEVICE (device));

	g_free (device->priv->id);
	device->priv->id = g_strdup (id);
}

/**
 * fu_device_get_property:
 **/
static void
fu_device_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuDevice *device = FU_DEVICE (object);
	FuDevicePrivate *priv = device->priv;

	switch (prop_id) {
	case PROP_ID:
		g_value_set_string (value, priv->id);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * fu_device_set_property:
 **/
static void
fu_device_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuDevice *device = FU_DEVICE (object);
	FuDevicePrivate *priv = device->priv;

	switch (prop_id) {
	case PROP_ID:
		g_free (priv->id);
		priv->id = g_strdup (g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * fu_device_class_init:
 **/
static void
fu_device_class_init (FuDeviceClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_device_finalize;
	object_class->get_property = fu_device_get_property;
	object_class->set_property = fu_device_set_property;

	/**
	 * FuDevice:id:
	 */
	pspec = g_param_spec_string ("id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ID, pspec);

	g_type_class_add_private (klass, sizeof (FuDevicePrivate));
}

/**
 * fu_device_init:
 **/
static void
fu_device_init (FuDevice *device)
{
	device->priv = FU_DEVICE_GET_PRIVATE (device);
}

/**
 * fu_device_finalize:
 **/
static void
fu_device_finalize (GObject *object)
{
	FuDevice *device = FU_DEVICE (object);
	FuDevicePrivate *priv = device->priv;

	g_free (priv->id);

	G_OBJECT_CLASS (fu_device_parent_class)->finalize (object);
}

/**
 * fu_device_new:
 **/
FuDevice *
fu_device_new (void)
{
	FuDevice *device;
	device = g_object_new (FU_TYPE_DEVICE, NULL);
	return FU_DEVICE (device);
}
