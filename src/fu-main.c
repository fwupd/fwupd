/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
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

#include "fwupd-enums-private.h"
#include "fwupd-resources.h"

#include "fu-debug.h"
#include "fu-device.h"
#include "fu-plugin-private.h"
#include "fu-keyring.h"
#include "fu-pending.h"
#include "fu-plugin.h"
#include "fu-quirks.h"

#ifndef HAVE_POLKIT_0_114
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PolkitAuthorizationResult, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PolkitSubject, g_object_unref)
#endif

#define FU_MAIN_FIRMWARE_SIZE_MAX	(32 * 1024 * 1024)	/* bytes */

typedef struct {
	GDBusConnection		*connection;
	GDBusNodeInfo		*introspection_daemon;
	GDBusProxy		*proxy_uid;
	GUsbContext		*usb_ctx;
	GKeyFile		*config;
	GMainLoop		*loop;
	GPtrArray		*devices;	/* of FuDeviceItem */
	PolkitAuthority		*authority;
	FwupdStatus		 status;
	guint			 percentage;
	FuPending		*pending;
	AsProfile		*profile;
	AsStore			*store;
	guint			 store_changed_id;
	guint			 owner_id;
	gboolean		 coldplug_running;
	guint			 coldplug_id;
	guint			 coldplug_delay;
	GPtrArray		*plugins;	/* of FuPlugin */
	GHashTable		*plugins_hash;	/* of name : FuPlugin */
} FuMainPrivate;

typedef struct {
	FuDevice		*device;
	FuPlugin		*plugin;
} FuDeviceItem;

static gboolean fu_main_get_updates_item_update (FuMainPrivate *priv, FuDeviceItem *item);

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

static void
fu_main_emit_device_added (FuMainPrivate *priv, FuDevice *device)
{
	GVariant *val;

	/* not yet connected */
	if (priv->connection == NULL)
		return;
	val = fwupd_result_to_data (FWUPD_RESULT (device), "(a{sv})");
	g_dbus_connection_emit_signal (priv->connection,
				       NULL,
				       FWUPD_DBUS_PATH,
				       FWUPD_DBUS_INTERFACE,
				       "DeviceAdded",
				       val, NULL);
}

static void
fu_main_emit_device_removed (FuMainPrivate *priv, FuDevice *device)
{
	GVariant *val;

	/* not yet connected */
	if (priv->connection == NULL)
		return;
	val = fwupd_result_to_data (FWUPD_RESULT (device), "(a{sv})");
	g_dbus_connection_emit_signal (priv->connection,
				       NULL,
				       FWUPD_DBUS_PATH,
				       FWUPD_DBUS_INTERFACE,
				       "DeviceRemoved",
				       val, NULL);
}

static void
fu_main_emit_device_changed (FuMainPrivate *priv, FuDevice *device)
{
	GVariant *val;

	/* not yet connected */
	if (priv->connection == NULL)
		return;
	val = fwupd_result_to_data (FWUPD_RESULT (device), "(a{sv})");
	g_dbus_connection_emit_signal (priv->connection,
				       NULL,
				       FWUPD_DBUS_PATH,
				       FWUPD_DBUS_INTERFACE,
				       "DeviceChanged",
				       val, NULL);
}

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

static void
fu_main_set_percentage (FuMainPrivate *priv, guint percentage)
{
	if (priv->percentage == percentage)
		return;
	priv->percentage = percentage;

	/* emit changed */
	g_debug ("Emitting PropertyChanged('Percentage'='%u%%')", percentage);
	fu_main_emit_property_changed (priv, "Percentage",
				       g_variant_new_uint32 (percentage));
}

static GVariant *
fu_main_device_array_to_variant (GPtrArray *devices, GError **error)
{
	GVariantBuilder builder;

	/* no devices */
	if (devices->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO,
				     "Nothing to do");
		return NULL;
	}

	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	for (guint i = 0; i < devices->len; i++) {
		GVariant *tmp;
		FuDeviceItem *item;
		item = g_ptr_array_index (devices, i);
		tmp = fwupd_result_to_data (FWUPD_RESULT (item->device), "{sa{sv}}");
		g_variant_builder_add_value (&builder, tmp);
	}
	return g_variant_new ("(a{sa{sv}})", &builder);
}

static void
fu_main_invocation_return_value (FuMainPrivate *priv,
				 GDBusMethodInvocation *invocation,
				 GVariant *parameters)
{
	fu_main_set_status (priv, FWUPD_STATUS_IDLE);
	g_dbus_method_invocation_return_value (invocation, parameters);
}

static void
fu_main_invocation_return_error (FuMainPrivate *priv,
				 GDBusMethodInvocation *invocation,
				 const GError *error)
{
	fu_main_set_status (priv, FWUPD_STATUS_IDLE);
	g_dbus_method_invocation_return_gerror (invocation, error);
}

static void
fu_main_item_free (FuDeviceItem *item)
{
	g_object_unref (item->device);
	g_object_unref (item->plugin);
	g_free (item);
}

static FuDeviceItem *
fu_main_get_item_by_id (FuMainPrivate *priv, const gchar *id)
{
	for (guint i = 0; i < priv->devices->len; i++) {
		FuDeviceItem *item = g_ptr_array_index (priv->devices, i);
		if (g_strcmp0 (fu_device_get_id (item->device), id) == 0)
			return item;
		if (g_strcmp0 (fu_device_get_equivalent_id (item->device), id) == 0)
			return item;
	}
	return NULL;
}

static FuDeviceItem *
fu_main_get_item_by_guid (FuMainPrivate *priv, const gchar *guid)
{
	for (guint i = 0; i < priv->devices->len; i++) {
		FuDeviceItem *item = g_ptr_array_index (priv->devices, i);
		if (fu_device_has_guid (item->device, guid))
			return item;
	}
	return NULL;
}

static FuPlugin *
fu_main_get_plugin_by_name (FuMainPrivate *priv, const gchar *name)
{
	for (guint i = 0; i < priv->plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index (priv->plugins, i);
		if (g_strcmp0 (fu_plugin_get_name (plugin), name) == 0)
			return plugin;
	}
	return NULL;
}

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
	FU_MAIN_AUTH_KIND_VERIFY_UPDATE,
	FU_MAIN_AUTH_KIND_LAST
} FuMainAuthKind;

typedef struct {
	GDBusMethodInvocation	*invocation;
	AsStore			*store;
	FwupdTrustFlags		 trust_flags;
	GPtrArray		*devices;	/* of FuDevice */
	GPtrArray		*blob_fws;	/* of GBytes */
	FwupdInstallFlags	 flags;
	GBytes			*blob_cab;
	gboolean		 is_downgrade;
	FuMainAuthKind		 auth_kind;
	FuMainPrivate		*priv;
} FuMainAuthHelper;

static void
fu_main_helper_free (FuMainAuthHelper *helper)
{
	/* free */
	if (helper->devices != NULL)
		g_ptr_array_unref (helper->devices);
	if (helper->blob_fws != NULL)
		g_ptr_array_unref (helper->blob_fws);
	if (helper->blob_cab != NULL)
		g_bytes_unref (helper->blob_cab);
	if (helper->store != NULL)
		g_object_unref (helper->store);
	g_object_unref (helper->invocation);
	g_free (helper);
}

static gboolean
fu_main_plugin_unlock_authenticated (FuMainAuthHelper *helper, GError **error)
{
	/* check the devices still exists */
	for (guint i = 0; i < helper->devices->len; i ++) {
		FuDeviceItem *item;
		FuDevice *device = g_ptr_array_index (helper->devices, i);

		item = fu_main_get_item_by_id (helper->priv,
					       fu_device_get_id (device));
		if (item == NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "device %s was removed",
				     fu_device_get_id (device));
			return FALSE;
		}

		/* run the correct plugin that added this */
		if (!fu_plugin_runner_unlock (item->plugin,
					      item->device,
					      error))
			return FALSE;

		/* make the UI update */
		fu_main_emit_device_changed (helper->priv, item->device);
	}

	/* make the UI update */
	fu_main_emit_changed (helper->priv);

	return TRUE;
}

static AsApp *
fu_main_verify_update_device_to_app (FuDevice *device)
{
	AsApp *app = NULL;
	g_autofree gchar *id = NULL;
	g_autoptr(AsChecksum) csum = NULL;
	g_autoptr(AsProvide) prov = NULL;
	g_autoptr(AsRelease) rel = NULL;

	/* make a plausible ID */
	id = g_strdup_printf ("%s.firmware", fu_device_get_guid_default (device));

	/* add app to store */
	app = as_app_new ();
	as_app_set_id (app, id);
	as_app_set_kind (app, AS_APP_KIND_FIRMWARE);
	rel = as_release_new ();
	as_release_set_version (rel, fu_device_get_version (device));
	csum = as_checksum_new ();
	as_checksum_set_kind (csum, G_CHECKSUM_SHA1);
	as_checksum_set_value (csum, fu_device_get_checksum (device));
	as_checksum_set_target (csum, AS_CHECKSUM_TARGET_CONTENT);
	as_release_add_checksum (rel, csum);
	as_app_add_release (app, rel);
	prov = as_provide_new ();
	as_provide_set_kind (prov, AS_PROVIDE_KIND_FIRMWARE_FLASHED);
	as_provide_set_value (prov, fu_device_get_guid_default (device));
	as_app_add_provide (app, prov);
	return app;
}

static gboolean
fu_main_plugin_verify_update_authenticated (FuMainAuthHelper *helper, GError **error)
{
	const gchar *fn = "/var/cache/app-info/xmls/fwupd-verify.xml";
	g_autoptr(AsStore) store = NULL;
	g_autoptr(GFile) xml_file = NULL;

	/* load existing store */
	store = as_store_new ();
	as_store_set_api_version (store, 0.9);
	xml_file = g_file_new_for_path (fn);
	if (g_file_query_exists (xml_file, NULL)) {
		if (!as_store_from_file (store, xml_file, NULL, NULL, error))
			return FALSE;
	}

	/* check the devices still exists */
	for (guint i = 0; i < helper->devices->len; i ++) {
		FuDevice *device = g_ptr_array_index (helper->devices, i);
		FuDeviceItem *item;
		g_autoptr(AsApp) app = NULL;

		item = fu_main_get_item_by_id (helper->priv,
					       fu_device_get_id (device));
		if (item == NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "device %s was removed",
				     fu_device_get_id (device));
			return FALSE;
		}

		/* unlock device if required */
		if (fu_device_has_flag (item->device, FWUPD_DEVICE_FLAG_LOCKED)) {
			if (!fu_plugin_runner_unlock (item->plugin,
						      item->device,
						      error))
				return FALSE;
			fu_main_emit_device_changed (helper->priv, item->device);
		}

		/* get the checksum */
		if (fu_device_get_checksum (item->device) == NULL) {
			if (!fu_plugin_runner_verify (item->plugin,
						      item->device,
						      FU_PLUGIN_VERIFY_FLAG_NONE,
						      error))
				return FALSE;
			fu_main_emit_device_changed (helper->priv, item->device);
		}

		/* we got nothing */
		if (fu_device_get_checksum (item->device) == NULL) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "device verification not supported");
			return FALSE;
		}

		/* add to store */
		app = fu_main_verify_update_device_to_app (item->device);
		as_store_add_app (store, app);
	}

	/* write */
	g_debug ("writing %s", fn);
	return as_store_to_file (store, xml_file,
				 AS_NODE_TO_XML_FLAG_ADD_HEADER |
				 AS_NODE_TO_XML_FLAG_FORMAT_INDENT |
				 AS_NODE_TO_XML_FLAG_FORMAT_MULTILINE,
				 NULL, error);
}

static gboolean
fu_main_plugin_update_authenticated (FuMainAuthHelper *helper, GError **error)
{
	FuMainPrivate *priv = helper->priv;
	FuDeviceItem *item;

	/* check the devices still exists */
	for (guint i = 0; i < helper->devices->len; i ++) {
		FuDevice *device = g_ptr_array_index (helper->devices, i);
		item = fu_main_get_item_by_id (helper->priv,
					       fu_device_get_id (device));
		if (item == NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "device %s was removed",
				     fu_device_get_id (device));
			return FALSE;
		}

		/* Called with online update, test if device is supposed to allow this */
		if (!(helper->flags & FWUPD_INSTALL_FLAG_OFFLINE) &&
		    !fu_device_has_flag (item->device, FWUPD_DEVICE_FLAG_ALLOW_ONLINE)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Device %s does not allow online updates",
				    fu_device_get_id (device));
			return FALSE;
		}
		/* Called with offline update, test if device is supposed to allow this */
		if (helper->flags & FWUPD_INSTALL_FLAG_OFFLINE &&
		    !fu_device_has_flag (item->device, FWUPD_DEVICE_FLAG_ALLOW_OFFLINE)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Device %s does not allow offline updates",
				    fu_device_get_id (device));
			return FALSE;
		}
	}

	/* run the correct plugin for each device */
	for (guint i = 0; i < helper->devices->len; i ++) {
		FuDevice *device = g_ptr_array_index (helper->devices, i);
		GBytes *blob_fw = g_ptr_array_index (helper->blob_fws, i);
		item = fu_main_get_item_by_id (helper->priv,
					       fu_device_get_id (device));

		/* signal to all the plugins the update is about to happen */
		for (guint j = 0; j < priv->plugins->len; j++) {
			FuPlugin *plugin = g_ptr_array_index (priv->plugins, j);
			if (!fu_plugin_runner_update_prepare (plugin, device, error))
				return FALSE;
		}

		/* do the update */
		if (!fu_plugin_runner_update (item->plugin,
					      item->device,
					      helper->blob_cab,
					      blob_fw,
					      helper->flags,
					      error)) {
			for (guint j = 0; j < priv->plugins->len; j++) {
				FuPlugin *plugin = g_ptr_array_index (priv->plugins, j);
				g_autoptr(GError) error_local = NULL;
				if (!fu_plugin_runner_update_cleanup (plugin,
								      device,
								      &error_local)) {
					g_warning ("failed to update-cleanup "
						   "after failed update: %s",
						   error_local->message);
				}
			}
			return FALSE;
		}

		/* signal to all the plugins the update has happened */
		for (guint j = 0; j < priv->plugins->len; j++) {
			FuPlugin *plugin = g_ptr_array_index (priv->plugins, j);
			g_autoptr(GError) error_local = NULL;
			if (!fu_plugin_runner_update_cleanup (plugin, device, &error_local)) {
				g_warning ("failed to update-cleanup: %s",
					   error_local->message);
			}
		}

		/* make the UI update */
		fu_device_set_modified (item->device, (guint64) g_get_real_time () / G_USEC_PER_SEC);
		fu_main_emit_device_changed (helper->priv, item->device);
	}

	/* make the UI update */
	fu_main_emit_changed (helper->priv);
	return TRUE;
}

static void
fu_main_check_authorization_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	FuMainAuthHelper *helper = (FuMainAuthHelper *) user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(PolkitAuthorizationResult) auth = NULL;

	/* get result */
	auth = polkit_authority_check_authorization_finish (POLKIT_AUTHORITY (source),
							    res, &error_local);
	if (auth == NULL) {
		g_set_error (&error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_AUTH_FAILED,
			     "could not check for auth: %s",
			     error_local->message);
		fu_main_invocation_return_error (helper->priv, helper->invocation, error);
		fu_main_helper_free (helper);
		return;
	}

	/* did not auth */
	if (!polkit_authorization_result_get_is_authorized (auth)) {
		g_set_error_literal (&error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_AUTH_FAILED,
				     "failed to obtain auth");
		fu_main_invocation_return_error (helper->priv, helper->invocation, error);
		fu_main_helper_free (helper);
		return;
	}

	/* we're good to go */
	if (helper->auth_kind == FU_MAIN_AUTH_KIND_INSTALL) {
		if (!fu_main_plugin_update_authenticated (helper, &error)) {
			fu_main_invocation_return_error (helper->priv,
							 helper->invocation,
							 error);
			fu_main_helper_free (helper);
			return;
		}
	} else if (helper->auth_kind == FU_MAIN_AUTH_KIND_UNLOCK) {
		if (!fu_main_plugin_unlock_authenticated (helper, &error)) {
			fu_main_invocation_return_error (helper->priv,
							 helper->invocation,
							 error);
			fu_main_helper_free (helper);
			return;
		}
	} else if (helper->auth_kind == FU_MAIN_AUTH_KIND_VERIFY_UPDATE) {
		if (!fu_main_plugin_verify_update_authenticated (helper, &error)) {
			fu_main_invocation_return_error (helper->priv,
							 helper->invocation,
							 error);
			fu_main_helper_free (helper);
			return;
		}
	} else {
		g_assert_not_reached ();
	}

	/* success */
	fu_main_invocation_return_value (helper->priv, helper->invocation, NULL);
	fu_main_helper_free (helper);
}

static gchar *
fu_main_get_guids_from_store (AsStore *store)
{
	AsProvide *prov;
	GPtrArray *provides;
	GPtrArray *apps;
	GString *str = g_string_new ("");

	/* return a string with all the firmware apps in the store */
	apps = as_store_get_apps (store);
	for (guint i = 0; i < apps->len; i++) {
		AsApp *app = AS_APP (g_ptr_array_index (apps, i));
		provides = as_app_get_provides (app);
		for (guint j = 0; j < provides->len; j++) {
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

static void
fu_main_vendor_quirk_release_version (AsApp *app)
{
	AsVersionParseFlag flags = AS_VERSION_PARSE_FLAG_USE_TRIPLET;
	GPtrArray *releases;

	/* no quirk required */
	if (as_app_get_kind (app) != AS_APP_KIND_FIRMWARE)
		return;

	for (guint i = 0; quirk_table[i].identifier != NULL; i++) {
		if (g_str_has_prefix (as_app_get_id(app), quirk_table[i].identifier))
			flags = quirk_table[i].flags;
	}

	/* fix each release */
	releases = as_app_get_releases (app);
	for (guint i = 0; i < releases->len; i++) {
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
		version_new = as_utils_version_from_uint32 ((guint32) ver_uint32, flags);
		as_release_set_version (rel, version_new);
	}
}

#if AS_CHECK_VERSION(0,6,7)
static gboolean
fu_main_check_version_requirement (AsApp *app,
				   AsRequireKind kind,
				   const gchar *id,
				   const gchar *version,
				   GError **error)
{
	AsRequire *req;

	/* check args */
	if (version == NULL) {
		g_debug ("no paramater given for %s{%s}",
			 as_require_kind_to_string (kind), id);
		return TRUE;
	}

	/* does requirement exist */
	req = as_app_get_require_by_value (app, kind, id);
	if (req == NULL) {
		g_debug ("no requirement on %s{%s}",
			 as_require_kind_to_string (kind), id);
		return TRUE;
	}

	/* check version */
	if (!as_require_version_compare (req, version, error)) {
		g_prefix_error (error, "version of %s incorrect: ", id);
		return FALSE;
	}

	/* success */
	g_debug ("requirement %s %s %s on %s passed",
		 as_require_get_version (req),
		 as_require_compare_to_string (as_require_get_compare (req)),
		 version, id);
	return TRUE;
}
#endif

static AsApp *
fu_main_store_get_app_by_guids (AsStore *store, FuDevice *device)
{
	GPtrArray *guids = fu_device_get_guids (device);
	for (guint i = 0; i < guids->len; i++) {
		AsApp *app = NULL;
		app = as_store_get_app_by_provide (store,
						   AS_PROVIDE_KIND_FIRMWARE_FLASHED,
						   g_ptr_array_index (guids, i));
		if (app != NULL)
			return app;
	}
	return NULL;
}

static gboolean
fu_main_check_app_versions (AsApp *app, FuDevice *device, GError **error)
{
#if AS_CHECK_VERSION(0,6,7)
	/* make sure requirements are satisfied */
	if (!fu_main_check_version_requirement (app,
						AS_REQUIRE_KIND_ID,
						"org.freedesktop.fwupd",
						VERSION,
						error)) {
		return FALSE;
	}

	if (device != NULL) {
		if (!fu_main_check_version_requirement (app,
							AS_REQUIRE_KIND_FIRMWARE,
							NULL,
							fu_device_get_version (device),
							error)) {
			return FALSE;
		}
		if (!fu_main_check_version_requirement (app,
							AS_REQUIRE_KIND_FIRMWARE,
							"bootloader",
							fu_device_get_version_bootloader (device),
							error)) {
			return FALSE;
		}
	}
#endif

	/* success */
	return TRUE;
}

static AsScreenshot *
_as_app_get_screenshot_default (AsApp *app)
{
	GPtrArray *array = as_app_get_screenshots (app);
	if (array->len == 0)
		return NULL;
	return g_ptr_array_index (array, 0);
}

static gboolean
fu_main_update_helper_for_device (FuMainAuthHelper *helper,
				  FuDevice *device,
				  GError **error)
{
	AsApp *app;
	AsChecksum *csum_tmp;
	AsRelease *rel;
	GBytes *blob_fw;
	const gchar *tmp;
	const gchar *version;
	gboolean is_downgrade;
	gint vercmp;

	/* find from guid */
	app = fu_main_store_get_app_by_guids (helper->store, device);
	if (app == NULL) {
		g_autofree gchar *guid = NULL;
		guid = fu_main_get_guids_from_store (helper->store);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "firmware is not for this hw: required %s got %s",
			     fu_device_get_guid_default (device), guid);
		return FALSE;
	}

	/* check we can install it */
	if (!fu_main_check_app_versions (app, device, error))
		return FALSE;

	/* parse the DriverVer */
	rel = as_app_get_release_default (app);
	if (rel == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no releases in the firmware component");
		return FALSE;
	}

	/* no update abilities */
	if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_ALLOW_OFFLINE) &&
	    !fu_device_has_flag (device, FWUPD_DEVICE_FLAG_ALLOW_ONLINE)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Device %s does not currently allow updates",
			     fu_device_get_id (device));
		return FALSE;
	}

	/* not in bootloader mode */
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER)) {
		const gchar *caption = NULL;
		AsScreenshot *ss = _as_app_get_screenshot_default (app);
		if (ss != NULL)
			caption = as_screenshot_get_caption (ss, NULL);
		if (caption != NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Device %s needs to manually be put in update mode: %s",
				     fu_device_get_name (device), caption);
		} else {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Device %s needs to manually be put in update mode",
				     fu_device_get_name (device));
		}
		return FALSE;
	}

	/* get the blob */
	csum_tmp = as_release_get_checksum_by_target (rel, AS_CHECKSUM_TARGET_CONTENT);
	tmp = as_checksum_get_filename (csum_tmp);
	if (tmp == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no checksum filename");
		return FALSE;
	}

	/* not all devices have to use the same blob */
	blob_fw = as_release_get_blob (rel, tmp);
	if (blob_fw == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "failed to get firmware blob");
		return FALSE;
	}

	/* possibly convert the version from 0x to dotted */
	fu_main_vendor_quirk_release_version (app);

	version = as_release_get_version (rel);
	fu_device_set_update_version (device, version);

	/* compare to the lowest supported version, if it exists */
	tmp = fu_device_get_version_lowest (device);
	if (tmp != NULL && as_utils_vercmp (tmp, version) > 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_VERSION_NEWER,
			     "Specified firmware is older than the minimum "
			     "required version '%s < %s'", tmp, version);
		return FALSE;
	}

	/* check the device is locked */
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_LOCKED)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Device %s is locked",
			     fu_device_get_id (device));
		return FALSE;
	}

	/* compare the versions of what we have installed */
	tmp = fu_device_get_version (device);
	if (tmp == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Device %s does not yet have a current version",
			     fu_device_get_id (device));
		return FALSE;
	}
	vercmp = as_utils_vercmp (tmp, version);
	if (vercmp == 0 && (helper->flags & FWUPD_INSTALL_FLAG_ALLOW_REINSTALL) == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_VERSION_SAME,
			     "Specified firmware is already installed '%s'",
			     tmp);
		return FALSE;
	}
	is_downgrade = vercmp > 0;
	if (is_downgrade && (helper->flags & FWUPD_INSTALL_FLAG_ALLOW_OLDER) == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_VERSION_NEWER,
			     "Specified firmware is older than installed '%s < %s'",
			     tmp, version);
		return FALSE;
	}

	/* if any downgrade, we want the global to be true */
	if (is_downgrade)
		helper->is_downgrade = is_downgrade;

	/* verify */
	if (!fu_main_get_release_trust_flags (rel, &helper->trust_flags, error))
		return FALSE;

	/* success */
	g_ptr_array_add (helper->blob_fws, g_bytes_ref (blob_fw));
	return TRUE;
}

static gboolean
fu_main_update_helper (FuMainAuthHelper *helper, GError **error)
{
	g_autoptr(GError) error_first = NULL;

	/* load store file which also decompresses firmware */
	fu_main_set_status (helper->priv, FWUPD_STATUS_DECOMPRESSING);
	if (!as_store_from_bytes (helper->store, helper->blob_cab, NULL, error))
		return FALSE;

	/* we've specified a specific device; failure is critical */
	if (helper->devices->len > 0) {
		for (guint i = 0; i < helper->devices->len; i ++) {
			FuDevice *device = g_ptr_array_index (helper->devices, i);
			if (!fu_main_update_helper_for_device (helper, device, error))
				return FALSE;
		}
		return TRUE;
	}

	/* if we've not chosen a device, try and find anything in the
	 * cabinet 'store' that matches any installed device and is updatable */
	for (guint i = 0; i < helper->priv->devices->len; i++) {
		AsApp *app;
		FuDeviceItem *item;
		g_autoptr(GError) error_local = NULL;

		/* guid found */
		item = g_ptr_array_index (helper->priv->devices, i);
		app = fu_main_store_get_app_by_guids (helper->store, item->device);
		if (app == NULL)
			continue;

		/* check we can install it */
		if (!fu_main_check_app_versions (app, item->device, &error_local)) {
			if (error_first == NULL)
				error_first = g_error_copy (error_local);
			continue;
		}

		/* try this device, error not fatal */
		if (!fu_main_update_helper_for_device (helper,
						       item->device,
						       &error_local)) {
			g_debug ("failed to add %s: %s",
				 fu_device_get_id (item->device),
				 error_local->message);

			/* save this for later */
			if (error_first == NULL)
				error_first = g_error_copy (error_local);
			continue;
		}

		/* success */
		g_ptr_array_add (helper->devices, g_object_ref (item->device));
	}
	if (helper->devices->len == 0) {
		if (error_first != NULL) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     error_first->message);
		} else {
			g_autofree gchar *guid = NULL;
			guid = fu_main_get_guids_from_store (helper->store);
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no attached hardware matched %s",
				     guid);
		}
		return FALSE;
	}

	/* sanity check */
	if (helper->devices->len != helper->blob_fws->len) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "not enough firmware blobs (%u) for devices (%u)",
			     helper->blob_fws->len,
			     helper->devices->len);
		return FALSE;
	}

	return TRUE;
}

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

static FuDeviceItem *
fu_main_get_item_by_id_fallback_pending (FuMainPrivate *priv, const gchar *id, GError **error)
{
	FuDevice *dev;
	FuPlugin *plugin;
	FuDeviceItem *item = NULL;
	FwupdUpdateState update_state;
	const gchar *tmp;
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
	for (guint i = 0; i < devices->len; i++) {
		dev = g_ptr_array_index (devices, i);
		update_state = fu_device_get_update_state (dev);
		if (update_state == FWUPD_UPDATE_STATE_UNKNOWN)
			continue;
		if (update_state == FWUPD_UPDATE_STATE_PENDING)
			continue;

		/* if the device is not still connected, fake a FuDeviceItem */
		item = fu_main_get_item_by_id (priv, fu_device_get_id (dev));
		if (item == NULL) {
			tmp = fu_device_get_plugin (dev);
			plugin = fu_main_get_plugin_by_name (priv, tmp);
			if (plugin == NULL) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_FOUND,
					     "no plugin %s found", tmp);
				return NULL;
			}
			item = g_new0 (FuDeviceItem, 1);
			item->device = g_object_ref (dev);
			item->plugin = g_object_ref (plugin);
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

static const gchar *
fu_main_get_action_id_for_device (FuMainAuthHelper *helper)
{
	gboolean all_removable = TRUE;
	gboolean is_trusted;

	/* only test the payload */
	is_trusted = (helper->trust_flags & FWUPD_TRUST_FLAG_PAYLOAD) > 0;

	/* any non-removable means false */
	for (guint i = 0; i < helper->devices->len; i ++) {
		FuDevice *device = g_ptr_array_index (helper->devices, i);
		if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_INTERNAL)) {
			all_removable = FALSE;
			break;
		}
	}

	/* relax authentication checks for removable devices */
	if (all_removable) {
		if (helper->is_downgrade)
			return "org.freedesktop.fwupd.downgrade-hotplug";
		if (is_trusted)
			return "org.freedesktop.fwupd.update-hotplug-trusted";
		return "org.freedesktop.fwupd.update-hotplug";
	}

	/* internal device */
	if (helper->is_downgrade)
		return "org.freedesktop.fwupd.downgrade-internal";
	if (is_trusted)
		return "org.freedesktop.fwupd.update-internal-trusted";
	return "org.freedesktop.fwupd.update-internal";
}

static gboolean
fu_main_daemon_update_metadata (FuMainPrivate *priv, gint fd, gint fd_sig, GError **error)
{
	const guint8 *data;
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
	g_autoptr(GFile) file_parent = NULL;
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
	for (guint i = 0; i < apps->len; i++) {
		AsApp *app = g_ptr_array_index (apps, i);
		as_store_add_app (priv->store, app);
	}

	/* ensure directory exists */
	file = g_file_new_for_path ("/var/cache/app-info/xmls/fwupd.xml");
	file_parent = g_file_get_parent (file);
	if (!g_file_query_exists (file_parent, NULL)) {
		if (!g_file_make_directory_with_parents (file_parent, NULL, error))
			return FALSE;
	}

	/* save the new file */
	as_store_set_api_version (priv->store, 0.9);
	as_store_set_origin (priv->store, as_store_get_origin (store));
	if (!as_store_to_file (priv->store, file,
			       AS_NODE_TO_XML_FLAG_ADD_HEADER |
			       AS_NODE_TO_XML_FLAG_FORMAT_MULTILINE |
			       AS_NODE_TO_XML_FLAG_FORMAT_INDENT,
			       NULL, error)) {
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_main_store_delay_cb (gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	GPtrArray *apps;

	/* print what we've got */
	apps = as_store_get_apps (priv->store);
	if (apps->len == 0) {
		g_debug ("no devices in store");
	} else {
		g_debug ("devices now in store:");
		for (guint i = 0; i < apps->len; i++) {
			AsApp *app = g_ptr_array_index (apps, i);
			g_debug ("%u\t%s\t%s", i + 1,
				 as_app_get_id (app),
				 as_app_get_name (app, NULL));
		}
	}

	/* are any devices now supported? */
	for (guint i = 0; i < priv->devices->len; i++) {
		FuDeviceItem *item = g_ptr_array_index (priv->devices, i);
		if (fu_main_get_updates_item_update (priv, item))
			fu_main_emit_device_changed (priv, item->device);
	}

	priv->store_changed_id = 0;
	return G_SOURCE_REMOVE;
}

static void
fu_main_store_changed_cb (AsStore *store, FuMainPrivate *priv)
{
	if (priv->store_changed_id != 0)
		return;
	priv->store_changed_id = g_timeout_add (200, fu_main_store_delay_cb, priv);
}

static gboolean
fu_main_get_updates_item_update (FuMainPrivate *priv, FuDeviceItem *item)
{
	AsApp *app;
	AsChecksum *csum;
	AsRelease *rel;
	GPtrArray *releases;
	const gchar *tmp;
	const gchar *version;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) updates_list = NULL;

	/* get device version */
	version = fu_device_get_version (item->device);
	if (version == NULL)
		return FALSE;

	/* match the GUIDs in the XML */
	app = fu_main_store_get_app_by_guids (priv->store, item->device);
	if (app == NULL)
		return FALSE;

	/* possibly convert the version from 0x to dotted */
	fu_main_vendor_quirk_release_version (app);

	/* get latest release */
	rel = as_app_get_release_default (app);
	if (rel == NULL) {
		g_debug ("%s [%s] has no firmware update metadata",
			 fu_device_get_id (item->device),
			 fu_device_get_name (item->device));
		return FALSE;
	}

	/* supported in metadata */
	fwupd_result_add_device_flag (FWUPD_RESULT (item->device),
				      FWUPD_DEVICE_FLAG_SUPPORTED);

	/* check if actually newer than what we have installed */
	if (as_utils_vercmp (as_release_get_version (rel), version) <= 0) {
		g_debug ("%s has no firmware updates",
			 fu_device_get_id (item->device));
		return FALSE;
	}

	/* check we can install it */
	if (!fu_main_check_app_versions (app, item->device, &error)) {
		g_debug ("can not be installed: %s", error->message);
		return FALSE;
	}

	/* only show devices that can be updated */
	if (!fu_device_has_flag (item->device, FWUPD_DEVICE_FLAG_ALLOW_OFFLINE) &&
	    !fu_device_has_flag (item->device, FWUPD_DEVICE_FLAG_ALLOW_ONLINE)) {
		g_debug ("ignoring %s [%s] as not updatable live or offline",
			 fu_device_get_id (item->device),
			 fu_device_get_name (item->device));
		return FALSE;
	}

	/* add application metadata */
	fu_device_set_update_id (item->device, as_app_get_id (app));
	tmp = as_app_get_developer_name (app, NULL);
	if (tmp != NULL)
		fu_device_set_update_vendor (item->device, tmp);
	tmp = as_app_get_name (app, NULL);
	if (tmp != NULL)
		fu_device_set_update_name (item->device, tmp);
	tmp = as_app_get_comment (app, NULL);
	if (tmp != NULL)
		fu_device_set_update_summary (item->device, tmp);
	tmp = as_app_get_description (app, NULL);
	if (tmp != NULL)
		fu_device_set_description (item->device, tmp);
	tmp = as_app_get_url_item (app, AS_URL_KIND_HOMEPAGE);
	if (tmp != NULL)
		fu_device_set_update_homepage (item->device, tmp);
	tmp = as_app_get_project_license (app);
	if (tmp != NULL)
		fu_device_set_update_license (item->device, tmp);
#if AS_CHECK_VERSION(0,6,1)
	tmp = as_app_get_unique_id (app);
	if (tmp != NULL)
		fu_device_set_unique_id (item->device, tmp);
#else
	fu_device_set_unique_id (item->device, as_app_get_id (app));
#endif

	/* add release information */
	tmp = as_release_get_version (rel);
	if (tmp != NULL)
		fu_device_set_update_version (item->device, tmp);
	csum = as_release_get_checksum_by_target (rel, AS_CHECKSUM_TARGET_CONTAINER);
	if (csum != NULL) {
		fu_device_set_update_checksum (item->device,
					       as_checksum_get_value (csum));
	}
	tmp = as_release_get_location_default (rel);
	if (tmp != NULL)
		fu_device_set_update_uri (item->device, tmp);

	/* get the list of releases newer than the one installed */
	updates_list = g_ptr_array_new ();
	releases = as_app_get_releases (app);
	for (guint i = 0; i < releases->len; i++) {
		rel = g_ptr_array_index (releases, i);
		if (as_utils_vercmp (as_release_get_version (rel), version) <= 0)
			continue;
		tmp = as_release_get_description (rel, NULL);
		if (tmp == NULL)
			continue;
		g_ptr_array_add (updates_list, rel);
	}

	/* no prefix on each release */
	if (updates_list->len == 1) {
		rel = g_ptr_array_index (updates_list, 0);
		fu_device_set_update_description (item->device,
						  as_release_get_description (rel, NULL));
	} else {
		g_autoptr(GString) update_desc = NULL;
		update_desc = g_string_new ("");

		/* get the descriptions with a version prefix */
		for (guint i = 0; i < updates_list->len; i++) {
			rel = g_ptr_array_index (updates_list, i);
			g_string_append_printf (update_desc,
						"<p>%s:</p>%s",
						as_release_get_version (rel),
						as_release_get_description (rel, NULL));
		}
		if (update_desc->len > 0)
			fu_device_set_update_description (item->device, update_desc->str);
	}

	/* success */
	return TRUE;
}

/* find any updates using the AppStream metadata */
static GPtrArray *
fu_main_get_updates (FuMainPrivate *priv, GError **error)
{
	GPtrArray *updates = g_ptr_array_new ();
	for (guint i = 0; i < priv->devices->len; i++) {
		FuDeviceItem *item = g_ptr_array_index (priv->devices, i);
		if (fu_main_get_updates_item_update (priv, item))
			g_ptr_array_add (updates, item);
	}
	return updates;
}

static AsStore *
fu_main_get_store_from_fd (FuMainPrivate *priv, gint fd, GError **error)
{
	g_autofree gchar *checksum = NULL;
	g_autoptr(AsStore) store = NULL;
	g_autoptr(GBytes) blob_cab = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* read the entire fd to a data blob */
	stream = g_unix_input_stream_new (fd, TRUE);
	blob_cab = g_input_stream_read_bytes (stream,
					      FU_MAIN_FIRMWARE_SIZE_MAX,
					      NULL, &error_local);
	if (blob_cab == NULL){
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     error_local->message);
		return NULL;
	}

	/* load file */
	store = as_store_new ();
	if (!as_store_from_bytes (store, blob_cab, NULL, &error_local)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     error_local->message);
		return NULL;
	}

	/* get a checksum of the file and use it as the origin */
	checksum = g_compute_checksum_for_data (G_CHECKSUM_SHA1,
						g_bytes_get_data (blob_cab, NULL),
						g_bytes_get_size (blob_cab));
	as_store_set_origin (store, checksum);

	return g_steal_pointer (&store);
}

static FwupdResult *
fu_main_get_result_from_app (FuMainPrivate *priv, AsApp *app, GError **error)
{
	FwupdTrustFlags trust_flags = FWUPD_TRUST_FLAG_NONE;
	AsRelease *rel;
	AsChecksum * csum_tmp;
	const gchar *fn;
	GPtrArray *provides;
	g_autoptr(FwupdResult) res = NULL;

	res = fwupd_result_new ();
	provides = as_app_get_provides (app);
	for (guint i = 0; i < provides->len; i++) {
		AsProvide *prov = AS_PROVIDE (g_ptr_array_index (provides, i));
		FuDeviceItem *item;
		const gchar *guid;

		/* not firmware */
		if (as_provide_get_kind (prov) != AS_PROVIDE_KIND_FIRMWARE_FLASHED)
			continue;

		/* is a online or offline update appropriate */
		guid = as_provide_get_value (prov);
		if (guid == NULL)
			continue;
		item = fu_main_get_item_by_guid (priv, guid);
		if (item != NULL) {
			fwupd_result_set_device_flags (res, fu_device_get_flags (item->device));
			fwupd_result_set_device_id (res, fu_device_get_id (item->device));
		}

		/* add GUID */
		fwupd_result_add_guid (res, guid);
	}
	if (fwupd_result_get_guids(res)->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "component has no GUIDs");
		return NULL;
	}

	/* check we can install it */
	if (!fu_main_check_app_versions (app, NULL, error))
		return NULL;

	/* verify trust */
	rel = as_app_get_release_default (app);
	if (!fu_main_get_release_trust_flags (rel, &trust_flags, error))
		return NULL;

	/* possibly convert the version from 0x to dotted */
	fu_main_vendor_quirk_release_version (app);

	/* create a result with all the metadata in */
	fwupd_result_set_device_description (res, as_app_get_description (app, NULL));
	fwupd_result_set_update_id (res, as_app_get_id (app));
	fwupd_result_set_update_description (res, as_release_get_description (rel, NULL));
	fwupd_result_set_update_homepage (res, as_app_get_url_item (app, AS_URL_KIND_HOMEPAGE));
	fwupd_result_set_update_license (res, as_app_get_project_license (app));
	fwupd_result_set_update_name (res, as_app_get_name (app, NULL));
	fwupd_result_set_update_size (res, as_release_get_size (rel, AS_SIZE_KIND_INSTALLED));
	fwupd_result_set_update_summary (res, as_app_get_comment (app, NULL));
	fwupd_result_set_update_trust_flags (res, trust_flags);
	fwupd_result_set_update_vendor (res, as_app_get_developer_name (app, NULL));
	fwupd_result_set_update_version (res, as_release_get_version (rel));
#if AS_CHECK_VERSION(0,6,1)
	fwupd_result_set_unique_id (res, as_app_get_unique_id (app));
#else
	fwupd_result_set_unique_id (res, as_app_get_id (app));
#endif

	csum_tmp = as_release_get_checksum_by_target (rel,
	AS_CHECKSUM_TARGET_CONTENT);
	fn = as_checksum_get_filename (csum_tmp);
	if (fn != NULL)
		fwupd_result_set_update_filename (res, fn);
	return g_steal_pointer (&res);
}

static GVariant *
fu_main_get_details_from_fd (FuMainPrivate *priv, gint fd, GError **error)
{
	AsApp *app = NULL;
	GPtrArray *apps;
	g_autoptr(AsStore) store = NULL;
	g_autoptr(FwupdResult) res = NULL;

	store = fu_main_get_store_from_fd (priv, fd, error);
	if (store == NULL)
		return NULL;

	/* get all apps */
	apps = as_store_get_apps (store);
	if (apps->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no components");
		return NULL;
	}
	if (apps->len > 1) {
		/* we've got a .cab file with multiple components,
		 * so try to find the first thing that's installed */
		for (guint i = 0; i < priv->devices->len; i++) {
			FuDeviceItem *item = g_ptr_array_index (priv->devices, i);
			app = fu_main_store_get_app_by_guids (store, item->device);
			if (app != NULL)
				break;
		}
	}

	/* well, we've tried our best, just show the first entry */
	if (app == NULL)
		app = AS_APP (g_ptr_array_index (apps, 0));

	/* check we can install it */
	if (!fu_main_check_app_versions (app, NULL, error))
		return FALSE;

	/* create a result with all the metadata in */
	as_app_set_origin (app, as_store_get_origin (store));
	res = fu_main_get_result_from_app (priv, app, error);
	if (res == NULL)
		return NULL;
	return fwupd_result_to_data (res, "(a{sv})");
}

static GVariant *
fu_main_get_details_local_from_fd (FuMainPrivate *priv, gint fd, GError **error)
{
	GPtrArray *apps;
	GVariantBuilder builder;
	g_autoptr(AsStore) store = NULL;

	store = fu_main_get_store_from_fd (priv, fd, error);
	if (store == NULL)
		return NULL;

	/* get all apps */
	apps = as_store_get_apps (store);
	if (apps->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no components");
		return NULL;
	}

	/* create results with all the metadata in */
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	for (guint i = 0; i < apps->len; i++) {
		g_autoptr(FwupdResult) res = NULL;
		AsApp *app = g_ptr_array_index (apps, i);
		GVariant *tmp;

		/* check we can install it */
		if (!fu_main_check_app_versions (app, NULL, error))
			return NULL;

		as_app_set_origin (app, as_store_get_origin (store));
		res = fu_main_get_result_from_app (priv, app, error);
		if (res == NULL)
			return NULL;
		tmp = fwupd_result_to_data (res, "{sa{sv}}");
		g_variant_builder_add_value (&builder, tmp);
	}
	return g_variant_new ("(a{sa{sv}})", &builder);
}

static void
fu_main_daemon_method_call (GDBusConnection *connection, const gchar *sender,
			    const gchar *object_path, const gchar *interface_name,
			    const gchar *method_name, GVariant *parameters,
			    GDBusMethodInvocation *invocation, gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	GVariant *val;
	g_autoptr(GError) error = NULL;

	/* return 'as' */
	if (g_strcmp0 (method_name, "GetDevices") == 0) {
		g_debug ("Called %s()", method_name);
		val = fu_main_device_array_to_variant (priv->devices, &error);
		if (val == NULL) {
			if (g_error_matches (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOTHING_TO_DO)) {
				g_prefix_error (&error, "No detected devices: ");
			}
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}
		fu_main_invocation_return_value (priv, invocation, val);
		return;
	}

	/* return 'as' */
	if (g_strcmp0 (method_name, "GetUpdates") == 0) {
		g_autoptr(GPtrArray) updates = NULL;
		g_debug ("Called %s()", method_name);
		updates = fu_main_get_updates (priv, &error);
		if (updates == NULL) {
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}
		val = fu_main_device_array_to_variant (updates, &error);
		if (val == NULL) {
			if (g_error_matches (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOTHING_TO_DO)) {
				g_prefix_error (&error, "No devices can be updated: ");
			}
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}
		fu_main_invocation_return_value (priv, invocation, val);
		return;
	}

	/* return '' */
	if (g_strcmp0 (method_name, "ClearResults") == 0) {
		FuDeviceItem *item = NULL;
		const gchar *id = NULL;

		g_variant_get (parameters, "(&s)", &id);
		g_debug ("Called %s(%s)", method_name, id);

		/* find device */
		item = fu_main_get_item_by_id_fallback_pending (priv, id, &error);
		if (item == NULL) {
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}

		/* call into the plugin */
		if (!fu_plugin_runner_clear_results (item->plugin, item->device, &error)) {
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}

		/* success */
		fu_main_invocation_return_value (priv, invocation, NULL);
		return;
	}

	/* return 'a{sv}' */
	if (g_strcmp0 (method_name, "GetResults") == 0) {
		FuDeviceItem *item = NULL;
		const gchar *id = NULL;

		g_variant_get (parameters, "(&s)", &id);
		g_debug ("Called %s(%s)", method_name, id);

		/* find device */
		item = fu_main_get_item_by_id_fallback_pending (priv, id, &error);
		if (item == NULL) {
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}

		/* call into the plugin */
		if (!fu_plugin_runner_get_results (item->plugin, item->device, &error)) {
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}

		/* ensure the unique ID is set */
		if (fwupd_result_get_unique_id (FWUPD_RESULT (item->device)) == NULL) {
			g_autofree gchar *id2 = NULL;
			FwupdResult *res = FWUPD_RESULT (item->device);
#if AS_CHECK_VERSION(0,6,1)
			id2 = as_utils_unique_id_build (AS_APP_SCOPE_SYSTEM,
							AS_BUNDLE_KIND_UNKNOWN,
							NULL,
							AS_APP_KIND_FIRMWARE,
							fwupd_result_get_device_name (res),
							fwupd_result_get_device_version (res));
#else
			id2 = g_strdup_printf ("system/*/*/firmware/%s/%s",
					       fwupd_result_get_device_name (res),
					       fwupd_result_get_device_version (res));
#endif
			fwupd_result_set_unique_id (res, id2);
		}

		/* success */
		val = fwupd_result_to_data (FWUPD_RESULT (item->device), "(a{sv})");
		fu_main_invocation_return_value (priv, invocation, val);
		return;
	}

	/* return '' */
	if (g_strcmp0 (method_name, "UpdateMetadata") == 0) {
		GDBusMessage *message;
		GUnixFDList *fd_list;
		gint fd_data;
		gint fd_sig;

		message = g_dbus_method_invocation_get_message (invocation);
		fd_list = g_dbus_message_get_unix_fd_list (message);
		if (fd_list == NULL || g_unix_fd_list_get_length (fd_list) != 2) {
			g_set_error (&error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid handle");
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}
		fd_data = g_unix_fd_list_get (fd_list, 0, &error);
		if (fd_data < 0) {
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}
		fd_sig = g_unix_fd_list_get (fd_list, 1, &error);
		if (fd_sig < 0) {
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}
		if (!fu_main_daemon_update_metadata (priv, fd_data, fd_sig, &error)) {
			g_prefix_error (&error, "failed to update metadata: ");
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}
		fu_main_invocation_return_value (priv, invocation, NULL);
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
			g_set_error (&error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "No such device %s", id);
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}

		/* check the device is locked */
		if (!fu_device_has_flag (item->device, FWUPD_DEVICE_FLAG_LOCKED)) {
			g_set_error (&error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "Device %s is not locked", id);
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}

		/* process the firmware */
		helper = g_new0 (FuMainAuthHelper, 1);
		helper->auth_kind = FU_MAIN_AUTH_KIND_UNLOCK;
		helper->invocation = g_object_ref (invocation);
		helper->devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
		helper->priv = priv;

		/* FIXME: do we want to support "*"? */
		g_ptr_array_add (helper->devices, g_object_ref (item->device));

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

	/* return 'b' */
	if (g_strcmp0 (method_name, "VerifyUpdate") == 0) {
		FuDeviceItem *item = NULL;
		FuMainAuthHelper *helper;
		const gchar *id = NULL;
		g_autoptr(PolkitSubject) subject = NULL;

		/* check the id exists */
		g_variant_get (parameters, "(&s)", &id);
		g_debug ("Called %s(%s)", method_name, id);
		item = fu_main_get_item_by_id (priv, id);
		if (item == NULL) {
			g_set_error (&error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "No such device %s", id);
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}

		/* process the firmware */
		helper = g_new0 (FuMainAuthHelper, 1);
		helper->auth_kind = FU_MAIN_AUTH_KIND_VERIFY_UPDATE;
		helper->invocation = g_object_ref (invocation);
		helper->devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
		helper->priv = priv;
		g_ptr_array_add (helper->devices, g_object_ref (item->device));

		/* authenticate */
		subject = polkit_system_bus_name_new (sender);
		polkit_authority_check_authorization (helper->priv->authority, subject,
						      "org.freedesktop.fwupd.verify-update",
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

		/* check the id exists */
		g_variant_get (parameters, "(&s)", &id);
		g_debug ("Called %s(%s)", method_name, id);
		item = fu_main_get_item_by_id (priv, id);
		if (item == NULL) {
			g_set_error (&error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "No such device %s", id);
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}

		/* set the device firmware hash */
		if (!fu_plugin_runner_verify (item->plugin, item->device,
					 FU_PLUGIN_VERIFY_FLAG_NONE, &error)) {
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}

		/* find component in metadata */
		app = fu_main_store_get_app_by_guids (priv->store, item->device);
		if (app == NULL) {
			g_set_error_literal (&error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_FOUND,
					     "No metadata");
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}

		/* find version in metadata */
		version = fu_device_get_version (item->device);
		release = as_app_get_release (app, version);
		if (release == NULL)
			release = as_app_get_release_default (app);
		if (release == NULL) {
			g_set_error (&error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "No version %s", version);
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}

		/* find checksum */
		csum = as_release_get_checksum_by_target (release, AS_CHECKSUM_TARGET_CONTENT);
		if (csum == NULL) {
			g_set_error (&error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "No content checksum for %s", version);
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}
		hash = fu_device_get_checksum (item->device);
		if (g_strcmp0 (as_checksum_get_value (csum), hash) != 0) {
			g_set_error (&error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "For v%s expected %s, got %s",
				     version,
				     as_checksum_get_value (csum),
				     hash);
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}
		fu_main_invocation_return_value (priv, invocation, NULL);
		return;
	}

	/* return '' */
	if (g_strcmp0 (method_name, "Install") == 0) {
		FuDeviceItem *item = NULL;
		FuMainAuthHelper *helper;
		FwupdInstallFlags flags = FWUPD_INSTALL_FLAG_NONE;
		GDBusMessage *message;
		GUnixFDList *fd_list;
		GVariant *prop_value;
		const gchar *action_id;
		const gchar *id = NULL;
		gchar *prop_key;
		gint32 fd_handle = 0;
		gint fd;
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
				g_set_error (&error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_FOUND,
					     "no such device %s", id);
				fu_main_invocation_return_error (priv, invocation, error);
				return;
			}
		}

		/* get options */
		while (g_variant_iter_next (iter, "{&sv}",
					    &prop_key, &prop_value)) {
			g_debug ("got option %s", prop_key);
			if (g_strcmp0 (prop_key, "offline") == 0 &&
			    g_variant_get_boolean (prop_value) == TRUE)
				flags |= FWUPD_INSTALL_FLAG_OFFLINE;
			if (g_strcmp0 (prop_key, "allow-older") == 0 &&
			    g_variant_get_boolean (prop_value) == TRUE)
				flags |= FWUPD_INSTALL_FLAG_ALLOW_OLDER;
			if (g_strcmp0 (prop_key, "allow-reinstall") == 0 &&
			    g_variant_get_boolean (prop_value) == TRUE)
				flags |= FWUPD_INSTALL_FLAG_ALLOW_REINSTALL;
			if (g_strcmp0 (prop_key, "force") == 0 &&
			    g_variant_get_boolean (prop_value) == TRUE)
				flags |= FWUPD_INSTALL_FLAG_FORCE;
			g_variant_unref (prop_value);
		}

		/* get the fd */
		message = g_dbus_method_invocation_get_message (invocation);
		fd_list = g_dbus_message_get_unix_fd_list (message);
		if (fd_list == NULL || g_unix_fd_list_get_length (fd_list) != 1) {
			g_set_error (&error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid handle");
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}
		fd = g_unix_fd_list_get (fd_list, 0, &error);
		if (fd < 0) {
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}

		/* read the entire fd to a data blob */
		stream = g_unix_input_stream_new (fd, TRUE);
		blob_cab = g_input_stream_read_bytes (stream,
						      FU_MAIN_FIRMWARE_SIZE_MAX,
						      NULL, &error);
		if (blob_cab == NULL){
			fu_main_invocation_return_error (priv, invocation, error);
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
		helper->devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
		helper->blob_fws = g_ptr_array_new_with_free_func ((GDestroyNotify) g_bytes_unref);
		if (item != NULL)
			g_ptr_array_add (helper->devices, g_object_ref (item->device));
		if (!fu_main_update_helper (helper, &error)) {
			fu_main_invocation_return_error (helper->priv, helper->invocation, error);
			fu_main_helper_free (helper);
			return;
		}

		/* is root */
		if (fu_main_dbus_get_uid (priv, sender) == 0) {
			if (!fu_main_plugin_update_authenticated (helper, &error)) {
				fu_main_invocation_return_error (priv, invocation, error);
			} else {
				fu_main_invocation_return_value (priv, invocation, NULL);
			}
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

	/* get a single result object from a local file */
	if (g_strcmp0 (method_name, "GetDetails") == 0) {
		GDBusMessage *message;
		GUnixFDList *fd_list;
		gint32 fd_handle = 0;
		gint fd;

		/* get parameters */
		g_variant_get (parameters, "(h)", &fd_handle);
		g_debug ("Called %s(%i)", method_name, fd_handle);

		/* get the fd */
		message = g_dbus_method_invocation_get_message (invocation);
		fd_list = g_dbus_message_get_unix_fd_list (message);
		if (fd_list == NULL || g_unix_fd_list_get_length (fd_list) != 1) {
			g_set_error (&error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid handle");
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}
		fd = g_unix_fd_list_get (fd_list, 0, &error);
		if (fd < 0) {
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}

		/* get details about the file */
		val = fu_main_get_details_from_fd (priv, fd, &error);
		if (val == NULL) {
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}
		fu_main_invocation_return_value (priv, invocation, val);
		return;
	}

	/* get multiple result objects from a local file */
	if (g_strcmp0 (method_name, "GetDetailsLocal") == 0) {
		GDBusMessage *message;
		GUnixFDList *fd_list;
		gint32 fd_handle = 0;
		gint fd;

		/* get parameters */
		g_variant_get (parameters, "(h)", &fd_handle);
		g_debug ("Called %s(%i)", method_name, fd_handle);

		/* get the fd */
		message = g_dbus_method_invocation_get_message (invocation);
		fd_list = g_dbus_message_get_unix_fd_list (message);
		if (fd_list == NULL || g_unix_fd_list_get_length (fd_list) != 1) {
			g_set_error (&error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid handle");
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}
		fd = g_unix_fd_list_get (fd_list, 0, &error);
		if (fd < 0) {
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}

		/* get details about the file */
		val = fu_main_get_details_local_from_fd (priv, fd, &error);
		if (val == NULL) {
			fu_main_invocation_return_error (priv, invocation, error);
			return;
		}
		fu_main_invocation_return_value (priv, invocation, val);
		return;
	}

	/* we suck */
	g_set_error (&error,
		     G_DBUS_ERROR,
		     G_DBUS_ERROR_UNKNOWN_METHOD,
		     "no such method %s", method_name);
	fu_main_invocation_return_error (priv, invocation, error);
}

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
		return g_variant_new_uint32 (priv->status);

	/* return an error */
	g_set_error (error,
		     G_DBUS_ERROR,
		     G_DBUS_ERROR_UNKNOWN_PROPERTY,
		     "failed to get daemon property %s",
		     property_name);
	return NULL;
}

static void
fu_main_plugins_setup (FuMainPrivate *priv)
{
	g_autoptr(AsProfileTask) ptask = NULL;

	ptask = as_profile_start_literal (priv->profile, "FuMain:setup");
	g_assert (ptask != NULL);
	for (guint i = 0; i < priv->plugins->len; i++) {
		g_autoptr(GError) error = NULL;
		g_autoptr(AsProfileTask) ptask2 = NULL;
		FuPlugin *plugin = g_ptr_array_index (priv->plugins, i);
		ptask2 = as_profile_start (priv->profile,
					   "FuMain:setup{%s}",
					   fu_plugin_get_name (plugin));
		g_assert (ptask2 != NULL);
		if (!fu_plugin_runner_startup (plugin, &error)) {
			fu_plugin_set_enabled (plugin, FALSE);
			g_warning ("disabling plugin because: %s", error->message);
		}
	}
}

static void
fu_main_plugins_coldplug (FuMainPrivate *priv)
{
	g_autoptr(AsProfileTask) ptask = NULL;

	/* don't allow coldplug to be scheduled when in coldplug */
	priv->coldplug_running = TRUE;

	/* prepare */
	for (guint i = 0; i < priv->plugins->len; i++) {
		g_autoptr(GError) error = NULL;
		FuPlugin *plugin = g_ptr_array_index (priv->plugins, i);
		if (!fu_plugin_runner_coldplug_prepare (plugin, &error))
			g_warning ("failed to prepare coldplug: %s", error->message);
	}

	/* do this in one place */
	if (priv->coldplug_delay > 0) {
		g_debug ("sleeping for %ums", priv->coldplug_delay);
		g_usleep (priv->coldplug_delay * 1000);
	}

	/* exec */
	ptask = as_profile_start_literal (priv->profile, "FuMain:coldplug");
	g_assert (ptask != NULL);
	for (guint i = 0; i < priv->plugins->len; i++) {
		g_autoptr(GError) error = NULL;
		g_autoptr(AsProfileTask) ptask2 = NULL;
		FuPlugin *plugin = g_ptr_array_index (priv->plugins, i);
		ptask2 = as_profile_start (priv->profile,
					   "FuMain:coldplug{%s}",
					   fu_plugin_get_name (plugin));
		g_assert (ptask2 != NULL);
		if (!fu_plugin_runner_coldplug (plugin, &error)) {
			fu_plugin_set_enabled (plugin, FALSE);
			g_warning ("disabling plugin because: %s", error->message);
		}
	}

	/* cleanup */
	for (guint i = 0; i < priv->plugins->len; i++) {
		g_autoptr(GError) error = NULL;
		FuPlugin *plugin = g_ptr_array_index (priv->plugins, i);
		if (!fu_plugin_runner_coldplug_cleanup (plugin, &error))
			g_warning ("failed to cleanup coldplug: %s", error->message);
	}

	/* we can recoldplug from this point on */
	priv->coldplug_running = FALSE;
}

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

static void
fu_main_on_name_acquired_cb (GDBusConnection *connection,
			     const gchar *name,
			     gpointer user_data)
{
	g_debug ("FuMain: acquired name: %s", name);
}

static void
fu_main_on_name_lost_cb (GDBusConnection *connection,
			 const gchar *name,
			 gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	g_debug ("FuMain: lost name: %s", name);
	g_main_loop_quit (priv->loop);
}

static gboolean
fu_main_timed_exit_cb (gpointer user_data)
{
	GMainLoop *loop = (GMainLoop *) user_data;
	g_main_loop_quit (loop);
	return G_SOURCE_REMOVE;
}

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

static void
fu_main_plugin_device_added_cb (FuPlugin *plugin,
				FuDevice *device,
				gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	FuDeviceItem *item;
	g_auto(GStrv) guids = NULL;
	g_autoptr(GError) error = NULL;

	/* device has no GUIDs set! */
	if (fu_device_get_guid_default (device) == NULL) {
		g_warning ("no GUIDs for device %s [%s]",
			   fu_device_get_id (device),
			   fu_device_get_name (device));
		return;
	}

	/* is this GUID blacklisted */
	guids = g_key_file_get_string_list (priv->config,
					    "fwupd",
					    "BlacklistDevices",
					    NULL, /* length */
					    NULL);
	if (guids != NULL &&
	    g_strv_contains ((const gchar * const *) guids,
			     fu_device_get_guid_default (device))) {
		g_debug ("%s is blacklisted [%s], ignoring from %s",
			 fu_device_get_id (device),
			 fu_device_get_guid_default (device),
			 fu_plugin_get_name (plugin));
		return;
	}

	/* remove any fake device */
	item = fu_main_get_item_by_id (priv, fu_device_get_id (device));
	if (item != NULL) {
		g_debug ("already added %s by %s, ignoring same device from %s",
			 fu_device_get_id (item->device),
			 fu_device_get_plugin (item->device),
			 fu_plugin_get_name (plugin));
		return;
	}

	/* create new device */
	item = g_new0 (FuDeviceItem, 1);
	item->device = g_object_ref (device);
	item->plugin = g_object_ref (plugin);
	g_ptr_array_add (priv->devices, item);

	/* match the metadata at this point so clients can tell if the
	 * device is worthy */
	fu_main_get_updates_item_update (priv, item);

	/* notify clients */
	fu_main_emit_device_added (priv, item->device);
	fu_main_emit_changed (priv);
}

static void
fu_main_plugin_device_removed_cb (FuPlugin *plugin,
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

	/* check this came from the same plugin */
	if (g_strcmp0 (fu_plugin_get_name (plugin),
		       fu_plugin_get_name (item->plugin)) != 0) {
		g_debug ("ignoring duplicate removal from %s",
			 fu_plugin_get_name (plugin));
		return;
	}

	/* make the UI update */
	fu_main_emit_device_removed (priv, device);
	g_ptr_array_remove (priv->devices, item);
	fu_main_emit_changed (priv);
}

static void
fu_main_plugin_status_changed_cb (FuPlugin *plugin,
				    FwupdStatus status,
				    gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	fu_main_set_status (priv, status);
}

static void
fu_main_plugin_percentage_changed_cb (FuPlugin *plugin,
					guint percentage,
					gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	fu_main_set_percentage (priv, percentage);
}

static gboolean
fu_main_recoldplug_delay_cb (gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	g_debug ("performing a recoldplug");
	fu_main_plugins_coldplug (priv);
	priv->coldplug_id = 0;
	return FALSE;
}

static void
fu_main_plugin_recoldplug_cb (FuPlugin *plugin, FuMainPrivate *priv)
{
	if (priv->coldplug_running) {
		g_warning ("coldplug already running, cannot recoldplug");
		return;
	}
	g_debug ("scheduling a recoldplug");
	if (priv->coldplug_id != 0)
		g_source_remove (priv->coldplug_id);
	priv->coldplug_id = g_timeout_add (1500, fu_main_recoldplug_delay_cb, priv);
}

static void
fu_main_plugin_set_coldplug_delay_cb (FuPlugin *plugin, guint duration, FuMainPrivate *priv)
{
	priv->coldplug_delay = MAX (priv->coldplug_delay, duration);
	g_debug ("got coldplug delay of %ums, global maximum is now %ums",
		 duration, priv->coldplug_delay);
}

static gboolean
fu_main_load_plugins (FuMainPrivate *priv, GError **error)
{
	const gchar *fn;
	g_autofree gchar *plugin_dir = NULL;
	g_autoptr(GDir) dir = NULL;
	g_auto(GStrv) blacklist = NULL;

	/* get plugin blacklist */
	blacklist = g_key_file_get_string_list (priv->config,
						"fwupd",
						"BlacklistPlugins",
						NULL, /* length */
						NULL);

	/* search */
	plugin_dir = g_build_filename (LIBDIR, "fwupd-plugins-2", NULL);
	dir = g_dir_open (plugin_dir, 0, error);
	if (dir == NULL)
		return FALSE;
	while ((fn = g_dir_read_name (dir)) != NULL) {
		g_autofree gchar *filename = NULL;
		g_autoptr(FuPlugin) plugin = NULL;
		g_autoptr(GError) error_local = NULL;

		/* ignore non-plugins */
		if (!g_str_has_suffix (fn, ".so"))
			continue;

		/* open module */
		filename = g_build_filename (plugin_dir, fn, NULL);
		plugin = fu_plugin_new ();
		fu_plugin_set_usb_context (plugin, priv->usb_ctx);
		g_debug ("adding plugin %s", filename);
		if (!fu_plugin_open (plugin, filename, &error_local)) {
			g_warning ("failed to open plugin %s: %s",
				   filename, error_local->message);
			continue;
		}

		/* is blacklisted */
		if (blacklist != NULL &&
		    g_strv_contains ((const gchar * const *) blacklist,
				     fu_plugin_get_name (plugin))) {
			fu_plugin_set_enabled (plugin, FALSE);
			g_debug ("%s blacklisted by config",
				 fu_plugin_get_name (plugin));
			continue;
		}

		/* watch for changes */
		g_signal_connect (plugin, "device-added",
				  G_CALLBACK (fu_main_plugin_device_added_cb),
				  priv);
		g_signal_connect (plugin, "device-removed",
				  G_CALLBACK (fu_main_plugin_device_removed_cb),
				  priv);
		g_signal_connect (plugin, "status-changed",
				  G_CALLBACK (fu_main_plugin_status_changed_cb),
				  priv);
		g_signal_connect (plugin, "percentage-changed",
				  G_CALLBACK (fu_main_plugin_percentage_changed_cb),
				  priv);
		g_signal_connect (plugin, "recoldplug",
				  G_CALLBACK (fu_main_plugin_recoldplug_cb),
				  priv);
		g_signal_connect (plugin, "set-coldplug-delay",
				  G_CALLBACK (fu_main_plugin_set_coldplug_delay_cb),
				  priv);

		/* add */
		g_ptr_array_add (priv->plugins, g_object_ref (plugin));
		g_hash_table_insert (priv->plugins_hash,
				     g_strdup (fu_plugin_get_name (plugin)),
				     g_object_ref (plugin));
	}

	return TRUE;
}

/* returns FALSE if any plugins have pending devices to be added */
static gboolean
fu_main_check_plugins_pending (FuMainPrivate *priv, GError **error)
{
	for (guint i = 0; i < priv->plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index (priv->plugins, i);
		if (fu_plugin_has_device_delay (plugin)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "%s pending",
				     fu_plugin_get_name (plugin));
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_main_perhaps_own_name (gpointer user_data)
{
	FuMainPrivate *priv = (FuMainPrivate *) user_data;
	g_autoptr(GError) error = NULL;

	/* are any plugins pending */
	if (!fu_main_check_plugins_pending (priv, &error)) {
		g_debug ("trying again: %s", error->message);
		return G_SOURCE_CONTINUE;
	}

	/* own the object */
	g_debug ("registering D-Bus service");
	priv->owner_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
					 FWUPD_DBUS_SERVICE,
					 G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
					 G_BUS_NAME_OWNER_FLAGS_REPLACE,
					 fu_main_on_bus_acquired_cb,
					 fu_main_on_name_acquired_cb,
					 fu_main_on_name_lost_cb,
					 priv, NULL);
	return G_SOURCE_REMOVE;
}

int
main (int argc, char *argv[])
{
	FuMainPrivate *priv = NULL;
	gboolean immediate_exit = FALSE;
	gboolean ret;
	gboolean timed_exit = FALSE;
	GOptionContext *context;
	gint retval = 1;
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
	priv->percentage = 0;
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
	as_store_add_filter (priv->store, AS_APP_KIND_FIRMWARE);
	if (!as_store_load (priv->store,
			    AS_STORE_LOAD_FLAG_APP_INFO_SYSTEM,
			    NULL, &error)){
		g_warning ("FuMain: failed to load AppStream data: %s",
			   error->message);
		return FALSE;
	}

	/* read config file */
	config_file = g_build_filename (SYSCONFDIR, "fwupd.conf", NULL);
	g_debug ("Loading fallback values from %s", config_file);
	priv->config = g_key_file_new ();
	if (!g_key_file_load_from_file (priv->config, config_file,
					G_KEY_FILE_NONE, &error)) {
		g_print ("failed to load config file %s: %s\n",
			  config_file, error->message);
		retval = EXIT_FAILURE;
		goto out;
	}

	/* set shared USB context */
	priv->usb_ctx = g_usb_context_new (&error);
	if (priv->usb_ctx == NULL) {
		g_warning ("FuMain: failed to get USB context: %s",
			   error->message);
		goto out;
	}

	/* load plugin */
	priv->plugins = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	priv->plugins_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
					       g_free, (GDestroyNotify) g_object_unref);
	if (!fu_main_load_plugins (priv, &error)) {
		g_print ("failed to load plugins: %s\n", error->message);
		retval = EXIT_FAILURE;
		goto out;
	}

	/* disable udev? */
	if (!g_key_file_get_boolean (priv->config, "fwupd", "EnableOptionROM", NULL)) {
		FuPlugin *plugin = g_hash_table_lookup (priv->plugins_hash, "udev");
		if (plugin != NULL)
			fu_plugin_set_enabled (plugin, FALSE);
	}

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

	/* add devices */
	fu_main_plugins_setup (priv);
	g_usb_context_enumerate (priv->usb_ctx);
	fu_main_plugins_coldplug (priv);

	/* keep polling until all the plugins are ready */
	g_timeout_add (200, fu_main_perhaps_own_name, priv);

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
	if (priv != NULL) {
		if (priv->loop != NULL)
			g_main_loop_unref (priv->loop);
		if (priv->owner_id > 0)
			g_bus_unown_name (priv->owner_id);
		if (priv->proxy_uid != NULL)
			g_object_unref (priv->proxy_uid);
		if (priv->usb_ctx != NULL)
			g_object_unref (priv->usb_ctx);
		if (priv->config != NULL)
			g_key_file_unref (priv->config);
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
		if (priv->coldplug_id != 0)
			g_source_remove (priv->coldplug_id);
		if (priv->plugins != NULL)
			g_ptr_array_unref (priv->plugins);
		if (priv->plugins_hash != NULL)
			g_hash_table_unref (priv->plugins_hash);
		g_ptr_array_unref (priv->devices);
		g_free (priv);
	}
	return retval;
}

