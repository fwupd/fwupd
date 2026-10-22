/*
 * Copyright 2026 NVIDIA Corporation
 * Author: Vishnu Raghav <vraghav@nvidia.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-nvidia-oob-request.h"

struct _FuNvidiaOobRequest {
	GObject parent_instance;
	CURL *curl;
	gchar *base_uri;
	struct curl_slist *headers; /* nullable */
	GByteArray *buf;
	glong status_code;
	gchar *location;
	FwupdJsonParser *json_parser;
	FwupdJsonObject *json_obj; /* nullable */
};

G_DEFINE_TYPE(FuNvidiaOobRequest, fu_nvidia_oob_request, G_TYPE_OBJECT)

typedef struct curl_slist _curl_slist;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(_curl_slist, curl_slist_free_all)

FwupdJsonObject *
fu_nvidia_oob_request_get_json_object(FuNvidiaOobRequest *self, GError **error)
{
	g_return_val_if_fail(FU_IS_NVIDIA_OOB_REQUEST(self), NULL);
	if (self->json_obj == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "no object");
		return NULL;
	}
	return fwupd_json_object_ref(self->json_obj);
}

glong
fu_nvidia_oob_request_get_status_code(FuNvidiaOobRequest *self)
{
	g_return_val_if_fail(FU_IS_NVIDIA_OOB_REQUEST(self), G_MAXLONG);
	return self->status_code;
}

void
fu_nvidia_oob_request_set_session_key(FuNvidiaOobRequest *self, const gchar *session_key)
{
	g_autofree gchar *header = NULL;

	g_return_if_fail(FU_IS_NVIDIA_OOB_REQUEST(self));
	g_return_if_fail(session_key != NULL);

	/* X-Auth-Token session authentication, DSP0266 12.3 */
	header = g_strdup_printf("X-Auth-Token: %s", session_key);
	self->headers = curl_slist_append(self->headers, header);
}

void
fu_nvidia_oob_request_set_credentials(FuNvidiaOobRequest *self,
				      const gchar *username,
				      const gchar *password)
{
	g_return_if_fail(FU_IS_NVIDIA_OOB_REQUEST(self));
	(void)curl_easy_setopt(self->curl, CURLOPT_HTTPAUTH, (glong)CURLAUTH_BASIC);
	(void)curl_easy_setopt(self->curl, CURLOPT_USERNAME, username);
	(void)curl_easy_setopt(self->curl, CURLOPT_PASSWORD, password);
}

curl_mime *
fu_nvidia_oob_request_mime_new(FuNvidiaOobRequest *self)
{
	g_return_val_if_fail(FU_IS_NVIDIA_OOB_REQUEST(self), NULL);
	return curl_mime_init(self->curl);
}

static gboolean
fu_nvidia_oob_request_load_json(FuNvidiaOobRequest *self, GError **error)
{
	g_autoptr(FwupdJsonNode) json_node = NULL;
	g_autoptr(FwupdJsonObject) json_error = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;

	json_node = fwupd_json_parser_load_from_data(self->json_parser,
						     (const gchar *)self->buf->data,
						     (gssize)self->buf->len,
						     error);
	if (json_node == NULL)
		return FALSE;
	json_obj = fwupd_json_node_get_object(json_node, error);
	if (json_obj == NULL)
		return FALSE;

	/* a Redfish error response */
	json_error = fwupd_json_object_get_object(json_obj, "error", NULL);
	if (json_error != NULL) {
		FwupdError error_code = FWUPD_ERROR_INTERNAL;
		const gchar *id = NULL;
		const gchar *msg = NULL;
		g_autoptr(FwupdJsonArray) json_info = NULL;

		json_info = fwupd_json_object_get_array(json_error, "@Message.ExtendedInfo", NULL);
		if (json_info != NULL && fwupd_json_array_get_size(json_info) > 0) {
			g_autoptr(FwupdJsonObject) json_info0 =
			    fwupd_json_array_get_object(json_info, 0, NULL);
			if (json_info0 != NULL) {
				id = fwupd_json_object_get_string(json_info0, "MessageId", NULL);
				msg = fwupd_json_object_get_string(json_info0, "Message", NULL);
			}
		}
		if (id == NULL)
			id = fwupd_json_object_get_string(json_error, "code", NULL);
		if (msg == NULL)
			msg = fwupd_json_object_get_string(json_error, "message", NULL);
		if (id != NULL && g_pattern_match_simple("Base.*.AccessDenied", id))
			error_code = FWUPD_ERROR_AUTH_FAILED;
		else if (id != NULL && g_pattern_match_simple("Base.*.PasswordChangeRequired", id))
			error_code = FWUPD_ERROR_AUTH_EXPIRED;
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    error_code,
				    msg != NULL ? msg : "unknown failure");
		return FALSE;
	}
	self->json_obj = g_steal_pointer(&json_obj);
	return TRUE;
}

static size_t
fu_nvidia_oob_request_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	GByteArray *buf = (GByteArray *)userdata;
	gsize realsize = size * nmemb;
	g_byte_array_append(buf, (const guint8 *)ptr, realsize);
	return realsize;
}

static size_t
fu_nvidia_oob_request_header_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	FuNvidiaOobRequest *self = FU_NVIDIA_OOB_REQUEST(userdata);
	gsize realsize = size * nmemb;

	if (realsize > 9 && g_ascii_strncasecmp(ptr, "Location:", 9) == 0) {
		g_autofree gchar *value = g_strndup(ptr + 9, realsize - 9);
		g_free(self->location);
		self->location = g_strdup(g_strstrip(value));
	}
	return realsize;
}

static gboolean
fu_nvidia_oob_request_send(FuNvidiaOobRequest *self,
			   const gchar *path,
			   struct curl_slist *headers,
			   GError **error)
{
	CURLcode res;
	g_autofree gchar *uri = g_strconcat(self->base_uri, path, NULL);

	g_return_val_if_fail(self->status_code == 0, FALSE);

	(void)curl_easy_setopt(self->curl, CURLOPT_URL, uri);
	(void)curl_easy_setopt(self->curl, CURLOPT_HTTPHEADER, headers);
	res = curl_easy_perform(self->curl);
	curl_easy_getinfo(self->curl, CURLINFO_RESPONSE_CODE, &self->status_code);
	g_debug("%s [%li]: %.*s",
		uri,
		self->status_code,
		(gint)MIN(self->buf->len, 4096),
		(const gchar *)self->buf->data);
	if (res != CURLE_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "failed to request %s: %s",
			    uri,
			    curl_easy_strerror(res));
		return FALSE;
	}
	if (self->status_code == 401) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_AUTH_FAILED,
				    "authentication failed");
		return FALSE;
	}

	/* an empty body is valid, the GB300 task monitor answers 200 with one */
	if (self->buf->len > 0) {
		if (!fu_nvidia_oob_request_load_json(self, error)) {
			g_prefix_error(error, "failed to parse %s: ", uri);
			return FALSE;
		}
	}
	return TRUE;
}

gboolean
fu_nvidia_oob_request_perform(FuNvidiaOobRequest *self, const gchar *path, GError **error)
{
	g_return_val_if_fail(FU_IS_NVIDIA_OOB_REQUEST(self), FALSE);
	g_return_val_if_fail(path != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	(void)curl_easy_setopt(self->curl, CURLOPT_HTTPGET, 1L);
	return fu_nvidia_oob_request_send(self, path, self->headers, error);
}

gboolean
fu_nvidia_oob_request_post(FuNvidiaOobRequest *self,
			   const gchar *path,
			   FwupdJsonObject *json_obj,
			   GError **error)
{
	g_autoptr(_curl_slist) headers = NULL;
	g_autoptr(GString) str = NULL;

	g_return_val_if_fail(FU_IS_NVIDIA_OOB_REQUEST(self), FALSE);
	g_return_val_if_fail(path != NULL, FALSE);
	g_return_val_if_fail(json_obj != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	str = fwupd_json_object_to_string(json_obj, FWUPD_JSON_EXPORT_FLAG_NONE);
	g_debug("POST %s: %s", path, str->str);
	(void)curl_easy_setopt(self->curl, CURLOPT_POST, 1L);
	(void)curl_easy_setopt(self->curl, CURLOPT_COPYPOSTFIELDS, str->str);
	headers = curl_slist_append(headers, "Content-Type: application/json");
	for (struct curl_slist *h = self->headers; h != NULL; h = h->next)
		headers = curl_slist_append(headers, h->data);
	return fu_nvidia_oob_request_send(self, path, headers, error);
}

gboolean
fu_nvidia_oob_request_post_mime(FuNvidiaOobRequest *self,
				const gchar *path,
				curl_mime *mime,
				gchar **location,
				GError **error)
{
	g_return_val_if_fail(FU_IS_NVIDIA_OOB_REQUEST(self), FALSE);
	g_return_val_if_fail(path != NULL, FALSE);
	g_return_val_if_fail(mime != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	(void)curl_easy_setopt(self->curl, CURLOPT_MIMEPOST, mime);
	(void)curl_easy_setopt(self->curl, CURLOPT_HEADERFUNCTION, fu_nvidia_oob_request_header_cb);
	(void)curl_easy_setopt(self->curl, CURLOPT_HEADERDATA, self);
	/* a ~117MB bundle over the USB host interface at 1-2MB/s */
	(void)curl_easy_setopt(self->curl, CURLOPT_TIMEOUT, 600L);
	if (!fu_nvidia_oob_request_send(self, path, self->headers, error))
		return FALSE;
	if (location != NULL)
		*location = g_strdup(self->location);
	return TRUE;
}

FuNvidiaOobRequest *
fu_nvidia_oob_request_new(const gchar *base_uri, gboolean cacheck)
{
	FuNvidiaOobRequest *self = g_object_new(FU_TYPE_NVIDIA_OOB_REQUEST, NULL);
	g_autofree gchar *user_agent = g_strdup_printf("%s/%s", PACKAGE_NAME, PACKAGE_VERSION);

	g_return_val_if_fail(base_uri != NULL, NULL);

	self->base_uri = g_strdup(base_uri);
	(void)curl_easy_setopt(self->curl, CURLOPT_USERAGENT, user_agent);
	(void)curl_easy_setopt(self->curl, CURLOPT_CONNECTTIMEOUT, 60L);
	(void)curl_easy_setopt(self->curl, CURLOPT_TIMEOUT, 180L);
	if (!cacheck) {
		(void)curl_easy_setopt(self->curl, CURLOPT_SSL_VERIFYPEER, 0L);
		(void)curl_easy_setopt(self->curl, CURLOPT_SSL_VERIFYHOST, 0L);
	}
	return self;
}

static void
fu_nvidia_oob_request_init(FuNvidiaOobRequest *self)
{
	self->curl = curl_easy_init();
	self->buf = g_byte_array_new();
	self->json_parser = fwupd_json_parser_new();
	(void)curl_easy_setopt(self->curl, CURLOPT_WRITEFUNCTION, fu_nvidia_oob_request_write_cb);
	(void)curl_easy_setopt(self->curl, CURLOPT_WRITEDATA, self->buf);
	fwupd_json_parser_set_max_depth(self->json_parser, 50);
	fwupd_json_parser_set_max_items(self->json_parser, 1000);
	fwupd_json_parser_set_max_quoted(self->json_parser, 100000);
}

static void
fu_nvidia_oob_request_finalize(GObject *object)
{
	FuNvidiaOobRequest *self = FU_NVIDIA_OOB_REQUEST(object);
	if (self->json_obj != NULL)
		fwupd_json_object_unref(self->json_obj);
	if (self->headers != NULL)
		curl_slist_free_all(self->headers);
	g_object_unref(self->json_parser);
	g_byte_array_unref(self->buf);
	curl_easy_cleanup(self->curl);
	g_free(self->location);
	g_free(self->base_uri);
	G_OBJECT_CLASS(fu_nvidia_oob_request_parent_class)->finalize(object);
}

static void
fu_nvidia_oob_request_class_init(FuNvidiaOobRequestClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_nvidia_oob_request_finalize;
}
