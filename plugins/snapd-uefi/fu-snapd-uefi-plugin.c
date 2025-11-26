/*
 * Copyright 2024 Maciej Borzecki <maciej.borzecki@canonical.com>
 * Copyright 2025 Simon Johnsson <simon.johnsson@canonical.com>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <curl/curl.h>
#include <curl/easy.h>

#include "fu-snapd-uefi-plugin.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC(CURL, curl_easy_cleanup);

struct _FuSnapPlugin {
	FuPlugin parent_instance;
	gboolean snapd_integration_supported;
	CURL *curl_template;
	struct curl_slist *req_hdrs;
};

G_DEFINE_TYPE(FuSnapPlugin, fu_snapd_uefi_plugin, FU_TYPE_PLUGIN)

static const gchar *
fu_snapd_uefi_plugin_device_to_key_database(FuSnapPlugin *self, FuDevice *device)
{
	const gchar *plugin = fu_device_get_plugin(device);
	if (g_strcmp0(plugin, "uefi_dbx") == 0)
		return "DBX";
	if (g_strcmp0(plugin, "uefi_db") == 0)
		return "DB";
	if (g_strcmp0(plugin, "uefi_kek") == 0)
		return "KEK";
	if (g_strcmp0(plugin, "uefi_pk") == 0)
		return "PK";
	return NULL;
}

static void
fu_snapd_uefi_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	FuSnapPlugin *self = FU_SNAPD_UEFI_PLUGIN(plugin);

	/* is not UEFI update */
	if (fu_snapd_uefi_plugin_device_to_key_database(self, device) == NULL)
		return;

	/* if snapd integration is supported, but we are unable to use snapd, inhibit updates */
	if (!self->snapd_integration_supported) {
		fu_device_inhibit(FU_DEVICE(device),
				  "no-snapd",
				  "snapd integration for UEFI update is not available");
	}
}

/* see CURLOPT_WRITEFUNCTION(3) */
static size_t
fu_snapd_uefi_plugin_rsp_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	GByteArray *bufarr = (GByteArray *)userdata;
	gsize sz = size * nmemb;
	g_byte_array_append(bufarr, (const guint8 *)ptr, sz);
	return sz;
}

static gboolean
fu_snapd_uefi_plugin_simple_req(FuSnapPlugin *self,
				const gchar *endpoint,
				const gchar *data,
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
	(void)curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fu_snapd_uefi_plugin_rsp_cb);

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

static gboolean
fu_snapd_uefi_plugin_is_in_snap(void)
{
	return getenv("SNAP") != NULL;
}

static gboolean
fu_snapd_uefi_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuSnapPlugin *self = FU_SNAPD_UEFI_PLUGIN(plugin);
	FuContext *ctx = fu_plugin_get_context(plugin);
	const gchar *startup_msg = "{\"action\":\"efi-secureboot-update-startup\"}";
	const gchar *snapd_snap_socket_override = g_getenv("FWUPD_SNAPD_SNAP_SOCKET");
	g_autoptr(GError) error_local = NULL;

	/* only enable snapd integration if either running inside a snap or we detect that this is a
	snapd FDE setup. either of these cases makes snapd integration mandatory */
	if (!fu_snapd_uefi_plugin_is_in_snap() &&
	    !fu_context_has_flag(ctx, FU_CONTEXT_FLAG_FDE_SNAPD)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "not run as a snap and no snap FDE");
		return FALSE;
	}

	/* default path is different inside the snap sandbox vs out */
	self->curl_template = curl_easy_init();
	if (snapd_snap_socket_override != NULL) {
		(void)curl_easy_setopt(self->curl_template,
				       CURLOPT_UNIX_SOCKET_PATH,
				       snapd_snap_socket_override);
	} else {
		const gchar *snapd_snap_socket = fu_snapd_uefi_plugin_is_in_snap()
						     ? "/run/snapd-snap.socket"
						     : "/run/snapd.socket";
		/* use snap dedicated socket when running inside a snap */
		(void)curl_easy_setopt(self->curl_template,
				       CURLOPT_UNIX_SOCKET_PATH,
				       snapd_snap_socket);
	}

	/* set up curl */
	self->req_hdrs = curl_slist_append(self->req_hdrs, "Content-Type: application/json");
	(void)curl_easy_setopt(self->curl_template, CURLOPT_HTTPHEADER, self->req_hdrs);

	/* notify snapd of that the DBX manager has started */
	if (!fu_snapd_uefi_plugin_simple_req(self,
					     "/v2/system-secureboot",
					     startup_msg,
					     strlen(startup_msg),
					     &error_local)) {
		/* unless we got specific error indicating lack of relevant APIs, snapd integration
		 * is considered to be supported, even if snapd itself cannot be reached */
		self->snapd_integration_supported =
		    !g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
		g_info("snapd integration non-functional: %s", error_local->message);
	} else {
		g_info("snapd integration enabled");
		self->snapd_integration_supported = TRUE;
	}
	return TRUE;
}

static gboolean
fu_snapd_uefi_plugin_cleanup(FuSnapPlugin *self, FuDevice *device, GError **error)
{
	const gchar *msg = "{\"action\":\"efi-secureboot-update-db-cleanup\"}";

	/* notify of an completed update to one of secureboot key databases --
	 * a successful call shall result in completion of a corresponding change on the
	 * snapd side */
	if (!fu_snapd_uefi_plugin_simple_req(self,
					     "/v2/system-secureboot",
					     msg,
					     strlen(msg),
					     error)) {
		g_prefix_error_literal(error, "failed to notify snapd of cleanup: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_snapd_uefi_plugin_composite_cleanup(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	FuSnapPlugin *self = FU_SNAPD_UEFI_PLUGIN(plugin);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		if (fu_snapd_uefi_plugin_device_to_key_database(self, device) == NULL) {
			if (!fu_snapd_uefi_plugin_cleanup(self, device, error))
				return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_snapd_uefi_plugin_composite_peek_firmware(FuPlugin *plugin,
					     FuDevice *device,
					     FuFirmware *firmware,
					     FuProgress *progress,
					     FwupdInstallFlags flags,
					     GError **error)
{
	FuSnapPlugin *self = FU_SNAPD_UEFI_PLUGIN(plugin);
	gsize bufsz = 0;
	const gchar *key_database;
	const guint8 *buf;
	g_autofree gchar *b64data = NULL;
	g_autofree gchar *msg = NULL;
	g_autoptr(GBytes) fw = NULL;

	/* not interesting */
	key_database = fu_snapd_uefi_plugin_device_to_key_database(self, device);
	if (key_database == NULL)
		return TRUE;

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;
	buf = g_bytes_get_data(fw, &bufsz);
	b64data = g_base64_encode(buf, bufsz);
	msg = g_strdup_printf("{"
			      "\"action\":\"efi-secureboot-update-db-prepare\","
			      "\"key-database\":\"%s\","
			      "\"payload\":\"%s\""
			      "}",
			      key_database,
			      b64data);

	/* Notify of an upcoming update to the DBX. A successful call shall initiate a
	 * change tracking an update to the DBX on the snapd side */
	if (!fu_snapd_uefi_plugin_simple_req(self,
					     "/v2/system-secureboot",
					     msg,
					     strlen(msg),
					     error)) {
		g_prefix_error_literal(error, "failed to notify snapd of prepare: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_snapd_uefi_plugin_init(FuSnapPlugin *self)
{
}

static void
fu_snapd_uefi_plugin_finalize(GObject *object)
{
	FuSnapPlugin *self = FU_SNAPD_UEFI_PLUGIN(object);

	if (self->req_hdrs != NULL)
		curl_slist_free_all(self->req_hdrs);
	if (self->curl_template != NULL)
		curl_easy_cleanup(self->curl_template);

	G_OBJECT_CLASS(fu_snapd_uefi_plugin_parent_class)->finalize(object);
}

static void
fu_snapd_uefi_plugin_class_init(FuSnapPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);

	plugin_class->startup = fu_snapd_uefi_plugin_startup;
	plugin_class->device_registered = fu_snapd_uefi_plugin_device_registered;
	plugin_class->composite_cleanup = fu_snapd_uefi_plugin_composite_cleanup;
	plugin_class->composite_peek_firmware = fu_snapd_uefi_plugin_composite_peek_firmware;

	object_class->finalize = fu_snapd_uefi_plugin_finalize;
}
