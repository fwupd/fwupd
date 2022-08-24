/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#ifdef HAVE_GIO_UNIX
#include <gio/gunixinputstream.h>
#endif

#include "fwupd-client-private.h"
#include "fwupd-client-sync.h"
#include "fwupd-client.h"
#include "fwupd-common-private.h"
#include "fwupd-error.h"

typedef struct {
	gboolean ret;
	gchar *str;
	GError *error;
	GPtrArray *array;
	GMainContext *context;
	GMainLoop *loop;
	GVariant *val;
	GHashTable *hash;
	GBytes *bytes;
	FwupdDevice *device;
} FwupdClientHelper;

static void
fwupd_client_helper_free(FwupdClientHelper *helper)
{
	if (helper->val != NULL)
		g_variant_unref(helper->val);
	if (helper->error != NULL)
		g_error_free(helper->error);
	if (helper->array != NULL)
		g_ptr_array_unref(helper->array);
	if (helper->hash != NULL)
		g_hash_table_unref(helper->hash);
	if (helper->bytes != NULL)
		g_bytes_unref(helper->bytes);
	if (helper->device != NULL)
		g_object_unref(helper->device);
	g_free(helper->str);
	g_main_loop_unref(helper->loop);
	g_main_context_unref(helper->context);
	g_main_context_pop_thread_default(helper->context);
	g_free(helper);
}

static FwupdClientHelper *
fwupd_client_helper_new(FwupdClient *self)
{
	FwupdClientHelper *helper;
	helper = g_new0(FwupdClientHelper, 1);
	helper->context = fwupd_client_get_main_context(self);
	helper->loop = g_main_loop_new(helper->context, FALSE);
	g_main_context_push_thread_default(helper->context);
	return helper;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FwupdClientHelper, fwupd_client_helper_free)
#pragma clang diagnostic pop

static void
fwupd_client_connect_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->ret = fwupd_client_connect_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_connect: (skip)
 * @self: a #FwupdClient
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
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
fwupd_client_connect(FwupdClient *self, GCancellable *cancellable, GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_connect_async(self, cancellable, fwupd_client_connect_cb, helper);
	g_main_loop_run(helper->loop);
	if (!helper->ret) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return FALSE;
	}
	return TRUE;
}

static void
fwupd_client_get_devices_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->array = fwupd_client_get_devices_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_get_devices:
 * @self: a #FwupdClient
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Gets all the devices registered with the daemon.
 *
 * Returns: (element-type FwupdDevice) (transfer container): results
 *
 * Since: 0.9.2
 **/
GPtrArray *
fwupd_client_get_devices(FwupdClient *self, GCancellable *cancellable, GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return NULL;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_get_devices_async(self, cancellable, fwupd_client_get_devices_cb, helper);
	g_main_loop_run(helper->loop);
	if (helper->array == NULL) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return NULL;
	}
	return g_steal_pointer(&helper->array);
}

static void
fwupd_client_get_plugins_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->array = fwupd_client_get_plugins_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_get_plugins:
 * @self: a #FwupdClient
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Gets all the plugins being used the daemon.
 *
 * Returns: (element-type FwupdPlugin) (transfer container): results
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_plugins(FwupdClient *self, GCancellable *cancellable, GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return NULL;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_get_plugins_async(self, cancellable, fwupd_client_get_plugins_cb, helper);
	g_main_loop_run(helper->loop);
	if (helper->array == NULL) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return NULL;
	}
	return g_steal_pointer(&helper->array);
}

static void
fwupd_client_get_history_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->array = fwupd_client_get_history_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_get_history:
 * @self: a #FwupdClient
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Gets all the history.
 *
 * Returns: (element-type FwupdDevice) (transfer container): results
 *
 * Since: 1.0.4
 **/
GPtrArray *
fwupd_client_get_history(FwupdClient *self, GCancellable *cancellable, GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return NULL;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_get_history_async(self, cancellable, fwupd_client_get_history_cb, helper);
	g_main_loop_run(helper->loop);
	if (helper->array == NULL) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return NULL;
	}
	return g_steal_pointer(&helper->array);
}

static void
fwupd_client_get_releases_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->array = fwupd_client_get_releases_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_get_releases:
 * @self: a #FwupdClient
 * @device_id: (not nullable): the device ID
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Gets all the releases for a specific device
 *
 * Returns: (element-type FwupdRelease) (transfer container): results
 *
 * Since: 0.9.3
 **/
GPtrArray *
fwupd_client_get_releases(FwupdClient *self,
			  const gchar *device_id,
			  GCancellable *cancellable,
			  GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(device_id != NULL, NULL);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return NULL;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_get_releases_async(self,
					device_id,
					cancellable,
					fwupd_client_get_releases_cb,
					helper);
	g_main_loop_run(helper->loop);
	if (helper->array == NULL) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return NULL;
	}
	return g_steal_pointer(&helper->array);
}

static void
fwupd_client_get_downgrades_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->array =
	    fwupd_client_get_downgrades_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_get_downgrades:
 * @self: a #FwupdClient
 * @device_id: (not nullable): the device ID
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Gets all the downgrades for a specific device.
 *
 * Returns: (element-type FwupdRelease) (transfer container): results
 *
 * Since: 0.9.8
 **/
GPtrArray *
fwupd_client_get_downgrades(FwupdClient *self,
			    const gchar *device_id,
			    GCancellable *cancellable,
			    GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(device_id != NULL, NULL);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return NULL;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_get_downgrades_async(self,
					  device_id,
					  cancellable,
					  fwupd_client_get_downgrades_cb,
					  helper);
	g_main_loop_run(helper->loop);
	if (helper->array == NULL) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return NULL;
	}
	return g_steal_pointer(&helper->array);
}

static void
fwupd_client_get_upgrades_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->array = fwupd_client_get_upgrades_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_get_upgrades:
 * @self: a #FwupdClient
 * @device_id: (not nullable): the device ID
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Gets all the upgrades for a specific device.
 *
 * Returns: (element-type FwupdRelease) (transfer container): results
 *
 * Since: 0.9.8
 **/
GPtrArray *
fwupd_client_get_upgrades(FwupdClient *self,
			  const gchar *device_id,
			  GCancellable *cancellable,
			  GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(device_id != NULL, NULL);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return NULL;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_get_upgrades_async(self,
					device_id,
					cancellable,
					fwupd_client_get_upgrades_cb,
					helper);
	g_main_loop_run(helper->loop);
	if (helper->array == NULL) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return NULL;
	}
	return g_steal_pointer(&helper->array);
}

static void
fwupd_client_get_details_bytes_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->array =
	    fwupd_client_get_details_bytes_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_get_details_bytes:
 * @self: a #FwupdClient
 * @bytes: the firmware archive
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Gets details about a specific firmware file.
 *
 * Returns: (transfer container) (element-type FwupdDevice): an array of results
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_details_bytes(FwupdClient *self,
			       GBytes *bytes,
			       GCancellable *cancellable,
			       GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(bytes != NULL, NULL);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return NULL;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_get_details_bytes_async(self,
					     bytes,
					     cancellable,
					     fwupd_client_get_details_bytes_cb,
					     helper);
	g_main_loop_run(helper->loop);
	if (helper->array == NULL) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return NULL;
	}
	return g_steal_pointer(&helper->array);
}

#ifdef HAVE_GIO_UNIX
static void
fwupd_client_get_details_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->array =
	    fwupd_client_get_details_bytes_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}
#endif

/**
 * fwupd_client_get_details:
 * @self: a #FwupdClient
 * @filename: the firmware filename, e.g. `firmware.cab`
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Gets details about a specific firmware file.
 *
 * Returns: (transfer container) (element-type FwupdDevice): an array of results
 *
 * Since: 1.0.0
 **/
GPtrArray *
fwupd_client_get_details(FwupdClient *self,
			 const gchar *filename,
			 GCancellable *cancellable,
			 GError **error)
{
#ifdef HAVE_GIO_UNIX
	g_autoptr(GUnixInputStream) istr = NULL;
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(filename != NULL, NULL);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return NULL;

	/* call async version and run loop until complete */
	istr = fwupd_unix_input_stream_from_fn(filename, error);
	if (istr == NULL)
		return NULL;
	helper = fwupd_client_helper_new(self);
	fwupd_client_get_details_stream_async(self,
					      istr,
					      cancellable,
					      fwupd_client_get_details_cb,
					      helper);
	g_main_loop_run(helper->loop);
	if (helper->array == NULL) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return NULL;
	}
	return g_steal_pointer(&helper->array);
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not supported as <glib-unix.h> is unavailable");
	return NULL;
#endif
}

static void
fwupd_client_verify_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->ret = fwupd_client_verify_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_verify:
 * @self: a #FwupdClient
 * @device_id: (not nullable): the device ID
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Verify a specific device.
 *
 * Returns: %TRUE for verification success
 *
 * Since: 0.7.0
 **/
gboolean
fwupd_client_verify(FwupdClient *self,
		    const gchar *device_id,
		    GCancellable *cancellable,
		    GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(device_id != NULL, FALSE);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return FALSE;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_verify_async(self, device_id, cancellable, fwupd_client_verify_cb, helper);
	g_main_loop_run(helper->loop);
	if (!helper->ret) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return FALSE;
	}
	return TRUE;
}

static void
fwupd_client_verify_update_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->ret = fwupd_client_verify_update_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_verify_update:
 * @self: a #FwupdClient
 * @device_id: (not nullable): the device ID
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Update the verification record for a specific device.
 *
 * Returns: %TRUE for verification success
 *
 * Since: 0.8.0
 **/
gboolean
fwupd_client_verify_update(FwupdClient *self,
			   const gchar *device_id,
			   GCancellable *cancellable,
			   GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(device_id != NULL, FALSE);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return FALSE;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_verify_update_async(self,
					 device_id,
					 cancellable,
					 fwupd_client_verify_update_cb,
					 helper);
	g_main_loop_run(helper->loop);
	if (!helper->ret) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return FALSE;
	}
	return TRUE;
}

static void
fwupd_client_unlock_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->ret = fwupd_client_unlock_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_unlock:
 * @self: a #FwupdClient
 * @device_id: (not nullable): the device ID
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Unlocks a specific device so firmware can be read or wrote.
 *
 * Returns: %TRUE for success
 *
 * Since: 0.7.0
 **/
gboolean
fwupd_client_unlock(FwupdClient *self,
		    const gchar *device_id,
		    GCancellable *cancellable,
		    GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(device_id != NULL, FALSE);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return FALSE;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_unlock_async(self, device_id, cancellable, fwupd_client_unlock_cb, helper);
	g_main_loop_run(helper->loop);
	if (!helper->ret) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return FALSE;
	}
	return TRUE;
}
static void
fwupd_client_modify_config_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->ret = fwupd_client_modify_config_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_modify_config
 * @self: a #FwupdClient
 * @key: config key, e.g. `DisabledPlugins`
 * @value: config value, e.g. `*`
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Modifies a daemon config option.
 * The daemon will only respond to this request with proper permissions.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.2.8
 **/
gboolean
fwupd_client_modify_config(FwupdClient *self,
			   const gchar *key,
			   const gchar *value,
			   GCancellable *cancellable,
			   GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(key != NULL, FALSE);
	g_return_val_if_fail(value != NULL, FALSE);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return FALSE;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_modify_config_async(self,
					 key,
					 value,
					 cancellable,
					 fwupd_client_modify_config_cb,
					 helper);
	g_main_loop_run(helper->loop);
	if (!helper->ret) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return FALSE;
	}
	return TRUE;
}

static void
fwupd_client_activate_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->ret = fwupd_client_activate_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_activate:
 * @self: a #FwupdClient
 * @cancellable: (nullable): optional #GCancellable
 * @device_id: a device
 * @error: (nullable): optional return location for an error
 *
 * Activates up a device, which normally means the device switches to a new
 * firmware version. This should only be called when data loss cannot occur.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.2.6
 **/
gboolean
fwupd_client_activate(FwupdClient *self,
		      GCancellable *cancellable,
		      const gchar *device_id, /* yes, this is the wrong way around :/ */
		      GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(device_id != NULL, FALSE);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return FALSE;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_activate_async(self, device_id, cancellable, fwupd_client_activate_cb, helper);
	g_main_loop_run(helper->loop);
	if (!helper->ret) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return FALSE;
	}
	return TRUE;
}

static void
fwupd_client_clear_results_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->ret = fwupd_client_clear_results_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_clear_results:
 * @self: a #FwupdClient
 * @device_id: (not nullable): the device ID
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Clears the results for a specific device.
 *
 * Returns: %TRUE for success
 *
 * Since: 0.7.0
 **/
gboolean
fwupd_client_clear_results(FwupdClient *self,
			   const gchar *device_id,
			   GCancellable *cancellable,
			   GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(device_id != NULL, FALSE);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return FALSE;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_clear_results_async(self,
					 device_id,
					 cancellable,
					 fwupd_client_clear_results_cb,
					 helper);
	g_main_loop_run(helper->loop);
	if (!helper->ret) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return FALSE;
	}
	return TRUE;
}

static void
fwupd_client_get_results_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->device = fwupd_client_get_results_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_get_results:
 * @self: a #FwupdClient
 * @device_id: (not nullable): the device ID
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Gets the results of a previous firmware update for a specific device.
 *
 * Returns: (transfer full): a device, or %NULL for failure
 *
 * Since: 0.7.0
 **/
FwupdDevice *
fwupd_client_get_results(FwupdClient *self,
			 const gchar *device_id,
			 GCancellable *cancellable,
			 GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(device_id != NULL, NULL);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return NULL;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_get_results_async(self,
				       device_id,
				       cancellable,
				       fwupd_client_get_results_cb,
				       helper);
	g_main_loop_run(helper->loop);
	if (helper->device == NULL) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return NULL;
	}
	return g_steal_pointer(&helper->device);
}

static void
fwupd_client_modify_bios_setting_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->ret =
	    fwupd_client_modify_bios_setting_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_modify_bios_setting
 * @self: a #FwupdClient
 * @settings: (transfer container): BIOS settings
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Modifies a BIOS setting using kernel API.
 * The daemon will only respond to this request with proper permissions.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.4
 **/
gboolean
fwupd_client_modify_bios_setting(FwupdClient *self,
				 GHashTable *settings,
				 GCancellable *cancellable,
				 GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(settings != NULL, FALSE);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return FALSE;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_modify_bios_setting_async(self,
					       settings,
					       cancellable,
					       fwupd_client_modify_bios_setting_cb,
					       helper);
	g_main_loop_run(helper->loop);
	if (!helper->ret) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return FALSE;
	}
	return TRUE;
}

static void
fwupd_client_get_bios_settings_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->array =
	    fwupd_client_get_bios_settings_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_get_bios_settings:
 * @self: a #FwupdClient
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Gets all the BIOS settings from the daemon.
 *
 * Returns: (element-type FwupdBiosSetting) (transfer container): attributes
 *
 * Since: 1.8.4
 **/
GPtrArray *
fwupd_client_get_bios_settings(FwupdClient *self, GCancellable *cancellable, GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return NULL;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_get_bios_settings_async(self,
					     cancellable,
					     fwupd_client_get_bios_settings_cb,
					     helper);
	g_main_loop_run(helper->loop);
	if (helper->array == NULL) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return NULL;
	}
	return g_steal_pointer(&helper->array);
}

static void
fwupd_client_get_host_security_attrs_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->array =
	    fwupd_client_get_host_security_attrs_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_get_host_security_attrs:
 * @self: a #FwupdClient
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Gets all the host security attributes from the daemon.
 *
 * Returns: (element-type FwupdSecurityAttr) (transfer container): attributes
 *
 * Since: 1.5.0
 **/
GPtrArray *
fwupd_client_get_host_security_attrs(FwupdClient *self, GCancellable *cancellable, GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return NULL;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_get_host_security_attrs_async(self,
						   cancellable,
						   fwupd_client_get_host_security_attrs_cb,
						   helper);
	g_main_loop_run(helper->loop);
	if (helper->array == NULL) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return NULL;
	}
	return g_steal_pointer(&helper->array);
}

static void
fwupd_client_get_host_security_events_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->array =
	    fwupd_client_get_host_security_events_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_get_host_security_events:
 * @self: a #FwupdClient
 * @limit: maximum number of events, or 0 for no limit
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Gets all the host security events from the daemon.
 *
 * Returns: (element-type FwupdSecurityAttr) (transfer container): attributes
 *
 * Since: 1.7.1
 **/
GPtrArray *
fwupd_client_get_host_security_events(FwupdClient *self,
				      guint limit,
				      GCancellable *cancellable,
				      GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return NULL;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_get_host_security_events_async(self,
						    limit,
						    cancellable,
						    fwupd_client_get_host_security_events_cb,
						    helper);
	g_main_loop_run(helper->loop);
	if (helper->array == NULL) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return NULL;
	}
	return g_steal_pointer(&helper->array);
}

static void
fwupd_client_get_device_by_id_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->device =
	    fwupd_client_get_device_by_id_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_get_device_by_id:
 * @self: a #FwupdClient
 * @device_id: the device ID, e.g. `usb:00:01:03:03`
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Gets a device by its device ID.
 *
 * Returns: (transfer full): a device or %NULL
 *
 * Since: 0.9.3
 **/
FwupdDevice *
fwupd_client_get_device_by_id(FwupdClient *self,
			      const gchar *device_id,
			      GCancellable *cancellable,
			      GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(device_id != NULL, NULL);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return NULL;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_get_device_by_id_async(self,
					    device_id,
					    cancellable,
					    fwupd_client_get_device_by_id_cb,
					    helper);
	g_main_loop_run(helper->loop);
	if (helper->device == NULL) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return NULL;
	}
	return g_steal_pointer(&helper->device);
}

static void
fwupd_client_get_devices_by_guid_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->array =
	    fwupd_client_get_devices_by_guid_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_get_devices_by_guid:
 * @self: a #FwupdClient
 * @guid: the GUID, e.g. `e22c4520-43dc-5bb3-8245-5787fead9b63`
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Gets any devices that provide a specific GUID. An error is returned if no
 * devices contains this GUID.
 *
 * Returns: (element-type FwupdDevice) (transfer container): devices or %NULL
 *
 * Since: 1.4.1
 **/
GPtrArray *
fwupd_client_get_devices_by_guid(FwupdClient *self,
				 const gchar *guid,
				 GCancellable *cancellable,
				 GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(guid != NULL, NULL);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return NULL;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_get_devices_by_guid_async(self,
					       guid,
					       cancellable,
					       fwupd_client_get_devices_by_guid_cb,
					       helper);
	g_main_loop_run(helper->loop);
	if (helper->array == NULL) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return NULL;
	}
	return g_steal_pointer(&helper->array);
}

#ifdef HAVE_GIO_UNIX
static void
fwupd_client_install_fd_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->ret = fwupd_client_install_bytes_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}
#endif

/**
 * fwupd_client_install:
 * @self: a #FwupdClient
 * @device_id: (not nullable): the device ID
 * @filename: the filename to install
 * @install_flags: install flags, e.g. %FWUPD_INSTALL_FLAG_ALLOW_REINSTALL
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Install a file onto a specific device.
 *
 * Returns: %TRUE for success
 *
 * Since: 0.7.0
 **/
gboolean
fwupd_client_install(FwupdClient *self,
		     const gchar *device_id,
		     const gchar *filename,
		     FwupdInstallFlags install_flags,
		     GCancellable *cancellable,
		     GError **error)
{
#ifdef HAVE_GIO_UNIX
	g_autoptr(GUnixInputStream) istr = NULL;
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(device_id != NULL, FALSE);
	g_return_val_if_fail(filename != NULL, FALSE);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return FALSE;

	/* move to a thread if this ever takes more than a few ms */
	istr = fwupd_unix_input_stream_from_fn(filename, error);
	if (istr == NULL)
		return FALSE;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_install_stream_async(self,
					  device_id,
					  istr,
					  filename,
					  install_flags,
					  cancellable,
					  fwupd_client_install_fd_cb,
					  helper);
	g_main_loop_run(helper->loop);
	if (!helper->ret) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return FALSE;
	}
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not supported as <glib-unix.h> is unavailable");
	return FALSE;
#endif
}

static void
fwupd_client_install_bytes_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->ret = fwupd_client_install_bytes_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_install_bytes:
 * @self: a #FwupdClient
 * @device_id: (not nullable): the device ID
 * @bytes: cabinet archive
 * @install_flags: install flags, e.g. %FWUPD_INSTALL_FLAG_ALLOW_REINSTALL
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Install firmware onto a specific device.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.5
 **/
gboolean
fwupd_client_install_bytes(FwupdClient *self,
			   const gchar *device_id,
			   GBytes *bytes,
			   FwupdInstallFlags install_flags,
			   GCancellable *cancellable,
			   GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(device_id != NULL, FALSE);
	g_return_val_if_fail(bytes != NULL, FALSE);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return FALSE;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_install_bytes_async(self,
					 device_id,
					 bytes,
					 install_flags,
					 cancellable,
					 fwupd_client_install_bytes_cb,
					 helper);
	g_main_loop_run(helper->loop);
	if (!helper->ret) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return FALSE;
	}
	return TRUE;
}
static void
fwupd_client_install_release_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->ret =
	    fwupd_client_install_release_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_install_release2:
 * @self: a #FwupdClient
 * @device: a device
 * @release: a release
 * @install_flags: install flags, e.g. %FWUPD_INSTALL_FLAG_ALLOW_REINSTALL
 * @download_flags: download flags, e.g. %FWUPD_CLIENT_DOWNLOAD_FLAG_NONE
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Installs a new release on a device, downloading the firmware if required.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.6
 **/
gboolean
fwupd_client_install_release2(FwupdClient *self,
			      FwupdDevice *device,
			      FwupdRelease *release,
			      FwupdInstallFlags install_flags,
			      FwupdClientDownloadFlags download_flags,
			      GCancellable *cancellable,
			      GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(FWUPD_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(FWUPD_IS_RELEASE(release), FALSE);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return FALSE;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_install_release2_async(self,
					    device,
					    release,
					    install_flags,
					    download_flags,
					    cancellable,
					    fwupd_client_install_release_cb,
					    helper);
	g_main_loop_run(helper->loop);
	if (!helper->ret) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return FALSE;
	}
	return TRUE;
}

/**
 * fwupd_client_install_release:
 * @self: a #FwupdClient
 * @device: a device
 * @release: a release
 * @install_flags: install flags, e.g. %FWUPD_INSTALL_FLAG_ALLOW_REINSTALL
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Installs a new release on a device, downloading the firmware if required.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.5
 * Deprecated: 1.5.6
 **/
gboolean
fwupd_client_install_release(FwupdClient *self,
			     FwupdDevice *device,
			     FwupdRelease *release,
			     FwupdInstallFlags install_flags,
			     GCancellable *cancellable,
			     GError **error)
{
	return fwupd_client_install_release2(self,
					     device,
					     release,
					     install_flags,
					     FWUPD_CLIENT_DOWNLOAD_FLAG_NONE,
					     cancellable,
					     error);
}

#ifdef HAVE_GIO_UNIX
static void
fwupd_client_update_metadata_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->ret =
	    fwupd_client_update_metadata_bytes_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}
#endif

/**
 * fwupd_client_update_metadata:
 * @self: a #FwupdClient
 * @remote_id: the remote ID, e.g. `lvfs-testing`
 * @metadata_fn: the XML metadata filename
 * @signature_fn: the GPG signature file
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
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
fwupd_client_update_metadata(FwupdClient *self,
			     const gchar *remote_id,
			     const gchar *metadata_fn,
			     const gchar *signature_fn,
			     GCancellable *cancellable,
			     GError **error)
{
#ifdef HAVE_GIO_UNIX
	g_autoptr(GUnixInputStream) istr = NULL;
	g_autoptr(GUnixInputStream) istr_sig = NULL;
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(remote_id != NULL, FALSE);
	g_return_val_if_fail(metadata_fn != NULL, FALSE);
	g_return_val_if_fail(signature_fn != NULL, FALSE);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return FALSE;

	istr = fwupd_unix_input_stream_from_fn(metadata_fn, error);
	if (istr == NULL)
		return FALSE;
	istr_sig = fwupd_unix_input_stream_from_fn(signature_fn, error);
	if (istr_sig == NULL)
		return FALSE;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_update_metadata_stream_async(self,
						  remote_id,
						  istr,
						  istr_sig,
						  cancellable,
						  fwupd_client_update_metadata_cb,
						  helper);
	g_main_loop_run(helper->loop);
	if (!helper->ret) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return FALSE;
	}
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not supported as <glib-unix.h> is unavailable");
	return FALSE;
#endif
}

static void
fwupd_client_update_metadata_bytes_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->ret =
	    fwupd_client_update_metadata_bytes_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_update_metadata_bytes:
 * @self: a #FwupdClient
 * @remote_id: remote ID, e.g. `lvfs-testing`
 * @metadata: XML metadata data
 * @signature: signature data
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
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
fwupd_client_update_metadata_bytes(FwupdClient *self,
				   const gchar *remote_id,
				   GBytes *metadata,
				   GBytes *signature,
				   GCancellable *cancellable,
				   GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(remote_id != NULL, FALSE);
	g_return_val_if_fail(metadata != NULL, FALSE);
	g_return_val_if_fail(signature != NULL, FALSE);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return FALSE;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_update_metadata_bytes_async(self,
						 remote_id,
						 metadata,
						 signature,
						 cancellable,
						 fwupd_client_update_metadata_bytes_cb,
						 helper);
	g_main_loop_run(helper->loop);
	if (!helper->ret) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return FALSE;
	}
	return TRUE;
}

static void
fwupd_client_refresh_remote_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->ret = fwupd_client_refresh_remote_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_refresh_remote:
 * @self: a #FwupdClient
 * @remote: a #FwupdRemote
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Refreshes a remote by downloading new metadata.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.5
 **/
gboolean
fwupd_client_refresh_remote(FwupdClient *self,
			    FwupdRemote *remote,
			    GCancellable *cancellable,
			    GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(FWUPD_IS_REMOTE(remote), FALSE);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_refresh_remote_async(self,
					  remote,
					  cancellable,
					  fwupd_client_refresh_remote_cb,
					  helper);
	g_main_loop_run(helper->loop);
	if (!helper->ret) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return FALSE;
	}
	return TRUE;
}

static void
fwupd_client_modify_remote_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->ret = fwupd_client_modify_remote_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_modify_remote:
 * @self: a #FwupdClient
 * @remote_id: the remote ID, e.g. `lvfs-testing`
 * @key: the key, e.g. `Enabled`
 * @value: the key, e.g. `true`
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
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
fwupd_client_modify_remote(FwupdClient *self,
			   const gchar *remote_id,
			   const gchar *key,
			   const gchar *value,
			   GCancellable *cancellable,
			   GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(remote_id != NULL, FALSE);
	g_return_val_if_fail(key != NULL, FALSE);
	g_return_val_if_fail(value != NULL, FALSE);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return FALSE;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_modify_remote_async(self,
					 remote_id,
					 key,
					 value,
					 cancellable,
					 fwupd_client_modify_remote_cb,
					 helper);
	g_main_loop_run(helper->loop);
	if (!helper->ret) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return FALSE;
	}
	return TRUE;
}

static void
fwupd_client_get_report_metadata_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->hash =
	    fwupd_client_get_report_metadata_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_get_report_metadata:
 * @self: a #FwupdClient
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Gets all the report metadata from the daemon.
 *
 * Returns: (transfer container): attributes
 *
 * Since: 1.5.0
 **/
GHashTable *
fwupd_client_get_report_metadata(FwupdClient *self, GCancellable *cancellable, GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return NULL;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_get_report_metadata_async(self,
					       cancellable,
					       fwupd_client_get_report_metadata_cb,
					       helper);
	g_main_loop_run(helper->loop);
	if (helper->hash == NULL) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return NULL;
	}
	return g_steal_pointer(&helper->hash);
}

static void
fwupd_client_modify_device_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->ret = fwupd_client_modify_device_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_modify_device:
 * @self: a #FwupdClient
 * @device_id: (not nullable): the device ID
 * @key: (not nullable): the key, e.g. `Flags`
 * @value: (not nullable): the key, e.g. `reported`
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
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
fwupd_client_modify_device(FwupdClient *self,
			   const gchar *device_id,
			   const gchar *key,
			   const gchar *value,
			   GCancellable *cancellable,
			   GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(device_id != NULL, FALSE);
	g_return_val_if_fail(key != NULL, FALSE);
	g_return_val_if_fail(value != NULL, FALSE);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return FALSE;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_modify_device_async(self,
					 device_id,
					 key,
					 value,
					 cancellable,
					 fwupd_client_modify_device_cb,
					 helper);
	g_main_loop_run(helper->loop);
	if (!helper->ret) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return FALSE;
	}
	return TRUE;
}

static void
fwupd_client_get_remotes_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->array = fwupd_client_get_remotes_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_get_remotes:
 * @self: a #FwupdClient
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Gets the list of remotes that have been configured for the system.
 *
 * Returns: (element-type FwupdRemote) (transfer container): list of remotes, or %NULL
 *
 * Since: 0.9.3
 **/
GPtrArray *
fwupd_client_get_remotes(FwupdClient *self, GCancellable *cancellable, GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return NULL;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_get_remotes_async(self, cancellable, fwupd_client_get_remotes_cb, helper);
	g_main_loop_run(helper->loop);
	if (helper->array == NULL) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return NULL;
	}
	return g_steal_pointer(&helper->array);
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

/**
 * fwupd_client_get_remote_by_id:
 * @self: a #FwupdClient
 * @remote_id: the remote ID, e.g. `lvfs-testing`
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Gets a specific remote that has been configured for the system.
 *
 * Returns: (transfer full): a #FwupdRemote, or %NULL if not found
 *
 * Since: 0.9.3
 **/
FwupdRemote *
fwupd_client_get_remote_by_id(FwupdClient *self,
			      const gchar *remote_id,
			      GCancellable *cancellable,
			      GError **error)
{
	FwupdRemote *remote;
	g_autoptr(GPtrArray) remotes = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(remote_id != NULL, NULL);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* find remote in list */
	remotes = fwupd_client_get_remotes(self, cancellable, error);
	if (remotes == NULL)
		return NULL;
	remote = fwupd_client_get_remote_by_id_noref(remotes, remote_id);
	if (remote == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "No remote '%s' found in search paths",
			    remote_id);
		return NULL;
	}

	/* success */
	return g_object_ref(remote);
}

static void
fwupd_client_get_approved_firmware_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->array =
	    fwupd_client_get_approved_firmware_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_get_approved_firmware:
 * @self: a #FwupdClient
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Gets the list of approved firmware.
 *
 * Returns: (transfer full): checksums, or %NULL for error
 *
 * Since: 1.2.6
 **/
gchar **
fwupd_client_get_approved_firmware(FwupdClient *self, GCancellable *cancellable, GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;
	gchar **argv;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return NULL;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_get_approved_firmware_async(self,
						 cancellable,
						 fwupd_client_get_approved_firmware_cb,
						 helper);
	g_main_loop_run(helper->loop);
	if (helper->array == NULL) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return NULL;
	}
	argv = g_new0(gchar *, helper->array->len + 1);
	for (guint i = 0; i < helper->array->len; i++) {
		const gchar *tmp = g_ptr_array_index(helper->array, i);
		argv[i] = g_strdup(tmp);
	}
	return argv;
}

static void
fwupd_client_set_approved_firmware_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->ret =
	    fwupd_client_set_approved_firmware_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_set_approved_firmware:
 * @self: a #FwupdClient
 * @checksums: (not nullable): Array of checksums
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Sets the list of approved firmware.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.2.6
 **/
gboolean
fwupd_client_set_approved_firmware(FwupdClient *self,
				   gchar **checksums,
				   GCancellable *cancellable,
				   GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;
	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func(g_free);

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(checksums != NULL, FALSE);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return FALSE;

	/* convert */
	for (guint i = 0; checksums[i] != NULL; i++)
		g_ptr_array_add(array, g_strdup(checksums[i]));

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_set_approved_firmware_async(self,
						 array,
						 cancellable,
						 fwupd_client_set_approved_firmware_cb,
						 helper);
	g_main_loop_run(helper->loop);
	if (!helper->ret) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return FALSE;
	}
	return TRUE;
}

static void
fwupd_client_get_blocked_firmware_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->array =
	    fwupd_client_get_blocked_firmware_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_get_blocked_firmware:
 * @self: a #FwupdClient
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Gets the list of blocked firmware.
 *
 * Returns: (transfer full): checksums, or %NULL for error
 *
 * Since: 1.4.6
 **/
gchar **
fwupd_client_get_blocked_firmware(FwupdClient *self, GCancellable *cancellable, GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;
	gchar **argv;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return NULL;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_get_blocked_firmware_async(self,
						cancellable,
						fwupd_client_get_blocked_firmware_cb,
						helper);
	g_main_loop_run(helper->loop);
	if (helper->array == NULL) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return NULL;
	}
	argv = g_new0(gchar *, helper->array->len + 1);
	for (guint i = 0; i < helper->array->len; i++) {
		const gchar *tmp = g_ptr_array_index(helper->array, i);
		argv[i] = g_strdup(tmp);
	}
	return argv;
}

static void
fwupd_client_set_blocked_firmware_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->ret =
	    fwupd_client_set_blocked_firmware_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_set_blocked_firmware:
 * @self: a #FwupdClient
 * @checksums: Array of checksums
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Sets the list of approved firmware.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.6
 **/
gboolean
fwupd_client_set_blocked_firmware(FwupdClient *self,
				  gchar **checksums,
				  GCancellable *cancellable,
				  GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;
	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func(g_free);

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(checksums != NULL, FALSE);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return FALSE;

	for (guint i = 0; checksums[i] != NULL; i++)
		g_ptr_array_add(array, g_strdup(checksums[i]));

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_set_blocked_firmware_async(self,
						array,
						cancellable,
						fwupd_client_set_blocked_firmware_cb,
						helper);
	g_main_loop_run(helper->loop);
	if (!helper->ret) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return FALSE;
	}
	return TRUE;
}

static void
fwupd_client_set_feature_flags_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->ret =
	    fwupd_client_set_feature_flags_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_set_feature_flags:
 * @self: a #FwupdClient
 * @feature_flags: feature flags, e.g. %FWUPD_FEATURE_FLAG_UPDATE_TEXT
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
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
fwupd_client_set_feature_flags(FwupdClient *self,
			       FwupdFeatureFlags feature_flags,
			       GCancellable *cancellable,
			       GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return FALSE;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_set_feature_flags_async(self,
					     feature_flags,
					     cancellable,
					     fwupd_client_set_feature_flags_cb,
					     helper);
	g_main_loop_run(helper->loop);
	if (!helper->ret) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return FALSE;
	}
	return TRUE;
}

static void
fwupd_client_self_sign_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->str = fwupd_client_self_sign_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_self_sign:
 * @self: a #FwupdClient
 * @value: (not nullable): a string to sign, typically a JSON blob
 * @flags: signing flags, e.g. %FWUPD_SELF_SIGN_FLAG_ADD_TIMESTAMP
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Signs the data using the client self-signed certificate.
 *
 * Returns: a signature, or %NULL for failure
 *
 * Since: 1.2.6
 **/
gchar *
fwupd_client_self_sign(FwupdClient *self,
		       const gchar *value,
		       FwupdSelfSignFlags flags,
		       GCancellable *cancellable,
		       GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(value != NULL, NULL);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return NULL;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_self_sign_async(self,
				     value,
				     flags,
				     cancellable,
				     fwupd_client_self_sign_cb,
				     helper);
	g_main_loop_run(helper->loop);
	if (helper->str == NULL) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return NULL;
	}
	return g_steal_pointer(&helper->str);
}

static void
fwupd_client_download_bytes_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->bytes =
	    fwupd_client_download_bytes_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_download_bytes:
 * @self: a #FwupdClient
 * @url: (not nullable): the remote URL
 * @flags: download flags, e.g. %FWUPD_CLIENT_DOWNLOAD_FLAG_NONE
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Downloads data from a remote server. The [method@Client.set_user_agent] function
 * should be called before this method is used.
 *
 * Returns: (transfer full): downloaded data, or %NULL for error
 *
 * Since: 1.4.5
 **/
GBytes *
fwupd_client_download_bytes(FwupdClient *self,
			    const gchar *url,
			    FwupdClientDownloadFlags flags,
			    GCancellable *cancellable,
			    GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(url != NULL, NULL);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	g_return_val_if_fail(fwupd_client_get_user_agent(self) != NULL, NULL);

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_download_bytes_async(self,
					  url,
					  flags,
					  cancellable,
					  fwupd_client_download_bytes_cb,
					  helper);
	g_main_loop_run(helper->loop);
	if (helper->bytes == NULL) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return NULL;
	}
	return g_steal_pointer(&helper->bytes);
}

/**
 * fwupd_client_download_file:
 * @self: a #FwupdClient
 * @url: (not nullable): the remote URL
 * @file: (not nullable): a file
 * @flags: download flags, e.g. %FWUPD_CLIENT_DOWNLOAD_FLAG_NONE
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Downloads data from a remote server. The [method@Client.set_user_agent] function
 * should be called before this method is used.
 *
 * Returns: %TRUE if the file was written
 *
 * Since: 1.5.2
 **/
gboolean
fwupd_client_download_file(FwupdClient *self,
			   const gchar *url,
			   GFile *file,
			   FwupdClientDownloadFlags flags,
			   GCancellable *cancellable,
			   GError **error)
{
	gssize size;
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GOutputStream) ostream = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(url != NULL, FALSE);
	g_return_val_if_fail(G_IS_FILE(file), FALSE);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail(fwupd_client_get_user_agent(self) != NULL, FALSE);

	/* download then write */
	bytes = fwupd_client_download_bytes(self, url, flags, cancellable, error);
	if (bytes == NULL)
		return FALSE;
	ostream =
	    G_OUTPUT_STREAM(g_file_replace(file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, error));
	if (ostream == NULL)
		return FALSE;
	size = g_output_stream_write_bytes(ostream, bytes, NULL, error);
	if (size < 0)
		return FALSE;

	/* success */
	return TRUE;
}

static void
fwupd_client_upload_bytes_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	FwupdClientHelper *helper = (FwupdClientHelper *)user_data;
	helper->bytes = fwupd_client_upload_bytes_finish(FWUPD_CLIENT(source), res, &helper->error);
	g_main_loop_quit(helper->loop);
}

/**
 * fwupd_client_upload_bytes:
 * @self: a #FwupdClient
 * @url: (not nullable): the remote URL
 * @payload: (not nullable): payload string
 * @signature: (nullable): signature string
 * @flags: download flags, e.g. %FWUPD_CLIENT_DOWNLOAD_FLAG_NONE
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Uploads data to a remote server. The [method@Client.set_user_agent] function
 * should be called before this method is used.
 *
 * Returns: (transfer full): response data, or %NULL for error
 *
 * Since: 1.4.5
 **/
GBytes *
fwupd_client_upload_bytes(FwupdClient *self,
			  const gchar *url,
			  const gchar *payload,
			  const gchar *signature,
			  FwupdClientUploadFlags flags,
			  GCancellable *cancellable,
			  GError **error)
{
	g_autoptr(FwupdClientHelper) helper = NULL;

	g_return_val_if_fail(FWUPD_IS_CLIENT(self), NULL);
	g_return_val_if_fail(url != NULL, NULL);
	g_return_val_if_fail(payload != NULL, NULL);
	g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* connect */
	if (!fwupd_client_connect(self, cancellable, error))
		return NULL;

	/* call async version and run loop until complete */
	helper = fwupd_client_helper_new(self);
	fwupd_client_upload_bytes_async(self,
					url,
					payload,
					signature,
					flags,
					cancellable,
					fwupd_client_upload_bytes_cb,
					helper);
	g_main_loop_run(helper->loop);
	if (helper->bytes == NULL) {
		g_propagate_error(error, g_steal_pointer(&helper->error));
		return NULL;
	}
	return g_steal_pointer(&helper->bytes);
}
