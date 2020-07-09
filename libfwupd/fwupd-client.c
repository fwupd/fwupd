/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <glib-object.h>
#include <gio/gio.h>
#include <libsoup/soup.h>
#ifdef HAVE_GIO_UNIX
#include <gio/gunixfdlist.h>
#endif

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "fwupd-client.h"
#include "fwupd-common-private.h"
#include "fwupd-deprecated.h"
#include "fwupd-enums.h"
#include "fwupd-error.h"
#include "fwupd-device-private.h"
#include "fwupd-release-private.h"
#include "fwupd-remote-private.h"

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
	FwupdStatus			 status;
	gboolean			 tainted;
	gboolean			 interactive;
	guint				 percentage;
	gchar				*daemon_version;
	gchar				*host_product;
	gchar				*host_machine_id;
	GDBusConnection			*conn;
	GDBusProxy			*proxy;
	SoupSession			*soup_session;
	gchar				*user_agent;
} FwupdClientPrivate;

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
	PROP_SOUP_SESSION,
	PROP_HOST_PRODUCT,
	PROP_HOST_MACHINE_ID,
	PROP_INTERACTIVE,
	PROP_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (FwupdClient, fwupd_client, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fwupd_client_get_instance_private (o))

typedef struct {
	gboolean	 ret;
	GError		*error;
	GMainLoop	*loop;
	GVariant	*val;
	GDBusMessage	*message;
} FwupdClientHelper;

static void
fwupd_client_helper_free (FwupdClientHelper *helper)
{
	if (helper->message != NULL)
		g_object_unref (helper->message);
	if (helper->val != NULL)
		g_variant_unref (helper->val);
	if (helper->error != NULL)
		g_error_free (helper->error);
	g_main_loop_unref (helper->loop);
	g_free (helper);
}

static FwupdClientHelper *
fwupd_client_helper_new (void)
{
	FwupdClientHelper *helper;
	helper = g_new0 (FwupdClientHelper, 1);
	helper->loop = g_main_loop_new (NULL, FALSE);
	return helper;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FwupdClientHelper, fwupd_client_helper_free)
#pragma clang diagnostic pop

static void
fwupd_client_set_host_product (FwupdClient *client, const gchar *host_product)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_free (priv->host_product);
	priv->host_product = g_strdup (host_product);
	g_object_notify (G_OBJECT (client), "host-product");
}

static void
fwupd_client_set_host_machine_id (FwupdClient *client, const gchar *host_machine_id)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_free (priv->host_machine_id);
	priv->host_machine_id = g_strdup (host_machine_id);
	g_object_notify (G_OBJECT (client), "host-machine-id");
}

static void
fwupd_client_set_daemon_version (FwupdClient *client, const gchar *daemon_version)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_free (priv->daemon_version);
	priv->daemon_version = g_strdup (daemon_version);
	g_object_notify (G_OBJECT (client), "daemon-version");
}

static void
fwupd_client_set_status (FwupdClient *client, FwupdStatus status)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	if (priv->status == status)
		return;
	priv->status = status;
	g_debug ("Emitting ::status-changed() [%s]",
		 fwupd_status_to_string (priv->status));
	g_signal_emit (client, signals[SIGNAL_STATUS_CHANGED], 0, priv->status);
	g_object_notify (G_OBJECT (client), "status");
}

static void
fwupd_client_set_percentage (FwupdClient *client, guint percentage)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	if (priv->percentage == percentage)
		return;
	priv->percentage = percentage;
	g_object_notify (G_OBJECT (client), "percentage");
}

static void
fwupd_client_properties_changed_cb (GDBusProxy *proxy,
				    GVariant *changed_properties,
				    GStrv invalidated_properties,
				    FwupdClient *client)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(GVariantDict) dict = NULL;

	/* print to the console */
	dict = g_variant_dict_new (changed_properties);
	if (g_variant_dict_contains (dict, "Status")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property (proxy, "Status");
		if (val != NULL)
			fwupd_client_set_status (client, g_variant_get_uint32 (val));
	}
	if (g_variant_dict_contains (dict, "Tainted")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property (proxy, "Tainted");
		if (val != NULL) {
			priv->tainted = g_variant_get_boolean (val);
			g_object_notify (G_OBJECT (client), "tainted");
		}
	}
	if (g_variant_dict_contains (dict, "Interactive")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property (proxy, "Interactive");
		if (val != NULL) {
			priv->interactive = g_variant_get_boolean (val);
			g_object_notify (G_OBJECT (client), "interactive");
		}
	}
	if (g_variant_dict_contains (dict, "Percentage")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property (proxy, "Percentage");
		if (val != NULL)
			fwupd_client_set_percentage (client, g_variant_get_uint32 (val));
	}
	if (g_variant_dict_contains (dict, "DaemonVersion")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property (proxy, "DaemonVersion");
		if (val != NULL)
			fwupd_client_set_daemon_version (client, g_variant_get_string (val, NULL));
	}
	if (g_variant_dict_contains (dict, "HostProduct")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property (proxy, "HostProduct");
		if (val != NULL)
			fwupd_client_set_host_product (client, g_variant_get_string (val, NULL));
	}
	if (g_variant_dict_contains (dict, "HostMachineId")) {
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property (proxy, "HostMachineId");
		if (val != NULL)
			fwupd_client_set_host_machine_id (client, g_variant_get_string (val, NULL));
	}
}

static void
fwupd_client_signal_cb (GDBusProxy *proxy,
			const gchar *sender_name,
			const gchar *signal_name,
			GVariant *parameters,
			FwupdClient *client)
{
	g_autoptr(FwupdDevice) dev = NULL;
	if (g_strcmp0 (signal_name, "Changed") == 0) {
		g_debug ("Emitting ::changed()");
		g_signal_emit (client, signals[SIGNAL_CHANGED], 0);
		return;
	}
	if (g_strcmp0 (signal_name, "DeviceAdded") == 0) {
		dev = fwupd_device_from_variant (parameters);
		g_debug ("Emitting ::device-added(%s)",
			 fwupd_device_get_id (dev));
		g_signal_emit (client, signals[SIGNAL_DEVICE_ADDED], 0, dev);
		return;
	}
	if (g_strcmp0 (signal_name, "DeviceRemoved") == 0) {
		dev = fwupd_device_from_variant (parameters);
		g_signal_emit (client, signals[SIGNAL_DEVICE_REMOVED], 0, dev);
		g_debug ("Emitting ::device-removed(%s)",
			 fwupd_device_get_id (dev));
		return;
	}
	if (g_strcmp0 (signal_name, "DeviceChanged") == 0) {
		dev = fwupd_device_from_variant (parameters);
		g_signal_emit (client, signals[SIGNAL_DEVICE_CHANGED], 0, dev);
		g_debug ("Emitting ::device-changed(%s)",
			 fwupd_device_get_id (dev));
		return;
	}
	g_debug ("Unknown signal name '%s' from %s", signal_name, sender_name);
}

static gboolean
fwupd_client_ensure_networking (FwupdClient *client, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	const gchar *http_proxy;
	g_autoptr(SoupSession) session = NULL;

	/* already exists */
	if (priv->soup_session != NULL)
		return TRUE;

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

	/* create the soup session */
	session = soup_session_new_with_options (SOUP_SESSION_USER_AGENT, priv->user_agent,
						 SOUP_SESSION_TIMEOUT, 60,
						 NULL);
	if (session == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "failed to setup networking");
		return FALSE;
	}

	/* relax the SSL checks for broken corporate proxies */
	if (g_getenv ("DISABLE_SSL_STRICT") != NULL)
		g_object_set (session, SOUP_SESSION_SSL_STRICT, FALSE, NULL);

	/* set the proxy */
	http_proxy = g_getenv ("https_proxy");
	if (http_proxy == NULL)
		http_proxy = g_getenv ("HTTPS_PROXY");
	if (http_proxy == NULL)
		http_proxy = g_getenv ("http_proxy");
	if (http_proxy == NULL)
		http_proxy = g_getenv ("HTTP_PROXY");
	if (http_proxy != NULL && strlen (http_proxy) > 0) {
		g_autoptr(SoupURI) proxy_uri = soup_uri_new (http_proxy);
		if (proxy_uri == NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid proxy URI: %s", http_proxy);
			return FALSE;
		}
		g_object_set (session, SOUP_SESSION_PROXY_URI, proxy_uri, NULL);
	}

	/* this disables the double-compression of the firmware.xml.gz file */
	soup_session_remove_feature_by_type (session, SOUP_TYPE_CONTENT_DECODER);
	priv->soup_session = g_steal_pointer (&session);
	return TRUE;
}

/**
 * fwupd_client_connect:
 * @client: A #FwupdClient
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Sets up the client ready for use. Most other methods call this
 * for you, and do you only need to call this if you are just watching
 * the client.
 *
 * Returns: %TRUE for success
 *
 * Since: 0.7.1
 **/
gboolean
fwupd_client_connect (FwupdClient *client, GCancellable *cancellable, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(GVariant) val = NULL;
	g_autoptr(GVariant) val2 = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* nothing to do */
	if (priv->proxy != NULL)
		return TRUE;

	/* connect to the daemon */
	priv->conn = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
	if (priv->conn == NULL) {
		g_prefix_error (error, "Failed to connect to system D-Bus: ");
		return FALSE;
	}
	priv->proxy = g_dbus_proxy_new_sync (priv->conn,
					     G_DBUS_PROXY_FLAGS_NONE,
					     NULL,
					     FWUPD_DBUS_SERVICE,
					     FWUPD_DBUS_PATH,
					     FWUPD_DBUS_INTERFACE,
					     NULL,
					     error);
	if (priv->proxy == NULL)
		return FALSE;
	g_signal_connect (priv->proxy, "g-properties-changed",
			  G_CALLBACK (fwupd_client_properties_changed_cb), client);
	g_signal_connect (priv->proxy, "g-signal",
			  G_CALLBACK (fwupd_client_signal_cb), client);
	val = g_dbus_proxy_get_cached_property (priv->proxy, "DaemonVersion");
	if (val != NULL)
		fwupd_client_set_daemon_version (client, g_variant_get_string (val, NULL));
	val2 = g_dbus_proxy_get_cached_property (priv->proxy, "Tainted");
	if (val2 != NULL)
		priv->tainted = g_variant_get_boolean (val2);
	val2 = g_dbus_proxy_get_cached_property (priv->proxy, "Interactive");
	if (val2 != NULL)
		priv->interactive = g_variant_get_boolean (val2);
	val = g_dbus_proxy_get_cached_property (priv->proxy, "HostProduct");
	if (val != NULL)
		fwupd_client_set_host_product (client, g_variant_get_string (val, NULL));
	val = g_dbus_proxy_get_cached_property (priv->proxy, "HostMachineId");
	if (val != NULL)
		fwupd_client_set_host_machine_id (client, g_variant_get_string (val, NULL));

	return TRUE;
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

/**
 * fwupd_client_get_devices:
 * @client: A #FwupdClient
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Gets all the devices registered with the daemon.
 *
 * Returns: (element-type FwupdDevice) (transfer container): results
 *
 * Since: 0.9.2
 **/
GPtrArray *
fwupd_client_get_devices (FwupdClient *client, GCancellable *cancellable, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return NULL;

	/* call into daemon */
	val = g_dbus_proxy_call_sync (priv->proxy,
				      "GetDevices",
				      NULL,
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      cancellable,
				      error);
	if (val == NULL) {
		if (error != NULL)
			fwupd_client_fixup_dbus_error (*error);
		return NULL;
	}
	return fwupd_device_array_from_variant (val);
}

/**
 * fwupd_client_get_history:
 * @client: A #FwupdClient
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Gets all the history.
 *
 * Returns: (element-type FwupdDevice) (transfer container): results
 *
 * Since: 1.0.4
 **/
GPtrArray *
fwupd_client_get_history (FwupdClient *client, GCancellable *cancellable, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return NULL;

	/* call into daemon */
	val = g_dbus_proxy_call_sync (priv->proxy,
				      "GetHistory",
				      NULL,
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      cancellable,
				      error);
	if (val == NULL) {
		if (error != NULL)
			fwupd_client_fixup_dbus_error (*error);
		return NULL;
	}
	return fwupd_device_array_from_variant (val);
}

/**
 * fwupd_client_get_device_by_id:
 * @client: A #FwupdClient
 * @device_id: the device ID, e.g. `usb:00:01:03:03`
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Gets a device by it's device ID.
 *
 * Returns: (transfer full): a #FwupdDevice or %NULL
 *
 * Since: 0.9.3
 **/
FwupdDevice *
fwupd_client_get_device_by_id (FwupdClient *client,
			       const gchar *device_id,
			       GCancellable *cancellable,
			       GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), NULL);
	g_return_val_if_fail (device_id != NULL, NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* get all the devices */
	devices = fwupd_client_get_devices (client, cancellable, error);
	if (devices == NULL)
		return NULL;

	/* find the device by ID (client side) */
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (devices, i);
		if (g_strcmp0 (fwupd_device_get_id (dev), device_id) == 0)
			return g_object_ref (dev);
	}
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_FOUND,
		     "failed to find %s", device_id);
	return NULL;
}

/**
 * fwupd_client_get_devices_by_guid:
 * @client: A #FwupdClient
 * @guid: the GUID, e.g. `e22c4520-43dc-5bb3-8245-5787fead9b63`
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Gets any devices that provide a specific GUID. An error is returned if no
 * devices contains this GUID.
 *
 * Returns: (element-type FwupdDevice) (transfer container): devices or %NULL
 *
 * Since: 1.4.1
 **/
GPtrArray *
fwupd_client_get_devices_by_guid (FwupdClient *client,
				  const gchar *guid,
				  GCancellable *cancellable,
				  GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) devices_tmp = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), NULL);
	g_return_val_if_fail (guid != NULL, NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* get all the devices */
	devices_tmp = fwupd_client_get_devices (client, cancellable, error);
	if (devices_tmp == NULL)
		return NULL;

	/* find the devices by GUID (client side) */
	devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (guint i = 0; i < devices_tmp->len; i++) {
		FwupdDevice *dev_tmp = g_ptr_array_index (devices_tmp, i);
		if (fwupd_device_has_guid (dev_tmp, guid))
			g_ptr_array_add (devices, g_object_ref (dev_tmp));
	}

	/* nothing */
	if (devices->len == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "failed to find any device providing %s", guid);
		return NULL;
	}

	/* success */
	return g_steal_pointer (&devices);
}

/**
 * fwupd_client_get_releases:
 * @client: A #FwupdClient
 * @device_id: the device ID
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Gets all the releases for a specific device
 *
 * Returns: (element-type FwupdRelease) (transfer container): results
 *
 * Since: 0.9.3
 **/
GPtrArray *
fwupd_client_get_releases (FwupdClient *client, const gchar *device_id,
			   GCancellable *cancellable, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), NULL);
	g_return_val_if_fail (device_id != NULL, NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return NULL;

	/* call into daemon */
	val = g_dbus_proxy_call_sync (priv->proxy,
				      "GetReleases",
				      g_variant_new ("(s)", device_id),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      cancellable,
				      error);
	if (val == NULL) {
		if (error != NULL)
			fwupd_client_fixup_dbus_error (*error);
		return NULL;
	}
	return fwupd_release_array_from_variant (val);
}

/**
 * fwupd_client_get_downgrades:
 * @client: A #FwupdClient
 * @device_id: the device ID
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Gets all the downgrades for a specific device.
 *
 * Returns: (element-type FwupdRelease) (transfer container): results
 *
 * Since: 0.9.8
 **/
GPtrArray *
fwupd_client_get_downgrades (FwupdClient *client, const gchar *device_id,
			     GCancellable *cancellable, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), NULL);
	g_return_val_if_fail (device_id != NULL, NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return NULL;

	/* call into daemon */
	val = g_dbus_proxy_call_sync (priv->proxy,
				      "GetDowngrades",
				      g_variant_new ("(s)", device_id),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      cancellable,
				      error);
	if (val == NULL) {
		if (error != NULL)
			fwupd_client_fixup_dbus_error (*error);
		return NULL;
	}
	return fwupd_release_array_from_variant (val);
}

/**
 * fwupd_client_get_upgrades:
 * @client: A #FwupdClient
 * @device_id: the device ID
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Gets all the upgrades for a specific device.
 *
 * Returns: (element-type FwupdRelease) (transfer container): results
 *
 * Since: 0.9.8
 **/
GPtrArray *
fwupd_client_get_upgrades (FwupdClient *client, const gchar *device_id,
			   GCancellable *cancellable, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), NULL);
	g_return_val_if_fail (device_id != NULL, NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return NULL;

	/* call into daemon */
	val = g_dbus_proxy_call_sync (priv->proxy,
				      "GetUpgrades",
				      g_variant_new ("(s)", device_id),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      cancellable,
				      error);
	if (val == NULL) {
		if (error != NULL)
			fwupd_client_fixup_dbus_error (*error);
		return NULL;
	}
	return fwupd_release_array_from_variant (val);
}

static void
fwupd_client_proxy_call_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *) user_data;
	helper->val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source),
						res, &helper->error);
	if (helper->val != NULL)
		helper->ret = TRUE;
	if (helper->error != NULL)
		fwupd_client_fixup_dbus_error (helper->error);
	g_main_loop_quit (helper->loop);
}

/**
 * fwupd_client_modify_config
 * @client: A #FwupdClient
 * @key: key, e.g. `BlacklistPlugins`
 * @value: value, e.g. `*`
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Modifies a daemon config option.
 * The daemon will only respond to this request with proper permissions
 *
 * Returns: %TRUE for success
 *
 * Since: 1.2.8
 **/
gboolean
fwupd_client_modify_config (FwupdClient *client, const gchar *key, const gchar *value,
			    GCancellable *cancellable, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return FALSE;

	/* call into daemon */
	helper = fwupd_client_helper_new ();
	g_dbus_proxy_call (priv->proxy,
			   "ModifyConfig",
			   g_variant_new ("(ss)", key, value),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   cancellable,
			   fwupd_client_proxy_call_cb,
			   helper);
	g_main_loop_run (helper->loop);
	if (!helper->ret) {
		g_propagate_error (error, helper->error);
		helper->error = NULL;
		return FALSE;
	}
	return TRUE;
}

/**
 * fwupd_client_activate:
 * @client: A #FwupdClient
 * @cancellable: the #GCancellable, or %NULL
 * @device_id: a device
 * @error: the #GError, or %NULL
 *
 * Activates up a device, which normally means the device switches to a new
 * firmware version. This should only be called when data loss cannot occur.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.2.6
 **/
gboolean
fwupd_client_activate (FwupdClient *client, GCancellable *cancellable,
		       const gchar *device_id, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (device_id != NULL, FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return FALSE;

	/* call into daemon */
	helper = fwupd_client_helper_new ();
	g_dbus_proxy_call (priv->proxy,
			   "Activate",
			   g_variant_new ("(s)", device_id),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   cancellable,
			   fwupd_client_proxy_call_cb,
			   helper);
	g_main_loop_run (helper->loop);
	if (!helper->ret) {
		g_propagate_error (error, helper->error);
		helper->error = NULL;
		return FALSE;
	}
	return TRUE;
}

/**
 * fwupd_client_verify:
 * @client: A #FwupdClient
 * @device_id: the device ID
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Verify a specific device.
 *
 * Returns: %TRUE for verification success
 *
 * Since: 0.7.0
 **/
gboolean
fwupd_client_verify (FwupdClient *client, const gchar *device_id,
		     GCancellable *cancellable, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (device_id != NULL, FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return FALSE;

	/* call into daemon */
	helper = fwupd_client_helper_new ();
	g_dbus_proxy_call (priv->proxy,
			   "Verify",
			   g_variant_new ("(s)", device_id),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   cancellable,
			   fwupd_client_proxy_call_cb,
			   helper);
	g_main_loop_run (helper->loop);
	if (!helper->ret) {
		g_propagate_error (error, helper->error);
		helper->error = NULL;
		return FALSE;
	}
	return TRUE;
}

/**
 * fwupd_client_verify_update:
 * @client: A #FwupdClient
 * @device_id: the device ID
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Update the verification record for a specific device.
 *
 * Returns: %TRUE for verification success
 *
 * Since: 0.8.0
 **/
gboolean
fwupd_client_verify_update (FwupdClient *client, const gchar *device_id,
		     GCancellable *cancellable, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (device_id != NULL, FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return FALSE;

	/* call into daemon */
	helper = fwupd_client_helper_new ();
	g_dbus_proxy_call (priv->proxy,
			   "VerifyUpdate",
			   g_variant_new ("(s)", device_id),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   cancellable,
			   fwupd_client_proxy_call_cb,
			   helper);
	g_main_loop_run (helper->loop);
	if (!helper->ret) {
		g_propagate_error (error, helper->error);
		helper->error = NULL;
		return FALSE;
	}
	return TRUE;
}

/**
 * fwupd_client_unlock:
 * @client: A #FwupdClient
 * @device_id: the device ID
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Unlocks a specific device so firmware can be read or wrote.
 *
 * Returns: %TRUE for success
 *
 * Since: 0.7.0
 **/
gboolean
fwupd_client_unlock (FwupdClient *client, const gchar *device_id,
		     GCancellable *cancellable, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (device_id != NULL, FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return FALSE;

	/* call into daemon */
	helper = fwupd_client_helper_new ();
	g_dbus_proxy_call (priv->proxy,
			   "Unlock",
			   g_variant_new ("(s)", device_id),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   cancellable,
			   fwupd_client_proxy_call_cb,
			   helper);
	g_main_loop_run (helper->loop);
	if (!helper->ret) {
		g_propagate_error (error, helper->error);
		helper->error = NULL;
		return FALSE;
	}
	return TRUE;
}

/**
 * fwupd_client_clear_results:
 * @client: A #FwupdClient
 * @device_id: the device ID
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Clears the results for a specific device.
 *
 * Returns: %TRUE for success
 *
 * Since: 0.7.0
 **/
gboolean
fwupd_client_clear_results (FwupdClient *client, const gchar *device_id,
			    GCancellable *cancellable, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (device_id != NULL, FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return FALSE;

	/* call into daemon */
	helper = fwupd_client_helper_new ();
	g_dbus_proxy_call (priv->proxy,
			   "ClearResults",
			   g_variant_new ("(s)", device_id),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   cancellable,
			   fwupd_client_proxy_call_cb,
			   helper);
	g_main_loop_run (helper->loop);
	if (!helper->ret) {
		g_propagate_error (error, helper->error);
		helper->error = NULL;
		return FALSE;
	}
	return TRUE;
}

/**
 * fwupd_client_get_results:
 * @client: A #FwupdClient
 * @device_id: the device ID
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Gets the results of a previous firmware update for a specific device.
 *
 * Returns: (transfer full): a #FwupdDevice, or %NULL for failure
 *
 * Since: 0.7.0
 **/
FwupdDevice *
fwupd_client_get_results (FwupdClient *client, const gchar *device_id,
			  GCancellable *cancellable, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), NULL);
	g_return_val_if_fail (device_id != NULL, NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return NULL;

	/* call into daemon */
	helper = fwupd_client_helper_new ();
	g_dbus_proxy_call (priv->proxy,
			   "GetResults",
			   g_variant_new ("(s)", device_id),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   cancellable,
			   fwupd_client_proxy_call_cb,
			   helper);
	g_main_loop_run (helper->loop);
	if (!helper->ret) {
		g_propagate_error (error, helper->error);
		helper->error = NULL;
		return NULL;
	}
	return fwupd_device_from_variant (helper->val);
}

#ifdef HAVE_GIO_UNIX
static void
fwupd_client_send_message_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *) user_data;
	GDBusConnection *con = G_DBUS_CONNECTION (source_object);
	helper->message = g_dbus_connection_send_message_with_reply_finish (con, res,
									    &helper->error);
	if (helper->message &&
	    !g_dbus_message_to_gerror (helper->message, &helper->error)) {
		helper->ret = TRUE;
		helper->val = g_dbus_message_get_body (helper->message);
		if (helper->val != NULL)
			g_variant_ref (helper->val);
	}
	if (helper->error != NULL)
		fwupd_client_fixup_dbus_error (helper->error);
	g_main_loop_quit (helper->loop);
}
#endif

#ifdef HAVE_GIO_UNIX
static gboolean
fwupd_client_install_fd (FwupdClient *client,
			 const gchar *device_id,
			 GUnixInputStream *istr,
			 const gchar *filename_hint,
			 FwupdInstallFlags install_flags,
			 GCancellable *cancellable,
			 GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	GVariant *body;
	GVariantBuilder builder;
	gint retval;
	g_autoptr(FwupdClientHelper) helper = NULL;
	g_autoptr(GDBusMessage) request = NULL;
	g_autoptr(GUnixFDList) fd_list = NULL;

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
	if (install_flags & FWUPD_INSTALL_FLAG_FORCE) {
		g_variant_builder_add (&builder, "{sv}",
				       "force", g_variant_new_boolean (TRUE));
	}
	if (install_flags & FWUPD_INSTALL_FLAG_NO_HISTORY) {
		g_variant_builder_add (&builder, "{sv}",
				       "no-history", g_variant_new_boolean (TRUE));
	}

	/* set out of band file descriptor */
	fd_list = g_unix_fd_list_new ();
	retval = g_unix_fd_list_append (fd_list, g_unix_input_stream_get_fd (istr), NULL);
	g_assert (retval != -1);
	request = g_dbus_message_new_method_call (FWUPD_DBUS_SERVICE,
						  FWUPD_DBUS_PATH,
						  FWUPD_DBUS_INTERFACE,
						  "Install");
	g_dbus_message_set_unix_fd_list (request, fd_list);

	/* call into daemon */
	helper = fwupd_client_helper_new ();
	body = g_variant_new ("(sha{sv})", device_id, g_unix_input_stream_get_fd (istr), &builder);
	g_dbus_message_set_body (request, body);
	g_dbus_connection_send_message_with_reply (priv->conn,
						   request,
						   G_DBUS_SEND_MESSAGE_FLAGS_NONE,
						   G_MAXINT,
						   NULL,
						   cancellable,
						   fwupd_client_send_message_cb,
						   helper);
	g_main_loop_run (helper->loop);
	if (!helper->ret) {
		g_propagate_error (error, helper->error);
		helper->error = NULL;
		return FALSE;
	}
	return TRUE;
}
#endif

/**
 * fwupd_client_install_bytes:
 * @client: A #FwupdClient
 * @device_id: the device ID
 * @bytes: #GBytes
 * @install_flags: the #FwupdInstallFlags, e.g. %FWUPD_INSTALL_FLAG_ALLOW_REINSTALL
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Install firmware onto a specific device.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.5
 **/
gboolean
fwupd_client_install_bytes (FwupdClient *client,
			    const gchar *device_id,
			    GBytes *bytes,
			    FwupdInstallFlags install_flags,
			    GCancellable *cancellable,
			    GError **error)
{
#ifdef HAVE_GIO_UNIX
	g_autoptr(GUnixInputStream) istr = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (device_id != NULL, FALSE);
	g_return_val_if_fail (bytes != NULL, FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return FALSE;

	istr = fwupd_unix_input_stream_from_bytes (bytes, error);
	if (istr == NULL)
		return FALSE;
	return fwupd_client_install_fd (client, device_id, istr, NULL,
					install_flags, cancellable, error);
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Not supported as <glib-unix.h> is unavailable");
	return FALSE;
#endif
}

/**
 * fwupd_client_install:
 * @client: A #FwupdClient
 * @device_id: the device ID
 * @filename: the filename to install
 * @install_flags: the #FwupdInstallFlags, e.g. %FWUPD_INSTALL_FLAG_ALLOW_REINSTALL
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Install a file onto a specific device.
 *
 * Returns: %TRUE for success
 *
 * Since: 0.7.0
 **/
gboolean
fwupd_client_install (FwupdClient *client,
		      const gchar *device_id,
		      const gchar *filename,
		      FwupdInstallFlags install_flags,
		      GCancellable *cancellable,
		      GError **error)
{
#ifdef HAVE_GIO_UNIX
	g_autoptr(GUnixInputStream) istr = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (device_id != NULL, FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return FALSE;

	istr = fwupd_unix_input_stream_from_fn (filename, error);
	if (istr == NULL)
		return FALSE;
	return fwupd_client_install_fd (client, device_id, istr, filename,
					install_flags, cancellable, error);
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Not supported as <glib-unix.h> is unavailable");
	return FALSE;
#endif
}

/**
 * fwupd_client_install_release:
 * @client: A #FwupdClient
 * @device: A #FwupdDevice
 * @release: A #FwupdRelease
 * @install_flags: the #FwupdInstallFlags, e.g. %FWUPD_INSTALL_FLAG_ALLOW_REINSTALL
 * @cancellable: A #GCancellable, or %NULL
 * @error: A #GError, or %NULL
 *
 * Installs a new release on a device, downloading the firmware if required.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.5
 **/
gboolean
fwupd_client_install_release (FwupdClient *client,
			      FwupdDevice *device,
			      FwupdRelease *release,
			      FwupdInstallFlags install_flags,
			      GCancellable *cancellable,
			      GError **error)
{
	GChecksumType checksum_type;
	const gchar *checksum_expected;
	const gchar *remote_id;
	const gchar *uri_tmp;
	g_autofree gchar *checksum_actual = NULL;
	g_autofree gchar *uri_str = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* work out what remote-specific URI fields this should use */
	uri_tmp = fwupd_release_get_uri (release);
	remote_id = fwupd_release_get_remote_id (release);
	if (remote_id != NULL) {
		g_autoptr(FwupdRemote) remote = NULL;
		g_autofree gchar *fn = NULL;

		/* if a remote-id was specified, the remote has to exist */
		remote = fwupd_client_get_remote_by_id (client, remote_id, cancellable, error);
		if (remote == NULL)
			return FALSE;

		/* local and directory remotes have the firmware already */
		if (fwupd_remote_get_kind (remote) == FWUPD_REMOTE_KIND_LOCAL) {
			const gchar *fn_cache = fwupd_remote_get_filename_cache (remote);
			g_autofree gchar *path = g_path_get_dirname (fn_cache);

			fn = g_build_filename (path, uri_tmp, NULL);
		} else if (fwupd_remote_get_kind (remote) == FWUPD_REMOTE_KIND_DIRECTORY) {
			fn = g_strdup (uri_tmp + 7);
		}

		/* install with flags chosen by the user */
		if (fn != NULL) {
			return fwupd_client_install (client, fwupd_device_get_id (device),
						     fn, install_flags, cancellable, error);
		}

		/* remote file */
		uri_str = fwupd_remote_build_firmware_uri (remote, uri_tmp, error);
		if (uri_str == NULL)
			return FALSE;
	} else {
		uri_str = g_strdup (uri_tmp);
	}

	/* download file */
	blob = fwupd_client_download_bytes (client, uri_str,
					    FWUPD_CLIENT_DOWNLOAD_FLAG_NONE,
					    cancellable, error);
	if (blob == NULL)
		return FALSE;

	/* verify checksum */
	checksum_expected = fwupd_checksum_get_best (fwupd_release_get_checksums (release));
	checksum_type = fwupd_checksum_guess_kind (checksum_expected);
	checksum_actual = g_compute_checksum_for_bytes (checksum_type, blob);
	if (g_strcmp0 (checksum_expected, checksum_actual) != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Checksum invalid, expected %s got %s",
			     checksum_expected, checksum_actual);
		return FALSE;
	}

	/* if the device specifies ONLY_OFFLINE automatically set this flag */
	if (fwupd_device_has_flag (device, FWUPD_DEVICE_FLAG_ONLY_OFFLINE))
		install_flags |= FWUPD_INSTALL_FLAG_OFFLINE;
	return fwupd_client_install_bytes (client,
					   fwupd_device_get_id (device), blob,
					   install_flags, NULL, error);
}

/**
 * fwupd_client_get_details:
 * @client: A #FwupdClient
 * @filename: the firmware filename, e.g. `firmware.cab`
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Gets details about a specific firmware file.
 *
 * Returns: (transfer container) (element-type FwupdDevice): an array of results
 *
 * Since: 1.0.0
 **/
GPtrArray *
fwupd_client_get_details (FwupdClient *client, const gchar *filename,
			  GCancellable *cancellable, GError **error)
{
#ifdef HAVE_GIO_UNIX
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	GVariant *body;
	gint fd;
	gint retval;
	g_autoptr(FwupdClientHelper) helper = NULL;
	g_autoptr(GDBusMessage) request = NULL;
	g_autoptr(GUnixFDList) fd_list = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), NULL);
	g_return_val_if_fail (filename != NULL, NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return NULL;

	/* open file */
	fd = open (filename, O_RDONLY);
	if (fd < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to open %s",
			     filename);
		return NULL;
	}

	/* set out of band file descriptor */
	fd_list = g_unix_fd_list_new ();
	retval = g_unix_fd_list_append (fd_list, fd, NULL);
	g_assert (retval != -1);
	request = g_dbus_message_new_method_call (FWUPD_DBUS_SERVICE,
						  FWUPD_DBUS_PATH,
						  FWUPD_DBUS_INTERFACE,
						  "GetDetails");
	g_dbus_message_set_unix_fd_list (request, fd_list);

	/* g_unix_fd_list_append did a dup() already */
	close (fd);

	/* call into daemon */
	helper = fwupd_client_helper_new ();
	body = g_variant_new ("(h)", fd);
	g_dbus_message_set_body (request, body);

	g_dbus_connection_send_message_with_reply (priv->conn,
						   request,
						   G_DBUS_SEND_MESSAGE_FLAGS_NONE,
						   -1,
						   NULL,
						   cancellable,
						   fwupd_client_send_message_cb,
						   helper);
	g_main_loop_run (helper->loop);
	if (!helper->ret) {
		g_propagate_error (error, helper->error);
		helper->error = NULL;
		return NULL;
	}

	/* return results */
	return fwupd_device_array_from_variant (helper->val);
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Not supported as <glib-unix.h> is unavailable");
	return NULL;
#endif
}

/**
 * fwupd_client_get_percentage:
 * @client: A #FwupdClient
 *
 * Gets the last returned percentage value.
 *
 * Returns: a percentage, or 0 for unknown.
 *
 * Since: 0.7.3
 **/
guint
fwupd_client_get_percentage (FwupdClient *client)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_return_val_if_fail (FWUPD_IS_CLIENT (client), 0);
	return priv->percentage;
}

/**
 * fwupd_client_get_daemon_version:
 * @client: A #FwupdClient
 *
 * Gets the daemon version number.
 *
 * Returns: a string, or %NULL for unknown.
 *
 * Since: 0.9.6
 **/
const gchar *
fwupd_client_get_daemon_version (FwupdClient *client)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_return_val_if_fail (FWUPD_IS_CLIENT (client), NULL);
	return priv->daemon_version;
}

/**
 * fwupd_client_get_host_product:
 * @client: A #FwupdClient
 *
 * Gets the string that represents the host running fwupd
 *
 * Returns: a string, or %NULL for unknown.
 *
 * Since: 1.3.1
 **/
const gchar *
fwupd_client_get_host_product (FwupdClient *client)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_return_val_if_fail (FWUPD_IS_CLIENT (client), NULL);
	return priv->host_product;
}

/**
 * fwupd_client_get_host_machine_id:
 * @client: A #FwupdClient
 *
 * Gets the string that represents the host machine ID
 *
 * Returns: a string, or %NULL for unknown.
 *
 * Since: 1.3.2
 **/
const gchar *
fwupd_client_get_host_machine_id (FwupdClient *client)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_return_val_if_fail (FWUPD_IS_CLIENT (client), NULL);
	return priv->host_machine_id;
}

/**
 * fwupd_client_get_status:
 * @client: A #FwupdClient
 *
 * Gets the last returned status value.
 *
 * Returns: a #FwupdStatus, or %FWUPD_STATUS_UNKNOWN for unknown.
 *
 * Since: 0.7.3
 **/
FwupdStatus
fwupd_client_get_status (FwupdClient *client)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FWUPD_STATUS_UNKNOWN);
	return priv->status;
}

/**
 * fwupd_client_get_tainted:
 * @client: A #FwupdClient
 *
 * Gets if the daemon has been tainted by 3rd party code.
 *
 * Returns: %TRUE if the daemon is unsupported
 *
 * Since: 1.2.4
 **/
gboolean
fwupd_client_get_tainted (FwupdClient *client)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	return priv->tainted;
}


/**
 * fwupd_client_get_daemon_interactive:
 * @client: A #FwupdClient
 *
 * Gets if the daemon is running in an interactive terminal.
 *
 * Returns: %TRUE if the daemon is running in an interactive terminal
 *
 * Since: 1.3.4
 **/
gboolean
fwupd_client_get_daemon_interactive (FwupdClient *client)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	return priv->interactive;
}

#ifdef HAVE_GIO_UNIX
static gboolean
fwupd_client_update_metadata_fds (FwupdClient *client,
				  const gchar *remote_id,
				  GUnixInputStream *metadata,
				  GUnixInputStream *signature,
				  GCancellable *cancellable,
				  GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	GVariant *body;
	g_autoptr(FwupdClientHelper) helper = NULL;
	g_autoptr(GDBusMessage) request = NULL;
	g_autoptr(GUnixFDList) fd_list = NULL;

	/* set out of band file descriptor */
	fd_list = g_unix_fd_list_new ();
	g_unix_fd_list_append (fd_list, g_unix_input_stream_get_fd (metadata), NULL);
	g_unix_fd_list_append (fd_list, g_unix_input_stream_get_fd (signature), NULL);
	request = g_dbus_message_new_method_call (FWUPD_DBUS_SERVICE,
						  FWUPD_DBUS_PATH,
						  FWUPD_DBUS_INTERFACE,
						  "UpdateMetadata");
	g_dbus_message_set_unix_fd_list (request, fd_list);

	/* call into daemon */
	body = g_variant_new ("(shh)",
			      remote_id,
			      g_unix_input_stream_get_fd (metadata),
			      g_unix_input_stream_get_fd (signature));
	g_dbus_message_set_body (request, body);
	helper = fwupd_client_helper_new ();
	g_dbus_connection_send_message_with_reply (priv->conn,
						   request,
						   G_DBUS_SEND_MESSAGE_FLAGS_NONE,
						   -1,
						   NULL,
						   cancellable,
						   fwupd_client_send_message_cb,
						   helper);
	g_main_loop_run (helper->loop);
	if (!helper->ret) {
		g_propagate_error (error, helper->error);
		helper->error = NULL;
		return FALSE;
	}
	return TRUE;
}

#endif

/**
 * fwupd_client_update_metadata:
 * @client: A #FwupdClient
 * @remote_id: the remote ID, e.g. `lvfs-testing`
 * @metadata_fn: the XML metadata filename
 * @signature_fn: the GPG signature file
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Updates the metadata. This allows a session process to download the metadata
 * and metadata signing file to be passed into the daemon to be checked and
 * parsed.
 *
 * The @remote_id allows the firmware to be tagged so that the remote can be
 * matched when the firmware is downloaded.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.0.0
 **/
gboolean
fwupd_client_update_metadata (FwupdClient *client,
			      const gchar *remote_id,
			      const gchar *metadata_fn,
			      const gchar *signature_fn,
			      GCancellable *cancellable,
			      GError **error)
{
#ifdef HAVE_GIO_UNIX
	GUnixInputStream *istr;
	GUnixInputStream *istr_sig;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (remote_id != NULL, FALSE);
	g_return_val_if_fail (metadata_fn != NULL, FALSE);
	g_return_val_if_fail (signature_fn != NULL, FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return FALSE;

	/* open files */
	istr = fwupd_unix_input_stream_from_fn (metadata_fn, error);
	if (istr == NULL)
		return FALSE;
	istr_sig = fwupd_unix_input_stream_from_fn (signature_fn, error);
	if (istr_sig == NULL)
		return FALSE;
	return fwupd_client_update_metadata_fds (client, remote_id,
						 istr, istr_sig,
						 cancellable, error);
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Not supported as <glib-unix.h> is unavailable");
	return FALSE;
#endif
}

/**
 * fwupd_client_update_metadata_bytes:
 * @client: A #FwupdClient
 * @remote_id: remote ID, e.g. `lvfs-testing`
 * @metadata: XML metadata data
 * @signature: signature data
 * @cancellable: #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Updates the metadata. This allows a session process to download the metadata
 * and metadata signing file to be passed into the daemon to be checked and
 * parsed.
 *
 * The @remote_id allows the firmware to be tagged so that the remote can be
 * matched when the firmware is downloaded.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.5
 **/
gboolean
fwupd_client_update_metadata_bytes (FwupdClient *client,
				    const gchar *remote_id,
				    GBytes *metadata,
				    GBytes *signature,
				    GCancellable *cancellable,
				    GError **error)
{
#ifdef HAVE_GIO_UNIX
	GUnixInputStream *istr;
	GUnixInputStream *istr_sig;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (remote_id != NULL, FALSE);
	g_return_val_if_fail (metadata != NULL, FALSE);
	g_return_val_if_fail (signature != NULL, FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* convert bytes to a readable fd */
	istr = fwupd_unix_input_stream_from_bytes (metadata, error);
	if (istr == NULL)
		return FALSE;
	istr_sig = fwupd_unix_input_stream_from_bytes (signature, error);
	if (istr_sig == NULL)
		return FALSE;
	return fwupd_client_update_metadata_fds (client, remote_id,
						 istr, istr_sig,
						 cancellable, error);
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Not supported as <glib-unix.h> is unavailable");
	return FALSE;
#endif
}

/**
 * fwupd_client_refresh_remote:
 * @client: A #FwupdClient
 * @remote: A #FwupdRemote
 * @cancellable: A #GCancellable, or %NULL
 * @error: A #GError, or %NULL
 *
 * Refreshes a remote by downloading new metadata.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.5
 **/
gboolean
fwupd_client_refresh_remote (FwupdClient *client,
			     FwupdRemote *remote,
			     GCancellable *cancellable,
			     GError **error)
{
	g_autoptr(GBytes) metadata = NULL;
	g_autoptr(GBytes) signature = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (FWUPD_IS_REMOTE (remote), FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* download the signature */
	signature = fwupd_client_download_bytes (client,
						 fwupd_remote_get_metadata_uri_sig (remote),
						 FWUPD_CLIENT_DOWNLOAD_FLAG_NONE,
						 cancellable, error);
	if (signature == NULL)
		return FALSE;

	/* find the download URI of the metadata from the JCat file */
	if (!fwupd_remote_load_signature_bytes (remote, signature, error))
		return FALSE;

	/* download the metadata */
	metadata = fwupd_client_download_bytes (client,
						fwupd_remote_get_metadata_uri (remote),
						FWUPD_CLIENT_DOWNLOAD_FLAG_NONE,
						cancellable, error);
	if (metadata == NULL)
		return FALSE;

	/* send all this to fwupd */
	return fwupd_client_update_metadata_bytes (client,
						   fwupd_remote_get_id (remote),
						   metadata, signature,
						   cancellable, error);
}

/**
 * fwupd_client_get_remotes:
 * @client: A #FwupdClient
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Gets the list of remotes that have been configured for the system.
 *
 * Returns: (element-type FwupdRemote) (transfer container): list of remotes, or %NULL
 *
 * Since: 0.9.3
 **/
GPtrArray *
fwupd_client_get_remotes (FwupdClient *client, GCancellable *cancellable, GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return NULL;

	/* call into daemon */
	val = g_dbus_proxy_call_sync (priv->proxy,
				      "GetRemotes",
				      NULL,
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      cancellable,
				      error);
	if (val == NULL) {
		if (error != NULL)
			fwupd_client_fixup_dbus_error (*error);
		return NULL;
	}
	return fwupd_remote_array_from_variant (val);
}

/**
 * fwupd_client_get_approved_firmware:
 * @client: A #FwupdClient
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Gets the list of approved firmware.
 *
 * Returns: (transfer full): list of remotes, or %NULL
 *
 * Since: 1.2.6
 **/
gchar **
fwupd_client_get_approved_firmware (FwupdClient *client,
				    GCancellable *cancellable,
				    GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(GVariant) val = NULL;
	gchar **retval = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return NULL;

	/* call into daemon */
	val = g_dbus_proxy_call_sync (priv->proxy,
				      "GetApprovedFirmware",
				      NULL,
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      cancellable,
				      error);
	if (val == NULL) {
		if (error != NULL)
			fwupd_client_fixup_dbus_error (*error);
		return NULL;
	}
	g_variant_get (val, "(^as)", &retval);
	return retval;
}

/**
 * fwupd_client_set_approved_firmware:
 * @client: A #FwupdClient
 * @checksums: Array of checksums
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Sets the list of approved firmware.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.2.6
 **/
gboolean
fwupd_client_set_approved_firmware (FwupdClient *client,
				    gchar **checksums,
				    GCancellable *cancellable,
				    GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return FALSE;

	/* call into daemon */
	val = g_dbus_proxy_call_sync (priv->proxy,
				      "SetApprovedFirmware",
				      g_variant_new ("(^as)", checksums),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      cancellable,
				      error);
	if (val == NULL) {
		if (error != NULL)
			fwupd_client_fixup_dbus_error (*error);
		return FALSE;
	}
	return TRUE;
}

/**
 * fwupd_client_set_feature_flags:
 * @client: A #FwupdClient
 * @feature_flags: #FwupdFeatureFlags, e.g. %FWUPD_FEATURE_FLAG_UPDATE_TEXT
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Sets the features the client supports. This allows firmware to depend on
 * specific front-end features, for instance showing the user an image on
 * how to detach the hardware.
 *
 * Clients can call this none or multiple times.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.5
 **/
gboolean
fwupd_client_set_feature_flags (FwupdClient *client,
				FwupdFeatureFlags feature_flags,
				GCancellable *cancellable,
				GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return FALSE;

	/* call into daemon */
	val = g_dbus_proxy_call_sync (priv->proxy,
				      "SetFeatureFlags",
				      g_variant_new ("(t)", (guint64) feature_flags),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      cancellable,
				      error);
	if (val == NULL) {
		if (error != NULL)
			fwupd_client_fixup_dbus_error (*error);
		return FALSE;
	}
	return TRUE;
}

/**
 * fwupd_client_self_sign:
 * @client: A #FwupdClient
 * @value: A string to sign, typically a JSON blob
 * @flags: #FwupdSelfSignFlags, e.g. %FWUPD_SELF_SIGN_FLAG_ADD_TIMESTAMP
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Signs the data using the client self-signed certificate.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.2.6
 **/
gchar *
fwupd_client_self_sign (FwupdClient *client,
			const gchar *value,
			FwupdSelfSignFlags flags,
			GCancellable *cancellable,
			GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	GVariantBuilder builder;
	g_autoptr(GVariant) val = NULL;
	gchar *retval = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return NULL;

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
	val = g_dbus_proxy_call_sync (priv->proxy,
				      "SelfSign",
				      g_variant_new ("(sa{sv})", value, &builder),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      cancellable,
				      error);
	if (val == NULL) {
		if (error != NULL)
			fwupd_client_fixup_dbus_error (*error);
		return NULL;
	}
	g_variant_get (val, "(s)", &retval);
	return retval;
}

/**
 * fwupd_client_modify_remote:
 * @client: A #FwupdClient
 * @remote_id: the remote ID, e.g. `lvfs-testing`
 * @key: the key, e.g. `Enabled`
 * @value: the key, e.g. `true`
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Modifies a system remote in a specific way.
 *
 * NOTE: User authentication may be required to complete this action.
 *
 * Returns: %TRUE for success
 *
 * Since: 0.9.8
 **/
gboolean
fwupd_client_modify_remote (FwupdClient *client,
			    const gchar *remote_id,
			    const gchar *key,
			    const gchar *value,
			    GCancellable *cancellable,
			    GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (remote_id != NULL, FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return FALSE;

	/* call into daemon */
	val = g_dbus_proxy_call_sync (priv->proxy,
				      "ModifyRemote",
				      g_variant_new ("(sss)", remote_id, key, value),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      cancellable,
				      error);
	if (val == NULL) {
		if (error != NULL)
			fwupd_client_fixup_dbus_error (*error);
		return FALSE;
	}
	return TRUE;
}

/**
 * fwupd_client_modify_device:
 * @client: A #FwupdClient
 * @device_id: the device ID
 * @key: the key, e.g. `Flags`
 * @value: the key, e.g. `reported`
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Modifies a device in a specific way. Not all properties on the #FwupdDevice
 * are settable by the client, and some may have other restrictions on @value.
 *
 * NOTE: User authentication may be required to complete this action.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.0.4
 **/
gboolean
fwupd_client_modify_device (FwupdClient *client,
			    const gchar *remote_id,
			    const gchar *key,
			    const gchar *value,
			    GCancellable *cancellable,
			    GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (remote_id != NULL, FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return FALSE;

	/* call into daemon */
	val = g_dbus_proxy_call_sync (priv->proxy,
				      "ModifyDevice",
				      g_variant_new ("(sss)", remote_id, key, value),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      cancellable,
				      error);
	if (val == NULL) {
		if (error != NULL)
			fwupd_client_fixup_dbus_error (*error);
		return FALSE;
	}
	return TRUE;
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

/**
 * fwupd_client_get_remote_by_id:
 * @client: A #FwupdClient
 * @remote_id: the remote ID, e.g. `lvfs-testing`
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Gets a specific remote that has been configured for the system.
 *
 * Returns: (transfer full): a #FwupdRemote, or %NULL if not found
 *
 * Since: 0.9.3
 **/
FwupdRemote *
fwupd_client_get_remote_by_id (FwupdClient *client,
			       const gchar *remote_id,
			       GCancellable *cancellable,
			       GError **error)
{
	FwupdRemote *remote;
	g_autoptr(GPtrArray) remotes = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), NULL);
	g_return_val_if_fail (remote_id != NULL, NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* find remote in list */
	remotes = fwupd_client_get_remotes (client, cancellable, error);
	if (remotes == NULL)
		return NULL;
	remote = fwupd_client_get_remote_by_id_noref (remotes, remote_id);
	if (remote == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "No remote '%s' found in search paths",
			     remote_id);
		return NULL;
	}

	/* success */
	return g_object_ref (remote);
}

/**
 * fwupd_client_set_user_agent:
 * @client: A #FwupdClient
 * @user_agent: the user agent ID, e.g. `gnome-software/3.34.1`
 *
 * Manually sets the user agent that is used for downloading. The user agent
 * should contain the runtime version of fwupd somewhere in the provided string.
 *
 * Since: 1.4.5
 **/
void
fwupd_client_set_user_agent (FwupdClient *client, const gchar *user_agent)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	g_return_if_fail (FWUPD_IS_CLIENT (client));
	g_return_if_fail (user_agent != NULL);
	g_free (priv->user_agent);
	priv->user_agent = g_strdup (user_agent);
}

/**
 * fwupd_client_set_user_agent_for_package:
 * @client: A #FwupdClient
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
fwupd_client_set_user_agent_for_package (FwupdClient *client,
					 const gchar *package_name,
					 const gchar *package_version)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	GString *str = g_string_new (NULL);
	g_autofree gchar *system = NULL;

	g_return_if_fail (FWUPD_IS_CLIENT (client));
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


static void
fwupd_client_download_chunk_cb (SoupMessage *msg, SoupBuffer *chunk, gpointer user_data)
{
	guint percentage;
	goffset header_size;
	goffset body_length;
	FwupdClient *client = FWUPD_CLIENT (user_data);

	/* if it's returning "Found" or an error, ignore the percentage */
	if (msg->status_code != SOUP_STATUS_OK) {
		g_debug ("ignoring status code %u (%s)",
			 msg->status_code, msg->reason_phrase);
		return;
	}

	/* get data */
	body_length = msg->response_body->length;
	header_size = soup_message_headers_get_content_length (msg->response_headers);
	if (header_size < body_length)
		return;

	/* calculate percentage */
	percentage = (guint) ((100 * body_length) / header_size);
	g_debug ("progress: %u%%", percentage);
	fwupd_client_set_status (client, FWUPD_STATUS_DOWNLOADING);
	fwupd_client_set_percentage (client, percentage);
}

/**
 * fwupd_client_download_bytes:
 * @client: A #FwupdClient
 * @url: the remote URL
 * @flags: #FwupdClientDownloadFlags, e.g. %FWUPD_CLIENT_DOWNLOAD_FLAG_NONE
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Downloads data from a remote server. The fwupd_client_set_user_agent() function
 * should be called before this method is used.
 *
 * Returns: (transfer full): downloaded data, or %NULL for error
 *
 * Since: 1.4.5
 **/
GBytes *
fwupd_client_download_bytes (FwupdClient *client,
			     const gchar *url,
			     FwupdClientDownloadFlags flags,
			     GCancellable *cancellable,
			     GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	guint status_code;
	g_autoptr(SoupMessage) msg = NULL;
	g_autoptr(SoupURI) uri = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), NULL);
	g_return_val_if_fail (url != NULL, NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* ensure networking set up */
	if (!fwupd_client_ensure_networking (client, error))
		return NULL;

	/* download data */
	g_debug ("downloading %s", url);
	uri = soup_uri_new (url);
	msg = soup_message_new_from_uri (SOUP_METHOD_GET, uri);
	if (msg == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to parse URI %s", url);
		return NULL;
	}
	g_signal_connect (msg, "got-chunk",
			  G_CALLBACK (fwupd_client_download_chunk_cb),
			  client);
	status_code = soup_session_send_message (priv->soup_session, msg);
	fwupd_client_set_status (client, FWUPD_STATUS_IDLE);
	if (status_code == 429) {
		g_autofree gchar *str = g_strndup (msg->response_body->data,
						   msg->response_body->length);
		if (g_strcmp0 (str, "Too Many Requests") == 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Failed to download due to server limit");
			return NULL;
		}
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to download due to server limit: %s", str);
		return NULL;
	}
	if (status_code != SOUP_STATUS_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to download %s: %s",
			     url, soup_status_get_phrase (status_code));
		return NULL;
	}

	/* success */
	return g_bytes_new (msg->response_body->data, msg->response_body->length);
}

/**
 * fwupd_client_upload_bytes:
 * @client: A #FwupdClient
 * @url: the remote URL
 * @payload: payload string
 * @signature: (nullable): signature string
 * @flags: #FwupdClientDownloadFlags, e.g. %FWUPD_CLIENT_DOWNLOAD_FLAG_NONE
 * @cancellable: the #GCancellable, or %NULL
 * @error: the #GError, or %NULL
 *
 * Uploads data to a remote server. The fwupd_client_set_user_agent() function
 * should be called before this method is used.
 *
 * Returns: (transfer full): downloaded data, or %NULL for error
 *
 * Since: 1.4.5
 **/
GBytes *
fwupd_client_upload_bytes (FwupdClient *client,
			   const gchar *url,
			   const gchar *payload,
			   const gchar *signature,
			   FwupdClientUploadFlags flags,
			   GCancellable *cancellable,
			   GError **error)
{
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	guint status_code;
	g_autoptr(SoupMessage) msg = NULL;
	g_autoptr(SoupURI) uri = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), NULL);
	g_return_val_if_fail (url != NULL, NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* ensure networking set up */
	if (!fwupd_client_ensure_networking (client, error))
		return NULL;

	/* build message */
	if ((flags | FWUPD_CLIENT_UPLOAD_FLAG_ALWAYS_MULTIPART) > 0 ||
	    signature != NULL) {
		g_autoptr(SoupMultipart) mp = NULL;
		mp = soup_multipart_new (SOUP_FORM_MIME_TYPE_MULTIPART);
		soup_multipart_append_form_string (mp, "payload", payload);
		if (signature != NULL)
			soup_multipart_append_form_string (mp, "signature", signature);
		msg = soup_form_request_new_from_multipart (url, mp);
	} else {
		msg = soup_message_new (SOUP_METHOD_POST, url);
		soup_message_set_request (msg, "application/json; charset=utf-8",
					  SOUP_MEMORY_COPY, payload, strlen (payload));
	}

	/* POST request */
	if (msg == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to parse URI %s", url);
		return NULL;
	}
	g_debug ("uploading to %s", url);
	status_code = soup_session_send_message (priv->soup_session, msg);
	g_debug ("server returned: %s", msg->response_body->data);

	/* fall back to HTTP status codes in case the server is offline */
	if (!SOUP_STATUS_IS_SUCCESSFUL (status_code)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to upllooad to %s: %s",
			     url, soup_status_get_phrase (status_code));
		return NULL;
	}

	/* success */
	return g_bytes_new (msg->response_body->data, msg->response_body->length);
}

static void
fwupd_client_get_property (GObject *object, guint prop_id,
			   GValue *value, GParamSpec *pspec)
{
	FwupdClient *client = FWUPD_CLIENT (object);
	FwupdClientPrivate *priv = GET_PRIVATE (client);

	switch (prop_id) {
	case PROP_STATUS:
		g_value_set_uint (value, priv->status);
		break;
	case PROP_TAINTED:
		g_value_set_boolean (value, priv->tainted);
		break;
	case PROP_SOUP_SESSION:
		g_value_set_object (value, priv->soup_session);
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
	FwupdClient *client = FWUPD_CLIENT (object);
	FwupdClientPrivate *priv = GET_PRIVATE (client);

	switch (prop_id) {
	case PROP_STATUS:
		priv->status = g_value_get_uint (value);
		break;
	case PROP_PERCENTAGE:
		priv->percentage = g_value_get_uint (value);
		break;
	case PROP_SOUP_SESSION:
		g_set_object (&priv->soup_session, g_value_get_object (value));
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
	 * @client: the #FwupdClient instance that emitted the signal
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
	 * @client: the #FwupdClient instance that emitted the signal
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
	 * @client: the #FwupdClient instance that emitted the signal
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
	 * @client: the #FwupdClient instance that emitted the signal
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
	 * @client: the #FwupdClient instance that emitted the signal
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
	 * The libsoup session.
	 *
	 * Since: 1.4.5
	 */
	pspec = g_param_spec_object ("soup-session", NULL, NULL, SOUP_TYPE_SESSION,
				     G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
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
}

static void
fwupd_client_init (FwupdClient *client)
{
}

static void
fwupd_client_finalize (GObject *object)
{
	FwupdClient *client = FWUPD_CLIENT (object);
	FwupdClientPrivate *priv = GET_PRIVATE (client);

	g_free (priv->user_agent);
	g_free (priv->daemon_version);
	g_free (priv->host_product);
	g_free (priv->host_machine_id);
	if (priv->conn != NULL)
		g_object_unref (priv->conn);
	if (priv->proxy != NULL)
		g_object_unref (priv->proxy);
	if (priv->soup_session != NULL)
		g_object_unref (priv->soup_session);

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
	FwupdClient *client;
	client = g_object_new (FWUPD_TYPE_CLIENT, NULL);
	return FWUPD_CLIENT (client);
}
