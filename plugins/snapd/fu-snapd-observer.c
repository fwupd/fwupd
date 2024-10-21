/*
 * Copyright 2024 Maciej Borzecki <maciej.borzecki@canonical.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "fu-snapd-observer.h"

#include <curl/curl.h>
#include <curl/easy.h>

#include "fwupd-error.h"

#include "fu-snapd-error.h"
#include "fu-snapd-snap.h"

struct _FuSnapdObserver {
	GObject parent_instance;
	CURL *curl_template;
	struct curl_slist *req_hdrs;
};

G_DEFINE_TYPE(FuSnapdObserver, fu_snapd_observer, G_TYPE_OBJECT)

FuSnapdObserver *
fu_snapd_observer_new(void)
{
	return g_object_new(FU_TYPE_SNAPD_OBSERVER, NULL);
}

static void
fu_snapd_observer_init(FuSnapdObserver *self)
{
	self->curl_template = curl_easy_init();
	/* TODO choose different socket when in snap vs non-snap */
	if (fu_snapd_is_in_snap()) {
		curl_easy_setopt(self->curl_template,
				 CURLOPT_UNIX_SOCKET_PATH,
				 "/run/snapd-snap.socket");
	} else {
		curl_easy_setopt(self->curl_template,
				 CURLOPT_UNIX_SOCKET_PATH,
				 "/run/snapd.socket");
	}

	self->req_hdrs = curl_slist_append(self->req_hdrs, "Content-Type: application/json");
	curl_easy_setopt(self->curl_template, CURLOPT_HTTPHEADER, self->req_hdrs);
}

static void
fu_snapd_observer_finalize(GObject *object)
{
	FuSnapdObserver *self = FU_SNAPD_OBSERVER(object);
	curl_slist_free_all(self->req_hdrs);
	curl_easy_cleanup(self->curl_template);
	G_OBJECT_CLASS(fu_snapd_observer_parent_class)->finalize(object);
}

static void
fu_snapd_observer_class_init(FuSnapdObserverClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_snapd_observer_finalize;
}

/* see CURLOPT_WRITEFUNCTION(3) */
static size_t
fu_snapd_observer_rsp_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	GByteArray *bufarr = (GByteArray *)userdata;
	gsize sz = size * nmemb;
	g_byte_array_append(bufarr, (const guint8 *)ptr, sz);
	return sz;
}

static gboolean
fu_snapd_observer_simple_req(FuSnapdObserver *self,
			     const char *endpoint,
			     const char *data,
			     gsize len,
			     GError **error)
{
	CURL *curl = NULL;
	CURLcode res = -1;
	glong status_code = 0;
	g_autofree gchar *endpoint_str = NULL;
	g_autoptr(GByteArray) rsp_buf = g_byte_array_new();

	/* duplicate a preconfigured curl handle */
	curl = curl_easy_duphandle(self->curl_template);

	endpoint_str = g_strdup_printf("http://localhost%s", endpoint);

	curl_easy_setopt(curl, CURLOPT_URL, endpoint_str);

	g_debug("snapd simple request to %s with %" G_GSIZE_FORMAT " bytes of data",
		endpoint_str,
		len);
	g_debug("request data: '%s'", data);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, len);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);

	/* collect response for debugging */
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, rsp_buf);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fu_snapd_observer_rsp_cb);

	/* TODO: disable verbose */
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		/* TODO inspect the error */
		g_set_error(error,
			    FU_SNAPD_ERROR,
			    FU_SNAPD_ERROR_INTERNAL,
			    "cannot communicate with snapd: %s",
			    curl_easy_strerror(res));
		g_warning("cannot notify snapd: %s", curl_easy_strerror(res));
		return FALSE;
	}

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
	curl_easy_cleanup(curl);

	if (status_code == 404) {
		g_set_error(error,
			    FU_SNAPD_ERROR,
			    FU_SNAPD_ERROR_UNSUPPORTED,
			    "snapd notification endpoint not supported by snapd API");
		return FALSE;
	}

	if (status_code != 200) {
		if (rsp_buf->len > 0) {
			/* make sure the response is null terminated */
			g_byte_array_append(rsp_buf, (const guint8 *)"\0", 1);
		}

		/* TODO check whether the response is even printable? */
		g_warning("snapd request failed with status %ld, response: %s",
			  (glong)status_code,
			  rsp_buf->data);
		g_set_error(error,
			    FU_SNAPD_ERROR,
			    FU_SNAPD_ERROR_INTERNAL,
			    "snapd request failed with status %ld",
			    (glong)status_code);
		return FALSE;
	}

	g_debug("snapd request success");

	return TRUE;
}

gboolean
fu_snapd_observer_notify_secureboot_manager_startup(FuSnapdObserver *self, GError **error)
{
	const char *startup_msg = "{"
				  "\"action\":\"efi-secureboot-update-startup\""
				  "}";

	g_debug("snapd observer secureboot manager startup");

	if (!fu_snapd_observer_simple_req(self,
					  "/v2/system-secureboot",
					  startup_msg,
					  strlen(startup_msg),
					  error)) {
		g_prefix_error(error, "failed to notify snapd of startup: ");
		return FALSE;
	}

	g_debug("snapd notified of secureboot manager startup");

	return TRUE;
}

/**
 * fu_snapd_observer_notify_secureboot_dbx_update_prepare:
 * @self: a #FuSnapdObserver
 * @fw_payload: payload used for the update
 *
 * Notify of an upcoming update to the DBX. A successful call shall initiate a
 * change tracking an update to the DBX on the snapd side.
 *
 * Returns: TRUE if the notification was successful.
 *
 **/
gboolean
fu_snapd_observer_notify_secureboot_dbx_update_prepare(FuSnapdObserver *self,
						       GBytes *fw_payload,
						       GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw_payload, &bufsz);
	g_autofree gchar *b64data = g_base64_encode(buf, bufsz);
	g_autofree gchar *msg = g_strdup_printf("{"
						"\"action\":\"efi-secureboot-update-db-prepare\","
						"\"key-database\":\"DBX\","
						"\"payload\":\"%s\""
						"}",
						b64data);

	g_debug("snapd observer prepare, with %" G_GSIZE_FORMAT " bytes of data", bufsz);

	if (!fu_snapd_observer_simple_req(self, "/v2/system-secureboot", msg, strlen(msg), error)) {
		g_prefix_error(error, "failed to notify snapd of prepare: ");
		return FALSE;
	}

	g_debug("snapd notified of prepare");

	return TRUE;
}

/**
 * fu_snapd_observer_notify_secureboot_db_update_cleanup:
 * @self: a #FuSnapdObserver
 *
 * Notify of an completed update to one of secureboot key databases. A
 * successful call shall result in completion of a corresponding change on the
 * snapd side.
 *
 * Returns: TRUE if the notification was successful.
 *
 **/
gboolean
fu_snapd_observer_notify_secureboot_db_update_cleanup(FuSnapdObserver *self, GError **error)
{
	const char *msg = "{"
			  "\"action\":\"efi-secureboot-update-db-cleanup\""
			  "}";

	g_debug("snapd observer cleanup");

	if (!fu_snapd_observer_simple_req(self, "/v2/system-secureboot", msg, strlen(msg), error)) {
		g_prefix_error(error, "failed to notify snapd of cleanup: ");
		return FALSE;
	}

	g_debug("snapd notified of cleanup");

	return TRUE;
}
