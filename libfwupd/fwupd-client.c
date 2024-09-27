/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <gio/gio.h>
#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#endif
#ifdef HAVE_GIO_UNIX
#include <gio/gunixfdlist.h>
#endif
#ifdef HAVE_UTSNAME_H
#include <sys/utsname.h>
#endif

#include <fcntl.h>
#include <locale.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "fwupd-bios-setting.h"
#include "fwupd-client-private.h"
#include "fwupd-client-sync.h"
#include "fwupd-codec.h"
#include "fwupd-common-private.h"
#include "fwupd-device-private.h"
#include "fwupd-enums-private.h"
#include "fwupd-error.h"
#include "fwupd-plugin.h"
#include "fwupd-release.h"
#include "fwupd-remote-private.h"
#include "fwupd-request-private.h"
#include "fwupd-security-attr-private.h"

static void
fwupd_client_fixup_dbus_error(GError *error);

typedef GObject *(*FwupdClientObjectNewFunc)(void);

#define FWUPD_CLIENT_DBUS_PROXY_TIMEOUT 180000 /* ms */

/**
 * FwupdClient:
 *
 * Allow client code to call the daemon methods.
 *
 * See also: [class@FwupdDevice]
 */

static void
fwupd_client_finalize(GObject *object);

typedef struct {
	GMainContext *main_ctx;
	FwupdStatus status;
	gboolean tainted;
	gboolean interactive;
	guint percentage;
	guint32 battery_level;
	guint32 battery_threshold;
	guint download_retries;
	GMutex idle_mutex; /* for @idle_id and @idle_sources */
	guint idle_id;
	GPtrArray *idle_sources; /* element-type FwupdClientContextHelper */
	gchar *daemon_version;
	gchar *host_bkc;
	gchar *host_product;
	gchar *host_vendor;
	gchar *host_machine_id;
	gchar *host_security_id;
	gboolean only_trusted;
	GMutex proxy_mutex; /* for @proxy */
	GDBusProxy *proxy;
	GProxyResolver *proxy_resolver;
	gchar *package_name;
	gchar *package_version;
	gchar *user_agent;
	GHashTable *hints;		/* str:str */
	GHashTable *immediate_requests; /* str:FwupdRequest */
} FwupdClientPrivate;

#ifdef HAVE_LIBCURL
typedef struct {
	GPtrArray *urls;
	CURL *curl;
	curl_mime *mime;
	struct curl_slist *headers;
} FwupdCurlHelper;
#endif

enum {
	SIGNAL_CHANGED,
	SIGNAL_STATUS_CHANGED,
	SIGNAL_DEVICE_ADDED,
	SIGNAL_DEVICE_REMOVED,
	SIGNAL_DEVICE_CHANGED,
	SIGNAL_DEVICE_REQUEST,
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_STATUS,
	PROP_PERCENTAGE,
	PROP_DAEMON_VERSION,
	PROP_TAINTED,
	PROP_HOST_PRODUCT,
	PROP_HOST_VENDOR,
	PROP_HOST_MACHINE_ID,
	PROP_HOST_SECURITY_ID,
	PROP_HOST_BKC,
	PROP_INTERACTIVE,
	PROP_ONLY_TRUSTED,
	PROP_BATTERY_LEVEL,
	PROP_BATTERY_THRESHOLD,
	PROP_LAST
};

static guint signals[SIGNAL_LAST] = {0};

G_DEFINE_TYPE_WITH_PRIVATE(FwupdClient, fwupd_client, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fwupd_client_get_instance_private(o))

#ifdef HAVE_LIBCURL
G_DEFINE_AUTOPTR_CLEANUP_FUNC(CURLU, curl_url_cleanup)
typedef char CURLSTR;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(CURLSTR, curl_free)

static void
fwupd_client_curl_helper_free(FwupdCurlHelper *helper)
{
	if (helper->curl != NULL)
		curl_easy_cleanup(helper->curl);
	if (helper->mime != NULL)
		curl_mime_free(helper->mime);
	if (helper->headers != NULL)
		curl_slist_free_all(helper->headers);
	if (helper->urls != NULL)
		g_ptr_array_unref(helper->urls);
	g_free(helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FwupdCurlHelper, fwupd_client_curl_helper_free)
#endif

typedef struct {
	FwupdClient *self;
	gchar *property_name;
	guint signal_id;
	GObject *payload;
} FwupdClientContextHelper;

static void
fwupd_client_context_helper_free(FwupdClientContextHelper *helper)
{
	g_clear_object(&helper->payload);
	g_object_unref(helper->self);
	g_free(helper->property_name);
	g_free(helper);
}

/* always executed in the main context given by priv->main_ctx */
static void
fwupd_client_context_object_notify(FwupdClient *self, const gchar *property_name)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(g_main_context_is_owner(priv->main_ctx));

	/* property */
	g_object_notify(G_OBJECT(self), property_name);

	/* legacy signal name */
	if (g_strcmp0(property_name, "status") == 0)
		g_signal_emit(self, signals[SIGNAL_STATUS_CHANGED], 0, priv->status);
}

/* emits all pending context helpers in the correct GMainContext */
static gboolean
fwupd_client_context_idle_cb(gpointer user_data)
{
	FwupdClient *self = FWUPD_CLIENT(user_data);
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&priv->idle_mutex);

	g_return_val_if_fail(locker != NULL, FALSE);

	for (guint i = 0; i < priv->idle_sources->len; i++) {
		FwupdClientContextHelper *helper = g_ptr_array_index(priv->idle_sources, i);

		/* property */
		if (helper->property_name != NULL)
			fwupd_client_context_object_notify(self, helper->property_name);
		if (g_strcmp0(helper->property_name, "FwupdRequest") == 0)
			fwupd_request_emit_invalidate(FWUPD_REQUEST(helper->payload));

		/* payload signal */
		if (helper->signal_id != 0 && helper->payload != NULL)
			g_signal_emit(self, signals[helper->signal_id], 0, helper->payload);
	}

	/* all done */
	g_ptr_array_set_size(priv->idle_sources, 0);
	priv->idle_id = 0;
	return G_SOURCE_REMOVE;
}

static void
fwupd_client_context_helper(FwupdClient *self, FwupdClientContextHelper *helper)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&priv->idle_mutex);

	g_return_if_fail(locker != NULL);

	/* no source already attached to the context */
	if (priv->idle_id == 0) {
		g_autoptr(GSource) source = g_idle_source_new();
		g_source_set_callback(source, fwupd_client_context_idle_cb, self, NULL);
		priv->idle_id = g_source_attach(source, priv->main_ctx);
	}

	/* run in the correct GMainContext and thread */
	g_ptr_array_add(priv->idle_sources, helper);
}

/* run callback in the correct thread */
static void
fwupd_client_object_notify(FwupdClient *self, const gchar *property_name)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	FwupdClientContextHelper *helper = NULL;

	/* shortcut */
	if (g_main_context_is_owner(priv->main_ctx)) {
		fwupd_client_context_object_notify(self, property_name);
		return;
	}

	/* run in the correct GMainContext and thread */
	helper = g_new0(FwupdClientContextHelper, 1);
	helper->self = g_object_ref(self);
	helper->property_name = g_strdup(property_name);
	fwupd_client_context_helper(self, helper);
}

/* run callback in the correct thread */
static void
fwupd_client_request_invalidate(FwupdClient *self, FwupdRequest *request)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	FwupdClientContextHelper *helper = NULL;

	/* shortcut */
	if (g_main_context_is_owner(priv->main_ctx)) {
		fwupd_request_emit_invalidate(request);
		return;
	}

	/* run in the correct GMainContext and thread */
	helper = g_new0(FwupdClientContextHelper, 1);
	helper->self = g_object_ref(self);
	helper->property_name = g_strdup("FwupdRequest");
	helper->payload = G_OBJECT(g_object_ref(request));
	fwupd_client_context_helper(self, helper);
}

/* run callback in the correct thread */
static void
fwupd_client_signal_emit_object(FwupdClient *self, guint signal_id, GObject *payload)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	FwupdClientContextHelper *helper = NULL;

	/* shortcut */
	if (g_main_context_is_owner(priv->main_ctx)) {
		g_signal_emit(self, signals[signal_id], 0, payload);
		return;
	}

	/* run in the correct GMainContext and thread */
	helper = g_new0(FwupdClientContextHelper, 1);
	helper->self = g_object_ref(self);
	helper->signal_id = signal_id;
	helper->payload = g_object_ref(payload);
	fwupd_client_context_helper(self, helper);
}

static gchar *
fwupd_client_build_user_agent_os_release(void)
{
	const gchar *keys[] = {G_OS_INFO_KEY_NAME, G_OS_INFO_KEY_VERSION_ID, "VARIANT", NULL};
	g_autoptr(GPtrArray) ids_os = g_ptr_array_new_with_free_func(g_free);

	/* create an array of the keys that exist */
	for (guint i = 0; keys[i] != NULL; i++) {
		g_autofree gchar *value = g_get_os_info(keys[i]);
		if (value != NULL)
			g_ptr_array_add(ids_os, g_steal_pointer(&value));
	}
	if (ids_os->len == 0)
		return NULL;
	g_ptr_array_add(ids_os, NULL);
	return g_strjoinv(" ", (gchar **)ids_os->pdata);
}

static gchar *
fwupd_client_build_user_agent_system(void)
{
#ifdef HAVE_UTSNAME_H
	struct utsname name_tmp;
#endif
	g_autofree gchar *locale = NULL;
	g_autofree gchar *os_release = NULL;
	g_autoptr(GPtrArray) ids = g_ptr_array_new_with_free_func(g_free);

	/* system, architecture and kernel, e.g. "Linux i686 4.14.5" */
#ifdef HAVE_UTSNAME_H
	memset(&name_tmp, 0, sizeof(struct utsname));
	if (uname(&name_tmp) >= 0) {
		g_ptr_array_add(ids,
				g_strdup_printf("%s %s %s",
						name_tmp.sysname,
						name_tmp.machine,
						name_tmp.release));
	}
#endif

	/* current locale, e.g. "en-gb" */
#ifdef HAVE_LC_MESSAGES
	locale = g_strdup(setlocale(LC_MESSAGES, NULL));
#endif
	if (locale != NULL) {
		g_strdelimit(locale, ".", '\0');
		g_strdelimit(locale, "_", '-');
		g_ptr_array_add(ids, g_steal_pointer(&locale));
	}

	/* OS release, e.g. "Fedora 27 Workstation" */
	os_release = fwupd_client_build_user_agent_os_release();
	if (os_release != NULL)
		g_ptr_array_add(ids, g_steal_pointer(&os_release));

	/* convert to string */
	if (ids->len == 0)
		return NULL;
	g_ptr_array_add(ids, NULL);
	return g_strjoinv("; ", (gchar **)ids->pdata);
}

static void
fwupd_client_rebuild_user_agent(FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GString) str = g_string_new(NULL);
	g_autofree gchar *system = NULL;

	/* application name and version */
	if (priv->package_name != NULL && priv->package_version != NULL)
		g_string_append_printf(str, "%s/%s ", priv->package_name, priv->package_version);

	/* system information */
	system = fwupd_client_build_user_agent_system();
	if (system != NULL)
		g_string_append_printf(str, "(%s) ", system);

	/* am running in CI */
	if (g_getenv("CI") != NULL)
		g_string_append_printf(str, "ci/%s ", g_getenv("CI"));

	/* platform, unless the application name is fwupd itself */
	if (priv->daemon_version != NULL && g_strcmp0(priv->package_name, "fwupd") != 0)
		g_string_append_printf(str, "fwupd/%s", priv->daemon_version);

	/* success */
	g_free(priv->user_agent);
	priv->user_agent = g_string_free(g_steal_pointer(&str), FALSE);
}

static void
fwupd_client_set_host_vendor(FwupdClient *self, const gchar *host_vendor)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->host_vendor, host_vendor) == 0)
		return;

	g_free(priv->host_vendor);
	priv->host_vendor = g_strdup(host_vendor);
	fwupd_client_object_notify(self, "host-vendor");
}

static void
fwupd_client_set_host_product(FwupdClient *self, const gchar *host_product)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->host_product, host_product) == 0)
		return;

	g_free(priv->host_product);
	priv->host_product = g_strdup(host_product);
	fwupd_client_object_notify(self, "host-product");
}

static void
fwupd_client_set_host_machine_id(FwupdClient *self, const gchar *host_machine_id)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->host_machine_id, host_machine_id) == 0)
		return;

	g_free(priv->host_machine_id);
	priv->host_machine_id = g_strdup(host_machine_id);
	fwupd_client_object_notify(self, "host-machine-id");
}

static void
fwupd_client_set_host_security_id(FwupdClient *self, const gchar *host_security_id)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->host_security_id, host_security_id) == 0)
		return;

	g_free(priv->host_security_id);
	priv->host_security_id = g_strdup(host_security_id);
	fwupd_client_object_notify(self, "host-security-id");
}

/**
 * fwupd_client_set_daemon_version:
 * @self: a #FwupdClient
 * @daemon_version: A semantic version, e.g. "1.2.3"
 *
 * Sets the daemon version number.
 *
 * Since: 1.8.11
 **/
void
fwupd_client_set_daemon_version(FwupdClient *self, const gchar *daemon_version)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->daemon_version, daemon_version) == 0)
		return;

	g_free(priv->daemon_version);
	priv->daemon_version = g_strdup(daemon_version);
	fwupd_client_object_notify(self, "daemon-version");
	fwupd_client_rebuild_user_agent(self);
}

/**
 * fwupd_client_download_set_retries:
 * @self: a #FwupdClient
 * @retries: number of tries, defaulting to 0
 *
 * Sets the number of retries should be attempted on transient download errors.
 *
 * Since: 1.9.19
 **/
void
fwupd_client_download_set_retries(FwupdClient *self, guint retries)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_CLIENT(self));
	priv->download_retries = retries;
}

static void
fwupd_client_set_host_bkc(FwupdClient *self, const gchar *host_bkc)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);

	/* emulate a D-Bus maybe type */
	if (g_strcmp0(host_bkc, "") == 0)
		host_bkc = NULL;

	/* not changed */
	if (g_strcmp0(priv->host_bkc, host_bkc) == 0)
		return;

	g_free(priv->host_bkc);
	priv->host_bkc = g_strdup(host_bkc);
	fwupd_client_object_notify(self, "host-bkc");
}

static void
fwupd_client_set_status(FwupdClient *self, FwupdStatus status)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	if (priv->status == status)
		return;
	priv->status = status;
	g_debug("Emitting ::status-changed() [%s]", fwupd_status_to_string(priv->status));
	fwupd_client_object_notify(self, "status");
}

static void
fwupd_client_set_percentage(FwupdClient *self, guint percentage)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	if (priv->percentage == percentage)
		return;
	priv->percentage = percentage;
	fwupd_client_object_notify(self, "percentage");
}

static void
fwupd_client_set_battery_level(FwupdClient *self, guint32 battery_level)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	if (priv->battery_level == battery_level)
		return;
	priv->battery_level = battery_level;
	g_object_notify(G_OBJECT(self), "battery-level");
}

static void
fwupd_client_set_battery_threshold(FwupdClient *self, guint32 battery_threshold)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	if (priv->battery_threshold == battery_threshold)
		return;
	priv->battery_threshold = battery_threshold;
	g_object_notify(G_OBJECT(self), "battery-threshold");
}

static void
fwupd_client_properties_changed_cb(GDBusProxy *proxy,
				   GVariant *changed_properties,
				   GStrv invalidated_properties,
				   FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GVariantDict) dict = NULL;

	/* print to the console */
	dict = g_variant_dict_new(changed_properties);
	if (g_variant_dict_contains(dict, "Status")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property(proxy, "Status");
		if (val != NULL)
			fwupd_client_set_status(self, g_variant_get_uint32(val));
	}
	if (g_variant_dict_contains(dict, "Tainted")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property(proxy, "Tainted");
		if (val != NULL) {
			priv->tainted = g_variant_get_boolean(val);
			fwupd_client_object_notify(self, "tainted");
		}
	}
	if (g_variant_dict_contains(dict, "Interactive")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property(proxy, "Interactive");
		if (val != NULL) {
			priv->interactive = g_variant_get_boolean(val);
			fwupd_client_object_notify(self, "interactive");
		}
	}
	if (g_variant_dict_contains(dict, "Percentage")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property(proxy, "Percentage");
		if (val != NULL)
			fwupd_client_set_percentage(self, g_variant_get_uint32(val));
	}
	if (g_variant_dict_contains(dict, FWUPD_RESULT_KEY_BATTERY_LEVEL)) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property(proxy, FWUPD_RESULT_KEY_BATTERY_LEVEL);
		if (val != NULL)
			fwupd_client_set_battery_level(self, g_variant_get_uint32(val));
	}
	if (g_variant_dict_contains(dict, FWUPD_RESULT_KEY_BATTERY_THRESHOLD)) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property(proxy, FWUPD_RESULT_KEY_BATTERY_THRESHOLD);
		if (val != NULL)
			fwupd_client_set_battery_threshold(self, g_variant_get_uint32(val));
	}
	if (g_variant_dict_contains(dict, "DaemonVersion")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property(proxy, "DaemonVersion");
		if (val != NULL)
			fwupd_client_set_daemon_version(self, g_variant_get_string(val, NULL));
	}
	if (g_variant_dict_contains(dict, "HostBkc")) {
		g_autoptr(GVariant) val = g_dbus_proxy_get_cached_property(proxy, "HostBkc");
		if (val != NULL)
			fwupd_client_set_host_bkc(self, g_variant_get_string(val, NULL));
	}
	if (g_variant_dict_contains(dict, "HostVendor")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property(proxy, "HostVendor");
		if (val != NULL)
			fwupd_client_set_host_vendor(self, g_variant_get_string(val, NULL));
	}
	if (g_variant_dict_contains(dict, "HostProduct")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property(proxy, "HostProduct");
		if (val != NULL)
			fwupd_client_set_host_product(self, g_variant_get_string(val, NULL));
	}
	if (g_variant_dict_contains(dict, "HostMachineId")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property(proxy, "HostMachineId");
		if (val != NULL)
			fwupd_client_set_host_machine_id(self, g_variant_get_string(val, NULL));
	}
	if (g_variant_dict_contains(dict, "HostSecurityId")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property(proxy, "HostSecurityId");
		if (val != NULL)
			fwupd_client_set_host_security_id(self, g_variant_get_string(val, NULL));
	}
	if (g_variant_dict_contains(dict, "OnlyTrusted")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property(proxy, "OnlyTrusted");
		if (val != NULL) {
			priv->only_trusted = g_variant_get_boolean(val);
			fwupd_client_object_notify(self, "only-trusted");
		}
	}
}

static void
fwupd_client_signal_cb(GDBusProxy *proxy,
		       const gchar *sender_name,
		       const gchar *signal_name,
		       GVariant *parameters,
		       FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdDevice) dev = NULL;
	g_autoptr(GError) error = NULL;
	if (g_strcmp0(signal_name, "Changed") == 0) {
		g_debug("Emitting ::changed()");
		g_signal_emit(self, signals[SIGNAL_CHANGED], 0);
		return;
	}
	if (g_strcmp0(signal_name, "DeviceAdded") == 0) {
		dev = fwupd_device_new();
		if (!fwupd_codec_from_variant(FWUPD_CODEC(dev), parameters, &error)) {
			g_warning("failed to build FwupdDevice[DeviceAdded]: %s", error->message);
			return;
		}
		g_debug("Emitting ::device-added(%s)", fwupd_device_get_id(dev));
		fwupd_client_signal_emit_object(self, SIGNAL_DEVICE_ADDED, G_OBJECT(dev));
		return;
	}
	if (g_strcmp0(signal_name, "DeviceRemoved") == 0) {
		dev = fwupd_device_new();
		if (!fwupd_codec_from_variant(FWUPD_CODEC(dev), parameters, &error)) {
			g_warning("failed to build FwupdDevice[DeviceRemoved]: %s", error->message);
			return;
		}
		g_debug("Emitting ::device-removed(%s)", fwupd_device_get_id(dev));
		fwupd_client_signal_emit_object(self, SIGNAL_DEVICE_REMOVED, G_OBJECT(dev));
		return;
	}
	if (g_strcmp0(signal_name, "DeviceChanged") == 0) {
		dev = fwupd_device_new();
		if (!fwupd_codec_from_variant(FWUPD_CODEC(dev), parameters, &error)) {
			g_warning("failed to build FwupdDevice[DeviceChanged]: %s", error->message);
			return;
		}
		g_debug("Emitting ::device-changed(%s)", fwupd_device_get_id(dev));
		fwupd_client_signal_emit_object(self, SIGNAL_DEVICE_CHANGED, G_OBJECT(dev));

		/* invalidate request */
		if (fwupd_device_get_status(dev) != FWUPD_STATUS_WAITING_FOR_USER) {
			FwupdRequest *req =
			    g_hash_table_lookup(priv->immediate_requests, fwupd_device_get_id(dev));
			if (req != NULL) {
				fwupd_client_request_invalidate(self, req);
				g_hash_table_remove(priv->immediate_requests,
						    fwupd_device_get_id(dev));
			}
		}
		return;
	}
	if (g_strcmp0(signal_name, "DeviceRequest") == 0) {
		g_autoptr(FwupdRequest) req = fwupd_request_new();
		if (!fwupd_codec_from_variant(FWUPD_CODEC(req), parameters, &error)) {
			g_warning("failed to convert DeviceRequest: %s", error->message);
			return;
		}
		g_debug("Emitting ::device-request(%s)", fwupd_request_get_id(req));
		fwupd_client_signal_emit_object(self, SIGNAL_DEVICE_REQUEST, G_OBJECT(req));

		/* we may need to invalidate this later */
		if (fwupd_request_get_kind(req) == FWUPD_REQUEST_KIND_IMMEDIATE &&
		    fwupd_request_get_device_id(req) != NULL) {
			g_hash_table_insert(priv->immediate_requests,
					    g_strdup(fwupd_request_get_device_id(req)),
					    g_object_ref(req));
		}
		return;
	}
	g_debug("Unknown signal name '%s' from %s", signal_name, sender_name);
}

/**
 * fwupd_client_get_main_context:
 * @self: a #FwupdClient
 *
 * Gets the internal #GMainContext to use for synchronous methods.
 * By default the value is set a new #GMainContext.
 *
 * Returns: (transfer full): the main context
 *
 * Since: 1.5.3
 **/
GMainContext *
fwupd_client_get_main_context(FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	if (priv->main_ctx != NULL)
		return g_main_context_ref(priv->main_ctx);
	return g_main_context_new();
}

/**
 * fwupd_client_set_main_context:
 * @self: a #FwupdClient
 * @main_ctx: (nullable): the global default main context to use
 *
 * Sets the internal main context to use for returning progress signals.
 *
 * Since: 1.5.3
 **/
void
fwupd_client_set_main_context(FwupdClient *self, GMainContext *main_ctx)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_CLIENT(self));
	if (main_ctx == priv->main_ctx)
		return;
	g_clear_pointer(&priv->main_ctx, g_main_context_unref);
	if (main_ctx != NULL)
		priv->main_ctx = g_main_context_ref(main_ctx);
}

/**
 * fwupd_client_ensure_networking:
 * @self: a #FwupdClient
 * @error: (nullable): optional return location for an error
 *
 * Sets up the client networking support ready for use. Most other download and
 * upload methods call this automatically, and do you only need to call this if
 * the session is being used outside the [class@FwupdClient].
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.5
 **/
gboolean
fwupd_client_ensure_networking(FwupdClient *self, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* check the user agent is sane */
	if (priv->user_agent == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "user agent unset");
		return FALSE;
	}
	if (g_strstr_len(priv->user_agent, -1, "fwupd/") == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "user agent unsuitable; fwupd version required");
		return FALSE;
	}
	return TRUE;
}

#ifdef HAVE_LIBCURL
static int
fwupd_client_progress_callback_cb(void *clientp,
				  curl_off_t dltotal,
				  curl_off_t dlnow,
				  curl_off_t ultotal,
				  curl_off_t ulnow)
{
	FwupdClient *self = FWUPD_CLIENT(clientp);
	FwupdClientPrivate *priv = GET_PRIVATE(self);

	/* calculate percentage */
	if (dltotal > 0 && dlnow >= 0 && dlnow <= dltotal) {
		guint percentage = (guint)((100 * dlnow) / dltotal);
		if (priv->percentage != percentage)
			g_info("download progress: %u%%", percentage);
		fwupd_client_set_percentage(self, percentage);
	} else if (ultotal > 0 && ulnow >= 0 && ulnow <= ultotal) {
		guint percentage = (guint)((100 * ulnow) / ultotal);
		if (priv->percentage != percentage)
			g_info("upload progress: %u%%", percentage);
		fwupd_client_set_percentage(self, percentage);
	}

	return 0;
}

static gboolean
fwupd_client_curl_helper_set_proxy(FwupdClient *self,
				   FwupdCurlHelper *helper,
				   const gchar *url,
				   GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_auto(GStrv) proxies = NULL;

	proxies = g_proxy_resolver_lookup(priv->proxy_resolver, url, NULL, error);
	if (proxies == NULL) {
		g_prefix_error(error, "failed to lookup proxy for %s: ", url);
		return FALSE;
	}
	if (g_strcmp0(proxies[0], "direct://") != 0)
		(void)curl_easy_setopt(helper->curl, CURLOPT_PROXY, proxies[0]);

	/* success */
	return TRUE;
}

static FwupdCurlHelper *
fwupd_client_curl_new(FwupdClient *self, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdCurlHelper) helper = g_new0(FwupdCurlHelper, 1);

	/* check the user agent is sane */
	if (!fwupd_client_ensure_networking(self, error))
		return NULL;

	/* create the session */
	helper->curl = curl_easy_init();
	if (helper->curl == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to setup networking");
		return NULL;
	}
	if (g_getenv("FWUPD_CURL_VERBOSE") != NULL)
		(void)curl_easy_setopt(helper->curl, CURLOPT_VERBOSE, 1L);
	(void)curl_easy_setopt(helper->curl,
			       CURLOPT_XFERINFOFUNCTION,
			       fwupd_client_progress_callback_cb);
	(void)curl_easy_setopt(helper->curl, CURLOPT_XFERINFODATA, self);
	(void)curl_easy_setopt(helper->curl, CURLOPT_USERAGENT, priv->user_agent);
	(void)curl_easy_setopt(helper->curl, CURLOPT_CONNECTTIMEOUT, 60L);
	(void)curl_easy_setopt(helper->curl, CURLOPT_NOPROGRESS, 0L);
	(void)curl_easy_setopt(helper->curl, CURLOPT_FOLLOWLOCATION, 1L);
	(void)curl_easy_setopt(helper->curl, CURLOPT_MAXREDIRS, 5L);
#ifdef _WIN32
	(void)curl_easy_setopt(helper->curl, CURLOPT_CAINFO, "ca-bundle.crt");
#endif
#if CURL_AT_LEAST_VERSION(7, 71, 0)
	(void)curl_easy_setopt(helper->curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
#endif

	/* this disables the double-compression of the firmware.xml.gz file */
	(void)curl_easy_setopt(helper->curl, CURLOPT_HTTP_CONTENT_DECODING, 0L);
	return g_steal_pointer(&helper);
}
#endif

static void
fwupd_client_set_hints_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		/* new libfwupd and old daemon, just swallow the error */
		if (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD)) {
			g_debug("ignoring %s", error->message);
			g_task_return_boolean(task, TRUE);
			return;
		}
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}

static void
fwupd_client_connect_get_proxy_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	GVariantBuilder builder;
	GHashTableIter iter;
	gpointer key, value;
	FwupdClient *self = g_task_get_source_object(task);
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	GCancellable *cancellable = g_task_get_cancellable(task);
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;
	g_autoptr(GVariant) val2 = NULL;
	g_autoptr(GVariant) val3 = NULL;
	g_autoptr(GVariant) val4 = NULL;
	g_autoptr(GVariant) val5 = NULL;
	g_autoptr(GVariant) val6 = NULL;
	g_autoptr(GVariant) val7 = NULL;
	g_autoptr(GVariant) val8 = NULL;
	g_autoptr(GVariant) val9 = NULL;
	g_autoptr(GVariant) val10 = NULL;
	g_autoptr(GMutexLocker) locker = NULL;

	proxy = g_dbus_proxy_new_finish(res, &error);
	if (proxy == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* another thread did this for us */
	locker = g_mutex_locker_new(&priv->proxy_mutex);
	if (locker == NULL || priv->proxy != NULL) {
		g_task_return_boolean(task, TRUE);
		return;
	}
	priv->proxy = g_steal_pointer(&proxy);

	/* connect signals, etc. */
	g_signal_connect(G_DBUS_PROXY(priv->proxy),
			 "g-properties-changed",
			 G_CALLBACK(fwupd_client_properties_changed_cb),
			 self);
	g_signal_connect(G_DBUS_PROXY(priv->proxy),
			 "g-signal",
			 G_CALLBACK(fwupd_client_signal_cb),
			 self);
	val = g_dbus_proxy_get_cached_property(priv->proxy, "DaemonVersion");
	if (val != NULL)
		fwupd_client_set_daemon_version(self, g_variant_get_string(val, NULL));
	val2 = g_dbus_proxy_get_cached_property(priv->proxy, "Tainted");
	if (val2 != NULL)
		priv->tainted = g_variant_get_boolean(val2);
	val3 = g_dbus_proxy_get_cached_property(priv->proxy, "Status");
	if (val3 != NULL)
		fwupd_client_set_status(self, g_variant_get_uint32(val3));
	val4 = g_dbus_proxy_get_cached_property(priv->proxy, "Interactive");
	if (val4 != NULL)
		priv->interactive = g_variant_get_boolean(val4);
	val5 = g_dbus_proxy_get_cached_property(priv->proxy, "HostProduct");
	if (val5 != NULL)
		fwupd_client_set_host_product(self, g_variant_get_string(val5, NULL));
	val10 = g_dbus_proxy_get_cached_property(priv->proxy, "HostVendor");
	if (val10 != NULL)
		fwupd_client_set_host_vendor(self, g_variant_get_string(val10, NULL));
	val6 = g_dbus_proxy_get_cached_property(priv->proxy, "HostMachineId");
	if (val6 != NULL)
		fwupd_client_set_host_machine_id(self, g_variant_get_string(val6, NULL));
	val7 = g_dbus_proxy_get_cached_property(priv->proxy, "HostSecurityId");
	if (val7 != NULL)
		fwupd_client_set_host_security_id(self, g_variant_get_string(val7, NULL));
	val8 = g_dbus_proxy_get_cached_property(priv->proxy, "HostBkc");
	if (val8 != NULL)
		fwupd_client_set_host_bkc(self, g_variant_get_string(val8, NULL));
	val9 = g_dbus_proxy_get_cached_property(priv->proxy, "OnlyTrusted");
	if (val9 != NULL)
		priv->only_trusted = g_variant_get_boolean(val9);

	/* build client hints */
	g_variant_builder_init(&builder, G_VARIANT_TYPE("a{ss}"));
	g_hash_table_iter_init(&iter, priv->hints);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		if (value == NULL)
			continue;
		g_variant_builder_add(&builder, "{ss}", (const gchar *)key, (const gchar *)value);
	}

	/* only supported on fwupd >= 1.7.1 */
	g_dbus_proxy_call(priv->proxy,
			  "SetHints",
			  g_variant_new("(a{ss})", &builder),
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_set_hints_cb,
			  g_steal_pointer(&task));
}

static void
fwupd_client_connect_get_connection_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	GCancellable *cancellable = g_task_get_cancellable(task);
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GError) error = NULL;

	connection = g_dbus_connection_new_for_address_finish(res, &error);
	if (connection == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	g_dbus_proxy_new(connection,
			 G_DBUS_PROXY_FLAGS_NONE,
			 NULL,
			 NULL, /* bus_name */
			 FWUPD_DBUS_PATH,
			 FWUPD_DBUS_INTERFACE,
			 cancellable,
			 fwupd_client_connect_get_proxy_cb,
			 g_steal_pointer(&task));
}

/**
 * fwupd_client_connect_async:
 * @self: a #FwupdClient
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Sets up the client ready for use. This is probably the first method you call
 * when wanting to use libfwupd in an asynchronous manner.
 *
 * Other methods such as [method@FwupdClient.get_devices_async] should only be called
 * after [method@FwupdClient.connect_finish] has been called without an error.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_connect_async(FwupdClient *self,
			   GCancellable *cancellable,
			   GAsyncReadyCallback callback,
			   gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	const gchar *socket_filename = g_getenv("FWUPD_DBUS_SOCKET");
	g_autofree gchar *socket_address = NULL;
	g_autoptr(GTask) task = g_task_new(self, cancellable, callback, callback_data);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&priv->proxy_mutex);

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));

	g_return_if_fail(locker != NULL);

	/* nothing to do */
	if (priv->proxy != NULL) {
		g_task_return_boolean(task, TRUE);
		return;
	}

#ifdef FWUPD_DBUS_SOCKET_ADDRESS
	/* this is set for macOS and Windows */
	if (socket_filename == NULL)
		socket_filename = g_strdup(FWUPD_DBUS_SOCKET_ADDRESS);
#endif

	/* convert from filename to address, if required */
	if (socket_filename != NULL) {
		if (g_strrstr(socket_filename, "=") == NULL) {
			socket_address = g_strdup_printf("unix:path=%s", socket_filename);
		} else {
			socket_address = g_strdup(socket_filename);
		}
	}

	/* use peer-to-peer only if the env variable is set */
	if (socket_address != NULL) {
		g_dbus_connection_new_for_address(socket_address,
						  G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
						  NULL,
						  cancellable,
						  fwupd_client_connect_get_connection_cb,
						  g_steal_pointer(&task));
		return;
	}

	/* typical case */
	g_dbus_proxy_new_for_bus(G_BUS_TYPE_SYSTEM,
				 G_DBUS_PROXY_FLAGS_NONE,
				 NULL,
				 FWUPD_DBUS_SERVICE,
				 FWUPD_DBUS_PATH,
				 FWUPD_DBUS_INTERFACE,
				 cancellable,
				 fwupd_client_connect_get_proxy_cb,
				 g_steal_pointer(&task));
}

/**
 * fwupd_client_connect_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@Client.connect_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_connect_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

/**
 * fwupd_client_disconnect: (skip)
 * @self: a #FwupdClient
 * @error: (nullable): optional return location for an error
 *
 * Tears down client after use. You only need to call this method if you are:
 *
 * - connecting to the daemon in one thread and finalizing the client in another one
 * - to change the `FWUPD_DBUS_SOCKET` for a different peer-to-peer connection
 * - to add or change connection hints as specified by [method@FwupdClient.add_hint].
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.0
 **/
gboolean
fwupd_client_disconnect(FwupdClient *self, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&priv->proxy_mutex);

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	g_return_val_if_fail(locker != NULL, FALSE);

	/* sanity check */
	if (priv->proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "not connected");
		return FALSE;
	}
	g_signal_handlers_disconnect_by_data(priv->proxy, self);
	g_clear_object(&priv->proxy);

	/* success */
	return TRUE;
}

static void
fwupd_client_quit_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClient *self = FWUPD_CLIENT(g_task_get_source_object(G_TASK(user_data)));
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_clear_object(&priv->proxy);
	g_task_return_boolean(task, TRUE);
}

/**
 * fwupd_client_quit_async:
 * @self: a #FwupdClient
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Asks the daemon to quit. This can only be called by the root user.
 *
 * NOTE: This will only actually quit if an install is not already in progress.
 *
 * Since: 1.8.11
 **/
void
fwupd_client_quit_async(FwupdClient *self,
			GCancellable *cancellable,
			GAsyncReadyCallback callback,
			gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "Quit",
			  NULL,
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_quit_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_quit_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.quit_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.11
 **/
gboolean
fwupd_client_quit_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

static void
fwupd_client_fixup_dbus_error(GError *error)
{
	g_autofree gchar *name = NULL;

	g_return_if_fail(error != NULL);

	/* is a remote error? */
	if (!g_dbus_error_is_remote_error(error))
		return;

	/* parse the remote error */
	name = g_dbus_error_get_remote_error(error);
	if (name == NULL)
		return;
	if (g_str_has_prefix(name, FWUPD_DBUS_INTERFACE)) {
		error->domain = FWUPD_ERROR;
		error->code = fwupd_error_from_string(name);
	} else if (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN) ||
		   g_error_matches(error, G_IO_ERROR, G_IO_ERROR_DBUS_ERROR)) {
		error->domain = FWUPD_ERROR;
		error->code = FWUPD_ERROR_NOT_SUPPORTED;
	} else {
		error->domain = FWUPD_ERROR;
		error->code = FWUPD_ERROR_INTERNAL;
	}
	g_dbus_error_strip_remote_error(error);
}

static void
fwupd_client_get_host_security_attrs_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	array = fwupd_codec_array_from_variant(val, FWUPD_TYPE_SECURITY_ATTR, &error);
	if (array == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_pointer(task, g_steal_pointer(&array), (GDestroyNotify)g_ptr_array_unref);
}

/**
 * fwupd_client_get_host_security_attrs_async:
 * @self: a #FwupdClient
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets all the host security attributes from the daemon.
 *
 * You must have called [method@Client.connect_async] on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_host_security_attrs_async(FwupdClient *self,
					   GCancellable *cancellable,
					   GAsyncReadyCallback callback,
					   gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "GetHostSecurityAttrs",
			  NULL,
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_get_host_security_attrs_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_get_host_security_attrs_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.get_host_security_attrs_async].
 *
 * Returns: (element-type FwupdSecurityAttr) (transfer container): attributes
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_host_security_attrs_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(g_task_is_valid(res, self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

static void
fwupd_client_modify_bios_setting_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}

/**
 * fwupd_client_modify_bios_setting_async:
 * @self: a #FwupdClient
 * @settings: (transfer container): BIOS settings
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Modifies a BIOS setting using kernel API.
 * The daemon will only respond to this request with proper permissions.
 *
 * Since: 1.8.4
 **/
void
fwupd_client_modify_bios_setting_async(FwupdClient *self,
				       GHashTable *settings,
				       GCancellable *cancellable,
				       GAsyncReadyCallback callback,
				       gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	GVariantBuilder builder;
	GHashTableIter iter;
	gpointer key, value;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(settings != NULL);
	g_return_if_fail(g_hash_table_size(settings) > 0);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_variant_builder_init(&builder, G_VARIANT_TYPE("a{ss}"));
	g_hash_table_iter_init(&iter, settings);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		if (value == NULL)
			continue;
		g_variant_builder_add(&builder, "{ss}", (const gchar *)key, (const gchar *)value);
	}
	g_dbus_proxy_call(priv->proxy,
			  "SetBiosSettings",
			  g_variant_new("(a{ss})", &builder),
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_modify_bios_setting_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_modify_bios_setting_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.modify_bios_setting_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.4
 **/
gboolean
fwupd_client_modify_bios_setting_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

static void
fwupd_client_get_bios_settings_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	array = fwupd_codec_array_from_variant(val, FWUPD_TYPE_BIOS_SETTING, &error);
	if (array == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_pointer(task, g_steal_pointer(&array), (GDestroyNotify)g_ptr_array_unref);
}

/**
 * fwupd_client_get_bios_settings_async:
 * @self: a #FwupdClient
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets all the host security attributes from the daemon.
 *
 * You must have called [method@Client.connect_async] on @self before using
 * this method.
 *
 * Since: 1.8.4
 **/
void
fwupd_client_get_bios_settings_async(FwupdClient *self,
				     GCancellable *cancellable,
				     GAsyncReadyCallback callback,
				     gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "GetBiosSettings",
			  NULL,
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_get_bios_settings_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_get_bios_settings_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.get_bios_settings_async].
 *
 * Returns: (element-type FwupdBiosSetting) (transfer container): attributes
 *
 * Since: 1.8.4
 **/
GPtrArray *
fwupd_client_get_bios_settings_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(g_task_is_valid(res, self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

static void
fwupd_client_get_host_security_events_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	array = fwupd_codec_array_from_variant(val, FWUPD_TYPE_SECURITY_ATTR, &error);
	if (array == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_pointer(task, g_steal_pointer(&array), (GDestroyNotify)g_ptr_array_unref);
}

/**
 * fwupd_client_get_host_security_events_async:
 * @self: a #FwupdClient
 * @limit: maximum number of events, or 0 for no limit
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets all the host security events from the daemon.
 *
 * You must have called [method@Client.connect_async] on @self before using
 * this method.
 *
 * Since: 1.7.1
 **/
void
fwupd_client_get_host_security_events_async(FwupdClient *self,
					    guint limit,
					    GCancellable *cancellable,
					    GAsyncReadyCallback callback,
					    gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "GetHostSecurityEvents",
			  g_variant_new("(u)", limit),
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_get_host_security_events_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_get_host_security_events_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.get_host_security_events_async].
 *
 * Returns: (element-type FwupdSecurityAttr) (transfer container): attributes
 *
 * Since: 1.7.1
 **/
GPtrArray *
fwupd_client_get_host_security_events_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(g_task_is_valid(res, self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

static GHashTable *
fwupd_client_report_metadata_hash_from_variant(GVariant *value)
{
	GHashTable *hash;
	gsize sz;
	g_autoptr(GVariant) untuple = NULL;

	hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	untuple = g_variant_get_child_value(value, 0);
	sz = g_variant_n_children(untuple);
	for (guint i = 0; i < sz; i++) {
		g_autoptr(GVariant) data = NULL;
		const gchar *key = NULL;
		const gchar *val = NULL;
		data = g_variant_get_child_value(untuple, i);
		g_variant_get(data, "{&s&s}", &key, &val);
		g_hash_table_insert(hash, g_strdup(key), g_strdup(val));
	}
	return hash;
}

static void
fwupd_client_get_report_metadata_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_pointer(task,
			      fwupd_client_report_metadata_hash_from_variant(val),
			      (GDestroyNotify)g_hash_table_unref);
}

/**
 * fwupd_client_get_report_metadata_async:
 * @self: a #FwupdClient
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets all the report metadata from the daemon.
 *
 * You must have called [method@Client.connect_async] on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_report_metadata_async(FwupdClient *self,
				       GCancellable *cancellable,
				       GAsyncReadyCallback callback,
				       gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "GetReportMetadata",
			  NULL,
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_get_report_metadata_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_get_report_metadata_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.get_report_metadata_async].
 *
 * Returns: (transfer container): attributes
 *
 * Since: 1.5.0
 **/
GHashTable *
fwupd_client_get_report_metadata_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(g_task_is_valid(res, self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

static void
fwupd_client_get_devices_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	array = fwupd_codec_array_from_variant(val, FWUPD_TYPE_DEVICE, &error);
	if (array == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	fwupd_device_array_ensure_parents(array);

	/* success */
	g_task_return_pointer(task, g_steal_pointer(&array), (GDestroyNotify)g_ptr_array_unref);
}

/**
 * fwupd_client_get_devices_async:
 * @self: a #FwupdClient
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets all the devices registered with the daemon.
 *
 * You must have called [method@Client.connect_async] on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_devices_async(FwupdClient *self,
			       GCancellable *cancellable,
			       GAsyncReadyCallback callback,
			       gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "GetDevices",
			  NULL,
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_get_devices_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_get_devices_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.get_devices_async].
 *
 * Returns: (element-type FwupdDevice) (transfer container): results
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_devices_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(g_task_is_valid(res, self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

static void
fwupd_client_get_plugins_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	array = fwupd_codec_array_from_variant(val, FWUPD_TYPE_PLUGIN, &error);
	if (array == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_pointer(task, g_steal_pointer(&array), (GDestroyNotify)g_ptr_array_unref);
}

/**
 * fwupd_client_get_plugins_async:
 * @self: a #FwupdClient
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets all the plugins being used by the daemon.
 *
 * You must have called [method@Client.connect_async] on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_plugins_async(FwupdClient *self,
			       GCancellable *cancellable,
			       GAsyncReadyCallback callback,
			       gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "GetPlugins",
			  NULL,
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_get_plugins_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_get_plugins_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.get_plugins_async].
 *
 * Returns: (element-type FwupdDevice) (transfer container): results
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_plugins_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(g_task_is_valid(res, self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

static void
fwupd_client_get_history_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	array = fwupd_codec_array_from_variant(val, FWUPD_TYPE_DEVICE, &error);
	if (array == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	fwupd_device_array_ensure_parents(array);

	/* success */
	g_task_return_pointer(task, g_steal_pointer(&array), (GDestroyNotify)g_ptr_array_unref);
}

/**
 * fwupd_client_get_history_async:
 * @self: a #FwupdClient
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets all the history.
 *
 * You must have called [method@Client.connect_async] on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_history_async(FwupdClient *self,
			       GCancellable *cancellable,
			       GAsyncReadyCallback callback,
			       gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "GetHistory",
			  NULL,
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_get_history_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_get_history_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.get_history_async].
 *
 * Returns: (element-type FwupdDevice) (transfer container): results
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_history_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(g_task_is_valid(res, self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

static void
fwupd_client_get_device_by_id_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdDevice *device_result = NULL;
	gsize device_id_len;
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	const gchar *device_id = g_task_get_task_data(task);

	devices = fwupd_client_get_devices_finish(FWUPD_CLIENT(source), res, &error);
	if (devices == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* support abbreviated hashes (client side) */
	device_id_len = strlen(device_id);
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devices, i);
		if (strncmp(fwupd_device_get_id(dev), device_id, device_id_len) == 0) {
			if (device_result != NULL) {
				g_task_return_new_error(task,
							FWUPD_ERROR,
							FWUPD_ERROR_NOT_FOUND,
							"more than one matching ID prefix '%s'",
							device_id);
				return;
			}
			device_result = dev;
		}
	}

	/* one result */
	if (device_result != NULL) {
		g_task_return_pointer(task,
				      g_object_ref(device_result),
				      (GDestroyNotify)g_object_unref);
		return;
	}

	/* failed */
	g_task_return_new_error(task,
				FWUPD_ERROR,
				FWUPD_ERROR_NOT_FOUND,
				"failed to find %s",
				device_id);
}

/**
 * fwupd_client_get_device_by_id_async:
 * @self: a #FwupdClient
 * @device_id: (not nullable): the device ID
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets a device by it's device ID.
 *
 * You must have called [method@Client.connect_async] on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_device_by_id_async(FwupdClient *self,
				    const gchar *device_id,
				    GCancellable *cancellable,
				    GAsyncReadyCallback callback,
				    gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(device_id != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_task_set_task_data(task, g_strdup(device_id), g_free);
	fwupd_client_get_devices_async(self,
				       cancellable,
				       fwupd_client_get_device_by_id_cb,
				       g_steal_pointer(&task));
}

/**
 * fwupd_client_get_device_by_id_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.get_device_by_id_async].
 *
 * Returns: (transfer full): a device, or %NULL for failure
 *
 * Since: 1.5.0
 **/
FwupdDevice *
fwupd_client_get_device_by_id_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(g_task_is_valid(res, self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

static void
fwupd_client_get_devices_by_guid_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) devices_tmp = NULL;
	const gchar *guid = g_task_get_task_data(task);

	/* get all the devices */
	devices_tmp = fwupd_client_get_devices_finish(FWUPD_CLIENT(source), res, &error);
	if (devices_tmp == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* find the devices by GUID (client side) */
	devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (guint i = 0; i < devices_tmp->len; i++) {
		FwupdDevice *dev_tmp = g_ptr_array_index(devices_tmp, i);
		if (fwupd_device_has_guid(dev_tmp, guid))
			g_ptr_array_add(devices, g_object_ref(dev_tmp));
	}

	/* nothing */
	if (devices->len == 0) {
		g_task_return_new_error(task,
					FWUPD_ERROR,
					FWUPD_ERROR_NOT_FOUND,
					"failed to find any device providing %s",
					guid);
		return;
	}

	/* success */
	g_task_return_pointer(task, g_steal_pointer(&devices), (GDestroyNotify)g_ptr_array_unref);
}

/**
 * fwupd_client_get_devices_by_guid_async:
 * @self: a #FwupdClient
 * @guid: the GUID, e.g. `e22c4520-43dc-5bb3-8245-5787fead9b63`
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets any devices that provide a specific GUID. An error is returned if no
 * devices contains this GUID.
 *
 * You must have called [method@Client.connect_async] on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_devices_by_guid_async(FwupdClient *self,
				       const gchar *guid,
				       GCancellable *cancellable,
				       GAsyncReadyCallback callback,
				       gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(guid != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_task_set_task_data(task, g_strdup(guid), g_free);
	fwupd_client_get_devices_async(self,
				       cancellable,
				       fwupd_client_get_devices_by_guid_cb,
				       g_steal_pointer(&task));
}

/**
 * fwupd_client_get_devices_by_guid_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.get_devices_by_guid_async].
 *
 * Returns: (element-type FwupdRelease) (transfer container): results
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_devices_by_guid_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(g_task_is_valid(res, self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

static void
fwupd_client_get_releases_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	array = fwupd_codec_array_from_variant(val, FWUPD_TYPE_RELEASE, &error);
	if (array == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_pointer(task, g_steal_pointer(&array), (GDestroyNotify)g_ptr_array_unref);
}

/**
 * fwupd_client_get_releases_async:
 * @self: a #FwupdClient
 * @device_id: (not nullable): the device ID
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets all the releases for a specific device
 *
 * You must have called [method@Client.connect_async] on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_releases_async(FwupdClient *self,
				const gchar *device_id,
				GCancellable *cancellable,
				GAsyncReadyCallback callback,
				gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(device_id != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "GetReleases",
			  g_variant_new("(s)", device_id),
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_get_releases_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_get_releases_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.get_releases_async].
 *
 * Returns: (element-type FwupdRelease) (transfer container): results
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_releases_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(g_task_is_valid(res, self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

static void
fwupd_client_get_downgrades_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	array = fwupd_codec_array_from_variant(val, FWUPD_TYPE_RELEASE, &error);
	if (array == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_pointer(task, g_steal_pointer(&array), (GDestroyNotify)g_ptr_array_unref);
}

/**
 * fwupd_client_get_downgrades_async:
 * @self: a #FwupdClient
 * @device_id: (not nullable): the device ID
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets all the downgrades for a specific device.
 *
 * You must have called [method@Client.connect_async] on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_downgrades_async(FwupdClient *self,
				  const gchar *device_id,
				  GCancellable *cancellable,
				  GAsyncReadyCallback callback,
				  gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(device_id != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "GetDowngrades",
			  g_variant_new("(s)", device_id),
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_get_downgrades_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_get_downgrades_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.get_downgrades_async].
 *
 * Returns: (element-type FwupdRelease) (transfer container): results
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_downgrades_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(g_task_is_valid(res, self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

static void
fwupd_client_get_upgrades_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	array = fwupd_codec_array_from_variant(val, FWUPD_TYPE_RELEASE, &error);
	if (array == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_pointer(task, g_steal_pointer(&array), (GDestroyNotify)g_ptr_array_unref);
}

/**
 * fwupd_client_get_upgrades_async:
 * @self: a #FwupdClient
 * @device_id: (not nullable): the device ID
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets all the upgrades for a specific device.
 *
 * You must have called [method@Client.connect_async] on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_upgrades_async(FwupdClient *self,
				const gchar *device_id,
				GCancellable *cancellable,
				GAsyncReadyCallback callback,
				gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(device_id != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "GetUpgrades",
			  g_variant_new("(s)", device_id),
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_get_upgrades_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_get_upgrades_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.get_upgrades_async].
 *
 * Returns: (element-type FwupdRelease) (transfer container): results
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_upgrades_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(g_task_is_valid(res, self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

static void
fwupd_client_modify_config_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}

/**
 * fwupd_client_modify_config_async:
 * @self: a #FwupdClient
 * @section: config section, e.g. `redfish`
 * @key: config key, e.g. `DisabledPlugins`
 * @value: config value, e.g. `*`
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Modifies a daemon config option.
 * The daemon will only respond to this request with proper permissions.
 *
 * Since: 2.0.0
 **/
void
fwupd_client_modify_config_async(FwupdClient *self,
				 const gchar *section,
				 const gchar *key,
				 const gchar *value,
				 GCancellable *cancellable,
				 GAsyncReadyCallback callback,
				 gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(section != NULL);
	g_return_if_fail(key != NULL);
	g_return_if_fail(value != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "ModifyConfig",
			  g_variant_new("(sss)", section, key, value),
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_modify_config_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_modify_config_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.modify_config_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.0
 **/
gboolean
fwupd_client_modify_config_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

static void
fwupd_client_reset_config_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}

/**
 * fwupd_client_reset_config_async:
 * @self: a #FwupdClient
 * @section: config section, e.g. `redfish`
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Resets a daemon config section.
 * The daemon will only respond to this request with proper permissions.
 *
 * Since: 1.9.15
 **/
void
fwupd_client_reset_config_async(FwupdClient *self,
				const gchar *section,
				GCancellable *cancellable,
				GAsyncReadyCallback callback,
				gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(section != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "ResetConfig",
			  g_variant_new("(s)", section),
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_reset_config_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_reset_config_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.reset_config_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 1.9.15
 **/
gboolean
fwupd_client_reset_config_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

static void
fwupd_client_activate_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}

/**
 * fwupd_client_activate_async:
 * @self: a #FwupdClient
 * @device_id: a device
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Activates up a device, which normally means the device switches to a new
 * firmware version. This should only be called when data loss cannot occur.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_activate_async(FwupdClient *self,
			    const gchar *device_id,
			    GCancellable *cancellable,
			    GAsyncReadyCallback callback,
			    gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(device_id != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "Activate",
			  g_variant_new("(s)", device_id),
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_activate_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_activate_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.activate_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_activate_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

static void
fwupd_client_verify_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}

/**
 * fwupd_client_verify_async:
 * @self: a #FwupdClient
 * @device_id: (not nullable): the device ID
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Verify a specific device.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_verify_async(FwupdClient *self,
			  const gchar *device_id,
			  GCancellable *cancellable,
			  GAsyncReadyCallback callback,
			  gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(device_id != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "Verify",
			  g_variant_new("(s)", device_id),
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_verify_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_verify_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.verify_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_verify_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

static void
fwupd_client_verify_update_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}

/**
 * fwupd_client_verify_update_async:
 * @self: a #FwupdClient
 * @device_id: (not nullable): the device ID
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Update the verification record for a specific device.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_verify_update_async(FwupdClient *self,
				 const gchar *device_id,
				 GCancellable *cancellable,
				 GAsyncReadyCallback callback,
				 gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(device_id != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "VerifyUpdate",
			  g_variant_new("(s)", device_id),
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_verify_update_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_verify_update_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.verify_update_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_verify_update_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

static void
fwupd_client_unlock_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}

/**
 * fwupd_client_unlock_async:
 * @self: a #FwupdClient
 * @device_id: (not nullable): the device ID
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Unlocks a specific device so firmware can be read or wrote.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_unlock_async(FwupdClient *self,
			  const gchar *device_id,
			  GCancellable *cancellable,
			  GAsyncReadyCallback callback,
			  gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(device_id != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "Unlock",
			  g_variant_new("(s)", device_id),
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_unlock_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_unlock_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.unlock_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_unlock_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

static void
fwupd_client_clear_results_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}

/**
 * fwupd_client_clear_results_async:
 * @self: a #FwupdClient
 * @device_id: a device
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Clears the results for a specific device.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_clear_results_async(FwupdClient *self,
				 const gchar *device_id,
				 GCancellable *cancellable,
				 GAsyncReadyCallback callback,
				 gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(device_id != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "ClearResults",
			  g_variant_new("(s)", device_id),
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_clear_results_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_clear_results_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.clear_results_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_clear_results_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

static void
fwupd_client_get_results_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(FwupdDevice) device = fwupd_device_new();
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	if (!fwupd_codec_from_variant(FWUPD_CODEC(device), val, &error)) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_pointer(task, g_steal_pointer(&device), (GDestroyNotify)g_ptr_array_unref);
}

/**
 * fwupd_client_get_results_async:
 * @self: a #FwupdClient
 * @device_id: (not nullable): the device ID
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets the results of a previous firmware update for a specific device.
 *
 * You must have called [method@Client.connect_async] on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_results_async(FwupdClient *self,
			       const gchar *device_id,
			       GCancellable *cancellable,
			       GAsyncReadyCallback callback,
			       gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(device_id != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "GetResults",
			  g_variant_new("(s)", device_id),
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_get_results_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_get_results_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.get_results_async].
 *
 * Returns: (transfer full): a device, or %NULL for failure
 *
 * Since: 1.5.0
 **/
FwupdDevice *
fwupd_client_get_results_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(g_task_is_valid(res, self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

#ifdef HAVE_GIO_UNIX

static void
fwupd_client_install_stream_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GDBusMessage) msg = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = G_TASK(user_data);

	msg = g_dbus_connection_send_message_with_reply_finish(G_DBUS_CONNECTION(source),
							       res,
							       &error);
	if (msg == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	if (g_dbus_message_to_gerror(msg, &error)) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}

void
fwupd_client_install_stream_async(FwupdClient *self,
				  const gchar *device_id,
				  GUnixInputStream *istr,
				  const gchar *filename_hint,
				  FwupdInstallFlags install_flags,
				  GCancellable *cancellable,
				  GAsyncReadyCallback callback,
				  gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	GVariantBuilder builder;
	g_autoptr(GDBusMessage) request = NULL;
	g_autoptr(GUnixFDList) fd_list = NULL;
	g_autoptr(GTask) task = g_task_new(self, cancellable, callback, callback_data);

	/* set options */
	g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add(&builder, "{sv}", "reason", g_variant_new_string("user-action"));
	if (filename_hint != NULL) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      "filename",
				      g_variant_new_string(filename_hint));
	}
	g_variant_builder_add(&builder,
			      "{sv}",
			      "install-flags",
			      g_variant_new_uint64(install_flags));

	/* set out of band file descriptor */
	fd_list = g_unix_fd_list_new();
	g_unix_fd_list_append(fd_list, g_unix_input_stream_get_fd(istr), NULL);
	request = g_dbus_message_new_method_call(FWUPD_DBUS_SERVICE,
						 FWUPD_DBUS_PATH,
						 FWUPD_DBUS_INTERFACE,
						 "Install");
	g_dbus_message_set_unix_fd_list(request, fd_list);

	/* call into daemon */
	g_dbus_message_set_body(
	    request,
	    g_variant_new("(sha{sv})", device_id, g_unix_input_stream_get_fd(istr), &builder));
	g_dbus_connection_send_message_with_reply(g_dbus_proxy_get_connection(priv->proxy),
						  request,
						  G_DBUS_SEND_MESSAGE_FLAGS_NONE,
						  G_MAXINT,
						  NULL,
						  cancellable,
						  fwupd_client_install_stream_cb,
						  g_steal_pointer(&task));
}
#endif

/**
 * fwupd_client_install_bytes_async:
 * @self: a #FwupdClient
 * @device_id: (not nullable): the device ID
 * @bytes: cabinet archive
 * @install_flags: install flags, e.g. %FWUPD_INSTALL_FLAG_ALLOW_REINSTALL
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Install firmware onto a specific device.
 *
 * NOTE: This method is thread-safe, but progress signals will be
 * emitted in the global default main context, if not explicitly set with
 * [method@Client.set_main_context].
 *
 * Since: 1.5.0
 **/
void
fwupd_client_install_bytes_async(FwupdClient *self,
				 const gchar *device_id,
				 GBytes *bytes,
				 FwupdInstallFlags install_flags,
				 GCancellable *cancellable,
				 GAsyncReadyCallback callback,
				 gpointer callback_data)
{
#ifdef HAVE_GIO_UNIX
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GError) error = NULL;
	g_autoptr(GUnixInputStream) istr = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* move to a thread if this ever takes more than a few ms */
	istr = fwupd_unix_input_stream_from_bytes(bytes, &error);
	if (istr == NULL) {
		g_autoptr(GTask) task = g_task_new(self, cancellable, callback, callback_data);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* call into daemon */
	fwupd_client_install_stream_async(self,
					  device_id,
					  istr,
					  NULL,
					  install_flags,
					  cancellable,
					  callback,
					  callback_data);
#else
	g_autoptr(GTask) task = g_task_new(self, cancellable, callback, callback_data);
	g_task_return_new_error(task,
				FWUPD_ERROR,
				FWUPD_ERROR_NOT_SUPPORTED,
				"Install CAB only supported on Linux");
#endif
}

/**
 * fwupd_client_install_bytes_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.install_bytes_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_install_bytes_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

/**
 * fwupd_client_install_async:
 * @self: a #FwupdClient
 * @device_id: (not nullable): the device ID
 * @filename: the filename to install
 * @install_flags: install flags, e.g. %FWUPD_INSTALL_FLAG_ALLOW_REINSTALL
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Install firmware onto a specific device.
 *
 * NOTE: This method is thread-safe, but progress signals will be
 * emitted in the global default main context, if not explicitly set with
 * [method@Client.set_main_context].
 *
 * Since: 1.5.0
 **/
void
fwupd_client_install_async(FwupdClient *self,
			   const gchar *device_id,
			   const gchar *filename,
			   FwupdInstallFlags install_flags,
			   GCancellable *cancellable,
			   GAsyncReadyCallback callback,
			   gpointer callback_data)
{
#ifdef HAVE_GIO_UNIX
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GError) error = NULL;
	g_autoptr(GUnixInputStream) istr = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(device_id != NULL);
	g_return_if_fail(filename != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* move to a thread if this ever takes more than a few ms */
	istr = fwupd_unix_input_stream_from_fn(filename, &error);
	if (istr == NULL) {
		g_autoptr(GTask) task = g_task_new(self, cancellable, callback, callback_data);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* call into daemon */
	fwupd_client_install_stream_async(self,
					  device_id,
					  istr,
					  NULL,
					  install_flags,
					  cancellable,
					  callback,
					  callback_data);
#else
	g_autoptr(GTask) task = g_task_new(self, cancellable, callback, callback_data);
	g_task_return_new_error(task,
				FWUPD_ERROR,
				FWUPD_ERROR_NOT_SUPPORTED,
				"Install CAB async only supported on Linux");
#endif
}

/**
 * fwupd_client_install_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.install_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_install_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

typedef struct {
	FwupdDevice *device;
	FwupdRelease *release;
	FwupdInstallFlags install_flags;
	FwupdClientDownloadFlags download_flags;
} FwupdClientInstallReleaseData;

static void
fwupd_client_install_release_data_free(FwupdClientInstallReleaseData *data)
{
	g_object_unref(data->device);
	g_object_unref(data->release);
	g_free(data);
}

static void
fwupd_client_install_release_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = G_TASK(user_data);

	if (!fwupd_client_install_release_finish(FWUPD_CLIENT(source), res, &error)) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}

static void
fwupd_client_install_release_bytes_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = G_TASK(user_data);

	if (!fwupd_client_install_bytes_finish(FWUPD_CLIENT(source), res, &error)) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}

static void
fwupd_client_install_release_download_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = G_TASK(user_data);
	FwupdClientInstallReleaseData *data = g_task_get_task_data(task);
	GChecksumType checksum_type;
	GCancellable *cancellable = g_task_get_cancellable(task);
	const gchar *checksum_expected;
	g_autofree gchar *checksum_actual = NULL;

	blob = fwupd_client_download_bytes_finish(FWUPD_CLIENT(source), res, &error);
	if (blob == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* verify checksum */
	checksum_expected = fwupd_checksum_get_best(fwupd_release_get_checksums(data->release));
	checksum_type = fwupd_checksum_guess_kind(checksum_expected);
	checksum_actual = g_compute_checksum_for_bytes(checksum_type, blob);
	if (g_strcmp0(checksum_expected, checksum_actual) != 0) {
		g_task_return_new_error(task,
					FWUPD_ERROR,
					FWUPD_ERROR_INVALID_FILE,
					"checksum invalid, expected %s got %s",
					checksum_expected,
					checksum_actual);
		return;
	}

	fwupd_client_install_bytes_async(FWUPD_CLIENT(source),
					 fwupd_device_get_id(data->device),
					 blob,
					 data->install_flags,
					 cancellable,
					 fwupd_client_install_release_bytes_cb,
					 g_steal_pointer(&task));
}

static gboolean
fwupd_client_is_url_http(const gchar *perhaps_url)
{
#ifdef HAVE_LIBCURL
	g_autoptr(CURLU) h = curl_url();
	return curl_url_set(h, CURLUPART_URL, perhaps_url, 0) == CURLUE_OK;
#else
	return g_str_has_prefix(perhaps_url, "http://") ||
	       g_str_has_prefix(perhaps_url, "https://");
#endif
}

static gboolean
fwupd_client_is_url_ipfs(const gchar *perhaps_url)
{
	if (perhaps_url == NULL)
		return FALSE;
	return g_str_has_prefix(perhaps_url, "ipfs://") || g_str_has_prefix(perhaps_url, "ipns://");
}

static gboolean
fwupd_client_is_localhost(const gchar *url)
{
#ifdef HAVE_LIBCURL
	g_autoptr(CURLU) h = curl_url();
	g_autoptr(CURLSTR) hostname = NULL;
	if (curl_url_set(h, CURLUPART_URL, url, 0) != CURLUE_OK)
		return FALSE;
	(void)curl_url_get(h, CURLUPART_HOST, &hostname, 0);
	return g_strcmp0(hostname, "localhost") == 0;
#else
	if (g_str_has_prefix(url, "https://localhost/") ||
	    g_str_has_prefix(url, "https://localhost:"))
		return TRUE;
	return FALSE;
#endif
}

static gboolean
fwupd_client_is_url_p2p(const gchar *perhaps_url)
{
	if (perhaps_url == NULL)
		return FALSE;
	if (fwupd_client_is_url_ipfs(perhaps_url))
		return TRUE;
	if (fwupd_client_is_localhost(perhaps_url))
		return TRUE;
	return FALSE;
}

static void
fwupd_client_install_release_remote_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	GPtrArray *locations;
	const gchar *uri_tmp;
	g_autofree gchar *fn = NULL;
	g_autoptr(FwupdRemote) remote = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GPtrArray) uris_built = g_ptr_array_new_with_free_func(g_free);
	FwupdClientInstallReleaseData *data = g_task_get_task_data(task);
	GCancellable *cancellable = g_task_get_cancellable(task);

	/* if a remote-id was specified, the remote has to exist */
	remote = fwupd_client_get_remote_by_id_finish(FWUPD_CLIENT(source), res, &error);
	if (remote == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* get the default release only until other parts of fwupd can cope */
	locations = fwupd_release_get_locations(data->release);
	if (locations->len == 0) {
		g_task_return_new_error(task,
					FWUPD_ERROR,
					FWUPD_ERROR_INVALID_FILE,
					"release missing URI");
		return;
	}
	uri_tmp = g_ptr_array_index(locations, 0);

	/* local and directory remotes may have the firmware already */
	if (fwupd_remote_get_kind(remote) == FWUPD_REMOTE_KIND_LOCAL &&
	    !fwupd_client_is_url_http(uri_tmp)) {
		const gchar *fn_cache = fwupd_remote_get_filename_cache(remote);
		g_autofree gchar *path = g_path_get_dirname(fn_cache);
		fn = g_build_filename(path, uri_tmp, NULL);
	} else if (fwupd_remote_get_kind(remote) == FWUPD_REMOTE_KIND_DIRECTORY) {
		fn = g_strdup(uri_tmp + 7);
	}

	/* install with flags chosen by the user */
	if (fn != NULL) {
		fwupd_client_install_async(FWUPD_CLIENT(source),
					   fwupd_device_get_id(data->device),
					   fn,
					   data->install_flags,
					   cancellable,
					   fwupd_client_install_release_cb,
					   g_steal_pointer(&task));
		return;
	}

	/* maybe get payload from Passim */
	if (fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_ALLOW_P2P_FIRMWARE)) {
		const gchar *checksum_sha256 =
		    fwupd_checksum_get_by_kind(fwupd_release_get_checksums(data->release),
					       G_CHECKSUM_SHA256);
		if (checksum_sha256 != NULL) {
			g_autofree gchar *basename =
			    g_path_get_basename(fwupd_release_get_filename(data->release));
			g_ptr_array_add(uris_built,
					g_strdup_printf("https://localhost:27500/%s?sha256=%s",
							basename,
							checksum_sha256));
		}
	}

	/* remote file */
	for (guint i = 0; i < locations->len; i++) {
		uri_tmp = g_ptr_array_index(locations, i);
		if (fwupd_client_is_url_p2p(uri_tmp)) {
			g_ptr_array_add(uris_built, g_strdup(uri_tmp));
		} else if (fwupd_client_is_url_http(uri_tmp)) {
			g_autofree gchar *uri_str = NULL;
			uri_str = fwupd_remote_build_firmware_uri(remote, uri_tmp, &error);
			if (uri_str == NULL) {
				g_task_return_error(task, g_steal_pointer(&error));
				return;
			}
			g_ptr_array_add(uris_built, g_steal_pointer(&uri_str));
		} else {
			g_debug("do not how to handle URI %s", uri_tmp);
		}
	}
	if (uris_built->len == 0) {
		g_task_return_new_error(task,
					FWUPD_ERROR,
					FWUPD_ERROR_INVALID_FILE,
					"No URIs to download");
		return;
	}

	/* download file */
	fwupd_client_download_bytes2_async(FWUPD_CLIENT(source),
					   uris_built,
					   data->download_flags,
					   cancellable,
					   fwupd_client_install_release_download_cb,
					   g_steal_pointer(&task));
}

#ifdef HAVE_LIBCURL
static GPtrArray *
fwupd_client_filter_locations(GPtrArray *locations,
			      FwupdClientDownloadFlags download_flags,
			      GError **error)
{
	g_autoptr(GPtrArray) uris_filtered = g_ptr_array_new_with_free_func(g_free);

	g_return_val_if_fail(locations != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	for (guint i = 0; i < locations->len; i++) {
		const gchar *uri = g_ptr_array_index(locations, i);
		if ((download_flags & FWUPD_CLIENT_DOWNLOAD_FLAG_ONLY_P2P) > 0 &&
		    !fwupd_client_is_url_p2p(uri))
			continue;
		g_ptr_array_add(uris_filtered, g_strdup(uri));
	}
	if (uris_filtered->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no valid release URIs");
		return NULL;
	}
	return g_steal_pointer(&uris_filtered);
}
#endif

/**
 * fwupd_client_install_release_async:
 * @self: a #FwupdClient
 * @device: (not nullable): a device
 * @release: (not nullable): a release
 * @install_flags: install flags, e.g. %FWUPD_INSTALL_FLAG_ALLOW_REINSTALL
 * @download_flags: download flags, e.g. %FWUPD_CLIENT_DOWNLOAD_FLAG_ONLY_P2P
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Installs a new release on a device, downloading the firmware if required.
 *
 * NOTE: This method is thread-safe, but progress signals will be
 * emitted in the global default main context, if not explicitly set with
 * [method@Client.set_main_context].
 *
 * Since: 2.0.0
 **/
void
fwupd_client_install_release_async(FwupdClient *self,
				   FwupdDevice *device,
				   FwupdRelease *release,
				   FwupdInstallFlags install_flags,
				   FwupdClientDownloadFlags download_flags,
				   GCancellable *cancellable,
				   GAsyncReadyCallback callback,
				   gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;
	FwupdClientInstallReleaseData *data;
	const gchar *remote_id;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(FWUPD_IS_DEVICE(device));
	g_return_if_fail(FWUPD_IS_RELEASE(release));
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	data = g_new0(FwupdClientInstallReleaseData, 1);
	data->device = g_object_ref(device);
	data->release = g_object_ref(release);
	data->download_flags = download_flags;
	data->install_flags = install_flags;
	g_task_set_task_data(task, data, (GDestroyNotify)fwupd_client_install_release_data_free);

	/* work out what remote-specific URI fields this should use */
	remote_id = fwupd_release_get_remote_id(release);
	if (remote_id == NULL) {
		fwupd_client_download_bytes2_async(self,
						   fwupd_release_get_locations(release),
						   download_flags,
						   cancellable,
						   fwupd_client_install_release_download_cb,
						   g_steal_pointer(&task));
		return;
	}

	/* if a remote-id was specified, the remote has to exist */
	fwupd_client_get_remote_by_id_async(self,
					    remote_id,
					    cancellable,
					    fwupd_client_install_release_remote_cb,
					    g_steal_pointer(&task));
}

/**
 * fwupd_client_install_release_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.install_release_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_install_release_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

#ifdef HAVE_GIO_UNIX

static void
fwupd_client_get_details_stream_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GDBusMessage) msg = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GTask) task = G_TASK(user_data);

	msg = g_dbus_connection_send_message_with_reply_finish(G_DBUS_CONNECTION(source),
							       res,
							       &error);
	if (msg == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	if (g_dbus_message_to_gerror(msg, &error)) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	array =
	    fwupd_codec_array_from_variant(g_dbus_message_get_body(msg), FWUPD_TYPE_DEVICE, &error);
	if (array == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_pointer(task, g_steal_pointer(&array), (GDestroyNotify)g_ptr_array_unref);
}

void
fwupd_client_get_details_stream_async(FwupdClient *self,
				      GUnixInputStream *istr,
				      GCancellable *cancellable,
				      GAsyncReadyCallback callback,
				      gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	gint fd = g_unix_input_stream_get_fd(istr);
	g_autoptr(GDBusMessage) request = NULL;
	g_autoptr(GUnixFDList) fd_list = NULL;
	g_autoptr(GTask) task = g_task_new(self, cancellable, callback, callback_data);

	/* set out of band file descriptor */
	fd_list = g_unix_fd_list_new();
	g_unix_fd_list_append(fd_list, fd, NULL);
	request = g_dbus_message_new_method_call(FWUPD_DBUS_SERVICE,
						 FWUPD_DBUS_PATH,
						 FWUPD_DBUS_INTERFACE,
						 "GetDetails");
	g_dbus_message_set_unix_fd_list(request, fd_list);

	/* call into daemon */
	g_dbus_message_set_body(request, g_variant_new("(h)", fd));
	g_dbus_connection_send_message_with_reply(g_dbus_proxy_get_connection(priv->proxy),
						  request,
						  G_DBUS_SEND_MESSAGE_FLAGS_NONE,
						  G_MAXINT,
						  NULL,
						  cancellable,
						  fwupd_client_get_details_stream_cb,
						  g_steal_pointer(&task));
}
#endif

/**
 * fwupd_client_get_details_bytes_async:
 * @self: a #FwupdClient
 * @bytes: firmware archive
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets details about a specific firmware file.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_details_bytes_async(FwupdClient *self,
				     GBytes *bytes,
				     GCancellable *cancellable,
				     GAsyncReadyCallback callback,
				     gpointer callback_data)
{
#ifdef HAVE_GIO_UNIX
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GError) error = NULL;
	g_autoptr(GUnixInputStream) istr = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* move to a thread if this ever takes more than a few ms */
	istr = fwupd_unix_input_stream_from_bytes(bytes, &error);
	if (istr == NULL) {
		g_autoptr(GTask) task = g_task_new(self, cancellable, callback, callback_data);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* call into daemon */
	fwupd_client_get_details_stream_async(self, istr, cancellable, callback, callback_data);
#else
	g_autoptr(GTask) task = g_task_new(self, cancellable, callback, callback_data);
	g_task_return_new_error(task,
				FWUPD_ERROR,
				FWUPD_ERROR_NOT_SUPPORTED,
				"Get Details only supported on Linux");
#endif
}

/**
 * fwupd_client_get_details_bytes_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.get_details_bytes_async].
 *
 * Returns: (transfer container) (element-type FwupdDevice): an array of results
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_details_bytes_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(g_task_is_valid(res, self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

/**
 * fwupd_client_get_percentage:
 * @self: a #FwupdClient
 *
 * Gets the last returned percentage value.
 *
 * Returns: a percentage, or 0 for unknown.
 *
 * Since: 0.7.3
 **/
guint
fwupd_client_get_percentage(FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), 0);
	return priv->percentage;
}

/**
 * fwupd_client_get_daemon_version:
 * @self: a #FwupdClient
 *
 * Gets the daemon version number.
 *
 * Returns: a string, or %NULL for unknown.
 *
 * Since: 0.9.6
 **/
const gchar *
fwupd_client_get_daemon_version(FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	return priv->daemon_version;
}

/**
 * fwupd_client_get_host_bkc:
 * @self: a #FwupdClient
 *
 * Gets the host best known configuration, e.g. `vendor-factory-2021q1,mycompany-2023`.
 *
 * Returns: a string, or %NULL for unknown.
 *
 * Since: 1.7.3
 **/
const gchar *
fwupd_client_get_host_bkc(FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	return priv->host_bkc;
}

/**
 * fwupd_client_get_host_product:
 * @self: a #FwupdClient
 *
 * Gets the string that represents the host running fwupd
 *
 * Returns: a string, or %NULL for unknown.
 *
 * Since: 1.3.1
 **/
const gchar *
fwupd_client_get_host_product(FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	return priv->host_product;
}

/**
 * fwupd_client_get_host_vendor:
 * @self: a #FwupdClient
 *
 * Gets the string that represents the vendor of the host running fwupd
 *
 * Returns: a string, or %NULL for unknown.
 *
 * Since: 1.8.2
 **/
const gchar *
fwupd_client_get_host_vendor(FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	return priv->host_vendor;
}

/**
 * fwupd_client_get_host_machine_id:
 * @self: a #FwupdClient
 *
 * Gets the string that represents the host machine ID
 *
 * Returns: a string, or %NULL for unknown.
 *
 * Since: 1.3.2
 **/
const gchar *
fwupd_client_get_host_machine_id(FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	return priv->host_machine_id;
}

/**
 * fwupd_client_get_host_security_id:
 * @self: a #FwupdClient
 *
 * Gets the string that represents the host machine ID
 *
 * Returns: a string, or %NULL for unknown.
 *
 * Since: 1.5.0
 **/
const gchar *
fwupd_client_get_host_security_id(FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	return priv->host_security_id;
}

/**
 * fwupd_client_get_battery_level:
 * @self: a #FwupdClient
 *
 * Returns the system battery level.
 *
 * Returns: value in percent
 *
 * Since: 1.8.1
 **/
guint32
fwupd_client_get_battery_level(FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FWUPD_BATTERY_LEVEL_INVALID);
	return priv->battery_level;
}

/**
 * fwupd_client_get_battery_threshold:
 * @self: a #FwupdClient
 *
 * Returns the system battery threshold under which a firmware update cannot be
 * performed.
 *
 * Returns: value in percent
 *
 * Since: 1.8.1
 **/
guint32
fwupd_client_get_battery_threshold(FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FWUPD_BATTERY_LEVEL_INVALID);
	return priv->battery_threshold;
}

/**
 * fwupd_client_get_status:
 * @self: a #FwupdClient
 *
 * Gets the last returned status value.
 *
 * Returns: a #FwupdStatus, or %FWUPD_STATUS_UNKNOWN for unknown.
 *
 * Since: 0.7.3
 **/
FwupdStatus
fwupd_client_get_status(FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FWUPD_STATUS_UNKNOWN);
	return priv->status;
}

/**
 * fwupd_client_get_tainted:
 * @self: a #FwupdClient
 *
 * Gets if the daemon has been tainted by 3rd party code.
 *
 * Returns: %TRUE if the daemon is unsupported
 *
 * Since: 1.2.4
 **/
gboolean
fwupd_client_get_tainted(FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	return priv->tainted;
}

/**
 * fwupd_client_get_only_trusted:
 * @self: a #FwupdClient
 *
 * Gets if the daemon is verifying signatures from a trusted authority.
 *
 * Returns: %TRUE if the daemon is checking signatures
 *
 * Since: 1.8.0
 **/
gboolean
fwupd_client_get_only_trusted(FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	return priv->only_trusted;
}

/**
 * fwupd_client_get_daemon_interactive:
 * @self: a #FwupdClient
 *
 * Gets if the daemon is running in an interactive terminal.
 *
 * Returns: %TRUE if the daemon is running in an interactive terminal
 *
 * Since: 1.3.4
 **/
gboolean
fwupd_client_get_daemon_interactive(FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	return priv->interactive;
}

#ifdef HAVE_GIO_UNIX

static void
fwupd_client_update_metadata_stream_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GDBusMessage) msg = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = G_TASK(user_data);

	msg = g_dbus_connection_send_message_with_reply_finish(G_DBUS_CONNECTION(source),
							       res,
							       &error);
	if (msg == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	if (g_dbus_message_to_gerror(msg, &error)) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}

void
fwupd_client_update_metadata_stream_async(FwupdClient *self,
					  const gchar *remote_id,
					  GUnixInputStream *istr,
					  GUnixInputStream *istr_sig,
					  GCancellable *cancellable,
					  GAsyncReadyCallback callback,
					  gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GDBusMessage) request = NULL;
	g_autoptr(GUnixFDList) fd_list = NULL;
	g_autoptr(GTask) task = g_task_new(self, cancellable, callback, callback_data);

	/* set out of band file descriptor */
	fd_list = g_unix_fd_list_new();
	g_unix_fd_list_append(fd_list, g_unix_input_stream_get_fd(istr), NULL);
	g_unix_fd_list_append(fd_list, g_unix_input_stream_get_fd(istr_sig), NULL);
	request = g_dbus_message_new_method_call(FWUPD_DBUS_SERVICE,
						 FWUPD_DBUS_PATH,
						 FWUPD_DBUS_INTERFACE,
						 "UpdateMetadata");
	g_dbus_message_set_unix_fd_list(request, fd_list);

	/* call into daemon */
	g_dbus_message_set_body(request,
				g_variant_new("(shh)",
					      remote_id,
					      g_unix_input_stream_get_fd(istr),
					      g_unix_input_stream_get_fd(istr_sig)));
	g_dbus_connection_send_message_with_reply(g_dbus_proxy_get_connection(priv->proxy),
						  request,
						  G_DBUS_SEND_MESSAGE_FLAGS_NONE,
						  G_MAXINT,
						  NULL,
						  cancellable,
						  fwupd_client_update_metadata_stream_cb,
						  g_steal_pointer(&task));
}
#endif

/**
 * fwupd_client_update_metadata_bytes_async:
 * @self: a #FwupdClient
 * @remote_id: remote ID, e.g. `lvfs-testing`
 * @metadata: XML metadata data
 * @signature: signature data
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Updates the metadata. This allows a session process to download the metadata
 * and metadata signing file to be passed into the daemon to be checked and
 * parsed.
 *
 * The @remote_id allows the firmware to be tagged so that the remote can be
 * matched when the firmware is downloaded.
 *
 * NOTE: This method is thread-safe, but progress signals will be
 * emitted in the global default main context, if not explicitly set with
 * [method@Client.set_main_context].
 *
 * Since: 1.5.0
 **/
void
fwupd_client_update_metadata_bytes_async(FwupdClient *self,
					 const gchar *remote_id,
					 GBytes *metadata,
					 GBytes *signature,
					 GCancellable *cancellable,
					 GAsyncReadyCallback callback,
					 gpointer callback_data)
{
#ifdef HAVE_GIO_UNIX
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GError) error = NULL;
	g_autoptr(GUnixInputStream) istr = NULL;
	g_autoptr(GUnixInputStream) istr_sig = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(remote_id != NULL);
	g_return_if_fail(metadata != NULL);
	g_return_if_fail(signature != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* move to a thread if this ever takes more than a few ms */
	istr = fwupd_unix_input_stream_from_bytes(metadata, &error);
	if (istr == NULL) {
		g_autoptr(GTask) task = g_task_new(self, cancellable, callback, callback_data);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	istr_sig = fwupd_unix_input_stream_from_bytes(signature, &error);
	if (istr_sig == NULL) {
		g_autoptr(GTask) task = g_task_new(self, cancellable, callback, callback_data);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* call into daemon */
	fwupd_client_update_metadata_stream_async(self,
						  remote_id,
						  istr,
						  istr_sig,
						  cancellable,
						  callback,
						  callback_data);
#else
	g_autoptr(GTask) task = g_task_new(self, cancellable, callback, callback_data);
	g_task_return_new_error(task,
				FWUPD_ERROR,
				FWUPD_ERROR_NOT_SUPPORTED,
				"Update metadata only supported on Linux");
#endif
}

/**
 * fwupd_client_update_metadata_bytes_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.update_metadata_bytes_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_update_metadata_bytes_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

typedef struct {
	FwupdRemote *remote;
	FwupdClientDownloadFlags download_flags;
	GBytes *signature;
	GBytes *metadata;
} FwupdClientRefreshRemoteData;

static void
fwupd_client_refresh_remote_data_free(FwupdClientRefreshRemoteData *data)
{
	if (data->signature != NULL)
		g_bytes_unref(data->signature);
	if (data->metadata != NULL)
		g_bytes_unref(data->metadata);
	g_object_unref(data->remote);
	g_free(data);
}

static void
fwupd_client_refresh_remote_update_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = G_TASK(user_data);

	/* save metadata */
	if (!fwupd_client_update_metadata_bytes_finish(FWUPD_CLIENT(source), res, &error)) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}

static void
fwupd_client_refresh_remote_metadata_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = G_TASK(user_data);
	FwupdClientRefreshRemoteData *data = g_task_get_task_data(task);
	FwupdClient *self = g_task_get_source_object(task);
	GCancellable *cancellable = g_task_get_cancellable(task);

	/* save metadata */
	bytes = fwupd_client_download_bytes_finish(FWUPD_CLIENT(source), res, &error);
	if (bytes == NULL) {
		g_prefix_error(&error,
			       "Failed to download metadata for %s: ",
			       fwupd_remote_get_id(data->remote));
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	data->metadata = g_steal_pointer(&bytes);

	/* verify this was what we expected */
	if (fwupd_remote_get_checksum_metadata(data->remote) != NULL) {
		GChecksumType checksum_kind =
		    fwupd_checksum_guess_kind(fwupd_remote_get_checksum_metadata(data->remote));
		g_autofree gchar *checksum =
		    g_compute_checksum_for_bytes(checksum_kind, data->metadata);
		if (g_strcmp0(checksum, fwupd_remote_get_checksum_metadata(data->remote)) != 0) {
			g_task_return_new_error(task,
						FWUPD_ERROR,
						FWUPD_ERROR_INVALID_FILE,
						"metadata checksum expected %s and got %s",
						fwupd_remote_get_checksum_metadata(data->remote),
						checksum);
			return;
		}
	}

	/* send all this to fwupd */
	fwupd_client_update_metadata_bytes_async(self,
						 fwupd_remote_get_id(data->remote),
						 data->metadata,
						 data->signature,
						 cancellable,
						 fwupd_client_refresh_remote_update_cb,
						 g_steal_pointer(&task));
}

static void
fwupd_client_refresh_remote_signature_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = G_TASK(user_data);
	FwupdClientRefreshRemoteData *data = g_task_get_task_data(task);
	FwupdClient *self = g_task_get_source_object(task);
	GCancellable *cancellable = g_task_get_cancellable(task);
	g_autoptr(GPtrArray) urls = g_ptr_array_new_with_free_func(g_free);

	/* save signature */
	bytes = fwupd_client_download_bytes_finish(FWUPD_CLIENT(source), res, &error);
	if (bytes == NULL) {
		g_prefix_error(&error,
			       "Failed to download metadata for %s: ",
			       fwupd_remote_get_id(data->remote));
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	data->signature = g_steal_pointer(&bytes);
	if (!fwupd_remote_load_signature_bytes(data->remote, data->signature, &error)) {
		g_prefix_error(&error, "Failed to load signature: ");
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* is the signature checksum the same? */
	if (fwupd_remote_get_checksum(data->remote) != NULL) {
		GChecksumType checksum_kind =
		    fwupd_checksum_guess_kind(fwupd_remote_get_checksum(data->remote));
		g_autofree gchar *checksum = g_compute_checksum_for_data(
		    checksum_kind,
		    (const guchar *)g_bytes_get_data(data->signature, NULL),
		    g_bytes_get_size(data->signature));
		if (g_strcmp0(checksum, fwupd_remote_get_checksum(data->remote)) == 0) {
			g_info("metadata signature of %s is unchanged, skipping",
			       fwupd_remote_get_id(data->remote));
			g_task_return_boolean(task, TRUE);
			return;
		}
	}

	/* maybe get metadata from Passim */
	if (fwupd_remote_has_flag(data->remote, FWUPD_REMOTE_FLAG_ALLOW_P2P_METADATA) &&
	    fwupd_remote_get_checksum_metadata(data->remote) != NULL &&
	    fwupd_remote_get_username(data->remote) == NULL &&
	    fwupd_remote_get_password(data->remote) == NULL) {
		g_autofree gchar *basename =
		    g_path_get_basename(fwupd_remote_get_metadata_uri(data->remote));
		g_ptr_array_add(urls,
				g_strdup_printf("https://localhost:27500/%s?sha256=%s",
						basename,
						fwupd_remote_get_checksum_metadata(data->remote)));
	}
	if ((data->download_flags & FWUPD_CLIENT_DOWNLOAD_FLAG_ONLY_P2P) == 0) {
		g_autofree gchar *uri = fwupd_remote_build_metadata_uri(data->remote, &error);
		if (uri == NULL) {
			g_task_return_error(task, g_steal_pointer(&error));
			return;
		}
		g_ptr_array_add(urls, g_steal_pointer(&uri));
	}
	fwupd_client_download_bytes2_async(self,
					   urls,
					   FWUPD_CLIENT_DOWNLOAD_FLAG_NONE,
					   cancellable,
					   fwupd_client_refresh_remote_metadata_cb,
					   g_steal_pointer(&task));
}

/**
 * fwupd_client_refresh_remote_async:
 * @self: a #FwupdClient
 * @remote: a #FwupdRemote
 * @download_flags: download flags, e.g. %FWUPD_CLIENT_DOWNLOAD_FLAG_ONLY_P2P
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Refreshes a remote by downloading new metadata.
 *
 * NOTE: This method is thread-safe, but progress signals will be
 * emitted in the global default main context, if not explicitly set with
 * [method@Client.set_main_context].
 *
 * Since: 2.0.0
 **/
void
fwupd_client_refresh_remote_async(FwupdClient *self,
				  FwupdRemote *remote,
				  FwupdClientDownloadFlags download_flags,
				  GCancellable *cancellable,
				  GAsyncReadyCallback callback,
				  gpointer callback_data)
{
	FwupdClientRefreshRemoteData *data;
	g_autofree gchar *uri = NULL;
	g_autoptr(GTask) task = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(FWUPD_IS_REMOTE(remote));
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));

	task = g_task_new(self, cancellable, callback, callback_data);
	data = g_new0(FwupdClientRefreshRemoteData, 1);
	data->download_flags = download_flags;
	data->remote = g_object_ref(remote);
	g_task_set_task_data(task,
			     g_steal_pointer(&data),
			     (GDestroyNotify)fwupd_client_refresh_remote_data_free);

	/* nothing to do */
	if (fwupd_remote_get_kind(remote) != FWUPD_REMOTE_KIND_DOWNLOAD) {
		g_debug("ignoring %s as %s",
			fwupd_remote_get_id(remote),
			fwupd_remote_kind_to_string(fwupd_remote_get_kind(remote)));
		g_task_return_boolean(task, TRUE);
		return;
	}

	/* sanity check */
	if (fwupd_remote_get_metadata_uri_sig(remote) == NULL ||
	    fwupd_remote_get_metadata_uri(remote) == NULL) {
		g_task_return_new_error(task,
					FWUPD_ERROR,
					FWUPD_ERROR_NOT_SUPPORTED,
					"no metadata URIs for %s",
					fwupd_remote_get_id(remote));
		return;
	}

	/* download signature */
	uri = fwupd_remote_build_metadata_sig_uri(remote, &error);
	if (uri == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	fwupd_client_download_bytes_async(self,
					  uri,
					  download_flags & ~FWUPD_CLIENT_DOWNLOAD_FLAG_ONLY_P2P,
					  cancellable,
					  fwupd_client_refresh_remote_signature_cb,
					  g_steal_pointer(&task));
}

/**
 * fwupd_client_refresh_remote_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.refresh_remote_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_refresh_remote_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

static void
fwupd_client_get_remotes_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	array = fwupd_codec_array_from_variant(val, FWUPD_TYPE_REMOTE, &error);
	if (array == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_pointer(task, g_steal_pointer(&array), (GDestroyNotify)g_ptr_array_unref);
}

/**
 * fwupd_client_get_remotes_async:
 * @self: a #FwupdClient
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets the list of remotes that have been configured for the system.
 *
 * You must have called [method@Client.connect_async] on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_remotes_async(FwupdClient *self,
			       GCancellable *cancellable,
			       GAsyncReadyCallback callback,
			       gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "GetRemotes",
			  NULL,
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_get_remotes_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_get_remotes_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.get_remotes_async].
 *
 * Returns: (element-type FwupdRemote) (transfer container): results
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_remotes_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(g_task_is_valid(res, self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

static void
fwupd_client_get_approved_firmware_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_auto(GStrv) strv = NULL;
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	g_variant_get(val, "(^as)", &strv);
	for (guint i = 0; strv[i] != NULL; i++)
		g_ptr_array_add(array, g_strdup(strv[i]));

	/* success */
	g_task_return_pointer(task, g_steal_pointer(&array), (GDestroyNotify)g_ptr_array_unref);
}

/**
 * fwupd_client_get_approved_firmware_async:
 * @self: a #FwupdClient
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets the list of approved firmware.
 *
 * You must have called [method@Client.connect_async] on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_approved_firmware_async(FwupdClient *self,
					 GCancellable *cancellable,
					 GAsyncReadyCallback callback,
					 gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "GetApprovedFirmware",
			  NULL,
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_get_approved_firmware_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_get_approved_firmware_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.get_approved_firmware_async].
 *
 * Returns: (element-type utf8) (transfer container): checksums, or %NULL for error
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_approved_firmware_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(g_task_is_valid(res, self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

static void
fwupd_client_set_approved_firmware_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}

/**
 * fwupd_client_set_approved_firmware_async:
 * @self: a #FwupdClient
 * @checksums: (element-type utf8): firmware checksums
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Sets the list of approved firmware.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_set_approved_firmware_async(FwupdClient *self,
					 GPtrArray *checksums,
					 GCancellable *cancellable,
					 GAsyncReadyCallback callback,
					 gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;
	g_auto(GStrv) strv = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	strv = g_new0(gchar *, checksums->len + 1);
	for (guint i = 0; i < checksums->len; i++) {
		const gchar *tmp = g_ptr_array_index(checksums, i);
		strv[i] = g_strdup(tmp);
	}
	g_dbus_proxy_call(priv->proxy,
			  "SetApprovedFirmware",
			  g_variant_new("(^as)", strv),
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_set_approved_firmware_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_set_approved_firmware_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.set_approved_firmware_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_set_approved_firmware_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

static void
fwupd_client_get_blocked_firmware_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_auto(GStrv) strv = NULL;
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	g_variant_get(val, "(^as)", &strv);
	for (guint i = 0; strv[i] != NULL; i++)
		g_ptr_array_add(array, g_strdup(strv[i]));

	/* success */
	g_task_return_pointer(task, g_steal_pointer(&array), (GDestroyNotify)g_ptr_array_unref);
}

/**
 * fwupd_client_get_blocked_firmware_async:
 * @self: a #FwupdClient
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets the list of blocked firmware.
 *
 * You must have called [method@Client.connect_async] on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_blocked_firmware_async(FwupdClient *self,
					GCancellable *cancellable,
					GAsyncReadyCallback callback,
					gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "GetBlockedFirmware",
			  NULL,
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_get_blocked_firmware_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_get_blocked_firmware_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.get_blocked_firmware_async].
 *
 * Returns: (element-type utf8) (transfer container): checksums, or %NULL for error
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_blocked_firmware_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(g_task_is_valid(res, self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

static void
fwupd_client_set_blocked_firmware_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}

/**
 * fwupd_client_set_blocked_firmware_async:
 * @self: a #FwupdClient
 * @checksums: (element-type utf8): firmware checksums
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Sets the list of blocked firmware.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_set_blocked_firmware_async(FwupdClient *self,
					GPtrArray *checksums,
					GCancellable *cancellable,
					GAsyncReadyCallback callback,
					gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;
	g_auto(GStrv) strv = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	strv = g_new0(gchar *, checksums->len + 1);
	for (guint i = 0; i < checksums->len; i++) {
		const gchar *tmp = g_ptr_array_index(checksums, i);
		strv[i] = g_strdup(tmp);
	}
	g_dbus_proxy_call(priv->proxy,
			  "SetBlockedFirmware",
			  g_variant_new("(^as)", strv),
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_set_blocked_firmware_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_set_blocked_firmware_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.set_blocked_firmware_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_set_blocked_firmware_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

static void
fwupd_client_set_feature_flags_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}

/**
 * fwupd_client_set_feature_flags_async:
 * @self: a #FwupdClient
 * @feature_flags: feature flags, e.g. %FWUPD_FEATURE_FLAG_UPDATE_TEXT
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Sets the features the client supports. This allows firmware to depend on
 * specific front-end features, for instance showing the user an image on
 * how to detach the hardware.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_set_feature_flags_async(FwupdClient *self,
				     FwupdFeatureFlags feature_flags,
				     GCancellable *cancellable,
				     GAsyncReadyCallback callback,
				     gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "SetFeatureFlags",
			  g_variant_new("(t)", (guint64)feature_flags),
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_set_feature_flags_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_set_feature_flags_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.set_feature_flags_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_set_feature_flags_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

static void
fwupd_client_self_sign_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	gchar *str = NULL;
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_variant_get(val, "(s)", &str);
	g_task_return_pointer(task, g_steal_pointer(&str), (GDestroyNotify)g_free);
}

/**
 * fwupd_client_self_sign_async:
 * @self: a #FwupdClient
 * @value: a string to sign, typically a JSON blob
 * @flags: signing flags, e.g. %FWUPD_SELF_SIGN_FLAG_ADD_TIMESTAMP
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Signs the data using the client self-signed certificate.
 *
 * You must have called [method@Client.connect_async] on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_self_sign_async(FwupdClient *self,
			     const gchar *value,
			     FwupdSelfSignFlags flags,
			     GCancellable *cancellable,
			     GAsyncReadyCallback callback,
			     gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	GVariantBuilder builder;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(value != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* set options */
	g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
	if (flags & FWUPD_SELF_SIGN_FLAG_ADD_TIMESTAMP) {
		g_variant_builder_add(&builder,
				      "{sv}",
				      "add-timestamp",
				      g_variant_new_boolean(TRUE));
	}
	if (flags & FWUPD_SELF_SIGN_FLAG_ADD_CERT) {
		g_variant_builder_add(&builder, "{sv}", "add-cert", g_variant_new_boolean(TRUE));
	}

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "SelfSign",
			  g_variant_new("(sa{sv})", value, &builder),
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_self_sign_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_self_sign_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.self_sign_async].
 *
 * Returns: a signature, or %NULL for failure
 *
 * Since: 1.5.0
 **/
gchar *
fwupd_client_self_sign_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(g_task_is_valid(res, self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

static void
fwupd_client_modify_remote_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}

/**
 * fwupd_client_modify_remote_async:
 * @self: a #FwupdClient
 * @remote_id: the remote ID, e.g. `lvfs-testing`
 * @key: the key, e.g. `Enabled`
 * @value: the key, e.g. `true`
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Modifies a system remote in a specific way.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_modify_remote_async(FwupdClient *self,
				 const gchar *remote_id,
				 const gchar *key,
				 const gchar *value,
				 GCancellable *cancellable,
				 GAsyncReadyCallback callback,
				 gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(remote_id != NULL);
	g_return_if_fail(key != NULL);
	g_return_if_fail(value != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "ModifyRemote",
			  g_variant_new("(sss)", remote_id, key, value),
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_modify_remote_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_modify_remote_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.modify_remote_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_modify_remote_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

static void
fwupd_client_modify_device_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}

/**
 * fwupd_client_modify_device_async:
 * @self: a #FwupdClient
 * @device_id: (not nullable): the device ID
 * @key: (not nullable): the key, e.g. `Flags`
 * @value: (not nullable): the value, e.g. `reported`
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Modifies a device in a specific way. Not all properties on the #FwupdDevice
 * are settable by the client, and some may have other restrictions on @value.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_modify_device_async(FwupdClient *self,
				 const gchar *device_id,
				 const gchar *key,
				 const gchar *value,
				 GCancellable *cancellable,
				 GAsyncReadyCallback callback,
				 gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(device_id != NULL);
	g_return_if_fail(key != NULL);
	g_return_if_fail(value != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "ModifyDevice",
			  g_variant_new("(sss)", device_id, key, value),
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_modify_device_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_modify_device_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.modify_device_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_modify_device_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

static FwupdRemote *
fwupd_client_get_remote_by_id_noref(GPtrArray *remotes, const gchar *remote_id)
{
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index(remotes, i);
		if (g_strcmp0(remote_id, fwupd_remote_get_id(remote)) == 0)
			return remote;
	}
	return NULL;
}

static void
fwupd_client_get_remote_by_id_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdRemote *remote_tmp;
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) remotes = NULL;
	const gchar *remote_id = g_task_get_task_data(task);

	remotes = fwupd_client_get_remotes_finish(FWUPD_CLIENT(source), res, &error);
	if (remotes == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	remote_tmp = fwupd_client_get_remote_by_id_noref(remotes, remote_id);
	if (remote_tmp == NULL) {
		g_task_return_new_error(task,
					FWUPD_ERROR,
					FWUPD_ERROR_NOT_FOUND,
					"no remote '%s' found in search paths",
					remote_id);
		return;
	}

	/* success */
	g_task_return_pointer(task, g_object_ref(remote_tmp), (GDestroyNotify)g_object_unref);
}

/**
 * fwupd_client_get_remote_by_id_async:
 * @self: a #FwupdClient
 * @remote_id: (not nullable): the remote ID, e.g. `lvfs-testing`
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets a specific remote that has been configured for the system.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_remote_by_id_async(FwupdClient *self,
				    const gchar *remote_id,
				    GCancellable *cancellable,
				    GAsyncReadyCallback callback,
				    gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(remote_id != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_task_set_task_data(task, g_strdup(remote_id), g_free);
	fwupd_client_get_remotes_async(self,
				       cancellable,
				       fwupd_client_get_remote_by_id_cb,
				       g_steal_pointer(&task));
}

/**
 * fwupd_client_get_remote_by_id_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.get_remote_by_id_async].
 *
 * Returns: (transfer full): a #FwupdRemote, or %NULL if not found
 *
 * Since: 1.5.0
 **/
FwupdRemote *
fwupd_client_get_remote_by_id_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(g_task_is_valid(res, self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

/**
 * fwupd_client_set_user_agent:
 * @self: a #FwupdClient
 * @user_agent: the user agent ID, e.g. `gnome-software/3.34.1`
 *
 * Manually sets the user agent that is used for downloading. The user agent
 * should contain the runtime version of fwupd somewhere in the provided string.
 *
 * Since: 1.4.5
 **/
void
fwupd_client_set_user_agent(FwupdClient *self, const gchar *user_agent)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(user_agent != NULL);

	/* not changed */
	if (g_strcmp0(priv->user_agent, user_agent) == 0)
		return;

	g_free(priv->user_agent);
	priv->user_agent = g_strdup(user_agent);
}

/**
 * fwupd_client_get_user_agent:
 * @self: a #FwupdClient
 *
 * Gets the string that represents the user agent that is used for
 * uploading and downloading. The user agent will contain the runtime
 * version of fwupd somewhere in the provided string.
 *
 * Returns: a string, or %NULL for unknown.
 *
 * Since: 1.5.2
 **/
const gchar *
fwupd_client_get_user_agent(FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	return priv->user_agent;
}

/**
 * fwupd_client_set_user_agent_for_package:
 * @self: a #FwupdClient
 * @package_name: (not nullable): client program name, e.g. `gnome-software`
 * @package_version: (not nullable): client program version, e.g. `3.28.1`
 *
 * Builds a user-agent to use for the download.
 *
 * Supplying harmless details to the server means it knows more about each
 * client. This allows the web service to respond in a different way, for
 * instance sending a different metadata file for old versions of fwupd, or
 * returning an error for Solaris machines.
 *
 * Before freaking out about theoretical privacy implications, much more data
 * than this is sent to each and every website you visit.
 *
 * Since: 1.4.5
 **/
void
fwupd_client_set_user_agent_for_package(FwupdClient *self,
					const gchar *package_name,
					const gchar *package_version)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(package_name != NULL);
	g_return_if_fail(package_version != NULL);

	g_free(priv->package_name);
	g_free(priv->package_version);
	priv->package_name = g_path_get_basename(package_name);
	priv->package_version = g_strdup(package_version);
	fwupd_client_rebuild_user_agent(self);
}

#ifdef HAVE_LIBCURL
static size_t
fwupd_client_download_write_callback_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	GByteArray *buf = (GByteArray *)userdata;
	gsize realsize = size * nmemb;
	g_byte_array_append(buf, (const guint8 *)ptr, realsize);
	return realsize;
}

static GBytes *
fwupd_client_download_ipfs(FwupdClient *self,
			   const gchar *url,
			   GCancellable *cancellable,
			   GError **error)
{
	GSubprocessFlags flags = G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE;
	g_autofree gchar *path = NULL;
	g_autoptr(GBytes) bstdout = NULL;
	g_autoptr(GBytes) bstderr = NULL;
	g_autoptr(GSubprocess) subprocess = NULL;

	/* we get no detailed progress details */
	fwupd_client_set_status(self, FWUPD_STATUS_DOWNLOADING);
	fwupd_client_set_percentage(self, 0);

	/* convert from URI to path */
	if (g_str_has_prefix(url, "ipfs://")) {
		path = g_strdup_printf("/ipfs/%s", url + 7);
	} else if (g_str_has_prefix(url, "ipns://")) {
		path = g_strdup_printf("/ipns/%s", url + 7);
	} else {
		path = g_strdup(url);
	}

	/* run sync */
	subprocess = g_subprocess_new(flags, error, "ipfs", "cat", path, NULL);
	if (subprocess == NULL)
		return NULL;
	if (!g_subprocess_communicate(subprocess, NULL, cancellable, &bstdout, &bstderr, error))
		return NULL;
	fwupd_client_set_status(self, FWUPD_STATUS_IDLE);
	if (g_subprocess_get_exit_status(subprocess) != 0) {
		const gchar *msg = g_bytes_get_data(bstderr, NULL);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "failed to download file: %s",
			    msg);
		return NULL;
	}
	return g_steal_pointer(&bstdout);
}

static GBytes *
fwupd_client_download_http(FwupdClient *self, CURL *curl, const gchar *url, GError **error)
{
	CURLcode res;
	gchar errbuf[CURL_ERROR_SIZE] = {'\0'};
	glong status_code = 0;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* relax the SSL checks on localhost URLs and broken corporate proxies */
	if (fwupd_client_is_localhost(url) || g_getenv("DISABLE_SSL_STRICT") != NULL) {
		(void)curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		(void)curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	} else {
		(void)curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
		(void)curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 1L);
	}

	fwupd_client_set_status(self, FWUPD_STATUS_DOWNLOADING);
	(void)curl_easy_setopt(curl, CURLOPT_URL, url);
	(void)curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
	(void)curl_easy_setopt(curl,
			       CURLOPT_WRITEFUNCTION,
			       fwupd_client_download_write_callback_cb);
	(void)curl_easy_setopt(curl, CURLOPT_WRITEDATA, buf);
	res = curl_easy_perform(curl);
	fwupd_client_set_status(self, FWUPD_STATUS_IDLE);
	fwupd_client_set_percentage(self, 100);
	if (res != CURLE_OK) {
		if (errbuf[0] != '\0') {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "failed to download file: %s",
				    errbuf);
			return NULL;
		}
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "failed to download file: %s",
			    curl_easy_strerror(res));
		return NULL;
	}

	/* check for server limit */
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
	g_info("status-code was %ld", status_code);
	if (status_code == 429) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "Failed to download due to server limit");
		return NULL;
	}
	if (status_code == 502 || status_code == 503 || status_code == 504) {
		g_autofree gchar *str = g_strndup((const gchar *)buf->data, MIN(buf->len, 4000));
		if (g_str_is_ascii(str)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_TIMED_OUT,
				    "Transient failure to download, server response was %u: %s",
				    (guint)status_code,
				    str);
			return NULL;
		}
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_TIMED_OUT,
			    "Transient failure to download, server response was %u",
			    (guint)status_code);
		return NULL;
	}
	if (status_code >= 400) {
		g_autofree gchar *str = g_strndup((const gchar *)buf->data, MIN(buf->len, 4000));
		if (g_str_is_ascii(str)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "Failed to download, server response was %u: %s",
				    (guint)status_code,
				    str);
			return NULL;
		}
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "Failed to download, server response was %u",
			    (guint)status_code);
		return NULL;
	}

	return g_bytes_new(buf->data, buf->len);
}

static gboolean
fwupd_client_download_error_is_fatal(const GError *error)
{
	if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT))
		return FALSE;
	return TRUE;
}

static GBytes *
fwupd_client_download_http_retry(FwupdClient *self, CURL *curl, const gchar *url, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	gulong delay_ms = 2500;
	for (guint i = 0;; i++, delay_ms *= 2) {
		g_autoptr(GBytes) blob = NULL;
		g_autoptr(GError) error_local = NULL;

		blob = fwupd_client_download_http(self, curl, url, &error_local);
		if (blob != NULL)
			return g_steal_pointer(&blob);
		if (i >= priv->download_retries ||
		    fwupd_client_download_error_is_fatal(error_local)) {
			g_propagate_error(error, g_steal_pointer(&error_local));
			break;
		}
		g_debug("ignoring and trying again: %s", error_local->message);
		g_usleep(delay_ms * 1000);
	}
	return NULL;
}
static void
fwupd_client_download_bytes_thread_cb(GTask *task,
				      gpointer source_object,
				      gpointer task_data,
				      GCancellable *cancellable)
{
	FwupdClient *self = FWUPD_CLIENT(source_object);
	FwupdCurlHelper *helper = g_task_get_task_data(task);
	g_autoptr(GBytes) blob = NULL;

	for (guint i = 0; i < helper->urls->len; i++) {
		const gchar *url = g_ptr_array_index(helper->urls, i);
		g_autoptr(GError) error = NULL;
		g_info("downloading %s", url);
		if (!fwupd_client_curl_helper_set_proxy(self, helper, url, &error)) {
			g_task_return_error(task, g_steal_pointer(&error));
			return;
		}
		if (fwupd_client_is_url_http(url)) {
			blob = fwupd_client_download_http_retry(self, helper->curl, url, &error);
			if (blob != NULL)
				break;
		} else if (fwupd_client_is_url_ipfs(url)) {
			blob = fwupd_client_download_ipfs(self, url, cancellable, &error);
			if (blob != NULL)
				break;
		} else {
			g_set_error(&error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "not sure how to handle: %s",
				    url);
		}
		if (i == helper->urls->len - 1) {
			g_task_return_error(task, g_steal_pointer(&error));
			return;
		}
		fwupd_client_set_percentage(self, 0);
		fwupd_client_set_status(self, FWUPD_STATUS_IDLE);
		g_info("failed to download %s: %s, trying next URI…", url, error->message);
	}
	g_task_return_pointer(task, g_steal_pointer(&blob), (GDestroyNotify)g_bytes_unref);
}
#endif

/* private */
void
fwupd_client_download_bytes2_async(FwupdClient *self,
				   GPtrArray *urls,
				   FwupdClientDownloadFlags flags,
				   GCancellable *cancellable,
				   GAsyncReadyCallback callback,
				   gpointer callback_data)
{
	g_autoptr(GTask) task = NULL;
#ifdef HAVE_LIBCURL
	g_autoptr(GError) error = NULL;
	g_autoptr(FwupdCurlHelper) helper = NULL;
#endif

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(urls != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));

	/* ensure networking set up */
	task = g_task_new(self, cancellable, callback, callback_data);
#ifdef HAVE_LIBCURL
	helper = fwupd_client_curl_new(self, &error);
	if (helper == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	helper->urls = fwupd_client_filter_locations(urls, flags, &error);
	if (helper->urls == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	g_task_set_task_data(task,
			     g_steal_pointer(&helper),
			     (GDestroyNotify)fwupd_client_curl_helper_free);

	/* download data */
	g_task_run_in_thread(task, fwupd_client_download_bytes_thread_cb);
#else
	g_task_return_new_error(task, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no libcurl support");
#endif
}

/**
 * fwupd_client_download_bytes_async:
 * @self: a #FwupdClient
 * @url: (not nullable): the remote URL
 * @flags: download flags, e.g. %FWUPD_CLIENT_DOWNLOAD_FLAG_NONE
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Downloads data from a remote server. The [method@Client.set_user_agent] function
 * should be called before this method is used.
 *
 * You must have called [method@Client.connect_async] on @self before using
 * this method.
 *
 * NOTE: This method is thread-safe, but progress signals will be
 * emitted in the global default main context, if not explicitly set with
 * [method@Client.set_main_context].
 *
 * Since: 1.5.0
 **/
void
fwupd_client_download_bytes_async(FwupdClient *self,
				  const gchar *url,
				  FwupdClientDownloadFlags flags,
				  GCancellable *cancellable,
				  GAsyncReadyCallback callback,
				  gpointer callback_data)
{
	g_autoptr(GPtrArray) urls = g_ptr_array_new_with_free_func(g_free);

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(url != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));

	/* just proxy */
	g_ptr_array_add(urls, g_strdup(url));
	fwupd_client_download_bytes2_async(self, urls, flags, cancellable, callback, callback_data);
}

/**
 * fwupd_client_download_bytes_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.download_bytes_async].
 *
 * Returns: (transfer full): downloaded data, or %NULL for error
 *
 * Since: 1.5.0
 **/
GBytes *
fwupd_client_download_bytes_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(g_task_is_valid(res, self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

#ifdef HAVE_LIBCURL
static void
fwupd_client_upload_bytes_thread_cb(GTask *task,
				    gpointer source_object,
				    gpointer task_data,
				    GCancellable *cancellable)
{
	FwupdClient *self = FWUPD_CLIENT(source_object);
	FwupdCurlHelper *helper = g_task_get_task_data(task);
	CURLcode res;
	gchar errbuf[CURL_ERROR_SIZE] = {'\0'};
	g_autoptr(GByteArray) buf = g_byte_array_new();

	(void)curl_easy_setopt(helper->curl, CURLOPT_ERRORBUFFER, errbuf);
	(void)curl_easy_setopt(helper->curl,
			       CURLOPT_WRITEFUNCTION,
			       fwupd_client_download_write_callback_cb);
	(void)curl_easy_setopt(helper->curl, CURLOPT_WRITEDATA, buf);
	res = curl_easy_perform(helper->curl);
	fwupd_client_set_status(self, FWUPD_STATUS_IDLE);
	if (res != CURLE_OK) {
		glong status_code = 0;
		curl_easy_getinfo(helper->curl, CURLINFO_RESPONSE_CODE, &status_code);
		g_info("status-code was %ld", status_code);
		if (errbuf[0] != '\0') {
			g_task_return_new_error(task,
						FWUPD_ERROR,
						FWUPD_ERROR_INVALID_FILE,
						"failed to upload file: %s",
						errbuf);
			return;
		}
		g_task_return_new_error(task,
					FWUPD_ERROR,
					FWUPD_ERROR_INVALID_FILE,
					"failed to upload file: %s",
					curl_easy_strerror(res));

		return;
	}
	g_task_return_pointer(task,
			      g_bytes_new(buf->data, buf->len),
			      (GDestroyNotify)g_bytes_unref);
}
#endif

/**
 * fwupd_client_upload_bytes_async:
 * @self: a #FwupdClient
 * @url: (not nullable): the remote URL
 * @payload: (not nullable): payload string
 * @signature: (nullable): signature string
 * @flags: download flags, e.g. %FWUPD_CLIENT_DOWNLOAD_FLAG_NONE
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Uploads data to a remote server. The [method@Client.set_user_agent] function
 * should be called before this method is used.
 *
 * You must have called [method@Client.connect_async] on @self before using
 * this method.
 *
 * NOTE: This method is thread-safe, but progress signals will be
 * emitted in the global default main context, if not explicitly set with
 * [method@Client.set_main_context].
 *
 * Since: 1.5.0
 **/
void
fwupd_client_upload_bytes_async(FwupdClient *self,
				const gchar *url,
				const gchar *payload,
				const gchar *signature,
				FwupdClientUploadFlags flags,
				GCancellable *cancellable,
				GAsyncReadyCallback callback,
				gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;
#ifdef HAVE_LIBCURL
	g_autoptr(FwupdCurlHelper) helper = NULL;
	g_autoptr(GError) error = NULL;
#endif

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(url != NULL);
	g_return_if_fail(payload != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* ensure networking set up */
	task = g_task_new(self, cancellable, callback, callback_data);
#ifdef HAVE_LIBCURL
	helper = fwupd_client_curl_new(self, &error);
	if (helper == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* build message */
	if ((flags & FWUPD_CLIENT_UPLOAD_FLAG_ALWAYS_MULTIPART) > 0 || signature != NULL) {
		curl_mimepart *part;
		helper->mime = curl_mime_init(helper->curl);
		(void)curl_easy_setopt(helper->curl, CURLOPT_MIMEPOST, helper->mime);
		part = curl_mime_addpart(helper->mime);
		(void)curl_mime_data(part, payload, CURL_ZERO_TERMINATED);
		curl_mime_name(part, "payload");
		if (signature != NULL) {
			part = curl_mime_addpart(helper->mime);
			(void)curl_mime_data(part, signature, CURL_ZERO_TERMINATED);
			curl_mime_name(part, "signature");
		}
	} else {
		helper->headers = curl_slist_append(helper->headers, "Content-Type: text/plain");
		(void)curl_easy_setopt(helper->curl, CURLOPT_HTTPHEADER, helper->headers);
		(void)curl_easy_setopt(helper->curl, CURLOPT_POST, 1L);
		(void)curl_easy_setopt(helper->curl, CURLOPT_POSTFIELDSIZE, strlen(payload));
		(void)curl_easy_setopt(helper->curl, CURLOPT_COPYPOSTFIELDS, payload);
	}

	/* relax the SSL checks on localhost URLs and broken corporate proxies */
	if (fwupd_client_is_localhost(url) || g_getenv("DISABLE_SSL_STRICT") != NULL) {
		(void)curl_easy_setopt(helper->curl, CURLOPT_SSL_VERIFYPEER, 0L);
		(void)curl_easy_setopt(helper->curl, CURLOPT_SSL_VERIFYHOST, 0L);
	} else {
		(void)curl_easy_setopt(helper->curl, CURLOPT_SSL_VERIFYPEER, 1L);
		(void)curl_easy_setopt(helper->curl, CURLOPT_SSL_VERIFYHOST, 1L);
	}

	fwupd_client_set_status(self, FWUPD_STATUS_IDLE);
	g_info("uploading to %s", url);
	(void)curl_easy_setopt(helper->curl, CURLOPT_URL, url);
	g_task_set_task_data(task,
			     g_steal_pointer(&helper),
			     (GDestroyNotify)fwupd_client_curl_helper_free);
	g_task_run_in_thread(task, fwupd_client_upload_bytes_thread_cb);
#else
	g_task_return_new_error(task, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no libcurl support");
#endif
}

/**
 * fwupd_client_upload_bytes_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.upload_bytes_async].
 *
 * Returns: (transfer full): response data, or %NULL for error
 *
 * Since: 1.5.0
 **/
GBytes *
fwupd_client_upload_bytes_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(g_task_is_valid(res, self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

static void
fwupd_client_upload_report_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	const gchar *server_msg = NULL;
	JsonNode *json_root;
	JsonObject *json_object;
	g_autofree gchar *str = NULL;
	g_autofree gchar *uri = NULL;
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(JsonParser) json_parser = NULL;

	/* parse */
	bytes = fwupd_client_upload_bytes_finish(FWUPD_CLIENT(source), res, &error);
	if (bytes == NULL) {
		g_prefix_error(&error, "failed to upload report: ");
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* server returned nothing, and probably exploded in a ball of flames */
	if (g_bytes_get_size(bytes) == 0) {
		g_task_return_new_error(task,
					FWUPD_ERROR,
					FWUPD_ERROR_INVALID_FILE,
					"failed to upload, zero length data");
		return;
	}

	/* parse JSON reply */
	json_parser = json_parser_new();
	str = g_strndup(g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes));
	if (!json_parser_load_from_data(json_parser, str, -1, &error)) {
		g_task_return_new_error(task,
					FWUPD_ERROR,
					FWUPD_ERROR_INVALID_DATA,
					"failed to parse JSON response from '%s': %s",
					str,
					error->message);
		return;
	}
	json_root = json_parser_get_root(json_parser);
	if (json_root == NULL) {
		g_task_return_new_error(task,
					FWUPD_ERROR,
					FWUPD_ERROR_INVALID_DATA,
					"JSON response was malformed: '%s'",
					str);
		return;
	}
	json_object = json_node_get_object(json_root);
	if (json_object == NULL) {
		g_task_return_new_error(task,
					FWUPD_ERROR,
					FWUPD_ERROR_INVALID_DATA,
					"JSON response object was malformed: '%s'",
					str);
		return;
	}

	/* get any optional server message */
	if (json_object_has_member(json_object, "msg"))
		server_msg = json_object_get_string_member(json_object, "msg");

	/* server reported failed */
	if (!json_object_get_boolean_member(json_object, "success")) {
		g_task_return_new_error(task,
					FWUPD_ERROR,
					FWUPD_ERROR_PERMISSION_DENIED,
					"server rejected report: %s",
					server_msg != NULL ? server_msg : "unspecified");
		return;
	}

	/* server wanted us to see the message */
	if (server_msg != NULL) {
		g_info("server message: %s", server_msg);
		if (json_object_has_member(json_object, "uri"))
			uri = g_strdup(json_object_get_string_member(json_object, "uri"));
	}

	/* fallback */
	if (uri == NULL)
		uri = g_strdup("");

	/* success */
	g_task_return_pointer(task, g_steal_pointer(&uri), g_free);
}

/**
 * fwupd_client_upload_report_async:
 * @self: a #FwupdClient
 * @url: (not nullable): the remote URL
 * @payload: (not nullable): payload string
 * @signature: (nullable): signature string
 * @flags: download flags, e.g. %FWUPD_CLIENT_DOWNLOAD_FLAG_NONE
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Uploads a report to a remote server. The [method@Client.set_user_agent] function
 * should be called before this method is used.
 *
 * You must have called [method@Client.connect_async] on @self before using
 * this method.
 *
 * NOTE: This method is thread-safe, but progress signals will be
 * emitted in the global default main context, if not explicitly set with
 * [method@Client.set_main_context].
 *
 * Since: 1.9.20
 **/
void
fwupd_client_upload_report_async(FwupdClient *self,
				 const gchar *url,
				 const gchar *payload,
				 const gchar *signature,
				 FwupdClientUploadFlags flags,
				 GCancellable *cancellable,
				 GAsyncReadyCallback callback,
				 gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(url != NULL);
	g_return_if_fail(payload != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	task = g_task_new(self, cancellable, callback, callback_data);
	fwupd_client_upload_bytes_async(self,
					url,
					payload,
					signature,
					flags,
					cancellable,
					fwupd_client_upload_report_cb,
					g_steal_pointer(&task));
}

/**
 * fwupd_client_upload_report_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.upload_report_async].
 *
 * Returns: (transfer full): a URI (perhaps an empty string), or %NULL for error
 *
 * Since: 1.9.20
 **/
gchar *
fwupd_client_upload_report_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(g_task_is_valid(res, self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

static void
fwupd_client_inhibit_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autofree gchar *inhibit_id = NULL;
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_variant_get(val, "(s)", &inhibit_id);
	g_task_return_pointer(task, g_steal_pointer(&inhibit_id), g_free);
}

/**
 * fwupd_client_inhibit_async:
 * @self: a #FwupdClient
 * @reason: (not nullable): the inhibit reason, e.g. `user active`
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Marks all devices as unavailable for update. Update is only available if there is no other
 * inhibit imposed by other applications or by the system (e.g. low power state).
 *
 * The same application can inhibit the system multiple times.
 *
 * Since: 1.8.11
 **/
void
fwupd_client_inhibit_async(FwupdClient *self,
			   const gchar *reason,
			   GCancellable *cancellable,
			   GAsyncReadyCallback callback,
			   gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(reason != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "Inhibit",
			  g_variant_new("(s)", reason),
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_inhibit_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_inhibit_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.inhibit_async].
 *
 * Returns: (transfer full): a string to use for [method@FwupdClient.uninhibit_async],
 * or %NULL for failure
 *
 * Since: 1.8.11
 **/
gchar *
fwupd_client_inhibit_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(g_task_is_valid(res, self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

static void
fwupd_client_uninhibit_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}

/**
 * fwupd_client_uninhibit_async:
 * @self: a #FwupdClient
 * @inhibit_id: (not nullable): the inhibit ID
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Removes the inhibit token added by the application.
 *
 * Since: 1.8.11
 **/
void
fwupd_client_uninhibit_async(FwupdClient *self,
			     const gchar *inhibit_id,
			     GCancellable *cancellable,
			     GAsyncReadyCallback callback,
			     gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(inhibit_id != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "Uninhibit",
			  g_variant_new("(s)", inhibit_id),
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_uninhibit_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_uninhibit_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.uninhibit_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.11
 **/
gboolean
fwupd_client_uninhibit_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

/**
 * fwupd_client_add_hint:
 * @self: a #FwupdClient
 * @key: (not nullable): the key, e.g. `locale`
 * @value: (nullable): the value @key should be set
 *
 * Sets optional hints from the client that may affect the list of devices.
 *
 * Since: 1.7.1
 **/
void
fwupd_client_add_hint(FwupdClient *self, const gchar *key, const gchar *value)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(key != NULL);

	g_hash_table_insert(priv->hints, g_strdup(key), g_strdup(value));
}

#ifdef HAVE_GIO_UNIX
static void
fwupd_client_emulation_load_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GDBusMessage) msg = NULL;
	g_autoptr(GError) error = NULL;

	msg = g_dbus_connection_send_message_with_reply_finish(G_DBUS_CONNECTION(source),
							       res,
							       &error);
	if (msg == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	if (g_dbus_message_to_gerror(msg, &error)) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}
#endif

/**
 * fwupd_client_emulation_load_async:
 * @self: a #FwupdClient
 * @filename: archive data of JSON files
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Loads an emulated device into the daemon backend that has the phases set by the JSON data,
 * for instance, having one USB device emulated for the bootloader and another emulated for the
 * runtime interface.
 *
 * Since: 2.0.0
 **/
void
fwupd_client_emulation_load_async(FwupdClient *self,
				  const gchar *filename,
				  GCancellable *cancellable,
				  GAsyncReadyCallback callback,
				  gpointer callback_data)
{
#ifdef HAVE_GIO_UNIX
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GDBusMessage) request = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = g_task_new(self, cancellable, callback, callback_data);
	g_autoptr(GUnixFDList) fd_list = NULL;
	g_autoptr(GUnixInputStream) istr = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(filename != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	istr = fwupd_unix_input_stream_from_fn(filename, &error);
	if (istr == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* set out of band file descriptor */
	fd_list = g_unix_fd_list_new();
	g_unix_fd_list_append(fd_list, g_unix_input_stream_get_fd(istr), NULL);
	request = g_dbus_message_new_method_call(FWUPD_DBUS_SERVICE,
						 FWUPD_DBUS_PATH,
						 FWUPD_DBUS_INTERFACE,
						 "EmulationLoad");
	g_dbus_message_set_unix_fd_list(request, fd_list);

	/* call into daemon */
	g_dbus_message_set_body(request, g_variant_new("(h)", g_unix_input_stream_get_fd(istr)));
	g_dbus_connection_send_message_with_reply(g_dbus_proxy_get_connection(priv->proxy),
						  request,
						  G_DBUS_SEND_MESSAGE_FLAGS_NONE,
						  G_MAXINT,
						  NULL,
						  cancellable,
						  fwupd_client_emulation_load_cb,
						  g_steal_pointer(&task));
#else
	g_autoptr(GTask) task = g_task_new(self, cancellable, callback, callback_data);
	g_task_return_new_error_literal(task,
					FWUPD_ERROR,
					FWUPD_ERROR_NOT_SUPPORTED,
					"not supported as <gio/gunixfdlist.h> not found");
#endif
}

/**
 * fwupd_client_emulation_load_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.emulation_load_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.0
 **/
gboolean
fwupd_client_emulation_load_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

#ifdef HAVE_GIO_UNIX
static void
fwupd_client_emulation_save_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GDBusMessage) msg = NULL;
	g_autoptr(GError) error = NULL;

	msg = g_dbus_connection_send_message_with_reply_finish(G_DBUS_CONNECTION(source),
							       res,
							       &error);
	if (msg == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	if (g_dbus_message_to_gerror(msg, &error)) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}
#endif

/**
 * fwupd_client_emulation_save_async:
 * @self: a #FwupdClient
 * @filename: archive data of JSON files
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets the captured data from all filtered devices for all recorded phases. The data is returned
 * in a ZIP archive of JSON output.
 *
 * NOTE: Device events are not automatically recorded for all devices. You must call something
 * like `ModifyDevice(device_id, 'flags','emulation-tag')` to start the recording the backend.
 *
 * Once the device has been re-inserted then the emulation data will be available using
 * this API call.
 *
 * You must have called [method@Client.connect_async] on @self before using
 * this method.
 *
 * Since: 2.0.0
 **/
void
fwupd_client_emulation_save_async(FwupdClient *self,
				  const gchar *filename,
				  GCancellable *cancellable,
				  GAsyncReadyCallback callback,
				  gpointer callback_data)
{
#ifdef HAVE_GIO_UNIX
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GDBusMessage) request = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = g_task_new(self, cancellable, callback, callback_data);
	g_autoptr(GUnixFDList) fd_list = NULL;
	g_autoptr(GUnixOutputStream) istr = NULL;

	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(filename != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	istr = fwupd_unix_output_stream_from_fn(filename, &error);
	if (istr == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* set out of band file descriptor */
	fd_list = g_unix_fd_list_new();
	g_unix_fd_list_append(fd_list, g_unix_output_stream_get_fd(istr), NULL);
	request = g_dbus_message_new_method_call(FWUPD_DBUS_SERVICE,
						 FWUPD_DBUS_PATH,
						 FWUPD_DBUS_INTERFACE,
						 "EmulationSave");
	g_dbus_message_set_unix_fd_list(request, fd_list);

	/* call into daemon */
	g_dbus_message_set_body(request, g_variant_new("(h)", g_unix_output_stream_get_fd(istr)));
	g_dbus_connection_send_message_with_reply(g_dbus_proxy_get_connection(priv->proxy),
						  request,
						  G_DBUS_SEND_MESSAGE_FLAGS_NONE,
						  G_MAXINT,
						  NULL,
						  cancellable,
						  fwupd_client_emulation_save_cb,
						  g_steal_pointer(&task));
#else
	g_autoptr(GTask) task = g_task_new(self, cancellable, callback, callback_data);
	g_task_return_new_error_literal(task,
					FWUPD_ERROR,
					FWUPD_ERROR_NOT_SUPPORTED,
					"not supported as <gio/gunixfdlist.h> not found");
#endif
}

/**
 * fwupd_client_emulation_save_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.emulation_save_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.0
 **/
gboolean
fwupd_client_emulation_save_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

static void
fwupd_client_fix_host_security_attr_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}

/**
 * fwupd_client_fix_host_security_attr_async:
 * @self: a #FwupdClient
 * @appstream_id: HSI AppStream ID
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Fix one specific security attribute.
 *
 * Since: 1.9.6
 **/
void
fwupd_client_fix_host_security_attr_async(FwupdClient *self,
					  const gchar *appstream_id,
					  GCancellable *cancellable,
					  GAsyncReadyCallback callback,
					  gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(appstream_id != NULL);
	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "FixHostSecurityAttr",
			  g_variant_new("(s)", appstream_id),
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_fix_host_security_attr_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_fix_host_security_attr_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.fix_host_security_attr_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 1.9.6
 **/
gboolean
fwupd_client_fix_host_security_attr_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

static void
fwupd_client_undo_host_security_attr_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error(error);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}

/**
 * fwupd_client_undo_host_security_attr_async:
 * @self: a #FwupdClient
 * @appstream_id: HSI AppStream ID
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async) (closure callback_data): the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Reverts the fix to one specific security attribute.
 *
 * Since: 1.9.6
 **/
void
fwupd_client_undo_host_security_attr_async(FwupdClient *self,
					   const gchar *appstream_id,
					   GCancellable *cancellable,
					   GAsyncReadyCallback callback,
					   gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(appstream_id != NULL);
	g_return_if_fail(FWUPD_IS_CLIENT(self));
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new(self, cancellable, callback, callback_data);
	g_dbus_proxy_call(priv->proxy,
			  "UndoHostSecurityAttr",
			  g_variant_new("(s)", appstream_id),
			  G_DBUS_CALL_FLAGS_NONE,
			  FWUPD_CLIENT_DBUS_PROXY_TIMEOUT,
			  cancellable,
			  fwupd_client_undo_host_security_attr_cb,
			  g_steal_pointer(&task));
}

/**
 * fwupd_client_undo_host_security_attr_finish:
 * @self: a #FwupdClient
 * @res: (not nullable): the asynchronous result
 * @error: (nullable): optional return location for an error
 *
 * Gets the result of [method@FwupdClient.undo_host_security_attr_async].
 *
 * Returns: %TRUE for success
 *
 * Since: 1.9.6
 **/
gboolean
fwupd_client_undo_host_security_attr_finish(FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

static void
fwupd_client_build_report_metadata(JsonBuilder *builder, GHashTable *metadata)
{
	GHashTableIter iter;
	const gchar *key;
	const gchar *value;

	g_hash_table_iter_init(&iter, metadata);
	while (g_hash_table_iter_next(&iter, (gpointer *)&key, (gpointer *)&value)) {
		json_builder_set_member_name(builder, key);
		json_builder_add_string_value(builder, value);
	}
}

/**
 * fwupd_client_build_report_devices:
 * @self: a #FwupdClient
 * @devices: (element-type FwupdDevice): devices
 * @metadata: (element-type utf8 utf8): attributes
 * @error: (nullable): optional return location for an error
 *
 * Builds a JSON report for the list of devices.
 *
 * This function should be called *before* asking the interactive user if they want to upload a
 * report -- as this function filters devices and may return an error if there is nothing to do.
 *
 * You must have called [method@Client.connect_async] on @self before using
 * this method.
 *
 * Returns: a string, or %NULL if the ID is not present
 *
 * Since: 1.9.20
 **/
gchar *
fwupd_client_build_report_devices(FwupdClient *self,
				  GPtrArray *devices,
				  GHashTable *metadata,
				  GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	guint cnt = 0;
	g_autofree gchar *data = NULL;
	g_autoptr(JsonBuilder) builder = json_builder_new();
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonNode) json_root = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(devices != NULL, NULL);
	g_return_val_if_fail(metadata != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "ReportType");
	json_builder_add_string_value(builder, "device-list");
	json_builder_set_member_name(builder, "ReportVersion");
	json_builder_add_int_value(builder, 2);
	if (priv->host_machine_id != NULL) {
		json_builder_set_member_name(builder, "MachineId");
		json_builder_add_string_value(builder, priv->host_machine_id);
	}

	/* this is system metadata not stored in the database */
	if (g_hash_table_size(metadata) > 0) {
		json_builder_set_member_name(builder, "Metadata");
		json_builder_begin_object(builder);
		fwupd_client_build_report_metadata(builder, metadata);
		json_builder_end_object(builder);
	}

	/* devices */
	json_builder_set_member_name(builder, "Devices");
	json_builder_begin_array(builder);
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devices, i);
		if (!fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE) &&
		    !fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN)) {
			g_debug("ignoring %s as not updatable", fwupd_device_get_id(dev));
			continue;
		}
		json_builder_begin_object(builder);
		fwupd_codec_to_json(FWUPD_CODEC(dev), builder, FWUPD_CODEC_FLAG_TRUSTED);
		json_builder_end_object(builder);
		cnt++;
	}
	json_builder_end_array(builder);
	json_builder_end_object(builder);

	/* nothing to do */
	if (cnt == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "no devices to upload");
		return NULL;
	}

	/* export as a string */
	json_root = json_builder_get_root(builder);
	json_generator = json_generator_new();
	json_generator_set_pretty(json_generator, TRUE);
	json_generator_set_root(json_generator, json_root);
	data = json_generator_to_data(json_generator, NULL);
	if (data == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to convert to JSON string");
		return NULL;
	}
	return g_steal_pointer(&data);
}

static void
fwupd_client_build_report_history_device(JsonBuilder *builder, FwupdDevice *dev)
{
	FwupdRelease *rel = fwupd_device_get_release_default(dev);
	GChecksumType checksum_types[] = {G_CHECKSUM_SHA256, G_CHECKSUM_SHA1, 0};
	GHashTable *metadata = fwupd_release_get_metadata(rel);
	GPtrArray *checksums;
	GPtrArray *guids;

	/* identify the firmware used */
	checksums = fwupd_release_get_checksums(rel);
	for (guint i = 0; checksum_types[i] != 0; i++) {
		const gchar *checksum = fwupd_checksum_get_by_kind(checksums, checksum_types[i]);
		if (checksum != NULL) {
			json_builder_set_member_name(builder, "Checksum");
			json_builder_add_string_value(builder, checksum);
			break;
		}
	}

	/* identify the firmware written */
	checksums = fwupd_device_get_checksums(dev);
	if (checksums->len > 0) {
		json_builder_set_member_name(builder, "ChecksumDevice");
		json_builder_begin_array(builder);
		for (guint i = 0; i < checksums->len; i++) {
			const gchar *checksum = g_ptr_array_index(checksums, i);
			json_builder_add_string_value(builder, checksum);
		}
		json_builder_end_array(builder);
	}

	/* allow matching the specific component */
	json_builder_set_member_name(builder, "ReleaseId");
	json_builder_add_string_value(builder, fwupd_release_get_id(rel));

	/* include the protocol used */
	if (fwupd_release_get_protocol(rel) != NULL) {
		json_builder_set_member_name(builder, "Protocol");
		json_builder_add_string_value(builder, fwupd_release_get_protocol(rel));
	}

	/* set the error state of the report */
	json_builder_set_member_name(builder, "UpdateState");
	json_builder_add_int_value(builder, fwupd_device_get_update_state(dev));
	if (fwupd_device_get_update_error(dev) != NULL) {
		json_builder_set_member_name(builder, "UpdateError");
		json_builder_add_string_value(builder, fwupd_device_get_update_error(dev));
	}
	if (fwupd_release_get_update_message(rel) != NULL) {
		json_builder_set_member_name(builder, "UpdateMessage");
		json_builder_add_string_value(builder, fwupd_release_get_update_message(rel));
	}

	/* find out if the predicted duration was accurate */
	if (fwupd_device_get_install_duration(dev) != 0) {
		json_builder_set_member_name(builder, "InstallDuration");
		json_builder_add_int_value(builder, fwupd_device_get_install_duration(dev));
	}

	/* map back to the dev type on the LVFS */
	guids = fwupd_device_get_guids(dev);
	if (guids->len > 0) {
		json_builder_set_member_name(builder, "Guid");
		json_builder_begin_array(builder);
		for (guint i = 0; i < guids->len; i++) {
			const gchar *guid = g_ptr_array_index(guids, i);
			json_builder_add_string_value(builder, guid);
		}
		json_builder_end_array(builder);
	}

	json_builder_set_member_name(builder, "Plugin");
	json_builder_add_string_value(builder, fwupd_device_get_plugin(dev));

	/* report what we're trying to update *from* and *to* */
	json_builder_set_member_name(builder, "VersionOld");
	json_builder_add_string_value(builder, fwupd_device_get_version(dev));
	json_builder_set_member_name(builder, "VersionNew");
	json_builder_add_string_value(builder, fwupd_release_get_version(rel));

	/* to know the state of the dev we're trying to update */
	json_builder_set_member_name(builder, "Flags");
	json_builder_add_int_value(builder, fwupd_device_get_flags(dev));

	/* to know when the update tried to happen, and how soon after boot */
	json_builder_set_member_name(builder, "Created");
	json_builder_add_int_value(builder, fwupd_device_get_created(dev));
	json_builder_set_member_name(builder, "Modified");
	json_builder_add_int_value(builder, fwupd_device_get_modified(dev));

	/* add saved metadata to the report */
	if (g_hash_table_size(metadata) > 0) {
		json_builder_set_member_name(builder, "Metadata");
		json_builder_begin_object(builder);
		fwupd_client_build_report_metadata(builder, metadata);
		json_builder_end_object(builder);
	}
}

/**
 * fwupd_client_build_report_history:
 * @self: a #FwupdClient
 * @devices: (element-type FwupdDevice): devices
 * @remote: (nullable): optional #FwupdRemote
 * @metadata: (element-type utf8 utf8): attributes
 * @error: (nullable): optional return location for an error
 *
 * Builds a JSON report for the list of devices.
 *
 * This function should be called *before* asking the interactive user if they want to upload a
 * report -- as this function filters devices and may return an error if there is nothing to do.
 *
 * You must have called [method@Client.connect_async] on @self before using
 * this method.
 *
 * Returns: a string, or %NULL on error
 *
 * Since: 2.0.0
 **/
gchar *
fwupd_client_build_report_history(FwupdClient *self,
				  GPtrArray *devices,
				  FwupdRemote *remote,
				  GHashTable *metadata,
				  GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	guint cnt = 0;
	g_autofree gchar *data = NULL;
	g_autoptr(JsonBuilder) builder = json_builder_new();
	g_autoptr(JsonGenerator) json_generator = json_generator_new();
	g_autoptr(JsonNode) json_root = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(devices != NULL, NULL);
	g_return_val_if_fail(FWUPD_IS_REMOTE(remote) || remote == NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* sanity check */
	if (remote != NULL && fwupd_remote_get_report_uri(remote) == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOTHING_TO_DO,
			    "%s has no ReportURI",
			    fwupd_remote_get_id(remote));
		return NULL;
	}

	/* create header */
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "ReportType");
	json_builder_add_string_value(builder, "history");
	json_builder_set_member_name(builder, "ReportVersion");
	json_builder_add_int_value(builder, 2);
	if (priv->host_machine_id != NULL) {
		json_builder_set_member_name(builder, "MachineId");
		json_builder_add_string_value(builder, priv->host_machine_id);
	}

	/* this is system metadata not stored in the database */
	if (g_hash_table_size(metadata) > 0) {
		json_builder_set_member_name(builder, "Metadata");
		json_builder_begin_object(builder);
		fwupd_client_build_report_metadata(builder, metadata);
		json_builder_end_object(builder);
	}

	/* add each device */
	json_builder_set_member_name(builder, "Reports");
	json_builder_begin_array(builder);
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devices, i);

		if (fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_REPORTED)) {
			g_debug("ignoring %s as already reported", fwupd_device_get_id(dev));
			continue;
		}
		if (fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION)) {
			g_debug("ignoring %s as needs-activation", fwupd_device_get_id(dev));
			continue;
		}
		if (fwupd_device_get_update_state(dev) != FWUPD_UPDATE_STATE_FAILED &&
		    fwupd_device_get_update_state(dev) != FWUPD_UPDATE_STATE_SUCCESS) {
			g_debug("ignoring %s with UpdateState %s",
				fwupd_device_get_id(dev),
				fwupd_update_state_to_string(fwupd_device_get_update_state(dev)));
			continue;
		}

		/* is this the correct remote */
		if (remote != NULL) {
			FwupdRelease *rel = fwupd_device_get_release_default(dev);
			if (fwupd_release_get_remote_id(rel) == NULL) {
				g_debug("%s has no RemoteID", fwupd_device_get_id(dev));
				continue;
			}
			if (g_strcmp0(fwupd_release_get_remote_id(rel),
				      fwupd_remote_get_id(remote)) != 0) {
				g_debug("%s has incorrect RemoteID: %s != %s",
					fwupd_device_get_id(dev),
					fwupd_release_get_remote_id(rel),
					fwupd_remote_get_id(remote));
				continue;
			}
		}

		json_builder_begin_object(builder);
		fwupd_client_build_report_history_device(builder, dev);
		json_builder_end_object(builder);
		cnt++;
	}
	json_builder_end_array(builder);
	json_builder_end_object(builder);

	/* nothing to do */
	if (cnt == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "no history to upload");
		return NULL;
	}

	/* export as a string */
	json_root = json_builder_get_root(builder);
	json_generator_set_pretty(json_generator, TRUE);
	json_generator_set_root(json_generator, json_root);
	data = json_generator_to_data(json_generator, NULL);
	if (data == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to convert to JSON string");
		return NULL;
	}
	return g_steal_pointer(&data);
}

/**
 * fwupd_client_build_report_security:
 * @self: a #FwupdClient
 * @attrs: (element-type FwupdSecurityAttr): attributes
 * @metadata: (element-type utf8 utf8): attributes
 * @error: (nullable): optional return location for an error
 *
 * Builds a JSON security report.
 *
 * You must have called [method@Client.connect_async] on @self before using
 * this method.
 *
 * Returns: a string, or %NULL on error
 *
 * Since: 2.0.0
 **/
gchar *
fwupd_client_build_report_security(FwupdClient *self,
				   GPtrArray *attrs,
				   GHashTable *metadata,
				   GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	guint cnt = 0;
	g_autofree gchar *data = NULL;
	g_autoptr(JsonBuilder) builder = json_builder_new();
	g_autoptr(JsonGenerator) json_generator = json_generator_new();
	g_autoptr(JsonNode) json_root = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(attrs != NULL, NULL);
	g_return_val_if_fail(metadata != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* create header */
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "ReportType");
	json_builder_add_string_value(builder, "hsi");
	json_builder_set_member_name(builder, "ReportVersion");
	json_builder_add_int_value(builder, 2);
	if (priv->host_machine_id != NULL) {
		json_builder_set_member_name(builder, "MachineId");
		json_builder_add_string_value(builder, priv->host_machine_id);
	}

	/* this is system metadata not stored in the database */
	if (g_hash_table_size(metadata) > 0 || fwupd_client_get_host_security_id(self) != NULL) {
		json_builder_set_member_name(builder, "Metadata");
		json_builder_begin_object(builder);
		fwupd_client_build_report_metadata(builder, metadata);
		json_builder_set_member_name(builder, "HostSecurityId");
		json_builder_add_string_value(builder, fwupd_client_get_host_security_id(self));
		json_builder_end_object(builder);
	}

	/* attrs */
	json_builder_set_member_name(builder, "SecurityAttributes");
	json_builder_begin_array(builder);
	for (guint i = 0; i < attrs->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index(attrs, i);
		json_builder_begin_object(builder);
		fwupd_codec_to_json(FWUPD_CODEC(attr), builder, FWUPD_CODEC_FLAG_TRUSTED);
		json_builder_end_object(builder);
		cnt++;
	}
	json_builder_end_array(builder);
	json_builder_end_object(builder);

	/* nothing to do */
	if (cnt == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "no attributes to upload");
		return NULL;
	}

	/* export as a string */
	json_root = json_builder_get_root(builder);
	json_generator_set_pretty(json_generator, TRUE);
	json_generator_set_root(json_generator, json_root);
	data = json_generator_to_data(json_generator, NULL);
	if (data == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to convert to JSON string");
		return NULL;
	}
	return g_steal_pointer(&data);
}

static void
fwupd_client_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FwupdClient *self = FWUPD_CLIENT(object);
	FwupdClientPrivate *priv = GET_PRIVATE(self);

	switch (prop_id) {
	case PROP_STATUS:
		g_value_set_uint(value, priv->status);
		break;
	case PROP_TAINTED:
		g_value_set_boolean(value, priv->tainted);
		break;
	case PROP_PERCENTAGE:
		g_value_set_uint(value, priv->percentage);
		break;
	case PROP_DAEMON_VERSION:
		g_value_set_string(value, priv->daemon_version);
		break;
	case PROP_HOST_BKC:
		g_value_set_string(value, priv->host_bkc);
		break;
	case PROP_HOST_VENDOR:
		g_value_set_string(value, priv->host_vendor);
		break;
	case PROP_HOST_PRODUCT:
		g_value_set_string(value, priv->host_product);
		break;
	case PROP_HOST_MACHINE_ID:
		g_value_set_string(value, priv->host_machine_id);
		break;
	case PROP_HOST_SECURITY_ID:
		g_value_set_string(value, priv->host_security_id);
		break;
	case PROP_ONLY_TRUSTED:
		g_value_set_boolean(value, priv->only_trusted);
		break;
	case PROP_INTERACTIVE:
		g_value_set_boolean(value, priv->interactive);
		break;
	case PROP_BATTERY_LEVEL:
		g_value_set_uint(value, priv->battery_level);
		break;
	case PROP_BATTERY_THRESHOLD:
		g_value_set_uint(value, priv->battery_threshold);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fwupd_client_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FwupdClient *self = FWUPD_CLIENT(object);
	FwupdClientPrivate *priv = GET_PRIVATE(self);

	switch (prop_id) {
	case PROP_STATUS:
		priv->status = g_value_get_uint(value);
		break;
	case PROP_PERCENTAGE:
		priv->percentage = g_value_get_uint(value);
		break;
	case PROP_BATTERY_LEVEL:
		fwupd_client_set_battery_level(self, g_value_get_uint(value));
		break;
	case PROP_BATTERY_THRESHOLD:
		fwupd_client_set_battery_threshold(self, g_value_get_uint(value));
		break;
	case PROP_HOST_BKC:
		fwupd_client_set_host_bkc(self, g_value_get_string(value));
		break;
	case PROP_HOST_VENDOR:
		fwupd_client_set_host_vendor(self, g_value_get_string(value));
		break;
	case PROP_HOST_PRODUCT:
		fwupd_client_set_host_product(self, g_value_get_string(value));
		break;
	case PROP_HOST_MACHINE_ID:
		fwupd_client_set_host_machine_id(self, g_value_get_string(value));
		break;
	case PROP_HOST_SECURITY_ID:
		fwupd_client_set_host_security_id(self, g_value_get_string(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fwupd_client_class_init(FwupdClientClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fwupd_client_finalize;
	object_class->get_property = fwupd_client_get_property;
	object_class->set_property = fwupd_client_set_property;

	/**
	 * FwupdClient::changed:
	 * @self: the #FwupdClient instance that emitted the signal
	 *
	 * The ::changed signal is emitted when the daemon internal has
	 * changed, for instance when a device has been added or removed.
	 *
	 * Since: 0.7.0
	 **/
	signals[SIGNAL_CHANGED] = g_signal_new("changed",
					       G_TYPE_FROM_CLASS(object_class),
					       G_SIGNAL_RUN_LAST,
					       G_STRUCT_OFFSET(FwupdClientClass, changed),
					       NULL,
					       NULL,
					       g_cclosure_marshal_VOID__VOID,
					       G_TYPE_NONE,
					       0);

	/**
	 * FwupdClient::state-changed:
	 * @self: the #FwupdClient instance that emitted the signal
	 * @status: the #FwupdStatus
	 *
	 * The ::state-changed signal is emitted when the daemon status has
	 * changed, e.g. going from %FWUPD_STATUS_IDLE to %FWUPD_STATUS_DEVICE_WRITE.
	 *
	 * Since: 0.7.0
	 **/
	signals[SIGNAL_STATUS_CHANGED] =
	    g_signal_new("status-changed",
			 G_TYPE_FROM_CLASS(object_class),
			 G_SIGNAL_RUN_LAST,
			 G_STRUCT_OFFSET(FwupdClientClass, status_changed),
			 NULL,
			 NULL,
			 g_cclosure_marshal_VOID__UINT,
			 G_TYPE_NONE,
			 1,
			 G_TYPE_UINT);

	/**
	 * FwupdClient::device-added:
	 * @self: the #FwupdClient instance that emitted the signal
	 * @result: the #FwupdDevice
	 *
	 * The ::device-added signal is emitted when a device has been
	 * added.
	 *
	 * Since: 0.7.1
	 **/
	signals[SIGNAL_DEVICE_ADDED] = g_signal_new("device-added",
						    G_TYPE_FROM_CLASS(object_class),
						    G_SIGNAL_RUN_LAST,
						    G_STRUCT_OFFSET(FwupdClientClass, device_added),
						    NULL,
						    NULL,
						    g_cclosure_marshal_generic,
						    G_TYPE_NONE,
						    1,
						    FWUPD_TYPE_DEVICE);

	/**
	 * FwupdClient::device-removed:
	 * @self: the #FwupdClient instance that emitted the signal
	 * @result: the #FwupdDevice
	 *
	 * The ::device-removed signal is emitted when a device has been
	 * removed.
	 *
	 * Since: 0.7.1
	 **/
	signals[SIGNAL_DEVICE_REMOVED] =
	    g_signal_new("device-removed",
			 G_TYPE_FROM_CLASS(object_class),
			 G_SIGNAL_RUN_LAST,
			 G_STRUCT_OFFSET(FwupdClientClass, device_removed),
			 NULL,
			 NULL,
			 g_cclosure_marshal_generic,
			 G_TYPE_NONE,
			 1,
			 FWUPD_TYPE_DEVICE);

	/**
	 * FwupdClient::device-changed:
	 * @self: the #FwupdClient instance that emitted the signal
	 * @result: the #FwupdDevice
	 *
	 * The ::device-changed signal is emitted when a device has been
	 * changed in some way, e.g. the version number is updated.
	 *
	 * Since: 0.7.1
	 **/
	signals[SIGNAL_DEVICE_CHANGED] =
	    g_signal_new("device-changed",
			 G_TYPE_FROM_CLASS(object_class),
			 G_SIGNAL_RUN_LAST,
			 G_STRUCT_OFFSET(FwupdClientClass, device_changed),
			 NULL,
			 NULL,
			 g_cclosure_marshal_generic,
			 G_TYPE_NONE,
			 1,
			 FWUPD_TYPE_DEVICE);

	/**
	 * FwupdClient::device-request:
	 * @self: the #FwupdClient instance that emitted the signal
	 * @msg: the #FwupdRequest
	 *
	 * The ::device-request signal is emitted when a device has been
	 * emitted some kind of event, e.g. a manual action is required.
	 *
	 * Since: 1.6.2
	 **/
	signals[SIGNAL_DEVICE_REQUEST] =
	    g_signal_new("device-request",
			 G_TYPE_FROM_CLASS(object_class),
			 G_SIGNAL_RUN_LAST,
			 G_STRUCT_OFFSET(FwupdClientClass, device_request),
			 NULL,
			 NULL,
			 g_cclosure_marshal_generic,
			 G_TYPE_NONE,
			 1,
			 FWUPD_TYPE_REQUEST);

	/**
	 * FwupdClient:status:
	 *
	 * The last-reported status of the daemon.
	 *
	 * Since: 0.7.0
	 */
	pspec = g_param_spec_uint("status",
				  NULL,
				  NULL,
				  0,
				  FWUPD_STATUS_LAST,
				  FWUPD_STATUS_UNKNOWN,
				  G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_STATUS, pspec);

	/**
	 * FwupdClient:tainted:
	 *
	 * If the daemon is tainted by 3rd party code.
	 *
	 * Since: 1.2.4
	 */
	pspec = g_param_spec_boolean("tainted",
				     NULL,
				     NULL,
				     FALSE,
				     G_PARAM_READABLE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_TAINTED, pspec);

	/**
	 * FwupdClient:interactive:
	 *
	 * If the daemon is running in an interactive terminal
	 *
	 * Since: 1.3.4
	 */
	pspec = g_param_spec_boolean("interactive",
				     NULL,
				     NULL,
				     FALSE,
				     G_PARAM_READABLE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_INTERACTIVE, pspec);

	/**
	 * FwupdClient:percentage:
	 *
	 * The last-reported percentage of the daemon.
	 *
	 * Since: 0.7.3
	 */
	pspec = g_param_spec_uint("percentage",
				  NULL,
				  NULL,
				  0,
				  100,
				  0,
				  G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_PERCENTAGE, pspec);

	/**
	 * FwupdClient:daemon-version:
	 *
	 * The daemon version number.
	 *
	 * Since: 0.9.6
	 */
	pspec = g_param_spec_string("daemon-version",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READABLE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_DAEMON_VERSION, pspec);

	/**
	 * FwupdClient:host-bkc:
	 *
	 * The host best known configuration.
	 *
	 * Since: 1.7.3
	 */
	pspec = g_param_spec_string("host-bkc",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_HOST_BKC, pspec);

	/**
	 * FwupdClient:host-vendor:
	 *
	 * The host vendor string
	 *
	 * Since: 1.8.2
	 */
	pspec = g_param_spec_string("host-vendor",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_HOST_VENDOR, pspec);

	/**
	 * FwupdClient:host-product:
	 *
	 * The host product string
	 *
	 * Since: 1.3.1
	 */
	pspec = g_param_spec_string("host-product",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_HOST_PRODUCT, pspec);

	/**
	 * FwupdClient:host-machine-id:
	 *
	 * The host machine-id string
	 *
	 * Since: 1.3.2
	 */
	pspec = g_param_spec_string("host-machine-id",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_HOST_MACHINE_ID, pspec);

	/**
	 * FwupdClient:host-security-id:
	 *
	 * The host machine-id string
	 *
	 * Since: 1.5.0
	 */
	pspec = g_param_spec_string("host-security-id",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_HOST_SECURITY_ID, pspec);

	/**
	 * FwupdClient:only-trusted:
	 *
	 * If the daemon is verifying signatures from a trusted authority.
	 *
	 * Since: 1.8.0
	 */
	pspec = g_param_spec_boolean("only-trusted",
				     NULL,
				     NULL,
				     TRUE,
				     G_PARAM_READABLE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_ONLY_TRUSTED, pspec);

	/**
	 * FwupdClient:battery-level:
	 *
	 * The system battery level in percent.
	 *
	 * Since: 1.8.1
	 */
	pspec = g_param_spec_uint("battery-level",
				  NULL,
				  NULL,
				  0,
				  FWUPD_BATTERY_LEVEL_INVALID,
				  FWUPD_BATTERY_LEVEL_INVALID,
				  G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_BATTERY_LEVEL, pspec);

	/**
	 * FwupdClient:battery-threshold:
	 *
	 * The system battery threshold in percent.
	 *
	 * Since: 1.8.1
	 */
	pspec = g_param_spec_uint("battery-threshold",
				  NULL,
				  NULL,
				  0,
				  FWUPD_BATTERY_LEVEL_INVALID,
				  FWUPD_BATTERY_LEVEL_INVALID,
				  G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_BATTERY_THRESHOLD, pspec);
}

static void
fwupd_client_init(FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE(self);
	g_mutex_init(&priv->proxy_mutex);
	g_mutex_init(&priv->idle_mutex);
	priv->idle_sources =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fwupd_client_context_helper_free);
	priv->proxy_resolver = g_proxy_resolver_get_default();
	priv->hints = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	priv->battery_level = FWUPD_BATTERY_LEVEL_INVALID;
	priv->battery_threshold = FWUPD_BATTERY_LEVEL_INVALID;
	priv->immediate_requests =
	    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_object_unref);

	/* we get this one for free */
	fwupd_client_add_hint(self, "locale", g_getenv("LANG"));
}

static void
fwupd_client_finalize(GObject *object)
{
	FwupdClient *self = FWUPD_CLIENT(object);
	FwupdClientPrivate *priv = GET_PRIVATE(self);

	g_clear_pointer(&priv->main_ctx, g_main_context_unref);
	g_free(priv->user_agent);
	g_free(priv->package_name);
	g_free(priv->package_version);
	g_free(priv->daemon_version);
	g_free(priv->host_bkc);
	g_free(priv->host_vendor);
	g_free(priv->host_product);
	g_free(priv->host_machine_id);
	g_free(priv->host_security_id);
	g_hash_table_unref(priv->hints);
	g_hash_table_unref(priv->immediate_requests);
	g_mutex_clear(&priv->idle_mutex);
	if (priv->idle_id != 0)
		g_source_remove(priv->idle_id);
	g_ptr_array_unref(priv->idle_sources);
	g_mutex_clear(&priv->proxy_mutex);
	if (priv->proxy != NULL)
		g_object_unref(priv->proxy);

	G_OBJECT_CLASS(fwupd_client_parent_class)->finalize(object);
}

/**
 * fwupd_client_new:
 *
 * Creates a new client.
 *
 * Returns: a new #FwupdClient
 *
 * Since: 0.7.0
 **/
FwupdClient *
fwupd_client_new(void)
{
	FwupdClient *self;
	self = g_object_new(FWUPD_TYPE_CLIENT, NULL);
	return FWUPD_CLIENT(self);
}
