/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include <glib-object.h>
#include <gio/gio.h>
#include <string.h>

#include "fwupd-device-private.h"
#include "fwupd-enums-private.h"
#include "fwupd-error.h"
#include "fwupd-release-private.h"
#include "fwupd-result.h"

static void fwupd_result_finalize	 (GObject *object);

typedef struct {
	gchar				*unique_id;
	FwupdTrustFlags			 update_trust_flags;
	FwupdUpdateState		 update_state;
	gchar				*update_error;
	FwupdDevice			*device;
	FwupdRelease			*release;
} FwupdResultPrivate;

enum {
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_DEVICE_ID,
	PROP_UNIQUE_ID,
	PROP_LAST
};

G_DEFINE_TYPE_WITH_PRIVATE (FwupdResult, fwupd_result, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fwupd_result_get_instance_private (o))

/**
 * fwupd_result_get_unique_id:
 * @result: A #FwupdResult
 *
 * Gets the unique ID that can be used for caching.
 *
 * Returns: the ID, or %NULL if unset
 *
 * Since: 0.7.3
 **/
const gchar *
fwupd_result_get_unique_id (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return priv->unique_id;
}

/**
 * fwupd_result_set_unique_id:
 * @result: A #FwupdResult
 * @unique_id: the result ID, e.g. "USB:foo"
 *
 * Sets the unique ID that can be used for caching.
 *
 * Since: 0.7.3
 **/
void
fwupd_result_set_unique_id (FwupdResult *result, const gchar *unique_id)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	g_free (priv->unique_id);
	priv->unique_id = g_strdup (unique_id);
}

/**
 * fwupd_result_get_release:
 * @result: A #FwupdResult
 *
 * Gets the default release for this result.
 *
 * Returns: (transfer none): the #FwupdRelease
 *
 * Since: 0.9.3
 **/
FwupdRelease *
fwupd_result_get_release (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return priv->release;
}

/**
 * fwupd_result_get_device:
 * @result: A #FwupdResult
 *
 * Gets the default device for this result.
 *
 * Returns: (transfer none): the #FwupdDevice
 *
 * Since: 0.9.3
 **/
FwupdDevice *
fwupd_result_get_device (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return priv->device;
}

/**
 * fwupd_result_get_device_id:
 * @result: A #FwupdResult
 *
 * Gets the ID.
 *
 * Returns: the ID, or %NULL if unset
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_result_get_device_id (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return fwupd_device_get_id (priv->device);
}

/**
 * fwupd_result_set_device_id:
 * @result: A #FwupdResult
 * @device_id: the result ID, e.g. "USB:foo"
 *
 * Sets the ID.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_device_id (FwupdResult *result, const gchar *device_id)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_device_set_id (priv->device, device_id);
}

/**
 * fwupd_result_get_guids:
 * @result: A #FwupdResult
 *
 * Gets the GUIDs.
 *
 * Returns: (element-type utf8) (transfer none): the GUIDs
 *
 * Since: 0.7.2
 **/
GPtrArray *
fwupd_result_get_guids (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return fwupd_device_get_guids (priv->device);
}

/**
 * fwupd_result_has_guid:
 * @result: A #FwupdResult
 * @guid: the GUID, e.g. "2082b5e0-7a64-478a-b1b2-e3404fab6dad"
 *
 * Finds out if the device has this specific GUID.
 *
 * Returns: %TRUE if the GUID is found
 *
 * Since: 0.7.2
 **/
gboolean
fwupd_result_has_guid (FwupdResult *result, const gchar *guid)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), FALSE);
	return fwupd_device_has_guid (priv->device, guid);
}

/**
 * fwupd_result_add_guid:
 * @result: A #FwupdResult
 * @guid: the GUID, e.g. "2082b5e0-7a64-478a-b1b2-e3404fab6dad"
 *
 * Adds the GUID if it does not already exist.
 *
 * Since: 0.7.2
 **/
void
fwupd_result_add_guid (FwupdResult *result, const gchar *guid)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_device_add_guid (priv->device, guid);
}

/**
 * fwupd_result_get_guid_default:
 * @result: A #FwupdResult
 *
 * Gets the default GUID.
 *
 * Returns: the GUID, or %NULL if unset
 *
 * Since: 0.7.2
 **/
const gchar *
fwupd_result_get_guid_default (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return fwupd_device_get_guid_default (priv->device);
}

/**
 * fwupd_result_get_guid:
 * @result: A #FwupdResult
 *
 * Gets the GUID.
 *
 * Returns: the GUID, or %NULL if unset
 *
 * This function has been deprecated since 0.7.2.
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_result_get_guid (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return fwupd_device_get_guid_default (priv->device);
}

/**
 * fwupd_result_set_guid:
 * @result: A #FwupdResult
 * @guid: the GUID, e.g. "2082b5e0-7a64-478a-b1b2-e3404fab6dad"
 *
 * Sets the GUID.
 *
 * This function has been deprecated since 0.7.2.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_guid (FwupdResult *result, const gchar *guid)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	fwupd_device_add_guid (priv->device, guid);
}

/**
 * fwupd_result_get_device_name:
 * @result: A #FwupdResult
 *
 * Gets the device name.
 *
 * Returns: the device name, or %NULL if unset
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_result_get_device_name (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return fwupd_device_get_name (priv->device);
}

/**
 * fwupd_result_set_device_name:
 * @result: A #FwupdResult
 * @device_name: the device update_name, e.g. "ColorHug2"
 *
 * Sets the device update_name.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_device_name (FwupdResult *result, const gchar *device_name)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_device_set_name (priv->device, device_name);
}

/**
 * fwupd_result_get_device_vendor:
 * @result: A #FwupdResult
 *
 * Gets the device vendor.
 *
 * Returns: the device vendor, or %NULL if unset
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_result_get_device_vendor (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return fwupd_device_get_vendor (priv->device);
}

/**
 * fwupd_result_set_device_vendor:
 * @result: A #FwupdResult
 * @device_vendor: the description
 *
 * Sets the device vendor.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_device_vendor (FwupdResult *result, const gchar *device_vendor)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_device_set_vendor (priv->device, device_vendor);
}

/**
 * fwupd_result_get_device_description:
 * @result: A #FwupdResult
 *
 * Gets the device description in AppStream markup format.
 *
 * Returns: the device description, or %NULL if unset
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_result_get_device_description (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return fwupd_device_get_description (priv->device);
}

/**
 * fwupd_result_set_device_description:
 * @result: A #FwupdResult
 * @device_description: the description in AppStream markup format
 *
 * Sets the device description.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_device_description (FwupdResult *result, const gchar *device_description)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_device_set_description (priv->device, device_description);
}

/**
 * fwupd_result_get_device_version:
 * @result: A #FwupdResult
 *
 * Gets the device version.
 *
 * Returns: the device version, or %NULL if unset
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_result_get_device_version (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return fwupd_device_get_version (priv->device);
}

/**
 * fwupd_result_set_device_version:
 * @result: A #FwupdResult
 * @device_version: the device version, e.g. "1.2.3"
 *
 * Sets the device version.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_device_version (FwupdResult *result, const gchar *device_version)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_device_set_version (priv->device, device_version);
}

/**
 * fwupd_result_get_update_version:
 * @result: A #FwupdResult
 *
 * Gets the update version.
 *
 * Returns: the update version, or %NULL if unset
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_result_get_update_version (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return fwupd_release_get_version (priv->release);
}

/**
 * fwupd_result_get_device_version_lowest:
 * @result: A #FwupdResult
 *
 * Gets the lowest version of firmware the device will accept.
 *
 * Returns: the device version_lowest, or %NULL if unset
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_result_get_device_version_lowest (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return fwupd_device_get_version_lowest (priv->device);
}

/**
 * fwupd_result_set_device_version_lowest:
 * @result: A #FwupdResult
 * @device_version_lowest: the description
 *
 * Sets the lowest version of firmware the device will accept.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_device_version_lowest (FwupdResult *result, const gchar *device_version_lowest)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_device_set_version_lowest (priv->device, device_version_lowest);
}

/**
 * fwupd_result_get_device_version_bootloader:
 * @result: A #FwupdResult
 *
 * Gets the version of the bootloader.
 *
 * Returns: the device version_bootloader, or %NULL if unset
 *
 * Since: 0.8.0
 **/
const gchar *
fwupd_result_get_device_version_bootloader (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return fwupd_device_get_version_bootloader (priv->device);
}

/**
 * fwupd_result_set_device_version_bootloader:
 * @result: A #FwupdResult
 * @device_version_bootloader: the description
 *
 * Sets the bootloader version.
 *
 * Since: 0.8.0
 **/
void
fwupd_result_set_device_version_bootloader (FwupdResult *result,
					    const gchar *device_version_bootloader)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_device_set_version_bootloader (priv->device, device_version_bootloader);
}

/**
 * fwupd_result_device_get_flashes_left:
 * @result: A #FwupdResult
 *
 * Gets the number of flash cycles left on the device
 *
 * Returns: the flash cycles left, or %NULL if unset
 *
 * Since: 0.7.1
 **/
guint32
fwupd_result_get_device_flashes_left (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), 0);
	return fwupd_device_get_flashes_left (priv->device);
}

/**
 * fwupd_result_device_set_flashes_left:
 * @result: A #FwupdResult
 * @flashes_left: the description
 *
 * Sets the number of flash cycles left on the device
 *
 * Since: 0.7.1
 **/
void
fwupd_result_set_device_flashes_left (FwupdResult *result, guint32 flashes_left)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_device_set_flashes_left (priv->device, flashes_left);
}

/**
 * fwupd_result_set_update_version:
 * @result: A #FwupdResult
 * @update_version: the update version, e.g. "1.2.4"
 *
 * Sets the update version.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_update_version (FwupdResult *result, const gchar *update_version)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_release_set_version (priv->release, update_version);
}

/**
 * fwupd_result_get_update_filename:
 * @result: A #FwupdResult
 *
 * Gets the update filename.
 *
 * Returns: the update filename, or %NULL if unset
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_result_get_update_filename (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return fwupd_release_get_filename (priv->release);
}

/**
 * fwupd_result_set_update_filename:
 * @result: A #FwupdResult
 * @update_filename: the update filename on disk
 *
 * Sets the update filename.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_update_filename (FwupdResult *result, const gchar *update_filename)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_release_set_filename (priv->release, update_filename);
}

/**
 * fwupd_result_get_update_state:
 * @result: A #FwupdResult
 *
 * Gets the update state.
 *
 * Returns: the update state, or %FWUPD_UPDATE_STATE_UNKNOWN if unset
 *
 * Since: 0.7.0
 **/
FwupdUpdateState
fwupd_result_get_update_state (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), FWUPD_UPDATE_STATE_UNKNOWN);
	return priv->update_state;
}

/**
 * fwupd_result_set_update_state:
 * @result: A #FwupdResult
 * @update_state: the state, e.g. %FWUPD_UPDATE_STATE_PENDING
 *
 * Sets the update state.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_update_state (FwupdResult *result, FwupdUpdateState update_state)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	priv->update_state = update_state;
}

/**
 * fwupd_result_get_update_checksum:
 * @result: A #FwupdResult
 *
 * Gets the update checksum.
 *
 * Returns: the update checksum, or %NULL if unset
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_result_get_update_checksum (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return fwupd_release_get_checksum (priv->release);
}

/**
 * fwupd_result_set_update_checksum:
 * @result: A #FwupdResult
 * @update_checksum: the update checksum
 *
 * Sets the update checksum.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_update_checksum (FwupdResult *result, const gchar *update_checksum)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_release_set_checksum (priv->release, update_checksum);
}

/**
 * fwupd_result_get_update_checksum_kind:
 * @result: A #FwupdResult
 *
 * Gets the update checkum kind.
 *
 * Returns: the #GChecksumType
 *
 * Since: 0.7.0
 **/
GChecksumType
fwupd_result_get_update_checksum_kind (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), 0);
	return fwupd_release_get_checksum_kind (priv->release);
}

/**
 * fwupd_result_set_update_checksum_kind:
 * @result: A #FwupdResult
 * @checkum_kind: the checksum kind, e.g. %G_CHECKSUM_SHA1
 *
 * Sets the update checkum kind.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_update_checksum_kind (FwupdResult *result, GChecksumType checkum_kind)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_release_set_checksum_kind (priv->release, checkum_kind);
}

/**
 * fwupd_result_get_update_uri:
 * @result: A #FwupdResult
 *
 * Gets the update uri.
 *
 * Returns: the update uri, or %NULL if unset
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_result_get_update_uri (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return fwupd_release_get_uri (priv->release);
}

/**
 * fwupd_result_set_update_uri:
 * @result: A #FwupdResult
 * @update_uri: the update URI
 *
 * Sets the update uri, i.e. where you can download the firmware from.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_update_uri (FwupdResult *result, const gchar *update_uri)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_release_set_uri (priv->release, update_uri);
}

/**
 * fwupd_result_get_update_homepage:
 * @result: A #FwupdResult
 *
 * Gets the update homepage.
 *
 * Returns: the update homepage, or %NULL if unset
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_result_get_update_homepage (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return fwupd_release_get_homepage (priv->release);
}

/**
 * fwupd_result_set_update_homepage:
 * @result: A #FwupdResult
 * @update_homepage: the description
 *
 * Sets the update homepage.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_update_homepage (FwupdResult *result, const gchar *update_homepage)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_release_set_homepage (priv->release, update_homepage);
}

/**
 * fwupd_result_get_update_description:
 * @result: A #FwupdResult
 *
 * Gets the update description in AppStream markup format.
 *
 * Returns: the update description, or %NULL if unset
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_result_get_update_description (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return fwupd_release_get_description (priv->release);
}

/**
 * fwupd_result_set_update_description:
 * @result: A #FwupdResult
 * @update_description: the update description in AppStream markup format
 *
 * Sets the update description.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_update_description (FwupdResult *result, const gchar *update_description)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_release_set_description (priv->release, update_description);
}

/**
 * fwupd_result_get_update_id:
 * @result: A #FwupdResult
 *
 * Gets the update id.
 *
 * Returns: the update id, or %NULL if unset
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_result_get_update_id (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return fwupd_release_get_appstream_id (priv->release);
}

/**
 * fwupd_result_set_update_id:
 * @result: A #FwupdResult
 * @update_id: the AppStream component ID, e.g. "org.hughski.ColorHug2.firmware"
 *
 * Sets the update id.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_update_id (FwupdResult *result, const gchar *update_id)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_release_set_appstream_id (priv->release, update_id);
}

/**
 * fwupd_result_get_update_size:
 * @result: A #FwupdResult
 *
 * Gets the update size.
 *
 * Returns: the update size in bytes, or 0 if unset
 *
 * Since: 0.7.0
 **/
guint64
fwupd_result_get_update_size (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), 0);
	return fwupd_release_get_size (priv->release);
}

/**
 * fwupd_result_set_update_size:
 * @result: A #FwupdResult
 * @update_size: the update size in bytes
 *
 * Sets the update size.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_update_size (FwupdResult *result, guint64 update_size)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_release_set_size (priv->release, update_size);
}

/**
 * fwupd_result_get_device_checksum:
 * @result: A #FwupdResult
 *
 * Gets the device checksum.
 *
 * Returns: the device checksum, or %NULL if unset
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_result_get_device_checksum (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return fwupd_device_get_checksum (priv->device);
}

/**
 * fwupd_result_set_device_checksum:
 * @result: A #FwupdResult
 * @device_checksum: the device checksum
 *
 * Sets the device checksum, i.e. what is on the device right now.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_device_checksum (FwupdResult *result, const gchar *device_checksum)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_device_set_checksum (priv->device, device_checksum);
}

/**
 * fwupd_result_get_device_checksum_kind:
 * @result: A #FwupdResult
 *
 * Gets the device checkum kind.
 *
 * Returns: the #GChecksumType
 *
 * Since: 0.7.0
 **/
GChecksumType
fwupd_result_get_device_checksum_kind (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), 0);
	return fwupd_device_get_checksum_kind (priv->device);
}

/**
 * fwupd_result_set_device_checksum_kind:
 * @result: A #FwupdResult
 * @checkum_kind: the checksum kind, e.g. %G_CHECKSUM_SHA1
 *
 * Sets the device checkum kind.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_device_checksum_kind (FwupdResult *result, GChecksumType checkum_kind)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_device_set_checksum_kind (priv->device, checkum_kind);
}

/**
 * fwupd_result_get_update_summary:
 * @result: A #FwupdResult
 *
 * Gets the update summary.
 *
 * Returns: the update summary, or %NULL if unset
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_result_get_update_summary (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return fwupd_release_get_summary (priv->release);
}

/**
 * fwupd_result_set_update_summary:
 * @result: A #FwupdResult
 * @update_summary: the update one line summary
 *
 * Sets the update summary.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_update_summary (FwupdResult *result, const gchar *update_summary)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_release_set_summary (priv->release, update_summary);
}

/**
 * fwupd_result_get_device_provider:
 * @result: A #FwupdResult
 *
 * Gets the device provider.
 *
 * Returns: the device provider, or %NULL if unset
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_result_get_device_provider (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return fwupd_device_get_provider (priv->device);
}

/**
 * fwupd_result_set_device_provider:
 * @result: A #FwupdResult
 * @device_provider: the provider name, e.g. "colorhug"
 *
 * Sets the device provider.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_device_provider (FwupdResult *result, const gchar *device_provider)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_device_set_provider (priv->device, device_provider);
}

/**
 * fwupd_result_get_update_error:
 * @result: A #FwupdResult
 *
 * Gets the update error.
 *
 * Returns: the update error, or %NULL if unset
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_result_get_update_error (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return priv->update_error;
}

/**
 * fwupd_result_set_update_error:
 * @result: A #FwupdResult
 * @update_error: the update error string
 *
 * Sets the update error.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_update_error (FwupdResult *result, const gchar *update_error)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	g_free (priv->update_error);
	priv->update_error = g_strdup (update_error);
}

/**
 * fwupd_result_get_update_trust_flags:
 * @result: A #FwupdResult
 *
 * Gets the update trust_flags.
 *
 * Returns: the #FwupdTrustFlags, or 0 if unset
 *
 * Since: 0.7.0
 **/
FwupdTrustFlags
fwupd_result_get_update_trust_flags (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), 0);
	return priv->update_trust_flags;
}

/**
 * fwupd_result_set_update_trust_flags:
 * @result: A #FwupdResult
 * @trust_flags: the trust flags, e.g. %FWUPD_TRUST_FLAG_PAYLOAD
 *
 * Sets the update trust_flags.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_update_trust_flags (FwupdResult *result, FwupdTrustFlags trust_flags)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	priv->update_trust_flags = trust_flags;
}

/**
 * fwupd_result_get_update_vendor:
 * @result: A #FwupdResult
 *
 * Gets the update vendor.
 *
 * Returns: the update vendor, or %NULL if unset
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_result_get_update_vendor (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return fwupd_release_get_vendor (priv->release);
}

/**
 * fwupd_result_set_update_vendor:
 * @result: A #FwupdResult
 * @update_vendor: the vendor name, e.g. "Hughski Limited"
 *
 * Sets the update vendor.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_update_vendor (FwupdResult *result, const gchar *update_vendor)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_release_set_vendor (priv->release, update_vendor);
}

/**
 * fwupd_result_get_update_license:
 * @result: A #FwupdResult
 *
 * Gets the update license.
 *
 * Returns: the update license, or %NULL if unset
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_result_get_update_license (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return fwupd_release_get_license (priv->release);
}

/**
 * fwupd_result_set_update_license:
 * @result: A #FwupdResult
 * @update_license: the description
 *
 * Sets the update license.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_update_license (FwupdResult *result, const gchar *update_license)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_release_set_license (priv->release, update_license);
}

/**
 * fwupd_result_get_update_name:
 * @result: A #FwupdResult
 *
 * Gets the update name.
 *
 * Returns: the update name, or %NULL if unset
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_result_get_update_name (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return fwupd_release_get_name (priv->release);
}

/**
 * fwupd_result_set_update_name:
 * @result: A #FwupdResult
 * @update_name: the description
 *
 * Sets the update name.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_update_name (FwupdResult *result, const gchar *update_name)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_release_set_name (priv->release, update_name);
}

/**
 * fwupd_result_get_device_flags:
 * @result: A #FwupdResult
 *
 * Gets the device flags.
 *
 * Returns: the device flags, or 0 if unset
 *
 * Since: 0.7.0
 **/
guint64
fwupd_result_get_device_flags (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), 0);
	return fwupd_device_get_flags (priv->device);
}

/**
 * fwupd_result_set_device_flags:
 * @result: A #FwupdResult
 * @device_flags: the device flags, e.g. %FWUPD_DEVICE_FLAG_REQUIRE_AC
 *
 * Sets the device flags.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_device_flags (FwupdResult *result, guint64 device_flags)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_device_set_flags (priv->device, device_flags);
}

/**
 * fwupd_result_add_device_flag:
 * @result: A #FwupdResult
 * @flag: the #FwupdDeviceFlags
 *
 * Adds a specific device flag to the result.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_add_device_flag (FwupdResult *result, FwupdDeviceFlags flag)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_device_add_flag (priv->device, flag);
}

/**
 * fwupd_result_remove_device_flag:
 * @result: A #FwupdResult
 * @flag: the #FwupdDeviceFlags
 *
 * Removes a specific device flag from the result.
 *
 * Since: 0.7.3
 **/
void
fwupd_result_remove_device_flag (FwupdResult *result, FwupdDeviceFlags flag)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_device_remove_flag (priv->device, flag);
}

/**
 * fwupd_result_has_device_flag:
 * @result: A #FwupdResult
 * @flag: the #FwupdDeviceFlags
 *
 * Finds if the device has a specific device flag.
 *
 * Returns: %TRUE if the flag is set
 *
 * Since: 0.7.0
 **/
gboolean
fwupd_result_has_device_flag (FwupdResult *result, FwupdDeviceFlags flag)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), FALSE);
	return fwupd_device_has_flag (priv->device, flag);
}

/**
 * fwupd_result_get_device_created:
 * @result: A #FwupdResult
 *
 * Gets when the result was device_created.
 *
 * Returns: the UNIX time, or 0 if unset
 *
 * Since: 0.7.0
 **/
guint64
fwupd_result_get_device_created (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), 0);
	return fwupd_device_get_created (priv->device);
}


/**
 * fwupd_result_set_device_created:
 * @result: A #FwupdResult
 * @device_created: the UNIX time
 *
 * Sets when the result was device_created.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_device_created (FwupdResult *result, guint64 device_created)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_device_set_created (priv->device, device_created);
}

/**
 * fwupd_result_get_device_modified:
 * @result: A #FwupdResult
 *
 * Gets when the result was device_modified.
 *
 * Returns: the UNIX time, or 0 if unset
 *
 * Since: 0.7.0
 **/
guint64
fwupd_result_get_device_modified (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), 0);
	return fwupd_device_get_modified (priv->device);
}

/**
 * fwupd_result_set_device_modified:
 * @result: A #FwupdResult
 * @device_modified: the UNIX time
 *
 * Sets when the result was device_modified.
 *
 * Since: 0.7.0
 **/
void
fwupd_result_set_device_modified (FwupdResult *result, guint64 device_modified)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	fwupd_device_set_modified (priv->device, device_modified);
}

/**
 * fwupd_result_to_data:
 * @result: A #FwupdResult
 * @type_string: The Gvariant type string, e.g. "{sa{sv}}" or "(a{sv})"
 *
 * Creates a GVariant from the result data.
 *
 * Returns: the GVariant, or %NULL for error
 *
 * Since: 0.7.0
 **/
GVariant *
fwupd_result_to_data (FwupdResult *result, const gchar *type_string)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	GVariantBuilder builder;

	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	g_return_val_if_fail (type_string != NULL, NULL);

	/* create an array with all the metadata in */
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	if (priv->unique_id != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_UNIQUE_ID,
				       g_variant_new_string (priv->unique_id));
	}
	if (priv->update_error != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_UPDATE_ERROR,
				       g_variant_new_string (priv->update_error));
	}
	if (priv->update_state != FWUPD_UPDATE_STATE_UNKNOWN) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_UPDATE_STATE,
				       g_variant_new_uint32 (priv->update_state));
	}
	if (priv->update_trust_flags != 0) {
		g_variant_builder_add (&builder, "{sv}",
				       FWUPD_RESULT_KEY_UPDATE_TRUST_FLAGS,
				       g_variant_new_uint64 (priv->update_trust_flags));
	}

	/* device and release objects */
	fwupd_release_to_variant_builder (priv->release, &builder);
	fwupd_device_to_variant_builder (priv->device, &builder);

	/* supported types */
	if (g_strcmp0 (type_string, "{sa{sv}}") == 0) {
		const gchar *device_id = fwupd_device_get_id (priv->device);
		if (device_id == NULL)
			device_id = "";
		return g_variant_new ("{sa{sv}}", device_id, &builder);
	}
	if (g_strcmp0 (type_string, "(a{sv})") == 0)
		return g_variant_new ("(a{sv})", &builder);
	return NULL;
}

static void
fwupd_result_from_key_value (FwupdResult *result, const gchar *key, GVariant *value)
{
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_UNIQUE_ID) == 0) {
		fwupd_result_set_unique_id (result, g_variant_get_string (value, NULL));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_UPDATE_TRUST_FLAGS) == 0) {
		fwupd_result_set_update_trust_flags (result, g_variant_get_uint64 (value));
		return;
	}
	if (g_strcmp0 (key, FWUPD_RESULT_KEY_UPDATE_STATE) == 0) {
		/* old daemon version and new client */
		if (g_strcmp0 (g_variant_get_type_string (value), "s") == 0) {
			FwupdUpdateState tmp;
			tmp = fwupd_update_state_from_string (g_variant_get_string (value, NULL));
			fwupd_result_set_update_state (result, tmp);
		} else {
			fwupd_result_set_update_state (result, g_variant_get_uint32 (value));
		}
		return;
	}
}

static void
fwupd_pad_kv_str (GString *str, const gchar *key, const gchar *value)
{
	/* ignore */
	if (key == NULL || value == NULL)
		return;
	g_string_append_printf (str, "  %s: ", key);
	for (gsize i = strlen (key); i < 20; i++)
		g_string_append (str, " ");
	g_string_append_printf (str, "%s\n", value);
}

static void
fwupd_pad_kv_ups (GString *str, const gchar *key, FwupdUpdateState value)
{
	if (value == FWUPD_UPDATE_STATE_UNKNOWN)
		return;
	fwupd_pad_kv_str (str, key, fwupd_update_state_to_string (value));
}

static void
fwupd_pad_kv_tfl (GString *str, const gchar *key, FwupdTrustFlags trust_flags)
{
	guint i;
	g_autoptr(GString) tmp = NULL;

	tmp = g_string_new ("");
	for (i = 1; i < FWUPD_TRUST_FLAG_LAST; i *= 2) {
		if ((trust_flags & i) == 0)
			continue;
		g_string_append_printf (tmp, "%s|",
					fwupd_trust_flag_to_string (i));
	}
	if (tmp->len == 0) {
		g_string_append (tmp, fwupd_trust_flag_to_string (0));
	} else {
		g_string_truncate (tmp, tmp->len - 1);
	}
	fwupd_pad_kv_str (str, key, tmp->str);
}

/**
 * fwupd_result_to_string:
 * @result: A #FwupdResult
 *
 * Builds a text representation of the object.
 *
 * Returns: text, or %NULL for invalid
 *
 * Since: 0.7.0
 **/
gchar *
fwupd_result_to_string (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	GString *str;
	g_autofree gchar *device_str = NULL;
	g_autofree gchar *release_str = NULL;

	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);

	str = g_string_new ("");

	/* not set when using GetDetails */
	if (fwupd_device_get_name (priv->device) != NULL)
		g_string_append_printf (str, "%s\n", fwupd_device_get_name (priv->device));
	else
		g_string_append_printf (str, "%s\n", "Unknown Device");

	/* device */
	fwupd_pad_kv_str (str, FWUPD_RESULT_KEY_UNIQUE_ID, priv->unique_id);
	fwupd_pad_kv_ups (str, FWUPD_RESULT_KEY_UPDATE_STATE, priv->update_state);

	/* device and release */
	device_str = fwupd_device_to_string (priv->device);
	g_string_append (str, device_str);
	release_str = fwupd_release_to_string (priv->release);
	g_string_append (str, release_str);
	if (fwupd_release_get_version (priv->release) != NULL)
		fwupd_pad_kv_tfl (str, FWUPD_RESULT_KEY_UPDATE_TRUST_FLAGS, priv->update_trust_flags);

	return g_string_free (str, FALSE);
}

static void
fwupd_result_get_property (GObject *object, guint prop_id,
			   GValue *value, GParamSpec *pspec)
{
	FwupdResult *result = FWUPD_RESULT (object);
	FwupdResultPrivate *priv = GET_PRIVATE (result);

	switch (prop_id) {
	case PROP_DEVICE_ID:
		g_value_set_string (value, fwupd_device_get_id (priv->device));
		break;
	case PROP_UNIQUE_ID:
		g_value_set_string (value, priv->unique_id);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fwupd_result_set_property (GObject *object, guint prop_id,
			   const GValue *value, GParamSpec *pspec)
{
	FwupdResult *result = FWUPD_RESULT (object);
	FwupdResultPrivate *priv = GET_PRIVATE (result);

	switch (prop_id) {
	case PROP_DEVICE_ID:
		fwupd_device_set_id (priv->device, g_value_get_string (value));
		break;
	case PROP_UNIQUE_ID:
		g_free (priv->unique_id);
		priv->unique_id = g_strdup (g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fwupd_result_class_init (FwupdResultClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fwupd_result_finalize;
	object_class->get_property = fwupd_result_get_property;
	object_class->set_property = fwupd_result_set_property;

	/**
	 * FwupdResult:device-id:
	 *
	 * The device ID for this result.
	 *
	 * Since: 0.7.0
	 */
	pspec = g_param_spec_string ("device-id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_DEVICE_ID, pspec);
}

static void
fwupd_result_init (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	priv->release = fwupd_release_new ();
	priv->device = fwupd_device_new ();
}

static void
fwupd_result_finalize (GObject *object)
{
	FwupdResult *result = FWUPD_RESULT (object);
	FwupdResultPrivate *priv = GET_PRIVATE (result);

	g_object_unref (priv->device);
	g_object_unref (priv->release);
	g_free (priv->unique_id);
	g_free (priv->update_error);

	G_OBJECT_CLASS (fwupd_result_parent_class)->finalize (object);
}

static void
fwupd_result_from_variant_iter (FwupdResult *result, GVariantIter *iter)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	GVariant *value;
	const gchar *key;
	while (g_variant_iter_next (iter, "{&sv}", &key, &value)) {
		fwupd_result_from_key_value (result, key, value);
		fwupd_release_from_key_value (priv->release, key, value);
		fwupd_device_from_key_value (priv->device, key, value);
		g_variant_unref (value);
	}
}

/**
 * fwupd_result_new_from_data:
 * @data: a #GVariant
 *
 * Creates a new result using packed data.
 *
 * Returns: a new #FwupdResult, or %NULL if @data was invalid
 *
 * Since: 0.7.0
 **/
FwupdResult *
fwupd_result_new_from_data (GVariant *data)
{
	FwupdResult *res = NULL;
	const gchar *id;
	const gchar *type_string;
	g_autoptr(GVariantIter) iter = NULL;

	/* format from GetDetails */
	type_string = g_variant_get_type_string (data);
	if (g_strcmp0 (type_string, "(a{sv})") == 0) {
		res = fwupd_result_new ();
		g_variant_get (data, "(a{sv})", &iter);
		fwupd_result_from_variant_iter (res, iter);
	} else if (g_strcmp0 (type_string, "{sa{sv}}") == 0) {
		res = fwupd_result_new ();
		g_variant_get (data, "{&sa{sv}}", &id, &iter);
		fwupd_device_set_id (fwupd_result_get_device (res), id);
		fwupd_result_from_variant_iter (res, iter);
	} else {
		g_warning ("type %s not known", type_string);
	}
	return res;
}

/**
 * fwupd_result_new:
 *
 * Creates a new result.
 *
 * Returns: a new #FwupdResult
 *
 * Since: 0.7.0
 **/
FwupdResult *
fwupd_result_new (void)
{
	FwupdResult *result;
	result = g_object_new (FWUPD_TYPE_RESULT, NULL);
	return FWUPD_RESULT (result);
}
