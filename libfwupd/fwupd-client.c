/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <glib-object.h>
#include <gio/gio.h>
#ifdef HAVE_GIO_UNIX
#include <gio/gunixfdlist.h>
#endif

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "fwupd-client.h"
#include "fwupd-common.h"
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
		if (val != NULL) {
			priv->status = g_variant_get_uint32 (val);
			g_debug ("Emitting ::status-changed() [%s]",
				 fwupd_status_to_string (priv->status));
			g_signal_emit (client, signals[SIGNAL_STATUS_CHANGED], 0, priv->status);
			g_object_notify (G_OBJECT (client), "status");
		}
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
		if (val != NULL) {
			priv->percentage = g_variant_get_uint32 (val);
			g_object_notify (G_OBJECT (client), "percentage");
		}
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
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	GVariant *body;
	GVariantBuilder builder;
	gint retval;
	gint fd;
	g_autoptr(FwupdClientHelper) helper = NULL;
	g_autoptr(GDBusMessage) request = NULL;
	g_autoptr(GUnixFDList) fd_list = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (device_id != NULL, FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return FALSE;

	/* set options */
	g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add (&builder, "{sv}",
			       "reason", g_variant_new_string ("user-action"));
	g_variant_builder_add (&builder, "{sv}",
			       "filename", g_variant_new_string (filename));
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

	/* open file */
	fd = open (filename, O_RDONLY);
	if (fd < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to open %s",
			     filename);
		return FALSE;
	}

	/* set out of band file descriptor */
	fd_list = g_unix_fd_list_new ();
	retval = g_unix_fd_list_append (fd_list, fd, NULL);
	g_assert (retval != -1);
	request = g_dbus_message_new_method_call (FWUPD_DBUS_SERVICE,
						  FWUPD_DBUS_PATH,
						  FWUPD_DBUS_INTERFACE,
						  "Install");
	g_dbus_message_set_unix_fd_list (request, fd_list);

	/* g_unix_fd_list_append did a dup() already */
	close (fd);

	/* call into daemon */
	helper = fwupd_client_helper_new ();
	body = g_variant_new ("(sha{sv})", device_id, fd, &builder);
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
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Not supported as <glib-unix.h> is unavailable");
	return FALSE;
#endif
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
	FwupdClientPrivate *priv = GET_PRIVATE (client);
	GVariant *body;
	gint fd;
	gint fd_sig;
	g_autoptr(FwupdClientHelper) helper = NULL;
	g_autoptr(GDBusMessage) request = NULL;
	g_autoptr(GUnixFDList) fd_list = NULL;

	g_return_val_if_fail (FWUPD_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (remote_id != NULL, FALSE);
	g_return_val_if_fail (metadata_fn != NULL, FALSE);
	g_return_val_if_fail (signature_fn != NULL, FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect (client, cancellable, error))
		return FALSE;

	/* open file */
	fd = open (metadata_fn, O_RDONLY);
	if (fd < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to open %s",
			     metadata_fn);
		return FALSE;
	}
	fd_sig = open (signature_fn, O_RDONLY);
	if (fd_sig < 0) {
		close (fd);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to open %s",
			     signature_fn);
		return FALSE;
	}

	/* set out of band file descriptor */
	fd_list = g_unix_fd_list_new ();
	g_unix_fd_list_append (fd_list, fd, NULL);
	g_unix_fd_list_append (fd_list, fd_sig, NULL);
	request = g_dbus_message_new_method_call (FWUPD_DBUS_SERVICE,
						  FWUPD_DBUS_PATH,
						  FWUPD_DBUS_INTERFACE,
						  "UpdateMetadata");
	g_dbus_message_set_unix_fd_list (request, fd_list);

	/* g_unix_fd_list_append did a dup() already */
	close (fd);
	close (fd_sig);

	/* call into daemon */
	body = g_variant_new ("(shh)", remote_id, fd, fd_sig);
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
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Not supported as <glib-unix.h> is unavailable");
	return FALSE;
#endif
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

	g_free (priv->daemon_version);
	g_free (priv->host_product);
	g_free (priv->host_machine_id);
	if (priv->conn != NULL)
		g_object_unref (priv->conn);
	if (priv->proxy != NULL)
		g_object_unref (priv->proxy);

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
