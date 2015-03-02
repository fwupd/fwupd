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

static void     fu_provider_uefi_finalize	(GObject	*object);

G_DEFINE_TYPE (FuProviderUefi, fu_provider_uefi, FU_TYPE_PROVIDER)

/**
 * fu_provider_uefi_update:
 **/
static gboolean
fu_provider_uefi_update (FuProvider *provider,
			 FuDevice *device,
			 gint fd,
			 FuProviderFlags flags,
			 GError **error)
{
	/* this only makes sense offline */
	if ((flags & FU_PROVIDER_UPDATE_FLAG_OFFLINE) == 0) {
		g_set_error_literal (error,
				     FU_ERROR,
				     FU_ERROR_INTERNAL,
				     "Cannot do UEFI update online");
		return FALSE;
	}

	//FIXME
	g_debug ("DOING UEFI UPDATE USING FD %i", fd);
	return TRUE;
}

/**
 * fu_provider_uefi_coldplug:
 **/
static gboolean
fu_provider_uefi_coldplug (FuProvider *provider, GError **error)
{
	_cleanup_object_unref_ FuDevice *dev = NULL;

	//FIXME
	g_debug ("Adding fake UEFI device");
	dev = fu_device_new ();
	fu_device_set_id (dev, "UEFI-819b858e-c52c-402f-80e1-5b311b6c1959-dev1");
	fu_device_set_metadata (dev, FU_DEVICE_KEY_PROVIDER, "UEFI");
	fu_device_set_metadata (dev, FU_DEVICE_KEY_VERSION, "12345");
	fu_provider_emit_added (provider, dev);
	return TRUE;
}

/**
 * fu_provider_uefi_class_init:
 **/
static void
fu_provider_uefi_class_init (FuProviderUefiClass *klass)
{
	FuProviderClass *provider_class = FU_PROVIDER_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	provider_class->coldplug = fu_provider_uefi_coldplug;
	provider_class->update = fu_provider_uefi_update;
	object_class->finalize = fu_provider_uefi_finalize;
}

/**
 * fu_provider_uefi_init:
 **/
static void
fu_provider_uefi_init (FuProviderUefi *provider_uefi)
{
}

/**
 * fu_provider_uefi_finalize:
 **/
static void
fu_provider_uefi_finalize (GObject *object)
{
	G_OBJECT_CLASS (fu_provider_uefi_parent_class)->finalize (object);
}

/**
 * fu_provider_uefi_new:
 **/
FuProvider *
fu_provider_uefi_new (void)
{
	FuProviderUefi *provider;
	provider = g_object_new (FU_TYPE_PROVIDER_UEFI, NULL);
	return FU_PROVIDER (provider);
}
