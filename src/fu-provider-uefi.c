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
#include <fwup.h>

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
	const gchar *guid_str;
	efi_guid_t guid;

	/* get the hardware we're referencing */
	guid_str = fu_device_get_metadata (device, FU_DEVICE_KEY_GUID);
	if (efi_str_to_guid (guid_str, &guid) < 0) {
		g_set_error_literal (error,
				     FU_ERROR,
				     FU_ERROR_NOT_POSSIBLE,
				     "Failed to prepare UEFI update");
		return FALSE;
	}

	/* perform the update */
	g_debug ("Performing UEFI capsule update");
	fu_provider_set_status (provider, FU_STATUS_SCHEDULING);
	if (fwup_set_up_update (&guid, fd) < 0) {
		g_set_error (error,
			     FU_ERROR,
			     FU_ERROR_NOT_POSSIBLE,
			     "UEFI firmware update failed: %s",
			     fwup_strerror (fwup_error));
		return FALSE;
	}

	return TRUE;
}

/**
 * fu_provider_uefi_coldplug:
 **/
static gboolean
fu_provider_uefi_coldplug (FuProvider *provider, GError **error)
{
	_cleanup_object_unref_ FuDevice *dev = NULL;
	fwup_resource_iter *iter = NULL;

	/* not supported */
	if (!fwup_supported ()) {
		g_set_error_literal (error,
				     FU_ERROR,
				     FU_ERROR_NOT_POSSIBLE,
				     "UEFI firmware updating not supported");
		return FALSE;
	}

	if (fwup_resource_iter_create (&iter) < 0) {
		g_set_error_literal (error,
				     FU_ERROR,
				     FU_ERROR_INTERNAL,
				     "Cannot create fwup iter");
		return FALSE;
	}

	while (1) {
		fwup_resource re;
		int rc;
		_cleanup_free_ gchar *guid = NULL;
		_cleanup_free_ gchar *id = NULL;
		_cleanup_free_ gchar *version = NULL;
		_cleanup_free_ gchar *version_lowest = NULL;

		rc = fwup_resource_iter_next (iter, &re);
		if (rc == 0)
			break;
		if (rc < 0) {
			g_warning ("failed to get next fwup iter");
			break;
		}

		/* convert to strings */
		guid = g_strdup ("00000000-0000-0000-0000-000000000000");
		if (efi_guid_to_str (&re.guid, &guid) < 0) {
			g_warning ("failed to convert guid to string");
			continue;
		}
		version = g_strdup_printf ("%" G_GUINT32_FORMAT, re.fw_version);
		id = g_strdup_printf ("UEFI-%s-dev%" G_GUINT64_FORMAT,
				      guid, re.hardware_instance);

		dev = fu_device_new ();
		fu_device_set_id (dev, id);
		fu_device_set_metadata (dev, FU_DEVICE_KEY_PROVIDER, "UEFI");
		fu_device_set_metadata (dev, FU_DEVICE_KEY_GUID, guid);
		fu_device_set_metadata (dev, FU_DEVICE_KEY_VERSION, version);
		fu_device_set_metadata (dev, FU_DEVICE_KEY_KIND, "internal");
		if (re.lowest_supported_fw_version != 0) {
			version_lowest = g_strdup_printf ("%" G_GUINT32_FORMAT,
							  re.lowest_supported_fw_version);
			fu_device_set_metadata (dev, FU_DEVICE_KEY_VERSION_LOWEST,
						version_lowest);
		}
		fu_device_set_metadata (dev, FU_DEVICE_KEY_ONLY_OFFLINE, "TRUE");
		//fu_device_set_metadata (dev, FU_DEVICE_KEY_DISPLAY_NAME, "FIXME");
		fu_provider_emit_added (provider, dev);
	}
	fwup_resource_iter_destroy (&iter);
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
	provider_class->update_offline = fu_provider_uefi_update;
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
