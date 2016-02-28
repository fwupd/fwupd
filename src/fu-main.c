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
#include <appstream-glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <gio/gunixinputstream.h>
#include <glib/gi18n.h>
#include <locale.h>
#include <polkit/polkit.h>
#include <stdlib.h>
#include <fcntl.h>

#include "fu-debug.h"
#include "fu-device.h"
#include "fu-keyring.h"
#include "fu-pending.h"
#include "fu-provider.h"
#include "fu-provider-dfu.h"
#include "fu-provider-rpi.h"
#include "fu-provider-udev.h"
#include "fu-provider-usb.h"
#include "fu-resources.h"
#include "fu-quirks.h"

#ifdef HAVE_COLORHUG
  #include "fu-provider-chug.h"
#endif
#ifdef HAVE_UEFI
  #include "fu-provider-uefi.h"
#endif

#ifndef PolkitAuthorizationResult_autoptr
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PolkitAuthorizationResult, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PolkitSubject, g_object_unref)
#endif

#define FU_MAIN_FIRMWARE_SIZE_MAX	(32 * 1024 * 1024)	/* bytes */

typedef struct {
	GDBusConnection		*connection;
	GDBusNodeInfo		*introspection_daemon;
	GDBusProxy		*proxy_uid;
	GMainLoop		*loop;
	GPtrArray		*devices;	/* of FuDeviceItem */
	GPtrArray		*providers;
	PolkitAuthority		*authority;
	FwupdStatus		 status;
	FuPending		*pending;
	AsProfile		*profile;
	AsStore			*store;
	guint			 store_changed_id;
} FuMainPrivate;

typedef struct {
	FuDevice		*device;
	FuProvider		*provider;
} FuDeviceItem;

/**
 * fu_main_emit_changed:
 **/
static void
fu_main_emit_changed (FuMainPrivate *priv)
{
	/* not yet connected */
	if (priv->connection == NULL)
		return;
	g_dbus_connection_emit_signal (priv->connection,
				       NULL,
				       FWUPD_DBUS_PATH,
				       FWUPD_DBUS_INTERFACE,
				       "Changed",
				       NULL, NULL);
}

/**
 * fu_main_emit_property_changed:
 **/
static void
fu_main_emit_property_changed (FuMainPrivate *priv,
			       const gchar *property_name,
			       GVariant *property_value)
{
	GVariantBuilder builder;
	GVariantBuilder invalidated_builder;

	/* not yet connected */
	if (priv->connection == NULL)
		return;

	/* build the dict */
	g_variant_builder_init (&invalidated_builder, G_VARIANT_TYPE ("as"));
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	g_variant_builder_add (&builder,
			       "{sv}",
			       property_name,
			       property_value);
	g_dbus_connection_emit_signal (priv->connection,
				       NULL,
				       FWUPD_DBUS_PATH,
				       "org.freedesktop.DBus.Properties",
				       "PropertiesChanged",
				       g_variant_new ("(sa{sv}as)",
				       FWUPD_DBUS_INTERFACE,
				       &builder,
				       &invalidated_builder),
				       NULL);
	g_variant_builder_clear (&builder);
	g_variant_builder_clear (&invalidated_builder);
}

/**
 * fu_main_set_status:
 **/
static void
fu_main_set_status (FuMainPrivate *priv, FwupdStatus status)
{
	if (priv->status == status)
		return;
	priv->status = status;

	/* emit changed */
	g_debug ("Emitting PropertyChanged('Status'='%s')",
		 fwupd_status_to_string (status));
	fu_main_emit_property_changed (priv, "Status", g_variant_new_uint32 (status));
}

/**
 * fu_main_device_array_to_variant:
 **/
static GVariant *
fu_main_device_array_to_variant (GPtrArray *devices, GError **error)
{
	GVariantBuilder builder;
	guint i;

	/* no devices */
	if (devices->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO,
				     "Nothing to do");
		return NULL;
	}

	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	for (i = 0; i < devices->len; i++) {
		GVariant *tmp;
		FuDeviceItem *item;
		item = g_ptr_array_index (devices, i);
		tmp = fu_device_to_variant (item->device);
		g_variant_builder_add_value (&builder, tmp);
	}
	return g_variant_new ("(a{sa{sv}})", &builder);
}

/**
 * fu_main_item_free:
 **/
static void
fu_main_item_free (FuDeviceItem *item)
{
	g_object_unref (item->device);
	g_object_unref (item->provider);
	g_free (item);
}

/**
 * fu_main_get_item_by_id:
 **/
static FuDeviceItem *
fu_main_get_item_by_id (FuMainPrivate *priv, const gchar *id)
{
	FuDeviceItem *item;
	guint i;

	for (i = 0; i < priv->devices->len; i++) {
		item = g_ptr_array_index (priv->devices, i);
		if (g_strcmp0 (fu_device_get_id (item->device), id) == 0)
			return item;
		if (g_strcmp0 (fu_device_get_equivalent_id (item->device), id) == 0)
			return item;
	}
	return NULL;
}

/**
 * fu_main_get_provider_by_name:
 **/
static FuProvider *
fu_main_get_provider_by_name (FuMainPrivate *priv, const gchar *name)
{
	FuProvider *provider;
	guint i;

	for (i = 0; i < priv->providers->len; i++) {
		provider = g_ptr_array_index (priv->providers, i);
		if (g_strcmp0 (fu_provider_get_name (provider), name) == 0)
			return provider;
	}
	return NULL;
}

/**
 * fu_main_get_release_trust_flags:
 **/
static gboolean
fu_main_get_release_trust_flags (AsRelease *release,
				 FwupdTrustFlags *trust_flags,
				 GError **error)
{
	AsChecksum *csum_tmp;
	GBytes *blob_payload;
	GBytes *blob_signature;
	const gchar *fn;
	g_autofree gchar *pki_dir = NULL;
	g_autofree gchar *fn_signature = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuKeyring) kr = NULL;

	/* no filename? */
	csum_tmp = as_release_get_checksum_by_target (release, AS_CHECKSUM_TARGET_CONTENT);
	fn = as_checksum_get_filename (csum_tmp);
	if (fn == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no filename");
		return FALSE;
	}

	/* no signature == no trust */
	fn_signature = g_strdup_printf ("%s.asc", fn);
	blob_signature = as_release_get_blob (release, fn_signature);
	if (blob_signature == NULL) {
		g_debug ("firmware archive contained no GPG signature");
		return TRUE;
	}

	/* get payload */
	blob_payload = as_release_get_blob (release, fn);
	if (blob_payload == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no payload");
		return FALSE;
	}

	/* check we were installed correctly */
	pki_dir = g_build_filename (SYSCONFDIR, "pki", "fwupd", NULL);
	if (!g_file_test (pki_dir, G_FILE_TEST_EXISTS)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "PKI directory %s not found", pki_dir);
		return FALSE;
	}

	/* verify against the system trusted keys */
	kr = fu_keyring_new ();
	if (!fu_keyring_add_public_keys (kr, pki_dir, error))
		return FALSE;
	if (!fu_keyring_verify_data (kr, blob_payload, blob_signature, &error_local)) {
		g_warning ("untrusted as failed to verify: %s",
			   error_local->message);
		return TRUE;
	}

	/* awesome! */
	g_debug ("marking payload as trusted");
	*trust_flags |= FWUPD_TRUST_FLAG_PAYLOAD;
	return TRUE;
}

typedef enum {
	FU_MAIN_AUTH_KIND_UNKNOWN,
	FU_MAIN_AUTH_KIND_INSTALL,
	FU_MAIN_AUTH_KIND_UNLOCK,
	FU_MAIN_AUTH_KIND_LAST
} FuMainAuthKind;

typedef struct {
	GDBusMethodInvocation	*invocation;
	AsStore			*store;
	FwupdTrustFlags		 trust_flags;
	FuDevice		*device;
	FuProviderFlags		 flags;
	GBytes			*blob_fw;
	GBytes			*blob_cab;
	gint			 vercmp;
	FuMainAuthKind		 auth_kind;
	FuMainPrivate		*priv;
} FuMainAuthHelper;

/**
 * fu_main_helper_free:
 **/
static void
fu_main_helper_free (FuMainAuthHelper *helper)
{
	/* free */
	if (helper->device != NULL)
		g_object_unref (helper->device);
	if (helper->blob_fw > 0)
		g_bytes_unref (helper->blob_fw);
	if (helper->blob_cab > 0)
		g_bytes_unref (helper->blob_cab);
	if (helper->store != NULL)
		g_object_unref (helper->store);
	g_object_unref (helper->invocation);
	g_free (helper);
}

/**
 * fu_main_on_battery:
 **/
static gboolean
fu_main_on_battery (void)
{
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) value = NULL;

	proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
					       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
					       NULL,
					       "org.freedesktop.UPower",
					       "/org/freedesktop/UPower",
					       "org.freedesktop.UPower",
					       NULL,
					       &error);
	if (proxy == NULL) {
		g_warning ("Failed to conect UPower: %s", error->message);
		return FALSE;
	}
	value = g_dbus_proxy_get_cached_property (proxy, "OnBattery");
	if (value == NULL) {
		g_warning ("Failed to get OnBattery property value");
		return FALSE;
	}
	return g_variant_get_boolean (value);
}

/**
 * fu_main_provider_unlock_authenticated:
 **/
static gboolean
fu_main_provider_unlock_authenticated (FuMainAuthHelper *helper, GError **error)
{
	FuDeviceItem *item;

	/* check the device still exists */
	item = fu_main_get_item_by_id (helper->priv, fu_device_get_id (helper->device));
	if (item == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "device %s was removed",
			     fu_device_get_id (helper->device));
		return FALSE;
	}

	/* run the correct provider that added this */
	if (!fu_provider_unlock (item->provider,
				 item->device,
				 error))
		return FALSE;

	/* make the UI update */
	fu_main_emit_changed (helper->priv);
	return TRUE;
}

/**
 * fu_main_provider_update_authenticated:
 **/
static gboolean
fu_main_provider_update_authenticated (FuMainAuthHelper *helper, GError **error)
{
	FuDeviceItem *item;

	/* check the device still exists */
	item = fu_main_get_item_by_id (helper->priv, fu_device_get_id (helper->device));
	if (item == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "device %s was removed",
			     fu_device_get_id (helper->device));
		return FALSE;
	}

	/* can we only do this on AC power */
	if (fu_device_get_flags (item->device) & FU_DEVICE_FLAG_REQUIRE_AC) {
		if (fu_main_on_battery ()) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "Cannot install update "
					     "when not on AC power");
			return FALSE;
		}
	}

	/* run the correct provider that added this */
	if (!fu_provider_update (item->provider,
				 item->device,
				 helper->blob_cab,
				 helper->blob_fw,
				 helper->flags,
				 error))
		return FALSE;

	/* make the UI update */
	fu_device_set_modified (item->device, g_get_real_time () / G_USEC_PER_SEC);
	fu_main_emit_changed (helper->priv);
	return TRUE;
}

/**
 * fu_main_check_authorization_cb:
 **/
static void
fu_main_check_authorization_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	FuMainAuthHelper *helper = (FuMainAuthHelper *) user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(PolkitAuthorizationResult) auth = NULL;

	/* get result */
	auth = polkit_authority_check_authorization_finish (POLKIT_AUTHORITY (source),
							    res, &error);
	if (auth == NULL) {
		g_dbus_method_invocation_return_error (helper->invocation,
						       FWUPD_ERROR,
						       FWUPD_ERROR_AUTH_FAILED,
						       "could not check for auth: %s",
						       error->message);
		fu_main_helper_free (helper);
		return;
	}

	/* did not auth */
	if (!polkit_authorization_result_get_is_authorized (auth)) {
		g_dbus_method_invocation_return_error (helper->invocation,
						       FWUPD_ERROR,
						       FWUPD_ERROR_AUTH_FAILED,
						       "failed to obtain auth");
		fu_main_helper_free (helper);
		return;
	}

	/* we're good to go */
	if (helper->auth_kind == FU_MAIN_AUTH_KIND_INSTALL) {
		if (!fu_main_provider_update_authenticated (helper, &error)) {
			g_dbus_method_invocation_return_gerror (helper->invocation, error);
			fu_main_helper_free (helper);
			return;
		}
	} else if (helper->auth_kind == FU_MAIN_AUTH_KIND_UNLOCK) {
		if (!fu_main_provider_unlock_authenticated (helper, &error)) {
			g_dbus_method_invocation_return_gerror (helper->invocation, error);
			fu_main_helper_free (helper);
			return;
		}
	} else {
		g_assert_not_reached ();
	}

	/* success */
	g_dbus_method_invocation_return_value (helper->invocation, NULL);
	fu_main_helper_free (helper);
}

/**
 * fu_main_get_guids_from_store:
 **/
static gchar *
fu_main_get_guids_from_store (AsStore *store)
{
	AsApp *app;
	AsProvide *prov;
	GPtrArray *provides;
	GPtrArray *apps;
	GString *str = g_string_new ("");
	guint i;
	guint j;

	/* return a string with all the firmware apps in the store */
	apps = as_store_get_apps (store);
	for (i = 0; i < apps->len; i++) {
		app = AS_APP (g_ptr_array_index (apps, i));
		provides = as_app_get_provides (app);
		for (j = 0; j < provides->len; j++) {
			prov = AS_PROVIDE (g_ptr_array_index (provides, j));
			if (as_provide_get_kind (prov) != AS_PROVIDE_KIND_FIRMWARE_FLASHED)
				continue;
			g_string_append_printf (str, "%s,", as_provide_get_value (prov));
		}
	}
	if (str->len == 0)
		return NULL;
	g_string_truncate (str, str->len - 1);
	return g_string_free (str, FALSE);
}

/**
 * fu_main_vendor_quirk_release_version:
 **/
static void
fu_main_vendor_quirk_release_version (AsApp *app)
{
	AsVersionParseFlag flags = AS_VERSION_PARSE_FLAG_USE_TRIPLET;
	GPtrArray *releases;
	guint i;

	/* no quirk required */
	if (as_app_get_id_kind (app) != AS_ID_KIND_FIRMWARE)
		return;

        for (i = 0; quirk_table[i].identifier != NULL; i++)
		if (g_str_has_prefix (as_app_get_id(app), quirk_table[i].identifier))
			flags = quirk_table[i].flags;

	/* fix each release */
	releases = as_app_get_releases (app);
	for (i = 0; i < releases->len; i++) {
		AsRelease *rel;
		const gchar *version;
		guint64 ver_uint32;
		g_autofree gchar *version_new = NULL;

		rel = g_ptr_array_index (releases, i);
		version = as_release_get_version (rel);
		if (version == NULL)
			continue;
		if (g_strstr_len (version, -1, ".") != NULL)
			continue;

		/* metainfo files use hex and the LVFS uses decimal */
		if (g_str_has_prefix (version, "0x")) {
			ver_uint32 = g_ascii_strtoull (version + 2, NULL, 16);
		} else {
			ver_uint32 = g_ascii_strtoull (version, NULL, 10);
		}
		if (ver_uint32 == 0)
			continue;

		/* convert to dotted decimal */
		version_new = as_utils_version_from_uint32 (ver_uint32, flags);
		as_release_set_version (rel, version_new);
	}
}

/**
 * fu_main_update_helper:
 **/
static gboolean
fu_main_update_helper (FuMainAuthHelper *helper, GError **error)
{
	AsApp *app;
	AsChecksum *csum_tmp;
	AsRelease *rel;
	const gchar *tmp;
	const gchar *version;
	guint i;

	/* load store file which also decompresses firmware */
	fu_main_set_status (helper->priv, FWUPD_STATUS_DECOMPRESSING);
	if (!as_store_from_bytes (helper->store, helper->blob_cab, NULL, error))
		return FALSE;

	/* if we've not chosen a device, try and find anything in the
	 * cabinet 'store' that matches any installed device */
	if (helper->device == NULL) {
		for (i = 0; i < helper->priv->devices->len; i++) {
			FuDeviceItem *item;
			item = g_ptr_array_index (helper->priv->devices, i);
			app = as_store_get_app_by_provide (helper->store,
							   AS_PROVIDE_KIND_FIRMWARE_FLASHED,
							   fu_device_get_guid (item->device));
			if (app != NULL) {
				helper->device = g_object_ref (item->device);
				break;
			}
		}

		/* nothing found */
		if (helper->device == NULL) {
			g_autofree gchar *guid = NULL;
			guid = fu_main_get_guids_from_store (helper->store);
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no attached hardware matched %s",
				     guid);
			return FALSE;
		}
	} else {
		/* find an application from the cabinet 'store' for the
		 * chosen device */
		app = as_store_get_app_by_provide (helper->store,
						   AS_PROVIDE_KIND_FIRMWARE_FLASHED,
						   fu_device_get_guid (helper->device));
		if (app == NULL) {
			g_autofree gchar *guid = NULL;
			guid = fu_main_get_guids_from_store (helper->store);
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "firmware is not for this hw: required %s got %s",
				     fu_device_get_guid (helper->device), guid);
			return FALSE;
		}
	}

	/* parse the DriverVer */
	rel = as_app_get_release_default (app);
	if (rel == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no releases in the firmware component");
		return FALSE;
	}

	/* get the blob */
	csum_tmp = as_release_get_checksum_by_target (rel, AS_CHECKSUM_TARGET_CONTENT);
	tmp = as_checksum_get_filename (csum_tmp);
	g_assert (tmp != NULL);
	helper->blob_fw = as_release_get_blob (rel, tmp);
	if (helper->blob_fw == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "failed to get firmware blob");
		return FALSE;
	}

	/* possibly convert the version from 0x to dotted */
	fu_main_vendor_quirk_release_version (app);

	version = as_release_get_version (rel);
	fu_device_set_metadata (helper->device, FU_DEVICE_KEY_UPDATE_VERSION, version);

	/* compare to the lowest supported version, if it exists */
	tmp = fu_device_get_metadata (helper->device, FU_DEVICE_KEY_VERSION_LOWEST);
	if (tmp != NULL && as_utils_vercmp (tmp, version) > 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_VERSION_NEWER,
			     "Specified firmware is older than the minimum "
			     "required version '%s < %s'", tmp, version);
		return FALSE;
	}

	/* compare the versions of what we have installed */
	tmp = fu_device_get_metadata (helper->device, FU_DEVICE_KEY_VERSION);
	if (tmp == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Device %s does not yet have a current version",
			     fu_device_get_id (helper->device));
		return FALSE;
	}
	helper->vercmp = as_utils_vercmp (tmp, version);
	if (helper->vercmp == 0 && (helper->flags & FU_PROVIDER_UPDATE_FLAG_ALLOW_REINSTALL) == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_VERSION_SAME,
			     "Specified firmware is already installed '%s'",
			     tmp);
		return FALSE;
	}
	if (helper->vercmp > 0 && (helper->flags & FU_PROVIDER_UPDATE_FLAG_ALLOW_OLDER) == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_VERSION_NEWER,
			     "Specified firmware is older than installed '%s < %s'",
			     tmp, version);
		return FALSE;
	}

	/* verify */
	if (!fu_main_get_release_trust_flags (rel, &helper->trust_flags, error))
		return FALSE;
	return TRUE;
}

/**
 * fu_main_dbus_get_uid:
 *
 * Return value: the UID, or %G_MAXUINT if it could not be obtained
 **/
static guint
fu_main_dbus_get_uid (FuMainPrivate *priv, const gchar *sender)
{
	guint uid;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) value = NULL;

	if (priv->proxy_uid == NULL)
		return G_MAXUINT;
	value = g_dbus_proxy_call_sync (priv->proxy_uid,
					"GetConnectionUnixUser",
					g_variant_new ("(s)", sender),
					G_DBUS_CALL_FLAGS_NONE,
					-1,
					NULL,
					&error);
	if (value == NULL) {
		g_warning ("Failed to get uid for %s: %s",
			   sender, error->message);
		return G_MAXUINT;
	}
	g_variant_get (value, "(u)", &uid);
	return uid;
}

/**
 * fu_main_get_item_by_id_fallback_pending:
 **/
static FuDeviceItem *
fu_main_get_item_by_id_fallback_pending (FuMainPrivate *priv, const gchar *id, GError **error)
{
	FuDevice *dev;
	FuProvider *provider;
	FuDeviceItem *item = NULL;
	const gchar *tmp;
	guint i;
	g_autoptr(GPtrArray) devices = NULL;

	/* not a wildcard */
	if (g_strcmp0 (id, FWUPD_DEVICE_ID_ANY) != 0) {
		item = fu_main_get_item_by_id (priv, id);
		if (item == NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "no suitable device found for %s", id);
		}
		return item;
	}

	/* allow '*' for any */
	devices = fu_pending_get_devices (priv->pending, error);
	if (devices == NULL)
		return NULL;
	for (i = 0; i < devices->len; i++) {
		dev = g_ptr_array_index (devices, i);
		tmp = fu_device_get_metadata (dev, FU_DEVICE_KEY_PENDING_STATE);
		if (tmp == NULL)
			continue;
		if (g_strcmp0 (tmp, "scheduled") == 0)
			continue;

		/* if the device is not still connected, fake a FuDeviceItem */
		item = fu_main_get_item_by_id (priv, fu_device_get_id (dev));
		if (item == NULL) {
			tmp = fu_device_get_metadata (dev, FU_DEVICE_KEY_PROVIDER);
			provider = fu_main_get_provider_by_name (priv, tmp);
			if (provider == NULL) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_FOUND,
					     "no provider %s found", tmp);
			}
			item = g_new0 (FuDeviceItem, 1);
			item->device = g_object_ref (dev);
			item->provider = g_object_ref (provider);
			g_ptr_array_add (priv->devices, item);

			/* FIXME: just a boolean on FuDeviceItem? */
			fu_device_set_metadata (dev, "FakeDevice", "TRUE");
		}
		break;
	}

	/* no device found */
	if (item == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "no suitable devices found");
	}
	return item;
}

/**
 * fu_main_get_action_id_for_device:
 **/
static const gchar *
fu_main_get_action_id_for_device (FuMainAuthHelper *helper)
{
	gboolean is_trusted;
	gboolean is_downgrade;

	/* only test the payload */
	is_trusted = (helper->trust_flags & FWUPD_TRUST_FLAG_PAYLOAD) > 0;
	is_downgrade = helper->vercmp > 0;

	/* relax authentication checks for removable devices */
	if ((fu_device_get_flags (helper->device) & FU_DEVICE_FLAG_INTERNAL) == 0) {
		if (is_downgrade)
			return "org.freedesktop.fwupd.downgrade-hotplug";
		if (is_trusted)
			return "org.freedesktop.fwupd.update-hotplug-trusted";
		return "org.freedesktop.fwupd.update-hotplug";
	}

	/* internal device */
	if (is_downgrade)
		return "org.freedesktop.fwupd.downgrade-internal";
	if (is_trusted)
		return "org.freedesktop.fwupd.update-internal-trusted";
	return "org.freedesktop.fwupd.update-internal";
}

/**
 * fu_main_daemon_update_metadata:
 *
 * Supports optionally GZipped AppStream files up to 1MiB in size.
 **/
static gboolean
fu_main_daemon_update_metadata (FuMainPrivate *priv, gint fd, gint fd_sig, GError **error)
{
	const guint8 *data;
	guint i;
	gsize size;
	GPtrArray *apps;
	g_autofree gchar *xml = NULL;
	g_autoptr(AsStore) store = NULL;
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GBytes) bytes_raw = NULL;
	g_autoptr(GBytes) bytes_sig = NULL;
	g_autoptr(FuKeyring) kr = NULL;
	g_autoptr(GConverter) converter = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GInputStream) stream_buf = NULL;
	g_autoptr(GInputStream) stream_fd = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GInputStream) stream_sig = NULL;

	/* read the entire file into memory */
	stream_fd = g_unix_input_stream_new (fd, TRUE);
	bytes_raw = g_input_stream_read_bytes (stream_fd, 0x100000, NULL, error);
	if (bytes_raw == NULL)
		return FALSE;
	stream_buf = g_memory_input_stream_new ();
	g_memory_input_stream_add_bytes (G_MEMORY_INPUT_STREAM (stream_buf), bytes_raw);

	/* peek the file type and get data */
	data = g_bytes_get_data (bytes_raw, &size);
	if (size < 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "file is too small");
		return FALSE;
	}
	if (data[0] == 0x1f && data[1] == 0x8b) {
		g_debug ("using GZip decompressor for data");
		converter = G_CONVERTER (g_zlib_decompressor_new (G_ZLIB_COMPRESSOR_FORMAT_GZIP));
		stream = g_converter_input_stream_new (stream_buf, converter);
		bytes = g_input_stream_read_bytes (stream, 0x100000, NULL, error);
		if (bytes == NULL)
			return FALSE;
	} else if (data[0] == '<' && data[1] == '?') {
		g_debug ("using no decompressor for data");
		bytes = g_bytes_ref (bytes_raw);
	} else {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "file type '0x%02x,0x%02x' not supported",
			     data[0], data[1]);
		return FALSE;
	}

	/* read signature */
	stream_sig = g_unix_input_stream_new (fd_sig, TRUE);
	bytes_sig = g_input_stream_read_bytes (stream_sig, 0x800, NULL, error);
	if (bytes_sig == NULL)
		return FALSE;

	/* verify file */
	kr = fu_keyring_new ();
	if (!fu_keyring_add_public_keys (kr, "/etc/pki/fwupd-metadata", error))
		return FALSE;
	if (!fu_keyring_verify_data (kr, bytes_raw, bytes_sig, error))
		return FALSE;

	/* load the store locally until we know it is valid */
	store = as_store_new ();
	data = g_bytes_get_data (bytes, &size);
	xml = g_strndup ((const gchar *) data, size);
	if (!as_store_from_xml (store, xml, NULL, error))
		return FALSE;

	/* add the new application from the store */
	as_store_remove_all (priv->store);
	apps = as_store_get_apps (store);
	for (i = 0; i < apps->len; i++) {
		AsApp *app = g_ptr_array_index (apps, i);
		as_store_add_app (priv->store, app);
	}

	/* save the new file */
	as_store_set_api_version (priv->store, 0.9);
	file = g_file_new_for_path ("/var/cache/app-info/xmls/fwupd.xml");
	if (!as_store_to_file (priv->store, file,
			       AS_NODE_TO_XML_FLAG_ADD_HEADER |
			       AS_NODE_TO_XML_FLAG_FORMAT_MULTILINE |
			       AS_NODE_TO_XML_FLAG_FORMAT_INDENT,
			       NULL, error)) {
		return FALSE;
	}

	return TRUE;
}

/**
 * fu_main_store_delay_cb:
 **/
static gboolean
fu_main_store_delay_cb (gpointer user_data)
{
	AsApp *app;
	GPtrArray *apps;
	guint i;
	FuMainPrivate *priv = (FuMainPrivate *) user_data;

	apps = as_store_get_apps (priv->store);
	if (apps->len == 0) {
		g_debug ("no devices in store");
	} else {
		g_debug ("devices now in store:");
		for (i = 0; i < apps->len; i++) {
			app = g_ptr_array_index (apps, i);
			g_debug ("%i\t%s\t%s", i + 1,
				 as_app_get_id (app),
				 as_app_get_name (app, NULL));
		}
	}
	priv->store_changed_id = 0;
	return G_SOURCE_REMOVE;
}

/**
 * fu_main_store_changed_cb:
 **/
static void
fu_main_store_changed_cb (AsStore *store, FuMainPrivate *priv)
{
	if (priv->store_changed_id != 0)
		return;
	priv->store_changed_id = g_timeout_add (200, fu_main_store_delay_cb, priv);
}

/**
 * fu_main_get_updates:
 **/
static GPtrArray *
fu_main_get_updates (FuMainPrivate *priv, GError **error)
{
	AsApp *app;
	AsRelease *rel;
	FuDeviceItem *item;
	GPtrArray *updates;
	guint i;
	const gchar *tmp;

	/* find any updates using the AppStream metadata */
	updates = g_ptr_array_new ();
	for (i = 0; i < priv->devices->len; i++) {
		const gchar *version;
		AsChecksum *csum;

		item = g_ptr_array_index (priv->devices, i);

		/* get device version */
		version = fu_device_get_metadata (item->device, FU_DEVICE_KEY_VERSION);
		if (version == NULL)
			continue;

		/* match the GUID in the XML */
		app = as_store_get_app_by_provide (priv->store,
						   AS_PROVIDE_KIND_FIRMWARE_FLASHED,
						   fu_device_get_guid (item->device));
		if (app == NULL)
			continue;

		/* possibly convert the version from 0x to dotted */
		fu_main_vendor_quirk_release_version (app);

		/* get latest release */
		rel = as_app_get_release_default (app);
		if (rel == NULL) {
			g_debug ("%s has no firmware update metadata",
				 fu_device_get_id (item->device));
			continue;
		}

		/* check if actually newer than what we have installed */
		if (as_utils_vercmp (as_release_get_version (rel), version) <= 0) {
			g_debug ("%s has no firmware updates",
				 fu_device_get_id (item->device));
			continue;
		}

		/* add application metadata */
		fu_device_set_metadata (item->device,
					FU_DEVICE_KEY_APPSTREAM_ID,
					as_app_get_id (app));
		tmp = as_app_get_developer_name (app, NULL);
		if (tmp != NULL) {
			fu_device_set_metadata (item->device,
						FU_DEVICE_KEY_VENDOR, tmp);
		}
		tmp = as_app_get_name (app, NULL);
		if (tmp != NULL) {
			fu_device_set_metadata (item->device,
						FU_DEVICE_KEY_NAME, tmp);
		}
		tmp = as_app_get_comment (app, NULL);
		if (tmp != NULL) {
			fu_device_set_metadata (item->device,
						FU_DEVICE_KEY_SUMMARY, tmp);
		}
		tmp = as_app_get_description (app, NULL);
		if (tmp != NULL) {
			fu_device_set_metadata (item->device,
						FU_DEVICE_KEY_DESCRIPTION, tmp);
		}
		tmp = as_app_get_url_item (app, AS_URL_KIND_HOMEPAGE);
		if (tmp != NULL) {
			fu_device_set_metadata (item->device,
						FU_DEVICE_KEY_URL_HOMEPAGE, tmp);
		}
		tmp = as_app_get_project_license (app);
		if (tmp != NULL) {
			fu_device_set_metadata (item->device,
						FU_DEVICE_KEY_LICENSE, tmp);
		}

		/* add release information */
		tmp = as_release_get_version (rel);
		if (tmp != NULL) {
			fu_device_set_metadata (item->device,
						FU_DEVICE_KEY_UPDATE_VERSION, tmp);
		}
		csum = as_release_get_checksum_by_target (rel, AS_CHECKSUM_TARGET_CONTAINER);
		if (csum != NULL) {
			fu_device_set_metadata (item->device,
						FU_DEVICE_KEY_UPDATE_HASH,
						as_checksum_get_value (csum));
		}
		tmp = as_release_get_location_default (rel);
		if (tmp != NULL) {
			fu_device_set_metadata (item->device,
						FU_DEVICE_KEY_UPDATE_URI, tmp);
		}
		tmp = as_release_get_description (rel, NULL);
		if (tmp != NULL) {
			fu_device_set_metadata (item->device,
						FU_DEVICE_KEY_UPDATE_DESCRIPTION,
						tmp);
		}
		g_ptr_array_add (updates, item);
	}

	return updates;
}

/**
 * fu_main_daemon_method_call:
 **/
static void
fu_main_daemon_method_call (GDBusConnection *connection, const gchar *sender,
			    const gchar *object_path, const gchar *interface_name,
			    const gchar *method_name, GVariant *parameters,
			    GDBusMethodInvocation *invocation, gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	GVariant *val;

	/* return 'as' */
	if (g_strcmp0 (method_name, "GetDevices") == 0) {
		g_autoptr(GError) error = NULL;
		g_debug ("Called %s()", method_name);
		val = fu_main_device_array_to_variant (priv->devices, &error);
		if (val == NULL) {
			if (g_error_matches (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOTHING_TO_DO)) {
				g_prefix_error (&error, "No detected devices: ");
			}
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		g_dbus_method_invocation_return_value (invocation, val);
		fu_main_set_status (priv, FWUPD_STATUS_IDLE);
		return;
	}

	/* return 'as' */
	if (g_strcmp0 (method_name, "GetUpdates") == 0) {
		g_autoptr(GError) error = NULL;
		g_autoptr(GPtrArray) updates = NULL;
		g_debug ("Called %s()", method_name);
		updates = fu_main_get_updates (priv, &error);
		if (updates == NULL) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		val = fu_main_device_array_to_variant (updates, &error);
		if (val == NULL) {
			if (g_error_matches (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOTHING_TO_DO)) {
				g_prefix_error (&error, "No devices can be updated: ");
			}
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		g_dbus_method_invocation_return_value (invocation, val);
		fu_main_set_status (priv, FWUPD_STATUS_IDLE);
		return;
	}

	/* return '' */
	if (g_strcmp0 (method_name, "ClearResults") == 0) {
		FuDeviceItem *item = NULL;
		const gchar *id = NULL;
		g_autoptr(GError) error = NULL;

		g_variant_get (parameters, "(&s)", &id);
		g_debug ("Called %s(%s)", method_name, id);

		/* find device */
		item = fu_main_get_item_by_id_fallback_pending (priv, id, &error);
		if (item == NULL) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}

		/* call into the provider */
		if (!fu_provider_clear_results (item->provider, item->device, &error)) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}

		/* success */
		g_dbus_method_invocation_return_value (invocation, NULL);
		return;
	}

	/* return 'a{sv}' */
	if (g_strcmp0 (method_name, "GetResults") == 0) {
		FuDeviceItem *item = NULL;
		const gchar *id = NULL;
		g_autoptr(GError) error = NULL;

		g_variant_get (parameters, "(&s)", &id);
		g_debug ("Called %s(%s)", method_name, id);

		/* find device */
		item = fu_main_get_item_by_id_fallback_pending (priv, id, &error);
		if (item == NULL) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}

		/* call into the provider */
		if (!fu_provider_get_results (item->provider, item->device, &error)) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}

		/* success */
		val = fu_device_get_metadata_as_variant (item->device);
		g_dbus_method_invocation_return_value (invocation, val);
		return;
	}

	/* return '' */
	if (g_strcmp0 (method_name, "UpdateMetadata") == 0) {
		GDBusMessage *message;
		GUnixFDList *fd_list;
		gint fd_data;
		gint fd_sig;
		g_autoptr(GError) error = NULL;

		message = g_dbus_method_invocation_get_message (invocation);
		fd_list = g_dbus_message_get_unix_fd_list (message);
		if (fd_list == NULL || g_unix_fd_list_get_length (fd_list) != 2) {
			g_dbus_method_invocation_return_error (invocation,
							       FWUPD_ERROR,
							       FWUPD_ERROR_INTERNAL,
							       "invalid handle");
			return;
		}
		fd_data = g_unix_fd_list_get (fd_list, 0, &error);
		if (fd_data < 0) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		fd_sig = g_unix_fd_list_get (fd_list, 1, &error);
		if (fd_sig < 0) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		if (!fu_main_daemon_update_metadata (priv, fd_data, fd_sig, &error)) {
			g_prefix_error (&error, "failed to update metadata: ");
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		g_dbus_method_invocation_return_value (invocation, NULL);
		return;
	}

	/* return 's' */
	if (g_strcmp0 (method_name, "Unlock") == 0) {
		FuDeviceItem *item = NULL;
		FuMainAuthHelper *helper;
		const gchar *id = NULL;
		g_autoptr(PolkitSubject) subject = NULL;

		/* check the id exists */
		g_variant_get (parameters, "(&s)", &id);
		g_debug ("Called %s(%s)", method_name, id);
		item = fu_main_get_item_by_id (priv, id);
		if (item == NULL) {
			g_dbus_method_invocation_return_error (invocation,
							       FWUPD_ERROR,
							       FWUPD_ERROR_NOT_FOUND,
							       "No such device %s",
							       id);
			return;
		}

		/* check the device is locked */
		if ((fu_device_get_flags (item->device) & FU_DEVICE_FLAG_LOCKED) == 0) {
			g_dbus_method_invocation_return_error (invocation,
							       FWUPD_ERROR,
							       FWUPD_ERROR_NOT_FOUND,
							       "Device %s is not locked",
							       id);
			return;
		}

		/* process the firmware */
		helper = g_new0 (FuMainAuthHelper, 1);
		helper->auth_kind = FU_MAIN_AUTH_KIND_UNLOCK;
		helper->invocation = g_object_ref (invocation);
		helper->device = g_object_ref (item->device);
		helper->priv = priv;

		/* authenticate */
		subject = polkit_system_bus_name_new (sender);
		polkit_authority_check_authorization (helper->priv->authority, subject,
						      "org.freedesktop.fwupd.device-unlock",
						      NULL,
						      POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
						      NULL,
						      fu_main_check_authorization_cb,
						      helper);
		return;
	}

	/* return 's' */
	if (g_strcmp0 (method_name, "Verify") == 0) {
		AsApp *app;
		AsChecksum *csum;
		AsRelease *release;
		FuDeviceItem *item = NULL;
		const gchar *hash = NULL;
		const gchar *id = NULL;
		const gchar *version = NULL;
		g_autoptr(GError) error = NULL;

		/* check the id exists */
		g_variant_get (parameters, "(&s)", &id);
		g_debug ("Called %s(%s)", method_name, id);
		item = fu_main_get_item_by_id (priv, id);
		if (item == NULL) {
			g_dbus_method_invocation_return_error (invocation,
							       FWUPD_ERROR,
							       FWUPD_ERROR_NOT_FOUND,
							       "No such device %s",
							       id);
			return;
		}

		/* set the device firmware hash */
		if (!fu_provider_verify (item->provider, item->device,
					 FU_PROVIDER_VERIFY_FLAG_NONE, &error)) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}

		/* find component in metadata */
		app = as_store_get_app_by_provide (priv->store,
						   AS_PROVIDE_KIND_FIRMWARE_FLASHED,
						   fu_device_get_guid (item->device));
		if (app == NULL) {
			g_dbus_method_invocation_return_error (invocation,
							       FWUPD_ERROR,
							       FWUPD_ERROR_NOT_SUPPORTED,
							       "No metadata");
			return;
		}

		/* find version in metadata */
		version = fu_device_get_metadata (item->device, FU_DEVICE_KEY_VERSION);
		release = as_app_get_release (app, version);
		if (release == NULL) {
			g_dbus_method_invocation_return_error (invocation,
							       FWUPD_ERROR,
							       FWUPD_ERROR_NOT_SUPPORTED,
							       "No version %s",
							       version);
			return;
		}

		/* find checksum */
		csum = as_release_get_checksum_by_target (release, AS_CHECKSUM_TARGET_CONTENT);
		if (csum == NULL) {
			g_dbus_method_invocation_return_error (invocation,
							       FWUPD_ERROR,
							       FWUPD_ERROR_NOT_SUPPORTED,
							       "No content checksum for %s",
							       version);
			return;
		}
		hash = fu_device_get_metadata (item->device, FU_DEVICE_KEY_FIRMWARE_HASH);
		if (g_strcmp0 (as_checksum_get_value (csum), hash) != 0) {
			g_dbus_method_invocation_return_error (invocation,
							       FWUPD_ERROR,
							       FWUPD_ERROR_NOT_SUPPORTED,
							       "For v%s expected %s, got %s",
							       version,
							       as_checksum_get_value (csum),
							       hash);
			return;
		}
		g_dbus_method_invocation_return_value (invocation, NULL);
		return;
	}

	/* return '' */
	if (g_strcmp0 (method_name, "Install") == 0) {
		FuDeviceItem *item = NULL;
		FuMainAuthHelper *helper;
		FuProviderFlags flags = FU_PROVIDER_UPDATE_FLAG_NONE;
		GDBusMessage *message;
		GUnixFDList *fd_list;
		GVariant *prop_value;
		const gchar *action_id;
		const gchar *id = NULL;
		gchar *prop_key;
		gint32 fd_handle = 0;
		gint fd;
		g_autoptr(GError) error = NULL;
		g_autoptr(PolkitSubject) subject = NULL;
		g_autoptr(GVariantIter) iter = NULL;
		g_autoptr(GBytes) blob_cab = NULL;
		g_autoptr(GInputStream) stream = NULL;

		/* check the id exists */
		g_variant_get (parameters, "(&sha{sv})", &id, &fd_handle, &iter);
		g_debug ("Called %s(%s,%i)", method_name, id, fd_handle);
		if (g_strcmp0 (id, FWUPD_DEVICE_ID_ANY) != 0) {
			item = fu_main_get_item_by_id (priv, id);
			if (item == NULL) {
				g_dbus_method_invocation_return_error (invocation,
								       FWUPD_ERROR,
								       FWUPD_ERROR_NOT_FOUND,
								       "no such device %s",
								       id);
				return;
			}
		}

		/* get options */
		while (g_variant_iter_next (iter, "{&sv}",
					    &prop_key, &prop_value)) {
			g_debug ("got option %s", prop_key);
			if (g_strcmp0 (prop_key, "offline") == 0 &&
			    g_variant_get_boolean (prop_value) == TRUE)
				flags |= FU_PROVIDER_UPDATE_FLAG_OFFLINE;
			if (g_strcmp0 (prop_key, "allow-older") == 0 &&
			    g_variant_get_boolean (prop_value) == TRUE)
				flags |= FU_PROVIDER_UPDATE_FLAG_ALLOW_OLDER;
			if (g_strcmp0 (prop_key, "allow-reinstall") == 0 &&
			    g_variant_get_boolean (prop_value) == TRUE)
				flags |= FU_PROVIDER_UPDATE_FLAG_ALLOW_REINSTALL;
			g_variant_unref (prop_value);
		}

		/* get the fd */
		message = g_dbus_method_invocation_get_message (invocation);
		fd_list = g_dbus_message_get_unix_fd_list (message);
		if (fd_list == NULL || g_unix_fd_list_get_length (fd_list) != 1) {
			g_dbus_method_invocation_return_error (invocation,
							       FWUPD_ERROR,
							       FWUPD_ERROR_INTERNAL,
							       "invalid handle");
			return;
		}
		fd = g_unix_fd_list_get (fd_list, fd_handle, &error);
		if (fd < 0) {
			g_dbus_method_invocation_return_gerror (invocation,
								error);
			return;
		}

		/* read the entire fd to a data blob */
		stream = g_unix_input_stream_new (fd, TRUE);
		blob_cab = g_input_stream_read_bytes (stream,
						      FU_MAIN_FIRMWARE_SIZE_MAX,
						      NULL, &error);
		if (blob_cab == NULL){
			g_dbus_method_invocation_return_gerror (invocation,
								error);
			return;
		}

		/* process the firmware */
		helper = g_new0 (FuMainAuthHelper, 1);
		helper->auth_kind = FU_MAIN_AUTH_KIND_INSTALL;
		helper->invocation = g_object_ref (invocation);
		helper->trust_flags = FWUPD_TRUST_FLAG_NONE;
		helper->blob_cab = g_bytes_ref (blob_cab);
		helper->flags = flags;
		helper->priv = priv;
		helper->store = as_store_new ();
		if (item != NULL)
			helper->device = g_object_ref (item->device);
		if (!fu_main_update_helper (helper, &error)) {
			g_dbus_method_invocation_return_gerror (helper->invocation,
							        error);
			fu_main_set_status (priv, FWUPD_STATUS_IDLE);
			fu_main_helper_free (helper);
			return;
		}

		/* is root */
		if (fu_main_dbus_get_uid (priv, sender) == 0) {
			if (!fu_main_provider_update_authenticated (helper, &error)) {
				g_dbus_method_invocation_return_gerror (invocation, error);
			} else {
				g_dbus_method_invocation_return_value (invocation, NULL);
			}
			fu_main_set_status (priv, FWUPD_STATUS_IDLE);
			fu_main_helper_free (helper);
			return;
		}

		/* authenticate */
		action_id = fu_main_get_action_id_for_device (helper);
		subject = polkit_system_bus_name_new (sender);
		polkit_authority_check_authorization (helper->priv->authority, subject,
						      action_id,
						      NULL,
						      POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
						      NULL,
						      fu_main_check_authorization_cb,
						      helper);
		return;
	}

	/* return 'a{sv}' */
	if (g_strcmp0 (method_name, "GetDetails") == 0) {
		AsApp *app = NULL;
		AsRelease *rel;
		GDBusMessage *message;
		GPtrArray *apps;
		GPtrArray *provides;
		GUnixFDList *fd_list;
		GVariantBuilder builder;
		FwupdTrustFlags trust_flags = FWUPD_TRUST_FLAG_NONE;
		const gchar *tmp;
		const gchar *guid = NULL;
		gint32 fd_handle = 0;
		guint i;
		gint fd;
		g_autoptr(AsStore) store = NULL;
		g_autoptr(GBytes) blob_cab = NULL;
		g_autoptr(GError) error = NULL;
		g_autoptr(GInputStream) stream = NULL;

		/* check the id exists */
		g_variant_get (parameters, "(h)", &fd_handle);
		g_debug ("Called %s(%i)", method_name, fd_handle);

		/* get the fd */
		message = g_dbus_method_invocation_get_message (invocation);
		fd_list = g_dbus_message_get_unix_fd_list (message);
		if (fd_list == NULL || g_unix_fd_list_get_length (fd_list) != 1) {
			g_dbus_method_invocation_return_error (invocation,
							       FWUPD_ERROR,
							       FWUPD_ERROR_INTERNAL,
							       "invalid handle");
			return;
		}
		fd = g_unix_fd_list_get (fd_list, fd_handle, &error);
		if (fd < 0) {
			g_dbus_method_invocation_return_gerror (invocation,
								error);
			return;
		}

		/* read the entire fd to a data blob */
		stream = g_unix_input_stream_new (fd, TRUE);
		blob_cab = g_input_stream_read_bytes (stream,
						      FU_MAIN_FIRMWARE_SIZE_MAX,
						      NULL, &error);
		if (blob_cab == NULL){
			g_dbus_method_invocation_return_gerror (invocation,
								error);
			return;
		}

		/* load file */
		store = as_store_new ();
		if (!as_store_from_bytes (store, blob_cab, NULL, &error)) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}

		/* get default app */
		apps = as_store_get_apps (store);
		if (apps->len == 0) {
			g_dbus_method_invocation_return_error (invocation,
							       FWUPD_ERROR,
							       FWUPD_ERROR_INVALID_FILE,
							       "no components");
			return;
		}
		if (apps->len > 1) {
			/* we've got a .cab file with multiple components,
			 * so try to find the first thing that's installed */
			for (i = 0; i < priv->devices->len; i++) {
				FuDeviceItem *item;
				item = g_ptr_array_index (priv->devices, i);
				app = as_store_get_app_by_provide (store,
								   AS_PROVIDE_KIND_FIRMWARE_FLASHED,
								   fu_device_get_guid (item->device));
				if (app != NULL)
					break;
			}
		}

		/* well, we've tried our best, just show the first entry */
		if (app == NULL)
			app = AS_APP (g_ptr_array_index (apps, 0));

		/* get guid */
		provides = as_app_get_provides (app);
		for (i = 0; i < provides->len; i++) {
			AsProvide *prov = AS_PROVIDE (g_ptr_array_index (provides, i));
			if (as_provide_get_kind (prov) == AS_PROVIDE_KIND_FIRMWARE_FLASHED) {
				guid = as_provide_get_value (prov);
				break;
			}
		}
		if (guid == NULL) {
			g_dbus_method_invocation_return_error (invocation,
							       FWUPD_ERROR,
							       FWUPD_ERROR_INTERNAL,
							       "component has no GUID");
			return;
		}

		/* verify trust */
		rel = as_app_get_release_default (app);
		if (!fu_main_get_release_trust_flags (rel, &trust_flags, &error)) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}

		/* possibly convert the version from 0x to dotted */
		fu_main_vendor_quirk_release_version (app);

		/* create an array with all the metadata in */
		g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
		g_variant_builder_add (&builder, "{sv}",
				       FU_DEVICE_KEY_VERSION,
				       g_variant_new_string (as_release_get_version (rel)));
		g_variant_builder_add (&builder, "{sv}",
				       FU_DEVICE_KEY_GUID,
				       g_variant_new_string (guid));
		g_variant_builder_add (&builder, "{sv}",
				       FU_DEVICE_KEY_SIZE,
				       g_variant_new_uint64 (as_release_get_size (rel, AS_SIZE_KIND_INSTALLED)));

		/* optional properties */
		tmp = as_app_get_developer_name (app, NULL);
		if (tmp != NULL) {
			g_variant_builder_add (&builder, "{sv}",
					       FU_DEVICE_KEY_VENDOR,
					       g_variant_new_string (tmp));
		}
		tmp = as_app_get_name (app, NULL);
		if (tmp != NULL) {
			g_variant_builder_add (&builder, "{sv}",
					       FU_DEVICE_KEY_NAME,
					       g_variant_new_string (tmp));
		}
		tmp = as_app_get_comment (app, NULL);
		if (tmp != NULL) {
			g_variant_builder_add (&builder, "{sv}",
					       FU_DEVICE_KEY_SUMMARY,
					       g_variant_new_string (tmp));
		}
		tmp = as_app_get_description (app, NULL);
		if (tmp != NULL) {
			g_variant_builder_add (&builder, "{sv}",
					       FU_DEVICE_KEY_DESCRIPTION,
					       g_variant_new_string (tmp));
		}
		tmp = as_app_get_url_item (app, AS_URL_KIND_HOMEPAGE);
		if (tmp != NULL) {
			g_variant_builder_add (&builder, "{sv}",
					       FU_DEVICE_KEY_URL_HOMEPAGE,
					       g_variant_new_string (tmp));
		}
		tmp = as_app_get_project_license (app);
		if (tmp != NULL) {
			g_variant_builder_add (&builder, "{sv}",
					       FU_DEVICE_KEY_LICENSE,
					       g_variant_new_string (tmp));
		}
		tmp = as_release_get_description (rel, NULL);
		if (tmp != NULL) {
			g_variant_builder_add (&builder, "{sv}",
					       FU_DEVICE_KEY_UPDATE_DESCRIPTION,
					       g_variant_new_string (tmp));
		}
		g_variant_builder_add (&builder, "{sv}",
				       FU_DEVICE_KEY_TRUSTED,
				       g_variant_new_uint64 (trust_flags));

		/* return whole array */
		val = g_variant_new ("(a{sv})", &builder);
		g_dbus_method_invocation_return_value (invocation, val);
		return;
	}

	/* we suck */
	g_dbus_method_invocation_return_error (invocation,
					       G_DBUS_ERROR,
					       G_DBUS_ERROR_UNKNOWN_METHOD,
					       "no such method %s",
					       method_name);
}

/**
 * fu_main_daemon_get_property:
 **/
static GVariant *
fu_main_daemon_get_property (GDBusConnection *connection_, const gchar *sender,
			     const gchar *object_path, const gchar *interface_name,
			     const gchar *property_name, GError **error,
			     gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;

	if (g_strcmp0 (property_name, "DaemonVersion") == 0)
		return g_variant_new_string (VERSION);

	if (g_strcmp0 (property_name, "Status") == 0)
		return g_variant_new_string (fwupd_status_to_string (priv->status));

	/* return an error */
	g_set_error (error,
		     G_DBUS_ERROR,
		     G_DBUS_ERROR_UNKNOWN_PROPERTY,
		     "failed to get daemon property %s",
		     property_name);
	return NULL;
}

/**
 * fu_main_providers_coldplug:
 **/
static void
fu_main_providers_coldplug (FuMainPrivate *priv)
{
	FuProvider *provider;
	guint i;
	g_autoptr(AsProfileTask) ptask = NULL;

	ptask = as_profile_start_literal (priv->profile, "FuMain:coldplug");
	for (i = 0; i < priv->providers->len; i++) {
		g_autoptr(GError) error = NULL;
		g_autoptr(AsProfileTask) ptask2 = NULL;
		provider = g_ptr_array_index (priv->providers, i);
		ptask2 = as_profile_start (priv->profile,
					   "FuMain:coldplug{%s}",
					   fu_provider_get_name (provider));
		if (!fu_provider_coldplug (FU_PROVIDER (provider), &error))
			g_warning ("Failed to coldplug: %s", error->message);
	}
}

/**
 * fu_main_on_bus_acquired_cb:
 **/
static void
fu_main_on_bus_acquired_cb (GDBusConnection *connection,
			    const gchar *name,
			    gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	guint registration_id;
	g_autoptr(GError) error = NULL;
	static const GDBusInterfaceVTable interface_vtable = {
		fu_main_daemon_method_call,
		fu_main_daemon_get_property,
		NULL
	};

	priv->connection = g_object_ref (connection);
	registration_id = g_dbus_connection_register_object (connection,
							     FWUPD_DBUS_PATH,
							     priv->introspection_daemon->interfaces[0],
							     &interface_vtable,
							     priv,  /* user_data */
							     NULL,  /* user_data_free_func */
							     NULL); /* GError** */
	g_assert (registration_id > 0);

	/* add devices */
	fu_main_providers_coldplug (priv);

	/* connect to D-Bus directly */
	priv->proxy_uid =
		g_dbus_proxy_new_sync (priv->connection,
				       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
				       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
				       NULL,
				       "org.freedesktop.DBus",
				       "/org/freedesktop/DBus",
				       "org.freedesktop.DBus",
				       NULL,
				       &error);
	if (priv->proxy_uid == NULL) {
		g_warning ("cannot connect to DBus: %s", error->message);
		return;
	}

	/* dump startup profile data */
	if (fu_debug_is_verbose ())
		as_profile_dump (priv->profile);
}

/**
 * fu_main_on_name_acquired_cb:
 **/
static void
fu_main_on_name_acquired_cb (GDBusConnection *connection,
			     const gchar *name,
			     gpointer user_data)
{
	g_debug ("FuMain: acquired name: %s", name);
}

/**
 * fu_main_on_name_lost_cb:
 **/
static void
fu_main_on_name_lost_cb (GDBusConnection *connection,
			 const gchar *name,
			 gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	g_debug ("FuMain: lost name: %s", name);
	g_main_loop_quit (priv->loop);
}

/**
 * fu_main_timed_exit_cb:
 **/
static gboolean
fu_main_timed_exit_cb (gpointer user_data)
{
	GMainLoop *loop = (GMainLoop *) user_data;
	g_main_loop_quit (loop);
	return G_SOURCE_REMOVE;
}

/**
 * fu_main_load_introspection:
 **/
static GDBusNodeInfo *
fu_main_load_introspection (const gchar *filename, GError **error)
{
	g_autoptr(GBytes) data = NULL;
	g_autofree gchar *path = NULL;

	/* lookup data */
	path = g_build_filename ("/org/freedesktop/fwupd", filename, NULL);
	data = g_resource_lookup_data (fu_get_resource (),
				       path,
				       G_RESOURCE_LOOKUP_FLAGS_NONE,
				       error);
	if (data == NULL)
		return NULL;

	/* build introspection from XML */
	return g_dbus_node_info_new_for_xml (g_bytes_get_data (data, NULL), error);
}

/**
 * cd_main_provider_device_added_cb:
 **/
static void
cd_main_provider_device_added_cb (FuProvider *provider,
				  FuDevice *device,
				  gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	FuDeviceItem *item;

	/* remove any fake device */
	item = fu_main_get_item_by_id (priv, fu_device_get_id (device));
	if (item != NULL) {
		g_debug ("already added %s by %s, ignoring same device from %s",
			 fu_device_get_id (item->device),
			 fu_device_get_metadata (item->device, FU_DEVICE_KEY_PROVIDER),
			 fu_provider_get_name (provider));
		return;
	}

	/* create new device */
	item = g_new0 (FuDeviceItem, 1);
	item->device = g_object_ref (device);
	item->provider = g_object_ref (provider);
	g_ptr_array_add (priv->devices, item);
	fu_main_emit_changed (priv);
}

/**
 * cd_main_provider_device_removed_cb:
 **/
static void
cd_main_provider_device_removed_cb (FuProvider *provider,
				    FuDevice *device,
				    gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	FuDeviceItem *item;

	item = fu_main_get_item_by_id (priv, fu_device_get_id (device));
	if (item == NULL) {
		g_debug ("no device to remove %s", fu_device_get_id (device));
		return;
	}

	/* check this came from the same provider */
	if (g_strcmp0 (fu_provider_get_name (provider),
		       fu_provider_get_name (item->provider)) != 0) {
		g_debug ("ignoring duplicate removal from %s",
			 fu_provider_get_name (provider));
		return;
	}

	g_ptr_array_remove (priv->devices, item);
	fu_main_emit_changed (priv);
}

/**
 * cd_main_provider_status_changed_cb:
 **/
static void
cd_main_provider_status_changed_cb (FuProvider *provider,
				    FwupdStatus status,
				    gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	fu_main_set_status (priv, status);
}

/**
 * fu_main_add_provider:
 **/
static void
fu_main_add_provider (FuMainPrivate *priv, FuProvider *provider)
{
	g_signal_connect (provider, "device-added",
			  G_CALLBACK (cd_main_provider_device_added_cb),
			  priv);
	g_signal_connect (provider, "device-removed",
			  G_CALLBACK (cd_main_provider_device_removed_cb),
			  priv);
	g_signal_connect (provider, "status-changed",
			  G_CALLBACK (cd_main_provider_status_changed_cb),
			  priv);
	g_ptr_array_add (priv->providers, provider);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	FuMainPrivate *priv = NULL;
	gboolean immediate_exit = FALSE;
	gboolean ret;
	gboolean timed_exit = FALSE;
	GOptionContext *context;
	guint owner_id = 0;
	guint retval = 1;
	const GOptionEntry options[] = {
		{ "timed-exit", '\0', 0, G_OPTION_ARG_NONE, &timed_exit,
		  /* TRANSLATORS: exit after we've started up, used for user profiling */
		  _("Exit after a small delay"), NULL },
		{ "immediate-exit", '\0', 0, G_OPTION_ARG_NONE, &immediate_exit,
		  /* TRANSLATORS: exit straight away, used for automatic profiling */
		  _("Exit after the engine has loaded"), NULL },
		{ NULL}
	};
	g_autoptr(GError) error = NULL;
	g_autofree gchar *config_file = NULL;
	g_autoptr(GKeyFile) config = NULL;

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* TRANSLATORS: program name */
	g_set_application_name (_("Firmware Update Daemon"));
	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, fu_debug_get_option_group ());
	/* TRANSLATORS: program summary */
	g_option_context_set_summary (context, _("Firmware Update D-Bus Service"));
	ret = g_option_context_parse (context, &argc, &argv, &error);
	if (!ret) {
		g_warning ("FuMain: failed to parse command line arguments: %s",
			   error->message);
		goto out;
	}

	/* create new objects */
	priv = g_new0 (FuMainPrivate, 1);
	priv->status = FWUPD_STATUS_IDLE;
	priv->devices = g_ptr_array_new_with_free_func ((GDestroyNotify) fu_main_item_free);
	priv->loop = g_main_loop_new (NULL, FALSE);
	priv->pending = fu_pending_new ();
	priv->store = as_store_new ();
	priv->profile = as_profile_new ();
	g_signal_connect (priv->store, "changed",
			  G_CALLBACK (fu_main_store_changed_cb), priv);
	as_store_set_watch_flags (priv->store, AS_STORE_WATCH_FLAG_ADDED |
					       AS_STORE_WATCH_FLAG_REMOVED);

	/* load AppStream */
	as_store_add_filter (priv->store, AS_ID_KIND_FIRMWARE);
	if (!as_store_load (priv->store,
			    AS_STORE_LOAD_FLAG_APP_INFO_SYSTEM,
			    NULL, &error)){
		g_warning ("FuMain: failed to load AppStream data: %s",
			   error->message);
		return FALSE;
	}

	/* read config file */
	config = g_key_file_new ();
	config_file = g_build_filename (SYSCONFDIR, "fwupd.conf", NULL);
	g_debug ("Loading fallback values from %s", config_file);
	if (!g_key_file_load_from_file (config, config_file,
					G_KEY_FILE_NONE, &error)) {
		g_print ("failed to load config file %s: %s\n",
			  config_file, error->message);
		retval = EXIT_FAILURE;
		goto out;
	}

	/* add providers */
	priv->providers = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	if (g_key_file_get_boolean (config, "fwupd", "EnableOptionROM", NULL))
		fu_main_add_provider (priv, fu_provider_udev_new ());
	fu_main_add_provider (priv, fu_provider_dfu_new ());
	fu_main_add_provider (priv, fu_provider_rpi_new ());
#ifdef HAVE_COLORHUG
	fu_main_add_provider (priv, fu_provider_chug_new ());
#endif
#ifdef HAVE_UEFI
	fu_main_add_provider (priv, fu_provider_uefi_new ());
#endif

	/* last as least priority */
	fu_main_add_provider (priv, fu_provider_usb_new ());

	/* load introspection from file */
	priv->introspection_daemon = fu_main_load_introspection (FWUPD_DBUS_INTERFACE ".xml",
								 &error);
	if (priv->introspection_daemon == NULL) {
		g_warning ("FuMain: failed to load daemon introspection: %s",
			   error->message);
		goto out;
	}

	/* get authority */
	priv->authority = polkit_authority_get_sync (NULL, &error);
	if (priv->authority == NULL) {
		g_warning ("FuMain: failed to load polkit authority: %s",
			   error->message);
		goto out;
	}

	/* own the object */
	owner_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
				   FWUPD_DBUS_SERVICE,
				   G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
				    G_BUS_NAME_OWNER_FLAGS_REPLACE,
				   fu_main_on_bus_acquired_cb,
				   fu_main_on_name_acquired_cb,
				   fu_main_on_name_lost_cb,
				   priv, NULL);

	/* Only timeout and close the mainloop if we have specified it
	 * on the command line */
	if (immediate_exit)
		g_idle_add (fu_main_timed_exit_cb, priv->loop);
	else if (timed_exit)
		g_timeout_add_seconds (5, fu_main_timed_exit_cb, priv->loop);

	/* wait */
	g_info ("Daemon ready for requests");
	g_main_loop_run (priv->loop);

	/* success */
	retval = 0;
out:
	g_option_context_free (context);
	if (owner_id > 0)
		g_bus_unown_name (owner_id);
	if (priv != NULL) {
		if (priv->loop != NULL)
			g_main_loop_unref (priv->loop);
		if (priv->proxy_uid != NULL)
			g_object_unref (priv->proxy_uid);
		if (priv->connection != NULL)
			g_object_unref (priv->connection);
		if (priv->authority != NULL)
			g_object_unref (priv->authority);
		if (priv->profile != NULL)
			g_object_unref (priv->profile);
		if (priv->store != NULL)
			g_object_unref (priv->store);
		if (priv->introspection_daemon != NULL)
			g_dbus_node_info_unref (priv->introspection_daemon);
		if (priv->store_changed_id != 0)
			g_source_remove (priv->store_changed_id);
		g_object_unref (priv->pending);
		if (priv->providers != NULL)
			g_ptr_array_unref (priv->providers);
		g_ptr_array_unref (priv->devices);
		g_free (priv);
	}
	return retval;
}

