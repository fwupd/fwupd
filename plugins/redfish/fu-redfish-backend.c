/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <curl/curl.h>
#include <string.h>

#include <fwupdplugin.h>

#include "fu-redfish-backend.h"
#include "fu-redfish-common.h"
#include "fu-redfish-smbios.h"

struct _FuRedfishBackend
{
	FuBackend		 parent_instance;
	CURL			*curl;
	gchar			*hostname;
	guint			 port;
	gchar			*update_uri_path;
	gchar			*push_uri_path;
	gboolean		 use_https;
	gboolean		 cacheck;
};

G_DEFINE_TYPE (FuRedfishBackend, fu_redfish_backend, FU_TYPE_BACKEND)

#ifdef HAVE_LIBCURL_7_62_0
typedef gchar curlptr;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(curlptr, curl_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(CURLU, curl_url_cleanup)
#endif

static size_t
fu_redfish_backend_fetch_data_cb (char *ptr, size_t size, size_t nmemb, void *userdata)
{
	GByteArray *buf = (GByteArray *) userdata;
	gsize realsize = size * nmemb;
	g_byte_array_append (buf, (const guint8 *) ptr, realsize);
	return realsize;
}

static GBytes *
fu_redfish_backend_fetch_data (FuRedfishBackend *self, const gchar *uri_path, GError **error)
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
	curl_easy_setopt (self->curl, CURLOPT_WRITEFUNCTION, fu_redfish_backend_fetch_data_cb);
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
fu_redfish_backend_coldplug_member (FuRedfishBackend *self,
				    JsonObject *member,
				    GError **error)
{
	g_autoptr(FuDevice) dev = fu_device_new ();
	const gchar *guid = NULL;
	g_autofree gchar *guid_lower = NULL;
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

	id = g_strdup_printf ("Redfish-Inventory-%s",
			      json_object_get_string_member (member, "Id"));
	fu_device_set_id (dev, id);
	fu_device_add_protocol (dev, "org.dmtf.redfish");

	guid_lower = g_ascii_strdown (guid, -1);
	fu_device_add_guid (dev, guid_lower);
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
	fu_backend_device_added (FU_BACKEND (self), dev);
	return TRUE;
}

static gboolean
fu_redfish_backend_coldplug_collection (FuRedfishBackend *self,
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
		blob = fu_redfish_backend_fetch_data (self, member_uri, error);
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
		if (!fu_redfish_backend_coldplug_member (self, member, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_redfish_backend_coldplug_inventory (FuRedfishBackend *self,
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
	blob = fu_redfish_backend_fetch_data (self, collection_uri, error);
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

	return fu_redfish_backend_coldplug_collection (self, collection, error);
}

static gboolean
fu_redfish_backend_coldplug (FuBackend *backend, GError **error)
{
	FuRedfishBackend *self = FU_REDFISH_BACKEND (backend);
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
	blob = fu_redfish_backend_fetch_data (self, self->update_uri_path, error);
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
		return fu_redfish_backend_coldplug_inventory (self, tmp, error);
	}
	if (json_object_has_member (obj_root, "SoftwareInventory")) {
		JsonObject *tmp = json_object_get_object_member (obj_root, "SoftwareInventory");
		return fu_redfish_backend_coldplug_inventory (self, tmp, error);
	}
	return TRUE;
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(curl_mime, curl_mime_free)

gboolean
fu_redfish_backend_update (FuRedfishBackend *self, FuDevice *device, GBytes *blob_fw,
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

static gboolean
fu_redfish_backend_setup (FuBackend *backend, GError **error)
{
	FuRedfishBackend *self = FU_REDFISH_BACKEND (backend);
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
	if (self->hostname != NULL)
		g_debug ("Hostname: %s", self->hostname);
	if (self->port != 0)
		g_debug ("Port:     %u", self->port);

	/* try to connect */
	blob = fu_redfish_backend_fetch_data (self, "/redfish/v1/", error);
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

void
fu_redfish_backend_set_hostname (FuRedfishBackend *self, const gchar *hostname)
{
	g_free (self->hostname);
	self->hostname = g_strdup (hostname);
}

void
fu_redfish_backend_set_port (FuRedfishBackend *self, guint port)
{
	self->port = port;
}

void
fu_redfish_backend_set_https (FuRedfishBackend *self, gboolean use_https)
{
	self->use_https = use_https;
}

void
fu_redfish_backend_set_cacheck (FuRedfishBackend *self, gboolean cacheck)
{
	self->cacheck = cacheck;
}

void
fu_redfish_backend_set_username (FuRedfishBackend *self, const gchar *username)
{
	curl_easy_setopt (self->curl, CURLOPT_USERNAME, username);
}

void
fu_redfish_backend_set_password (FuRedfishBackend *self, const gchar *password)
{
	curl_easy_setopt (self->curl, CURLOPT_PASSWORD, password);
}

static void
fu_redfish_backend_finalize (GObject *object)
{
	FuRedfishBackend *self = FU_REDFISH_BACKEND (object);
	if (self->curl != NULL)
		curl_easy_cleanup (self->curl);
	g_free (self->update_uri_path);
	g_free (self->push_uri_path);
	g_free (self->hostname);
	G_OBJECT_CLASS (fu_redfish_backend_parent_class)->finalize (object);
}

static void
fu_redfish_backend_class_init (FuRedfishBackendClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuBackendClass *klass_backend = FU_BACKEND_CLASS (klass);
	klass_backend->coldplug = fu_redfish_backend_coldplug;
	klass_backend->setup = fu_redfish_backend_setup;
	object_class->finalize = fu_redfish_backend_finalize;
}

static void
fu_redfish_backend_init (FuRedfishBackend *self)
{
	self->curl = curl_easy_init ();

	/* since DSP0266 makes Basic Authorization a requirement,
	 * it is safe to use Basic Auth for all implementations */
	curl_easy_setopt (self->curl, CURLOPT_HTTPAUTH, (glong) CURLAUTH_BASIC);
}

FuRedfishBackend *
fu_redfish_backend_new (FuContext *ctx)
{
	return FU_REDFISH_BACKEND (g_object_new (FU_REDFISH_TYPE_BACKEND,
						 "name", "redfish",
						 "context", ctx,
						 NULL));
}
