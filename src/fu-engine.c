/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2018 Richard Hughes <richard@hughsie.com>
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

#include <appstream-glib.h>
#include <gio/gio.h>
#include <gio/gunixinputstream.h>
#include <glib-object.h>
#include <string.h>
#include <sys/utsname.h>

#include "fwupd-common-private.h"
#include "fwupd-enums-private.h"
#include "fwupd-error.h"
#include "fwupd-release-private.h"
#include "fwupd-remote-private.h"
#include "fwupd-resources.h"

#include "fu-common-cab.h"
#include "fu-common.h"
#include "fu-config.h"
#include "fu-debug.h"
#include "fu-device-list.h"
#include "fu-device-private.h"
#include "fu-engine.h"
#include "fu-hwids.h"
#include "fu-keyring.h"
#include "fu-history.h"
#include "fu-plugin.h"
#include "fu-plugin-list.h"
#include "fu-plugin-private.h"
#include "fu-quirks.h"
#include "fu-smbios.h"

#ifdef ENABLE_GPG
#include "fu-keyring-gpg.h"
#endif
#ifdef ENABLE_PKCS7
#include "fu-keyring-pkcs7.h"
#endif

static void fu_engine_finalize	 (GObject *obj);

struct _FuEngine
{
	GObject			 parent_instance;
	GUsbContext		*usb_ctx;
	FuConfig		*config;
	FuDeviceList		*device_list;
	FwupdStatus		 status;
	guint			 percentage;
	FuHistory		*history;
	AsProfile		*profile;
	AsStore			*store;
	gboolean		 coldplug_running;
	guint			 coldplug_id;
	guint			 coldplug_delay;
	FuPluginList		*plugin_list;
	GPtrArray		*supported_guids;
	FuSmbios		*smbios;
	FuHwids			*hwids;
	FuQuirks		*quirks;
};

enum {
	SIGNAL_CHANGED,
	SIGNAL_DEVICE_ADDED,
	SIGNAL_DEVICE_REMOVED,
	SIGNAL_DEVICE_CHANGED,
	SIGNAL_STATUS_CHANGED,
	SIGNAL_PERCENTAGE_CHANGED,
	SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (FuEngine, fu_engine, G_TYPE_OBJECT)

#define FU_ENGINE_REQUIREMENT_FIRMWARE_RUNTIME		NULL /* yes, NULL */
#define FU_ENGINE_REQUIREMENT_FIRMWARE_BOOTLOADER	"bootloader"
#define FU_ENGINE_REQUIREMENT_FIRMWARE_VENDOR		"vendor-id"
#define FU_ENGINE_REQUIREMENT_ID_FWUPD			"org.freedesktop.fwupd"

static void
fu_engine_emit_changed (FuEngine *self)
{
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

static void
fu_engine_emit_device_changed (FuEngine *self, FuDevice *device)
{
	g_signal_emit (self, signals[SIGNAL_DEVICE_CHANGED], 0, device);
}

/**
 * fu_engine_get_status:
 * @self: A #FuEngine
 *
 * Gets the current engine status.
 *
 * Returns: a #FwupdStatus, e.g. %FWUPD_STATUS_DECOMPRESSING
 **/
FwupdStatus
fu_engine_get_status (FuEngine *self)
{
	g_return_val_if_fail (FU_IS_ENGINE (self), 0);
	return self->status;
}

/**
 * fu_engine_profile_dump:
 * @self: A #FuEngine
 *
 * Dumps the engine profiling state to the console.
 **/
void
fu_engine_profile_dump (FuEngine *self)
{
	g_return_if_fail (FU_IS_ENGINE (self));
	as_profile_set_duration_min (self->profile, 1);
	as_profile_dump (self->profile);
}

static void
fu_engine_set_status (FuEngine *self, FwupdStatus status)
{
	if (self->status == status)
		return;
	self->status = status;

	/* emit changed */
	g_debug ("Emitting PropertyChanged('Status'='%s')",
		 fwupd_status_to_string (status));
	g_signal_emit (self, signals[SIGNAL_STATUS_CHANGED], 0, status);
}

static void
fu_engine_set_percentage (FuEngine *self, guint percentage)
{
	if (self->percentage == percentage)
		return;
	self->percentage = percentage;

	/* emit changed */
	g_signal_emit (self, signals[SIGNAL_PERCENTAGE_CHANGED], 0, percentage);
}

static void
fu_engine_progress_notify_cb (FuDevice *device, GParamSpec *pspec, FuEngine *self)
{
	fu_engine_set_percentage (self, fu_device_get_progress (device));
}

static void
fu_engine_status_notify_cb (FuDevice *device, GParamSpec *pspec, FuEngine *self)
{
	fu_engine_set_status (self, fu_device_get_status (device));
}

static void
fu_engine_watch_device (FuEngine *self, FuDevice *device)
{
	FuDevice *device_old = fu_device_list_get_old (self->device_list, device);
	if (device_old != NULL) {
		g_signal_handlers_disconnect_by_func (device_old,
						      fu_engine_progress_notify_cb,
						      self);
		g_signal_handlers_disconnect_by_func (device_old,
						      fu_engine_status_notify_cb,
						      self);
	}
	g_signal_connect (device, "notify::progress",
			  G_CALLBACK (fu_engine_progress_notify_cb), self);
	g_signal_connect (device, "notify::status",
			  G_CALLBACK (fu_engine_status_notify_cb), self);
}

static void
fu_engine_device_added_cb (FuDeviceList *device_list, FuDevice *device, FuEngine *self)
{
	fu_engine_watch_device (self, device);
	g_signal_emit (self, signals[SIGNAL_DEVICE_ADDED], 0, device);
}

static void
fu_engine_device_removed_cb (FuDeviceList *device_list, FuDevice *device, FuEngine *self)
{
	g_signal_handlers_disconnect_by_data (device, self);
	g_signal_emit (self, signals[SIGNAL_DEVICE_REMOVED], 0, device);
}

static void
fu_engine_device_changed_cb (FuDeviceList *device_list, FuDevice *device, FuEngine *self)
{
	fu_engine_watch_device (self, device);
	fu_engine_emit_device_changed (self, device);
}

static const gchar *
_as_release_get_metadata_item (AsRelease *release, const gchar *key)
{
	GBytes *blob = as_release_get_blob (release, key);
	if (blob == NULL)
		return NULL;
	return (const gchar *) g_bytes_get_data (blob, NULL);
}

static void
fu_engine_set_release_from_appstream (FuEngine *self,
				      FwupdRelease *rel,
				      AsApp *app,
				      AsRelease *release)
{
	AsChecksum *csum;
	FwupdRemote *remote = NULL;
	const gchar *tmp;
	const gchar *remote_id;

	/* set from the AsApp */
	fwupd_release_set_appstream_id (rel, as_app_get_id (app));
	fwupd_release_set_homepage (rel, as_app_get_url_item (app, AS_URL_KIND_HOMEPAGE));
	fwupd_release_set_license (rel, as_app_get_project_license (app));
	fwupd_release_set_name (rel, as_app_get_name (app, NULL));
	fwupd_release_set_summary (rel, as_app_get_comment (app, NULL));
	fwupd_release_set_vendor (rel, as_app_get_developer_name (app, NULL));
	fwupd_release_set_appstream_id (rel, as_app_get_id (app));

	/* find the remote */
	remote_id = _as_release_get_metadata_item (release, "fwupd::RemoteId");
	if (remote_id != NULL) {
		fwupd_release_set_remote_id (rel, remote_id);
		remote = fu_config_get_remote_by_id (self->config, remote_id);
		if (remote == NULL) {
			g_warning ("no remote found for release %s",
				   as_release_get_version (release));
		}
	}

	tmp = as_release_get_version (release);
	if (tmp != NULL)
		fwupd_release_set_version (rel, tmp);
	tmp = as_release_get_description (release, NULL);
	if (tmp != NULL)
		fwupd_release_set_description (rel, tmp);
	tmp = as_release_get_location_default (release);
	if (tmp != NULL) {
		g_autofree gchar *uri = NULL;
		if (remote != NULL)
			uri = fwupd_remote_build_firmware_uri (remote, tmp, NULL);
		if (uri == NULL)
			uri = g_strdup (tmp);
		fwupd_release_set_uri (rel, uri);
	}
	csum = as_release_get_checksum_by_target (release, AS_CHECKSUM_TARGET_CONTENT);
	if (csum != NULL) {
		tmp = as_checksum_get_filename (csum);
		if (tmp != NULL)
			fwupd_release_set_filename (rel, tmp);
	}
	csum = as_release_get_checksum_by_target (release, AS_CHECKSUM_TARGET_CONTAINER);
	if (csum != NULL) {
		tmp = as_checksum_get_value (csum);
		if (tmp != NULL)
			fwupd_release_add_checksum (rel, tmp);
	}
	fwupd_release_set_size (rel, as_release_get_size (release, AS_SIZE_KIND_INSTALLED));
}

static FuKeyring *
fu_engine_get_keyring_for_kind (FwupdKeyringKind kind, GError **error)
{
	if (kind == FWUPD_KEYRING_KIND_GPG) {
#ifdef ENABLE_GPG
		return fu_keyring_gpg_new ();
#else
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "Not compiled with GPG support");
		return NULL;
#endif
	}
	if (kind == FWUPD_KEYRING_KIND_PKCS7) {
#ifdef ENABLE_PKCS7
		return fu_keyring_pkcs7_new ();
#else
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "Not compiled with PKCS7 support");
		return NULL;
#endif
	}
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_SUPPORTED,
		     "Keyring kind %s not supported",
		     fwupd_keyring_kind_to_string (kind));
	return NULL;
}

/* finds the remote-id for the first firmware in the store that matches this
 * container checksum */
static const gchar *
fu_engine_get_remote_id_for_checksum (FuEngine *self, const gchar *csum)
{
	GPtrArray *array = as_store_get_apps (self->store);
	for (guint i = 0; i < array->len; i++) {
		AsApp *app = g_ptr_array_index (array, i);
		GPtrArray *releases = as_app_get_releases (app);
		for (guint j = 0; j < releases->len; j++) {
			AsRelease *release = g_ptr_array_index (releases, j);
			AsChecksum *checksum;
			checksum = as_release_get_checksum_by_target (release,
								      AS_CHECKSUM_TARGET_CONTAINER);
			if (checksum == NULL)
				continue;
			if (g_strcmp0 (csum, as_checksum_get_value (checksum)) == 0)
				return _as_release_get_metadata_item (release, "fwupd::RemoteId");
		}
	}
	return NULL;
}

static gboolean
fu_engine_get_release_trust_flags (AsRelease *release,
				 FwupdTrustFlags *trust_flags,
				 GError **error)
{
	AsChecksum *csum_tmp;
	FwupdKeyringKind keyring_kind = FWUPD_KEYRING_KIND_UNKNOWN;
	GBytes *blob_payload;
	GBytes *blob_signature;
	const gchar *fn;
	g_autofree gchar *pki_dir = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuKeyring) kr = NULL;
	g_autoptr(FuKeyringResult) kr_result = NULL;
	struct {
		FwupdKeyringKind kind;
		const gchar *ext;
	} keyrings[] = {
		{ FWUPD_KEYRING_KIND_GPG,	"asc" },
		{ FWUPD_KEYRING_KIND_PKCS7,	"p7b" },
		{ FWUPD_KEYRING_KIND_PKCS7,	"p7c" },
		{ FWUPD_KEYRING_KIND_NONE,	NULL }
	};

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
	for (guint i = 0; keyrings[i].ext != NULL; i++) {
		g_autofree gchar *fn_tmp = g_strdup_printf ("%s.%s", fn, keyrings[i].ext);
		blob_signature = as_release_get_blob (release, fn_tmp);
		if (blob_signature != NULL) {
			keyring_kind = keyrings[i].kind;
			break;
		}
	}
	if (keyring_kind == FWUPD_KEYRING_KIND_UNKNOWN) {
		g_debug ("firmware archive contained no signature");
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
	kr = fu_engine_get_keyring_for_kind (keyring_kind, error);
	if (kr == NULL)
		return FALSE;
	if (!fu_keyring_setup (kr, error)) {
		g_prefix_error (error, "failed to set up %s keyring: ",
				fu_keyring_get_name (kr));
		return FALSE;
	}
	if (!fu_keyring_add_public_keys (kr, pki_dir, error)) {
		g_prefix_error (error, "failed to add public keys to %s keyring: ",
				fu_keyring_get_name (kr));
		return FALSE;
	}
	kr_result = fu_keyring_verify_data (kr, blob_payload, blob_signature, &error_local);
	if (kr_result == NULL) {
		g_warning ("untrusted as failed to verify from %s keyring: %s",
			   fu_keyring_get_name (kr),
			   error_local->message);
		return TRUE;
	}

	/* awesome! */
	g_debug ("marking payload as trusted");
	*trust_flags |= FWUPD_TRUST_FLAG_PAYLOAD;
	return TRUE;
}

/**
 * fu_engine_unlock:
 * @self: A #FuEngine
 * @device_id: A device ID
 * @error: A #GError, or %NULL
 *
 * Unlocks a device.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_unlock (FuEngine *self, const gchar *device_id, GError **error)
{
	FuDevice *device;
	FuPlugin *plugin;

	g_return_val_if_fail (FU_IS_ENGINE (self), FALSE);
	g_return_val_if_fail (device_id != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check the device exists */
	device = fu_device_list_get_by_id (self->device_list, device_id, error);
	if (device == NULL)
		return FALSE;

	/* get the plugin */
	plugin = fu_plugin_list_find_by_name (self->plugin_list,
					      fu_device_get_plugin (device),
					      error);
	if (plugin == NULL)
		return FALSE;

	/* run the correct plugin that added this */
	if (!fu_plugin_runner_unlock (plugin, device, error))
		return FALSE;

	/* make the UI update */
	fu_engine_emit_device_changed (self, device);
	fu_engine_emit_changed (self);
	return TRUE;
}

static AsApp *
fu_engine_verify_update_device_to_app (FuDevice *device)
{
	AsApp *app = NULL;
	GPtrArray *checksums;
	g_autofree gchar *id = NULL;
	g_autoptr(AsFormat) format = NULL;
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
	checksums = fu_device_get_checksums (device);
	for (guint j = 0; j < checksums->len; j++) {
		const gchar *checksum = g_ptr_array_index (checksums, j);
		g_autoptr(AsChecksum) csum = as_checksum_new ();
		as_checksum_set_kind (csum, fwupd_checksum_guess_kind (checksum));
		as_checksum_set_value (csum, checksum);
		as_checksum_set_target (csum, AS_CHECKSUM_TARGET_CONTENT);
		as_release_add_checksum (rel, csum);
	}
	as_app_add_release (app, rel);
	prov = as_provide_new ();
	as_provide_set_kind (prov, AS_PROVIDE_KIND_FIRMWARE_FLASHED);
	as_provide_set_value (prov, fu_device_get_guid_default (device));
	as_app_add_provide (app, prov);
	format = as_format_new ();
	as_format_set_kind (format, AS_FORMAT_KIND_UNKNOWN);
	as_app_add_format (app, format);
	return app;
}

static AsStore *
fu_engine_load_verify_store (GError **error)
{
	const gchar *fn = "/var/lib/fwupd/verify.xml";
	g_autoptr(AsStore) store = NULL;
	g_autoptr(GFile) file = NULL;

	/* load existing store */
	store = as_store_new ();
	as_store_set_api_version (store, 0.9);
	file = g_file_new_for_path (fn);
	if (g_file_query_exists (file, NULL)) {
		if (!as_store_from_file (store, file, NULL, NULL, error))
			return NULL;
	}
	return g_steal_pointer (&store);
}

/**
 * fu_engine_modify_remote:
 * @self: A #FuEngine
 * @remote_id: A remote ID
 * @key: the key, e.g. `Enabled`
 * @value: the key, e.g. `true`
 * @error: A #GError, or %NULL
 *
 * Updates the verification store entry for a specific device.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_modify_remote (FuEngine *self,
			 const gchar *remote_id,
			 const gchar *key,
			 const gchar *value,
			 GError **error)
{
	FwupdRemote *remote;
	const gchar *filename;
	const gchar *keys[] = { "Enabled", "MetadataURI", "FirmwareBaseURI", NULL };
	g_autoptr(GKeyFile) keyfile = g_key_file_new ();

	/* check remote is valid */
	remote = fu_config_get_remote_by_id (self->config, remote_id);
	if (remote == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "remote %s not found", remote_id);
		return FALSE;
	}

	/* check keys are valid */
	if (!g_strv_contains (keys, key)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "key %s not supported", key);
		return FALSE;
	}

	/* modify the remote filename */
	filename = fwupd_remote_get_filename_source (remote);
	if (!g_key_file_load_from_file (keyfile, filename,
					G_KEY_FILE_KEEP_COMMENTS,
					error)) {
		g_prefix_error (error, "failed to load %s: ", filename);
		return FALSE;
	}
	g_key_file_set_string (keyfile, "fwupd Remote", key, value);
	return g_key_file_save_to_file (keyfile, filename, error);
}

/**
 * fu_engine_modify_device:
 * @self: A #FuEngine
 * @device_id: A device ID
 * @key: the key, e.g. `Flags`
 * @value: the key, e.g. `reported`
 * @error: A #GError, or %NULL
 *
 * Sets the reported flag for a specific device. This ensures that other
 * front-end clients for fwupd do not report the same event.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_modify_device (FuEngine *self,
			 const gchar *device_id,
			 const gchar *key,
			 const gchar *value,
			 GError **error)
{
	g_autoptr(FuDevice) device = NULL;

	/* find the correct device */
	device = fu_history_get_device_by_id (self->history, device_id, error);
	if (device == NULL)
		return FALSE;

	/* support adding a subset of the device flags */
	if (g_strcmp0 (key, "Flags") == 0) {
		FwupdDeviceFlags flag = fwupd_device_flag_from_string (value);
		if (flag == FWUPD_DEVICE_FLAG_UNKNOWN) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "key %s not a valid flag", key);
			return FALSE;
		}
		if (fu_device_has_flag (device, flag))
			return TRUE;
		if (flag != FWUPD_DEVICE_FLAG_REPORTED &&
		    flag != FWUPD_DEVICE_FLAG_NOTIFIED) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "flag %s cannot be set from client", key);
			return FALSE;
		}
		fu_device_add_flag (device, flag);
		return fu_history_modify_device (self->history, device,
						 FU_HISTORY_FLAGS_MATCH_OLD_VERSION |
						 FU_HISTORY_FLAGS_MATCH_NEW_VERSION,
						 error);
	}

	/* others invalid */
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_SUPPORTED,
		     "key %s not supported", key);
	return FALSE;
}

/**
 * fu_engine_verify_update:
 * @self: A #FuEngine
 * @device_id: A device ID
 * @error: A #GError, or %NULL
 *
 * Updates the verification store entry for a specific device.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_verify_update (FuEngine *self, const gchar *device_id, GError **error)
{
	FuDevice *device;
	FuPlugin *plugin;
	GPtrArray *checksums;
	const gchar *fn = "/var/lib/fwupd/verify.xml";
	g_autoptr(AsApp) app = NULL;
	g_autoptr(AsStore) store = NULL;
	g_autoptr(GFile) file = NULL;

	g_return_val_if_fail (FU_IS_ENGINE (self), FALSE);
	g_return_val_if_fail (device_id != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check the devices still exists */
	device = fu_device_list_get_by_id (self->device_list, device_id, error);
	if (device == NULL)
		return FALSE;

	/* get the plugin */
	plugin = fu_plugin_list_find_by_name (self->plugin_list,
					      fu_device_get_plugin (device),
					      error);
	if (plugin == NULL)
		return FALSE;

	/* get the checksum */
	checksums = fu_device_get_checksums (device);
	if (checksums->len == 0) {
		if (!fu_plugin_runner_verify (plugin, device,
					      FU_PLUGIN_VERIFY_FLAG_NONE,
					      error))
			return FALSE;
		fu_engine_emit_device_changed (self, device);
	}

	/* we got nothing */
	if (checksums->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "device verification not supported");
		return FALSE;
	}

	/* load existing store */
	store = fu_engine_load_verify_store (error);
	if (store == NULL)
		return FALSE;

	/* add to store */
	app = fu_engine_verify_update_device_to_app (device);
	as_store_remove_app_by_id (store, as_app_get_id (app));
	as_store_add_app (store, app);

	/* write */
	g_debug ("writing %s", fn);
	file = g_file_new_for_path (fn);
	return as_store_to_file (store, file,
				 AS_NODE_TO_XML_FLAG_ADD_HEADER |
				 AS_NODE_TO_XML_FLAG_FORMAT_INDENT |
				 AS_NODE_TO_XML_FLAG_FORMAT_MULTILINE,
				 NULL, error);
}

static AsApp *
fu_engine_store_get_app_by_guids (AsStore *store, FuDevice *device)
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

/**
 * fu_engine_verify:
 * @self: A #FuEngine
 * @device_id: A device ID
 * @error: A #GError, or %NULL
 *
 * Verifies a device firmware checksum using the verification store entry.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_verify (FuEngine *self, const gchar *device_id, GError **error)
{
	AsApp *app;
	AsChecksum *csum;
	AsRelease *release;
	FuDevice *device = NULL;
	FuPlugin *plugin;
	GPtrArray *checksums;
	const gchar *hash = NULL;
	const gchar *version = NULL;
	g_autoptr(AsStore) store = NULL;

	g_return_val_if_fail (FU_IS_ENGINE (self), FALSE);
	g_return_val_if_fail (device_id != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check the id exists */
	device = fu_device_list_get_by_id (self->device_list, device_id, error);
	if (device == NULL)
		return FALSE;

	/* get the plugin */
	plugin = fu_plugin_list_find_by_name (self->plugin_list,
					      fu_device_get_plugin (device),
					      error);
	if (plugin == NULL)
		return FALSE;

	/* set the device firmware hash */
	if (!fu_plugin_runner_verify (plugin, device,
				      FU_PLUGIN_VERIFY_FLAG_NONE, error))
		return FALSE;

	/* find component in metadata */
	store = fu_engine_load_verify_store (error);
	if (store == NULL)
		return FALSE;
	app = fu_engine_store_get_app_by_guids (store, device);
	if (app == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "No metadata");
		return FALSE;
	}

	/* find version in metadata */
	version = fu_device_get_version (device);
	release = as_app_get_release (app, version);
	if (release == NULL) {
		/* try again with the system metadata */
		app = fu_engine_store_get_app_by_guids (self->store, device);
		if (app == NULL) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_FOUND,
					     "No system metadata");
			return FALSE;
		}
		release = as_app_get_release (app, version);
	}
	if (release == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "No version %s", version);
		return FALSE;
	}

	/* find checksum */
	csum = as_release_get_checksum_by_target (release, AS_CHECKSUM_TARGET_CONTENT);
	if (csum == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "No content checksum for %s", version);
		return FALSE;
	}

	/* get the matching checksum */
	checksums = fu_device_get_checksums (device);
	if (checksums->len == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "No device checksums for %s", version);
		return FALSE;
	}
	for (guint j = 0; j < checksums->len; j++) {
		const gchar *hash_tmp = g_ptr_array_index (checksums, j);
		GChecksumType hash_kind = fwupd_checksum_guess_kind (hash_tmp);
		if (as_checksum_get_kind (csum) == hash_kind) {
			hash = hash_tmp;
			break;
		}
	}
	if (hash == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "No matching hash kind for %s", version);
		return FALSE;
	}
	if (g_strcmp0 (as_checksum_get_value (csum), hash) != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "For v%s expected %s, got %s",
			     version,
			     as_checksum_get_value (csum),
			     hash);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static AsScreenshot *
_as_app_get_screenshot_default (AsApp *app)
{
#if AS_CHECK_VERSION(0,7,3)
	return as_app_get_screenshot_default (app);
#else
	GPtrArray *array = as_app_get_screenshots (app);
	if (array->len == 0)
		return NULL;
	return g_ptr_array_index (array, 0);
#endif
}

static GPtrArray *
_as_store_get_apps_by_provide (AsStore *store, AsProvideKind kind, const gchar *value)
{
#if AS_CHECK_VERSION(0,7,5)
	return as_store_get_apps_by_provide (store, kind, value);
#else
	GPtrArray *apps = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	GPtrArray *array = as_store_get_apps (store);
	for (guint i = 0; i < array->len; i++) {
		AsApp *app = g_ptr_array_index (array, i);
		GPtrArray *provides = as_app_get_provides (app);
		for (guint j = 0; j < provides->len; j++) {
			AsProvide *tmp = g_ptr_array_index (provides, j);
			if (kind != as_provide_get_kind (tmp))
				continue;
			if (g_strcmp0 (as_provide_get_value (tmp), value) != 0)
				continue;
			g_ptr_array_add (apps, g_object_ref (app));
		}
	}
	return apps;
#endif
}

static gboolean
fu_engine_check_version_requirement (AsApp *app,
				   AsRequireKind kind,
				   const gchar *id,
				   const gchar *version,
				   GError **error)
{
	AsRequire *req;
	g_autoptr(GError) error_local = NULL;

	/* check args */
	if (version == NULL) {
		g_debug ("no parameter given for %s{%s}",
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
	if (!as_require_version_compare (req, version, &error_local)) {

		/* firmware */
		if (g_strcmp0 (id, FU_ENGINE_REQUIREMENT_FIRMWARE_RUNTIME) == 0) {
			if (as_require_get_compare (req) == AS_REQUIRE_COMPARE_GE) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "Not compatible with firmware version %s, requires >= %s",
					     version, as_require_get_version (req));
			} else {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "Not compatible with firmware version: %s",
					     error_local->message);
			}
			return FALSE;
		}

		/* fwupd */
		if (g_strcmp0 (id, FU_ENGINE_REQUIREMENT_ID_FWUPD) == 0) {
			if (as_require_get_compare (req) == AS_REQUIRE_COMPARE_GE) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "Not compatible with fwupd version %s, requires >= %s",
					     version, as_require_get_version (req));
			} else {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "Not compatible with fwupd version: %s",
					     error_local->message);
			}
			return FALSE;
		}

		/* bootloader */
		if (g_strcmp0 (id, FU_ENGINE_REQUIREMENT_FIRMWARE_BOOTLOADER) == 0) {
			if (as_require_get_compare (req) == AS_REQUIRE_COMPARE_GE) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "Not compatible with bootloader version %s, requires >= %s",
					     version, as_require_get_version (req));
			} else {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "Not compatible with bootloader version: %s",
					     error_local->message);
			}
			return FALSE;
		}

		/* vendor */
		if (g_strcmp0 (id, FU_ENGINE_REQUIREMENT_FIRMWARE_VENDOR) == 0) {
			if (as_require_get_compare (req) == AS_REQUIRE_COMPARE_GE) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "Not compatible with vendor %s, requires >= %s",
					     version, as_require_get_version (req));
			} else {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "Not compatible with vendor: %s",
					     error_local->message);
			}
			return FALSE;
		}

		/* anything else */
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Not compatible with %s: %s",
			     id, error_local->message);
		return FALSE;
	}

	/* success */
	g_debug ("requirement %s %s %s on %s passed",
		 as_require_get_version (req),
		 as_require_compare_to_string (as_require_get_compare (req)),
		 version, id);
	return TRUE;
}

#if AS_CHECK_VERSION(0,7,4)
static gboolean
fu_engine_check_hardware_requirement (FuEngine *self, AsApp *app, GError **error)
{
	GPtrArray *requires = as_app_get_requires (app);

	/* check each HWID requirement */
	for (guint i = 0; i < requires->len; i++) {
		AsRequire *req = g_ptr_array_index (requires, i);
		if (as_require_get_kind (req) != AS_REQUIRE_KIND_HARDWARE)
			continue;
		if (!fu_hwids_has_guid (self->hwids, as_require_get_value (req))) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no HWIDs matched %s",
				     as_require_get_value (req));
			return FALSE;
		}
		g_debug ("HWID provided %s", as_require_get_value (req));
	}

	/* success */
	return TRUE;
}
#endif

static gboolean
fu_engine_check_requirements (FuEngine *self, AsApp *app, FuDevice *device, GError **error)
{
	/* make sure requirements are satisfied */
	if (!fu_engine_check_version_requirement (app,
						  AS_REQUIRE_KIND_ID,
						  FU_ENGINE_REQUIREMENT_ID_FWUPD,
						  VERSION,
						  error)) {
		return FALSE;
	}
#if AS_CHECK_VERSION(0,7,4)
	if (!fu_engine_check_hardware_requirement (self, app, error))
		return FALSE;
#endif

	if (device != NULL) {
		if (!fu_engine_check_version_requirement (app,
							  AS_REQUIRE_KIND_FIRMWARE,
							  FU_ENGINE_REQUIREMENT_FIRMWARE_RUNTIME,
							  fu_device_get_version (device),
							  error)) {
			return FALSE;
		}
		if (!fu_engine_check_version_requirement (app,
							  AS_REQUIRE_KIND_FIRMWARE,
							  FU_ENGINE_REQUIREMENT_FIRMWARE_BOOTLOADER,
							  fu_device_get_version_bootloader (device),
							  error)) {
			return FALSE;
		}
		if (!fu_engine_check_version_requirement (app,
							  AS_REQUIRE_KIND_FIRMWARE,
							  FU_ENGINE_REQUIREMENT_FIRMWARE_VENDOR,
							  fu_device_get_vendor_id (device),
							  error)) {
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static void
fu_engine_vendor_fixup_provide_value (AsApp *app)
{
	GPtrArray *provides;

	/* no quirk required */
	if (as_app_get_kind (app) != AS_APP_KIND_FIRMWARE)
		return;

	/* fix each provide to be a GUID */
	provides = as_app_get_provides (app);
	for (guint i = 0; i < provides->len; i++) {
		AsProvide *prov = g_ptr_array_index (provides, i);
		const gchar *value = as_provide_get_value (prov);
		g_autofree gchar *guid = NULL;
		if (as_provide_get_kind (prov) != AS_PROVIDE_KIND_FIRMWARE_FLASHED)
			continue;
		if (as_utils_guid_is_valid (value))
			continue;
		guid = as_utils_guid_from_string (value);
		as_provide_set_value (prov, guid);
	}
}

static void
fu_engine_vendor_quirk_release_version (FuEngine *self, AsApp *app)
{
	AsVersionParseFlag flags = AS_VERSION_PARSE_FLAG_USE_TRIPLET;
	GPtrArray *releases;
	const gchar *quirk;

	/* no quirk required */
	if (as_app_get_kind (app) != AS_APP_KIND_FIRMWARE)
		return;

	/* any quirks match */
	quirk = fu_quirks_lookup_by_glob (self->quirks,
					  FU_QUIRKS_DAEMON_VERSION_FORMAT,
					  as_app_get_id (app));
	if (g_strcmp0 (quirk, "none") == 0)
		flags = AS_VERSION_PARSE_FLAG_NONE;

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

static gchar *
fu_engine_get_guids_from_store (AsStore *store)
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
	if (str->len == 0) {
		g_string_free (str, TRUE);
		return NULL;
	}
	g_string_truncate (str, str->len - 1);
	return g_string_free (str, FALSE);
}

static FuDevice *
fu_engine_get_item_by_wildcard (FuEngine *self, AsStore *store, GError **error)
{
	g_autofree gchar *guids = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	devices = fu_device_list_get_all (self->device_list);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device_tmp = g_ptr_array_index (devices, i);
		if (fu_engine_store_get_app_by_guids (store, device_tmp) != NULL)
			return device_tmp;
	}
	guids = fu_engine_get_guids_from_store (store);
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_INVALID_FILE,
		     "Firmware is not for any attached hardware: got %s",
		     guids);
	return NULL;
}

static gchar *
fu_engine_get_boot_time (void)
{
	g_autofree gchar *buf = NULL;
	g_auto(GStrv) lines = NULL;
	if (!g_file_get_contents ("/proc/stat", &buf, NULL, NULL))
		return NULL;
	lines = g_strsplit (buf, "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		if (g_str_has_prefix (lines[i], "btime "))
			return g_strdup (lines[i] + 6);
	}
	return NULL;
}

static GHashTable *
fu_engine_get_report_metadata (FuEngine *self)
{
	GHashTable *hash;
	gchar *btime;
	struct utsname name_tmp = { 0 };

	/* used by pretty much every plugin and are hard deps of fwupd */
	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	g_hash_table_insert (hash,
			     g_strdup ("FwupdVersion"),
			     g_strdup (VERSION));
	g_hash_table_insert (hash,
			     g_strdup ("AppstreamGlibVersion"),
			     g_strdup_printf ("%i.%i.%i",
					      AS_MAJOR_VERSION,
					      AS_MINOR_VERSION,
					      AS_MICRO_VERSION));
	g_hash_table_insert (hash,
			     g_strdup ("GUsbVersion"),
			     g_strdup_printf ("%i.%i.%i",
					      G_USB_MAJOR_VERSION,
					      G_USB_MINOR_VERSION,
					      G_USB_MICRO_VERSION));

	/* kernel version is often important for debugging failures */
	if (uname (&name_tmp) >= 0) {
		g_hash_table_insert (hash,
				     g_strdup ("CpuArchitecture"),
				     g_strdup (name_tmp.machine));
		g_hash_table_insert (hash,
				     g_strdup ("KernelVersion"),
				     g_strdup (name_tmp.release));
	}

	/* add the kernel boot time so we can detect a reboot */
	btime = fu_engine_get_boot_time ();
	if (btime != NULL)
		g_hash_table_insert (hash, g_strdup ("BootTime"), btime);

	return hash;
}

/**
 * fu_engine_install:
 * @self: A #FuEngine
 * @device_id: A device ID
 * @store: The #AsStore with the firmware metadata
 * @blob_cab: The #GBytes of the .cab file
 * @flags: The #FwupdInstallFlags, e.g. %FWUPD_DEVICE_FLAG_UPDATABLE
 * @error: A #GError, or %NULL
 *
 * Installs a specfic firmware file on a device.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_install (FuEngine *self,
		   const gchar *device_id,
		   AsStore *store,
		   GBytes *blob_cab,
		   FwupdInstallFlags flags,
		   GError **error)
{
	AsApp *app;
	AsChecksum *csum_tmp;
	AsRelease *rel;
	FuDevice *device;
	FuPlugin *plugin;
	GBytes *blob_fw;
	GPtrArray *plugins;
	const gchar *tmp;
	const gchar *version;
	gboolean is_downgrade;
	gint vercmp;
	g_autofree gchar *checksum = NULL;
	g_autofree gchar *device_id_orig = NULL;
	g_autofree gchar *version_orig = NULL;
	g_autoptr(FwupdRelease) release_history = fwupd_release_new ();
	g_autoptr(GBytes) blob_fw2 = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GHashTable) metadata_hash = NULL;

	g_return_val_if_fail (FU_IS_ENGINE (self), FALSE);
	g_return_val_if_fail (device_id != NULL, FALSE);
	g_return_val_if_fail (AS_IS_STORE (store), FALSE);
	g_return_val_if_fail (blob_cab != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* wildcard */
	if (g_strcmp0 (device_id, FWUPD_DEVICE_ID_ANY) == 0) {
		device = fu_engine_get_item_by_wildcard (self, store, error);
		if (device == NULL)
			return FALSE;
	} else {
		/* find the specific device */
		device = fu_device_list_get_by_id (self->device_list, device_id, error);
		if (device == NULL)
			return FALSE;
	}

	/* check the device is not locked */
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_LOCKED)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Device %s is locked",
			     device_id);
		return FALSE;
	}

	/* no update abilities */
	if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Device %s does not currently allow updates",
			     fu_device_get_id (device));
		return FALSE;
	}

	/* called with online update, test if device is supposed to allow this */
	if ((flags & FWUPD_INSTALL_FLAG_OFFLINE) == 0 &&
	    fu_device_has_flag (device, FWUPD_DEVICE_FLAG_ONLY_OFFLINE)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Device %s only allows offline updates",
			    device_id);
		return FALSE;
	}

	/* find from guid */
	app = fu_engine_store_get_app_by_guids (store, device);
	if (app == NULL) {
		g_autofree gchar *guid = NULL;
		guid = fu_engine_get_guids_from_store (store);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Firmware is not for this hw: required %s got %s",
			     fu_device_get_guid_default (device), guid);
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

	/* possibly convert the version from 0x to dotted */
	fu_engine_vendor_quirk_release_version (self, app);

	/* possibly convert the flashed provide to a GUID */
	fu_engine_vendor_fixup_provide_value (app);

	/* check we can install it */
	if (!fu_engine_check_requirements (self, app, device, error))
		return FALSE;

	/* parse the DriverVer */
	rel = as_app_get_release_default (app);
	if (rel == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "No releases in the firmware component");
		return FALSE;
	}

	/* get the blob */
	csum_tmp = as_release_get_checksum_by_target (rel, AS_CHECKSUM_TARGET_CONTENT);
	tmp = as_checksum_get_filename (csum_tmp);
	if (tmp == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "No checksum filename");
		return FALSE;
	}

	/* not all devices have to use the same blob */
	blob_fw = as_release_get_blob (rel, tmp);
	if (blob_fw == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "Failed to get firmware blob");
		return FALSE;
	}

	/* use a bubblewrap helper script to build the firmware */
	tmp = as_app_get_metadata_item (app, "fwupd::BuilderScript");
	if (tmp != NULL) {
		const gchar *tmp2 = as_app_get_metadata_item (app, "fwupd::BuilderOutput");
		if (tmp2 == NULL)
			tmp2 = "firmware.bin";
		blob_fw2 = fu_common_firmware_builder (blob_fw, tmp, tmp2, error);
		if (blob_fw2 == NULL)
			return FALSE;
	} else {
		blob_fw2 = g_bytes_ref (blob_fw);
	}

	/* test the firmware is not an empty blob */
	if (g_bytes_get_size (blob_fw2) == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Firmware is invalid as has zero size");
		return FALSE;
	}

	version = as_release_get_version (rel);

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

	/* get the plugin */
	plugin = fu_plugin_list_find_by_name (self->plugin_list,
					      fu_device_get_plugin (device),
					      error);
	if (plugin == NULL)
		return FALSE;

	/* compare the versions of what we have installed */
	version_orig = g_strdup (fu_device_get_version (device));
	if (version_orig == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Device %s does not yet have a current version",
			     device_id);
		return FALSE;
	}
	vercmp = as_utils_vercmp (version_orig, version);
	if (vercmp == 0 && (flags & FWUPD_INSTALL_FLAG_ALLOW_REINSTALL) == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_VERSION_SAME,
			     "Specified firmware is already installed '%s'",
			     version_orig);
		return FALSE;
	}
	is_downgrade = vercmp > 0;
	if (is_downgrade && (flags & FWUPD_INSTALL_FLAG_ALLOW_OLDER) == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_VERSION_NEWER,
			     "Specified firmware is older than installed '%s < %s'",
			     version_orig, version);
		return FALSE;
	}

	/* signal to all the plugins the update is about to happen */
	plugins = fu_plugin_list_get_all (self->plugin_list);
	for (guint j = 0; j < plugins->len; j++) {
		FuPlugin *plugin_tmp = g_ptr_array_index (plugins, j);
		if (!fu_plugin_runner_update_prepare (plugin_tmp, device, error))
			return FALSE;
	}

	/* save the chosen device ID in case the device goes away */
	device_id_orig = g_strdup (fu_device_get_id (device));

	/* mark this as modified even if we actually fail to do the update */
	fu_device_set_modified (device, (guint64) g_get_real_time () / G_USEC_PER_SEC);

	/* build the version metadata */
	metadata_hash = fu_engine_get_report_metadata (self);
	fwupd_release_add_metadata (release_history, metadata_hash);
	fwupd_release_add_metadata (release_history,
				    fu_plugin_get_report_metadata (plugin));

	/* add device to database */
	checksum = g_compute_checksum_for_bytes (G_CHECKSUM_SHA1, blob_cab);
	fwupd_release_set_version (release_history, version);
	fwupd_release_add_checksum (release_history, checksum);
	fu_device_set_update_state (device, FWUPD_UPDATE_STATE_FAILED);
	if (!fu_history_add_device (self->history, device, release_history, error))
		return FALSE;

	/* do the update */
	if (!fu_plugin_runner_update_detach (plugin, device, &error_local)) {
		fu_device_set_update_error (device, error_local->message);
		if (!fu_history_modify_device (self->history, device,
					       FU_HISTORY_FLAGS_MATCH_OLD_VERSION,
					       error)) {
			return FALSE;
		}
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	device = fu_device_list_get_by_id (self->device_list, device_id_orig, error);
	if (device == NULL)
		return FALSE;
	if (!fu_plugin_runner_update (plugin,
				      device,
				      blob_cab,
				      blob_fw2,
				      flags,
				      &error_local)) {
		g_autoptr(GError) error_attach = NULL;

		/* save to database */
		fu_device_set_update_error (device, error_local->message);
		if (!fu_history_modify_device (self->history, device,
					       FU_HISTORY_FLAGS_MATCH_OLD_VERSION,
					       error)) {
			return FALSE;
		}
		g_propagate_error (error, g_steal_pointer (&error_local));

		/* attack back into runtime */
		if (!fu_plugin_runner_update_attach (plugin,
						     device,
						     &error_attach)) {
			g_warning ("failed to attach device after failed update: %s",
				   error_attach->message);
		}
		for (guint j = 0; j < plugins->len; j++) {
			FuPlugin *plugin_tmp = g_ptr_array_index (plugins, j);
			g_autoptr(GError) error_cleanup = NULL;
			if (!fu_plugin_runner_update_cleanup (plugin_tmp,
							      device,
							      &error_cleanup)) {
				g_warning ("failed to update-cleanup "
					   "after failed update: %s",
					   error_cleanup->message);
			}
		}
		fu_device_set_status (device, FWUPD_STATUS_IDLE);
		return FALSE;
	}
	device = fu_device_list_get_by_id (self->device_list, device_id_orig, error);
	if (device == NULL)
		return FALSE;
	if (!fu_plugin_runner_update_attach (plugin, device, &error_local)) {
		fu_device_set_update_error (device, error_local->message);
		if (!fu_history_modify_device (self->history, device,
					       FU_HISTORY_FLAGS_MATCH_OLD_VERSION |
					       FU_HISTORY_FLAGS_MATCH_NEW_VERSION,
					       error)) {
			return FALSE;
		}
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}

	/* get the new version number */
	device = fu_device_list_get_by_id (self->device_list, device_id_orig, error);
	if (device == NULL)
		return FALSE;
	if (!fu_plugin_runner_update_reload (plugin, device, error))
		return FALSE;

	/* signal to all the plugins the update has happened */
	for (guint j = 0; j < plugins->len; j++) {
		FuPlugin *plugin_tmp = g_ptr_array_index (plugins, j);
		g_autoptr(GError) error_cleanup = NULL;
		if (!fu_plugin_runner_update_cleanup (plugin_tmp, device, &error_cleanup)) {
			g_warning ("failed to update-cleanup: %s",
				   error_cleanup->message);
		}
	}

	/* make the UI update */
	fu_device_set_status (device, FWUPD_STATUS_IDLE);
	fu_engine_emit_device_changed (self, device);
	fu_engine_emit_changed (self);

	/* update database */
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT)) {
		fu_device_set_update_state (device, FWUPD_UPDATE_STATE_NEEDS_REBOOT);
		return fu_history_modify_device (self->history, device,
						 FU_HISTORY_FLAGS_MATCH_OLD_VERSION,
						 error);
	}

	/* for online updates, verify the version changed if not a re-install */
	if (g_strcmp0 (version_orig, version) != 0 &&
	    g_strcmp0 (version_orig, fu_device_get_version (device)) == 0) {
		fu_device_set_update_error (device, "device version not updated on success");
		return fu_history_modify_device (self->history, device,
						 FU_HISTORY_FLAGS_MATCH_OLD_VERSION,
						 error);
	}

	/* success */
	fu_device_set_update_state (device, FWUPD_UPDATE_STATE_SUCCESS);
	return fu_history_modify_device (self->history, device,
					 FU_HISTORY_FLAGS_MATCH_NEW_VERSION,
					 error);
}

static FuDevice *
fu_engine_get_item_by_id_fallback_history (FuEngine *self, const gchar *id, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;

	/* not a wildcard */
	if (g_strcmp0 (id, FWUPD_DEVICE_ID_ANY) != 0) {
		g_autoptr(FuDevice) dev = NULL;
		g_autoptr(GError) error_local = NULL;

		/* get this one device */
		dev = fu_history_get_device_by_id (self->history, id, &error_local);
		if (dev == NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO,
				     "Failed to find %s in history database: %s",
				     id, error_local->message);
			return NULL;
		}

		/* only useful */
		if (fu_device_get_update_state (dev) == FWUPD_UPDATE_STATE_SUCCESS ||
		    fu_device_get_update_state (dev) == FWUPD_UPDATE_STATE_FAILED) {
			return g_steal_pointer (&dev);
		}

		/* nothing in database */
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOTHING_TO_DO,
			     "Device %s has no results to report",
			     fu_device_get_id (dev));
		return NULL;
	}

	/* allow '*' for any */
	devices = fu_history_get_devices (self->history, error);
	if (devices == NULL)
		return NULL;
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index (devices, i);
		if (fu_device_get_update_state (dev) == FWUPD_UPDATE_STATE_SUCCESS ||
		    fu_device_get_update_state (dev) == FWUPD_UPDATE_STATE_FAILED)
			return g_object_ref (dev);
	}
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOTHING_TO_DO,
			     "Failed to find any useful results to report");
	return NULL;
}

/**
 * fu_engine_get_action_id_for_device:
 * @self: A #FuEngine
 * @device_id: A device ID
 * @store: The #AsStore with the firmware metadata
 * @flags: The #FwupdInstallFlags, e.g. %FWUPD_DEVICE_FLAG_UPDATABLE
 * @error: A #GError, or %NULL
 *
 * Gets the PolicyKit action ID to use for the install operation.
 *
 * Returns: string, e.g. `org.freedesktop.fwupd.update-internal-trusted`
 **/
const gchar *
fu_engine_get_action_id_for_device (FuEngine *self,
				    const gchar *device_id,
				    AsStore *store,
				    FwupdInstallFlags flags,
				    GError **error)
{
	AsApp *app;
	AsRelease *release;
	FuDevice *device;
	FwupdTrustFlags trust_flags = FWUPD_TRUST_FLAG_NONE;
	const gchar *version;
	const gchar *version_release;
	gboolean is_downgrade;
	gboolean is_trusted;
	gint vercmp;

	g_return_val_if_fail (FU_IS_ENGINE (self), NULL);
	g_return_val_if_fail (AS_IS_STORE (store), NULL);
	g_return_val_if_fail (device_id != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* wildcard */
	if (g_strcmp0 (device_id, FWUPD_DEVICE_ID_ANY) == 0) {
		device = fu_engine_get_item_by_wildcard (self, store, error);
		if (device == NULL)
			return NULL;
	} else {
		/* find the specific device */
		device = fu_device_list_get_by_id (self->device_list, device_id, error);
		if (device == NULL)
			return NULL;
	}

	/* get device */
	version = fu_device_get_version (device);
	if (version == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Device with ID %s has no firmware version",
			     device_id);
		return NULL;
	}

	/* match the GUIDs in the XML */
	app = fu_engine_store_get_app_by_guids (store, device);
	if (app == NULL) {
		g_autofree gchar *guid = NULL;
		guid = fu_engine_get_guids_from_store (store);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "firmware is not for this hw: required %s got %s",
			     fu_device_get_guid_default (device), guid);
		return NULL;
	}

	/* possibly convert the version from 0x to dotted */
	fu_engine_vendor_quirk_release_version (self, app);

	/* possibly convert the flashed provide to a GUID */
	fu_engine_vendor_fixup_provide_value (app);

	/* get latest release */
	release = as_app_get_release_default (app);
	if (release == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "%s [%s] has no firmware update metadata",
			     fu_device_get_id (device),
			     fu_device_get_name (device));
		return NULL;
	}

	/* is this a downgrade or re-install */
	version_release = as_release_get_version (release);
	if (version_release == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Release has no firmware version");
		return NULL;
	}
	vercmp = as_utils_vercmp (version, version_release);
	if (vercmp == 0 && (flags & FWUPD_INSTALL_FLAG_ALLOW_REINSTALL) == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_VERSION_SAME,
			     "Specified firmware is already installed '%s'",
			     version_release);
		return NULL;
	}
	is_downgrade = vercmp > 0;
	if (is_downgrade && (flags & FWUPD_INSTALL_FLAG_ALLOW_OLDER) == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_VERSION_NEWER,
			     "Specified firmware is older than installed '%s < %s'",
			     version_release, version);
		return NULL;
	}

	/* verify */
	if (!fu_engine_get_release_trust_flags (release, &trust_flags, error))
		return NULL;
	is_trusted = (trust_flags & FWUPD_TRUST_FLAG_PAYLOAD) > 0;

	/* relax authentication checks for removable devices */
	if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_INTERNAL)) {
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

static AsRelease *
_as_app_get_release_by_version (AsApp *app, const gchar *version)
{
#if AS_CHECK_VERSION(0,7,3)
	return as_app_get_release_by_version (app, version);
#else
	GPtrArray *releases = as_app_get_releases (app);
	for (guint i = 0; i < releases->len; i++) {
		AsRelease *release = g_ptr_array_index (releases, i);
		if (g_strcmp0 (version, as_release_get_version (release)) == 0)
			return release;
	}
	return NULL;
#endif
}

static void
fu_engine_add_component_to_store (FuEngine *self, AsApp *app)
{
	AsApp *app_old = as_store_get_app_by_id (self->store, as_app_get_id (app));
	GPtrArray *releases = as_app_get_releases (app);

	/* possibly convert the version from 0x to dotted */
	fu_engine_vendor_quirk_release_version (self, app);

	/* possibly convert the flashed provide to a GUID */
	fu_engine_vendor_fixup_provide_value (app);

	/* the app does not already exist */
	if (app_old == NULL) {
		as_store_add_app (self->store, app);
		return;
	}

	/* add releases that do not exist from a higher priority remote */
	for (guint j = 0; j < releases->len; j++) {
		AsRelease *release = g_ptr_array_index (releases, j);
		AsRelease *release_old;
		const gchar *version = as_release_get_version (release);
		release_old = _as_app_get_release_by_version (app_old, version);
		if (release_old != NULL) {
			g_debug ("skipping release %s that already exists for %s",
				 version, as_app_get_id (app_old));
			continue;
		}
		g_debug ("adding release %s to existing %s",
			 version, as_app_get_id (app_old));
		as_app_add_release (app_old, release);
	}
}

static gboolean
fu_engine_load_metadata_from_file (FuEngine *self,
				 const gchar *path,
				 const gchar *remote_id,
				 GError **error)
{
	GPtrArray *apps;
	g_autoptr(AsStore) store = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GBytes) remote_blob = NULL;

	/* load the store locally until we know it is valid */
	store = as_store_new ();
	file = g_file_new_for_path (path);
	if (!as_store_from_file (store, file, NULL, NULL, error))
		return FALSE;

	/* save the remote to the release */
	if (remote_id != NULL && remote_id[0] != '\0')
		remote_blob = g_bytes_new (remote_id, strlen (remote_id) + 1);

	/* add the new application from the store */
	apps = as_store_get_apps (store);
	for (guint i = 0; i < apps->len; i++) {
		AsApp *app = g_ptr_array_index (apps, i);

		/* save the remote-id to all the releases for this component */
		if (remote_blob != NULL) {
			GPtrArray *releases = as_app_get_releases (app);
			for (guint j = 0; j < releases->len; j++) {
				AsRelease *release = g_ptr_array_index (releases, j);
				as_release_set_blob (release,
						     "fwupd::RemoteId",
						     remote_blob);
			}
		}

		/* either add component, or merge in new releases */
		fu_engine_add_component_to_store (self, app);
	}
	return TRUE;
}

static gboolean
fu_engine_is_device_supported (FuEngine *self, FuDevice *device)
{
	AsApp *app;

	/* no device version */
	if (fu_device_get_version (device) == NULL)
		return FALSE;

	/* match the GUIDs in the XML */
	app = fu_engine_store_get_app_by_guids (self->store, device);
	if (app == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_engine_load_metadata_store (FuEngine *self, GError **error)
{
	GPtrArray *apps;
	GPtrArray *remotes;
	g_autofree gchar *guids_str = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	/* clear existing store */
	as_store_remove_all (self->store);

	/* load each enabled metadata file */
	remotes = fu_config_get_remotes (self->config);
	for (guint i = 0; i < remotes->len; i++) {
		const gchar *path = NULL;
		g_autoptr(GError) error_local = NULL;
		FwupdRemote *remote = g_ptr_array_index (remotes, i);
		if (!fwupd_remote_get_enabled (remote)) {
			g_debug ("remote %s not enabled, so skipping",
				 fwupd_remote_get_id (remote));
			continue;
		}
		path = fwupd_remote_get_filename_cache (remote);
		if (!g_file_test (path, G_FILE_TEST_EXISTS)) {
			g_debug ("no %s, so skipping", path);
			continue;
		}
		if (!fu_engine_load_metadata_from_file (self, path,
							fwupd_remote_get_id (remote),
							&error_local)) {
			g_warning ("failed to load remote %s: %s",
				   fwupd_remote_get_id (remote),
				   error_local->message);
			continue;
		}
	}

	/* print what we've got */
	apps = as_store_get_apps (self->store);
	if (apps->len == 0) {
		g_debug ("no devices in store");
	} else {
		g_debug ("devices now in store:");
		for (guint i = 0; i < apps->len; i++) {
			AsApp *app = g_ptr_array_index (apps, i);
			GPtrArray *releases = as_app_get_releases (app);
			g_autoptr(GString) releases_str = g_string_new (NULL);

			for (guint j = 0; j < releases->len; j++) {
				AsRelease *release = g_ptr_array_index (releases, j);
				g_string_append_printf (releases_str, "%s,",
							as_release_get_version (release));
				if (j >= 2) {
					g_string_append (releases_str, "…,");
					break;
				}
			}
			if (releases_str->len > 1)
				g_string_truncate (releases_str, releases_str->len - 1);
			g_debug ("%u\t%s\t%s [%s]", i + 1,
				 as_app_get_id (app),
				 as_app_get_name (app, NULL),
				 releases_str->str);
		}
	}

	/* update the list of supported GUIDs */
	g_ptr_array_set_size (self->supported_guids, 0);
	guids_str = fu_engine_get_guids_from_store (self->store);
	if (guids_str != NULL) {
		g_auto(GStrv) guids = g_strsplit (guids_str, ",", -1);
		for (guint i = 0; guids[i] != NULL; i++) {
			g_ptr_array_add (self->supported_guids,
					 g_steal_pointer (&guids[i]));
		}
	}

	/* did any devices SUPPORTED state change? */
	devices = fu_device_list_get_all (self->device_list);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index (devices, i);
		if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_SUPPORTED)) {
			if (!fu_engine_is_device_supported (self, device)) {
				/* was supported, now unsupported */
				fu_device_remove_flag (device, FWUPD_DEVICE_FLAG_SUPPORTED);
				fu_engine_emit_device_changed (self, device);
			}
		} else {
			/* was unsupported, now supported */
			if (fu_engine_is_device_supported (self, device)) {
				fu_device_add_flag (device, FWUPD_DEVICE_FLAG_SUPPORTED);
				fu_engine_emit_device_changed (self, device);
			}
		}
	}

	return TRUE;
}

static FuKeyringResult *
fu_engine_get_existing_keyring_result (FuEngine *self,
				       FuKeyring *kr,
				       FwupdRemote *remote,
				       GError **error)
{
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) blob_sig = NULL;
	blob = fu_common_get_contents_bytes (fwupd_remote_get_filename_cache (remote), error);
	if (blob == NULL)
		return NULL;
	blob_sig = fu_common_get_contents_bytes (fwupd_remote_get_filename_cache_sig (remote), error);
	if (blob_sig == NULL)
		return NULL;
	return fu_keyring_verify_data (kr, blob, blob_sig, error);
}

/**
 * fu_engine_update_metadata:
 * @self: A #FuEngine
 * @remote_id: A remote ID, e.g. `lvfs`
 * @fd: file descriptor of the metadata
 * @fd_sig: file descriptor of the metadata signature
 * @error: A #GError, or %NULL
 *
 * Updates the metadata for a specific remote.
 *
 * Note: this will close the fds when done
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_update_metadata (FuEngine *self, const gchar *remote_id,
			   gint fd, gint fd_sig, GError **error)
{
	FwupdKeyringKind keyring_kind;
	FwupdRemote *remote;
	g_autoptr(GBytes) bytes_raw = NULL;
	g_autoptr(GBytes) bytes_sig = NULL;
	g_autoptr(GInputStream) stream_fd = NULL;
	g_autoptr(GInputStream) stream_sig = NULL;

	g_return_val_if_fail (FU_IS_ENGINE (self), FALSE);
	g_return_val_if_fail (remote_id != NULL, FALSE);
	g_return_val_if_fail (fd > 0, FALSE);
	g_return_val_if_fail (fd_sig > 0, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensures the fd's are closed on error */
	stream_fd = g_unix_input_stream_new (fd, TRUE);
	stream_sig = g_unix_input_stream_new (fd_sig, TRUE);

	/* check remote is valid */
	remote = fu_config_get_remote_by_id (self->config, remote_id);
	if (remote == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "remote %s not found", remote_id);
		return FALSE;
	}
	if (!fwupd_remote_get_enabled (remote)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "remote %s not enabled", remote_id);
		return FALSE;
	}

	/* read the entire file into memory */
	bytes_raw = g_input_stream_read_bytes (stream_fd, 0x100000, NULL, error);
	if (bytes_raw == NULL)
		return FALSE;

	/* read signature */
	bytes_sig = g_input_stream_read_bytes (stream_sig, 0x800, NULL, error);
	if (bytes_sig == NULL)
		return FALSE;

	/* verify file */
	keyring_kind = fwupd_remote_get_keyring_kind (remote);
	if (keyring_kind != FWUPD_KEYRING_KIND_NONE) {
		g_autoptr(FuKeyring) kr = NULL;
		g_autoptr(FuKeyringResult) kr_result = NULL;
		g_autoptr(FuKeyringResult) kr_result_old = NULL;
		g_autoptr(GError) error_local = NULL;
		kr = fu_engine_get_keyring_for_kind (keyring_kind, error);
		if (kr == NULL)
			return FALSE;
		if (!fu_keyring_setup (kr, error))
			return FALSE;
		if (!fu_keyring_add_public_keys (kr, "/etc/pki/fwupd-metadata", error))
			return FALSE;
		kr_result = fu_keyring_verify_data (kr, bytes_raw, bytes_sig, error);
		if (kr_result == NULL)
			return FALSE;

		/* verify the metadata was signed later than the existing
		 * metadata for this remote to mitigate a rollback attack */
		kr_result_old = fu_engine_get_existing_keyring_result (self, kr,
								       remote,
								       &error_local);
		if (kr_result_old == NULL) {
			if (g_error_matches (error_local,
					     G_FILE_ERROR,
					     G_FILE_ERROR_NOENT)) {
				g_debug ("no existing valid keyrings: %s",
					 error_local->message);
			} else {
				g_warning ("could not get existing keyring result: %s",
					   error_local->message);
			}
		} else {
			gint64 delta = 0;
			if (fu_keyring_result_get_timestamp (kr_result) > 0 &&
			    fu_keyring_result_get_timestamp (kr_result_old) > 0) {
				delta = fu_keyring_result_get_timestamp (kr_result) -
					fu_keyring_result_get_timestamp (kr_result_old);
			}
			if (delta < 0) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "new signing timestamp was %"
					     G_GINT64_FORMAT " seconds older",
					     -delta);
				return FALSE;
			} else if (delta > 0) {
				g_debug ("timestamp increased, so no rollback");
			}
		}
	}

	/* save XML and signature to remotes.d */
	if (!fu_common_set_contents_bytes (fwupd_remote_get_filename_cache (remote),
					   bytes_raw, error))
		return FALSE;
	if (keyring_kind != FWUPD_KEYRING_KIND_NONE) {
		if (!fu_common_set_contents_bytes (fwupd_remote_get_filename_cache_sig (remote),
						   bytes_sig, error))
			return FALSE;
	}
	return fu_engine_load_metadata_store (self, error);
}

/**
 * fu_engine_get_store_from_blob:
 * @self: A #FuEngine
 * @blob_cab: A #GBytes
 * @error: A #GError, or %NULL
 *
 * Creates an AppStream store from a .cab file blob.
 *
 * Returns: (transfer container): a #AsStore, or %NULL
 **/
AsStore *
fu_engine_get_store_from_blob (FuEngine *self, GBytes *blob_cab, GError **error)
{
	g_autofree gchar *checksum = NULL;
	g_autoptr(AsStore) store = NULL;

	g_return_val_if_fail (FU_IS_ENGINE (self), NULL);
	g_return_val_if_fail (blob_cab != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* load file */
	fu_engine_set_status (self, FWUPD_STATUS_DECOMPRESSING);
	store = fu_common_store_from_cab_bytes (blob_cab,
						fu_engine_get_archive_size_max (self),
						error);
	if (store == NULL)
		return NULL;

	/* get a checksum of the file and use it as the origin */
	checksum = g_compute_checksum_for_bytes (G_CHECKSUM_SHA256, blob_cab);
	as_store_set_origin (store, checksum);

	fu_engine_set_status (self, FWUPD_STATUS_IDLE);
	return g_steal_pointer (&store);
}

static FwupdDevice *
fu_engine_get_result_from_app (FuEngine *self, AsApp *app, GError **error)
{
	FwupdTrustFlags trust_flags = FWUPD_TRUST_FLAG_NONE;
	AsRelease *release;
	GPtrArray *provides;
	g_autoptr(FwupdDevice) dev = NULL;
	g_autoptr(FwupdRelease) rel = NULL;

	dev = fwupd_device_new ();
	provides = as_app_get_provides (app);
	for (guint i = 0; i < provides->len; i++) {
		AsProvide *prov = AS_PROVIDE (g_ptr_array_index (provides, i));
		FuDevice *device;
		const gchar *guid;

		/* not firmware */
		if (as_provide_get_kind (prov) != AS_PROVIDE_KIND_FIRMWARE_FLASHED)
			continue;

		/* is a online or offline update appropriate */
		guid = as_provide_get_value (prov);
		if (guid == NULL)
			continue;
		device = fu_device_list_get_by_guid (self->device_list, guid, NULL);
		if (device != NULL) {
			fwupd_device_set_name (dev, fu_device_get_name (device));
			fwupd_device_set_flags (dev, fu_device_get_flags (device));
			fwupd_device_set_id (dev, fu_device_get_id (device));
		}

		/* add GUID */
		fwupd_device_add_guid (dev, guid);
	}
	if (fwupd_device_get_guids(dev)->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "component has no GUIDs");
		return NULL;
	}

	/* check we can install it */
	if (!fu_engine_check_requirements (self, app, NULL, error))
		return NULL;

	/* verify trust */
	release = as_app_get_release_default (app);
	if (!fu_engine_get_release_trust_flags (release, &trust_flags, error))
		return NULL;

	/* possibly convert the version from 0x to dotted */
	fu_engine_vendor_quirk_release_version (self, app);

	/* possibly convert the flashed provide to a GUID */
	fu_engine_vendor_fixup_provide_value (app);

	/* create a result with all the metadata in */
	fwupd_device_set_description (dev, as_app_get_description (app, NULL));
	rel = fwupd_release_new ();
	fwupd_release_set_trust_flags (rel, trust_flags);
	fu_engine_set_release_from_appstream (self, rel, app, release);
	fwupd_device_add_release (dev, rel);
	return g_steal_pointer (&dev);
}

/**
 * fu_engine_get_details:
 * @self: A #FuEngine
 * @fd: A file descriptor
 * @error: A #GError, or %NULL
 *
 * Gets the details about a local file.
 *
 * Note: this will close the fd when done
 *
 * Returns: (transfer container) (element-type FwupdDevice): results
 **/
GPtrArray *
fu_engine_get_details (FuEngine *self, gint fd, GError **error)
{
	GPtrArray *apps;
	const gchar *remote_id;
	g_autofree gchar *csum = NULL;
	g_autoptr(AsStore) store = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GPtrArray) details = NULL;

	g_return_val_if_fail (FU_IS_ENGINE (self), NULL);
	g_return_val_if_fail (fd > 0, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* get all apps */
	blob = fu_common_get_contents_fd (fd,
					  fu_engine_get_archive_size_max (self),
					  error);
	if (blob == NULL)
		return NULL;
	store = fu_engine_get_store_from_blob (self, blob, error);
	if (store == NULL)
		return NULL;
	apps = as_store_get_apps (store);
	if (apps->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no components");
		return NULL;
	}

	/* does this exist in any enabled remote */
	csum = g_compute_checksum_for_bytes (G_CHECKSUM_SHA1, blob);
	remote_id = fu_engine_get_remote_id_for_checksum (self, csum);

	/* create results with all the metadata in */
	details = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (guint i = 0; i < apps->len; i++) {
		AsApp *app = g_ptr_array_index (apps, i);
		FwupdDevice *dev;

		/* check we can install it */
		if (!fu_engine_check_requirements (self, app, NULL, error))
			return NULL;

		as_app_set_origin (app, as_store_get_origin (store));
		dev = fu_engine_get_result_from_app (self, app, error);
		if (dev == NULL)
			return NULL;
		if (remote_id != NULL) {
			FwupdRelease *rel = fwupd_device_get_release_default (dev);
			fwupd_release_set_remote_id (rel, remote_id);
			fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_SUPPORTED);
		}
		g_ptr_array_add (details, dev);
	}
	return g_steal_pointer (&details);
}

/**
 * fu_engine_get_devices:
 * @self: A #FuEngine
 * @error: A #GError, or %NULL
 *
 * Gets the list of devices.
 *
 * Returns: (transfer container) (element-type FwupdDevice): results
 **/
GPtrArray *
fu_engine_get_devices (FuEngine *self, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;

	g_return_val_if_fail (FU_IS_ENGINE (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	devices = fu_device_list_get_active (self->device_list);
	if (devices->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO,
				     "No detected devices");
		return NULL;
	}
	return g_steal_pointer (&devices);
}

/**
 * fu_engine_get_history:
 * @self: A #FuEngine
 * @error: A #GError, or %NULL
 *
 * Gets the list of history.
 *
 * Returns: (transfer container) (element-type FwupdDevice): results
 **/
GPtrArray *
fu_engine_get_history (FuEngine *self, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;

	g_return_val_if_fail (FU_IS_ENGINE (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	devices = fu_history_get_devices (self->history, error);
	if (devices == NULL)
		return NULL;
	if (devices->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO,
				     "No history");
		return NULL;
	}

	/* try to set the remote ID for each device */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index (devices, i);
		FwupdRelease *rel;
		GPtrArray *csums;

		/* get the checksums */
		rel = fu_device_get_release_default (dev);
		if (rel == NULL)
			continue;

		/* find the checksum that matches */
		csums = fwupd_release_get_checksums (rel);
		for (guint j = 0; j < csums->len; j++) {
			const gchar *csum = g_ptr_array_index (csums, j);
			const gchar *remote_id = fu_engine_get_remote_id_for_checksum (self, csum);
			if (remote_id != NULL) {
				fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_SUPPORTED);
				fwupd_release_set_remote_id (rel, remote_id);
				break;
			}
		}
	}

	return g_steal_pointer (&devices);
}

/**
 * fu_engine_get_remotes:
 * @self: A #FuEngine
 * @error: A #GError, or %NULL
 *
 * Gets the list of remotes in use by the engine.
 *
 * Returns: (transfer container) (element-type FwupdRemote): results
 **/
GPtrArray *
fu_engine_get_remotes (FuEngine *self, GError **error)
{
	GPtrArray *remotes;

	g_return_val_if_fail (FU_IS_ENGINE (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	remotes = fu_config_get_remotes (self->config);
	if (remotes->len == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "No remotes configured");
		return NULL;
	}
	return g_ptr_array_ref (remotes);
}

static gint
fu_engine_sort_releases_cb (gconstpointer a, gconstpointer b)
{
	FwupdRelease *rel_a = FWUPD_RELEASE (*((FwupdRelease **) a));
	FwupdRelease *rel_b = FWUPD_RELEASE (*((FwupdRelease **) b));
	return as_utils_vercmp (fwupd_release_get_version (rel_b),
				fwupd_release_get_version (rel_a));
}

static AsApp *
fu_engine_filter_apps_by_requirements (FuEngine *self, GPtrArray *apps,
				       FuDevice *device, GError **error)
{
	g_autoptr(GError) error_all = NULL;

	/* find the first component that passes all the requirements */
	for (guint i = 0; i < apps->len; i++) {
		g_autoptr(GError) error_local = NULL;
		AsApp *app_tmp = AS_APP (g_ptr_array_index (apps, i));
		if (!fu_engine_check_requirements (self, app_tmp, device, &error_local)) {
			if (error_all != NULL) {
				error_all = g_steal_pointer (&error_local);
				continue;
			}
			/* assume the domain and code is the same */
			g_prefix_error (&error_all, "%s, ", error_local->message);
			continue;
		}
		return g_object_ref (app_tmp);
	}

	/* return the compound error */
	g_propagate_error (error, g_steal_pointer (&error_all));
	return NULL;
}

static GPtrArray *
fu_engine_get_releases_for_device (FuEngine *self, FuDevice *device, GError **error)
{
	GPtrArray *device_guids;
	GPtrArray *releases;
	const gchar *version;

	/* get device version */
	version = fu_device_get_version (device);
	if (version == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "no version set");
		return NULL;
	}

	/* only show devices that can be updated */
	if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "ignoring %s [%s] as not updatable",
			     fu_device_get_id (device),
			     fu_device_get_name (device));
		return NULL;
	}

	releases = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	device_guids = fu_device_get_guids (device);
	for (guint i = 0; i < device_guids->len; i++) {
		GPtrArray *releases_tmp;
		g_autoptr(AsApp) app = NULL;
		g_autoptr(GPtrArray) apps = NULL;
		const gchar *guid = g_ptr_array_index (device_guids, i);

		/* get all the components that provide this GUID */
		apps = _as_store_get_apps_by_provide (self->store,
						      AS_PROVIDE_KIND_FIRMWARE_FLASHED,
						      guid);
		if (apps->len == 0)
			continue;

		/* filter by requirements */
		app = fu_engine_filter_apps_by_requirements (self, apps, device, error);
		if (app == NULL)
			return NULL;

		/* get all releases */
		releases_tmp = as_app_get_releases (app);
		for (guint j = 0; j < releases_tmp->len; j++) {
			AsRelease *release = g_ptr_array_index (releases_tmp, j);
			GPtrArray *checksums;
			g_autoptr(FwupdRelease) rel = fwupd_release_new ();

			/* create new FwupdRelease for the AsRelease */
			fu_engine_set_release_from_appstream (self, rel, app, release);

			/* invalid */
			if (fwupd_release_get_uri (rel) == NULL)
				continue;
			checksums = fwupd_release_get_checksums (rel);
			if (checksums->len == 0)
				continue;

			/* success */
			g_ptr_array_add (releases, g_steal_pointer (&rel));
		}
	}
	return releases;
}

/**
 * fu_engine_get_releases:
 * @self: A #FuEngine
 * @device_id: A device ID
 * @error: A #GError, or %NULL
 *
 * Gets the releases available for a specific device.
 *
 * Returns: (transfer container) (element-type FwupdDevice): results
 **/
GPtrArray *
fu_engine_get_releases (FuEngine *self, const gchar *device_id, GError **error)
{
	FuDevice *device;
	g_autoptr(GPtrArray) releases = NULL;

	g_return_val_if_fail (FU_IS_ENGINE (self), NULL);
	g_return_val_if_fail (device_id != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* find the device */
	device = fu_device_list_get_by_id (self->device_list, device_id, error);
	if (device == NULL)
		return NULL;

	/* get all the releases for the device */
	releases = fu_engine_get_releases_for_device (self, device, error);
	if (releases == NULL)
		return NULL;
	if (releases->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO,
				     "No releases for device");
		return NULL;
	}
	g_ptr_array_sort (releases, fu_engine_sort_releases_cb);
	return g_steal_pointer (&releases);
}

/**
 * fu_engine_get_downgrades:
 * @self: A #FuEngine
 * @device_id: A device ID
 * @error: A #GError, or %NULL
 *
 * Gets the downgrades available for a specific device.
 *
 * Returns: (transfer container) (element-type FwupdDevice): results
 **/
GPtrArray *
fu_engine_get_downgrades (FuEngine *self, const gchar *device_id, GError **error)
{
	FuDevice *device;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(GPtrArray) releases_tmp = NULL;
	g_autoptr(GString) error_str = g_string_new (NULL);

	g_return_val_if_fail (FU_IS_ENGINE (self), NULL);
	g_return_val_if_fail (device_id != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* find the device */
	device = fu_device_list_get_by_id (self->device_list, device_id, error);
	if (device == NULL)
		return NULL;

	/* get all the releases for the device */
	releases_tmp = fu_engine_get_releases_for_device (self, device, error);
	if (releases_tmp == NULL)
		return NULL;
	releases = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (guint i = 0; i < releases_tmp->len; i++) {
		FwupdRelease *rel_tmp = g_ptr_array_index (releases_tmp, i);
		gint vercmp;

		/* only include older firmware */
		vercmp = as_utils_vercmp (fwupd_release_get_version (rel_tmp),
					  fu_device_get_version (device));
		if (vercmp == 0) {
			g_string_append_printf (error_str, "%s=same, ",
						fwupd_release_get_version (rel_tmp));
			g_debug ("ignoring %s as the same as %s",
				 fwupd_release_get_version (rel_tmp),
				 fu_device_get_version (device));
			continue;
		}
		if (vercmp > 0) {
			g_string_append_printf (error_str, "%s=newer, ",
						fwupd_release_get_version (rel_tmp));
			g_debug ("ignoring %s as newer than %s",
				 fwupd_release_get_version (rel_tmp),
				 fu_device_get_version (device));
			continue;
		}

		/* don't show releases we are not allowed to dowgrade to */
		if (fu_device_get_version_lowest (device) != NULL) {
			if (as_utils_vercmp (fwupd_release_get_version (rel_tmp),
					     fu_device_get_version_lowest (device)) <= 0) {
				g_string_append_printf (error_str, "%s=lowest, ",
							fwupd_release_get_version (rel_tmp));
				g_debug ("ignoring %s as older than lowest %s",
					 fwupd_release_get_version (rel_tmp),
					 fu_device_get_version_lowest (device));
				continue;
			}
		}
		g_ptr_array_add (releases, g_object_ref (rel_tmp));
	}
	if (error_str->len > 2)
		g_string_truncate (error_str, error_str->len - 2);
	if (releases->len == 0) {
		if (error_str->len > 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO,
				     "No downgrades for device, current is %s: %s",
				     fu_device_get_version (device),
				     error_str->str);
		} else {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO,
				     "No downgrades for device, current is %s",
				     fu_device_get_version (device));
		}
		return NULL;
	}
	g_ptr_array_sort (releases, fu_engine_sort_releases_cb);
	return g_steal_pointer (&releases);
}

/**
 * fu_engine_get_upgrades:
 * @self: A #FuEngine
 * @device_id: A device ID
 * @error: A #GError, or %NULL
 *
 * Gets the upgrades available for a specific device.
 *
 * Returns: (transfer container) (element-type FwupdDevice): results
 **/
GPtrArray *
fu_engine_get_upgrades (FuEngine *self, const gchar *device_id, GError **error)
{
	FuDevice *device;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(GPtrArray) releases_tmp = NULL;
	g_autoptr(GString) error_str = g_string_new (NULL);

	g_return_val_if_fail (FU_IS_ENGINE (self), NULL);
	g_return_val_if_fail (device_id != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* find the device */
	device = fu_device_list_get_by_id (self->device_list, device_id, error);
	if (device == NULL)
		return NULL;

	/* get all the releases for the device */
	releases_tmp = fu_engine_get_releases_for_device (self, device, error);
	if (releases_tmp == NULL)
		return NULL;
	releases = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (guint i = 0; i < releases_tmp->len; i++) {
		FwupdRelease *rel_tmp = g_ptr_array_index (releases_tmp, i);
		gint vercmp;

		/* only include older firmware */
		vercmp = as_utils_vercmp (fwupd_release_get_version (rel_tmp),
					  fu_device_get_version (device));
		if (vercmp == 0) {
			g_string_append_printf (error_str, "%s=same, ",
						fwupd_release_get_version (rel_tmp));
			g_debug ("ignoring %s as the same as %s",
				 fwupd_release_get_version (rel_tmp),
				 fu_device_get_version (device));
			continue;
		}
		if (vercmp < 0) {
			g_string_append_printf (error_str, "%s=older, ",
						fwupd_release_get_version (rel_tmp));
			g_debug ("ignoring %s as older than %s",
				 fwupd_release_get_version (rel_tmp),
				 fu_device_get_version (device));
			continue;
		}
		g_ptr_array_add (releases, g_object_ref (rel_tmp));
	}
	if (error_str->len > 2)
		g_string_truncate (error_str, error_str->len - 2);
	if (releases->len == 0) {
		if (error_str->len > 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO,
				     "No upgrades for device, current is %s: %s",
				     fu_device_get_version (device),
				     error_str->str);
		} else {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO,
				     "No upgrades for device, current is %s",
				     fu_device_get_version (device));
		}
		return NULL;
	}
	g_ptr_array_sort (releases, fu_engine_sort_releases_cb);
	return g_steal_pointer (&releases);
}

/**
 * fu_engine_clear_results:
 * @self: A #FuEngine
 * @device_id: A device ID
 * @error: A #GError, or %NULL
 *
 * Clear the historical state of a specific device operation.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_clear_results (FuEngine *self, const gchar *device_id, GError **error)
{
	g_autoptr(FuDevice) device = NULL;
	FuPlugin *plugin;

	g_return_val_if_fail (FU_IS_ENGINE (self), FALSE);
	g_return_val_if_fail (device_id != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* find the device */
	device = fu_engine_get_item_by_id_fallback_history (self, device_id, error);
	if (device == NULL)
		return FALSE;

	/* already set on the database */
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_NOTIFIED)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "device already has notified flag");
		return FALSE;
	}

	/* call into the plugin if it still exists */
	plugin = fu_plugin_list_find_by_name (self->plugin_list,
					      fu_device_get_plugin (device),
					      error);
	if (plugin != NULL) {
		if (!fu_plugin_runner_clear_results (plugin, device, error))
			return FALSE;
	}

	/* override */
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_NOTIFIED);
	return fu_history_modify_device (self->history, device,
					 FU_HISTORY_FLAGS_MATCH_OLD_VERSION |
					 FU_HISTORY_FLAGS_MATCH_NEW_VERSION,
					 error);
}

/**
 * fu_engine_get_results:
 * @self: A #FuEngine
 * @device_id: A device ID
 * @error: A #GError, or %NULL
 *
 * Gets the historical state of a specific device operation.
 *
 * Returns: (transfer container): a #FwupdDevice, or %NULL
 **/
FwupdDevice *
fu_engine_get_results (FuEngine *self, const gchar *device_id, GError **error)
{
	g_autoptr(FuDevice) device = NULL;

	g_return_val_if_fail (FU_IS_ENGINE (self), NULL);
	g_return_val_if_fail (device_id != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* find the device */
	device = fu_engine_get_item_by_id_fallback_history (self, device_id, error);
	if (device == NULL)
		return FALSE;

	/* the notification has already been shown to the user */
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_NOTIFIED)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOTHING_TO_DO,
			     "User has already been notified about %s",
			     fu_device_get_id (device));
		return NULL;
	}

	/* success */
	return g_object_ref (FWUPD_DEVICE (device));
}

static void
fu_engine_plugins_setup (FuEngine *self)
{
	GPtrArray *plugins;
	g_autoptr(AsProfileTask) ptask = NULL;

	ptask = as_profile_start_literal (self->profile, "FuEngine:setup");
	g_assert (ptask != NULL);
	plugins = fu_plugin_list_get_all (self->plugin_list);
	for (guint i = 0; i < plugins->len; i++) {
		g_autoptr(GError) error = NULL;
		g_autoptr(AsProfileTask) ptask2 = NULL;
		FuPlugin *plugin = g_ptr_array_index (plugins, i);
		ptask2 = as_profile_start (self->profile,
					   "FuEngine:setup{%s}",
					   fu_plugin_get_name (plugin));
		g_assert (ptask2 != NULL);
		if (!fu_plugin_runner_startup (plugin, &error)) {
			fu_plugin_set_enabled (plugin, FALSE);
			g_message ("disabling plugin because: %s", error->message);
		}
	}
}

static void
fu_engine_plugins_coldplug (FuEngine *self, gboolean is_recoldplug)
{
	GPtrArray *plugins;
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(GString) str = g_string_new (NULL);

	/* don't allow coldplug to be scheduled when in coldplug */
	self->coldplug_running = TRUE;

	/* prepare */
	plugins = fu_plugin_list_get_all (self->plugin_list);
	for (guint i = 0; i < plugins->len; i++) {
		g_autoptr(GError) error = NULL;
		FuPlugin *plugin = g_ptr_array_index (plugins, i);
		if (!fu_plugin_runner_coldplug_prepare (plugin, &error))
			g_warning ("failed to prepare coldplug: %s", error->message);
	}

	/* do this in one place */
	if (self->coldplug_delay > 0) {
		g_debug ("sleeping for %ums", self->coldplug_delay);
		g_usleep (self->coldplug_delay * 1000);
	}

	/* exec */
	ptask = as_profile_start_literal (self->profile, "FuEngine:coldplug");
	g_assert (ptask != NULL);
	for (guint i = 0; i < plugins->len; i++) {
		g_autoptr(GError) error = NULL;
		g_autoptr(AsProfileTask) ptask2 = NULL;
		FuPlugin *plugin = g_ptr_array_index (plugins, i);
		ptask2 = as_profile_start (self->profile,
					   "FuEngine:coldplug{%s}",
					   fu_plugin_get_name (plugin));
		g_assert (ptask2 != NULL);
		if (is_recoldplug) {
			if (!fu_plugin_runner_recoldplug (plugin, &error))
				g_message ("failed recoldplug: %s", error->message);
		} else {
			if (!fu_plugin_runner_coldplug (plugin, &error)) {
				fu_plugin_set_enabled (plugin, FALSE);
				g_message ("disabling plugin because: %s",
					   error->message);
			}
		}
	}

	/* cleanup */
	for (guint i = 0; i < plugins->len; i++) {
		g_autoptr(GError) error = NULL;
		FuPlugin *plugin = g_ptr_array_index (plugins, i);
		if (!fu_plugin_runner_coldplug_cleanup (plugin, &error))
			g_warning ("failed to cleanup coldplug: %s", error->message);
	}

	/* print what we do have */
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index (plugins, i);
		if (!fu_plugin_get_enabled (plugin))
			continue;
		g_string_append_printf (str, "%s, ", fu_plugin_get_name (plugin));
	}
	if (str->len > 2) {
		g_string_truncate (str, str->len - 2);
		g_message ("using plugins: %s", str->str);
	}

	/* we can recoldplug from this point on */
	self->coldplug_running = FALSE;
}

static void
fu_engine_plugin_device_register (FuEngine *self, FuDevice *device)
{
	GPtrArray *plugins;
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_REGISTERED)) {
		g_warning ("already registered %s, ignoring",
			   fu_device_get_id (device));
		return;
	}
	plugins = fu_plugin_list_get_all (self->plugin_list);
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index (plugins, i);
		fu_plugin_runner_device_register (plugin, device);
	}
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_REGISTERED);
}

static void
fu_engine_plugin_device_register_cb (FuPlugin *plugin,
				    FuDevice *device,
				    gpointer user_data)
{
	FuEngine *self = FU_ENGINE (user_data);
	fu_engine_plugin_device_register (self, device);
}

static void
fu_engine_plugin_device_added_cb (FuPlugin *plugin,
				  FuDevice *device,
				  gpointer user_data)
{
	FuEngine *self = (FuEngine *) user_data;
	fu_engine_add_device (self, device);
}

void
fu_engine_add_device (FuEngine *self, FuDevice *device)
{
	GPtrArray *blacklisted_devices;
	GPtrArray *device_guids;

	/* device has no GUIDs set! */
	device_guids = fu_device_get_guids (device);
	if (device_guids->len == 0) {
		g_warning ("no GUIDs for device %s [%s]",
			   fu_device_get_id (device),
			   fu_device_get_name (device));
		return;
	}

	/* is this GUID blacklisted */
	blacklisted_devices = fu_config_get_blacklist_devices (self->config);
	for (guint i = 0; i < blacklisted_devices->len; i++) {
		const gchar *blacklisted_guid = g_ptr_array_index (blacklisted_devices, i);
		for (guint j = 0; j < device_guids->len; j++) {
			const gchar *device_guid = g_ptr_array_index (device_guids, j);
			if (g_strcmp0 (blacklisted_guid, device_guid) == 0) {
				g_debug ("%s is blacklisted [%s], ignoring from %s",
					 fu_device_get_id (device), device_guid,
					 fu_device_get_plugin (device));
				return;
			}
		}
	}

	/* notify all plugins about this new device */
	if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_REGISTERED))
		fu_engine_plugin_device_register (self, device);

	/* match the metadata at this point so clients can tell if the
	 * device is worthy */
	if (fu_engine_is_device_supported (self, device))
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_SUPPORTED);

	/* create new device */
	fu_device_list_add (self->device_list, device);
}

static void
fu_engine_plugin_device_removed_cb (FuPlugin *plugin,
				    FuDevice *device,
				    gpointer user_data)
{
	FuEngine *self = (FuEngine *) user_data;
	FuDevice *device_tmp;
	FuPlugin *plugin_old;
	g_autoptr(GError) error = NULL;

	device_tmp = fu_device_list_get_by_id (self->device_list,
					       fu_device_get_id (device),
					       &error);
	if (device_tmp == NULL) {
		g_debug ("%s", error->message);
		return;
	}

	/* get the plugin */
	plugin_old = fu_plugin_list_find_by_name (self->plugin_list,
						  fu_device_get_plugin (device),
						  &error);
	if (plugin_old == NULL) {
		g_debug ("%s", error->message);
		return;
	}

	/* check this came from the same plugin */
	if (g_strcmp0 (fu_plugin_get_name (plugin),
		       fu_plugin_get_name (plugin_old)) != 0) {
		g_debug ("ignoring duplicate removal from %s",
			 fu_plugin_get_name (plugin));
		return;
	}

	/* make the UI update */
	fu_device_list_remove (self->device_list, device);
	fu_engine_emit_changed (self);
}

static gboolean
fu_engine_recoldplug_delay_cb (gpointer user_data)
{
	FuEngine *self = (FuEngine *) user_data;
	g_debug ("performing a recoldplug");
	fu_engine_plugins_coldplug (self, TRUE);
	self->coldplug_id = 0;
	return FALSE;
}

static void
fu_engine_plugin_recoldplug_cb (FuPlugin *plugin, FuEngine *self)
{
	if (self->coldplug_running) {
		g_warning ("coldplug already running, cannot recoldplug");
		return;
	}
	g_debug ("scheduling a recoldplug");
	if (self->coldplug_id != 0)
		g_source_remove (self->coldplug_id);
	self->coldplug_id = g_timeout_add (1500, fu_engine_recoldplug_delay_cb, self);
}

static void
fu_engine_plugin_set_coldplug_delay_cb (FuPlugin *plugin, guint duration, FuEngine *self)
{
	self->coldplug_delay = MAX (self->coldplug_delay, duration);
	g_debug ("got coldplug delay of %ums, global maximum is now %ums",
		 duration, self->coldplug_delay);
}

/* for the self tests to use */
void
fu_engine_add_plugin (FuEngine *self, FuPlugin *plugin)
{
	fu_plugin_list_add (self->plugin_list, plugin);
}

static gboolean
fu_engine_load_plugins (FuEngine *self, GError **error)
{
	const gchar *fn;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* profile */
	ptask = as_profile_start_literal (self->profile, "FuEngine:load-plugins");
	g_assert (ptask != NULL);

	/* search */
	dir = g_dir_open (PLUGINDIR, 0, error);
	if (dir == NULL)
		return FALSE;
	while ((fn = g_dir_read_name (dir)) != NULL) {
		GPtrArray *blacklist;
		g_autofree gchar *filename = NULL;
		g_autoptr(FuPlugin) plugin = NULL;
		g_autoptr(GError) error_local = NULL;

		/* ignore non-plugins */
		if (!g_str_has_suffix (fn, ".so"))
			continue;

		/* open module */
		filename = g_build_filename (PLUGINDIR, fn, NULL);
		plugin = fu_plugin_new ();
		fu_plugin_set_usb_context (plugin, self->usb_ctx);
		fu_plugin_set_hwids (plugin, self->hwids);
		fu_plugin_set_smbios (plugin, self->smbios);
		fu_plugin_set_supported (plugin, self->supported_guids);
		fu_plugin_set_quirks (plugin, self->quirks);
		g_debug ("adding plugin %s", filename);
		if (!fu_plugin_open (plugin, filename, &error_local)) {
			g_warning ("failed to open plugin %s: %s",
				   filename, error_local->message);
			continue;
		}

		/* is blacklisted */
		blacklist = fu_config_get_blacklist_plugins (self->config);
		for (guint i = 0; i < blacklist->len; i++) {
			const gchar *name = g_ptr_array_index (blacklist, i);
			if (g_strcmp0 (name, fu_plugin_get_name (plugin)) == 0) {
				fu_plugin_set_enabled (plugin, FALSE);
				break;
			}
		}
		if (!fu_plugin_get_enabled (plugin)) {
			g_debug ("%s blacklisted by config",
				 fu_plugin_get_name (plugin));
			continue;
		}

		/* watch for changes */
		g_signal_connect (plugin, "device-added",
				  G_CALLBACK (fu_engine_plugin_device_added_cb),
				  self);
		g_signal_connect (plugin, "device-removed",
				  G_CALLBACK (fu_engine_plugin_device_removed_cb),
				  self);
		g_signal_connect (plugin, "device-register",
				  G_CALLBACK (fu_engine_plugin_device_register_cb),
				  self);
		g_signal_connect (plugin, "recoldplug",
				  G_CALLBACK (fu_engine_plugin_recoldplug_cb),
				  self);
		g_signal_connect (plugin, "set-coldplug-delay",
				  G_CALLBACK (fu_engine_plugin_set_coldplug_delay_cb),
				  self);

		/* add */
		fu_plugin_list_add (self->plugin_list, plugin);
	}

	/* depsolve into the correct order */
	if (!fu_plugin_list_depsolve (self->plugin_list, error))
		return FALSE;

	/* success */
	return TRUE;
}

/**
 * fu_engine_check_plugins_pending:
 * @self: A #FuEngine
 * @error: A #GError, or %NULL
 *
 * Checks if any plugins have pending devices to be added.
 *
 * Returns: %FALSE if any plugins have pending devices.
 **/
gboolean
fu_engine_check_plugins_pending (FuEngine *self, GError **error)
{
	GPtrArray *plugins;

	g_return_val_if_fail (FU_IS_ENGINE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	plugins = fu_plugin_list_get_all (self->plugin_list);
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index (plugins, i);
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
fu_engine_cleanup_state (GError **error)
{
	const gchar *filenames[] = {
		"/var/cache/app-info/xmls/fwupd-verify.xml",
		"/var/cache/app-info/xmls/fwupd.xml",
		NULL };
	for (guint i = 0; filenames[i] != NULL; i++) {
		g_autoptr(GFile) file = g_file_new_for_path (filenames[i]);
		if (g_file_query_exists (file, NULL)) {
			if (!g_file_delete (file, NULL, error))
				return FALSE;
		}
	}
	return TRUE;
}

guint64
fu_engine_get_archive_size_max (FuEngine *self)
{
	return fu_config_get_archive_size_max (self->config);
}

static void
fu_engine_usb_device_removed_cb (GUsbContext *ctx,
				 GUsbDevice *usb_device,
				 FuEngine *self)
{
	g_autoptr(GPtrArray) devices = NULL;

	/* go through each device and remove any that match */
	devices = fu_device_list_get_all (self->device_list);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index (devices, i);
		if (!FU_IS_USB_DEVICE (device))
			continue;
		if (g_strcmp0 (fu_device_get_platform_id (device),
			       g_usb_device_get_platform_id (usb_device)) == 0) {
			g_debug ("auto-removing FuUsbDevice");
			fu_device_list_remove (self->device_list, device);
		}
	}
}

static void
fu_engine_usb_device_added_cb (GUsbContext *ctx,
			       GUsbDevice *usb_device,
			       FuEngine *self)
{
	GPtrArray *plugins = fu_plugin_list_get_all (self->plugin_list);

	/* call into each plugin */
	for (guint j = 0; j < plugins->len; j++) {
		FuPlugin *plugin_tmp = g_ptr_array_index (plugins, j);
		g_autoptr(GError) error = NULL;
		if (!fu_plugin_runner_usb_device_added (plugin_tmp, usb_device, &error)) {
			if (g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
				g_debug ("ignoring: %s", error->message);
				continue;
			}
			g_warning ("failed to add USB device: %s", error->message);
		}
	}
}


static void
fu_engine_load_quirks (FuEngine *self)
{
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(GError) error = NULL;

	/* profile */
	ptask = as_profile_start_literal (self->profile, "FuEngine:load-quirks");
	g_assert (ptask != NULL);
	if (!fu_quirks_load (self->quirks, &error))
		g_warning ("Failed to load quirks: %s", error->message);
}

static void
fu_engine_load_smbios (FuEngine *self)
{
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(GError) error = NULL;

	/* profile */
	ptask = as_profile_start_literal (self->profile, "FuEngine:load-smbios");
	g_assert (ptask != NULL);
	if (!fu_smbios_setup (self->smbios, &error))
		g_warning ("Failed to load SMBIOS: %s", error->message);
}

static void
fu_engine_load_hwids (FuEngine *self)
{
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(GError) error = NULL;

	/* profile */
	ptask = as_profile_start_literal (self->profile, "FuEngine:load-hwids");
	g_assert (ptask != NULL);
	if (!fu_hwids_setup (self->hwids, self->smbios, &error))
		g_warning ("Failed to load HWIDs: %s", error->message);
}

static gboolean
fu_engine_update_history_device (FuEngine *self, FuDevice *dev_history, GError **error)
{
	FuDevice *dev;
	FuPlugin *plugin;
	FwupdRelease *rel_history;
	g_autofree gchar *btime = NULL;

	/* is in the device list */
	dev = fu_device_list_get_by_id (self->device_list,
					fu_device_get_id (dev_history),
					error);
	if (dev == NULL)
		return FALSE;

	/* does the installed version match what we tried to install
	 * before fwupd was restarted */
	rel_history = fu_device_get_release_default (dev_history);
	if (rel_history == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "no release for history FuDevice");
		return FALSE;
	}

	/* is this the same boot time as when we scheduled the update,
	 * i.e. has fwupd been restarted before we rebooted */
	btime = fu_engine_get_boot_time ();
	if (g_strcmp0 (fwupd_release_get_metadata_item (rel_history, "BootTime"),
		       btime) == 0) {
		g_debug ("service restarted, but no reboot has taken place");
		return TRUE;
	}

	/* the system is running with the new firmware version */
	if (g_strcmp0 (fu_device_get_version (dev),
		       fwupd_release_get_version (rel_history)) == 0) {
		g_debug ("installed version %s matching history %s",
			 fu_device_get_version (dev),
			 fwupd_release_get_version (rel_history));
		fu_device_set_update_state (dev_history, FWUPD_UPDATE_STATE_SUCCESS);
		return fu_history_modify_device (self->history, dev_history,
						 FU_HISTORY_FLAGS_MATCH_NEW_VERSION,
						 error);
	}

	/* does the plugin know the update failure */
	plugin = fu_plugin_list_find_by_name (self->plugin_list,
					      fu_device_get_plugin (dev),
					      error);
	if (plugin == NULL)
		return FALSE;
	if (!fu_plugin_runner_get_results (plugin, dev, error))
		return FALSE;

	/* the plugin either can't tell us the error, or doesn't know itself */
	if (fu_device_get_update_state (dev) != FWUPD_UPDATE_STATE_FAILED) {
		g_debug ("falling back to generic failure");
		fu_device_set_update_error (dev_history, "failed to run update on reboot");
	}

	/* update the state in the database */
	fu_device_set_update_error (dev_history, fu_device_get_update_error (dev));
	return fu_history_modify_device (self->history, dev_history,
					 FU_HISTORY_FLAGS_MATCH_OLD_VERSION,
					 error);
}

static gboolean
fu_engine_update_history_database (FuEngine *self, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;

	/* get any devices */
	devices = fu_history_get_devices (self->history, error);
	if (devices == NULL)
		return FALSE;
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index (devices, i);
		g_autoptr(GError) error_local = NULL;

		/* not in the required state */
		if (fu_device_get_update_state (dev) != FWUPD_UPDATE_STATE_NEEDS_REBOOT)
			continue;

		/* try to save the new update-state, but ignoring any error */
		if (!fu_engine_update_history_device (self, dev, &error_local))
			g_warning ("%s", error_local->message);
	}
	return TRUE;
}

/**
 * fu_engine_load:
 * @self: A #FuEngine
 * @error: A #GError, or %NULL
 *
 * Load the firmware update engine so it is ready for use.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_load (FuEngine *self, GError **error)
{
	g_autoptr(AsProfileTask) ptask = NULL;

	/* profile */
	ptask = as_profile_start_literal (self->profile, "FuEngine:load");
	g_assert (ptask != NULL);

	g_return_val_if_fail (FU_IS_ENGINE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* read config file */
	if (!fu_config_load (self->config, error)) {
		g_prefix_error (error, "Failed to load config: ");
		return FALSE;
	}

	/* load quirks, SMBIOS and the hwids */
	fu_engine_load_smbios (self);
	fu_engine_load_hwids (self);
	fu_engine_load_quirks (self);

	/* load AppStream metadata */
	as_store_add_filter (self->store, AS_APP_KIND_FIRMWARE);
	if (!fu_engine_load_metadata_store (self, error)) {
		g_prefix_error (error, "Failed to load AppStream data: ");
		return FALSE;
	}

	/* set shared USB context */
	self->usb_ctx = g_usb_context_new (error);
	if (self->usb_ctx == NULL) {
		g_prefix_error (error, "Failed to get USB context: ");
		return FALSE;
	}

	/* delete old data files */
	if (!fu_engine_cleanup_state (error)) {
		g_prefix_error (error, "Failed to clean up: ");
		return FALSE;
	}

	/* load plugin */
	if (!fu_engine_load_plugins (self, error)) {
		g_prefix_error (error, "Failed to load plugins: ");
		return FALSE;
	}

	/* watch the device list for updates and proxy */
	g_signal_connect (self->device_list, "added",
			  G_CALLBACK (fu_engine_device_added_cb),
			  self);
	g_signal_connect (self->device_list, "removed",
			  G_CALLBACK (fu_engine_device_removed_cb),
			  self);
	g_signal_connect (self->device_list, "changed",
			  G_CALLBACK (fu_engine_device_changed_cb),
			  self);

	/* add devices */
	fu_engine_plugins_setup (self);
	fu_engine_plugins_coldplug (self, FALSE);

	/* coldplug USB devices */
	g_signal_connect (self->usb_ctx, "device-added",
			  G_CALLBACK (fu_engine_usb_device_added_cb),
			  self);
	g_signal_connect (self->usb_ctx, "device-removed",
			  G_CALLBACK (fu_engine_usb_device_removed_cb),
			  self);
	g_usb_context_enumerate (self->usb_ctx);

	/* update the db for devices that were updated during the reboot */
	if (!fu_engine_update_history_database (self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_engine_class_init (FuEngineClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_engine_finalize;

	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals[SIGNAL_DEVICE_ADDED] =
		g_signal_new ("device-added",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, FU_TYPE_DEVICE);
	signals[SIGNAL_DEVICE_REMOVED] =
		g_signal_new ("device-removed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, FU_TYPE_DEVICE);
	signals[SIGNAL_DEVICE_CHANGED] =
		g_signal_new ("device-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, FU_TYPE_DEVICE);
	signals[SIGNAL_STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals[SIGNAL_PERCENTAGE_CHANGED] =
		g_signal_new ("percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
}

static void
fu_engine_init (FuEngine *self)
{
	self->percentage = 0;
	self->status = FWUPD_STATUS_IDLE;
	self->config = fu_config_new ();
	self->device_list = fu_device_list_new ();
	self->smbios = fu_smbios_new ();
	self->hwids = fu_hwids_new ();
	self->quirks = fu_quirks_new ();
	self->history = fu_history_new ();
	self->plugin_list = fu_plugin_list_new ();
	self->profile = as_profile_new ();
	self->store = as_store_new ();
	self->supported_guids = g_ptr_array_new_with_free_func (g_free);
}

static void
fu_engine_finalize (GObject *obj)
{
	FuEngine *self = FU_ENGINE (obj);

	if (self->usb_ctx != NULL)
		g_object_unref (self->usb_ctx);
	if (self->coldplug_id != 0)
		g_source_remove (self->coldplug_id);

	g_object_unref (self->config);
	g_object_unref (self->smbios);
	g_object_unref (self->quirks);
	g_object_unref (self->hwids);
	g_object_unref (self->history);
	g_object_unref (self->plugin_list);
	g_object_unref (self->profile);
	g_object_unref (self->store);
	g_object_unref (self->device_list);
	g_ptr_array_unref (self->supported_guids);

	G_OBJECT_CLASS (fu_engine_parent_class)->finalize (obj);
}

FuEngine *
fu_engine_new (void)
{
	FuEngine *self;
	self = g_object_new (FU_TYPE_ENGINE, NULL);
	return FU_ENGINE (self);
}
