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
#ifdef HAVE_GIO_UNIX
#include <glib-unix.h>
#endif
#include <fcntl.h>
#include <locale.h>
#include <stdlib.h>
#include <unistd.h>
#include <libsoup/soup.h>

#include "fu-device-private.h"
#include "fu-engine.h"
#include "fu-history.h"
#include "fu-plugin-private.h"
#include "fu-progressbar.h"
#include "fu-smbios-private.h"
#include "fu-util-common.h"
#include "fu-debug.h"
#include "fwupd-common-private.h"
#include "fwupd-device-private.h"

#ifdef HAVE_SYSTEMD
#include "fu-systemd.h"
#endif

/* custom return code */
#define EXIT_NOTHING_TO_DO		2

typedef enum {
	FU_UTIL_OPERATION_UNKNOWN,
	FU_UTIL_OPERATION_UPDATE,
	FU_UTIL_OPERATION_INSTALL,
	FU_UTIL_OPERATION_READ,
	FU_UTIL_OPERATION_LAST
} FuUtilOperation;

struct FuUtilPrivate {
	GCancellable		*cancellable;
	GMainLoop		*loop;
	GOptionContext		*context;
	FuEngine		*engine;
	FuProgressbar		*progressbar;
	gboolean		 no_reboot_check;
	gboolean		 no_safety_check;
	gboolean		 prepare_blob;
	gboolean		 cleanup_blob;
	gboolean		 enable_json_state;
	FwupdInstallFlags	 flags;
	gboolean		 show_all_devices;
	gboolean		 disable_ssl_strict;
	/* only valid in update and downgrade */
	FuUtilOperation		 current_operation;
	FwupdDevice		*current_device;
	gchar			*current_message;
	FwupdDeviceFlags	 completion_flags;
	FwupdDeviceFlags	 filter_include;
	FwupdDeviceFlags	 filter_exclude;
};

static gboolean
fu_util_save_current_state (FuUtilPrivate *priv, GError **error)
{
	g_autoptr(JsonBuilder) builder = NULL;
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonNode) json_root = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autofree gchar *state = NULL;
	g_autofree gchar *dirname = NULL;
	g_autofree gchar *filename = NULL;

	if (!priv->enable_json_state)
		return TRUE;

	devices = fu_engine_get_devices (priv->engine, error);
	if (devices == NULL)
		return FALSE;
	fwupd_device_array_ensure_parents (devices);

	/* create header */
	builder = json_builder_new ();
	json_builder_begin_object (builder);

	/* add each device */
	json_builder_set_member_name (builder, "Devices");
	json_builder_begin_array (builder);
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (devices, i);
		json_builder_begin_object (builder);
		fwupd_device_to_json (dev, builder);
		json_builder_end_object (builder);
	}
	json_builder_end_array (builder);
	json_builder_end_object (builder);

	/* export as a string */
	json_root = json_builder_get_root (builder);
	json_generator = json_generator_new ();
	json_generator_set_pretty (json_generator, TRUE);
	json_generator_set_root (json_generator, json_root);
	state = json_generator_to_data (json_generator, NULL);
	if (state == NULL)
		return FALSE;
	dirname = fu_common_get_path (FU_PATH_KIND_LOCALSTATEDIR_PKG);
	filename = g_build_filename (dirname, "state.json", NULL);
	return g_file_set_contents (filename, state, -1, error);
}

static gboolean
fu_util_start_engine (FuUtilPrivate *priv, FuEngineLoadFlags flags, GError **error)
{
	g_autoptr(GError) error_local = NULL;

#ifdef HAVE_SYSTEMD
	if (!fu_systemd_unit_stop (fu_util_get_systemd_unit (), &error_local))
		g_debug ("Failed to stop daemon: %s", error_local->message);
#endif
	if (!fu_engine_load (priv->engine, flags, error))
		return FALSE;
	if (fu_engine_get_tainted (priv->engine)) {
		g_printerr ("WARNING: This tool has loaded 3rd party code and "
			    "is no longer supported by the upstream developers!\n");
	}
	return TRUE;
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

#ifdef HAVE_GIO_UNIX
static gboolean
fu_util_sigint_cb (gpointer user_data)
{
	FuUtilPrivate *priv = (FuUtilPrivate *) user_data;
	g_debug ("Handling SIGINT");
	g_cancellable_cancel (priv->cancellable);
	return FALSE;
}
#endif

static void
fu_util_private_free (FuUtilPrivate *priv)
{
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
	if (!fu_util_start_engine (priv, FU_ENGINE_LOAD_FLAG_NONE, error))
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
fu_util_filter_device (FuUtilPrivate *priv, FwupdDevice *dev)
{
	if (priv->filter_include != FWUPD_DEVICE_FLAG_NONE) {
		if (!fwupd_device_has_flag (dev, priv->filter_include))
			return FALSE;
	}
	if (priv->filter_exclude != FWUPD_DEVICE_FLAG_NONE) {
		if (fwupd_device_has_flag (dev, priv->filter_exclude))
			return FALSE;
	}
	return TRUE;
}

static gchar *
fu_util_get_tree_title (FuUtilPrivate *priv)
{
	return g_strdup (fu_engine_get_host_product (priv->engine));
}

static gboolean
fu_util_get_updates (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GNode) root = g_node_new (NULL);
	g_autofree gchar *title = NULL;

	/* load engine */
	if (!fu_util_start_engine (priv, FU_ENGINE_LOAD_FLAG_NONE, error))
		return FALSE;
	title = fu_util_get_tree_title (priv);

	/* get devices from daemon */
	devices = fu_engine_get_devices (priv->engine, error);
	if (devices == NULL)
		return FALSE;
	fwupd_device_array_ensure_parents (devices);
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (devices, i);
		g_autofree gchar *upgrade_str = NULL;
		g_autoptr(GPtrArray) rels = NULL;
		g_autoptr(GError) error_local = NULL;
		GNode *child;

		/* not going to have results, so save a engine round-trip */
		if (!fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE))
			continue;
		if (!fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_SUPPORTED)) {
			/* TRANSLATORS: message letting the user know no device upgrade available due to missing on LVFS
			* %1 is the device name */
			g_autofree gchar *tmp = g_strdup_printf (_("• %s has no available firmware updates"),
								 fwupd_device_get_name (dev));
			g_printerr ("%s\n", tmp);
			continue;
		}
		if (!fu_util_filter_device (priv, dev))
			continue;

		/* get the releases for this device and filter for validity */
		rels = fu_engine_get_upgrades (priv->engine,
					       fwupd_device_get_id (dev),
					       &error_local);
		if (rels == NULL) {
			/* TRANSLATORS: message letting the user know no device upgrade available
			* %1 is the device name */
			g_autofree gchar *tmp = g_strdup_printf (_("• %s has the latest available firmware version"),
								 fwupd_device_get_name (dev));
			g_printerr ("%s\n", tmp);
			/* discard the actual reason from user, but leave for debugging */
			g_debug ("%s", error_local->message);
			continue;
		}
		child = g_node_append_data (root, dev);

		for (guint j = 0; j < rels->len; j++) {
			FwupdRelease *rel = g_ptr_array_index (rels, j);
			g_node_append_data (child, g_object_ref (rel));
		}
	}
	if (g_node_n_nodes (root, G_TRAVERSE_ALL) > 1)
		fu_util_print_tree (root, title);
	/* save the device state for other applications to see */
	if (!fu_util_save_current_state (priv, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_util_get_details (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GNode) root = g_node_new (NULL);
	g_autofree gchar *title = NULL;
	gint fd;

	/* load engine */
	if (!fu_util_start_engine (priv, FU_ENGINE_LOAD_FLAG_NONE, error))
		return FALSE;
	title = fu_util_get_tree_title (priv);

	/* check args */
	if (g_strv_length (values) != 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments");
		return FALSE;
	}

	/* implied, important for get-details on a device not in your system */
	priv->show_all_devices = TRUE;

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
		FwupdRelease *rel;
		GNode *child;
		if (!fu_util_filter_device (priv, dev))
			continue;
		child = g_node_append_data (root, dev);
		rel = fwupd_device_get_release_default (dev);
		if (rel != NULL)
			g_node_append_data (child, rel);

	}
	fu_util_print_tree (root, title);

	return TRUE;
}

static gboolean
fu_util_get_device_flags (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GString) str = g_string_new (NULL);

	for (FwupdDeviceFlags i = FWUPD_DEVICE_FLAG_INTERNAL; i < FWUPD_DEVICE_FLAG_UNKNOWN; i<<=1) {
		const gchar *tmp = fwupd_device_flag_to_string (i);
		if (tmp == NULL)
			break;
		if (i != FWUPD_DEVICE_FLAG_INTERNAL)
			g_string_append (str, " ");
		g_string_append (str, tmp);
		g_string_append (str, " ~");
		g_string_append (str, tmp);
	}
	g_print ("%s\n", str->str);

	return TRUE;
}

static void
fu_util_build_device_tree (FuUtilPrivate *priv, GNode *root, GPtrArray *devs, FuDevice *dev)
{
	for (guint i = 0; i < devs->len; i++) {
		FuDevice *dev_tmp = g_ptr_array_index (devs, i);
		if (!fu_util_filter_device (priv, FWUPD_DEVICE (dev_tmp)))
			continue;
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
fu_util_get_devices (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GNode) root = g_node_new (NULL);
	g_autofree gchar *title = NULL;
	g_autoptr(GPtrArray) devs = NULL;

	/* load engine */
	if (!fu_util_start_engine (priv, FU_ENGINE_LOAD_FLAG_NONE, error))
		return FALSE;
	title = fu_util_get_tree_title (priv);

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
	fwupd_device_array_ensure_parents (devs);
	fu_util_build_device_tree (priv, root, devs, NULL);
	fu_util_print_tree (root, title);

	/* save the device state for other applications to see */
	return fu_util_save_current_state (priv, error);
}

static FuDevice *
fu_util_prompt_for_device (FuUtilPrivate *priv, GError **error)
{
	FuDevice *dev;
	guint idx;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) devices_filtered = NULL;

	/* get devices from daemon */
	devices = fu_engine_get_devices (priv->engine, error);
	if (devices == NULL)
		return NULL;
	fwupd_device_array_ensure_parents (devices);

	/* filter results */
	devices_filtered = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (guint i = 0; i < devices->len; i++) {
		dev = g_ptr_array_index (devices, i);
		if (!fu_util_filter_device (priv, FWUPD_DEVICE (dev)))
			continue;
		g_ptr_array_add (devices_filtered, g_object_ref (dev));
	}

	/* nothing */
	if (devices_filtered->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO,
				     "No supported devices");
		return NULL;
	}

	/* exactly one */
	if (devices_filtered->len == 1) {
		dev = g_ptr_array_index (devices_filtered, 0);
		/* TRANSLATORS: Device has been chosen by the daemon for the user */
		g_print ("%s: %s\n", _("Selected device"), fu_device_get_name (dev));
		return g_object_ref (dev);
	}

	/* TRANSLATORS: get interactive prompt */
	g_print ("%s\n", _("Choose a device:"));
	/* TRANSLATORS: this is to abort the interactive prompt */
	g_print ("0.\t%s\n", _("Cancel"));
	for (guint i = 0; i < devices_filtered->len; i++) {
		dev = g_ptr_array_index (devices_filtered, i);
		g_print ("%u.\t%s (%s)\n",
			 i + 1,
			 fu_device_get_id (dev),
			 fu_device_get_name (dev));
	}
	idx = fu_util_prompt_for_number (devices_filtered->len);
	if (idx == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO,
				     "Request canceled");
		return NULL;
	}
	dev = g_ptr_array_index (devices_filtered, idx - 1);
	return g_object_ref (dev);
}

static void
fu_util_update_device_changed_cb (FwupdClient *client,
				  FwupdDevice *device,
				  FuUtilPrivate *priv)
{
	g_autofree gchar *str = NULL;

	/* allowed to set whenever the device has changed */
	if (fwupd_device_has_flag (device, FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN))
		priv->completion_flags |= FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN;
	if (fwupd_device_has_flag (device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT))
		priv->completion_flags |= FWUPD_DEVICE_FLAG_NEEDS_REBOOT;

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
	} else if (priv->current_operation == FU_UTIL_OPERATION_READ) {
		/* TRANSLATORS: %1 is a device name  */
		str = g_strdup_printf (_("Reading from %s…"),
				       fwupd_device_get_name (device));
		fu_progressbar_set_title (priv->progressbar, str);
	} else {
		g_warning ("no FuUtilOperation set");
	}
	g_set_object (&priv->current_device, device);

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
	if (!fu_util_start_engine (priv, FU_ENGINE_LOAD_FLAG_NONE, error))
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
	priv->flags = FWUPD_INSTALL_FLAG_NO_HISTORY;
	if (!fu_engine_install_blob (priv->engine, device, blob_fw, priv->flags, error))
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
			g_autoptr(GPtrArray) devices_new = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
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

static gboolean
fu_util_firmware_read (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(GBytes) blob_empty = g_bytes_new (NULL, 0);
	g_autoptr(GBytes) blob_fw = NULL;

	/* invalid args */
	if (g_strv_length (values) == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments");
		return FALSE;
	}

	/* file already exists */
	if ((priv->flags & FWUPD_INSTALL_FLAG_FORCE) == 0 &&
	    g_file_test (values[0], G_FILE_TEST_EXISTS)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Filename already exists");
		return FALSE;
	}

	/* write a zero length file to ensure the destination is writable to
	 * avoid failing at the end of a potentially lengthy operation */
	if (!fu_common_set_contents_bytes (values[0], blob_empty, error))
		return FALSE;

	/* load engine */
	if (!fu_util_start_engine (priv, FU_ENGINE_LOAD_FLAG_NONE, error))
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
	priv->current_operation = FU_UTIL_OPERATION_READ;
	g_signal_connect (priv->engine, "device-changed",
			  G_CALLBACK (fu_util_update_device_changed_cb), priv);

	/* dump firmware */
	blob_fw = fu_engine_firmware_read (priv->engine, device, priv->flags, error);
	if (blob_fw == NULL)
		return FALSE;
	return fu_common_set_contents_bytes (values[0], blob_fw, error);
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
	const gchar *argv[][5] = { { "wget", uri, "-O", fn, NULL },
				   { "curl", uri, "--output", fn, NULL },
				   { NULL } };
	for (guint i = 0; argv[i][0] != NULL; i++) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_common_find_program_in_path (argv[i][0], &error_local)) {
			g_debug ("%s", error_local->message);
			continue;
		}
		return fu_common_spawn_sync (argv[i], NULL, NULL, 0, NULL, error);
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
	if (g_file_test (perhapsfn, G_FILE_TEST_EXISTS))
		return g_strdup (perhapsfn);
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
	if (!fu_util_start_engine (priv, FU_ENGINE_LOAD_FLAG_NONE, error))
		return FALSE;

	/* handle both forms */
	if (g_strv_length (values) == 1) {
		devices_possible = fu_engine_get_devices (priv->engine, error);
		if (devices_possible == NULL)
			return FALSE;
		fwupd_device_array_ensure_parents (devices_possible);
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

	/* save the device state for other applications to see */
	if (!fu_util_save_current_state (priv, error))
		return FALSE;

	/* success */
	return fu_util_prompt_complete (priv->completion_flags, TRUE, error);
}

static gboolean
fu_util_install_release (FuUtilPrivate *priv, FwupdRelease *rel, GError **error)
{
	FwupdRemote *remote;
	const gchar *remote_id;
	const gchar *uri_tmp;
	g_auto(GStrv) argv = NULL;

	uri_tmp = fwupd_release_get_uri (rel);
	if (uri_tmp == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "release missing URI");
		return FALSE;
	}
	remote_id = fwupd_release_get_remote_id (rel);
	if (remote_id == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to find remote for %s",
			     uri_tmp);
		return FALSE;
	}

	remote = fu_engine_get_remote_by_id (priv->engine,
					     remote_id,
					     error);
	if (remote == NULL)
		return FALSE;

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
	return fu_util_install (priv, argv, error);
}

static gboolean
fu_util_update_all (FuUtilPrivate *priv, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;

	devices = fu_engine_get_devices (priv->engine, error);
	if (devices == NULL)
		return FALSE;
	fwupd_device_array_ensure_parents (devices);
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (devices, i);
		FwupdRelease *rel;
		const gchar *device_id;
		g_autoptr(GPtrArray) rels = NULL;
		g_autoptr(GError) error_local = NULL;

		if (!fu_util_is_interesting_device (dev))
			continue;
		/* only show stuff that has metadata available */
		if (!fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE))
			continue;
		if (!fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_SUPPORTED)) {
			/* TRANSLATORS: message letting the user know no device upgrade available due to missing on LVFS
			* %1 is the device name */
			g_autofree gchar *tmp = g_strdup_printf (_("• %s has no available firmware updates"),
								 fwupd_device_get_name (dev));
			g_printerr ("%s\n", tmp);
			continue;
		}
		if (!fu_util_filter_device (priv, dev))
			continue;

		device_id = fu_device_get_id (dev);
		rels = fu_engine_get_upgrades (priv->engine, device_id, &error_local);
		if (rels == NULL) {
			/* TRANSLATORS: message letting the user know no device upgrade available
			* %1 is the device name */
			g_autofree gchar *tmp = g_strdup_printf (_("• %s has the latest available firmware version"),
								 fwupd_device_get_name (dev));
			g_printerr ("%s\n", tmp);
			/* discard the actual reason from user, but leave for debugging */
			g_debug ("%s", error_local->message);
			continue;
		}

		if (!priv->no_safety_check) {
			if (!fu_util_prompt_warning (dev,
						     fu_util_get_tree_title (priv),
						     error))
				return FALSE;
		}

		rel = g_ptr_array_index (rels, 0);
		if (!fu_util_install_release (priv, rel, &error_local)) {
			g_printerr ("%s\n", error_local->message);
			continue;
		}
		fu_util_display_current_message (priv);
	}
	return TRUE;
}

static gboolean
fu_util_update_by_id (FuUtilPrivate *priv, const gchar *device_id, GError **error)
{
	FwupdRelease *rel;
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(GPtrArray) rels = NULL;

	/* do not allow a partial device-id */
	dev = fu_engine_get_device (priv->engine, device_id, error);
	if (dev == NULL)
		return FALSE;

	/* get the releases for this device and filter for validity */
	rels = fu_engine_get_upgrades (priv->engine, device_id, error);
	if (rels == NULL)
		return FALSE;
	rel = g_ptr_array_index (rels, 0);
	if (!fu_util_install_release (priv, rel, error))
		return FALSE;
	fu_util_display_current_message (priv);

	return TRUE;
}

static gboolean
fu_util_update (FuUtilPrivate *priv, gchar **values, GError **error)
{
	if (priv->flags & FWUPD_INSTALL_FLAG_ALLOW_OLDER) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "--allow-older is not supported for this command");
		return FALSE;
	}

	if (priv->flags & FWUPD_INSTALL_FLAG_ALLOW_REINSTALL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "--allow-reinstall is not supported for this command");
		return FALSE;
	}

	if (g_strv_length (values) > 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments");
		return FALSE;
	}

	if (!fu_util_start_engine (priv, FU_ENGINE_LOAD_FLAG_NONE, error))
		return FALSE;

	priv->current_operation = FU_UTIL_OPERATION_UPDATE;
	g_signal_connect (priv->engine, "device-changed",
			  G_CALLBACK (fu_util_update_device_changed_cb), priv);

	if (g_strv_length (values) == 1) {
		if (!fu_util_update_by_id (priv, values[0], error))
			return FALSE;
	} else {
		if (!fu_util_update_all (priv, error))
			return FALSE;
	}

	/* we don't want to ask anything */
	if (priv->no_reboot_check) {
		g_debug ("skipping reboot check");
		return TRUE;
	}

	/* save the device state for other applications to see */
	if (!fu_util_save_current_state (priv, error))
		return FALSE;

	return fu_util_prompt_complete (priv->completion_flags, TRUE, error);
}

static gboolean
fu_util_detach (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* load engine */
	if (!fu_util_start_engine (priv, FU_ENGINE_LOAD_FLAG_NONE, error))
		return FALSE;

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
	if (!fu_util_start_engine (priv, FU_ENGINE_LOAD_FLAG_NONE, error))
		return FALSE;

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
fu_util_activate (FuUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean has_pending = FALSE;
	g_autoptr(FuHistory) history = fu_history_new ();
	g_autoptr(GPtrArray) devices = NULL;

	/* check the history database before starting the daemon */
	if (g_strv_length (values) == 0) {
		devices = fu_history_get_devices (history, error);
		if (devices == NULL)
			return FALSE;
	} else if (g_strv_length (values) == 1) {
		FuDevice *device;
		device = fu_history_get_device_by_id (history, values[0], error);
		if (device == NULL)
			return FALSE;
		devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
		g_ptr_array_add (devices, device);
	} else {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments");
		return FALSE;
	}

	/* nothing to do */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index (devices, i);
		if (fu_device_has_flag (dev, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION)) {
			fu_engine_add_plugin_filter (priv->engine,
						     fu_device_get_plugin (dev));
			has_pending = TRUE;
		}
	}

	if (!has_pending) {
    		g_printerr ("No firmware to activate\n");
    		return TRUE;
	}

	/* load engine */
	if (!fu_util_start_engine (priv, FU_ENGINE_LOAD_FLAG_READONLY_FS, error))
		return FALSE;

	/* activate anything with _NEEDS_ACTIVATION */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index (devices, i);
		if (!fu_util_filter_device (priv, FWUPD_DEVICE (device)))
			continue;
		if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION))
			continue;
		/* TRANSLATORS: shown when shutting down to switch to the new version */
		g_print ("%s %s…\n", _("Activating firmware update"), fu_device_get_name (device));
		if (!fu_engine_activate (priv->engine, fu_device_get_id (device), error))
			return FALSE;
	}

	return TRUE;
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

static gboolean
fu_util_self_sign (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autofree gchar *sig = NULL;

	/* check args */
	if (g_strv_length (values) != 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments: value expected");
		return FALSE;
	}

	/* start engine */
	if (!fu_util_start_engine (priv, FU_ENGINE_LOAD_FLAG_NONE, error))
		return FALSE;
	sig = fu_engine_self_sign (priv->engine, values[0],
				   FU_KEYRING_SIGN_FLAG_ADD_TIMESTAMP |
				   FU_KEYRING_SIGN_FLAG_ADD_CERT, error);
	if (sig == NULL)
		return FALSE;
	g_print ("%s\n", sig);
	return TRUE;
}

static void
fu_util_device_added_cb (FwupdClient *client,
			 FwupdDevice *device,
			 gpointer user_data)
{
	g_autofree gchar *tmp = fu_util_device_to_string (device, 0);
	/* TRANSLATORS: this is when a device is hotplugged */
	g_print ("%s\n%s", _("Device added:"), tmp);
}

static void
fu_util_device_removed_cb (FwupdClient *client,
			   FwupdDevice *device,
			   gpointer user_data)
{
	g_autofree gchar *tmp = fu_util_device_to_string (device, 0);
	/* TRANSLATORS: this is when a device is hotplugged */
	g_print ("%s\n%s", _("Device removed:"), tmp);
}

static void
fu_util_device_changed_cb (FwupdClient *client,
			   FwupdDevice *device,
			   gpointer user_data)
{
	g_autofree gchar *tmp = fu_util_device_to_string (device, 0);
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

static gboolean
fu_util_get_firmware_types (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) firmware_types = NULL;

	/* load engine */
	if (!fu_engine_load (priv->engine, FU_ENGINE_LOAD_FLAG_NO_ENUMERATE, error))
		return FALSE;

	firmware_types = fu_engine_get_firmware_gtype_ids (priv->engine);
	for (guint i = 0; i < firmware_types->len; i++) {
		const gchar *id = g_ptr_array_index (firmware_types, i);
		g_print ("%s\n", id);
	}
	if (firmware_types->len == 0) {
		/* TRANSLATORS: nothing found */
		g_print ("%s\n", _("No firmware IDs found"));
		return TRUE;
	}

	return TRUE;
}

static gchar *
fu_util_prompt_for_firmware_type (FuUtilPrivate *priv, GError **error)
{
	g_autoptr(GPtrArray) firmware_types = NULL;
	guint idx;
	firmware_types = fu_engine_get_firmware_gtype_ids (priv->engine);

	/* TRANSLATORS: get interactive prompt */
	g_print ("%s\n", _("Choose a firmware type:"));
	/* TRANSLATORS: this is to abort the interactive prompt */
	g_print ("0.\t%s\n", _("Cancel"));
	for (guint i = 0; i < firmware_types->len; i++) {
		const gchar *id = g_ptr_array_index (firmware_types, i);
		g_print ("%u.\t%s\n", i + 1, id);
	}
	idx = fu_util_prompt_for_number (firmware_types->len);
	if (idx == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO,
				     "Request canceled");
		return NULL;
	}

	return g_strdup (g_ptr_array_index (firmware_types, idx - 1));
}

static gboolean
fu_util_firmware_parse (FuUtilPrivate *priv, gchar **values, GError **error)
{
	GType gtype;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autofree gchar *firmware_type = NULL;
	g_autofree gchar *str = NULL;

	/* check args */
	if (g_strv_length (values) == 0 || g_strv_length (values) > 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments: filename required");
		return FALSE;
	}

	if (g_strv_length (values) == 2)
		firmware_type = g_strdup (values[1]);

	/* load file */
	blob = fu_common_get_contents_bytes (values[0], error);
	if (blob == NULL)
		return FALSE;

	/* load engine */
	if (!fu_engine_load (priv->engine, FU_ENGINE_LOAD_FLAG_NO_ENUMERATE, error))
		return FALSE;

	/* find the GType to use */
	if (firmware_type == NULL)
		firmware_type = fu_util_prompt_for_firmware_type (priv, error);
	if (firmware_type == NULL)
		return FALSE;
	gtype = fu_engine_get_firmware_gtype_by_id (priv->engine, firmware_type);
	if (gtype == G_TYPE_INVALID) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_FOUND,
			     "GType %s not supported", firmware_type);
		return FALSE;
	}
	firmware = g_object_new (gtype, NULL);
	if (!fu_firmware_parse (firmware, blob, priv->flags, error))
		return FALSE;
	str = fu_firmware_to_string (firmware);
	g_print ("%s", str);
	return TRUE;
}

static gboolean
fu_util_verify_update (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autofree gchar *str = NULL;
	g_autoptr(FuDevice) dev = NULL;

	/* load engine */
	if (!fu_util_start_engine (priv, FU_ENGINE_LOAD_FLAG_NONE, error))
		return FALSE;

	/* get device */
	if (g_strv_length (values) == 1) {
		dev = fu_engine_get_device (priv->engine, values[1], error);
		if (dev == NULL)
			return FALSE;
	} else {
		dev = fu_util_prompt_for_device (priv, error);
		if (dev == NULL)
			return FALSE;
	}

	/* add checksums */
	if (!fu_engine_verify_update (priv->engine, fu_device_get_id (dev), error))
		return FALSE;

	/* show checksums */
	str = fu_device_to_string (dev);
	g_print ("%s\n", str);
	return TRUE;
}

static gboolean
fu_util_get_history (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GNode) root = g_node_new (NULL);
	g_autofree gchar *title = NULL;

	/* load engine */
	if (!fu_util_start_engine (priv, FU_ENGINE_LOAD_FLAG_NONE, error))
		return FALSE;
	title = fu_util_get_tree_title (priv);

	/* get all devices from the history database */
	devices = fu_engine_get_history (priv->engine, error);
	if (devices == NULL)
		return FALSE;

	/* show each device */
	for (guint i = 0; i < devices->len; i++) {
		g_autoptr(GPtrArray) rels = NULL;
		FwupdDevice *dev = g_ptr_array_index (devices, i);
		FwupdRelease *rel;
		const gchar *remote;
		GNode *child;

		if (!fu_util_filter_device (priv, dev))
			continue;
		child = g_node_append_data (root, dev);

		rel = fwupd_device_get_release_default (dev);
		if (rel == NULL)
			continue;
		remote = fwupd_release_get_remote_id (rel);

		/* doesn't actually map to remote */
		if (remote == NULL) {
			g_node_append_data (child, rel);
			continue;
		}

		/* try to lookup releases from client */
		rels = fu_engine_get_releases (priv->engine, fwupd_device_get_id (dev), error);
		if (rels == NULL)
			return FALSE;

		/* map to a release in client */
		for (guint j = 0; j < rels->len; j++) {
			FwupdRelease *rel2 = g_ptr_array_index (rels, j);
			if (g_strcmp0 (remote,
				       fwupd_release_get_remote_id (rel2)) != 0)
				continue;
			if (g_strcmp0 (fwupd_release_get_version (rel),
				       fwupd_release_get_version (rel2)) != 0)
				continue;
			g_node_append_data (child, g_object_ref (rel2));
			rel = NULL;
			break;
		}

		/* didn't match anything */
		if (rels->len == 0 || rel != NULL) {
			g_node_append_data (child, rel);
			continue;
		}

	}
	fu_util_print_tree (root, title);

	return TRUE;
}

static gboolean
fu_util_refresh_remote (FuUtilPrivate *priv, FwupdRemote *remote, GError **error)
{
	g_autofree gchar *fn_raw = NULL;
	g_autofree gchar *fn_sig = NULL;
	g_autoptr(GBytes) bytes_raw = NULL;
	g_autoptr(GBytes) bytes_sig = NULL;

	/* payload */
	fn_raw = fu_util_get_user_cache_path (fwupd_remote_get_metadata_uri (remote));
	if (!fu_common_mkdir_parent (fn_raw, error))
		return FALSE;
	if (!fu_util_download_out_of_process (fwupd_remote_get_metadata_uri (remote),
					      fn_raw, error))
		return FALSE;
	bytes_raw = fu_common_get_contents_bytes (fn_raw, error);
	if (bytes_raw == NULL)
		return FALSE;

	/* signature */
	fn_sig = fu_util_get_user_cache_path (fwupd_remote_get_metadata_uri_sig (remote));
	if (!fu_util_download_out_of_process (fwupd_remote_get_metadata_uri_sig (remote),
					      fn_sig, error))
		return FALSE;
	bytes_sig = fu_common_get_contents_bytes (fn_sig, error);
	if (bytes_sig == NULL)
		return FALSE;

	/* send to daemon */
	g_debug ("updating %s", fwupd_remote_get_id (remote));
	return fu_engine_update_metadata_bytes (priv->engine,
						fwupd_remote_get_id (remote),
						bytes_raw,
						bytes_sig,
						error);

}

static gboolean
fu_util_refresh (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) remotes = NULL;

	/* load engine */
	if (!fu_util_start_engine (priv, FU_ENGINE_LOAD_FLAG_NONE, error))
		return FALSE;

	/* download new metadata */
	remotes = fu_engine_get_remotes (priv->engine, error);
	if (remotes == NULL)
		return FALSE;
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index (remotes, i);
		if (!fwupd_remote_get_enabled (remote))
			continue;
		if (fwupd_remote_get_kind (remote) != FWUPD_REMOTE_KIND_DOWNLOAD)
			continue;
		if (!fu_util_refresh_remote (priv, remote, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_util_get_remotes (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GNode) root = g_node_new (NULL);
	g_autoptr(GPtrArray) remotes = NULL;
	g_autofree gchar *title = NULL;

	/* load engine */
	if (!fu_util_start_engine (priv, FU_ENGINE_LOAD_FLAG_NONE, error))
		return FALSE;
	title = fu_util_get_tree_title (priv);

	/* list remotes */
	remotes = fu_engine_get_remotes (priv->engine, error);
	if (remotes == NULL)
		return FALSE;
	if (remotes->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO,
				     "no remotes available");
		return FALSE;
	}
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote_tmp = g_ptr_array_index (remotes, i);
		g_node_append_data (root, remote_tmp);
	}
	fu_util_print_tree (root, title);

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
	gboolean interactive = isatty (fileno (stdout)) != 0;
	g_auto(GStrv) plugin_glob = NULL;
	g_autoptr(FuUtilPrivate) priv = g_new0 (FuUtilPrivate, 1);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) cmd_array = fu_util_cmd_array_new ();
	g_autofree gchar *cmd_descriptions = NULL;
	g_autofree gchar *filter = NULL;
	const GOptionEntry options[] = {
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &version,
			/* TRANSLATORS: command line option */
			_("Show client and daemon versions"), NULL },
		{ "allow-reinstall", '\0', 0, G_OPTION_ARG_NONE, &allow_reinstall,
			/* TRANSLATORS: command line option */
			_("Allow reinstalling existing firmware versions"), NULL },
		{ "allow-older", '\0', 0, G_OPTION_ARG_NONE, &allow_older,
			/* TRANSLATORS: command line option */
			_("Allow downgrading firmware versions"), NULL },
		{ "force", '\0', 0, G_OPTION_ARG_NONE, &force,
			/* TRANSLATORS: command line option */
			_("Override plugin warning"), NULL },
		{ "no-reboot-check", '\0', 0, G_OPTION_ARG_NONE, &priv->no_reboot_check,
			/* TRANSLATORS: command line option */
			_("Do not check for reboot after update"), NULL },
		{ "no-safety-check", '\0', 0, G_OPTION_ARG_NONE, &priv->no_safety_check,
			/* TRANSLATORS: command line option */
			_("Do not perform device safety checks"), NULL },
		{ "show-all-devices", '\0', 0, G_OPTION_ARG_NONE, &priv->show_all_devices,
			/* TRANSLATORS: command line option */
			_("Show devices that are not updatable"), NULL },
		{ "plugin-whitelist", '\0', 0, G_OPTION_ARG_STRING_ARRAY, &plugin_glob,
			/* TRANSLATORS: command line option */
			_("Manually whitelist specific plugins"), NULL },
		{ "prepare", '\0', 0, G_OPTION_ARG_NONE, &priv->prepare_blob,
			/* TRANSLATORS: command line option */
			_("Run the plugin composite prepare routine when using install-blob"), NULL },
		{ "cleanup", '\0', 0, G_OPTION_ARG_NONE, &priv->cleanup_blob,
			/* TRANSLATORS: command line option */
			_("Run the plugin composite cleanup routine when using install-blob"), NULL },
		{ "enable-json-state", '\0', 0, G_OPTION_ARG_NONE, &priv->enable_json_state,
			/* TRANSLATORS: command line option */
			_("Save device state into a JSON file between executions"), NULL },
		{ "disable-ssl-strict", '\0', 0, G_OPTION_ARG_NONE, &priv->disable_ssl_strict,
			/* TRANSLATORS: command line option */
			_("Ignore SSL strict checks when downloading files"), NULL },
		{ "filter", '\0', 0, G_OPTION_ARG_STRING, &filter,
			/* TRANSLATORS: command line option */
			_("Filter with a set of device flags using a ~ prefix to "
			  "exclude, e.g. 'internal,~needs-reboot'"), NULL },
		{ NULL}
	};

#ifdef _WIN32
	/* workaround Windows setting the codepage to 1252 */
	g_setenv ("LANG", "C.UTF-8", FALSE);
#endif

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, FWUPD_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

#ifdef HAVE_GETUID
	/* ensure root user */
	if (interactive && (getuid () != 0 || geteuid () != 0))
		/* TRANSLATORS: we're poking around as a power user */
		g_printerr ("%s\n", _("This program may only work correctly as root"));
#endif

	/* create helper object */
	priv->loop = g_main_loop_new (NULL, FALSE);
	priv->progressbar = fu_progressbar_new ();

	/* add commands */
	fu_util_cmd_array_add (cmd_array,
		     "build-firmware",
		     "FILE-IN FILE-OUT [SCRIPT] [OUTPUT]",
		     /* TRANSLATORS: command description */
		     _("Build firmware using a sandbox"),
		     fu_util_firmware_builder);
	fu_util_cmd_array_add (cmd_array,
		     "smbios-dump",
		     "FILE",
		     /* TRANSLATORS: command description */
		     _("Dump SMBIOS data from a file"),
		     fu_util_smbios_dump);
	fu_util_cmd_array_add (cmd_array,
		     "get-plugins",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Get all enabled plugins registered with the system"),
		     fu_util_get_plugins);
	fu_util_cmd_array_add (cmd_array,
		     "get-details",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Gets details about a firmware file"),
		     fu_util_get_details);
	fu_util_cmd_array_add (cmd_array,
		     "get-history",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Show history of firmware updates"),
		     fu_util_get_history);
	fu_util_cmd_array_add (cmd_array,
		     "get-updates,get-upgrades",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Gets the list of updates for connected hardware"),
		     fu_util_get_updates);
	fu_util_cmd_array_add (cmd_array,
		     "get-devices,get-topology",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Get all devices that support firmware updates"),
		     fu_util_get_devices);
	fu_util_cmd_array_add (cmd_array,
		     "get-device-flags",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Get all device flags supported by fwupd"),
		     fu_util_get_device_flags);
	fu_util_cmd_array_add (cmd_array,
		     "watch",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Watch for hardware changes"),
		     fu_util_watch);
	fu_util_cmd_array_add (cmd_array,
		     "install-blob",
		     "FILENAME DEVICE-ID",
		     /* TRANSLATORS: command description */
		     _("Install a firmware blob on a device"),
		     fu_util_install_blob);
	fu_util_cmd_array_add (cmd_array,
		     "install",
		     "FILE [DEVICE-ID]",
		     /* TRANSLATORS: command description */
		     _("Install a firmware file on this hardware"),
		     fu_util_install);
	fu_util_cmd_array_add (cmd_array,
		     "attach",
		     "DEVICE-ID",
		     /* TRANSLATORS: command description */
		     _("Attach to firmware mode"),
		     fu_util_attach);
	fu_util_cmd_array_add (cmd_array,
		     "detach",
		     "DEVICE-ID",
		     /* TRANSLATORS: command description */
		     _("Detach to bootloader mode"),
		     fu_util_detach);
	fu_util_cmd_array_add (cmd_array,
		     "activate",
		     "[DEVICE-ID]",
		     /* TRANSLATORS: command description */
		     _("Activate pending devices"),
		     fu_util_activate);
	fu_util_cmd_array_add (cmd_array,
		     "hwids",
		     "[FILE]",
		     /* TRANSLATORS: command description */
		     _("Return all the hardware IDs for the machine"),
		     fu_util_hwids);
	fu_util_cmd_array_add (cmd_array,
		     "monitor",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Monitor the daemon for events"),
		     fu_util_monitor);
	fu_util_cmd_array_add (cmd_array,
		     "update,upgrade",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Update all devices that match local metadata"),
		     fu_util_update);
	fu_util_cmd_array_add (cmd_array,
		     "self-sign",
		     "TEXT",
		     /* TRANSLATORS: command description */
		     C_("command-description",
			"Sign data using the client certificate"),
		     fu_util_self_sign);
	fu_util_cmd_array_add (cmd_array,
		     "verify-update",
		     "[DEVICE-ID]",
		     /* TRANSLATORS: command description */
		     _("Update the stored metadata with current contents"),
		     fu_util_verify_update);
	fu_util_cmd_array_add (cmd_array,
		     "firmware-read",
		     "FILENAME [DEVICE-ID]",
		     /* TRANSLATORS: command description */
		     _("Read a firmware blob from a device"),
		     fu_util_firmware_read);
	fu_util_cmd_array_add (cmd_array,
		     "firmware-parse",
		     "FILENAME [FIRMWARE-TYPE]",
		     /* TRANSLATORS: command description */
		     _("Parse and show details about a firmware file"),
		     fu_util_firmware_parse);
	fu_util_cmd_array_add (cmd_array,
		     "get-firmware-types",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("List the available firmware types"),
		     fu_util_get_firmware_types);
	fu_util_cmd_array_add (cmd_array,
		     "get-remotes",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Gets the configured remotes"),
		     fu_util_get_remotes);
	fu_util_cmd_array_add (cmd_array,
		     "refresh",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Refresh metadata from remote server"),
		     fu_util_refresh);

	/* do stuff on ctrl+c */
	priv->cancellable = g_cancellable_new ();
#ifdef HAVE_GIO_UNIX
	g_unix_signal_add_full (G_PRIORITY_DEFAULT,
				SIGINT, fu_util_sigint_cb,
				priv, NULL);
#endif
	g_signal_connect (priv->cancellable, "cancelled",
			  G_CALLBACK (fu_util_cancelled_cb), priv);

	/* sort by command name */
	fu_util_cmd_array_sort (cmd_array);

	/* non-TTY consoles cannot answer questions */
	if (!interactive) {
		priv->no_reboot_check = TRUE;
		priv->no_safety_check = TRUE;
		fu_progressbar_set_interactive (priv->progressbar, FALSE);
	}

	/* get a list of the commands */
	priv->context = g_option_context_new (NULL);
	cmd_descriptions = fu_util_cmd_array_to_string (cmd_array);
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

	/* allow disabling SSL strict mode for broken corporate proxies */
	if (priv->disable_ssl_strict) {
		/* TRANSLATORS: try to help */
		g_printerr ("%s\n", _("WARNING: Ignoring SSL strict checks, "
				      "to do this automatically in the future "
				      "export DISABLE_SSL_STRICT in your environment"));
		g_setenv ("DISABLE_SSL_STRICT", "1", TRUE);
	}

	/* parse filter flags */
	if (filter != NULL) {
		if (!fu_util_parse_filter_flags (filter,
						 &priv->filter_include,
						 &priv->filter_exclude,
						 &error)) {
			/* TRANSLATORS: the user didn't read the man page */
			g_print ("%s: %s\n", _("Failed to parse flags for --filter"),
				 error->message);
			return EXIT_FAILURE;
		}
	}


	/* set flags */
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
	ret = fu_util_cmd_array_run (cmd_array, priv, argv[1], (gchar**) &argv[2], &error);
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
