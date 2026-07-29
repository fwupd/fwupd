/*
 * Copyright 2026 NVIDIA Corporation
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Redfish host interface client for the Galaxy GB300 OOB plugin
 *
 * the plugin is a pure session-token consumer -- no credentials are ever
 * stored in memory beyond the token itself; session creation is handled
 * externally by install-plugin.sh (first run) or nvidia-oob-auth.sh
 * (subsequent runs / after reboot), both of which prompt for credentials
 * interactively and write only the resulting token to:
 *   /run/fwupd/nvidia_oob.session  (tmpfs, cleared on reboot, mode 600)
 *
 * token lookup order (setup):
 *   1. NVIDIA_OOB_TOKEN env var          (CI / scripted overrides)
 *   2. /run/fwupd/nvidia_oob.session     (written by auth scripts)
 *   neither present: startup() fails with a clear message
 *
 * BMC host lookup order:
 *   1. NVIDIA_OOB_BMC_HOST env var
 *   2. BmcHost in /etc/fwupd/nvidia_oob.conf
 *   3. Redfish host interface link-local fallback
 *
 * TLS: BMC certificate validated against /etc/fwupd/pki/nvidia-oob/ca.pem;
 * set NVIDIA_OOB_INSECURE=1 to skip (bringup only, NOT for production)
 */

#include "config.h"

#include <fwupdplugin.h>

#include <curl/curl.h>
#include <glib/gstdio.h>
#include <string.h>

#include "fu-nvidia-oob-redfish-client.h"

/* forward declarations for helpers used before their definition */
static gchar *
fu_nvidia_oob_redfish_client_absolute_url(FuNvidiaOobRedfishClient *self, const gchar *path);
static FwupdJsonNode *
fu_nvidia_oob_redfish_client_parse_body(GByteArray *body, GError **error);

#define NVIDIA_OOB_REDFISH_DEFAULT_CA	   "/etc/fwupd/pki/nvidia-oob/ca.pem"
#define NVIDIA_OOB_REDFISH_SESSION_FILE	   "/run/fwupd/nvidia_oob.session"
#define NVIDIA_OOB_REDFISH_CONF_FILE	   "/etc/fwupd/nvidia_oob.conf"
#define NVIDIA_OOB_REDFISH_CONNECT_TIMEOUT 10L
#define NVIDIA_OOB_REDFISH_UPLOAD_TIMEOUT  600L
#define NVIDIA_OOB_REDFISH_POLL_TIMEOUT	   30L

struct _FuNvidiaOobRedfishClient {
	GObject parent_instance;
	gchar *base_url;	   /* e.g. https://10.0.1.1 */
	gchar *ca_path;		   /* PEM bundle; NULL = insecure */
	gchar *auth_header;	   /* "X-Auth-Token: <token>" */
	gchar *multipart_push_uri; /* from UpdateService.MultipartHttpPushUri */
	gboolean insecure;
	CURL *curl;
	GMutex curl_mutex;
};

G_DEFINE_TYPE(FuNvidiaOobRedfishClient, fu_nvidia_oob_redfish_client, G_TYPE_OBJECT)

/* response accumulator */

typedef struct {
	GByteArray *body;
	gchar *task_monitor_location; /* from Location: header */
} FuRedfishResponse;

static size_t
curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	FuRedfishResponse *resp = (FuRedfishResponse *)userdata;
	gsize total = size * nmemb;
	g_byte_array_append(resp->body, (const guint8 *)ptr, total);
	return total;
}

static size_t
curl_header_cb(char *buffer, size_t size, size_t nitems, void *userdata)
{
	FuRedfishResponse *resp = (FuRedfishResponse *)userdata;
	gsize total = size * nitems;
	if (total > 9 && g_ascii_strncasecmp(buffer, "Location:", 9) == 0) {
		const gchar *value = buffer + 9;
		gsize value_len;
		while (*value == ' ' || *value == '\t')
			value++;
		value_len = total - (gsize)(value - buffer);
		while (value_len > 0 &&
		       (value[value_len - 1] == '\r' || value[value_len - 1] == '\n'))
			value_len--;
		g_clear_pointer(&resp->task_monitor_location, g_free);
		resp->task_monitor_location = g_strndup(value, value_len);
	}
	return total;
}

static void
fu_redfish_response_free(FuRedfishResponse *resp)
{
	if (resp == NULL)
		return;
	if (resp->body != NULL)
		g_byte_array_unref(resp->body);
	g_free(resp->task_monitor_location);
	g_free(resp);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuRedfishResponse, fu_redfish_response_free)

static FuRedfishResponse *
fu_redfish_response_new(void)
{
	FuRedfishResponse *r = g_new0(FuRedfishResponse, 1);
	r->body = g_byte_array_new();
	return r;
}

/* helpers */

static void
fu_nvidia_oob_redfish_client_apply_tls(FuNvidiaOobRedfishClient *self)
{
	if (self->insecure) {
		curl_easy_setopt(self->curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(self->curl, CURLOPT_SSL_VERIFYHOST, 0L);
	} else if (self->ca_path != NULL) {
		curl_easy_setopt(self->curl, CURLOPT_CAINFO, self->ca_path);
	}
}

static struct curl_slist *
fu_nvidia_oob_redfish_client_default_headers(FuNvidiaOobRedfishClient *self, const gchar *extra)
{
	struct curl_slist *h = NULL;
	h = curl_slist_append(h, "Accept: application/json");
	h = curl_slist_append(h, "OData-Version: 4.0");
	if (self->auth_header != NULL)
		h = curl_slist_append(h, self->auth_header);
	if (extra != NULL)
		h = curl_slist_append(h, extra);
	return h;
}

static gchar *
fu_nvidia_oob_redfish_client_resolve_bmc_host(void)
{
	const gchar *env_host = g_getenv("NVIDIA_OOB_BMC_HOST");
	if (env_host != NULL && env_host[0] != '\0')
		return g_strdup(env_host);

	if (g_file_test(NVIDIA_OOB_REDFISH_CONF_FILE, G_FILE_TEST_EXISTS)) {
		g_autoptr(GKeyFile) kf = g_key_file_new();
		if (g_key_file_load_from_file(kf,
					      NVIDIA_OOB_REDFISH_CONF_FILE,
					      G_KEY_FILE_NONE,
					      NULL)) {
			gchar *host = g_key_file_get_string(kf, "OOB", "BmcHost", NULL);
			if (host != NULL && host[0] != '\0')
				return host;
			g_free(host);
		}
	}

	return g_strdup("https://[fe80::1%25usb0]");
}

/* object lifecycle */

static void
fu_nvidia_oob_redfish_client_finalize(GObject *object)
{
	FuNvidiaOobRedfishClient *self = FU_NVIDIA_OOB_REDFISH_CLIENT(object);
	if (self->curl != NULL)
		curl_easy_cleanup(self->curl);
	g_mutex_clear(&self->curl_mutex);
	g_free(self->base_url);
	g_free(self->ca_path);
	g_free(self->auth_header);
	g_free(self->multipart_push_uri);
	G_OBJECT_CLASS(fu_nvidia_oob_redfish_client_parent_class)->finalize(object);
}

static void
fu_nvidia_oob_redfish_client_class_init(FuNvidiaOobRedfishClientClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_nvidia_oob_redfish_client_finalize;
}

static void
fu_nvidia_oob_redfish_client_init(FuNvidiaOobRedfishClient *self)
{
	g_mutex_init(&self->curl_mutex);
}

FuNvidiaOobRedfishClient *
fu_nvidia_oob_redfish_client_new(void)
{
	return g_object_new(FU_TYPE_NVIDIA_OOB_REDFISH_CLIENT, NULL);
}

/* setup -- reads token, does NOT store or use credentials */

gboolean
fu_nvidia_oob_redfish_client_setup(FuNvidiaOobRedfishClient *self, GError **error)
{
	g_autofree gchar *host = NULL;
	g_autoptr(GError) ping_error = NULL;
	g_autoptr(FwupdJsonNode) root = NULL;
	g_autoptr(FwupdJsonNode) us_node = NULL;
	const gchar *env_token = NULL;

	g_return_val_if_fail(FU_IS_NVIDIA_OOB_REDFISH_CLIENT(self), FALSE);

	/* resolve base URL */
	host = fu_nvidia_oob_redfish_client_resolve_bmc_host();
	self->base_url = g_str_has_prefix(host, "http") ? g_strdup(host)
							: g_strdup_printf("https://%s", host);

	/* TLS */
	self->insecure = g_strcmp0(g_getenv("NVIDIA_OOB_INSECURE"), "1") == 0;
	if (!self->insecure && g_file_test(NVIDIA_OOB_REDFISH_DEFAULT_CA, G_FILE_TEST_EXISTS))
		self->ca_path = g_strdup(NVIDIA_OOB_REDFISH_DEFAULT_CA);

	/* create curl handle */
	self->curl = curl_easy_init();
	if (self->curl == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "failed to create curl easy handle");
		return FALSE;
	}

	/* token lookup -- no credentials are read or stored here;
	 * run install-plugin.sh (first time) or nvidia-oob-auth.sh (re-auth)
	 * to create a fresh session and write the token */
	env_token = g_getenv("NVIDIA_OOB_TOKEN");
	if (env_token != NULL && env_token[0] != '\0') {
		self->auth_header = g_strdup_printf("X-Auth-Token: %s", env_token);
		g_debug("Using Redfish token from NVIDIA_OOB_TOKEN env var");
	} else if (g_file_test(NVIDIA_OOB_REDFISH_SESSION_FILE, G_FILE_TEST_EXISTS)) {
		g_autofree gchar *tok = NULL;
		gsize len = 0;
		if (g_file_get_contents(NVIDIA_OOB_REDFISH_SESSION_FILE, &tok, &len, NULL) &&
		    tok != NULL && tok[0] != '\0') {
			g_strchomp(tok);
			self->auth_header = g_strdup_printf("X-Auth-Token: %s", tok);
			g_debug("Using Redfish token from %s", NVIDIA_OOB_REDFISH_SESSION_FILE);
		}
	}

	if (self->auth_header == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_AUTH_FAILED,
			    "No Redfish session token found for BMC at %s.  "
			    "Run 'sudo nvidia-oob-auth.sh' to authenticate, "
			    "then restart fwupd.",
			    self->base_url);
		return FALSE;
	}

	/* sanity ping -- also validates that the token is still good */
	root = fu_nvidia_oob_redfish_client_get(self, "/redfish/v1/", &ping_error);
	if (root == NULL) {
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&ping_error),
					   "Redfish root not reachable at %s: ",
					   self->base_url);
		return FALSE;
	}

	/* read MultipartHttpPushUri from UpdateService so we POST to the
	 * correct endpoint; fall back to the legacy path if absent */
	us_node = fu_nvidia_oob_redfish_client_get(self, "/redfish/v1/UpdateService", NULL);
	if (us_node != NULL) {
		g_autoptr(FwupdJsonObject) us = fwupd_json_node_get_object(us_node, NULL);
		if (us != NULL && fwupd_json_object_has_node(us, "MultipartHttpPushUri")) {
			self->multipart_push_uri = g_strdup(
			    fwupd_json_object_get_string(us, "MultipartHttpPushUri", NULL));
		}
	}
	if (self->multipart_push_uri == NULL)
		self->multipart_push_uri = g_strdup("/redfish/v1/UpdateService");

	return TRUE;
}

/* generic GET */

static gchar *
fu_nvidia_oob_redfish_client_absolute_url(FuNvidiaOobRedfishClient *self, const gchar *path)
{
	if (g_str_has_prefix(path, "http"))
		return g_strdup(path);
	if (path[0] == '/')
		return g_strdup_printf("%s%s", self->base_url, path);
	return g_strdup_printf("%s/%s", self->base_url, path);
}

FwupdJsonNode *
fu_nvidia_oob_redfish_client_get(FuNvidiaOobRedfishClient *self, const gchar *uri, GError **error)
{
	g_autofree gchar *url = NULL;
	g_autoptr(FuRedfishResponse) resp = NULL;
	struct curl_slist *headers = NULL;
	long http_code = 0;
	CURLcode rc;

	g_return_val_if_fail(FU_IS_NVIDIA_OOB_REDFISH_CLIENT(self), NULL);

	url = fu_nvidia_oob_redfish_client_absolute_url(self, uri);
	resp = fu_redfish_response_new();

	{
		g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->curl_mutex);
		headers = fu_nvidia_oob_redfish_client_default_headers(self, NULL);

		curl_easy_reset(self->curl);
		curl_easy_setopt(self->curl, CURLOPT_URL, url);
		curl_easy_setopt(self->curl, CURLOPT_HTTPGET, 1L);
		curl_easy_setopt(self->curl, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(self->curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
		curl_easy_setopt(self->curl, CURLOPT_WRITEDATA, resp);
		curl_easy_setopt(self->curl,
				 CURLOPT_CONNECTTIMEOUT,
				 NVIDIA_OOB_REDFISH_CONNECT_TIMEOUT);
		curl_easy_setopt(self->curl, CURLOPT_TIMEOUT, NVIDIA_OOB_REDFISH_POLL_TIMEOUT);
		curl_easy_setopt(self->curl, CURLOPT_FOLLOWLOCATION, 1L);
		fu_nvidia_oob_redfish_client_apply_tls(self);

		rc = curl_easy_perform(self->curl);
		curl_slist_free_all(headers);

		if (rc == CURLE_OK)
			curl_easy_getinfo(self->curl, CURLINFO_RESPONSE_CODE, &http_code);
	}

	if (rc != CURLE_OK) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "Redfish GET %s failed: %s",
			    url,
			    curl_easy_strerror(rc));
		return NULL;
	}

	if (http_code == 401) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_AUTH_FAILED,
			    "Redfish token rejected by BMC (HTTP 401).  "
			    "Run 'sudo nvidia-oob-auth.sh' to re-authenticate, "
			    "then restart fwupd.");
		return NULL;
	}
	if (http_code < 200 || http_code >= 300) {
		/* surface 404 as a distinguishable code so poll_task() can fall
		 * back from a reaped TaskMonitor to the persistent Task resource */
		g_set_error(error,
			    G_IO_ERROR,
			    http_code == 404 ? G_IO_ERROR_NOT_FOUND : G_IO_ERROR_FAILED,
			    "Redfish GET %s returned HTTP %ld",
			    url,
			    http_code);
		return NULL;
	}

	return fu_nvidia_oob_redfish_client_parse_body(resp->body, error);
}

/* firmware inventory enumeration */

static FwupdJsonNode *
fu_nvidia_oob_redfish_client_parse_body(GByteArray *body, GError **error)
{
	/* guard against an empty/missing body BEFORE we hand it to the parser;
	 * a BMC returning 2xx with no body (e.g. session-probe responses or a
	 * reaped TaskMonitor answering HTTP 200 + empty payload) would otherwise
	 * produce an unclear parse error; returning a distinct
	 * G_IO_ERROR_INVALID_DATA here lets poll_task() recognise "resource gone"
	 * and fall back to the persistent Task resource */
	g_autoptr(FwupdJsonParser) parser = NULL;
	g_autoptr(GBytes) blob = NULL;

	if (body == NULL || body->len == 0 || body->data == NULL) {
		g_set_error_literal(
		    error,
		    G_IO_ERROR,
		    G_IO_ERROR_INVALID_DATA,
		    "empty body from BMC (Redfish response carried no JSON payload)");
		return NULL;
	}

	parser = fwupd_json_parser_new();
	blob = g_bytes_new(body->data, body->len);
	return fwupd_json_parser_load_from_bytes(parser, blob, FWUPD_JSON_LOAD_FLAG_NONE, error);
}

GPtrArray *
fu_nvidia_oob_redfish_client_list_inventory(FuNvidiaOobRedfishClient *self, GError **error)
{
	g_autoptr(FwupdJsonNode) root = NULL;
	g_autoptr(FwupdJsonObject) obj = NULL;
	g_autoptr(FwupdJsonArray) members = NULL;
	GPtrArray *uris = NULL;

	g_return_val_if_fail(FU_IS_NVIDIA_OOB_REDFISH_CLIENT(self), NULL);

	root = fu_nvidia_oob_redfish_client_get(self,
						"/redfish/v1/UpdateService/FirmwareInventory",
						error);
	if (root == NULL)
		return NULL;

	obj = fwupd_json_node_get_object(root, error);
	if (obj == NULL)
		return NULL;
	members = fwupd_json_object_get_array(obj, "Members", NULL);
	if (members == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "FirmwareInventory response missing Members array");
		return NULL;
	}

	uris = g_ptr_array_new_with_free_func(g_free);
	for (guint i = 0; i < fwupd_json_array_get_size(members); i++) {
		g_autoptr(FwupdJsonObject) m = fwupd_json_array_get_object(members, i, NULL);
		const gchar *ref = NULL;
		if (m == NULL)
			continue;
		if (!fwupd_json_object_has_node(m, "@odata.id"))
			continue;
		ref = fwupd_json_object_get_string(m, "@odata.id", NULL);
		if (ref != NULL)
			g_ptr_array_add(uris, g_strdup(ref));
	}
	return uris;
}

/* multipart push upload */

gchar *
fu_nvidia_oob_redfish_client_multipart_push(FuNvidiaOobRedfishClient *self,
					    const gchar *target_uri,
					    GBytes *firmware_blob,
					    GError **error)
{
	g_autofree gchar *url = NULL;
	g_autoptr(FwupdJsonObject) params = NULL;
	g_autoptr(FwupdJsonArray) targets = NULL;
	g_autoptr(GString) params_str = NULL;
	g_autoptr(FuRedfishResponse) resp = NULL;
	gsize fw_len = 0;
	gconstpointer fw_data = NULL;
	struct curl_slist *headers = NULL;
	long http_code = 0;
	CURLcode rc;

	g_return_val_if_fail(FU_IS_NVIDIA_OOB_REDFISH_CLIENT(self), NULL);
	g_return_val_if_fail(firmware_blob != NULL, NULL);

	url = fu_nvidia_oob_redfish_client_absolute_url(self, self->multipart_push_uri);

	/* UpdateParameters that the GB300 BMC requires:
	 *   Targets:[]                          — empty; BMC resolves components from bundle
	 * records ForceUpdate:true                    — bypasses same-version check on the BMC side
	 *   @Redfish.OperationApplyTime:Immediate — OnReset is not in the acceptable values list */
	params = fwupd_json_object_new();
	targets = fwupd_json_array_new();
	fwupd_json_object_add_array(params, "Targets", targets);
	fwupd_json_object_add_boolean(params, "ForceUpdate", TRUE);
	fwupd_json_object_add_string(params, "@Redfish.OperationApplyTime", "Immediate");
	params_str = fwupd_json_object_to_string(params, FWUPD_JSON_EXPORT_FLAG_NONE);

	fw_data = g_bytes_get_data(firmware_blob, &fw_len);
	resp = fu_redfish_response_new();
	headers = fu_nvidia_oob_redfish_client_default_headers(self, "Expect:");

	{
		g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->curl_mutex);
		curl_mime *mime = NULL;
		curl_mimepart *params_part = NULL;
		curl_mimepart *firmware_part = NULL;

		curl_easy_reset(self->curl);
		mime = curl_mime_init(self->curl);

		params_part = curl_mime_addpart(mime);
		curl_mime_name(params_part, "UpdateParameters");
		curl_mime_type(params_part, "application/json");
		curl_mime_data(params_part, params_str->str, params_str->len);

		firmware_part = curl_mime_addpart(mime);
		curl_mime_name(firmware_part, "UpdateFile");
		curl_mime_filename(firmware_part, "firmware.bin");
		curl_mime_type(firmware_part, "application/octet-stream");
		curl_mime_data(firmware_part, (const char *)fw_data, fw_len);

		curl_easy_setopt(self->curl, CURLOPT_URL, url);
		curl_easy_setopt(self->curl, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(self->curl, CURLOPT_MIMEPOST, mime);
		curl_easy_setopt(self->curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
		curl_easy_setopt(self->curl, CURLOPT_WRITEDATA, resp);
		curl_easy_setopt(self->curl, CURLOPT_HEADERFUNCTION, curl_header_cb);
		curl_easy_setopt(self->curl, CURLOPT_HEADERDATA, resp);
		curl_easy_setopt(self->curl,
				 CURLOPT_CONNECTTIMEOUT,
				 NVIDIA_OOB_REDFISH_CONNECT_TIMEOUT);
		curl_easy_setopt(self->curl, CURLOPT_TIMEOUT, NVIDIA_OOB_REDFISH_UPLOAD_TIMEOUT);
		fu_nvidia_oob_redfish_client_apply_tls(self);

		rc = curl_easy_perform(self->curl);
		curl_slist_free_all(headers);
		curl_mime_free(mime);

		if (rc == CURLE_OK)
			curl_easy_getinfo(self->curl, CURLINFO_RESPONSE_CODE, &http_code);
	}

	if (rc != CURLE_OK) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "MultipartHttpPush to %s failed: %s",
			    url,
			    curl_easy_strerror(rc));
		return NULL;
	}

	if (http_code == 401) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_AUTH_FAILED,
			    "Redfish token rejected during upload (HTTP 401).  "
			    "Run 'sudo nvidia-oob-auth.sh' to re-authenticate, then retry.");
		return NULL;
	}
	if (http_code != 202 && (http_code < 200 || http_code >= 300)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "MultipartHttpPush returned HTTP %ld: %.*s",
			    http_code,
			    (int)MIN(resp->body->len, 512),
			    (const char *)resp->body->data);
		return NULL;
	}

	if (resp->task_monitor_location == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "BMC accepted upload but returned no TaskMonitor Location");
		return NULL;
	}

	return g_steal_pointer(&resp->task_monitor_location);
}

/* task polling */

static FuOobTaskState
fu_nvidia_oob_redfish_client_map_task_state(const gchar *task_state_str)
{
	if (task_state_str == NULL)
		return FU_OOB_TASK_STATE_UNKNOWN;
	if (g_ascii_strcasecmp(task_state_str, "New") == 0)
		return FU_OOB_TASK_STATE_NEW;
	if (g_ascii_strcasecmp(task_state_str, "Running") == 0 ||
	    g_ascii_strcasecmp(task_state_str, "Starting") == 0 ||
	    g_ascii_strcasecmp(task_state_str, "Pending") == 0)
		return FU_OOB_TASK_STATE_RUNNING;
	if (g_ascii_strcasecmp(task_state_str, "Completed") == 0)
		return FU_OOB_TASK_STATE_COMPLETED;
	if (g_ascii_strcasecmp(task_state_str, "Exception") == 0 ||
	    g_ascii_strcasecmp(task_state_str, "Killed") == 0)
		return FU_OOB_TASK_STATE_EXCEPTION;
	if (g_ascii_strcasecmp(task_state_str, "Cancelled") == 0)
		return FU_OOB_TASK_STATE_CANCELLED;
	return FU_OOB_TASK_STATE_UNKNOWN;
}

/* some BMCs (notably OpenBMC) reap the TaskMonitor sub-resource the moment
 * the underlying Task transitions to Completed, while the persistent Task
 * resource at /Tasks/<id> lives on; if the Monitor URI 404s, derive the
 * task URI by stripping a trailing '/Monitor' segment and try that; the
 * Task body uses the same TaskState / PercentComplete / Messages schema,
 * so callers don't need to do anything different
 *
 * returns a newly-allocated Task URI if uri ends in '/Monitor', else NULL
 */
static gchar *
fu_nvidia_oob_redfish_client_derive_task_uri(const gchar *monitor_uri)
{
	gsize len;

	if (monitor_uri == NULL)
		return NULL;
	if (!g_str_has_suffix(monitor_uri, "/Monitor"))
		return NULL;
	len = strlen(monitor_uri) - strlen("/Monitor");
	return g_strndup(monitor_uri, len);
}

FuOobTaskState
fu_nvidia_oob_redfish_client_poll_task(FuNvidiaOobRedfishClient *self,
				       gchar **task_uri_inout,
				       guint *out_percent,
				       gchar **out_message,
				       GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FwupdJsonNode) node = NULL;
	g_autoptr(FwupdJsonObject) obj = NULL;
	const gchar *state = NULL;
	const gchar *status = NULL;
	FuOobTaskState task_state = FU_OOB_TASK_STATE_UNKNOWN;

	g_return_val_if_fail(FU_IS_NVIDIA_OOB_REDFISH_CLIENT(self), FU_OOB_TASK_STATE_UNKNOWN);
	g_return_val_if_fail(task_uri_inout != NULL && *task_uri_inout != NULL,
			     FU_OOB_TASK_STATE_UNKNOWN);

	node = fu_nvidia_oob_redfish_client_get(self, *task_uri_inout, &error_local);

	/* TaskMonitor reaped after task completion -> fall back to /Tasks/<id>
	 * and cache the resolved URI in the caller's variable so subsequent
	 * polls go straight to the Task resource without another miss
	 *
	 * the canonical "reaped" signal is HTTP 404, but the GB300 BMC has been
	 * observed to return HTTP 200 with an empty body for the reaped
	 * TaskMonitor instead -- parse_body() surfaces that as G_IO_ERROR_INVALID_DATA
	 * ("empty body from BMC ..."); treat both error codes as "Monitor is
	 * gone, try the persistent Task resource" */
	if (node == NULL && (g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
			     g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_INVALID_DATA))) {
		g_autofree gchar *fallback_uri =
		    fu_nvidia_oob_redfish_client_derive_task_uri(*task_uri_inout);
		if (fallback_uri != NULL) {
			g_clear_error(&error_local);
			node = fu_nvidia_oob_redfish_client_get(self, fallback_uri, &error_local);
			if (node != NULL) {
				g_free(*task_uri_inout);
				*task_uri_inout = g_steal_pointer(&fallback_uri);
			}
		}
	}

	if (node == NULL) {
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FU_OOB_TASK_STATE_UNKNOWN;
	}

	obj = fwupd_json_node_get_object(node, error);
	if (obj == NULL)
		return FU_OOB_TASK_STATE_UNKNOWN;
	state = fwupd_json_object_has_node(obj, "TaskState")
		    ? fwupd_json_object_get_string(obj, "TaskState", NULL)
		    : NULL;
	status = fwupd_json_object_has_node(obj, "TaskStatus")
		     ? fwupd_json_object_get_string(obj, "TaskStatus", NULL)
		     : NULL;

	if (out_percent != NULL && fwupd_json_object_has_node(obj, "PercentComplete")) {
		gint64 percent = 0;
		if (fwupd_json_object_get_integer(obj, "PercentComplete", &percent, NULL))
			*out_percent = (guint)percent;
	}

	task_state = fu_nvidia_oob_redfish_client_map_task_state(state);

	if (out_message != NULL && fwupd_json_object_has_node(obj, "Messages")) {
		g_autoptr(FwupdJsonArray) msgs = fwupd_json_object_get_array(obj, "Messages", NULL);
		if (msgs != NULL && fwupd_json_array_get_size(msgs) > 0) {
			g_autoptr(FwupdJsonObject) last =
			    fwupd_json_array_get_object(msgs,
							fwupd_json_array_get_size(msgs) - 1,
							NULL);
			if (last != NULL && fwupd_json_object_has_node(last, "Message"))
				*out_message =
				    g_strdup(fwupd_json_object_get_string(last, "Message", NULL));
		}
	}

	if (task_state == FU_OOB_TASK_STATE_COMPLETED && status != NULL &&
	    g_ascii_strcasecmp(status, "OK") != 0)
		return FU_OOB_TASK_STATE_EXCEPTION;

	return task_state;
}
