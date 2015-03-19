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

#include <fwupd.h>
#include <gio/gio.h>
#include <glib-object.h>

#include "fu-cleanup.h"
#include "fu-common.h"
#include "fu-device.h"
#include "fu-provider-fake.h"

static void     fu_provider_fake_finalize	(GObject	*object);

#define FU_PROVIDER_FAKE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), FU_TYPE_PROVIDER_FAKE, FuProviderFakePrivate))

/**
 * FuProviderFakePrivate:
 **/
struct _FuProviderFakePrivate
{
	GHashTable		*devices;
};

G_DEFINE_TYPE (FuProviderFake, fu_provider_fake, FU_TYPE_PROVIDER)

/**
 * fu_provider_fake_get_name:
 **/
static const gchar *
fu_provider_fake_get_name (FuProvider *provider)
{
	return "Fake";
}

/**
 * fu_provider_fake_update:
 **/
static gboolean
fu_provider_fake_update (FuProvider *provider,
			 FuDevice *device,
			 gint fd,
			 FuProviderFlags flags,
			 GError **error)
{
	if (flags & FU_PROVIDER_UPDATE_FLAG_OFFLINE) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "cannot handle offline");
	}
	fu_provider_set_status (provider, FU_STATUS_DECOMPRESSING);
	fu_provider_set_status (provider, FU_STATUS_DEVICE_WRITE);
	return TRUE;
}

/**
 * fu_provider_fake_coldplug:
 **/
static gboolean
fu_provider_fake_coldplug (FuProvider *provider, GError **error)
{
	_cleanup_object_unref_ FuDevice *device = NULL;
	device = fu_device_new ();
	fu_device_set_id (device, "FakeDevice");
	fu_device_set_guid (device, "00000000-0000-0000-0000-000000000000");
	fu_device_set_metadata (device, FU_DEVICE_KEY_KIND, "hotplug");
	fu_provider_device_add (provider, device);
	return TRUE;
}

/**
 * fu_provider_fake_class_init:
 **/
static void
fu_provider_fake_class_init (FuProviderFakeClass *klass)
{
	FuProviderClass *provider_class = FU_PROVIDER_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	provider_class->get_name = fu_provider_fake_get_name;
	provider_class->coldplug = fu_provider_fake_coldplug;
	provider_class->update_online = fu_provider_fake_update;
	object_class->finalize = fu_provider_fake_finalize;

	g_type_class_add_private (klass, sizeof (FuProviderFakePrivate));
}

/**
 * fu_provider_fake_init:
 **/
static void
fu_provider_fake_init (FuProviderFake *provider_fake)
{
	provider_fake->priv = FU_PROVIDER_FAKE_GET_PRIVATE (provider_fake);
}

/**
 * fu_provider_fake_finalize:
 **/
static void
fu_provider_fake_finalize (GObject *object)
{
//	FuProviderFake *provider_fake = FU_PROVIDER_FAKE (object);
//	FuProviderFakePrivate *priv = provider_fake->priv;

	G_OBJECT_CLASS (fu_provider_fake_parent_class)->finalize (object);
}

/**
 * fu_provider_fake_new:
 **/
FuProvider *
fu_provider_fake_new (void)
{
	FuProviderFake *provider;
	provider = g_object_new (FU_TYPE_PROVIDER_FAKE, NULL);
	return FU_PROVIDER (provider);
}
