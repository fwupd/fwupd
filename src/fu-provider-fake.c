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

#include "fu-device.h"
#include "fu-provider-fake.h"

static void	fu_provider_fake_finalize	(GObject	*object);

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
			 GBytes *blob_fw,
			 FwupdInstallFlags flags,
			 GError **error)
{
	if (flags & FWUPD_INSTALL_FLAG_OFFLINE) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "cannot handle offline");
	}
	fu_provider_set_status (provider, FWUPD_STATUS_DECOMPRESSING);
	fu_provider_set_status (provider, FWUPD_STATUS_DEVICE_WRITE);
	return TRUE;
}

/**
 * fu_provider_fake_coldplug:
 **/
static gboolean
fu_provider_fake_coldplug (FuProvider *provider, GError **error)
{
	g_autoptr(FuDevice) device = NULL;
	device = fu_device_new ();
	fu_device_set_id (device, "FakeDevice");
	fu_device_add_guid (device, "00000000-0000-0000-0000-000000000000");
	fu_device_set_name (device, "Integrated_Webcam(TM)");
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
}

/**
 * fu_provider_fake_init:
 **/
static void
fu_provider_fake_init (FuProviderFake *provider_fake)
{
}

/**
 * fu_provider_fake_finalize:
 **/
static void
fu_provider_fake_finalize (GObject *object)
{
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
