/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <glib-object.h>
#include <gio/gio.h>
#include <gmodule.h>
#include <curl/curl.h>
#ifdef HAVE_GIO_UNIX
#include <gio/gunixfdlist.h>
#endif

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "fwupd-client-private.h"
#include "fwupd-client-sync.h"
#include "fwupd-common-private.h"
#include "fwupd-deprecated.h"
#include "fwupd-enums.h"
#include "fwupd-error.h"
#include "fwupd-device-private.h"
#include "fwupd-plugin-private.h"
#include "fwupd-security-attr-private.h"
#include "fwupd-release-private.h"
#include "fwupd-remote-private.h"

typedef GObject		*(*FwupdClientObjectNewFunc)	(void);

/**
 * SECTION:fwupd-client
 * @short_description: a way of interfacing with the daemon
 *
 * An object that allows client code to call the daemon methods synchronously.
 *
 * See also: #FwupdDevice
 */

static void fwupd_client_finalize	 (GObject *object);

typedef struct {
	GMainContext			*main_ctx;
	FwupdStatus			 status;
	gboolean			 tainted;
	gboolean			 interactive;
	guint				 percentage;
	gchar				*daemon_version;
	gchar				*host_product;
	gchar				*host_machine_id;
	gchar				*host_security_id;
	GDBusConnection			*conn;
	GDBusProxy			*proxy;
	gchar				*user_agent;
#ifdef SOUP_SESSION_COMPAT
	GObject				*soup_session;
	GModule				*soup_module;	/* we leak this */
#endif
} FwupdClientPrivate;

typedef struct {
	CURL				*curl;
#ifdef HAVE_LIBCURL_7_56_0
	curl_mime			*mime;
#endif
	struct curl_slist		*headers;
} FwupdCurlHelper;

enum {
	SIGNAL_CHANGED,
	SIGNAL_STATUS_CHANGED,
	SIGNAL_DEVICE_ADDED,
	SIGNAL_DEVICE_REMOVED,
	SIGNAL_DEVICE_CHANGED,
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_STATUS,
	PROP_PERCENTAGE,
	PROP_DAEMON_VERSION,
	PROP_TAINTED,
	PROP_SOUP_SESSION, /* compat ABI, do not use! */
	PROP_HOST_PRODUCT,
	PROP_HOST_MACHINE_ID,
	PROP_HOST_SECURITY_ID,
	PROP_INTERACTIVE,
	PROP_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (FwupdClient, fwupd_client, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fwupd_client_get_instance_private (o))

#ifdef HAVE_LIBCURL_7_62_0
G_DEFINE_AUTOPTR_CLEANUP_FUNC(CURLU, curl_url_cleanup)
#endif

static void
fwupd_client_curl_helper_free (FwupdCurlHelper *helper)
{
	if (helper->curl != NULL)
		curl_easy_cleanup (helper->curl);
#ifdef HAVE_LIBCURL_7_56_0
	if (helper->mime != NULL)
		curl_mime_free (helper->mime);
#endif
	if (helper->headers != NULL)
		curl_slist_free_all (helper->headers);
	g_free (helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FwupdCurlHelper, fwupd_client_curl_helper_free)

static void
fwupd_client_set_host_product (FwupdClient *self, const gchar *host_product)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_free (priv->host_product);
	priv->host_product = g_strdup (host_product);
	g_object_notify (G_OBJECT (self), "host-product");
}

static void
fwupd_client_set_host_machine_id (FwupdClient *self, const gchar *host_machine_id)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_free (priv->host_machine_id);
	priv->host_machine_id = g_strdup (host_machine_id);
	g_object_notify (G_OBJECT (self), "host-machine-id");
}

static void
fwupd_client_set_host_security_id (FwupdClient *self, const gchar *host_security_id)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_free (priv->host_security_id);
	priv->host_security_id = g_strdup (host_security_id);
	g_object_notify (G_OBJECT (self), "host-security-id");
}

static void
fwupd_client_set_daemon_version (FwupdClient *self, const gchar *daemon_version)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_free (priv->daemon_version);
	priv->daemon_version = g_strdup (daemon_version);
	g_object_notify (G_OBJECT (self), "daemon-version");
}

static void
fwupd_client_set_status (FwupdClient *self, FwupdStatus status)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	if (priv->status == status)
		return;
	priv->status = status;
	g_debug ("Emitting ::status-changed() [%s]",
		 fwupd_status_to_string (priv->status));
	g_signal_emit (self, signals[SIGNAL_STATUS_CHANGED], 0, priv->status);
	g_object_notify (G_OBJECT (self), "status");
}

static void
fwupd_client_set_percentage (FwupdClient *self, guint percentage)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	if (priv->percentage == percentage)
		return;
	priv->percentage = percentage;
	g_object_notify (G_OBJECT (self), "percentage");
}

static void
fwupd_client_properties_changed_cb (GDBusProxy *proxy,
				    GVariant *changed_properties,
				    GStrv invalidated_properties,
				    FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GVariantDict) dict = NULL;

	/* print to the console */
	dict = g_variant_dict_new (changed_properties);
	if (g_variant_dict_contains (dict, "Status")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property (proxy, "Status");
		if (val != NULL)
			fwupd_client_set_status (self, g_variant_get_uint32 (val));
	}
	if (g_variant_dict_contains (dict, "Tainted")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property (proxy, "Tainted");
		if (val != NULL) {
			priv->tainted = g_variant_get_boolean (val);
			g_object_notify (G_OBJECT (self), "tainted");
		}
	}
	if (g_variant_dict_contains (dict, "Interactive")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property (proxy, "Interactive");
		if (val != NULL) {
			priv->interactive = g_variant_get_boolean (val);
			g_object_notify (G_OBJECT (self), "interactive");
		}
	}
	if (g_variant_dict_contains (dict, "Percentage")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property (proxy, "Percentage");
		if (val != NULL)
			fwupd_client_set_percentage (self, g_variant_get_uint32 (val));
	}
	if (g_variant_dict_contains (dict, "DaemonVersion")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property (proxy, "DaemonVersion");
		if (val != NULL)
			fwupd_client_set_daemon_version (self, g_variant_get_string (val, NULL));
	}
	if (g_variant_dict_contains (dict, "HostProduct")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property (proxy, "HostProduct");
		if (val != NULL)
			fwupd_client_set_host_product (self, g_variant_get_string (val, NULL));
	}
	if (g_variant_dict_contains (dict, "HostMachineId")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property (proxy, "HostMachineId");
		if (val != NULL)
			fwupd_client_set_host_machine_id (self, g_variant_get_string (val, NULL));
	}
	if (g_variant_dict_contains (dict, "HostSecurityId")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property (proxy, "HostSecurityId");
		if (val != NULL)
			fwupd_client_set_host_security_id (self, g_variant_get_string (val, NULL));
	}
}

static void
fwupd_client_signal_cb (GDBusProxy *proxy,
			const gchar *sender_name,
			const gchar *signal_name,
			GVariant *parameters,
			FwupdClient *self)
{
	g_autoptr(FwupdDevice) dev = NULL;
	if (g_strcmp0 (signal_name, "Changed") == 0) {
		g_debug ("Emitting ::changed()");
		g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
		return;
	}
	if (g_strcmp0 (signal_name, "DeviceAdded") == 0) {
		dev = fwupd_device_from_variant (parameters);
		g_debug ("Emitting ::device-added(%s)",
			 fwupd_device_get_id (dev));
		g_signal_emit (self, signals[SIGNAL_DEVICE_ADDED], 0, dev);
		return;
	}
	if (g_strcmp0 (signal_name, "DeviceRemoved") == 0) {
		dev = fwupd_device_from_variant (parameters);
		g_signal_emit (self, signals[SIGNAL_DEVICE_REMOVED], 0, dev);
		g_debug ("Emitting ::device-removed(%s)",
			 fwupd_device_get_id (dev));
		return;
	}
	if (g_strcmp0 (signal_name, "DeviceChanged") == 0) {
		dev = fwupd_device_from_variant (parameters);
		g_signal_emit (self, signals[SIGNAL_DEVICE_CHANGED], 0, dev);
		g_debug ("Emitting ::device-changed(%s)",
			 fwupd_device_get_id (dev));
		return;
	}
	g_debug ("Unknown signal name '%s' from %s", signal_name, sender_name);
}

/**
 * fwupd_client_get_main_context:
 * @self: A #FwupdClient
 *
 * Gets the internal #GMainContext to use for synchronous methods.
 * By default the value is set to the value of g_main_context_ref_thread_default()
 *
 * Return value: (transfer full): the #GMainContext
 *
 * Since: 1.5.3
 **/
GMainContext *
fwupd_client_get_main_context (FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	if (priv->main_ctx != NULL)
		return g_main_context_ref (priv->main_ctx);
	return g_main_context_ref_thread_default ();
}

/**
 * fwupd_client_set_main_context:
 * @self: A #FwupdClient
 * @main_ctx: #GMainContext
 *
 * Sets the internal #GMainContext to use for synchronous methods.
 *
 * Since: 1.5.3
 **/
void
fwupd_client_set_main_context (FwupdClient *self, GMainContext *main_ctx)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	if (main_ctx != priv->main_ctx)
		g_main_context_unref (priv->main_ctx);
	priv->main_ctx = g_main_context_ref (main_ctx);
}

/**
 * fwupd_client_ensure_networking:
 * @self: A #FwupdClient
 * @error: the #GError, or %NULL
 *
 * Sets up the client networking support ready for use. Most other download and
 * upload methods call this automatically, and do you only need to call this if
 * the session is being used outside the #FwupdClient.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.5
 **/
gboolean
fwupd_client_ensure_networking (FwupdClient *self, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);

	/* check the user agent is sane */
	if (priv->user_agent == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "user agent unset");
		return FALSE;
	}
	if (g_strstr_len (priv->user_agent, -1, "fwupd/") == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "user agent unsuitable; fwupd version required");
		return FALSE;
	}
#ifdef SOUP_SESSION_COMPAT
	if (priv->soup_session != NULL) {
		g_object_set (priv->soup_session,
			      "user-agent", priv->user_agent,
			      NULL);
	}
#endif
	return TRUE;
}

static int
fwupd_client_progress_callback_cb (void *clientp,
				   curl_off_t dltotal,
				   curl_off_t dlnow,
				   curl_off_t ultotal,
				   curl_off_t ulnow)
{
	FwupdClient *self = FWUPD_CLIENT (clientp);

	/* calculate percentage */
	if (dltotal > 0 && dlnow >= 0 && dlnow <= dltotal) {
		guint percentage = (guint) ((100 * dlnow) / dltotal);
		g_debug ("download progress: %u%%", percentage);
		fwupd_client_set_percentage (self, percentage);
	} else if (ultotal > 0 && ulnow >= 0 && ulnow <= ultotal) {
		guint percentage = (guint) ((100 * ulnow) / ultotal);
		g_debug ("upload progress: %u%%", percentage);
		fwupd_client_set_percentage (self, percentage);
	}

	return 0;
}

static FwupdCurlHelper *
fwupd_client_curl_new (FwupdClient *self, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	const gchar *http_proxy;
	g_autoptr(FwupdCurlHelper) helper = g_new0 (FwupdCurlHelper, 1);

	/* check the user agent is sane */
	if (!fwupd_client_ensure_networking (self, error))
		return NULL;

	/* create the session */
	helper->curl = curl_easy_init ();
	if (helper->curl == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "failed to setup networking");
		return NULL;
	}
	if (g_getenv ("FWUPD_CURL_VERBOSE") != NULL)
		curl_easy_setopt (helper->curl, CURLOPT_VERBOSE, 1L);
	curl_easy_setopt (helper->curl, CURLOPT_XFERINFOFUNCTION, fwupd_client_progress_callback_cb);
	curl_easy_setopt (helper->curl, CURLOPT_XFERINFODATA, self);
	curl_easy_setopt (helper->curl, CURLOPT_USERAGENT, priv->user_agent);
	curl_easy_setopt (helper->curl, CURLOPT_CONNECTTIMEOUT, 60L);

	/* relax the SSL checks for broken corporate proxies */
	if (g_getenv ("DISABLE_SSL_STRICT") != NULL)
		curl_easy_setopt (helper->curl, CURLOPT_SSL_VERIFYPEER, 0L);

	/* set the proxy */
	http_proxy = g_getenv ("https_proxy");
	if (http_proxy == NULL)
		http_proxy = g_getenv ("HTTPS_PROXY");
	if (http_proxy == NULL)
		http_proxy = g_getenv ("http_proxy");
	if (http_proxy == NULL)
		http_proxy = g_getenv ("HTTP_PROXY");
	if (http_proxy != NULL && strlen (http_proxy) > 0)
		curl_easy_setopt (helper->curl, CURLOPT_PROXY, http_proxy);

	/* this disables the double-compression of the firmware.xml.gz file */
	curl_easy_setopt (helper->curl, CURLOPT_HTTP_CONTENT_DECODING, 0L);
	return g_steal_pointer (&helper);
}

static void
fwupd_client_connect_get_proxy_cb (GObject *source,
				   GAsyncResult *res,
				   gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	FwupdClient *self = g_task_get_source_object (task);
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;
	g_autoptr(GVariant) val2 = NULL;

	priv->proxy = g_dbus_proxy_new_finish (res, &error);
	if (priv->proxy == NULL) {
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}
	g_signal_connect (priv->proxy, "g-properties-changed",
			  G_CALLBACK (fwupd_client_properties_changed_cb), self);
	g_signal_connect (priv->proxy, "g-signal",
			  G_CALLBACK (fwupd_client_signal_cb), self);
	val = g_dbus_proxy_get_cached_property (priv->proxy, "DaemonVersion");
	if (val != NULL)
		fwupd_client_set_daemon_version (self, g_variant_get_string (val, NULL));
	val2 = g_dbus_proxy_get_cached_property (priv->proxy, "Tainted");
	if (val2 != NULL)
		priv->tainted = g_variant_get_boolean (val2);
	val2 = g_dbus_proxy_get_cached_property (priv->proxy, "Interactive");
	if (val2 != NULL)
		priv->interactive = g_variant_get_boolean (val2);
	val = g_dbus_proxy_get_cached_property (priv->proxy, "HostProduct");
	if (val != NULL)
		fwupd_client_set_host_product (self, g_variant_get_string (val, NULL));
	val = g_dbus_proxy_get_cached_property (priv->proxy, "HostMachineId");
	if (val != NULL)
		fwupd_client_set_host_machine_id (self, g_variant_get_string (val, NULL));
	val = g_dbus_proxy_get_cached_property (priv->proxy, "HostSecurityId");
	if (val != NULL)
		fwupd_client_set_host_security_id (self, g_variant_get_string (val, NULL));

	/* success */
	g_task_return_boolean (task, TRUE);
}

static void
fwupd_client_connect_get_bus_cb (GObject *source,
				 GAsyncResult *res,
				 gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	FwupdClient *self = g_task_get_source_object (task);
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GError) error = NULL;

	priv->conn = g_bus_get_finish (res, &error);
	if (priv->conn == NULL) {
		g_prefix_error (&error, "Failed to connect to system D-Bus: ");
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}
	g_dbus_proxy_new (priv->conn,
			  G_DBUS_PROXY_FLAGS_NONE,
			  NULL,
			  FWUPD_DBUS_SERVICE,
			  FWUPD_DBUS_PATH,
			  FWUPD_DBUS_INTERFACE,
			  g_task_get_cancellable (task),
			  fwupd_client_connect_get_proxy_cb,
			  g_object_ref (task));
}

/**
 * fwupd_client_connect_async:
 * @self: A #FwupdClient
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Sets up the client ready for use. This is probably the first method you call
 * when wanting to use libfwupd in an asynchronous manner.
 *
 * Other methods such as fwupd_client_get_devices_async() should only be called
 * after fwupd_client_connect_finish() has been called without an error.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_connect_async (FwupdClient *self, GCancellable *cancellable,
			    GAsyncReadyCallback callback, gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = g_task_new (self, cancellable, callback, callback_data);

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* nothing to do */
	if (priv->proxy != NULL) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	g_bus_get (G_BUS_TYPE_SYSTEM, cancellable,
		   fwupd_client_connect_get_bus_cb,
		   g_steal_pointer (&task));

}

/**
 * fwupd_client_connect_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_connect_async().
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_connect_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean (G_TASK(res), error);
}

static void
fwupd_client_fixup_dbus_error (GError *error)
{
	g_autofree gchar *name = NULL;

	g_return_if_fail (error != NULL);

	/* is a remote error? */
	if (!g_dbus_error_is_remote_error (error))
		return;

	/* parse the remote error */
	name = g_dbus_error_get_remote_error (error);
	if (g_str_has_prefix (name, FWUPD_DBUS_INTERFACE)) {
		error->domain = FWUPD_ERROR;
		error->code = fwupd_error_from_string (name);
	} else if (g_error_matches (error,
				    G_DBUS_ERROR,
				    G_DBUS_ERROR_SERVICE_UNKNOWN)) {
		error->domain = FWUPD_ERROR;
		error->code = FWUPD_ERROR_NOT_SUPPORTED;
	} else if (g_error_matches (error,
				    G_IO_ERROR,
				    G_IO_ERROR_DBUS_ERROR)) {
		error->domain = FWUPD_ERROR;
		error->code = FWUPD_ERROR_NOT_SUPPORTED;
	} else {
		error->domain = FWUPD_ERROR;
		error->code = FWUPD_ERROR_INTERNAL;
	}
	g_dbus_error_strip_remote_error (error);
}

static void
fwupd_client_get_host_security_attrs_cb (GObject *source,
					 GAsyncResult *res,
					 gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_pointer (task,
			       fwupd_security_attr_array_from_variant (val),
			       (GDestroyNotify) g_ptr_array_unref);
}

/**
 * fwupd_client_get_host_security_attrs_async:
 * @self: A #FwupdClient
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets all the host security attributes from the daemon.
 *
 * You must have called fwupd_client_connect_async() on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_host_security_attrs_async (FwupdClient *self,
					    GCancellable *cancellable,
					    GAsyncReadyCallback callback,
					    gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_dbus_proxy_call (priv->proxy, "GetHostSecurityAttrs",
			   NULL,
			   G_DBUS_CALL_FLAGS_NONE,
			   -1, cancellable,
			   fwupd_client_get_host_security_attrs_cb,
			   g_steal_pointer (&task));
}

/**
 * fwupd_client_get_host_security_attrs_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_get_host_security_attrs_async().
 *
 * Returns: (element-type FwupdSecurityAttr) (transfer container): attributes
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_host_security_attrs_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), NULL);
	g_return_val_if_fail (g_task_is_valid (res, self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer (G_TASK(res), error);
}

static GHashTable *
fwupd_report_metadata_hash_from_variant (GVariant *value)
{
	GHashTable *hash;
	gsize sz;
	g_autoptr(GVariant) untuple = NULL;

	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	untuple = g_variant_get_child_value (value, 0);
	sz = g_variant_n_children (untuple);
	for (guint i = 0; i < sz; i++) {
		g_autoptr(GVariant) data = NULL;
		const gchar *key = NULL;
		const gchar *val = NULL;
		data = g_variant_get_child_value (untuple, i);
		g_variant_get (data, "{&s&s}", &key, &val);
		g_hash_table_insert (hash, g_strdup (key), g_strdup (val));
	}
	return hash;
}

static void
fwupd_client_get_report_metadata_cb (GObject *source,
				     GAsyncResult *res,
				     gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_pointer (task,
			       fwupd_report_metadata_hash_from_variant (val),
			       (GDestroyNotify) g_hash_table_unref);
}

/**
 * fwupd_client_get_report_metadata_async:
 * @self: A #FwupdClient
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets all the report metadata from the daemon.
 *
 * You must have called fwupd_client_connect_async() on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_report_metadata_async (FwupdClient *self,
					GCancellable *cancellable,
					GAsyncReadyCallback callback,
					gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_dbus_proxy_call (priv->proxy, "GetReportMetadata",
			   NULL,
			   G_DBUS_CALL_FLAGS_NONE,
			   -1, cancellable,
			   fwupd_client_get_report_metadata_cb,
			   g_steal_pointer (&task));
}

/**
 * fwupd_client_get_report_metadata_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_get_report_metadata_async().
 *
 * Returns: (transfer container): attributes
 *
 * Since: 1.5.0
 **/
GHashTable *
fwupd_client_get_report_metadata_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), NULL);
	g_return_val_if_fail (g_task_is_valid (res, self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer (G_TASK(res), error);
}

static void
fwupd_client_get_devices_cb (GObject *source,
			     GAsyncResult *res,
			     gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_pointer (task,
			       fwupd_device_array_from_variant (val),
			       (GDestroyNotify) g_ptr_array_unref);
}

/**
 * fwupd_client_get_devices_async:
 * @self: A #FwupdClient
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets all the devices registered with the daemon.
 *
 * You must have called fwupd_client_connect_async() on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_devices_async (FwupdClient *self, GCancellable *cancellable,
				GAsyncReadyCallback callback, gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_dbus_proxy_call (priv->proxy, "GetDevices",
			   NULL, G_DBUS_CALL_FLAGS_NONE,
			   -1, cancellable,
			   fwupd_client_get_devices_cb,
			   g_steal_pointer (&task));
}

/**
 * fwupd_client_get_devices_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_get_devices_async().
 *
 * Returns: (element-type FwupdDevice) (transfer container): results
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_devices_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), NULL);
	g_return_val_if_fail (g_task_is_valid (res, self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer (G_TASK(res), error);
}

static void
fwupd_client_get_plugins_cb (GObject *source,
			     GAsyncResult *res,
			     gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_pointer (task,
			       fwupd_plugin_array_from_variant (val),
			       (GDestroyNotify) g_ptr_array_unref);
}

/**
 * fwupd_client_get_plugins_async:
 * @self: A #FwupdClient
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets all the plugins being used by the daemon.
 *
 * You must have called fwupd_client_connect_async() on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_plugins_async (FwupdClient *self, GCancellable *cancellable,
				GAsyncReadyCallback callback, gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_dbus_proxy_call (priv->proxy, "GetPlugins",
			   NULL, G_DBUS_CALL_FLAGS_NONE,
			   -1, cancellable,
			   fwupd_client_get_plugins_cb,
			   g_steal_pointer (&task));
}

/**
 * fwupd_client_get_plugins_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_get_plugins_async().
 *
 * Returns: (element-type FwupdDevice) (transfer container): results
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_plugins_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), NULL);
	g_return_val_if_fail (g_task_is_valid (res, self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer (G_TASK(res), error);
}

static void
fwupd_client_get_history_cb (GObject *source,
			     GAsyncResult *res,
			     gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_pointer (task,
			       fwupd_device_array_from_variant (val),
			       (GDestroyNotify) g_ptr_array_unref);
}

/**
 * fwupd_client_get_history_async:
 * @self: A #FwupdClient
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets all the history.
 *
 * You must have called fwupd_client_connect_async() on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_history_async (FwupdClient *self, GCancellable *cancellable,
				GAsyncReadyCallback callback, gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_dbus_proxy_call (priv->proxy, "GetHistory",
			   NULL, G_DBUS_CALL_FLAGS_NONE,
			   -1, cancellable,
			   fwupd_client_get_history_cb,
			   g_steal_pointer (&task));
}

/**
 * fwupd_client_get_history_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_get_history_async().
 *
 * Returns: (element-type FwupdDevice) (transfer container): results
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_history_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), NULL);
	g_return_val_if_fail (g_task_is_valid (res, self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer (G_TASK(res), error);
}

static void
fwupd_client_get_device_by_id_cb (GObject *source,
				  GAsyncResult *res,
				  gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	const gchar *device_id = g_task_get_task_data (task);

	devices = fwupd_client_get_devices_finish (FWUPD_CLIENT (source), res, &error);
	if (devices == NULL) {
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* find the device by ID (client side) */
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (devices, i);
		if (g_strcmp0 (fwupd_device_get_id (dev), device_id) == 0) {
			g_task_return_pointer (task,
					       g_object_ref (dev),
					       (GDestroyNotify) g_object_unref);
			return;
		}
	}

	/* failed */
	g_task_return_new_error (task,
				 FWUPD_ERROR,
				 FWUPD_ERROR_NOT_FOUND,
				 "failed to find %s", device_id);
}

/**
 * fwupd_client_get_device_by_id_async:
 * @self: A #FwupdClient
 * @device_id: the device ID
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets a device by it's device ID.
 *
 * You must have called fwupd_client_connect_async() on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_device_by_id_async (FwupdClient *self, const gchar *device_id,
				     GCancellable *cancellable,
				     GAsyncReadyCallback callback,
				     gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (device_id != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_task_set_task_data (task, g_strdup (device_id), g_free);
	fwupd_client_get_devices_async (self, cancellable,
				        fwupd_client_get_device_by_id_cb,
				        g_steal_pointer (&task));
}

/**
 * fwupd_client_get_device_by_id_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_get_device_by_id_async().
 *
 * Returns: (transfer full): a #FwupdDevice, or %NULL for failure
 *
 * Since: 1.5.0
 **/
FwupdDevice *
fwupd_client_get_device_by_id_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), NULL);
	g_return_val_if_fail (g_task_is_valid (res, self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer (G_TASK(res), error);
}

static void
fwupd_client_get_devices_by_guid_cb (GObject *source,
				     GAsyncResult *res,
				     gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) devices_tmp = NULL;
	const gchar *guid = g_task_get_task_data (task);

	/* get all the devices */
	devices_tmp = fwupd_client_get_devices_finish (FWUPD_CLIENT (source), res, &error);
	if (devices_tmp == NULL) {
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* find the devices by GUID (client side) */
	devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (guint i = 0; i < devices_tmp->len; i++) {
		FwupdDevice *dev_tmp = g_ptr_array_index (devices_tmp, i);
		if (fwupd_device_has_guid (dev_tmp, guid))
			g_ptr_array_add (devices, g_object_ref (dev_tmp));
	}

	/* nothing */
	if (devices->len == 0) {
		g_task_return_new_error (task,
					 FWUPD_ERROR,
					 FWUPD_ERROR_NOT_FOUND,
					 "failed to find any device providing %s", guid);
		return;
	}

	/* success */
	g_task_return_pointer (task,
			       g_steal_pointer (&devices),
			       (GDestroyNotify) g_ptr_array_unref);
}

/**
 * fwupd_client_get_devices_by_guid_async:
 * @self: A #FwupdClient
 * @guid: the GUID, e.g. `e22c4520-43dc-5bb3-8245-5787fead9b63`
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets any devices that provide a specific GUID. An error is returned if no
 * devices contains this GUID.
 *
 * You must have called fwupd_client_connect_async() on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_devices_by_guid_async (FwupdClient *self, const gchar *guid,
					GCancellable *cancellable,
					GAsyncReadyCallback callback,
					gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (guid != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_task_set_task_data (task, g_strdup (guid), g_free);
	fwupd_client_get_devices_async (self, cancellable,
					fwupd_client_get_devices_by_guid_cb,
					g_steal_pointer (&task));
}

/**
 * fwupd_client_get_devices_by_guid_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_get_devices_by_guid_async().
 *
 * Returns: (element-type FwupdRelease) (transfer container): results
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_devices_by_guid_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), NULL);
	g_return_val_if_fail (g_task_is_valid (res, self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer (G_TASK(res), error);
}

static void
fwupd_client_get_releases_cb (GObject *source,
			      GAsyncResult *res,
			      gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_pointer (task,
			       fwupd_release_array_from_variant (val),
			       (GDestroyNotify) g_ptr_array_unref);
}

/**
 * fwupd_client_get_releases_async:
 * @self: A #FwupdClient
 * @device_id: the device ID
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets all the releases for a specific device
 *
 * You must have called fwupd_client_connect_async() on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_releases_async (FwupdClient *self, const gchar *device_id,
				 GCancellable *cancellable,
				 GAsyncReadyCallback callback,
				 gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (device_id != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_dbus_proxy_call (priv->proxy, "GetReleases",
			   g_variant_new ("(s)", device_id),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1, cancellable,
			   fwupd_client_get_releases_cb,
			   g_steal_pointer (&task));
}

/**
 * fwupd_client_get_releases_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_get_releases_async().
 *
 * Returns: (element-type FwupdRelease) (transfer container): results
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_releases_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), NULL);
	g_return_val_if_fail (g_task_is_valid (res, self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer (G_TASK(res), error);
}

static void
fwupd_client_get_downgrades_cb (GObject *source,
			        GAsyncResult *res,
			        gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_pointer (task,
			       fwupd_release_array_from_variant (val),
			       (GDestroyNotify) g_ptr_array_unref);
}

/**
 * fwupd_client_get_downgrades_async:
 * @self: A #FwupdClient
 * @device_id: the device ID
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets all the downgrades for a specific device.
 *
 * You must have called fwupd_client_connect_async() on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_downgrades_async (FwupdClient *self, const gchar *device_id,
				   GCancellable *cancellable,
				   GAsyncReadyCallback callback,
				   gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (device_id != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_dbus_proxy_call (priv->proxy, "GetDowngrades",
			   g_variant_new ("(s)", device_id),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1, cancellable,
			   fwupd_client_get_downgrades_cb,
			   g_steal_pointer (&task));
}

/**
 * fwupd_client_get_downgrades_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_get_downgrades_async().
 *
 * Returns: (element-type FwupdRelease) (transfer container): results
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_downgrades_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), NULL);
	g_return_val_if_fail (g_task_is_valid (res, self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer (G_TASK(res), error);
}

static void
fwupd_client_get_upgrades_cb (GObject *source,
			      GAsyncResult *res,
			      gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_pointer (task,
			       fwupd_release_array_from_variant (val),
			       (GDestroyNotify) g_ptr_array_unref);
}

/**
 * fwupd_client_get_upgrades_async:
 * @self: A #FwupdClient
 * @device_id: the device ID
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets all the upgrades for a specific device.
 *
 * You must have called fwupd_client_connect_async() on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_upgrades_async (FwupdClient *self, const gchar *device_id,
				 GCancellable *cancellable,
				 GAsyncReadyCallback callback,
				 gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (device_id != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_dbus_proxy_call (priv->proxy, "GetUpgrades",
			   g_variant_new ("(s)", device_id),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1, cancellable,
			   fwupd_client_get_upgrades_cb,
			   g_steal_pointer (&task));
}

/**
 * fwupd_client_get_upgrades_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_get_upgrades_async().
 *
 * Returns: (element-type FwupdRelease) (transfer container): results
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_upgrades_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), NULL);
	g_return_val_if_fail (g_task_is_valid (res, self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer (G_TASK(res), error);
}

static void
fwupd_client_modify_config_cb (GObject *source,
			       GAsyncResult *res,
			       gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_boolean (task, TRUE);
}

/**
 * fwupd_client_modify_config_async:
 * @self: A #FwupdClient
 * @key: key, e.g. `DisabledPlugins`
 * @value: value, e.g. `*`
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Modifies a daemon config option.
 * The daemon will only respond to this request with proper permissions
 *
 * Since: 1.5.0
 **/
void
fwupd_client_modify_config_async (FwupdClient *self,
				  const gchar *key,
				  const gchar *value,
				  GCancellable *cancellable,
				  GAsyncReadyCallback callback,
				  gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (key != NULL);
	g_return_if_fail (value != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_dbus_proxy_call (priv->proxy, "ModifyConfig",
			   g_variant_new ("(ss)", key, value),
			   G_DBUS_CALL_FLAGS_NONE, -1,
			   cancellable,
			   fwupd_client_modify_config_cb,
			   g_steal_pointer (&task));
}

/**
 * fwupd_client_modify_config_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_modify_config_async().
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_modify_config_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean (G_TASK(res), error);
}

static void
fwupd_client_activate_cb (GObject *source,
			  GAsyncResult *res,
			  gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_boolean (task, TRUE);
}

/**
 * fwupd_client_activate_async:
 * @self: A #FwupdClient
 * @device_id: a device
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Activates up a device, which normally means the device switches to a new
 * firmware version. This should only be called when data loss cannot occur.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_activate_async (FwupdClient *self,
			     const gchar *device_id,
			     GCancellable *cancellable,
			     GAsyncReadyCallback callback,
			     gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (device_id != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_dbus_proxy_call (priv->proxy, "Activate",
			   g_variant_new ("(s)", device_id),
			   G_DBUS_CALL_FLAGS_NONE, -1,
			   cancellable,
			   fwupd_client_activate_cb,
			   g_steal_pointer (&task));
}

/**
 * fwupd_client_activate_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_activate_async().
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_activate_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean (G_TASK(res), error);
}

static void
fwupd_client_verify_cb (GObject *source,
			GAsyncResult *res,
			gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_boolean (task, TRUE);
}

/**
 * fwupd_client_verify_async:
 * @self: A #FwupdClient
 * @device_id: the device ID
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Verify a specific device.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_verify_async (FwupdClient *self,
			   const gchar *device_id,
			   GCancellable *cancellable,
			   GAsyncReadyCallback callback,
			   gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (device_id != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_dbus_proxy_call (priv->proxy, "Verify",
			   g_variant_new ("(s)", device_id),
			   G_DBUS_CALL_FLAGS_NONE, -1,
			   cancellable,
			   fwupd_client_verify_cb,
			   g_steal_pointer (&task));
}

/**
 * fwupd_client_verify_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_verify_async().
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_verify_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean (G_TASK(res), error);
}

static void
fwupd_client_verify_update_cb (GObject *source,
			       GAsyncResult *res,
			       gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_boolean (task, TRUE);
}

/**
 * fwupd_client_verify_update_async:
 * @self: A #FwupdClient
 * @device_id: the device ID
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Update the verification record for a specific device.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_verify_update_async (FwupdClient *self,
				  const gchar *device_id,
				  GCancellable *cancellable,
				  GAsyncReadyCallback callback,
				  gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (device_id != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_dbus_proxy_call (priv->proxy, "VerifyUpdate",
			   g_variant_new ("(s)", device_id),
			   G_DBUS_CALL_FLAGS_NONE, -1,
			   cancellable,
			   fwupd_client_verify_update_cb,
			   g_steal_pointer (&task));
}

/**
 * fwupd_client_verify_update_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_verify_update_async().
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_verify_update_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean (G_TASK(res), error);
}

static void
fwupd_client_unlock_cb (GObject *source,
			GAsyncResult *res,
			gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_boolean (task, TRUE);
}

/**
 * fwupd_client_unlock_async:
 * @self: A #FwupdClient
 * @device_id: the device ID
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Unlocks a specific device so firmware can be read or wrote.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_unlock_async (FwupdClient *self,
			   const gchar *device_id,
			   GCancellable *cancellable,
			   GAsyncReadyCallback callback,
			   gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (device_id != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_dbus_proxy_call (priv->proxy, "Unlock",
			   g_variant_new ("(s)", device_id),
			   G_DBUS_CALL_FLAGS_NONE, -1,
			   cancellable,
			   fwupd_client_unlock_cb,
			   g_steal_pointer (&task));
}

/**
 * fwupd_client_unlock_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_unlock_async().
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_unlock_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean (G_TASK(res), error);
}

static void
fwupd_client_clear_results_cb (GObject *source,
			  GAsyncResult *res,
			  gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_boolean (task, TRUE);
}

/**
 * fwupd_client_clear_results_async:
 * @self: A #FwupdClient
 * @device_id: a device
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Clears the results for a specific device.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_clear_results_async (FwupdClient *self,
				  const gchar *device_id,
				  GCancellable *cancellable,
				  GAsyncReadyCallback callback,
				  gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (device_id != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_dbus_proxy_call (priv->proxy, "ClearResults",
			   g_variant_new ("(s)", device_id),
			   G_DBUS_CALL_FLAGS_NONE, -1,
			   cancellable,
			   fwupd_client_clear_results_cb,
			   g_steal_pointer (&task));
}

/**
 * fwupd_client_clear_results_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_clear_results_async().
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_clear_results_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean (G_TASK(res), error);
}

static void
fwupd_client_get_results_cb (GObject *source,
			      GAsyncResult *res,
			      gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_pointer (task,
			       fwupd_device_from_variant (val),
			       (GDestroyNotify) g_ptr_array_unref);
}

/**
 * fwupd_client_get_results_async:
 * @self: A #FwupdClient
 * @device_id: the device ID
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets the results of a previous firmware update for a specific device.
 *
 * You must have called fwupd_client_connect_async() on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_results_async (FwupdClient *self, const gchar *device_id,
				 GCancellable *cancellable,
				 GAsyncReadyCallback callback,
				 gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (device_id != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_dbus_proxy_call (priv->proxy, "GetResults",
			   g_variant_new ("(s)", device_id),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1, cancellable,
			   fwupd_client_get_results_cb,
			   g_steal_pointer (&task));
}

/**
 * fwupd_client_get_results_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_get_results_async().
 *
 * Returns: (transfer full): a #FwupdDevice, or %NULL for failure
 *
 * Since: 1.5.0
 **/
FwupdDevice *
fwupd_client_get_results_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), NULL);
	g_return_val_if_fail (g_task_is_valid (res, self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer (G_TASK(res), error);
}

#ifdef HAVE_GIO_UNIX

static void
fwupd_client_install_stream_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GDBusMessage) msg = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = G_TASK (user_data);

	msg = g_dbus_connection_send_message_with_reply_finish (G_DBUS_CONNECTION (source),
								res, &error);
	if (msg == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}
	if (g_dbus_message_to_gerror (msg, &error)) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_boolean (task, TRUE);
}

void
fwupd_client_install_stream_async (FwupdClient *self,
				   const gchar *device_id,
				   GUnixInputStream *istr,
				   const gchar *filename_hint,
				   FwupdInstallFlags install_flags,
				   GCancellable *cancellable,
				   GAsyncReadyCallback callback,
				   gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	GVariantBuilder builder;
	g_autoptr(GDBusMessage) request = NULL;
	g_autoptr(GUnixFDList) fd_list = NULL;
	g_autoptr(GTask) task = g_task_new (self, cancellable, callback, callback_data);

	/* set options */
	g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add (&builder, "{sv}",
			       "reason", g_variant_new_string ("user-action"));
	if (filename_hint != NULL) {
		g_variant_builder_add (&builder, "{sv}",
				       "filename", g_variant_new_string (filename_hint));
	}
	if (install_flags & FWUPD_INSTALL_FLAG_OFFLINE) {
		g_variant_builder_add (&builder, "{sv}",
				       "offline", g_variant_new_boolean (TRUE));
	}
	if (install_flags & FWUPD_INSTALL_FLAG_ALLOW_OLDER) {
		g_variant_builder_add (&builder, "{sv}",
				       "allow-older", g_variant_new_boolean (TRUE));
	}
	if (install_flags & FWUPD_INSTALL_FLAG_ALLOW_REINSTALL) {
		g_variant_builder_add (&builder, "{sv}",
				       "allow-reinstall", g_variant_new_boolean (TRUE));
	}
	if (install_flags & FWUPD_INSTALL_FLAG_ALLOW_BRANCH_SWITCH) {
		g_variant_builder_add (&builder, "{sv}",
				       "allow-branch-switch", g_variant_new_boolean (TRUE));
	}
	if (install_flags & FWUPD_INSTALL_FLAG_FORCE) {
		g_variant_builder_add (&builder, "{sv}",
				       "force", g_variant_new_boolean (TRUE));
	}
	if (install_flags & FWUPD_INSTALL_FLAG_IGNORE_POWER) {
		g_variant_builder_add (&builder, "{sv}",
				       "ignore-power", g_variant_new_boolean (TRUE));
	}
	if (install_flags & FWUPD_INSTALL_FLAG_NO_HISTORY) {
		g_variant_builder_add (&builder, "{sv}",
				       "no-history", g_variant_new_boolean (TRUE));
	}

	/* set out of band file descriptor */
	fd_list = g_unix_fd_list_new ();
	g_unix_fd_list_append (fd_list, g_unix_input_stream_get_fd (istr), NULL);
	request = g_dbus_message_new_method_call (FWUPD_DBUS_SERVICE,
						  FWUPD_DBUS_PATH,
						  FWUPD_DBUS_INTERFACE,
						  "Install");
	g_dbus_message_set_unix_fd_list (request, fd_list);

	/* call into daemon */
	g_dbus_message_set_body (request, g_variant_new ("(sha{sv})",
							 device_id,
							 g_unix_input_stream_get_fd (istr),
							 &builder));
	g_dbus_connection_send_message_with_reply (priv->conn,
						   request,
						   G_DBUS_SEND_MESSAGE_FLAGS_NONE,
						   G_MAXINT,
						   NULL,
						   cancellable,
						   fwupd_client_install_stream_cb,
						   g_steal_pointer (&task));
}
#endif

/**
 * fwupd_client_install_bytes_async:
 * @self: A #FwupdClient
 * @device_id: the device ID
 * @bytes: #GBytes
 * @install_flags: the #FwupdInstallFlags, e.g. %FWUPD_INSTALL_FLAG_ALLOW_REINSTALL
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Install firmware onto a specific device.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_install_bytes_async (FwupdClient *self,
				  const gchar *device_id,
				  GBytes *bytes,
				  FwupdInstallFlags install_flags,
				  GCancellable *cancellable,
				  GAsyncReadyCallback callback,
				  gpointer callback_data)
{
#ifdef HAVE_GIO_UNIX
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GError) error = NULL;
	g_autoptr(GUnixInputStream) istr = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* move to a thread if this ever takes more than a few ms */
	istr = fwupd_unix_input_stream_from_bytes (bytes, &error);
	if (istr == NULL) {
		g_autoptr(GTask) task = g_task_new (self, cancellable, callback, callback_data);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* call into daemon */
	fwupd_client_install_stream_async (self, device_id, istr, NULL,
					   install_flags, cancellable,
					   callback, callback_data);
#else
	g_autoptr(GTask) task = g_task_new (self, cancellable, callback, callback_data);
	g_task_return_new_error (task,
				 FWUPD_ERROR,
				 FWUPD_ERROR_NOT_SUPPORTED,
				 "Not supported as <glib-unix.h> is unavailable");
#endif
}

/**
 * fwupd_client_install_bytes_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_install_bytes_async().
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_install_bytes_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean (G_TASK(res), error);
}

/**
 * fwupd_client_install_async:
 * @self: A #FwupdClient
 * @device_id: the device ID
 * @filename: the filename to install
 * @install_flags: the #FwupdInstallFlags, e.g. %FWUPD_INSTALL_FLAG_ALLOW_REINSTALL
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Install firmware onto a specific device.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_install_async (FwupdClient *self,
			   const gchar *device_id,
			   const gchar *filename,
			   FwupdInstallFlags install_flags,
			   GCancellable *cancellable,
			   GAsyncReadyCallback callback,
			   gpointer callback_data)
{
#ifdef HAVE_GIO_UNIX
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GError) error = NULL;
	g_autoptr(GUnixInputStream) istr = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (device_id != NULL);
	g_return_if_fail (filename != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* move to a thread if this ever takes more than a few ms */
	istr = fwupd_unix_input_stream_from_fn (filename, &error);
	if (istr == NULL) {
		g_autoptr(GTask) task = g_task_new (self, cancellable, callback, callback_data);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* call into daemon */
	fwupd_client_install_stream_async (self, device_id, istr, NULL,
					   install_flags, cancellable,
					   callback, callback_data);
#else
	g_autoptr(GTask) task = g_task_new (self, cancellable, callback, callback_data);
	g_task_return_new_error (task,
				 FWUPD_ERROR,
				 FWUPD_ERROR_NOT_SUPPORTED,
				 "Not supported as <glib-unix.h> is unavailable");
#endif
}

/**
 * fwupd_client_install_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_install_async().
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_install_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean (G_TASK(res), error);
}

typedef struct {
	FwupdDevice		*device;
	FwupdRelease		*release;
	FwupdInstallFlags	 install_flags;
} FwupdClientInstallReleaseData;

static void
fwupd_client_install_release_data_free (FwupdClientInstallReleaseData *data)
{
	g_object_unref (data->device);
	g_object_unref (data->release);
	g_free (data);
}

static void
fwupd_client_install_release_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = G_TASK (user_data);

	if (!fwupd_client_install_release_finish (FWUPD_CLIENT (source), res, &error)) {
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_boolean (task, TRUE);
}

static void
fwupd_client_install_release_bytes_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = G_TASK (user_data);

	if (!fwupd_client_install_bytes_finish (FWUPD_CLIENT (source), res, &error)) {
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_boolean (task, TRUE);
}

static void
fwupd_client_install_release_download_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = G_TASK (user_data);
	FwupdClientInstallReleaseData *data = g_task_get_task_data (task);
	GChecksumType checksum_type;
	GCancellable *cancellable = g_task_get_cancellable (task);
	const gchar *checksum_expected;
	g_autofree gchar *checksum_actual = NULL;

	blob = fwupd_client_download_bytes_finish (FWUPD_CLIENT (source), res, &error);
	if (blob == NULL) {
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* verify checksum */
	checksum_expected = fwupd_checksum_get_best (fwupd_release_get_checksums (data->release));
	checksum_type = fwupd_checksum_guess_kind (checksum_expected);
	checksum_actual = g_compute_checksum_for_bytes (checksum_type, blob);
	if (g_strcmp0 (checksum_expected, checksum_actual) != 0) {
		g_task_return_new_error (task,
					 FWUPD_ERROR,
					 FWUPD_ERROR_INVALID_FILE,
					 "checksum invalid, expected %s got %s",
					 checksum_expected, checksum_actual);
		return;
	}

	/* if the device specifies ONLY_OFFLINE automatically set this flag */
	if (fwupd_device_has_flag (data->device, FWUPD_DEVICE_FLAG_ONLY_OFFLINE))
		data->install_flags |= FWUPD_INSTALL_FLAG_OFFLINE;
	fwupd_client_install_bytes_async (FWUPD_CLIENT (source),
					  fwupd_device_get_id (data->device), blob,
					  data->install_flags, cancellable,
					  fwupd_client_install_release_bytes_cb,
					  g_steal_pointer (&task));
}

static gboolean
fwupd_client_is_url (const gchar *perhaps_url)
{
#ifdef HAVE_LIBCURL_7_62_0
	g_autoptr(CURLU) h = curl_url ();
	return curl_url_set (h, CURLUPART_URL, perhaps_url, 0) == CURLUE_OK;
#else
	return g_str_has_prefix (perhaps_url, "http://") ||
		g_str_has_prefix (perhaps_url, "https://");
#endif
}

static void
fwupd_client_install_release_remote_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autofree gchar *fn = NULL;
	g_autofree gchar *uri_str = NULL;
	g_autoptr(FwupdRemote) remote = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = G_TASK (user_data);
	FwupdClientInstallReleaseData *data = g_task_get_task_data (task);
	GCancellable *cancellable = g_task_get_cancellable (task);

	/* if a remote-id was specified, the remote has to exist */
	remote = fwupd_client_get_remote_by_id_finish (FWUPD_CLIENT (source), res, &error);
	if (remote == NULL) {
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* local and directory remotes may have the firmware already */
	if (fwupd_remote_get_kind (remote) == FWUPD_REMOTE_KIND_LOCAL &&
	    !fwupd_client_is_url (fwupd_release_get_uri (data->release))) {
		const gchar *fn_cache = fwupd_remote_get_filename_cache (remote);
		g_autofree gchar *path = g_path_get_dirname (fn_cache);
		fn = g_build_filename (path, fwupd_release_get_uri (data->release), NULL);
	} else if (fwupd_remote_get_kind (remote) == FWUPD_REMOTE_KIND_DIRECTORY) {
		fn = g_strdup (fwupd_release_get_uri (data->release) + 7);
	}

	/* install with flags chosen by the user */
	if (fn != NULL) {
		fwupd_client_install_async (FWUPD_CLIENT (source),
					    fwupd_device_get_id (data->device),
					    fn, data->install_flags,
					    cancellable,
					    fwupd_client_install_release_cb,
					    g_steal_pointer (&task));
		return;
	}

	/* remote file */
	uri_str = fwupd_remote_build_firmware_uri (remote,
						   fwupd_release_get_uri (data->release),
						   &error);
	if (uri_str == NULL) {
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* download file */
	fwupd_client_download_bytes_async (FWUPD_CLIENT (source), uri_str,
					   FWUPD_CLIENT_DOWNLOAD_FLAG_NONE,
					   cancellable,
					   fwupd_client_install_release_download_cb,
					   g_steal_pointer (&task));
}

/**
 * fwupd_client_install_release_async:
 * @self: A #FwupdClient
 * @device: A #FwupdDevice
 * @release: A #FwupdRelease
 * @install_flags: the #FwupdInstallFlags, e.g. %FWUPD_INSTALL_FLAG_ALLOW_REINSTALL
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Installs a new release on a device, downloading the firmware if required.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_install_release_async (FwupdClient *self,
				    FwupdDevice *device,
				    FwupdRelease *release,
				    FwupdInstallFlags install_flags,
				    GCancellable *cancellable,
				    GAsyncReadyCallback callback,
				    gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;
	FwupdClientInstallReleaseData *data;
	const gchar *remote_id;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (FWUPD_IS_DEVICE (device));
	g_return_if_fail (FWUPD_IS_RELEASE (release));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	data = g_new0 (FwupdClientInstallReleaseData, 1);
	data->device = g_object_ref (device);
	data->release = g_object_ref (release);
	data->install_flags = install_flags;
	g_task_set_task_data (task, data, (GDestroyNotify) fwupd_client_install_release_data_free);

	/* work out what remote-specific URI fields this should use */
	remote_id = fwupd_release_get_remote_id (release);
	if (remote_id == NULL) {
		fwupd_client_download_bytes_async (self,
						   fwupd_release_get_uri (release),
						   FWUPD_CLIENT_DOWNLOAD_FLAG_NONE,
						   cancellable,
						   fwupd_client_install_release_download_cb,
						   g_steal_pointer (&task));
		return;
	}

	/* if a remote-id was specified, the remote has to exist */
	fwupd_client_get_remote_by_id_async (self, remote_id, cancellable,
					     fwupd_client_install_release_remote_cb,
					     g_steal_pointer (&task));
}

/**
 * fwupd_client_install_release_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_install_release_async().
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_install_release_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean (G_TASK(res), error);
}

#ifdef HAVE_GIO_UNIX

static void
fwupd_client_get_details_stream_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GDBusMessage) msg = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = G_TASK (user_data);

	msg = g_dbus_connection_send_message_with_reply_finish (G_DBUS_CONNECTION (source),
								res, &error);
	if (msg == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}
	if (g_dbus_message_to_gerror (msg, &error)) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_pointer (task,
			       fwupd_device_array_from_variant (g_dbus_message_get_body (msg)),
			       (GDestroyNotify) g_ptr_array_unref);
}

void
fwupd_client_get_details_stream_async (FwupdClient *self,
				       GUnixInputStream *istr,
				       GCancellable *cancellable,
				       GAsyncReadyCallback callback,
				       gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	gint fd = g_unix_input_stream_get_fd (istr);
	g_autoptr(GDBusMessage) request = NULL;
	g_autoptr(GUnixFDList) fd_list = NULL;
	g_autoptr(GTask) task = g_task_new (self, cancellable, callback, callback_data);

	/* set out of band file descriptor */
	fd_list = g_unix_fd_list_new ();
	g_unix_fd_list_append (fd_list, fd, NULL);
	request = g_dbus_message_new_method_call (FWUPD_DBUS_SERVICE,
						  FWUPD_DBUS_PATH,
						  FWUPD_DBUS_INTERFACE,
						  "GetDetails");
	g_dbus_message_set_unix_fd_list (request, fd_list);

	/* call into daemon */
	g_dbus_message_set_body (request, g_variant_new ("(h)", fd));
	g_dbus_connection_send_message_with_reply (priv->conn,
						   request,
						   G_DBUS_SEND_MESSAGE_FLAGS_NONE,
						   G_MAXINT,
						   NULL,
						   cancellable,
						   fwupd_client_get_details_stream_cb,
						   g_steal_pointer (&task));
}
#endif

/**
 * fwupd_client_get_details_bytes_async:
 * @self: A #FwupdClient
 * @bytes: a #GBytes for the firmware, e.g. `firmware.cab`
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets details about a specific firmware file.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_details_bytes_async (FwupdClient *self,
				      GBytes *bytes,
				      GCancellable *cancellable,
				      GAsyncReadyCallback callback,
				      gpointer callback_data)
{
#ifdef HAVE_GIO_UNIX
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GError) error = NULL;
	g_autoptr(GUnixInputStream) istr = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* move to a thread if this ever takes more than a few ms */
	istr = fwupd_unix_input_stream_from_bytes (bytes, &error);
	if (istr == NULL) {
		g_autoptr(GTask) task = g_task_new (self, cancellable, callback, callback_data);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* call into daemon */
	fwupd_client_get_details_stream_async (self, istr, cancellable,
					       callback, callback_data);
#else
	g_autoptr(GTask) task = g_task_new (self, cancellable, callback, callback_data);
	g_task_return_new_error (task,
				 FWUPD_ERROR,
				 FWUPD_ERROR_NOT_SUPPORTED,
				 "Not supported as <glib-unix.h> is unavailable");
#endif
}

/**
 * fwupd_client_get_details_bytes_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_get_details_bytes_async().
 *
 * Returns: (transfer container) (element-type FwupdDevice): an array of results
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_details_bytes_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), NULL);
	g_return_val_if_fail (g_task_is_valid (res, self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer (G_TASK(res), error);
}

/**
 * fwupd_client_get_percentage:
 * @self: A #FwupdClient
 *
 * Gets the last returned percentage value.
 *
 * Returns: a percentage, or 0 for unknown.
 *
 * Since: 0.7.3
 **/
guint
fwupd_client_get_percentage (FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), 0);
	return priv->percentage;
}

/**
 * fwupd_client_get_daemon_version:
 * @self: A #FwupdClient
 *
 * Gets the daemon version number.
 *
 * Returns: a string, or %NULL for unknown.
 *
 * Since: 0.9.6
 **/
const gchar *
fwupd_client_get_daemon_version (FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), NULL);
	return priv->daemon_version;
}

/**
 * fwupd_client_get_host_product:
 * @self: A #FwupdClient
 *
 * Gets the string that represents the host running fwupd
 *
 * Returns: a string, or %NULL for unknown.
 *
 * Since: 1.3.1
 **/
const gchar *
fwupd_client_get_host_product (FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), NULL);
	return priv->host_product;
}

/**
 * fwupd_client_get_host_machine_id:
 * @self: A #FwupdClient
 *
 * Gets the string that represents the host machine ID
 *
 * Returns: a string, or %NULL for unknown.
 *
 * Since: 1.3.2
 **/
const gchar *
fwupd_client_get_host_machine_id (FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), NULL);
	return priv->host_machine_id;
}

/**
 * fwupd_client_get_host_security_id:
 * @self: A #FwupdClient
 *
 * Gets the string that represents the host machine ID
 *
 * Returns: a string, or %NULL for unknown.
 *
 * Since: 1.5.0
 **/
const gchar *
fwupd_client_get_host_security_id (FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), NULL);
	return priv->host_security_id;
}

/**
 * fwupd_client_get_status:
 * @self: A #FwupdClient
 *
 * Gets the last returned status value.
 *
 * Returns: a #FwupdStatus, or %FWUPD_STATUS_UNKNOWN for unknown.
 *
 * Since: 0.7.3
 **/
FwupdStatus
fwupd_client_get_status (FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), FWUPD_STATUS_UNKNOWN);
	return priv->status;
}

/**
 * fwupd_client_get_tainted:
 * @self: A #FwupdClient
 *
 * Gets if the daemon has been tainted by 3rd party code.
 *
 * Returns: %TRUE if the daemon is unsupported
 *
 * Since: 1.2.4
 **/
gboolean
fwupd_client_get_tainted (FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), FALSE);
	return priv->tainted;
}

/**
 * fwupd_client_get_daemon_interactive:
 * @self: A #FwupdClient
 *
 * Gets if the daemon is running in an interactive terminal.
 *
 * Returns: %TRUE if the daemon is running in an interactive terminal
 *
 * Since: 1.3.4
 **/
gboolean
fwupd_client_get_daemon_interactive (FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), FALSE);
	return priv->interactive;
}

#ifdef HAVE_GIO_UNIX

static void
fwupd_client_update_metadata_stream_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GDBusMessage) msg = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = G_TASK (user_data);

	msg = g_dbus_connection_send_message_with_reply_finish (G_DBUS_CONNECTION (source),
								res, &error);
	if (msg == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}
	if (g_dbus_message_to_gerror (msg, &error)) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_boolean (task, TRUE);
}

void
fwupd_client_update_metadata_stream_async (FwupdClient *self,
					   const gchar *remote_id,
					   GUnixInputStream *istr,
					   GUnixInputStream *istr_sig,
					   GCancellable *cancellable,
					   GAsyncReadyCallback callback,
					   gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GDBusMessage) request = NULL;
	g_autoptr(GUnixFDList) fd_list = NULL;
	g_autoptr(GTask) task = g_task_new (self, cancellable, callback, callback_data);

	/* set out of band file descriptor */
	fd_list = g_unix_fd_list_new ();
	g_unix_fd_list_append (fd_list, g_unix_input_stream_get_fd (istr), NULL);
	g_unix_fd_list_append (fd_list, g_unix_input_stream_get_fd (istr_sig), NULL);
	request = g_dbus_message_new_method_call (FWUPD_DBUS_SERVICE,
						  FWUPD_DBUS_PATH,
						  FWUPD_DBUS_INTERFACE,
						  "UpdateMetadata");
	g_dbus_message_set_unix_fd_list (request, fd_list);

	/* call into daemon */
	g_dbus_message_set_body (request, g_variant_new ("(shh)",
							 remote_id,
							 g_unix_input_stream_get_fd (istr),
							 g_unix_input_stream_get_fd (istr_sig)));
	g_dbus_connection_send_message_with_reply (priv->conn,
						   request,
						   G_DBUS_SEND_MESSAGE_FLAGS_NONE,
						   G_MAXINT,
						   NULL,
						   cancellable,
						   fwupd_client_update_metadata_stream_cb,
						   g_steal_pointer (&task));
}
#endif

/**
 * fwupd_client_update_metadata_bytes_async:
 * @self: A #FwupdClient
 * @remote_id: remote ID, e.g. `lvfs-testing`
 * @metadata: XML metadata data
 * @signature: signature data
 * @cancellable: #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Updates the metadata. This allows a session process to download the metadata
 * and metadata signing file to be passed into the daemon to be checked and
 * parsed.
 *
 * The @remote_id allows the firmware to be tagged so that the remote can be
 * matched when the firmware is downloaded.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_update_metadata_bytes_async (FwupdClient *self,
					  const gchar *remote_id,
					  GBytes *metadata,
					  GBytes *signature,
					  GCancellable *cancellable,
					  GAsyncReadyCallback callback,
					  gpointer callback_data)
{
#ifdef HAVE_GIO_UNIX
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GError) error = NULL;
	g_autoptr(GUnixInputStream) istr = NULL;
	g_autoptr(GUnixInputStream) istr_sig = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (remote_id != NULL);
	g_return_if_fail (metadata != NULL);
	g_return_if_fail (signature != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* move to a thread if this ever takes more than a few ms */
	istr = fwupd_unix_input_stream_from_bytes (metadata, &error);
	if (istr == NULL) {
		g_autoptr(GTask) task = g_task_new (self, cancellable, callback, callback_data);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}
	istr_sig = fwupd_unix_input_stream_from_bytes (signature, &error);
	if (istr_sig == NULL) {
		g_autoptr(GTask) task = g_task_new (self, cancellable, callback, callback_data);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* call into daemon */
	fwupd_client_update_metadata_stream_async (self, remote_id, istr, istr_sig,
						   cancellable,
						   callback, callback_data);
#else
	g_autoptr(GTask) task = g_task_new (self, cancellable, callback, callback_data);
	g_task_return_new_error (task,
				 FWUPD_ERROR,
				 FWUPD_ERROR_NOT_SUPPORTED,
				 "Not supported as <glib-unix.h> is unavailable");
#endif
}

/**
 * fwupd_client_update_metadata_bytes_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_update_metadata_bytes_async().
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_update_metadata_bytes_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean (G_TASK(res), error);
}

typedef struct {
	FwupdRemote	*remote;
	GBytes		*signature;
	GBytes		*metadata;
} FwupdClientRefreshRemoteData;

static void
fwupd_client_refresh_remote_data_free (FwupdClientRefreshRemoteData *data)
{
	if (data->signature != NULL)
		g_bytes_unref (data->signature);
	if (data->metadata != NULL)
		g_bytes_unref (data->metadata);
	g_object_unref (data->remote);
	g_free (data);
}

static void
fwupd_client_refresh_remote_update_cb (GObject *source,
				       GAsyncResult *res,
				       gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = G_TASK (user_data);

	/* save metadata */
	if (!fwupd_client_update_metadata_bytes_finish (FWUPD_CLIENT (source), res, &error)) {
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_boolean (task, TRUE);
}

static void
fwupd_client_refresh_remote_metadata_cb (GObject *source,
					 GAsyncResult *res,
					 gpointer user_data)
{
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = G_TASK (user_data);
	FwupdClientRefreshRemoteData *data = g_task_get_task_data (task);
	FwupdClient *self = g_task_get_source_object (task);
	GCancellable *cancellable = g_task_get_cancellable (task);

	/* save metadata */
	bytes = fwupd_client_download_bytes_finish (FWUPD_CLIENT (source), res, &error);
	if (bytes == NULL) {
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}
	data->metadata = g_steal_pointer (&bytes);

	/* send all this to fwupd */
	fwupd_client_update_metadata_bytes_async (self,
						  fwupd_remote_get_id (data->remote),
						  data->metadata,
						  data->signature,
						  cancellable,
						  fwupd_client_refresh_remote_update_cb,
						  g_steal_pointer (&task));
}

static void
fwupd_client_refresh_remote_signature_cb (GObject *source,
					  GAsyncResult *res,
					  gpointer user_data)
{
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = G_TASK (user_data);
	FwupdClientRefreshRemoteData *data = g_task_get_task_data (task);
	FwupdClient *self = g_task_get_source_object (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	GChecksumType checksum_kind;
	g_autofree gchar *checksum = NULL;

	/* save signature */
	bytes = fwupd_client_download_bytes_finish (FWUPD_CLIENT (source), res, &error);
	if (bytes == NULL) {
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}
	data->signature = g_steal_pointer (&bytes);
	if (fwupd_remote_get_keyring_kind (data->remote) == FWUPD_KEYRING_KIND_JCAT) {
		if (!fwupd_remote_load_signature_bytes (data->remote, data->signature, &error)) {
			g_prefix_error (&error, "Failed to load signature: ");
			g_task_return_error (task, g_steal_pointer (&error));
			return;
		}
	}

	/* is the signature checksum the same? */
	checksum_kind = fwupd_checksum_guess_kind (fwupd_remote_get_checksum (data->remote));
	checksum = g_compute_checksum_for_data (checksum_kind,
						(const guchar *) g_bytes_get_data (data->signature, NULL),
						g_bytes_get_size (data->signature));
	if (g_strcmp0 (checksum, fwupd_remote_get_checksum (data->remote)) == 0) {
		g_debug ("metadata signature of %s is unchanged, skipping",
			 fwupd_remote_get_id (data->remote));
		g_task_return_boolean (task, TRUE);
		return;
	}

	/* download metadata */
	fwupd_client_download_bytes_async (self,
					   fwupd_remote_get_metadata_uri (data->remote),
					   FWUPD_CLIENT_DOWNLOAD_FLAG_NONE,
					   cancellable,
					   fwupd_client_refresh_remote_metadata_cb,
					   g_steal_pointer (&task));
}

/**
 * fwupd_client_refresh_remote_async:
 * @self: A #FwupdClient
 * @remote: A #FwupdRemote
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Refreshes a remote by downloading new metadata.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_refresh_remote_async (FwupdClient *self,
				   FwupdRemote *remote,
				   GCancellable *cancellable,
				   GAsyncReadyCallback callback,
				   gpointer callback_data)
{
	FwupdClientRefreshRemoteData *data;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (FWUPD_IS_REMOTE (remote));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	task = g_task_new (self, cancellable, callback, callback_data);
	data = g_new0 (FwupdClientRefreshRemoteData, 1);
	data->remote = g_object_ref (remote);
	g_task_set_task_data (task,
			      g_steal_pointer (&data),
			      (GDestroyNotify) fwupd_client_refresh_remote_data_free);

	/* download signature */
	fwupd_client_download_bytes_async (self,
					   fwupd_remote_get_metadata_uri_sig (remote),
					   FWUPD_CLIENT_DOWNLOAD_FLAG_NONE,
					   cancellable,
					   fwupd_client_refresh_remote_signature_cb,
					   g_steal_pointer (&task));

}

/**
 * fwupd_client_refresh_remote_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_refresh_remote_async().
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_refresh_remote_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean (G_TASK(res), error);
}

static void
fwupd_client_get_remotes_cb (GObject *source,
			     GAsyncResult *res,
			     gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_pointer (task,
			       fwupd_remote_array_from_variant (val),
			       (GDestroyNotify) g_ptr_array_unref);
}

/**
 * fwupd_client_get_remotes_async:
 * @self: A #FwupdClient
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets the list of remotes that have been configured for the system.
 *
 * You must have called fwupd_client_connect_async() on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_remotes_async (FwupdClient *self, GCancellable *cancellable,
				GAsyncReadyCallback callback, gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_dbus_proxy_call (priv->proxy, "GetRemotes",
			   NULL, G_DBUS_CALL_FLAGS_NONE,
			   -1, cancellable,
			   fwupd_client_get_remotes_cb,
			   g_steal_pointer (&task));
}

/**
 * fwupd_client_get_remotes_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_get_remotes_async().
 *
 * Returns: (element-type FwupdRemote) (transfer container): results
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_remotes_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), NULL);
	g_return_val_if_fail (g_task_is_valid (res, self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer (G_TASK(res), error);
}

static void
fwupd_client_get_approved_firmware_cb (GObject *source,
				       GAsyncResult *res,
				       gpointer user_data)
{
	g_auto(GStrv) strv = NULL;
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func (g_free);
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	g_variant_get (val, "(^as)", &strv);
	for (guint i = 0; strv[i] != NULL; i++)
		g_ptr_array_add (array, g_strdup (strv[i]));

	/* success */
	g_task_return_pointer (task,
			       g_steal_pointer (&array),
			       (GDestroyNotify) g_ptr_array_unref);
}

/**
 * fwupd_client_get_approved_firmware_async:
 * @self: A #FwupdClient
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets the list of approved firmware.
 *
 * You must have called fwupd_client_connect_async() on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_approved_firmware_async (FwupdClient *self,
					  GCancellable *cancellable,
					  GAsyncReadyCallback callback,
					  gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_dbus_proxy_call (priv->proxy, "GetApprovedFirmware",
			   NULL,
			   G_DBUS_CALL_FLAGS_NONE,
			   -1, cancellable,
			   fwupd_client_get_approved_firmware_cb,
			   g_steal_pointer (&task));
}

/**
 * fwupd_client_get_approved_firmware_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_get_approved_firmware_async().
 *
 * Returns: (element-type utf8) (transfer container): checksums, or %NULL for error
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_approved_firmware_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), NULL);
	g_return_val_if_fail (g_task_is_valid (res, self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer (G_TASK(res), error);
}

static void
fwupd_client_set_approved_firmware_cb (GObject *source,
				       GAsyncResult *res,
				       gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_boolean (task, TRUE);
}

/**
 * fwupd_client_set_approved_firmware_async:
 * @self: A #FwupdClient
 * @checksums: (element-type utf8): firmware checksums
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Sets the list of approved firmware.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_set_approved_firmware_async (FwupdClient *self,
					  GPtrArray *checksums,
					  GCancellable *cancellable,
					  GAsyncReadyCallback callback,
					  gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;
	g_auto(GStrv) strv = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	strv = g_new0 (gchar *, checksums->len + 1);
	for (guint i = 0; i < checksums->len; i++) {
		const gchar *tmp = g_ptr_array_index (checksums, i);
		strv[i] = g_strdup (tmp);
	}
	g_dbus_proxy_call (priv->proxy, "SetApprovedFirmware",
			   g_variant_new ("(^as)", strv),
			   G_DBUS_CALL_FLAGS_NONE, -1,
			   cancellable,
			   fwupd_client_set_approved_firmware_cb,
			   g_steal_pointer (&task));
}

/**
 * fwupd_client_set_approved_firmware_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_set_approved_firmware_async().
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_set_approved_firmware_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean (G_TASK(res), error);
}

static void
fwupd_client_get_blocked_firmware_cb (GObject *source,
				      GAsyncResult *res,
				      gpointer user_data)
{
	g_auto(GStrv) strv = NULL;
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func (g_free);
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	g_variant_get (val, "(^as)", &strv);
	for (guint i = 0; strv[i] != NULL; i++)
		g_ptr_array_add (array, g_strdup (strv[i]));

	/* success */
	g_task_return_pointer (task,
			       g_steal_pointer (&array),
			       (GDestroyNotify) g_ptr_array_unref);
}

/**
 * fwupd_client_get_blocked_firmware_async:
 * @self: A #FwupdClient
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets the list of blocked firmware.
 *
 * You must have called fwupd_client_connect_async() on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_blocked_firmware_async (FwupdClient *self,
					  GCancellable *cancellable,
					  GAsyncReadyCallback callback,
					  gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_dbus_proxy_call (priv->proxy, "GetBlockedFirmware",
			   NULL,
			   G_DBUS_CALL_FLAGS_NONE,
			   -1, cancellable,
			   fwupd_client_get_blocked_firmware_cb,
			   g_steal_pointer (&task));
}

/**
 * fwupd_client_get_blocked_firmware_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_get_blocked_firmware_async().
 *
 * Returns: (element-type utf8) (transfer container): checksums, or %NULL for error
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_blocked_firmware_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), NULL);
	g_return_val_if_fail (g_task_is_valid (res, self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer (G_TASK(res), error);
}

static void
fwupd_client_set_blocked_firmware_cb (GObject *source,
				      GAsyncResult *res,
				      gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_boolean (task, TRUE);
}

/**
 * fwupd_client_set_blocked_firmware_async:
 * @self: A #FwupdClient
 * @checksums: (element-type utf8): firmware checksums
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Sets the list of blocked firmware.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_set_blocked_firmware_async (FwupdClient *self,
					 GPtrArray *checksums,
					 GCancellable *cancellable,
					 GAsyncReadyCallback callback,
					 gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;
	g_auto(GStrv) strv = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	strv = g_new0 (gchar *, checksums->len + 1);
	for (guint i = 0; i < checksums->len; i++) {
		const gchar *tmp = g_ptr_array_index (checksums, i);
		strv[i] = g_strdup (tmp);
	}
	g_dbus_proxy_call (priv->proxy, "SetBlockedFirmware",
			   g_variant_new ("(^as)", strv),
			   G_DBUS_CALL_FLAGS_NONE, -1,
			   cancellable,
			   fwupd_client_set_blocked_firmware_cb,
			   g_steal_pointer (&task));
}

/**
 * fwupd_client_set_blocked_firmware_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_set_blocked_firmware_async().
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_set_blocked_firmware_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean (G_TASK(res), error);
}

static void
fwupd_client_set_feature_flags_cb (GObject *source,
				   GAsyncResult *res,
				   gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_boolean (task, TRUE);
}

/**
 * fwupd_client_set_feature_flags_async:
 * @self: A #FwupdClient
 * @feature_flags: #FwupdFeatureFlags, e.g. %FWUPD_FEATURE_FLAG_UPDATE_TEXT
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Sets the features the client supports. This allows firmware to depend on
 * specific front-end features, for instance showing the user an image on
 * how to detach the hardware.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_set_feature_flags_async (FwupdClient *self,
				      FwupdFeatureFlags feature_flags,
				      GCancellable *cancellable,
				      GAsyncReadyCallback callback,
				      gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_dbus_proxy_call (priv->proxy, "SetFeatureFlags",
			   g_variant_new ("(t)", (guint64) feature_flags),
			   G_DBUS_CALL_FLAGS_NONE, -1,
			   cancellable,
			   fwupd_client_set_feature_flags_cb,
			   g_steal_pointer (&task));
}

/**
 * fwupd_client_set_feature_flags_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_set_feature_flags_async().
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_set_feature_flags_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean (G_TASK(res), error);
}

static void
fwupd_client_self_sign_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	gchar *str = NULL;
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_variant_get (val, "(s)", &str);
	g_task_return_pointer (task,
			       g_steal_pointer (&str),
			       (GDestroyNotify) g_free);
}

/**
 * fwupd_client_self_sign_async:
 * @self: A #FwupdClient
 * @value: A string to sign, typically a JSON blob
 * @flags: #FwupdSelfSignFlags, e.g. %FWUPD_SELF_SIGN_FLAG_ADD_TIMESTAMP
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Signs the data using the client self-signed certificate.
 *
 * You must have called fwupd_client_connect_async() on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_self_sign_async (FwupdClient *self,
			      const gchar *value,
			      FwupdSelfSignFlags flags,
			      GCancellable *cancellable,
			      GAsyncReadyCallback callback,
			      gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	GVariantBuilder builder;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (value != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* set options */
	g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
	if (flags & FWUPD_SELF_SIGN_FLAG_ADD_TIMESTAMP) {
		g_variant_builder_add (&builder, "{sv}",
				       "add-timestamp", g_variant_new_boolean (TRUE));
	}
	if (flags & FWUPD_SELF_SIGN_FLAG_ADD_CERT) {
		g_variant_builder_add (&builder, "{sv}",
				       "add-cert", g_variant_new_boolean (TRUE));
	}

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_dbus_proxy_call (priv->proxy,
			   "SelfSign",
			   g_variant_new ("(sa{sv})", value, &builder),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1, cancellable,
			   fwupd_client_self_sign_cb,
			   g_steal_pointer (&task));
}

/**
 * fwupd_client_self_sign_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_self_sign_async().
 *
 * Returns: a signature, or %NULL for failure
 *
 * Since: 1.5.0
 **/
gchar *
fwupd_client_self_sign_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), NULL);
	g_return_val_if_fail (g_task_is_valid (res, self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer (G_TASK(res), error);
}

static void
fwupd_client_modify_remote_cb (GObject *source,
			       GAsyncResult *res,
			       gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_boolean (task, TRUE);
}

/**
 * fwupd_client_modify_remote_async:
 * @self: A #FwupdClient
 * @remote_id: the remote ID, e.g. `lvfs-testing`
 * @key: the key, e.g. `Enabled`
 * @value: the key, e.g. `true`
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Modifies a system remote in a specific way.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_modify_remote_async (FwupdClient *self,
				  const gchar *remote_id,
				  const gchar *key,
				  const gchar *value,
				  GCancellable *cancellable,
				  GAsyncReadyCallback callback,
				  gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (remote_id != NULL);
	g_return_if_fail (key != NULL);
	g_return_if_fail (value != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_dbus_proxy_call (priv->proxy, "ModifyRemote",
			   g_variant_new ("(sss)", remote_id, key, value),
			   G_DBUS_CALL_FLAGS_NONE, -1,
			   cancellable,
			   fwupd_client_modify_remote_cb,
			   g_steal_pointer (&task));
}

/**
 * fwupd_client_modify_remote_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_modify_remote_async().
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_modify_remote_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean (G_TASK(res), error);
}

static void
fwupd_client_modify_device_cb (GObject *source,
			       GAsyncResult *res,
			       gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
	if (val == NULL) {
		fwupd_client_fixup_dbus_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* success */
	g_task_return_boolean (task, TRUE);
}

/**
 * fwupd_client_modify_device_async:
 * @self: A #FwupdClient
 * @device_id: the device ID
 * @key: the key, e.g. `Flags`
 * @value: the key, e.g. `reported`
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Modifies a device in a specific way. Not all properties on the #FwupdDevice
 * are settable by the client, and some may have other restrictions on @value.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_modify_device_async (FwupdClient *self,
				  const gchar *device_id,
				  const gchar *key,
				  const gchar *value,
				  GCancellable *cancellable,
				  GAsyncReadyCallback callback,
				  gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (device_id != NULL);
	g_return_if_fail (key != NULL);
	g_return_if_fail (value != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_dbus_proxy_call (priv->proxy, "ModifyDevice",
			   g_variant_new ("(sss)", device_id, key, value),
			   G_DBUS_CALL_FLAGS_NONE, -1,
			   cancellable,
			   fwupd_client_modify_device_cb,
			   g_steal_pointer (&task));
}

/**
 * fwupd_client_modify_device_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_modify_device_async().
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.0
 **/
gboolean
fwupd_client_modify_device_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean (G_TASK(res), error);
}

static FwupdRemote *
fwupd_client_get_remote_by_id_noref (GPtrArray *remotes, const gchar *remote_id)
{
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index (remotes, i);
		if (g_strcmp0 (remote_id, fwupd_remote_get_id (remote)) == 0)
			return remote;
	}
	return NULL;
}

static void
fwupd_client_get_remote_by_id_cb (GObject *source,
				  GAsyncResult *res,
				  gpointer user_data)
{
	FwupdRemote *remote_tmp;
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) remotes = NULL;
	const gchar *remote_id = g_task_get_task_data (task);

	remotes = fwupd_client_get_remotes_finish (FWUPD_CLIENT (source), res, &error);
	if (remotes == NULL) {
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	remote_tmp = fwupd_client_get_remote_by_id_noref (remotes, remote_id);
	if (remote_tmp == NULL) {
		g_task_return_new_error (task,
					 FWUPD_ERROR,
					 FWUPD_ERROR_NOT_FOUND,
					 "no remote '%s' found in search paths",
					 remote_id);
		return;
	}

	/* success */
	g_task_return_pointer (task,
			       g_object_ref (remote_tmp),
			       (GDestroyNotify) g_object_unref);
}

/**
 * fwupd_client_get_remote_by_id_async:
 * @self: A #FwupdClient
 * @remote_id: the remote ID, e.g. `lvfs-testing`
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Gets a specific remote that has been configured for the system.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_get_remote_by_id_async (FwupdClient *self,
				     const gchar *remote_id,
				     GCancellable *cancellable,
				     GAsyncReadyCallback callback,
				     gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (remote_id != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* call into daemon */
	task = g_task_new (self, cancellable, callback, callback_data);
	g_task_set_task_data (task, g_strdup (remote_id), g_free);
	fwupd_client_get_remotes_async (self, cancellable,
					fwupd_client_get_remote_by_id_cb,
					g_steal_pointer (&task));
}

/**
 * fwupd_client_get_remote_by_id_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_get_remote_by_id_async().
 *
 * Returns: (transfer full): a #FwupdRemote, or %NULL if not found
 *
 * Since: 1.5.0
 **/
FwupdRemote *
fwupd_client_get_remote_by_id_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), NULL);
	g_return_val_if_fail (g_task_is_valid (res, self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer (G_TASK(res), error);
}

/**
 * fwupd_client_set_user_agent:
 * @self: A #FwupdClient
 * @user_agent: the user agent ID, e.g. `gnome-software/3.34.1`
 *
 * Manually sets the user agent that is used for downloading. The user agent
 * should contain the runtime version of fwupd somewhere in the provided string.
 *
 * Since: 1.4.5
 **/
void
fwupd_client_set_user_agent (FwupdClient *self, const gchar *user_agent)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (user_agent != NULL);
	g_free (priv->user_agent);
	priv->user_agent = g_strdup (user_agent);
}

/**
 * fwupd_client_get_user_agent:
 * @self: A #FwupdClient
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
fwupd_client_get_user_agent (FwupdClient *self)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), NULL);
	return priv->user_agent;
}

/**
 * fwupd_client_set_user_agent_for_package:
 * @self: A #FwupdClient
 * @package_name: client program name, e.g. "gnome-software"
 * @package_version: client program version, e.g. "3.28.1"
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
fwupd_client_set_user_agent_for_package (FwupdClient *self,
					 const gchar *package_name,
					 const gchar *package_version)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	GString *str = g_string_new (NULL);
	g_autofree gchar *system = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (package_name != NULL);
	g_return_if_fail (package_version != NULL);

	/* application name and version */
	g_string_append_printf (str, "%s/%s", package_name, package_version);

	/* system information */
	system = fwupd_build_user_agent_system ();
	if (system != NULL)
		g_string_append_printf (str, " (%s)", system);

	/* platform, which in our case is just fwupd */
	if (g_strcmp0 (package_name, "fwupd") != 0)
		g_string_append_printf (str, " fwupd/%s", priv->daemon_version);

	/* success */
	g_free (priv->user_agent);
	priv->user_agent = g_string_free (str, FALSE);
}

static size_t
fwupd_client_download_write_callback_cb (char *ptr, size_t size, size_t nmemb, void *userdata)
{
	GByteArray *buf = (GByteArray *) userdata;
	gsize realsize = size * nmemb;
	g_byte_array_append (buf, (const guint8 *) ptr, realsize);
	return realsize;
}

static void
fwupd_client_download_bytes_thread_cb (GTask *task,
				       gpointer source_object,
				       gpointer task_data,
				       GCancellable *cancellable)
{
	FwupdClient *self = FWUPD_CLIENT (source_object);
	FwupdCurlHelper *helper = g_task_get_task_data (task);
	CURLcode res;
	gchar errbuf[CURL_ERROR_SIZE] = { '\0' };
	g_autoptr(GByteArray) buf = g_byte_array_new ();

	curl_easy_setopt (helper->curl, CURLOPT_ERRORBUFFER, errbuf);
	curl_easy_setopt (helper->curl, CURLOPT_WRITEFUNCTION, fwupd_client_download_write_callback_cb);
	curl_easy_setopt (helper->curl, CURLOPT_WRITEDATA, buf);
	res = curl_easy_perform (helper->curl);
	fwupd_client_set_status (self, FWUPD_STATUS_IDLE);
	if (res != CURLE_OK) {
		glong status_code = 0;
		curl_easy_getinfo (helper->curl, CURLINFO_RESPONSE_CODE, &status_code);
		g_debug ("status-code was %ld", status_code);
		if (status_code == 429) {
			g_task_return_new_error (task,
						 FWUPD_ERROR,
						 FWUPD_ERROR_INVALID_FILE,
						 "Failed to download due to server limit");
			return;
		}
		if (errbuf[0] != '\0') {
			g_task_return_new_error (task,
						 FWUPD_ERROR,
						 FWUPD_ERROR_INVALID_FILE,
						 "failed to download file: %s",
						 errbuf);
			return;
		}
		g_task_return_new_error (task,
					 FWUPD_ERROR,
					 FWUPD_ERROR_INVALID_FILE,
					 "failed to download file: %s",
					 curl_easy_strerror (res));

		return;
	}
	g_task_return_pointer (task,
			       g_byte_array_free_to_bytes (g_steal_pointer (&buf)),
			       (GDestroyNotify) g_bytes_unref);
}

/**
 * fwupd_client_download_bytes_async:
 * @self: A #FwupdClient
 * @url: the remote URL
 * @flags: #FwupdClientDownloadFlags, e.g. %FWUPD_CLIENT_DOWNLOAD_FLAG_NONE
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Downloads data from a remote server. The fwupd_client_set_user_agent() function
 * should be called before this method is used.
 *
 * You must have called fwupd_client_connect_async() on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_download_bytes_async (FwupdClient *self,
				   const gchar *url,
				   FwupdClientDownloadFlags flags,
				   GCancellable *cancellable,
				   GAsyncReadyCallback callback,
				   gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FwupdCurlHelper) helper = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (url != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* ensure networking set up */
	task = g_task_new (self, cancellable, callback, callback_data);
	helper = fwupd_client_curl_new (self, &error);
	if (helper == NULL) {
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}
	curl_easy_setopt (helper->curl, CURLOPT_URL, url);
	g_task_set_task_data (task, g_steal_pointer (&helper), (GDestroyNotify) fwupd_client_curl_helper_free);

	/* download data */
	g_debug ("downloading %s", url);
	fwupd_client_set_status (self, FWUPD_STATUS_DOWNLOADING);
	g_task_run_in_thread (task, fwupd_client_download_bytes_thread_cb);
}

/**
 * fwupd_client_download_bytes_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_download_bytes_async().
 *
 * Returns: (transfer full): downloaded data, or %NULL for error
 *
 * Since: 1.5.0
 **/
GBytes *
fwupd_client_download_bytes_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), NULL);
	g_return_val_if_fail (g_task_is_valid (res, self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer (G_TASK(res), error);
}

static void
fwupd_client_upload_bytes_thread_cb (GTask *task,
				     gpointer source_object,
				     gpointer task_data,
				     GCancellable *cancellable)
{
	FwupdClient *self = FWUPD_CLIENT (source_object);
	FwupdCurlHelper *helper = g_task_get_task_data (task);
	CURLcode res;
	gchar errbuf[CURL_ERROR_SIZE] = { '\0' };
	g_autoptr(GByteArray) buf = g_byte_array_new ();

	curl_easy_setopt (helper->curl, CURLOPT_ERRORBUFFER, errbuf);
	curl_easy_setopt (helper->curl, CURLOPT_WRITEFUNCTION, fwupd_client_download_write_callback_cb);
	curl_easy_setopt (helper->curl, CURLOPT_WRITEDATA, buf);
	res = curl_easy_perform (helper->curl);
	fwupd_client_set_status (self, FWUPD_STATUS_IDLE);
	if (res != CURLE_OK) {
		glong status_code = 0;
		curl_easy_getinfo (helper->curl, CURLINFO_RESPONSE_CODE, &status_code);
		g_debug ("status-code was %ld", status_code);
		if (errbuf[0] != '\0') {
			g_task_return_new_error (task,
						 FWUPD_ERROR,
						 FWUPD_ERROR_INVALID_FILE,
						 "failed to upload file: %s",
						 errbuf);
			return;
		}
		g_task_return_new_error (task,
					 FWUPD_ERROR,
					 FWUPD_ERROR_INVALID_FILE,
					 "failed to upload file: %s",
					 curl_easy_strerror (res));

		return;
	}
	g_task_return_pointer (task,
			       g_byte_array_free_to_bytes (g_steal_pointer (&buf)),
			       (GDestroyNotify) g_bytes_unref);
}

/**
 * fwupd_client_upload_bytes_async:
 * @self: A #FwupdClient
 * @url: the remote URL
 * @payload: payload string
 * @signature: (nullable): signature string
 * @flags: #FwupdClientDownloadFlags, e.g. %FWUPD_CLIENT_DOWNLOAD_FLAG_NONE
 * @cancellable: the #GCancellable, or %NULL
 * @callback: the function to run on completion
 * @callback_data: the data to pass to @callback
 *
 * Uploads data to a remote server. The fwupd_client_set_user_agent() function
 * should be called before this method is used.
 *
 * You must have called fwupd_client_connect_async() on @self before using
 * this method.
 *
 * Since: 1.5.0
 **/
void
fwupd_client_upload_bytes_async (FwupdClient *self,
				 const gchar *url,
				 const gchar *payload,
				 const gchar *signature,
				 FwupdClientUploadFlags flags,
				 GCancellable *cancellable,
				 GAsyncReadyCallback callback,
				 gpointer callback_data)
{
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTask) task = NULL;
	g_autoptr(FwupdCurlHelper) helper = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (self));
	g_return_if_fail (url != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (priv->proxy != NULL);

	/* ensure networking set up */
	task = g_task_new (self, cancellable, callback, callback_data);
	helper = fwupd_client_curl_new (self, &error);
	if (helper == NULL) {
		g_task_return_error (task, g_steal_pointer (&error));
		return;
	}

	/* build message */
	if ((flags & FWUPD_CLIENT_UPLOAD_FLAG_ALWAYS_MULTIPART) > 0 ||
	    signature != NULL) {
#ifdef HAVE_LIBCURL_7_56_0
		curl_mimepart *part;
		helper->mime = curl_mime_init (helper->curl);
		curl_easy_setopt (helper->curl, CURLOPT_MIMEPOST, helper->mime);
		part = curl_mime_addpart (helper->mime);
		curl_mime_data (part, payload, CURL_ZERO_TERMINATED);
		curl_mime_name (part, "payload");
		if (signature != NULL) {
			part = curl_mime_addpart (helper->mime);
			curl_mime_data (part, signature, CURL_ZERO_TERMINATED);
			curl_mime_name (part, "signature");
		}
#else
		g_task_return_new_error (task,
					 FWUPD_ERROR,
					 FWUPD_ERROR_INTERNAL,
					 "not supported as libcurl is too old");
		return;
#endif
	} else {
		helper->headers = curl_slist_append (helper->headers, "Content-Type: text/plain");
		curl_easy_setopt (helper->curl, CURLOPT_HTTPHEADER, helper->headers);
		curl_easy_setopt (helper->curl, CURLOPT_POST, 1L);
		curl_easy_setopt (helper->curl, CURLOPT_POSTFIELDSIZE, strlen (payload));
		curl_easy_setopt (helper->curl, CURLOPT_COPYPOSTFIELDS, payload);
	}

	fwupd_client_set_status (self, FWUPD_STATUS_IDLE);
	g_debug ("uploading to %s", url);
	curl_easy_setopt (helper->curl, CURLOPT_URL, url);
	g_task_set_task_data (task, g_steal_pointer (&helper), (GDestroyNotify) fwupd_client_curl_helper_free);
	g_task_run_in_thread (task, fwupd_client_upload_bytes_thread_cb);
}

/**
 * fwupd_client_upload_bytes_finish:
 * @self: A #FwupdClient
 * @res: the #GAsyncResult
 * @error: the #GError, or %NULL
 *
 * Gets the result of fwupd_client_upload_bytes_async().
 *
 * Returns: (transfer full): response data, or %NULL for error
 *
 * Since: 1.5.0
 **/
GBytes *
fwupd_client_upload_bytes_finish (FwupdClient *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (FWUPD_IS_CLIENT (self), NULL);
	g_return_val_if_fail (g_task_is_valid (res, self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer (G_TASK(res), error);
}

#ifdef SOUP_SESSION_COMPAT
/* this is bad; we dlopen libsoup-2.4.so.1 and get the gtype manually
 * to avoid deps on both libcurl and libsoup whilst preserving ABI */
static void
fwupd_client_ensure_soup_session (FwupdClient *self)
{
	FwupdClientObjectNewFunc func = NULL;
	FwupdClientPrivate *priv = GET_PRIVATE (self);
	GType soup_gtype;

	/* already set up */
	if (priv->soup_session != NULL)
		return;

	/* known GType, just create */
	soup_gtype = g_type_from_name ("SoupSession");
	if (soup_gtype != 0) {
		priv->soup_session = g_object_new (soup_gtype, NULL);
		return;
	}

	/* load the library at runtime, leaking the module */
	if (priv->soup_module == NULL) {
		g_autofree gchar *fn = NULL;
		fn = g_build_filename (FWUPD_LIBDIR, "libsoup-2.4.so.1", NULL);
		priv->soup_module = g_module_open (fn, G_MODULE_BIND_LAZY);
		if (priv->soup_module == NULL) {
			g_warning ("failed to find libsoup library");
			return;
		}
	}
	if (!g_module_symbol (priv->soup_module,
			      "soup_session_new",
			      (gpointer *) &func)) {
		g_warning ("failed to find soup_session_get_type()");
		g_module_close (priv->soup_module);
		priv->soup_module = NULL;
		return;
	}
	priv->soup_session = func ();
	g_object_set (priv->soup_session, "timeout", (guint) 60, NULL);
}
#endif

static void
fwupd_client_get_property (GObject *object, guint prop_id,
			   GValue *value, GParamSpec *pspec)
{
	FwupdClient *self = FWUPD_CLIENT (object);
	FwupdClientPrivate *priv = GET_PRIVATE (self);

	switch (prop_id) {
	case PROP_STATUS:
		g_value_set_uint (value, priv->status);
		break;
	case PROP_TAINTED:
		g_value_set_boolean (value, priv->tainted);
		break;
	case PROP_SOUP_SESSION:
#ifdef SOUP_SESSION_COMPAT
		fwupd_client_ensure_soup_session (self);
		g_value_set_object (value, priv->soup_session);
#else
		g_value_set_object (value, NULL);
#endif
		break;
	case PROP_PERCENTAGE:
		g_value_set_uint (value, priv->percentage);
		break;
	case PROP_DAEMON_VERSION:
		g_value_set_string (value, priv->daemon_version);
		break;
	case PROP_HOST_PRODUCT:
		g_value_set_string (value, priv->host_product);
		break;
	case PROP_HOST_MACHINE_ID:
		g_value_set_string (value, priv->host_machine_id);
		break;
	case PROP_HOST_SECURITY_ID:
		g_value_set_string (value, priv->host_security_id);
		break;
	case PROP_INTERACTIVE:
		g_value_set_boolean (value, priv->interactive);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fwupd_client_set_property (GObject *object, guint prop_id,
			   const GValue *value, GParamSpec *pspec)
{
	FwupdClient *self = FWUPD_CLIENT (object);
	FwupdClientPrivate *priv = GET_PRIVATE (self);

	switch (prop_id) {
	case PROP_STATUS:
		priv->status = g_value_get_uint (value);
		break;
	case PROP_PERCENTAGE:
		priv->percentage = g_value_get_uint (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fwupd_client_class_init (FwupdClientClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
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
	signals [SIGNAL_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FwupdClientClass, changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

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
	signals [SIGNAL_STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FwupdClientClass, status_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

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
	signals [SIGNAL_DEVICE_ADDED] =
		g_signal_new ("device-added",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FwupdClientClass, device_added),
			      NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 1, FWUPD_TYPE_DEVICE);

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
	signals [SIGNAL_DEVICE_REMOVED] =
		g_signal_new ("device-removed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FwupdClientClass, device_removed),
			      NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 1, FWUPD_TYPE_DEVICE);

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
	signals [SIGNAL_DEVICE_CHANGED] =
		g_signal_new ("device-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FwupdClientClass, device_changed),
			      NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 1, FWUPD_TYPE_DEVICE);

	/**
	 * FwupdClient:status:
	 *
	 * The last-reported status of the daemon.
	 *
	 * Since: 0.7.0
	 */
	pspec = g_param_spec_uint ("status", NULL, NULL,
				   0, FWUPD_STATUS_LAST, FWUPD_STATUS_UNKNOWN,
				   G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_STATUS, pspec);

	/**
	 * FwupdClient:tainted:
	 *
	 * If the daemon is tainted by 3rd party code.
	 *
	 * Since: 1.2.4
	 */
	pspec = g_param_spec_boolean ("tainted", NULL, NULL, FALSE,
				      G_PARAM_READABLE | G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_TAINTED, pspec);

	/**
	 * FwupdClient:interactive:
	 *
	 * If the daemon is running in an interactive terminal
	 *
	 * Since: 1.3.4
	 */
	pspec = g_param_spec_boolean ("interactive", NULL, NULL, FALSE,
				      G_PARAM_READABLE | G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_INTERACTIVE, pspec);

	/**
	 * FwupdClient:percentage:
	 *
	 * The last-reported percentage of the daemon.
	 *
	 * Since: 0.7.3
	 */
	pspec = g_param_spec_uint ("percentage", NULL, NULL,
				   0, 100, 0,
				   G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_PERCENTAGE, pspec);

	/**
	 * FwupdClient:daemon-version:
	 *
	 * The daemon version number.
	 *
	 * Since: 0.9.6
	 */
	pspec = g_param_spec_string ("daemon-version", NULL, NULL,
				     NULL, G_PARAM_READABLE | G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_DAEMON_VERSION, pspec);

	/**
	 * FwupdClient:soup-session:
	 *
	 * The libsoup session, now unused.
	 *
	 * Since: 1.4.5
	 */
	pspec = g_param_spec_object ("soup-session", NULL, NULL, G_TYPE_OBJECT,
				     G_PARAM_READABLE | G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_SOUP_SESSION, pspec);

	/**
	 * FwupdClient:host-product:
	 *
	 * The host product string
	 *
	 * Since: 1.3.1
	 */
	pspec = g_param_spec_string ("host-product", NULL, NULL,
				     NULL, G_PARAM_READABLE | G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_HOST_PRODUCT, pspec);

	/**
	 * FwupdClient:host-machine-id:
	 *
	 * The host machine-id string
	 *
	 * Since: 1.3.2
	 */
	pspec = g_param_spec_string ("host-machine-id", NULL, NULL,
				     NULL, G_PARAM_READABLE | G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_HOST_MACHINE_ID, pspec);

	/**
	 * FwupdClient:host-security-id:
	 *
	 * The host machine-id string
	 *
	 * Since: 1.5.0
	 */
	pspec = g_param_spec_string ("host-security-id", NULL, NULL,
				     NULL, G_PARAM_READABLE | G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_HOST_SECURITY_ID, pspec);
}

static void
fwupd_client_init (FwupdClient *self)
{
}

static void
fwupd_client_finalize (GObject *object)
{
	FwupdClient *self = FWUPD_CLIENT (object);
	FwupdClientPrivate *priv = GET_PRIVATE (self);

	g_clear_pointer (&priv->main_ctx, g_main_context_unref);
	g_free (priv->user_agent);
	g_free (priv->daemon_version);
	g_free (priv->host_product);
	g_free (priv->host_machine_id);
	g_free (priv->host_security_id);
	if (priv->conn != NULL)
		g_object_unref (priv->conn);
	if (priv->proxy != NULL)
		g_object_unref (priv->proxy);
#ifdef SOUP_SESSION_COMPAT
	if (priv->soup_session != NULL)
		g_object_unref (priv->soup_session);
#endif

	G_OBJECT_CLASS (fwupd_client_parent_class)->finalize (object);
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
fwupd_client_new (void)
{
	FwupdClient *self;
	self = g_object_new (FWUPD_TYPE_CLIENT, NULL);
	return FWUPD_CLIENT (self);
}
