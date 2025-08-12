/*
 * Copyright 2024 Maciej Borzecki <maciej.borzecki@canonical.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "config.h"

#include <curl/curl.h>
#include <curl/easy.h>

#include "fu-uefi-dbx-snapd-notifier.h"
#include "glib.h"

struct _FuUefiDbxSnapdNotifier {
	GObject parent_instance;
	CURL *curl_template;
	struct curl_slist *req_hdrs;
};

G_DEFINE_TYPE(FuUefiDbxSnapdNotifier, fu_uefi_dbx_snapd_notifier, G_TYPE_OBJECT)

G_DEFINE_AUTOPTR_CLEANUP_FUNC(CURL, curl_easy_cleanup);

FuUefiDbxSnapdNotifier *
fu_uefi_dbx_snapd_notifier_new(void)
{
	return g_object_new(FU_TYPE_UEFI_DBX_SNAPD_NOTIFIER, NULL);
}

static void
fu_uefi_dbx_snapd_notifier_init(FuUefiDbxSnapdNotifier *self)
{
	/* default path is different inside the snap sandbox vs out */
	const char *snapd_snap_socket = fu_snap_is_in_snap() ? "/run/snapd-snap.socket"
							     : "/run/snapd.socket";
	const char *snapd_snap_socket_override = g_getenv("FWUPD_SNAPD_SNAP_SOCKET");

	self->curl_template = curl_easy_init();

	if (snapd_snap_socket_override != NULL)
		snapd_snap_socket = snapd_snap_socket_override;

	/* use snap dedicated socket when running inside a snap */
	(void)curl_easy_setopt(self->curl_template, CURLOPT_UNIX_SOCKET_PATH, snapd_snap_socket);

	self->req_hdrs = curl_slist_append(self->req_hdrs, "Content-Type: application/json");
	(void)curl_easy_setopt(self->curl_template, CURLOPT_HTTPHEADER, self->req_hdrs);
}

static void
fu_uefi_dbx_snapd_notifier_finalize(GObject *object)
{
	FuUefiDbxSnapdNotifier *self = FU_UEFI_DBX_SNAPD_NOTIFIER(object);
	curl_slist_free_all(self->req_hdrs);
	curl_easy_cleanup(self->curl_template);
	G_OBJECT_CLASS(fu_uefi_dbx_snapd_notifier_parent_class)->finalize(object);
}

static void
fu_uefi_dbx_snapd_notifier_class_init(FuUefiDbxSnapdNotifierClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_uefi_dbx_snapd_notifier_finalize;
}

/* see CURLOPT_WRITEFUNCTION(3) */
static size_t
fu_uefi_dbx_snapd_notifier_rsp_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	GByteArray *bufarr = (GByteArray *)userdata;
	gsize sz = size * nmemb;
	g_byte_array_append(bufarr, (const guint8 *)ptr, sz);
	return sz;
}

static gboolean
fu_uefi_dbx_snapd_notifier_simple_req(FuUefiDbxSnapdNotifier *self,
				      const char *endpoint,
				      const char *data,
				      gsize len,
				      GError **error)
{
	CURLcode res = -1;
	glong status_code = 0;
	g_autoptr(CURL) curl = NULL;
	g_autofree gchar *endpoint_str = NULL;
	g_autoptr(GByteArray) rsp_buf = g_byte_array_new();

	/* duplicate a preconfigured curl handle */
	curl = curl_easy_duphandle(self->curl_template);

	endpoint_str = g_strdup_printf("http://localhost%s", endpoint);

	(void)curl_easy_setopt(curl, CURLOPT_URL, endpoint_str);

	(void)curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, len);
	(void)curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);

	/* collect response for debugging */
	(void)curl_easy_setopt(curl, CURLOPT_WRITEDATA, rsp_buf);
	(void)curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fu_uefi_dbx_snapd_notifier_rsp_cb);

	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		/* TODO inspect the error */
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to communicate with snapd: %s",
			    curl_easy_strerror(res));
		return FALSE;
	}

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);

	if (status_code == 404) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "snapd notification endpoint not supported by snapd API");
		return FALSE;
	}

	if (status_code != 200) {
		g_autofree gchar *rsp = NULL;
		if (rsp_buf->len > 0) {
			/* make sure the response is printable */
			rsp = fu_strsafe((const char *)rsp_buf->data, rsp_buf->len + 1);
		}

		/* TODO check whether the response is even printable? */
		g_info("snapd request failed with status %ld, response: %s",
		       (glong)status_code,
		       rsp != NULL ? rsp : "<none>");
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "snapd request failed with status %ld",
			    (glong)status_code);
		return FALSE;
	}

	return TRUE;
}

/**
 * fu_uefi_dbx_snapd_notifier_dbx_manager_startup:
 * @self: a #FuUefiDbxSnapdNotifier
 * @error: (nullable): optional return location for an error
 *
 * Notify snapd of that the DBX manager has started.
 *
 * Returns: #TRUE if the notification was successful.
 **/
gboolean
fu_uefi_dbx_snapd_notifier_dbx_manager_startup(FuUefiDbxSnapdNotifier *self, GError **error)
{
	const char *startup_msg = "{\"action\":\"efi-secureboot-update-startup\"}";

	if (!fu_uefi_dbx_snapd_notifier_simple_req(self,
						   "/v2/system-secureboot",
						   startup_msg,
						   strlen(startup_msg),
						   error)) {
		g_prefix_error_literal(error, "failed to notify snapd of startup: ");
		return FALSE;
	}

	return TRUE;
}

/**
 * fu_uefi_dbx_snapd_notifier_dbx_update_prepare:
 * @self: a #FuUefiDbxSnapdNotifier
 * @fw_payload: payload used for the update
 * @error: (nullable): optional return location for an error
 *
 * Notify of an upcoming update to the DBX. A successful call shall initiate a
 * change tracking an update to the DBX on the snapd side.
 *
 * Returns: #TRUE if the notification was successful.
 **/
gboolean
fu_uefi_dbx_snapd_notifier_dbx_update_prepare(FuUefiDbxSnapdNotifier *self,
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

	if (!fu_uefi_dbx_snapd_notifier_simple_req(self,
						   "/v2/system-secureboot",
						   msg,
						   strlen(msg),
						   error)) {
		g_prefix_error_literal(error, "failed to notify snapd of prepare: ");
		return FALSE;
	}

	return TRUE;
}

/**
 * fu_uefi_dbx_snapd_notifier_dbx_update_cleanup:
 * @self: a #FuUefiDbxSnapdNotifier
 * @error: (nullable): optional return location for an error
 *
 * Notify of an completed update to one of secureboot key databases. A
 * successful call shall result in completion of a corresponding change on the
 * snapd side.
 *
 * Returns: #TRUE if the notification was successful.
 **/
gboolean
fu_uefi_dbx_snapd_notifier_dbx_update_cleanup(FuUefiDbxSnapdNotifier *self, GError **error)
{
	const char *msg = "{\"action\":\"efi-secureboot-update-db-cleanup\"}";

	if (!fu_uefi_dbx_snapd_notifier_simple_req(self,
						   "/v2/system-secureboot",
						   msg,
						   strlen(msg),
						   error)) {
		g_prefix_error_literal(error, "failed to notify snapd of cleanup: ");
		return FALSE;
	}

	return TRUE;
}
