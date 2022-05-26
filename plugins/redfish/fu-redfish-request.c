/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-redfish-request.h"

struct _FuRedfishRequest {
	GObject parent_instance;
	CURL *curl;
#ifdef HAVE_LIBCURL_7_62_0
	CURLU *uri;
#else
	gchar *uri_base;
#endif
	GByteArray *buf;
	glong status_code;
	JsonParser *json_parser;
	JsonObject *json_obj;
	GHashTable *cache; /* nullable */
};

G_DEFINE_TYPE(FuRedfishRequest, fu_redfish_request, G_TYPE_OBJECT)

typedef gchar curlptr;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(curlptr, curl_free)

JsonObject *
fu_redfish_request_get_json_object(FuRedfishRequest *self)
{
	g_return_val_if_fail(FU_IS_REDFISH_REQUEST(self), NULL);
	return self->json_obj;
}

CURL *
fu_redfish_request_get_curl(FuRedfishRequest *self)
{
	g_return_val_if_fail(FU_IS_REDFISH_REQUEST(self), NULL);
	return self->curl;
}

#ifdef HAVE_LIBCURL_7_62_0
CURLU *
fu_redfish_request_get_uri(FuRedfishRequest *self)
{
	g_return_val_if_fail(FU_IS_REDFISH_REQUEST(self), NULL);
	return self->uri;
}
#else
void
fu_redfish_request_set_uri_base(FuRedfishRequest *self, const gchar *uri_base)
{
	g_return_if_fail(FU_IS_REDFISH_REQUEST(self));
	self->uri_base = g_strdup(uri_base);
}
#endif

glong
fu_redfish_request_get_status_code(FuRedfishRequest *self)
{
	g_return_val_if_fail(FU_IS_REDFISH_REQUEST(self), G_MAXLONG);
	return self->status_code;
}

static gboolean
fu_redfish_request_load_json(FuRedfishRequest *self, GByteArray *buf, GError **error)
{
	JsonNode *json_root;

	/* load */
	if (buf->data == NULL || buf->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "there was no JSON payload");
		return FALSE;
	}
	if (!json_parser_load_from_data(self->json_parser,
					(const gchar *)buf->data,
					(gssize)buf->len,
					error)) {
		return FALSE;
	}
	json_root = json_parser_get_root(self->json_parser);
	if (json_root == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no JSON root node");
		return FALSE;
	}
	self->json_obj = json_node_get_object(json_root);
	if (self->json_obj == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "no JSON object");
		return FALSE;
	}

	/* dump for humans */
	if (g_getenv("FWUPD_REDFISH_VERBOSE") != NULL) {
		g_autoptr(GString) str = g_string_new(NULL);
		g_autoptr(JsonGenerator) json_generator = json_generator_new();
		json_generator_set_pretty(json_generator, TRUE);
		json_generator_set_root(json_generator, json_root);
		json_generator_to_gstring(json_generator, str);
		g_debug("response: %s", str->str);
	}

	/* unauthorized */
	if (json_object_has_member(self->json_obj, "error")) {
		FwupdError error_code = FWUPD_ERROR_INTERNAL;
		JsonObject *json_error;
		const gchar *id = NULL;
		const gchar *msg = "Unknown failure";

		/* extended error present */
		json_error = json_object_get_object_member(self->json_obj, "error");
		if (json_object_has_member(json_error, "@Message.ExtendedInfo")) {
			JsonArray *json_error_array;
			json_error_array =
			    json_object_get_array_member(json_error, "@Message.ExtendedInfo");
			if (json_array_get_length(json_error_array) > 0) {
				JsonObject *json_error2;
				json_error2 = json_array_get_object_element(json_error_array, 0);
				if (json_object_has_member(json_error2, "MessageId"))
					id =
					    json_object_get_string_member(json_error2, "MessageId");
				if (json_object_has_member(json_error2, "Message"))
					msg = json_object_get_string_member(json_error2, "Message");
			}
		} else {
			if (json_object_has_member(json_error, "code"))
				id = json_object_get_string_member(json_error, "code");
			if (json_object_has_member(json_error, "message"))
				msg = json_object_get_string_member(json_error, "message");
		}
		if (g_strcmp0(id, "Base.1.8.AccessDenied") == 0)
			error_code = FWUPD_ERROR_AUTH_FAILED;
		else if (g_strcmp0(id, "Base.1.8.PasswordChangeRequired") == 0)
			error_code = FWUPD_ERROR_AUTH_EXPIRED;
		g_set_error_literal(error, FWUPD_ERROR, error_code, msg);
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_redfish_request_perform(FuRedfishRequest *self,
			   const gchar *path,
			   FuRedfishRequestPerformFlags flags,
			   GError **error)
{
	CURLcode res;
#ifdef HAVE_LIBCURL_7_62_0
	g_autoptr(curlptr) uri_str = NULL;
#else
	g_autofree gchar *uri_str = NULL;
#endif

	g_return_val_if_fail(FU_IS_REDFISH_REQUEST(self), FALSE);
	g_return_val_if_fail(path != NULL, FALSE);
	g_return_val_if_fail(self->status_code == 0, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* already in cache? */
	if (flags & FU_REDFISH_REQUEST_PERFORM_FLAG_USE_CACHE && self->cache != NULL) {
		GByteArray *buf = g_hash_table_lookup(self->cache, path);
		if (buf != NULL) {
			if (flags & FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON)
				return fu_redfish_request_load_json(self, buf, error);
			g_byte_array_unref(self->buf);
			self->buf = g_byte_array_ref(buf);
			return TRUE;
		}
	}

	/* do request */
#ifdef HAVE_LIBCURL_7_62_0
	(void)curl_url_set(self->uri, CURLUPART_PATH, path, 0);
	(void)curl_url_get(self->uri, CURLUPART_URL, &uri_str, 0);
#else
	uri_str = g_strdup_printf("%s%s", self->uri_base, path);
	if (curl_easy_setopt(self->curl, CURLOPT_URL, uri_str) != CURLE_OK) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "failed to create message for URI");
		return FALSE;
	}
#endif
	res = curl_easy_perform(self->curl);
	curl_easy_getinfo(self->curl, CURLINFO_RESPONSE_CODE, &self->status_code);
	if (g_getenv("FWUPD_REDFISH_VERBOSE") != NULL) {
		g_autofree gchar *str = NULL;
		str = g_strndup((const gchar *)self->buf->data, self->buf->len);
		g_debug("%s: %s [%li]", uri_str, str, self->status_code);
	}

	/* check result */
	if (res != CURLE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "failed to request %s: %s",
			    uri_str,
			    curl_easy_strerror(res));
		return FALSE;
	}

	/* load JSON */
	if (flags & FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON) {
		if (!fu_redfish_request_load_json(self, self->buf, error)) {
			g_prefix_error(error, "failed to parse %s: ", uri_str);
			return FALSE;
		}
	}

	/* save to cache */
	if (self->cache != NULL && path != NULL) {
		g_hash_table_insert(self->cache, g_strdup(path), g_byte_array_ref(self->buf));
	}

	/* success */
	return TRUE;
}

typedef struct curl_slist _curl_slist;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(_curl_slist, curl_slist_free_all)

gboolean
fu_redfish_request_perform_full(FuRedfishRequest *self,
				const gchar *path,
				const gchar *request,
				JsonBuilder *builder,
				FuRedfishRequestPerformFlags flags,
				GError **error)
{
	g_autoptr(_curl_slist) hs = NULL;
	g_autoptr(GString) str = g_string_new(NULL);
	g_autoptr(JsonGenerator) json_generator = json_generator_new();
	g_autoptr(JsonNode) json_root = NULL;

	g_return_val_if_fail(FU_IS_REDFISH_REQUEST(self), FALSE);
	g_return_val_if_fail(path != NULL, FALSE);
	g_return_val_if_fail(request != NULL, FALSE);
	g_return_val_if_fail(builder != NULL, FALSE);

	/* export as a string */
	json_root = json_builder_get_root(builder);
	json_generator_set_pretty(json_generator, TRUE);
	json_generator_set_root(json_generator, json_root);
	json_generator_to_gstring(json_generator, str);
	if (g_getenv("FWUPD_REDFISH_VERBOSE") != NULL)
		g_debug("request to %s: %s", path, str->str);

	/* patch */
	(void)curl_easy_setopt(self->curl, CURLOPT_CUSTOMREQUEST, request);
	(void)curl_easy_setopt(self->curl, CURLOPT_POSTFIELDS, str->str);
	(void)curl_easy_setopt(self->curl, CURLOPT_POSTFIELDSIZE, (long)str->len);
	hs = curl_slist_append(hs, "Content-Type: application/json");
	(void)curl_easy_setopt(self->curl, CURLOPT_HTTPHEADER, hs);
	return fu_redfish_request_perform(self, path, flags, error);
}

static size_t
fu_redfish_request_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	GByteArray *buf = (GByteArray *)userdata;
	gsize realsize = size * nmemb;
	g_byte_array_append(buf, (const guint8 *)ptr, realsize);
	return realsize;
}

void
fu_redfish_request_set_cache(FuRedfishRequest *self, GHashTable *cache)
{
	g_return_if_fail(FU_IS_REDFISH_REQUEST(self));
	g_return_if_fail(cache != NULL);
	g_return_if_fail(self->cache == NULL);
	self->cache = g_hash_table_ref(cache);
}

void
fu_redfish_request_set_curlsh(FuRedfishRequest *self, CURLSH *curlsh)
{
	g_return_if_fail(FU_IS_REDFISH_REQUEST(self));
	g_return_if_fail(curlsh != NULL);
	(void)curl_easy_setopt(self->curl, CURLOPT_SHARE, curlsh);
}

static void
fu_redfish_request_init(FuRedfishRequest *self)
{
	self->curl = curl_easy_init();
#ifdef HAVE_LIBCURL_7_62_0
	self->uri = curl_url();
#endif
	self->buf = g_byte_array_new();
	self->json_parser = json_parser_new();
	(void)curl_easy_setopt(self->curl, CURLOPT_WRITEFUNCTION, fu_redfish_request_write_cb);
	(void)curl_easy_setopt(self->curl, CURLOPT_WRITEDATA, self->buf);
}

static void
fu_redfish_request_finalize(GObject *object)
{
	FuRedfishRequest *self = FU_REDFISH_REQUEST(object);
	if (self->cache != NULL)
		g_hash_table_unref(self->cache);
	g_object_unref(self->json_parser);
	g_byte_array_unref(self->buf);
	curl_easy_cleanup(self->curl);
#ifdef HAVE_LIBCURL_7_62_0
	curl_url_cleanup(self->uri);
#else
	g_free(self->uri_base);
#endif
	G_OBJECT_CLASS(fu_redfish_request_parent_class)->finalize(object);
}

static void
fu_redfish_request_class_init(FuRedfishRequestClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_redfish_request_finalize;
}
