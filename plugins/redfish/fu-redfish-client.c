/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <json-glib/json-glib.h>
#include <curl/curl.h>
#include <string.h>

#include <fwupdplugin.h>

#include "fu-redfish-client.h"
#include "fu-redfish-common.h"
#include "fu-redfish-smbios.h"

struct _FuRedfishClient
{
	GObject			 parent_instance;
	CURL			*curl;
	gchar			*hostname;
	guint			 port;
	gchar			*update_uri_path;
	gchar			*push_uri_path;
	gboolean		 use_https;
	gboolean		 cacheck;
	GPtrArray		*devices;
};

G_DEFINE_TYPE (FuRedfishClient, fu_redfish_client, G_TYPE_OBJECT)

#ifdef HAVE_LIBCURL_7_62_0
typedef gchar curlptr;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(curlptr, curl_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(CURLU, curl_url_cleanup)
#endif

static size_t
fu_redfish_client_fetch_data_cb (char *ptr, size_t size, size_t nmemb, void *userdata)
{
	GByteArray *buf = (GByteArray *) userdata;
	gsize realsize = size * nmemb;
	g_byte_array_append (buf, (const guint8 *) ptr, realsize);
	return realsize;
}

static GBytes *
fu_redfish_client_fetch_data (FuRedfishClient *self, const gchar *uri_path, GError **error)
{
	CURLcode res;
	g_autofree gchar *port = g_strdup_printf ("%u", self->port);
	g_autoptr(GByteArray) buf = g_byte_array_new ();
#ifdef HAVE_LIBCURL_7_62_0
	g_autoptr(CURLU) uri = NULL;
#else
	g_autofree gchar *uri = NULL;
#endif

	/* create URI */
#ifdef HAVE_LIBCURL_7_62_0
	uri = curl_url ();
	curl_url_set (uri, CURLUPART_SCHEME, self->use_https ? "https" : "http", 0);
	curl_url_set (uri, CURLUPART_PATH, uri_path, 0);
	curl_url_set (uri, CURLUPART_HOST, self->hostname, 0);
	curl_url_set (uri, CURLUPART_PORT, port, 0);
	if (curl_easy_setopt (self->curl, CURLOPT_CURLU, uri) != CURLE_OK) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "failed to create message for URI");
		return NULL;
	}
#else
	uri = g_strdup_printf ("%s://%s:%s%s",
			       self->use_https ? "https" : "http",
			       self->hostname,
			       port,
			       uri_path);
	if (curl_easy_setopt (self->curl, CURLOPT_URL, uri) != CURLE_OK) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "failed to create message for URI");
		return NULL;
	}
#endif
	curl_easy_setopt (self->curl, CURLOPT_WRITEFUNCTION, fu_redfish_client_fetch_data_cb);
	curl_easy_setopt (self->curl, CURLOPT_WRITEDATA, buf);
	res = curl_easy_perform (self->curl);
	if (res != CURLE_OK) {
		glong status_code = 0;
#ifdef HAVE_LIBCURL_7_62_0
		g_autoptr(curlptr) uri_str = NULL;
#endif
		curl_easy_getinfo (self->curl, CURLINFO_RESPONSE_CODE, &status_code);
#ifdef HAVE_LIBCURL_7_62_0
		curl_url_get (uri, CURLUPART_URL, &uri_str, 0);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to download %s: %s",
			     uri_str, curl_easy_strerror (res));
#else
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to download %s: %s",
			     uri, curl_easy_strerror (res));
#endif
		return NULL;
	}

	return g_byte_array_free_to_bytes (g_steal_pointer (&buf));
}

static gboolean
fu_redfish_client_coldplug_member (FuRedfishClient *self,
				   JsonObject *member,
				   GError **error)
{
	g_autoptr(FuDevice) dev = NULL;
	const gchar *guid = NULL;
	g_autofree gchar *id = NULL;

	if (json_object_has_member (member, "SoftwareId")) {
		guid = json_object_get_string_member (member, "SoftwareId");
	} else if (json_object_has_member (member, "Oem")) {
		JsonObject *oem = json_object_get_object_member (member, "Oem");
		if (oem != NULL && json_object_has_member (oem, "Hpe")) {
			JsonObject *hpe = json_object_get_object_member (oem, "Hpe");
			if (hpe != NULL && json_object_has_member (hpe, "DeviceClass"))
				guid = json_object_get_string_member (hpe, "DeviceClass");
		}
	}

	/* skip the devices without guid */
	if (guid == NULL)
		return TRUE;

	dev = fu_device_new ();

	id = g_strdup_printf ("Redfish-Inventory-%s",
			      json_object_get_string_member (member, "Id"));
	fu_device_set_id (dev, id);
	fu_device_add_protocol (dev, "org.dmtf.redfish");

	fu_device_add_guid (dev, guid);
	if (json_object_has_member (member, "Name"))
		fu_device_set_name (dev, json_object_get_string_member (member, "Name"));
	fu_device_set_summary (dev, "Redfish device");
	if (json_object_has_member (member, "Version"))
		fu_device_set_version (dev, json_object_get_string_member (member, "Version"));
	if (json_object_has_member (member, "LowestSupportedVersion"))
		fu_device_set_version_lowest (dev, json_object_get_string_member (member, "LowestSupportedVersion"));
	if (json_object_has_member (member, "Description"))
		fu_device_set_description (dev, json_object_get_string_member (member, "Description"));
	if (json_object_has_member (member, "Updateable")) {
		if (json_object_get_boolean_member (member, "Updateable"))
			fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE);
	} else {
		/* assume the device is updatable */
		fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE);
	}

	/* success */
	g_ptr_array_add (self->devices, g_steal_pointer (&dev));
	return TRUE;
}

static gboolean
fu_redfish_client_coldplug_collection (FuRedfishClient *self,
				       JsonObject *collection,
				       GError **error)
{
	JsonArray *members;
	JsonNode *node_root;
	JsonObject *member;

	members = json_object_get_array_member (collection, "Members");
	for (guint i = 0; i < json_array_get_length (members); i++) {
		g_autoptr(JsonParser) parser = json_parser_new ();
		g_autoptr(GBytes) blob = NULL;
		JsonObject *member_id;
		const gchar *member_uri;

		member_id = json_array_get_object_element (members, i);
		member_uri = json_object_get_string_member (member_id, "@odata.id");
		if (member_uri == NULL) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_FOUND,
					     "no @odata.id string");
			return FALSE;
		}

		/* try to connect */
		blob = fu_redfish_client_fetch_data (self, member_uri, error);
		if (blob == NULL)
			return FALSE;

		/* get the member object */
		if (!json_parser_load_from_data (parser,
						 g_bytes_get_data (blob, NULL),
						 (gssize) g_bytes_get_size (blob),
						 error)) {
			g_prefix_error (error, "failed to parse node: ");
			return FALSE;
		}
		node_root = json_parser_get_root (parser);
		if (node_root == NULL) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "no root node");
			return FALSE;
		}
		member = json_node_get_object (node_root);
		if (member == NULL) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "no member object");
			return FALSE;
		}

		/* Create the device for the member */
		if (!fu_redfish_client_coldplug_member (self, member, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_redfish_client_coldplug_inventory (FuRedfishClient *self,
				      JsonObject *inventory,
				      GError **error)
{
	g_autoptr(JsonParser) parser = json_parser_new ();
	g_autoptr(GBytes) blob = NULL;
	JsonNode *node_root;
	JsonObject *collection;
	const gchar *collection_uri;

	if (inventory == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "no inventory object");
		return FALSE;
	}

	collection_uri = json_object_get_string_member (inventory, "@odata.id");
	if (collection_uri == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "no @odata.id string");
		return FALSE;
	}

	/* try to connect */
	blob = fu_redfish_client_fetch_data (self, collection_uri, error);
	if (blob == NULL)
		return FALSE;

	/* get the inventory object */
	if (!json_parser_load_from_data (parser,
					 g_bytes_get_data (blob, NULL),
					 (gssize) g_bytes_get_size (blob),
					 error)) {
		g_prefix_error (error, "failed to parse node: ");
		return FALSE;
	}
	node_root = json_parser_get_root (parser);
	if (node_root == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no root node");
		return FALSE;
	}
	collection = json_node_get_object (node_root);
	if (collection == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no collection object");
		return FALSE;
	}

	return fu_redfish_client_coldplug_collection (self, collection, error);
}

gboolean
fu_redfish_client_coldplug (FuRedfishClient *self, GError **error)
{
	JsonNode *node_root;
	JsonObject *obj_root = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(JsonParser) parser = json_parser_new ();

	/* nothing set */
	if (self->update_uri_path == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "no update_uri_path");
		return FALSE;
	}

	/* try to connect */
	blob = fu_redfish_client_fetch_data (self, self->update_uri_path, error);
	if (blob == NULL)
		return FALSE;

	/* get the update service */
	if (!json_parser_load_from_data (parser,
					 g_bytes_get_data (blob, NULL),
					 (gssize) g_bytes_get_size (blob),
					 error)) {
		g_prefix_error (error, "failed to parse node: ");
		return FALSE;
	}
	node_root = json_parser_get_root (parser);
	if (node_root == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no root node");
		return FALSE;
	}
	obj_root = json_node_get_object (node_root);
	if (obj_root == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no root object");
		return FALSE;
	}
	if (!json_object_get_boolean_member (obj_root, "ServiceEnabled")) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "service is not enabled");
		return FALSE;
	}
	if (!json_object_has_member (obj_root, "HttpPushUri")) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "HttpPushUri is not available");
		return FALSE;
	}
	self->push_uri_path = g_strdup (json_object_get_string_member (obj_root, "HttpPushUri"));
	if (self->push_uri_path == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "HttpPushUri is invalid");
		return FALSE;
	}
	if (json_object_has_member (obj_root, "FirmwareInventory")) {
		JsonObject *tmp = json_object_get_object_member (obj_root, "FirmwareInventory");
		return fu_redfish_client_coldplug_inventory (self, tmp, error);
	}
	if (json_object_has_member (obj_root, "SoftwareInventory")) {
		JsonObject *tmp = json_object_get_object_member (obj_root, "SoftwareInventory");
		return fu_redfish_client_coldplug_inventory (self, tmp, error);
	}
	return TRUE;
}

static gboolean
fu_redfish_client_set_uefi_credentials (FuRedfishClient *self, GError **error)
{
	guint32 indications_le;
	g_autofree gchar *userpass_safe = NULL;
	g_auto(GStrv) split = NULL;
	g_autoptr(GBytes) indications = NULL;
	g_autoptr(GBytes) userpass = NULL;

	/* get the uint32 specifying if there are EFI variables set */
	indications = fu_efivar_get_data_bytes (REDFISH_EFI_INFORMATION_GUID,
						REDFISH_EFI_INFORMATION_INDICATIONS,
						NULL, error);
	if (indications == NULL)
		return FALSE;
	if (g_bytes_get_size (indications) != 4) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "invalid value for %s, got %" G_GSIZE_FORMAT " bytes",
			     REDFISH_EFI_INFORMATION_INDICATIONS,
			     g_bytes_get_size (indications));
		return FALSE;
	}
	memcpy (&indications_le, g_bytes_get_data (indications, NULL), 4);
	if ((indications_le & REDFISH_EFI_INDICATIONS_OS_CREDENTIALS) == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no indications for OS credentials");
		return FALSE;
	}

	/* read the correct EFI var for runtime */
	userpass = fu_efivar_get_data_bytes (REDFISH_EFI_INFORMATION_GUID,
					     REDFISH_EFI_INFORMATION_OS_CREDENTIALS,
					     NULL, error);
	if (userpass == NULL)
		return FALSE;

	/* it might not be NUL terminated */
	userpass_safe = g_strndup (g_bytes_get_data (userpass, NULL),
				   g_bytes_get_size (userpass));
	split = g_strsplit (userpass_safe, ":", -1);
	if (g_strv_length (split) != 2) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "invalid format for username:password, got '%s'",
			     userpass_safe);
		return FALSE;
	}
	fu_redfish_client_set_username (self, split[0]);
	fu_redfish_client_set_password (self, split[1]);
	return TRUE;
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(curl_mime, curl_mime_free)

gboolean
fu_redfish_client_update (FuRedfishClient *self, FuDevice *device, GBytes *blob_fw,
			  GError **error)
{
	CURLcode res;
	FwupdRelease *release;
	curl_mimepart *part;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *port = g_strdup_printf ("%u", self->port);
#ifdef HAVE_LIBCURL_7_62_0
	g_autoptr(CURLU) uri = curl_url ();
#else
	g_autofree gchar *uri = NULL;
#endif
	g_autoptr(curl_mime) mime = curl_mime_init (self->curl);

	/* Get the update version */
	release = fwupd_device_get_release_default (FWUPD_DEVICE (device));
	if (release != NULL) {
		filename = g_strdup_printf ("%s-%s.bin",
					    fu_device_get_name (device),
					    fwupd_release_get_version (release));
	} else {
		filename = g_strdup_printf ("%s.bin",
					    fu_device_get_name (device));
	}

	/* create URI */
#ifdef HAVE_LIBCURL_7_62_0
	curl_url_set (uri, CURLUPART_SCHEME, self->use_https ? "https" : "http", 0);
	curl_url_set (uri, CURLUPART_PATH, self->push_uri_path, 0);
	curl_url_set (uri, CURLUPART_HOST, self->hostname, 0);
	curl_url_set (uri, CURLUPART_PORT, port, 0);
	if (curl_easy_setopt (self->curl, CURLOPT_CURLU, uri) != CURLE_OK) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "failed to create message for URI");
		return FALSE;
	}
#else
	uri = g_strdup_printf ("%s://%s:%s%s",
			       self->use_https ? "https" : "http",
			       self->hostname,
			       port,
			       self->push_uri_path);
	if (curl_easy_setopt (self->curl, CURLOPT_URL, uri) != CURLE_OK) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "failed to create message for URI");
		return FALSE;
	}
#endif

	/* Create the multipart request */
	curl_easy_setopt (self->curl, CURLOPT_MIMEPOST, mime);
	part = curl_mime_addpart (mime);
	curl_mime_data (part, g_bytes_get_data (blob_fw, NULL), g_bytes_get_size (blob_fw));
	curl_mime_type (part, "application/octet-stream");
	res = curl_easy_perform (self->curl);
	if (res != CURLE_OK) {
		glong status_code = 0;
#ifdef HAVE_LIBCURL_7_62_0
		g_autoptr(curlptr) uri_str = NULL;
#endif
		curl_easy_getinfo (self->curl, CURLINFO_RESPONSE_CODE, &status_code);
#ifdef HAVE_LIBCURL_7_62_0
		curl_url_get (uri, CURLUPART_URL, &uri_str, 0);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to upload %s to %s: %s",
			     filename, uri_str,
			     curl_easy_strerror (res));
		return FALSE;
#else
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to upload %s to %s: %s",
			     filename, uri,
			     curl_easy_strerror (res));
		return FALSE;
#endif
	}

	return TRUE;
}

gboolean
fu_redfish_client_setup (FuRedfishClient *self, GBytes *smbios_table, GError **error)
{
	JsonNode *node_root;
	JsonObject *obj_root = NULL;
	JsonObject *obj_update_service = NULL;
	const gchar *data_id;
	const gchar *version = NULL;
	g_autofree gchar *user_agent = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(JsonParser) parser = json_parser_new ();

	/* sanity check */
	if (self->port == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "no port specified");
		return FALSE;
	}
	if (self->port > 0xffff) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "invalid port specified: 0x%x",
			     self->port);
		return FALSE;
	}

	/* setup networking */
	user_agent = g_strdup_printf ("%s/%s", PACKAGE_NAME, PACKAGE_VERSION);
	curl_easy_setopt (self->curl, CURLOPT_USERAGENT , user_agent);
	curl_easy_setopt (self->curl, CURLOPT_CONNECTTIMEOUT, 60L);
	if (self->cacheck == FALSE)
		curl_easy_setopt (self->curl, CURLOPT_SSL_VERIFYPEER , 0L);

	/* this is optional */
	if (smbios_table != NULL) {
		g_autoptr(GError) error_smbios = NULL;
		g_autoptr(GError) error_uefi = NULL;
		g_autoptr(FuRedfishSmbios) redfish_smbios = fu_redfish_smbios_new ();

		if (!fu_firmware_parse (FU_FIRMWARE (redfish_smbios),
					smbios_table,
					FWUPD_INSTALL_FLAG_NONE,
					&error_smbios)) {
			g_debug ("failed to get connection URI automatically: %s",
				 error_smbios->message);
		} else {
			const gchar *hostname = fu_redfish_smbios_get_ip_addr (redfish_smbios);
			if (hostname == NULL)
				hostname = fu_redfish_smbios_get_hostname (redfish_smbios);
			if (hostname == NULL) {
				g_set_error_literal (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INVALID_FILE,
						     "no hostname");
				return FALSE;
			}
			fu_redfish_client_set_hostname (self, hostname);
			fu_redfish_client_set_port (self, fu_redfish_smbios_get_port (redfish_smbios));
		}
		if (!fu_redfish_client_set_uefi_credentials (self, &error_uefi)) {
			g_debug ("failed to get username and password automatically: %s",
				 error_uefi->message);
		}
	}
	if (self->hostname != NULL)
		g_debug ("Hostname: %s", self->hostname);
	if (self->port != 0)
		g_debug ("Port:     %u", self->port);

	/* try to connect */
	blob = fu_redfish_client_fetch_data (self, "/redfish/v1/", error);
	if (blob == NULL)
		return FALSE;

	/* get the update service */
	if (!json_parser_load_from_data (parser,
					 g_bytes_get_data (blob, NULL),
					 (gssize) g_bytes_get_size (blob),
					 error)) {
		g_prefix_error (error, "failed to parse node: ");
		return FALSE;
	}
	node_root = json_parser_get_root (parser);
	if (node_root == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no root node");
		return FALSE;
	}
	obj_root = json_node_get_object (node_root);
	if (obj_root == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no root object");
		return FALSE;
	}
	if (json_object_has_member (obj_root, "ServiceVersion")) {
		version = json_object_get_string_member (obj_root,
							 "ServiceVersion");
	} else if (json_object_has_member (obj_root, "RedfishVersion")) {
		version = json_object_get_string_member (obj_root,
							 "RedfishVersion");
	}
	g_debug ("Version:  %s", version);
	g_debug ("UUID:     %s",
		 json_object_get_string_member (obj_root, "UUID"));

	if (json_object_has_member (obj_root, "UpdateService"))
		obj_update_service = json_object_get_object_member (obj_root, "UpdateService");
	if (obj_update_service == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "no UpdateService object");
		return FALSE;
	}
	data_id = json_object_get_string_member (obj_update_service, "@odata.id");
	if (data_id == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no @odata.id string");
		return FALSE;
	}
	self->update_uri_path = g_strdup (data_id);
	return TRUE;
}

GPtrArray *
fu_redfish_client_get_devices (FuRedfishClient *self)
{
	return self->devices;
}

void
fu_redfish_client_set_hostname (FuRedfishClient *self, const gchar *hostname)
{
	g_free (self->hostname);
	self->hostname = g_strdup (hostname);
}

void
fu_redfish_client_set_port (FuRedfishClient *self, guint port)
{
	self->port = port;
}

void
fu_redfish_client_set_https (FuRedfishClient *self, gboolean use_https)
{
	self->use_https = use_https;
}

void
fu_redfish_client_set_cacheck (FuRedfishClient *self, gboolean cacheck)
{
	self->cacheck = cacheck;
}

void
fu_redfish_client_set_username (FuRedfishClient *self, const gchar *username)
{
	curl_easy_setopt (self->curl, CURLOPT_USERNAME, username);
}

void
fu_redfish_client_set_password (FuRedfishClient *self, const gchar *password)
{
	curl_easy_setopt (self->curl, CURLOPT_PASSWORD, password);
}

static void
fu_redfish_client_finalize (GObject *object)
{
	FuRedfishClient *self = FU_REDFISH_CLIENT (object);
	if (self->curl != NULL)
		curl_easy_cleanup (self->curl);
	g_free (self->update_uri_path);
	g_free (self->push_uri_path);
	g_free (self->hostname);
	g_ptr_array_unref (self->devices);
	G_OBJECT_CLASS (fu_redfish_client_parent_class)->finalize (object);
}

static void
fu_redfish_client_class_init (FuRedfishClientClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_redfish_client_finalize;
}

static void
fu_redfish_client_init (FuRedfishClient *self)
{
	self->devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	self->curl = curl_easy_init ();

	/* since DSP0266 makes Basic Authorization a requirement,
	 * it is safe to use Basic Auth for all implementations */
	curl_easy_setopt (self->curl, CURLOPT_HTTPAUTH, (glong) CURLAUTH_BASIC);
}

FuRedfishClient *
fu_redfish_client_new (void)
{
	FuRedfishClient *self;
	self = g_object_new (REDFISH_TYPE_CLIENT, NULL);
	return FU_REDFISH_CLIENT (self);
}

/* vim: set noexpandtab: */
