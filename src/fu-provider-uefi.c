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
#include <glib-object.h>
#include <gio/gio.h>
#include <fwup.h>

#include "fu-cleanup.h"
#include "fu-device.h"
#include "fu-pending.h"
#include "fu-provider-uefi.h"

static void     fu_provider_uefi_finalize	(GObject	*object);

G_DEFINE_TYPE (FuProviderUefi, fu_provider_uefi, FU_TYPE_PROVIDER)

/**
 * fu_provider_uefi_get_name:
 **/
static const gchar *
fu_provider_uefi_get_name (FuProvider *provider)
{
	return "UEFI";
}

/**
 * fu_provider_uefi_find:
 **/
static fwup_resource *
fu_provider_uefi_find (fwup_resource_iter *iter, const gchar *guid_str, GError **error)
{
	efi_guid_t *guid_raw;
	fwup_resource *re_matched = NULL;
	fwup_resource *re = NULL;
	_cleanup_free_ gchar *guid_str_tmp = NULL;

	/* get the hardware we're referencing */
	guid_str_tmp = g_strdup ("00000000-0000-0000-0000-000000000000");
	while (fwup_resource_iter_next (iter, &re) > 0) {

		/* convert to strings */
		fwup_get_guid (re, &guid_raw);
		if (efi_guid_to_str (guid_raw, &guid_str_tmp) < 0) {
			g_warning ("failed to convert guid to string");
			continue;
		}

		/* FIXME: also match hardware_instance too */
		if (g_strcmp0 (guid_str, guid_str_tmp) == 0) {
			re_matched = re;
			break;
		}
	}

	/* paradoxically, no hardware matched */
	if (re_matched == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "No UEFI firmware matched %s",
			     guid_str);
	}

	return re_matched;
}

/**
 * fu_provider_uefi_clear_results:
 **/
static gboolean
fu_provider_uefi_clear_results (FuProvider *provider, FuDevice *device, GError **error)
{
	fwup_resource_iter *iter = NULL;
	fwup_resource *re = NULL;
	gboolean ret = TRUE;

	/* get the hardware we're referencing */
	fwup_resource_iter_create (&iter);
	re = fu_provider_uefi_find (iter, fu_device_get_guid (device), error);
	if (re == NULL) {
		ret = FALSE;
		goto out;
	}
	if (fwup_clear_status (re) < 0) {
		ret = FALSE;
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Cannot create clear UEFI status for %s",
			     fu_device_get_guid (device));
		goto out;
	}
out:
	fwup_resource_iter_destroy (&iter);
	return ret;
}

/* only in git master */
#ifndef FWUP_LAST_ATTEMPT_STATUS_SUCCESS
#define FWUP_LAST_ATTEMPT_STATUS_SUCCESS			0x00000000
#define FWUP_LAST_ATTEMPT_STATUS_ERROR_UNSUCCESSFUL		0x00000001
#define FWUP_LAST_ATTEMPT_STATUS_ERROR_INSUFFICIENT_RESOURCES	0x00000002
#define FWUP_LAST_ATTEMPT_STATUS_ERROR_INCORRECT_VERSION	0x00000003
#define FWUP_LAST_ATTEMPT_STATUS_ERROR_INVALID_FORMAT		0x00000004
#define FWUP_LAST_ATTEMPT_STATUS_ERROR_AUTH_ERROR		0x00000005
#define FWUP_LAST_ATTEMPT_STATUS_ERROR_PWR_EVT_AC		0x00000006
#define FWUP_LAST_ATTEMPT_STATUS_ERROR_PWR_EVT_BATT		0x00000007
#endif

/**
 * fu_provider_uefi_last_attempt_status_to_str:
 **/
static const gchar *
fu_provider_uefi_last_attempt_status_to_str (guint32 status)
{
	if (status == FWUP_LAST_ATTEMPT_STATUS_SUCCESS)
		return "Success";
	if (status == FWUP_LAST_ATTEMPT_STATUS_ERROR_UNSUCCESSFUL)
		return "Unsuccessful";
	if (status == FWUP_LAST_ATTEMPT_STATUS_ERROR_INSUFFICIENT_RESOURCES)
		return "Insufficient resources";
	if (status == FWUP_LAST_ATTEMPT_STATUS_ERROR_INCORRECT_VERSION)
		return "Incorrect version";
	if (status == FWUP_LAST_ATTEMPT_STATUS_ERROR_INVALID_FORMAT)
		return "Invalid firmware format";
	if (status == FWUP_LAST_ATTEMPT_STATUS_ERROR_AUTH_ERROR)
		return "Authentication signing error";
	if (status == FWUP_LAST_ATTEMPT_STATUS_ERROR_PWR_EVT_AC)
		return "AC power required";
	if (status == FWUP_LAST_ATTEMPT_STATUS_ERROR_PWR_EVT_BATT)
		return "Battery level is too low";
	return NULL;
}

/**
 * fu_provider_uefi_get_results:
 **/
static gboolean
fu_provider_uefi_get_results (FuProvider *provider, FuDevice *device, GError **error)
{
	const gchar *tmp;
	fwup_resource_iter *iter = NULL;
	fwup_resource *re = NULL;
	gboolean ret = TRUE;
	guint32 status = 0;
	guint32 version = 0;
	_cleanup_free_ gchar *version_str = NULL;

	/* get the hardware we're referencing */
	fwup_resource_iter_create (&iter);
	re = fu_provider_uefi_find (iter, fu_device_get_guid (device), error);
	if (re == NULL) {
		ret = FALSE;
		goto out;
	}
	if (fwup_get_last_attempt_info (re, &version, &status, NULL) < 0) {
		ret = FALSE;
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Cannot get UEFI status for %s",
			     fu_device_get_guid (device));
		goto out;
	}
	version_str = g_strdup_printf ("%u", version);
	fu_device_set_metadata (device, FU_DEVICE_KEY_VERSION_NEW, version_str);
	if (status == FWUP_LAST_ATTEMPT_STATUS_SUCCESS) {
		fu_device_set_metadata (device, FU_DEVICE_KEY_PENDING_STATE,
					fu_pending_state_to_string (FU_PENDING_STATE_SUCCESS));
	} else {
		fu_device_set_metadata (device, FU_DEVICE_KEY_PENDING_STATE,
					fu_pending_state_to_string (FU_PENDING_STATE_FAILED));
		tmp = fu_provider_uefi_last_attempt_status_to_str (status);
		if (tmp != NULL)
			fu_device_set_metadata (device, FU_DEVICE_KEY_PENDING_ERROR, tmp);
	}
out:
	fwup_resource_iter_destroy (&iter);
	return ret;
}

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
	fwup_resource_iter *iter = NULL;
	fwup_resource *re = NULL;
	gboolean ret = TRUE;
	gint rc = 0;
	guint64 hardware_instance = 0;	/* FIXME */
	_cleanup_error_free_ GError *error_local = NULL;
	_cleanup_free_ gchar *standard_error = NULL;

	/* get the hardware we're referencing */
	fwup_resource_iter_create (&iter);
	re = fu_provider_uefi_find (iter, fu_device_get_guid (device), error);
	if (re == NULL) {
		ret = FALSE;
		goto out;
	}

	/* perform the update */
	g_debug ("Performing UEFI capsule update");
	fu_provider_set_status (provider, FWUPD_STATUS_SCHEDULING);
	if (fwup_set_up_update (re, hardware_instance, fd) < 0) {
		ret = FALSE;
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "UEFI firmware update failed: %s",
			     fwup_strerror (fwup_error));
		goto out;
	}
out:
	fwup_resource_iter_destroy (&iter);
	return ret;
}

/**
 * fu_provider_uefi_coldplug:
 **/
static gboolean
fu_provider_uefi_coldplug (FuProvider *provider, GError **error)
{
	fwup_resource_iter *iter = NULL;
	fwup_resource *re;
	_cleanup_free_ gchar *guid = NULL;
	_cleanup_object_unref_ FuDevice *dev = NULL;

	/* not supported */
	if (!fwup_supported ()) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "UEFI firmware updating not supported");
		return FALSE;
	}

	/* this can fail if we have no permissions */
	if (fwup_resource_iter_create (&iter) < 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Cannot create fwup iter");
		return FALSE;
	}

	/* add each device */
	guid = g_strdup ("00000000-0000-0000-0000-000000000000");
	while (fwup_resource_iter_next (iter, &re) > 0) {
		efi_guid_t *guid_raw;
		guint32 version_raw;
		guint64 hardware_instance = 0;	/* FIXME */
		_cleanup_free_ gchar *id = NULL;
		_cleanup_free_ gchar *version = NULL;
		_cleanup_free_ gchar *version_lowest = NULL;

		/* convert to strings */
		fwup_get_guid (re, &guid_raw);
		if (efi_guid_to_str (guid_raw, &guid) < 0) {
			g_warning ("failed to convert guid to string");
			continue;
		}
		fwup_get_fw_version(re, &version_raw);
		version = g_strdup_printf ("%" G_GUINT32_FORMAT, version_raw);
		id = g_strdup_printf ("UEFI-%s-dev%" G_GUINT64_FORMAT,
				      guid, hardware_instance);

		dev = fu_device_new ();
		fu_device_set_id (dev, id);
		fu_device_set_guid (dev, guid);
		fu_device_set_metadata (dev, FU_DEVICE_KEY_VERSION, version);
		fwup_get_lowest_supported_fw_version (re, &version_raw);
		if (version_raw != 0) {
			version_lowest = g_strdup_printf ("%" G_GUINT32_FORMAT,
							  version_raw);
			fu_device_set_metadata (dev, FU_DEVICE_KEY_VERSION_LOWEST,
						version_lowest);
		}
		fu_device_add_flag (dev, FU_DEVICE_FLAG_INTERNAL);
		fu_device_add_flag (dev, FU_DEVICE_FLAG_ALLOW_OFFLINE);
		fu_provider_device_add (provider, dev);
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

	provider_class->get_name = fu_provider_uefi_get_name;
	provider_class->coldplug = fu_provider_uefi_coldplug;
	provider_class->update_offline = fu_provider_uefi_update;
	provider_class->clear_results = fu_provider_uefi_clear_results;
	provider_class->get_results = fu_provider_uefi_get_results;
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
