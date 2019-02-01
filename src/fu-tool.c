/*
 * Copyright (C) 2015-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuMain"

#include "config.h"

#include <fwupd.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <glib-unix.h>
#include <locale.h>
#include <stdlib.h>
#include <unistd.h>
#include <libsoup/soup.h>

#include "fu-device-private.h"
#include "fu-engine.h"
#include "fu-plugin-private.h"
#include "fu-progressbar.h"
#include "fu-smbios.h"
#include "fu-util-common.h"
#include "fu-debug.h"
#include "fwupd-common-private.h"

#define SYSTEMD_SERVICE			"org.freedesktop.systemd1"
#define SYSTEMD_OBJECT_PATH		"/org/freedesktop/systemd1"
#define SYSTEMD_MANAGER_INTERFACE	"org.freedesktop.systemd1.Manager"
#define SYSTEMD_FWUPD_UNIT		"fwupd.service"

/* custom return code */
#define EXIT_NOTHING_TO_DO		2

typedef enum {
	FU_UTIL_OPERATION_UNKNOWN,
	FU_UTIL_OPERATION_UPDATE,
	FU_UTIL_OPERATION_INSTALL,
	FU_UTIL_OPERATION_LAST
} FuUtilOperation;

typedef struct {
	GCancellable		*cancellable;
	GMainLoop		*loop;
	GOptionContext		*context;
	GPtrArray		*cmd_array;
	FuEngine		*engine;
	FuProgressbar		*progressbar;
	gboolean		 no_reboot_check;
	gboolean		 prepare_blob;
	gboolean		 cleanup_blob;
	FwupdInstallFlags	 flags;
	gboolean		 show_all_devices;
	/* only valid in update and downgrade */
	FuUtilOperation		 current_operation;
	FwupdDevice		*current_device;
	gchar			*current_message;
	FwupdDeviceFlags	 completion_flags;
} FuUtilPrivate;

typedef gboolean (*FuUtilPrivateCb)	(FuUtilPrivate	*util,
					 gchar		**values,
					 GError		**error);

typedef struct {
	gchar		*name;
	gchar		*arguments;
	gchar		*description;
	FuUtilPrivateCb	 callback;
} FuUtilItem;

static void
fu_util_item_free (FuUtilItem *item)
{
	g_free (item->name);
	g_free (item->arguments);
	g_free (item->description);
	g_free (item);
}

static gboolean
fu_util_start_engine (FuUtilPrivate *priv, GError **error)
{
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GVariant) val = NULL;
	g_autoptr(GError) error_local = NULL;

	/* try to stop any already running daemon */
	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
	if (connection == NULL)
		return FALSE;
	proxy = g_dbus_proxy_new_sync (connection,
					G_DBUS_PROXY_FLAGS_NONE,
					NULL,
					SYSTEMD_SERVICE,
					SYSTEMD_OBJECT_PATH,
					SYSTEMD_MANAGER_INTERFACE,
					NULL,
					&error_local);
	if (proxy == NULL) {
		g_debug ("Failed to find %s: %s",
			 SYSTEMD_SERVICE,
			 error_local->message);
	} else {
		val = g_dbus_proxy_call_sync (proxy,
					      "GetUnit",
					      g_variant_new ("(s)",
							     SYSTEMD_FWUPD_UNIT),
					      G_DBUS_CALL_FLAGS_NONE,
					      -1,
					      NULL,
					      &error_local);
		if (val == NULL) {
			g_debug ("Unable to find %s: %s",
				 SYSTEMD_FWUPD_UNIT,
				 error_local->message);
		} else {
			g_variant_unref (val);
			val = g_dbus_proxy_call_sync (proxy,
						      "StopUnit",
						      g_variant_new ("(ss)",
								     SYSTEMD_FWUPD_UNIT,
								     "replace"),
						      G_DBUS_CALL_FLAGS_NONE,
						      -1,
						      NULL,
						      error);
			if (val == NULL)
				return FALSE;
		}
	}

	if (!fu_engine_load (priv->engine, error))
		return FALSE;
	if (fu_engine_get_tainted (priv->engine)) {
		g_printerr ("WARNING: This tool has loaded 3rd party code and "
			    "is no longer supported by the upstream developers!\n");
	}
	return TRUE;
}

static gint
fu_sort_command_name_cb (FuUtilItem **item1, FuUtilItem **item2)
{
	return g_strcmp0 ((*item1)->name, (*item2)->name);
}

static void
fu_util_maybe_prefix_sandbox_error (const gchar *value, GError **error)
{
	g_autofree gchar *path = g_path_get_dirname (value);
	if (!g_file_test (path, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR)) {
		g_prefix_error (error,
				"Unable to access %s. You may need to copy %s to %s: ",
				path, value, g_getenv ("HOME"));
	}
}

static void
fu_util_add (GPtrArray *array,
	     const gchar *name,
	     const gchar *arguments,
	     const gchar *description,
	     FuUtilPrivateCb callback)
{
	g_auto(GStrv) names = NULL;

	g_return_if_fail (name != NULL);
	g_return_if_fail (description != NULL);
	g_return_if_fail (callback != NULL);

	/* add each one */
	names = g_strsplit (name, ",", -1);
	for (guint i = 0; names[i] != NULL; i++) {
		FuUtilItem *item = g_new0 (FuUtilItem, 1);
		item->name = g_strdup (names[i]);
		if (i == 0) {
			item->description = g_strdup (description);
		} else {
			/* TRANSLATORS: this is a command alias, e.g. 'get-devices' */
			item->description = g_strdup_printf (_("Alias to %s"),
							     names[0]);
		}
		item->arguments = g_strdup (arguments);
		item->callback = callback;
		g_ptr_array_add (array, item);
	}
}

static gchar *
fu_util_get_descriptions (GPtrArray *array)
{
	gsize len;
	const gsize max_len = 35;
	GString *string;

	/* print each command */
	string = g_string_new ("");
	for (guint i = 0; i < array->len; i++) {
		FuUtilItem *item = g_ptr_array_index (array, i);
		g_string_append (string, "  ");
		g_string_append (string, item->name);
		len = strlen (item->name) + 2;
		if (item->arguments != NULL) {
			g_string_append (string, " ");
			g_string_append (string, item->arguments);
			len += strlen (item->arguments) + 1;
		}
		if (len < max_len) {
			for (gsize j = len; j < max_len + 1; j++)
				g_string_append_c (string, ' ');
			g_string_append (string, item->description);
			g_string_append_c (string, '\n');
		} else {
			g_string_append_c (string, '\n');
			for (gsize j = 0; j < max_len + 1; j++)
				g_string_append_c (string, ' ');
			g_string_append (string, item->description);
			g_string_append_c (string, '\n');
		}
	}

	/* remove trailing newline */
	if (string->len > 0)
		g_string_set_size (string, string->len - 1);

	return g_string_free (string, FALSE);
}

static gboolean
fu_util_run (FuUtilPrivate *priv, const gchar *command, gchar **values, GError **error)
{
	/* find command */
	for (guint i = 0; i < priv->cmd_array->len; i++) {
		FuUtilItem *item = g_ptr_array_index (priv->cmd_array, i);
		if (g_strcmp0 (item->name, command) == 0)
			return item->callback (priv, values, error);
	}

	/* not found */
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_ARGS,
			     /* TRANSLATORS: error message */
			     _("Command not found"));
	return FALSE;
}

static void
fu_util_cancelled_cb (GCancellable *cancellable, gpointer user_data)
{
	FuUtilPrivate *priv = (FuUtilPrivate *) user_data;
	/* TRANSLATORS: this is when a device ctrl+c's a watch */
	g_print ("%s\n", _("Cancelled"));
	g_main_loop_quit (priv->loop);
}

static gboolean
fu_util_smbios_dump (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autofree gchar *tmp = NULL;
	g_autoptr(FuSmbios) smbios = NULL;
	if (g_strv_length (values) < 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments");
		return FALSE;
	}
	smbios = fu_smbios_new ();
	if (!fu_smbios_setup_from_file (smbios, values[0], error))
		return FALSE;
	tmp = fu_smbios_to_string (smbios);
	g_print ("%s\n", tmp);
	return TRUE;
}

static gboolean
fu_util_sigint_cb (gpointer user_data)
{
	FuUtilPrivate *priv = (FuUtilPrivate *) user_data;
	g_debug ("Handling SIGINT");
	g_cancellable_cancel (priv->cancellable);
	return FALSE;
}

static void
fu_util_private_free (FuUtilPrivate *priv)
{
	if (priv->cmd_array != NULL)
		g_ptr_array_unref (priv->cmd_array);
	if (priv->current_device != NULL)
		g_object_unref (priv->current_device);
	if (priv->engine != NULL)
		g_object_unref (priv->engine);
	if (priv->loop != NULL)
		g_main_loop_unref (priv->loop);
	if (priv->cancellable != NULL)
		g_object_unref (priv->cancellable);
	if (priv->progressbar != NULL)
		g_object_unref (priv->progressbar);
	if (priv->context != NULL)
		g_option_context_free (priv->context);
	g_free (priv->current_message);
	g_free (priv);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuUtilPrivate, fu_util_private_free)
#pragma clang diagnostic pop


static void
fu_main_engine_device_added_cb (FuEngine *engine,
				FuDevice *device,
				FuUtilPrivate *priv)
{
	g_autofree gchar *tmp = fu_device_to_string (device);
	g_debug ("ADDED:\n%s", tmp);
}

static void
fu_main_engine_device_removed_cb (FuEngine *engine,
				  FuDevice *device,
				  FuUtilPrivate *priv)
{
	g_autofree gchar *tmp = fu_device_to_string (device);
	g_debug ("REMOVED:\n%s", tmp);
}

static void
fu_main_engine_status_changed_cb (FuEngine *engine,
				  FwupdStatus status,
				  FuUtilPrivate *priv)
{
	fu_progressbar_update (priv->progressbar, status, 0);
}

static void
fu_main_engine_percentage_changed_cb (FuEngine *engine,
				      guint percentage,
				      FuUtilPrivate *priv)
{
	fu_progressbar_update (priv->progressbar, FWUPD_STATUS_UNKNOWN, percentage);
}

static gboolean
fu_util_watch (FuUtilPrivate *priv, gchar **values, GError **error)
{
	if (!fu_util_start_engine (priv, error))
		return FALSE;
	g_main_loop_run (priv->loop);
	return TRUE;
}

static gint
fu_util_plugin_name_sort_cb (FuPlugin **item1, FuPlugin **item2)
{
	return fu_plugin_name_compare (*item1, *item2);
}

static gboolean
fu_util_get_plugins (FuUtilPrivate *priv, gchar **values, GError **error)
{
	GPtrArray *plugins;
	guint cnt = 0;

	/* load engine */
	if (!fu_engine_load_plugins (priv->engine, error))
		return FALSE;

	/* print */
	plugins = fu_engine_get_plugins (priv->engine);
	g_ptr_array_sort (plugins, (GCompareFunc) fu_util_plugin_name_sort_cb);
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index (plugins, i);
		if (!fu_plugin_get_enabled (plugin))
			continue;
		g_print ("%s\n", fu_plugin_get_name (plugin));
		cnt++;
	}
	if (cnt == 0) {
		/* TRANSLATORS: nothing found */
		g_print ("%s\n", _("No plugins found"));
		return TRUE;
	}

	return TRUE;
}

static gboolean
fu_util_get_updates (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;

	/* load engine */
	if (!fu_util_start_engine (priv, error))
		return FALSE;

	/* get devices from daemon */
	devices = fu_engine_get_devices (priv->engine, error);
	if (devices == NULL)
		return FALSE;
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (devices, i);
		g_autoptr(GPtrArray) rels = NULL;
		g_autoptr(GError) error_local = NULL;

		/* not going to have results, so save a D-Bus round-trip */
		if (!fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_SUPPORTED))
			continue;

		/* get the releases for this device and filter for validity */
		rels = fu_engine_get_upgrades (priv->engine,
					       fwupd_device_get_id (dev),
					       &error_local);
		if (rels == NULL) {
			g_printerr ("%s\n", error_local->message);
			continue;
		}
		g_print ("%s", fwupd_device_to_string (dev));
		g_print (" Release information:\n");
		/* print all releases */
		for (guint j = 0; j < rels->len; j++) {
			FwupdRelease *rel = g_ptr_array_index (rels, j);
			g_print ("%s\n", fwupd_release_to_string (rel));
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_util_get_details (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) array = NULL;
	gint fd;

	/* load engine */
	if (!fu_util_start_engine (priv, error))
		return FALSE;

	/* check args */
	if (g_strv_length (values) != 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments");
		return FALSE;
	}

	/* open file */
	fd = open (values[0], O_RDONLY);
	if (fd < 0) {
		fu_util_maybe_prefix_sandbox_error (values[0], error);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to open %s",
			     values[0]);
		return FALSE;
	}
	array = fu_engine_get_details (priv->engine, fd, error);
	close (fd);

	if (array == NULL)
		return FALSE;
	for (guint i = 0; i < array->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (array, i);
		g_autofree gchar *tmp = NULL;
		tmp = fwupd_device_to_string (dev);
		g_print ("%s\n", tmp);
	}
	return TRUE;
}

static gboolean
fu_util_get_devices (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) devs = NULL;

	/* load engine */
	if (!fu_util_start_engine (priv, error))
		return FALSE;

	/* print */
	devs = fu_engine_get_devices (priv->engine, error);
	if (devs == NULL)
		return FALSE;
	if (devs->len == 0) {
		/* TRANSLATORS: nothing attached */
		g_print ("%s\n", _("No hardware detected with firmware update capability"));
		return TRUE;
	}
	for (guint i = 0; i < devs->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (devs, i);
		if (priv->show_all_devices || fu_util_is_interesting_device (dev)) {
			g_autofree gchar *tmp = fwupd_device_to_string (dev);
			g_print ("%s\n", tmp);
		}
	}

	return TRUE;
}

static void
fu_util_build_device_tree (FuUtilPrivate *priv, GNode *root, GPtrArray *devs, FuDevice *dev)
{
	for (guint i = 0; i < devs->len; i++) {
		FuDevice *dev_tmp = g_ptr_array_index (devs, i);
		if (!priv->show_all_devices &&
		    !fu_util_is_interesting_device (FWUPD_DEVICE (dev_tmp)))
			continue;
		if (fu_device_get_parent (dev_tmp) == dev) {
			GNode *child = g_node_append_data (root, dev_tmp);
			fu_util_build_device_tree (priv, child, devs, dev_tmp);
		}
	}
}

static gboolean
fu_util_get_topology (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GNode) root = g_node_new (NULL);
	g_autoptr(GPtrArray) devs = NULL;

	/* load engine */
	if (!fu_util_start_engine (priv, error))
		return FALSE;

	/* print */
	devs = fu_engine_get_devices (priv->engine, error);
	if (devs == NULL)
		return FALSE;

	/* print */
	if (devs->len == 0) {
		/* TRANSLATORS: nothing attached that can be upgraded */
		g_print ("%s\n", _("No hardware detected with firmware update capability"));
		return TRUE;
	}
	fu_util_build_device_tree (priv, root, devs, NULL);
	g_node_traverse (root, G_PRE_ORDER, G_TRAVERSE_ALL, -1,
			 fu_util_print_device_tree, priv);

	return TRUE;
}


static FuDevice *
fu_util_prompt_for_device (FuUtilPrivate *priv, GError **error)
{
	FuDevice *dev;
	guint idx;
	g_autoptr(GPtrArray) devices = NULL;

	/* get devices from daemon */
	devices = fu_engine_get_devices (priv->engine, error);
	if (devices == NULL)
		return NULL;

	/* exactly one */
	if (devices->len == 1) {
		dev = g_ptr_array_index (devices, 0);
		return g_object_ref (dev);
	}

	/* TRANSLATORS: get interactive prompt */
	g_print ("%s\n", _("Choose a device:"));
	/* TRANSLATORS: this is to abort the interactive prompt */
	g_print ("0.\t%s\n", _("Cancel"));
	for (guint i = 0; i < devices->len; i++) {
		dev = g_ptr_array_index (devices, i);
		g_print ("%u.\t%s (%s)\n",
			 i + 1,
			 fu_device_get_id (dev),
			 fu_device_get_name (dev));
	}
	idx = fu_util_prompt_for_number (devices->len);
	if (idx == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO,
				     "Request canceled");
		return NULL;
	}
	dev = g_ptr_array_index (devices, idx - 1);
	return g_object_ref (dev);
}

static void
fu_util_update_device_changed_cb (FwupdClient *client,
				  FwupdDevice *device,
				  FuUtilPrivate *priv)
{
	g_autofree gchar *str = NULL;

	/* same as last time, so ignore */
	if (priv->current_device != NULL &&
	    fwupd_device_compare (priv->current_device, device) == 0)
		return;

	/* show message in progressbar */
	if (priv->current_operation == FU_UTIL_OPERATION_UPDATE) {
		/* TRANSLATORS: %1 is a device name */
		str = g_strdup_printf (_("Updating %s…"),
				       fwupd_device_get_name (device));
		fu_progressbar_set_title (priv->progressbar, str);
	} else if (priv->current_operation == FU_UTIL_OPERATION_INSTALL) {
		/* TRANSLATORS: %1 is a device name  */
		str = g_strdup_printf (_("Installing on %s…"),
				       fwupd_device_get_name (device));
		fu_progressbar_set_title (priv->progressbar, str);
	} else {
		g_warning ("no FuUtilOperation set");
	}
	g_set_object (&priv->current_device, device);

	if (fwupd_device_has_flag (device, FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN))
		priv->completion_flags |= FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN;
	else if (fwupd_device_has_flag (device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT))
		priv->completion_flags |= FWUPD_DEVICE_FLAG_NEEDS_REBOOT;

	if (priv->current_message == NULL) {
		const gchar *tmp = fwupd_device_get_update_message (priv->current_device);
		if (tmp != NULL)
			priv->current_message = g_strdup (tmp);
	}
}

static void
fu_util_display_current_message (FuUtilPrivate *priv)
{
	if (priv->current_message == NULL)
		return;
	g_print ("%s\n", priv->current_message);
	g_clear_pointer (&priv->current_message, g_free);
}

static gboolean
fu_util_install_blob (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(GBytes) blob_fw = NULL;

	/* invalid args */
	if (g_strv_length (values) == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments");
		return FALSE;
	}

	/* parse blob */
	blob_fw = fu_common_get_contents_bytes (values[0], error);
	if (blob_fw == NULL) {
		fu_util_maybe_prefix_sandbox_error (values[0], error);
		return FALSE;
	}

	/* load engine */
	if (!fu_util_start_engine (priv, error))
		return FALSE;

	/* get device */
	if (g_strv_length (values) >= 2) {
		device = fu_engine_get_device (priv->engine, values[1], error);
		if (device == NULL)
			return FALSE;
	} else {
		device = fu_util_prompt_for_device (priv, error);
		if (device == NULL)
			return FALSE;
	}

	priv->current_operation = FU_UTIL_OPERATION_INSTALL;
	g_signal_connect (priv->engine, "device-changed",
			  G_CALLBACK (fu_util_update_device_changed_cb), priv);

	/* write bare firmware */
	if (priv->prepare_blob) {
		g_autoptr(GPtrArray) devices = NULL;
		devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
		g_ptr_array_add (devices, g_object_ref (device));
		if (!fu_engine_composite_prepare (priv->engine, devices, error)) {
			g_prefix_error (error, "failed to prepare composite action: ");
			return FALSE;
		}
	}
	if (!fu_engine_install_blob (priv->engine, device,
				     NULL, /* blob_cab */
				     blob_fw,
				     NULL, /* version */
				     priv->flags,
				     error))
		return FALSE;
	if (priv->cleanup_blob) {
		g_autoptr(FuDevice) device_new = NULL;
		g_autoptr(GError) error_local = NULL;

		/* get the possibly new device from the old ID */
		device_new = fu_engine_get_device (priv->engine,
						   fu_device_get_id (device),
						   &error_local);
		if (device_new == NULL) {
			g_debug ("failed to find new device: %s",
				 error_local->message);
		} else {
			g_autoptr(GPtrArray) devices_new = NULL;
			g_ptr_array_add (devices_new, g_steal_pointer (&device_new));
			if (!fu_engine_composite_cleanup (priv->engine, devices_new, error)) {
				g_prefix_error (error, "failed to cleanup composite action: ");
				return FALSE;
			}
		}
	}

	fu_util_display_current_message (priv);

	/* success */
	return fu_util_prompt_complete (priv->completion_flags, TRUE, error);
}

static gint
fu_util_install_task_sort_cb (gconstpointer a, gconstpointer b)
{
	FuInstallTask *task1 = *((FuInstallTask **) a);
	FuInstallTask *task2 = *((FuInstallTask **) b);
	return fu_install_task_compare (task1, task2);
}

static gboolean
fu_util_download_out_of_process (const gchar *uri, const gchar *fn, GError **error)
{
	const gchar *argv[][5] = { { "wget", uri, "-o", fn, NULL },
				   { "curl", uri, "--output", fn, NULL },
				   { NULL } };
	for (guint i = 0; argv[i][0] != NULL; i++) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_common_find_program_in_path (argv[i][0], &error_local)) {
			g_debug ("%s", error_local->message);
			continue;
		}
		return fu_common_spawn_sync (argv[i], NULL, NULL, NULL, error);
	}
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "no supported out-of-process downloaders found");
	return FALSE;
}

static gchar *
fu_util_download_if_required (FuUtilPrivate *priv, const gchar *perhapsfn, GError **error)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(SoupURI) uri = NULL;

	/* a local file */
	uri = soup_uri_new (perhapsfn);
	if (uri == NULL)
		return g_strdup (perhapsfn);

	/* download the firmware to a cachedir */
	filename = fu_util_get_user_cache_path (perhapsfn);
	if (!fu_common_mkdir_parent (filename, error))
		return NULL;
	if (!fu_util_download_out_of_process (perhapsfn, filename, error))
		return NULL;
	return g_steal_pointer (&filename);
}

static gboolean
fu_util_install (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(GBytes) blob_cab = NULL;
	g_autoptr(GPtrArray) components = NULL;
	g_autoptr(GPtrArray) devices_possible = NULL;
	g_autoptr(GPtrArray) errors = NULL;
	g_autoptr(GPtrArray) install_tasks = NULL;
	g_autoptr(XbSilo) silo = NULL;

	/* load engine */
	if (!fu_util_start_engine (priv, error))
		return FALSE;

	/* handle both forms */
	if (g_strv_length (values) == 1) {
		devices_possible = fu_engine_get_devices (priv->engine, error);
		if (devices_possible == NULL)
			return FALSE;
	} else if (g_strv_length (values) == 2) {
		FuDevice *device = fu_engine_get_device (priv->engine,
							 values[1],
							 error);
		if (device == NULL)
			return FALSE;
		devices_possible = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
		g_ptr_array_add (devices_possible, device);
	} else {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments");
		return FALSE;
	}

	/* download if required */
	filename = fu_util_download_if_required (priv, values[0], error);
	if (filename == NULL)
		return FALSE;

	/* parse silo */
	blob_cab = fu_common_get_contents_bytes (filename, error);
	if (blob_cab == NULL) {
		fu_util_maybe_prefix_sandbox_error (filename, error);
		return FALSE;
	}
	silo = fu_engine_get_silo_from_blob (priv->engine, blob_cab, error);
	if (silo == NULL)
		return FALSE;
	components = xb_silo_query (silo, "components/component", 0, error);
	if (components == NULL)
		return FALSE;

	/* for each component in the silo */
	errors = g_ptr_array_new_with_free_func ((GDestroyNotify) g_error_free);
	install_tasks = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index (components, i);

		/* do any devices pass the requirements */
		for (guint j = 0; j < devices_possible->len; j++) {
			FuDevice *device = g_ptr_array_index (devices_possible, j);
			g_autoptr(FuInstallTask) task = NULL;
			g_autoptr(GError) error_local = NULL;

			/* is this component valid for the device */
			task = fu_install_task_new (device, component);
			if (!fu_engine_check_requirements (priv->engine,
							   task, priv->flags,
							   &error_local)) {
				g_debug ("requirement on %s:%s failed: %s",
					 fu_device_get_id (device),
					 xb_node_query_text (component, "id", NULL),
					 error_local->message);
				g_ptr_array_add (errors, g_steal_pointer (&error_local));
				continue;
			}

			/* if component should have an update message from CAB */
			fu_device_incorporate_from_component (device, component);

			/* success */
			g_ptr_array_add (install_tasks, g_steal_pointer (&task));
		}
	}

	/* order the install tasks by the device priority */
	g_ptr_array_sort (install_tasks, fu_util_install_task_sort_cb);

	/* nothing suitable */
	if (install_tasks->len == 0) {
		GError *error_tmp = fu_common_error_array_get_best (errors);
		g_propagate_error (error, error_tmp);
		return FALSE;
	}

	priv->current_operation = FU_UTIL_OPERATION_INSTALL;
	g_signal_connect (priv->engine, "device-changed",
			  G_CALLBACK (fu_util_update_device_changed_cb), priv);

	/* install all the tasks */
	if (!fu_engine_install_tasks (priv->engine, install_tasks, blob_cab, priv->flags, error))
		return FALSE;

	fu_util_display_current_message (priv);

	/* we don't want to ask anything */
	if (priv->no_reboot_check) {
		g_debug ("skipping reboot check");
		return TRUE;
	}

	/* success */
	return fu_util_prompt_complete (priv->completion_flags, TRUE, error);
}

static gboolean
fu_util_update (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;

	/* load engine */
	if (!fu_util_start_engine (priv, error))
		return FALSE;

	priv->current_operation = FU_UTIL_OPERATION_UPDATE;
	g_signal_connect (priv->engine, "device-changed",
			  G_CALLBACK (fu_util_update_device_changed_cb), priv);

	devices = fu_engine_get_devices (priv->engine, error);
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (devices, i);
		FwupdRelease *rel;
		const gchar *remote_id;
		const gchar *device_id;
		const gchar *uri_tmp;
		g_autoptr(GPtrArray) rels = NULL;
		g_autoptr(GError) error_local = NULL;

		if (!fu_util_is_interesting_device (dev))
			continue;
		/* only show stuff that has metadata available */
		if (!fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_SUPPORTED))
			continue;

		device_id = fu_device_get_id (dev);
		rels = fu_engine_get_upgrades (priv->engine, device_id, &error_local);
		if (rels == NULL) {
			g_printerr ("%s\n", error_local->message);
			continue;
		}

		rel = g_ptr_array_index (rels, 0);
		uri_tmp = fwupd_release_get_uri (rel);
		remote_id = fwupd_release_get_remote_id (rel);
		if (remote_id != NULL) {
			FwupdRemote *remote;
			g_auto(GStrv) argv = NULL;

			remote = fu_engine_get_remote_by_id (priv->engine,
							     remote_id,
							     &error_local);
			if (remote == NULL) {
				g_printerr ("%s\n", error_local->message);
				continue;
			}

			argv = g_new0 (gchar *, 2);
			/* local remotes have the firmware already */
			if (fwupd_remote_get_kind (remote) == FWUPD_REMOTE_KIND_LOCAL) {
				const gchar *fn_cache = fwupd_remote_get_filename_cache (remote);
				g_autofree gchar *path = g_path_get_dirname (fn_cache);
				argv[0] = g_build_filename (path, uri_tmp, NULL);
			} else if (fwupd_remote_get_kind (remote) == FWUPD_REMOTE_KIND_DIRECTORY) {
				argv[0] = g_strdup (uri_tmp + 7);
			/* web remote, fu_util_install will download file */
			} else {
				argv[0] = fwupd_remote_build_firmware_uri (remote, uri_tmp, error);
			}
			if (!fu_util_install (priv, argv, &error_local)) {
				g_printerr ("%s\n", error_local->message);
				continue;
			}
			fu_util_display_current_message (priv);
		}
	}

	/* we don't want to ask anything */
	if (priv->no_reboot_check) {
		g_debug ("skipping reboot check");
		return TRUE;
	}

	return fu_util_prompt_complete (priv->completion_flags, TRUE, error);
}

static gboolean
fu_util_detach (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* load engine */
	if (!fu_util_start_engine (priv, error))
		return FALSE;

	/* invalid args */
	if (g_strv_length (values) == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments");
		return FALSE;
	}

	/* get device */
	if (g_strv_length (values) >= 1) {
		device = fu_engine_get_device (priv->engine, values[0], error);
		if (device == NULL)
			return FALSE;
	} else {
		device = fu_util_prompt_for_device (priv, error);
		if (device == NULL)
			return FALSE;
	}

	/* run vfunc */
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_detach (device, error);
}

static gboolean
fu_util_attach (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* load engine */
	if (!fu_util_start_engine (priv, error))
		return FALSE;

	/* invalid args */
	if (g_strv_length (values) == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments");
		return FALSE;
	}

	/* get device */
	if (g_strv_length (values) >= 1) {
		device = fu_engine_get_device (priv->engine, values[0], error);
		if (device == NULL)
			return FALSE;
	} else {
		device = fu_util_prompt_for_device (priv, error);
		if (device == NULL)
			return FALSE;
	}

	/* run vfunc */
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_attach (device, error);
}

static gboolean
fu_util_hwids (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FuSmbios) smbios = fu_smbios_new ();
	g_autoptr(FuHwids) hwids = fu_hwids_new ();
	const gchar *hwid_keys[] = {
		FU_HWIDS_KEY_BIOS_VENDOR,
		FU_HWIDS_KEY_BIOS_VERSION,
		FU_HWIDS_KEY_BIOS_MAJOR_RELEASE,
		FU_HWIDS_KEY_BIOS_MINOR_RELEASE,
		FU_HWIDS_KEY_MANUFACTURER,
		FU_HWIDS_KEY_FAMILY,
		FU_HWIDS_KEY_PRODUCT_NAME,
		FU_HWIDS_KEY_PRODUCT_SKU,
		FU_HWIDS_KEY_ENCLOSURE_KIND,
		FU_HWIDS_KEY_BASEBOARD_MANUFACTURER,
		FU_HWIDS_KEY_BASEBOARD_PRODUCT,
		NULL };

	/* read DMI data */
	if (g_strv_length (values) == 0) {
		if (!fu_smbios_setup (smbios, error))
			return FALSE;
	} else if (g_strv_length (values) == 1) {
		if (!fu_smbios_setup_from_file (smbios, values[0], error))
			return FALSE;
	} else {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments");
		return FALSE;
	}
	if (!fu_hwids_setup (hwids, smbios, error))
		return FALSE;

	/* show debug output */
	g_print ("Computer Information\n");
	g_print ("--------------------\n");
	for (guint i = 0; hwid_keys[i] != NULL; i++) {
		const gchar *tmp = fu_hwids_get_value (hwids, hwid_keys[i]);
		if (tmp == NULL)
			continue;
		if (g_strcmp0 (hwid_keys[i], FU_HWIDS_KEY_BIOS_MAJOR_RELEASE) == 0 ||
		    g_strcmp0 (hwid_keys[i], FU_HWIDS_KEY_BIOS_MINOR_RELEASE) == 0) {
			guint64 val = g_ascii_strtoull (tmp, NULL, 16);
			g_print ("%s: %" G_GUINT64_FORMAT "\n", hwid_keys[i], val);
		} else {
			g_print ("%s: %s\n", hwid_keys[i], tmp);
		}
	}

	/* show GUIDs */
	g_print ("\nHardware IDs\n");
	g_print ("------------\n");
	for (guint i = 0; i < 15; i++) {
		const gchar *keys = NULL;
		g_autofree gchar *guid = NULL;
		g_autofree gchar *key = NULL;
		g_autofree gchar *keys_str = NULL;
		g_auto(GStrv) keysv = NULL;
		g_autoptr(GError) error_local = NULL;

		/* get the GUID */
		key = g_strdup_printf ("HardwareID-%u", i);
		keys = fu_hwids_get_replace_keys (hwids, key);
		guid = fu_hwids_get_guid (hwids, key, &error_local);
		if (guid == NULL) {
			g_print ("%s\n", error_local->message);
			continue;
		}

		/* show what makes up the GUID */
		keysv = g_strsplit (keys, "&", -1);
		keys_str = g_strjoinv (" + ", keysv);
		g_print ("{%s}   <- %s\n", guid, keys_str);
	}

	return TRUE;
}

static gboolean
fu_util_firmware_builder (FuUtilPrivate *priv, gchar **values, GError **error)
{
	const gchar *script_fn = "startup.sh";
	const gchar *output_fn = "firmware.bin";
	g_autoptr(GBytes) archive_blob = NULL;
	g_autoptr(GBytes) firmware_blob = NULL;
	if (g_strv_length (values) < 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments");
		return FALSE;
	}
	archive_blob = fu_common_get_contents_bytes (values[0], error);
	if (archive_blob == NULL)
		return FALSE;
	if (g_strv_length (values) > 2)
		script_fn = values[2];
	if (g_strv_length (values) > 3)
		output_fn = values[3];
	firmware_blob = fu_common_firmware_builder (archive_blob, script_fn, output_fn, error);
	if (firmware_blob == NULL)
		return FALSE;
	return fu_common_set_contents_bytes (values[1], firmware_blob, error);
}

static void
fu_util_device_added_cb (FwupdClient *client,
			 FwupdDevice *device,
			 gpointer user_data)
{
	g_autofree gchar *tmp = fwupd_device_to_string (device);
	/* TRANSLATORS: this is when a device is hotplugged */
	g_print ("%s\n%s", _("Device added:"), tmp);
}

static void
fu_util_device_removed_cb (FwupdClient *client,
			   FwupdDevice *device,
			   gpointer user_data)
{
	g_autofree gchar *tmp = fwupd_device_to_string (device);
	/* TRANSLATORS: this is when a device is hotplugged */
	g_print ("%s\n%s", _("Device removed:"), tmp);
}

static void
fu_util_device_changed_cb (FwupdClient *client,
			   FwupdDevice *device,
			   gpointer user_data)
{
	g_autofree gchar *tmp = fwupd_device_to_string (device);
	/* TRANSLATORS: this is when a device has been updated */
	g_print ("%s\n%s", _("Device changed:"), tmp);
}

static void
fu_util_changed_cb (FwupdClient *client, gpointer user_data)
{
	/* TRANSLATORS: this is when the daemon state changes */
	g_print ("%s\n", _("Changed"));
}

static gboolean
fu_util_monitor (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FwupdClient) client = fwupd_client_new ();

	/* get all the devices */
	if (!fwupd_client_connect (client, priv->cancellable, error))
		return FALSE;

	/* watch for any hotplugged device */
	g_signal_connect (client, "changed",
			  G_CALLBACK (fu_util_changed_cb), priv);
	g_signal_connect (client, "device-added",
			  G_CALLBACK (fu_util_device_added_cb), priv);
	g_signal_connect (client, "device-removed",
			  G_CALLBACK (fu_util_device_removed_cb), priv);
	g_signal_connect (client, "device-changed",
			  G_CALLBACK (fu_util_device_changed_cb), priv);
	g_signal_connect (priv->cancellable, "cancelled",
			  G_CALLBACK (fu_util_cancelled_cb), priv);
	g_main_loop_run (priv->loop);
	return TRUE;
}

int
main (int argc, char *argv[])
{
	gboolean allow_older = FALSE;
	gboolean allow_reinstall = FALSE;
	gboolean force = FALSE;
	gboolean ret;
	gboolean version = FALSE;
	g_auto(GStrv) plugin_glob = NULL;
	g_autoptr(FuUtilPrivate) priv = g_new0 (FuUtilPrivate, 1);
	g_autoptr(GError) error = NULL;
	g_autofree gchar *cmd_descriptions = NULL;
	const GOptionEntry options[] = {
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &version,
			/* TRANSLATORS: command line option */
			_("Show client and daemon versions"), NULL },
		{ "allow-reinstall", '\0', 0, G_OPTION_ARG_NONE, &allow_reinstall,
			/* TRANSLATORS: command line option */
			_("Allow re-installing existing firmware versions"), NULL },
		{ "allow-older", '\0', 0, G_OPTION_ARG_NONE, &allow_older,
			/* TRANSLATORS: command line option */
			_("Allow downgrading firmware versions"), NULL },
		{ "force", '\0', 0, G_OPTION_ARG_NONE, &force,
			/* TRANSLATORS: command line option */
			_("Override plugin warning"), NULL },
		{ "no-reboot-check", '\0', 0, G_OPTION_ARG_NONE, &priv->no_reboot_check,
			/* TRANSLATORS: command line option */
			_("Do not check for reboot after update"), NULL },
		{ "show-all-devices", '\0', 0, G_OPTION_ARG_NONE, &priv->show_all_devices,
			/* TRANSLATORS: command line option */
			_("Show devices that are not updatable"), NULL },
		{ "plugin-whitelist", '\0', 0, G_OPTION_ARG_STRING_ARRAY, &plugin_glob,
			/* TRANSLATORS: command line option */
			_("Manually whitelist specific plugins"), NULL },
		{ "prepare", '\0', 0, G_OPTION_ARG_STRING_ARRAY, &priv->prepare_blob,
			/* TRANSLATORS: command line option */
			_("Run the plugin composite prepare routine when using install-blob"), NULL },
		{ "cleanup", '\0', 0, G_OPTION_ARG_STRING_ARRAY, &priv->cleanup_blob,
			/* TRANSLATORS: command line option */
			_("Run the plugin composite cleanup routine when using install-blob"), NULL },

		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* ensure root user */
	if (getuid () != 0 || geteuid () != 0)
		/* TRANSLATORS: we're poking around as a power user */
		g_printerr ("%s\n", _("This program may only work correctly as root"));

	/* create helper object */
	priv->loop = g_main_loop_new (NULL, FALSE);
	priv->progressbar = fu_progressbar_new ();

	/* add commands */
	priv->cmd_array = g_ptr_array_new_with_free_func ((GDestroyNotify) fu_util_item_free);
	fu_util_add (priv->cmd_array,
		     "build-firmware",
		     "FILE-IN FILE-OUT [SCRIPT] [OUTPUT]",
		     /* TRANSLATORS: command description */
		     _("Build firmware using a sandbox"),
		     fu_util_firmware_builder);
	fu_util_add (priv->cmd_array,
		     "smbios-dump",
		     "FILE",
		     /* TRANSLATORS: command description */
		     _("Dump SMBIOS data from a file"),
		     fu_util_smbios_dump);
	fu_util_add (priv->cmd_array,
		     "get-plugins",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Get all enabled plugins registered with the system"),
		     fu_util_get_plugins);
	fu_util_add (priv->cmd_array,
		     "get-details",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Gets details about a firmware file"),
		     fu_util_get_details);
	fu_util_add (priv->cmd_array,
		     "get-updates",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Gets the list of updates for connected hardware"),
		     fu_util_get_updates);
	fu_util_add (priv->cmd_array,
		     "get-devices",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Get all devices that support firmware updates"),
		     fu_util_get_devices);
	fu_util_add (priv->cmd_array,
		     "get-topology",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Get all devices according to the system topology"),
		     fu_util_get_topology);
	fu_util_add (priv->cmd_array,
		     "watch",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Watch for hardware changes"),
		     fu_util_watch);
	fu_util_add (priv->cmd_array,
		     "install-blob",
		     "FILENAME DEVICE-ID",
		     /* TRANSLATORS: command description */
		     _("Install a firmware blob on a device"),
		     fu_util_install_blob);
	fu_util_add (priv->cmd_array,
		     "install",
		     "FILE [ID]",
		     /* TRANSLATORS: command description */
		     _("Install a firmware file on this hardware"),
		     fu_util_install);
	fu_util_add (priv->cmd_array,
		     "attach",
		     "DEVICE-ID",
		     /* TRANSLATORS: command description */
		     _("Attach to firmware mode"),
		     fu_util_attach);
	fu_util_add (priv->cmd_array,
		     "detach",
		     "DEVICE-ID",
		     /* TRANSLATORS: command description */
		     _("Detach to bootloader mode"),
		     fu_util_detach);
	fu_util_add (priv->cmd_array,
		     "hwids",
		     "[FILE]",
		     /* TRANSLATORS: command description */
		     _("Return all the hardware IDs for the machine"),
		     fu_util_hwids);
	fu_util_add (priv->cmd_array,
		     "monitor",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Monitor the daemon for events"),
		     fu_util_monitor);
	fu_util_add (priv->cmd_array,
		     "update",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Update all devices that match local metadata"),
		     fu_util_update);

	/* do stuff on ctrl+c */
	priv->cancellable = g_cancellable_new ();
	g_unix_signal_add_full (G_PRIORITY_DEFAULT,
				SIGINT, fu_util_sigint_cb,
				priv, NULL);
	g_signal_connect (priv->cancellable, "cancelled",
			  G_CALLBACK (fu_util_cancelled_cb), priv);

	/* sort by command name */
	g_ptr_array_sort (priv->cmd_array,
			  (GCompareFunc) fu_sort_command_name_cb);

	/* non-TTY consoles cannot answer questions */
	if (isatty (fileno (stdout)) == 0) {
		priv->no_reboot_check = TRUE;
		fu_progressbar_set_interactive (priv->progressbar, FALSE);
	}

	/* get a list of the commands */
	priv->context = g_option_context_new (NULL);
	cmd_descriptions = fu_util_get_descriptions (priv->cmd_array);
	g_option_context_set_summary (priv->context, cmd_descriptions);
	g_option_context_set_description (priv->context,
		"This tool allows an administrator to use the fwupd plugins "
		"without being installed on the host system.");

	/* TRANSLATORS: program name */
	g_set_application_name (_("Firmware Utility"));
	g_option_context_add_main_entries (priv->context, options, NULL);
	g_option_context_add_group (priv->context, fu_debug_get_option_group ());
	ret = g_option_context_parse (priv->context, &argc, &argv, &error);
	if (!ret) {
		/* TRANSLATORS: the user didn't read the man page */
		g_print ("%s: %s\n", _("Failed to parse arguments"),
			 error->message);
		return EXIT_FAILURE;
	}

	/* set flags */
	priv->flags |= FWUPD_INSTALL_FLAG_NO_HISTORY;
	if (allow_reinstall)
		priv->flags |= FWUPD_INSTALL_FLAG_ALLOW_REINSTALL;
	if (allow_older)
		priv->flags |= FWUPD_INSTALL_FLAG_ALLOW_OLDER;
	if (force)
		priv->flags |= FWUPD_INSTALL_FLAG_FORCE;

	/* load engine */
	priv->engine = fu_engine_new (FU_APP_FLAGS_NO_IDLE_SOURCES);
	g_signal_connect (priv->engine, "device-added",
			  G_CALLBACK (fu_main_engine_device_added_cb),
			  priv);
	g_signal_connect (priv->engine, "device-removed",
			  G_CALLBACK (fu_main_engine_device_removed_cb),
			  priv);
	g_signal_connect (priv->engine, "status-changed",
			  G_CALLBACK (fu_main_engine_status_changed_cb),
			  priv);
	g_signal_connect (priv->engine, "percentage-changed",
			  G_CALLBACK (fu_main_engine_percentage_changed_cb),
			  priv);

	/* just show versions and exit */
	if (version) {
		g_autofree gchar *version_str = fu_util_get_versions ();
		g_print ("%s\n", version_str);
		return EXIT_SUCCESS;
	}

	/* any plugin whitelist specified */
	for (guint i = 0; plugin_glob != NULL && plugin_glob[i] != NULL; i++)
		fu_engine_add_plugin_filter (priv->engine, plugin_glob[i]);

	/* run the specified command */
	ret = fu_util_run (priv, argv[1], (gchar**) &argv[2], &error);
	if (!ret) {
		if (g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_ARGS)) {
			g_autofree gchar *tmp = NULL;
			tmp = g_option_context_get_help (priv->context, TRUE, NULL);
			g_print ("%s\n\n%s", error->message, tmp);
			return EXIT_FAILURE;
		}
		if (g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO)) {
			g_print ("%s\n", error->message);
			return EXIT_NOTHING_TO_DO;
		}
		g_print ("%s\n", error->message);
		return EXIT_FAILURE;
	}

	/* success */
	return EXIT_SUCCESS;
}
