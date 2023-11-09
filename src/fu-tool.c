/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuMain"

#include "config.h"

#include <fwupdplugin.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#ifdef HAVE_GIO_UNIX
#include <glib-unix.h>
#endif
#include <fcntl.h>
#include <jcat.h>
#include <locale.h>
#include <stdlib.h>
#include <unistd.h>

#include "fwupd-bios-setting-private.h"
#include "fwupd-client-private.h"
#include "fwupd-common-private.h"
#include "fwupd-device-private.h"
#include "fwupd-enums-private.h"
#include "fwupd-plugin-private.h"

#include "fu-bios-settings-private.h"
#include "fu-cabinet.h"
#include "fu-console.h"
#include "fu-context-private.h"
#include "fu-debug.h"
#include "fu-device-private.h"
#include "fu-engine.h"
#include "fu-history.h"
#include "fu-plugin-private.h"
#include "fu-security-attr-common.h"
#include "fu-security-attrs-private.h"
#include "fu-smbios-private.h"
#include "fu-util-bios-setting.h"
#include "fu-util-common.h"

#ifdef HAVE_SYSTEMD
#include "fu-systemd.h"
#endif

/* custom return codes */
#define EXIT_NOTHING_TO_DO 2
#define EXIT_NOT_FOUND	   3

typedef enum {
	FU_UTIL_OPERATION_UNKNOWN,
	FU_UTIL_OPERATION_UPDATE,
	FU_UTIL_OPERATION_INSTALL,
	FU_UTIL_OPERATION_READ,
	FU_UTIL_OPERATION_LAST
} FuUtilOperation;

struct FuUtilPrivate {
	GCancellable *cancellable;
	GMainContext *main_ctx;
	GMainLoop *loop;
	GOptionContext *context;
	FuEngine *engine;
	FuEngineRequest *request;
	FuProgress *progress;
	FuConsole *console;
	FwupdClient *client;
	gboolean as_json;
	gboolean no_reboot_check;
	gboolean no_safety_check;
	gboolean no_device_prompt;
	gboolean prepare_blob;
	gboolean cleanup_blob;
	gboolean enable_json_state;
	gboolean interactive;
	FwupdInstallFlags flags;
	gboolean show_all;
	gboolean disable_ssl_strict;
	gint lock_fd;
	/* only valid in update and downgrade */
	FuUtilOperation current_operation;
	FwupdDevice *current_device;
	GPtrArray *post_requests;
	FwupdDeviceFlags completion_flags;
	FwupdDeviceFlags filter_device_include;
	FwupdDeviceFlags filter_device_exclude;
	FwupdReleaseFlags filter_release_include;
	FwupdReleaseFlags filter_release_exclude;
};

static void
fu_util_client_notify_cb(GObject *object, GParamSpec *pspec, FuUtilPrivate *priv)
{
	if (priv->as_json)
		return;
	fu_console_set_progress(priv->console,
				fwupd_client_get_status(priv->client),
				fwupd_client_get_percentage(priv->client));
}

static void
fu_util_show_plugin_warnings(FuUtilPrivate *priv)
{
	FwupdPluginFlags flags = FWUPD_PLUGIN_FLAG_NONE;
	GPtrArray *plugins;

	/* get a superset so we do not show the same message more than once */
	plugins = fu_engine_get_plugins(priv->engine);
	for (guint i = 0; i < plugins->len; i++) {
		FwupdPlugin *plugin = g_ptr_array_index(plugins, i);
		if (fwupd_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED))
			continue;
		if (!fwupd_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_USER_WARNING))
			continue;
		flags |= fwupd_plugin_get_flags(plugin);
	}

	/* never show these, they're way too generic */
	flags &= ~FWUPD_PLUGIN_FLAG_DISABLED;
	flags &= ~FWUPD_PLUGIN_FLAG_NO_HARDWARE;
	flags &= ~FWUPD_PLUGIN_FLAG_REQUIRE_HWID;
	flags &= ~FWUPD_PLUGIN_FLAG_MEASURE_SYSTEM_INTEGRITY;

	/* print */
	for (guint i = 0; i < 64; i++) {
		FwupdPluginFlags flag = (guint64)1 << i;
		const gchar *tmp;
		g_autofree gchar *url = NULL;
		if ((flags & flag) == 0)
			continue;
		tmp = fu_util_plugin_flag_to_string((guint64)1 << i);
		if (tmp == NULL)
			continue;
		fu_console_print_full(priv->console, FU_CONSOLE_PRINT_FLAG_WARNING, "%s\n", tmp);
		url = g_strdup_printf("https://github.com/fwupd/fwupd/wiki/PluginFlag:%s",
				      fwupd_plugin_flag_to_string(flag));
		/* TRANSLATORS: %s is a link to a website */
		fu_console_print(priv->console, _("See %s for more information."), url);
	}
}

static gboolean
fu_util_lock(FuUtilPrivate *priv, GError **error)
{
#ifdef HAVE_WRLCK
	struct flock lockp = {
	    .l_type = F_WRLCK,
	    .l_whence = SEEK_SET,
	};
	g_autofree gchar *lockfn = NULL;
	gboolean use_user = FALSE;

#ifdef HAVE_GETUID
	if (getuid() != 0 || geteuid() != 0)
		use_user = TRUE;
#endif

	/* open file */
	if (use_user) {
		lockfn = fu_util_get_user_cache_path("fwupdtool");
	} else {
		g_autofree gchar *lockdir = fu_path_from_kind(FU_PATH_KIND_LOCKDIR);
		lockfn = g_build_filename(lockdir, "fwupdtool", NULL);
	}
	if (!fu_path_mkdir_parent(lockfn, error))
		return FALSE;
	priv->lock_fd = g_open(lockfn, O_RDWR | O_CREAT, S_IRWXU);
	if (priv->lock_fd < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to open %s",
			    lockfn);
		return FALSE;
	}

	/* write lock */
#ifdef HAVE_OFD
	if (fcntl(priv->lock_fd, F_OFD_SETLK, &lockp) < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "another instance has locked %s",
			    lockfn);
		return FALSE;
	}
#else
	if (fcntl(priv->lock_fd, F_SETLK, &lockp) < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "another instance has locked %s",
			    lockfn);
		return FALSE;
	}
#endif

	/* success */
	g_debug("locked %s", lockfn);
#endif
	return TRUE;
}

static gboolean
fu_util_start_engine(FuUtilPrivate *priv,
		     FuEngineLoadFlags flags,
		     FuProgress *progress,
		     GError **error)
{
	if (!fu_util_lock(priv, error)) {
		/* TRANSLATORS: another fwupdtool instance is already running */
		g_prefix_error(error, "%s: ", _("Failed to lock"));
		return FALSE;
	}
#ifdef HAVE_SYSTEMD
	if (getuid() != 0 || geteuid() != 0) {
		g_info("not attempting to stop daemon when running as user");
	} else {
		g_autoptr(GError) error_local = NULL;
		if (!fu_systemd_unit_stop(fu_util_get_systemd_unit(), &error_local))
			g_info("failed to stop daemon: %s", error_local->message);
	}
#endif
	flags |= FU_ENGINE_LOAD_FLAG_NO_IDLE_SOURCES;
	flags |= FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS;
	flags |= FU_ENGINE_LOAD_FLAG_HWINFO;
	if (!fu_engine_load(priv->engine, flags, progress, error))
		return FALSE;
	fu_util_show_plugin_warnings(priv);
	fu_util_show_unsupported_warning(priv->console);

	/* copy properties from engine to client */
	g_object_set(priv->client,
		     "host-vendor",
		     fu_engine_get_host_vendor(priv->engine),
		     "host-product",
		     fu_engine_get_host_product(priv->engine),
		     "battery-level",
		     fu_context_get_battery_level(fu_engine_get_context(priv->engine)),
		     "battery-threshold",
		     fu_context_get_battery_threshold(fu_engine_get_context(priv->engine)),
		     NULL);

	/* success */
	return TRUE;
}

static void
fu_util_maybe_prefix_sandbox_error(const gchar *value, GError **error)
{
	g_autofree gchar *path = g_path_get_dirname(value);
	if (!g_file_test(path, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR)) {
		g_prefix_error(error,
			       "Unable to access %s. You may need to copy %s to %s: ",
			       path,
			       value,
			       g_getenv("HOME"));
	}
}

static void
fu_util_cancelled_cb(GCancellable *cancellable, gpointer user_data)
{
	FuUtilPrivate *priv = (FuUtilPrivate *)user_data;
	/* TRANSLATORS: this is when a device ctrl+c's a watch */
	fu_console_print_literal(priv->console, _("Cancelled"));
	g_main_loop_quit(priv->loop);
}

static gboolean
fu_util_smbios_dump(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autofree gchar *tmp = NULL;
	g_autoptr(FuSmbios) smbios = NULL;
	if (g_strv_length(values) < 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}
	smbios = fu_smbios_new();
	if (!fu_smbios_setup_from_file(smbios, values[0], error))
		return FALSE;
	tmp = fu_firmware_to_string(FU_FIRMWARE(smbios));
	fu_console_print_literal(priv->console, tmp);
	return TRUE;
}

#ifdef HAVE_GIO_UNIX
static gboolean
fu_util_sigint_cb(gpointer user_data)
{
	FuUtilPrivate *priv = (FuUtilPrivate *)user_data;
	g_info("handling SIGINT");
	g_cancellable_cancel(priv->cancellable);
	return FALSE;
}
#endif

static void
fu_util_setup_signal_handlers(FuUtilPrivate *priv)
{
#ifdef HAVE_GIO_UNIX
	g_autoptr(GSource) source = g_unix_signal_source_new(SIGINT);
	g_source_set_callback(source, fu_util_sigint_cb, priv, NULL);
	g_source_attach(g_steal_pointer(&source), priv->main_ctx);
#endif
}

static void
fu_util_private_free(FuUtilPrivate *priv)
{
	if (priv->current_device != NULL)
		g_object_unref(priv->current_device);
	if (priv->engine != NULL)
		g_object_unref(priv->engine);
	if (priv->request != NULL)
		g_object_unref(priv->request);
	if (priv->client != NULL)
		g_object_unref(priv->client);
	if (priv->main_ctx != NULL)
		g_main_context_unref(priv->main_ctx);
	if (priv->loop != NULL)
		g_main_loop_unref(priv->loop);
	if (priv->cancellable != NULL)
		g_object_unref(priv->cancellable);
	if (priv->console != NULL)
		g_object_unref(priv->console);
	if (priv->progress != NULL)
		g_object_unref(priv->progress);
	if (priv->context != NULL)
		g_option_context_free(priv->context);
	if (priv->lock_fd >= 0)
		g_close(priv->lock_fd, NULL);
	g_ptr_array_unref(priv->post_requests);
	g_free(priv);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuUtilPrivate, fu_util_private_free)
#pragma clang diagnostic pop

static void
fu_util_update_device_request_cb(FwupdClient *client, FwupdRequest *request, FuUtilPrivate *priv)
{
	/* action has not been assigned yet */
	if (priv->current_operation == FU_UTIL_OPERATION_UNKNOWN)
		return;

	/* nothing sensible to show */
	if (fwupd_request_get_message(request) == NULL)
		return;

	/* show this now */
	if (fwupd_request_get_kind(request) == FWUPD_REQUEST_KIND_IMMEDIATE) {
		g_autofree gchar *fmt = NULL;
		g_autofree gchar *tmp = NULL;

		/* TRANSLATORS: the user needs to do something, e.g. remove the device */
		fmt = fu_console_color_format(_("Action Required:"), FU_CONSOLE_COLOR_RED);
		tmp = g_strdup_printf("%s %s", fmt, fwupd_request_get_message(request));
		fu_console_set_progress_title(priv->console, tmp);
		fu_console_beep(priv->console, 5);
	}

	/* save for later */
	if (fwupd_request_get_kind(request) == FWUPD_REQUEST_KIND_POST)
		g_ptr_array_add(priv->post_requests, g_object_ref(request));
}

static void
fu_main_engine_device_added_cb(FuEngine *engine, FuDevice *device, FuUtilPrivate *priv)
{
	g_autofree gchar *tmp = fu_device_to_string(device);
	g_debug("ADDED:\n%s", tmp);
}

static void
fu_main_engine_device_removed_cb(FuEngine *engine, FuDevice *device, FuUtilPrivate *priv)
{
	g_autofree gchar *tmp = fu_device_to_string(device);
	g_debug("REMOVED:\n%s", tmp);
}

static void
fu_main_engine_status_changed_cb(FuEngine *engine, FwupdStatus status, FuUtilPrivate *priv)
{
	if (priv->as_json)
		return;
	fu_console_set_progress(priv->console, status, 0);
}

static void
fu_util_progress_percentage_changed_cb(FuProgress *progress, guint percentage, FuUtilPrivate *priv)
{
	if (priv->as_json)
		return;
	fu_console_set_progress(priv->console, fu_progress_get_status(progress), percentage);
}

static void
fu_util_progress_status_changed_cb(FuProgress *progress, FwupdStatus status, FuUtilPrivate *priv)
{
	if (priv->as_json)
		return;
	fu_console_set_progress(priv->console, status, fu_progress_get_percentage(progress));
}

static gboolean
fu_util_watch(FuUtilPrivate *priv, gchar **values, GError **error)
{
	if (!fu_util_start_engine(priv, FU_ENGINE_LOAD_FLAG_COLDPLUG, priv->progress, error))
		return FALSE;
	g_main_loop_run(priv->loop);
	return TRUE;
}

static gint
fu_util_plugin_name_sort_cb(FuPlugin **item1, FuPlugin **item2)
{
	return fu_plugin_name_compare(*item1, *item2);
}

static gboolean
fu_util_get_plugins_as_json(FuUtilPrivate *priv, GPtrArray *plugins, GError **error)
{
	g_autoptr(JsonBuilder) builder = json_builder_new();
	json_builder_begin_object(builder);

	json_builder_set_member_name(builder, "Plugins");
	json_builder_begin_array(builder);
	for (guint i = 0; i < plugins->len; i++) {
		FwupdPlugin *plugin = g_ptr_array_index(plugins, i);
		json_builder_begin_object(builder);
		fwupd_plugin_to_json(plugin, builder);
		json_builder_end_object(builder);
	}
	json_builder_end_array(builder);
	json_builder_end_object(builder);
	return fu_util_print_builder(priv->console, builder, error);
}

static gboolean
fu_util_get_plugins(FuUtilPrivate *priv, gchar **values, GError **error)
{
	GPtrArray *plugins;

	/* load engine */
	if (!fu_util_start_engine(priv, FU_ENGINE_LOAD_FLAG_READONLY, priv->progress, error))
		return FALSE;

	/* print */
	plugins = fu_engine_get_plugins(priv->engine);
	g_ptr_array_sort(plugins, (GCompareFunc)fu_util_plugin_name_sort_cb);
	if (priv->as_json)
		return fu_util_get_plugins_as_json(priv, plugins, error);

	/* print */
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		g_autofree gchar *str = fu_util_plugin_to_string(FWUPD_PLUGIN(plugin), 0);
		fu_console_print_literal(priv->console, str);
	}
	if (plugins->len == 0) {
		/* TRANSLATORS: nothing found */
		fu_console_print_literal(priv->console, _("No plugins found"));
	}

	return TRUE;
}

static FuDevice *
fu_util_prompt_for_device(FuUtilPrivate *priv, GPtrArray *devices_opt, GError **error)
{
	FuDevice *dev;
	guint idx;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) devices_filtered = NULL;

	/* get devices from daemon */
	if (devices_opt != NULL) {
		devices = g_ptr_array_ref(devices_opt);
	} else {
		devices = fu_engine_get_devices(priv->engine, error);
		if (devices == NULL)
			return NULL;
	}
	fwupd_device_array_ensure_parents(devices);

	/* filter results */
	devices_filtered = fwupd_device_array_filter_flags(devices,
							   priv->filter_device_include,
							   priv->filter_device_exclude,
							   error);
	if (devices_filtered == NULL)
		return NULL;

	/* exactly one */
	if (devices_filtered->len == 1) {
		dev = g_ptr_array_index(devices_filtered, 0);
		if (!priv->as_json) {
			fu_console_print(
			    priv->console,
			    "%s: %s",
			    /* TRANSLATORS: device has been chosen by the daemon for the user */
			    _("Selected device"),
			    fu_device_get_name(dev));
		}
		return g_object_ref(dev);
	}

	/* no questions */
	if (priv->no_device_prompt) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "can't prompt for devices");
		return NULL;
	}

	/* TRANSLATORS: this is to abort the interactive prompt */
	fu_console_print(priv->console, "0.\t%s", _("Cancel"));
	for (guint i = 0; i < devices_filtered->len; i++) {
		dev = g_ptr_array_index(devices_filtered, i);
		fu_console_print(priv->console,
				 "%u.\t%s (%s)",
				 i + 1,
				 fu_device_get_id(dev),
				 fu_device_get_name(dev));
	}

	/* TRANSLATORS: get interactive prompt */
	idx = fu_console_input_uint(priv->console, devices_filtered->len, "%s", _("Choose device"));
	if (idx == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "Request canceled");
		return NULL;
	}
	dev = g_ptr_array_index(devices_filtered, idx - 1);
	return g_object_ref(dev);
}

static FuDevice *
fu_util_get_device(FuUtilPrivate *priv, const gchar *id, GError **error)
{
	if (fwupd_guid_is_valid(id)) {
		g_autoptr(GPtrArray) devices = NULL;
		devices = fu_engine_get_devices_by_guid(priv->engine, id, error);
		if (devices == NULL)
			return NULL;
		return fu_util_prompt_for_device(priv, devices, error);
	}

	/* did this look like a GUID? */
	for (guint i = 0; id[i] != '\0'; i++) {
		if (id[i] == '-') {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_ARGS,
					    "Invalid arguments");
			return NULL;
		}
	}
	return fu_engine_get_device(priv->engine, id, error);
}

static gboolean
fu_util_get_updates(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GNode) root = g_node_new(NULL);
	g_autoptr(GPtrArray) devices_no_support = g_ptr_array_new();
	g_autoptr(GPtrArray) devices_no_upgrades = g_ptr_array_new();

	/* load engine */
	if (!fu_util_start_engine(priv,
				  FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_REMOTES,
				  priv->progress,
				  error))
		return FALSE;

	/* parse arguments */
	if (g_strv_length(values) == 0) {
		devices = fu_engine_get_devices(priv->engine, error);
		if (devices == NULL)
			return FALSE;
	} else if (g_strv_length(values) == 1) {
		FuDevice *device;
		device = fu_util_get_device(priv, values[0], error);
		if (device == NULL)
			return FALSE;
		devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
		g_ptr_array_add(devices, device);
	} else {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}

	fwupd_device_array_ensure_parents(devices);
	g_ptr_array_sort(devices, fu_util_sort_devices_by_flags_cb);
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devices, i);
		g_autoptr(GPtrArray) rels = NULL;
		g_autoptr(GError) error_local = NULL;
		GNode *child;

		/* not going to have results, so save a engine round-trip */
		if (!fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE) &&
		    !fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN))
			continue;
		if (!fwupd_device_match_flags(dev,
					      priv->filter_device_include,
					      priv->filter_device_exclude))
			continue;
		if (!fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_SUPPORTED)) {
			g_ptr_array_add(devices_no_support, dev);
			continue;
		}

		/* get the releases for this device and filter for validity */
		rels = fu_engine_get_upgrades(priv->engine,
					      priv->request,
					      fwupd_device_get_id(dev),
					      &error_local);
		if (rels == NULL) {
			g_ptr_array_add(devices_no_upgrades, dev);
			/* discard the actual reason from user, but leave for debugging */
			g_debug("%s", error_local->message);
			continue;
		}
		child = g_node_append_data(root, dev);

		for (guint j = 0; j < rels->len; j++) {
			FwupdRelease *rel = g_ptr_array_index(rels, j);
			if (!fwupd_release_match_flags(rel,
						       priv->filter_release_include,
						       priv->filter_release_exclude))
				continue;
			g_node_append_data(child, g_object_ref(rel));
		}
	}

	/* devices that have no updates available for whatever reason */
	if (devices_no_support->len > 0) {
		fu_console_print_literal(priv->console,
					 /* TRANSLATORS: message letting the user know no device
					  * upgrade available due to missing on LVFS */
					 _("Devices with no available firmware updates: "));
		for (guint i = 0; i < devices_no_support->len; i++) {
			FwupdDevice *dev = g_ptr_array_index(devices_no_support, i);
			fu_console_print(priv->console, " • %s", fwupd_device_get_name(dev));
		}
	}
	if (devices_no_upgrades->len > 0) {
		fu_console_print_literal(
		    priv->console,
		    /* TRANSLATORS: message letting the user know no device upgrade available */
		    _("Devices with the latest available firmware version:"));
		for (guint i = 0; i < devices_no_upgrades->len; i++) {
			FwupdDevice *dev = g_ptr_array_index(devices_no_upgrades, i);
			fu_console_print(priv->console, " • %s", fwupd_device_get_name(dev));
		}
	}

	/* updates */
	if (g_node_n_nodes(root, G_TRAVERSE_ALL) <= 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    _("No updates available for remaining devices"));
		return FALSE;
	}

	fu_util_print_tree(priv->console, priv->client, root);
	return TRUE;
}

static gboolean
fu_util_get_details(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GNode) root = g_node_new(NULL);
	g_autoptr(GBytes) blob = NULL;

	/* load engine */
	if (!fu_util_start_engine(priv,
				  FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_REMOTES,
				  priv->progress,
				  error))
		return FALSE;

	/* check args */
	if (g_strv_length(values) != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}

	/* implied, important for get-details on a device not in your system */
	priv->show_all = TRUE;

	/* open file */
	blob = fu_bytes_get_contents(values[0], error);
	if (blob == NULL) {
		fu_util_maybe_prefix_sandbox_error(values[0], error);
		return FALSE;
	}
	array = fu_engine_get_details_for_bytes(priv->engine, priv->request, blob, error);
	if (array == NULL)
		return FALSE;
	for (guint i = 0; i < array->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(array, i);
		FwupdRelease *rel;
		GNode *child;
		if (!fwupd_device_match_flags(dev,
					      priv->filter_device_include,
					      priv->filter_device_exclude))
			continue;
		child = g_node_append_data(root, dev);
		rel = fwupd_device_get_release_default(dev);
		if (rel != NULL)
			g_node_append_data(child, rel);
	}
	fu_util_print_tree(priv->console, priv->client, root);

	return TRUE;
}

static gboolean
fu_util_get_device_flags(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GString) str = g_string_new(NULL);

	for (FwupdDeviceFlags i = FWUPD_DEVICE_FLAG_INTERNAL; i < FWUPD_DEVICE_FLAG_UNKNOWN;
	     i <<= 1) {
		const gchar *tmp = fwupd_device_flag_to_string(i);
		if (tmp == NULL)
			break;
		if (i != FWUPD_DEVICE_FLAG_INTERNAL)
			g_string_append(str, " ");
		g_string_append(str, tmp);
		g_string_append(str, " ~");
		g_string_append(str, tmp);
	}
	fu_console_print_literal(priv->console, str->str);

	return TRUE;
}

static void
fu_util_build_device_tree(FuUtilPrivate *priv, GNode *root, GPtrArray *devs, FuDevice *dev)
{
	for (guint i = 0; i < devs->len; i++) {
		FuDevice *dev_tmp = g_ptr_array_index(devs, i);
		if (!fwupd_device_match_flags(FWUPD_DEVICE(dev_tmp),
					      priv->filter_device_include,
					      priv->filter_device_exclude))
			continue;
		if (!priv->show_all && !fu_util_is_interesting_device(FWUPD_DEVICE(dev_tmp)))
			continue;
		if (fu_device_get_parent(dev_tmp) == dev) {
			GNode *child = g_node_append_data(root, dev_tmp);
			fu_util_build_device_tree(priv, child, devs, dev_tmp);
		}
	}
}

static gboolean
fu_util_get_devices_as_json(FuUtilPrivate *priv, GPtrArray *devs, GError **error)
{
	g_autoptr(JsonBuilder) builder = json_builder_new();
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "Devices");
	json_builder_begin_array(builder);
	for (guint i = 0; i < devs->len; i++) {
		FuDevice *dev = g_ptr_array_index(devs, i);
		g_autoptr(GPtrArray) rels = NULL;
		g_autoptr(GError) error_local = NULL;

		/* add all releases that could be applied */
		rels = fu_engine_get_releases_for_device(priv->engine,
							 priv->request,
							 dev,
							 &error_local);
		if (rels == NULL) {
			g_debug("not adding releases to device: %s", error_local->message);
		} else {
			for (guint j = 0; j < rels->len; j++) {
				FwupdRelease *rel = g_ptr_array_index(rels, j);
				if (!fwupd_release_match_flags(rel,
							       priv->filter_release_include,
							       priv->filter_release_exclude))
					continue;
				fu_device_add_release(dev, rel);
			}
		}

		/* add to builder */
		json_builder_begin_object(builder);
		fwupd_device_to_json_full(FWUPD_DEVICE(dev), builder, FWUPD_DEVICE_FLAG_TRUSTED);
		json_builder_end_object(builder);
	}
	json_builder_end_array(builder);
	json_builder_end_object(builder);
	return fu_util_print_builder(priv->console, builder, error);
}

static gboolean
fu_util_get_devices(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GNode) root = g_node_new(NULL);
	g_autoptr(GPtrArray) devs = NULL;

	/* load engine */
	if (!fu_util_start_engine(priv,
				  FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_REMOTES,
				  priv->progress,
				  error))
		return FALSE;

	/* get devices and build tree */
	if (g_strv_length(values) > 0) {
		devs = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
		for (guint i = 0; values[i] != NULL; i++) {
			FuDevice *device = fu_util_get_device(priv, values[i], error);
			if (device == NULL)
				return FALSE;
			g_ptr_array_add(devs, device);
		}
	} else {
		devs = fu_engine_get_devices(priv->engine, error);
		if (devs == NULL)
			return FALSE;
	}

	/* not for human consumption */
	if (priv->as_json)
		return fu_util_get_devices_as_json(priv, devs, error);

	if (devs->len > 0) {
		fwupd_device_array_ensure_parents(devs);
		fu_util_build_device_tree(priv, root, devs, NULL);
	}

	/* print */
	if (g_node_n_children(root) == 0) {
		fu_console_print_literal(priv->console,
					 /* TRANSLATORS: nothing attached that can be upgraded */
					 _("No hardware detected with firmware update capability"));
		return TRUE;
	}
	fu_util_print_tree(priv->console, priv->client, root);

	return TRUE;
}

static void
fu_util_update_device_changed_cb(FwupdClient *client, FwupdDevice *device, FuUtilPrivate *priv)
{
	g_autofree gchar *str = NULL;

	/* allowed to set whenever the device has changed */
	if (fwupd_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN))
		priv->completion_flags |= FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN;
	if (fwupd_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT))
		priv->completion_flags |= FWUPD_DEVICE_FLAG_NEEDS_REBOOT;

	/* same as last time, so ignore */
	if (priv->current_device == NULL ||
	    g_strcmp0(fwupd_device_get_composite_id(priv->current_device),
		      fwupd_device_get_composite_id(device)) == 0) {
		g_set_object(&priv->current_device, device);
		return;
	}

	/* ignore indirect devices that might have changed */
	if (fwupd_device_get_status(device) == FWUPD_STATUS_IDLE ||
	    fwupd_device_get_status(device) == FWUPD_STATUS_UNKNOWN) {
		g_debug("ignoring %s with status %s",
			fwupd_device_get_name(device),
			fwupd_status_to_string(fwupd_device_get_status(device)));
		return;
	}

	/* show message in console */
	if (priv->current_operation == FU_UTIL_OPERATION_UPDATE) {
		/* TRANSLATORS: %1 is a device name */
		str = g_strdup_printf(_("Updating %s…"), fwupd_device_get_name(device));
		fu_console_set_progress_title(priv->console, str);
	} else if (priv->current_operation == FU_UTIL_OPERATION_INSTALL) {
		/* TRANSLATORS: %1 is a device name  */
		str = g_strdup_printf(_("Installing on %s…"), fwupd_device_get_name(device));
		fu_console_set_progress_title(priv->console, str);
	} else if (priv->current_operation == FU_UTIL_OPERATION_READ) {
		/* TRANSLATORS: %1 is a device name  */
		str = g_strdup_printf(_("Reading from %s…"), fwupd_device_get_name(device));
		fu_console_set_progress_title(priv->console, str);
	} else {
		g_warning("no FuUtilOperation set");
	}
	g_set_object(&priv->current_device, device);
}

static void
fu_util_display_current_message(FuUtilPrivate *priv)
{
	/* print all POST requests */
	for (guint i = 0; i < priv->post_requests->len; i++) {
		FwupdRequest *request = g_ptr_array_index(priv->post_requests, i);
		fu_console_print_literal(priv->console, fu_util_request_get_message(request));
	}
}

static gboolean
fu_util_install_blob(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(GBytes) blob_fw = NULL;

	/* progress */
	fu_progress_set_id(priv->progress, G_STRLOC);
	fu_progress_add_flag(priv->progress, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_add_step(priv->progress, FWUPD_STATUS_LOADING, 2, "parse");
	fu_progress_add_step(priv->progress, FWUPD_STATUS_LOADING, 30, "start-engine");
	fu_progress_add_step(priv->progress, FWUPD_STATUS_DEVICE_WRITE, 68, NULL);

	/* invalid args */
	if (g_strv_length(values) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}

	/* parse blob */
	blob_fw = fu_bytes_get_contents(values[0], error);
	if (blob_fw == NULL) {
		fu_util_maybe_prefix_sandbox_error(values[0], error);
		return FALSE;
	}
	fu_progress_step_done(priv->progress);

	/* load engine */
	if (!fu_util_start_engine(priv,
				  FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_REMOTES,
				  fu_progress_get_child(priv->progress),
				  error))
		return FALSE;
	fu_progress_step_done(priv->progress);

	/* get device */
	priv->filter_device_include |= FWUPD_DEVICE_FLAG_UPDATABLE;
	if (g_strv_length(values) >= 2) {
		device = fu_util_get_device(priv, values[1], error);
		if (device == NULL)
			return FALSE;
	} else {
		device = fu_util_prompt_for_device(priv, NULL, error);
		if (device == NULL)
			return FALSE;
	}

	priv->current_operation = FU_UTIL_OPERATION_INSTALL;
	g_signal_connect(FU_ENGINE(priv->engine),
			 "device-changed",
			 G_CALLBACK(fu_util_update_device_changed_cb),
			 priv);

	/* write bare firmware */
	if (priv->prepare_blob) {
		g_autoptr(GPtrArray) devices = NULL;
		devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
		g_ptr_array_add(devices, g_object_ref(device));
		if (!fu_engine_composite_prepare(priv->engine, devices, error)) {
			g_prefix_error(error, "failed to prepare composite action: ");
			return FALSE;
		}
	}
	priv->flags |= FWUPD_INSTALL_FLAG_NO_HISTORY;
	if (!fu_engine_install_blob(priv->engine,
				    device,
				    blob_fw,
				    fu_progress_get_child(priv->progress),
				    priv->flags,
				    fu_engine_request_get_feature_flags(priv->request),
				    error))
		return FALSE;
	fu_progress_step_done(priv->progress);

	/* cleanup */
	if (priv->cleanup_blob) {
		g_autoptr(FuDevice) device_new = NULL;
		g_autoptr(GError) error_local = NULL;

		/* get the possibly new device from the old ID */
		device_new = fu_util_get_device(priv, fu_device_get_id(device), &error_local);
		if (device_new == NULL) {
			g_debug("failed to find new device: %s", error_local->message);
		} else {
			g_autoptr(GPtrArray) devices_new =
			    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
			g_ptr_array_add(devices_new, g_steal_pointer(&device_new));
			if (!fu_engine_composite_cleanup(priv->engine, devices_new, error)) {
				g_prefix_error(error, "failed to cleanup composite action: ");
				return FALSE;
			}
		}
	}

	fu_util_display_current_message(priv);

	/* success */
	return fu_util_prompt_complete(priv->console, priv->completion_flags, TRUE, error);
}

static gboolean
fu_util_firmware_sign(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(GBytes) archive_blob_new = NULL;
	g_autoptr(GBytes) archive_blob_old = NULL;
	g_autoptr(GBytes) cert = NULL;
	g_autoptr(GBytes) privkey = NULL;

	/* invalid args */
	if (g_strv_length(values) != 3) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments, expected firmware.cab "
				    "certificate.pem privatekey.pfx");
		return FALSE;
	}

	/* load arguments */
	archive_blob_old = fu_bytes_get_contents(values[0], error);
	if (archive_blob_old == NULL)
		return FALSE;
	cert = fu_bytes_get_contents(values[1], error);
	if (cert == NULL)
		return FALSE;
	privkey = fu_bytes_get_contents(values[2], error);
	if (privkey == NULL)
		return FALSE;

	/* load, sign, export */
	if (!fu_firmware_parse(FU_FIRMWARE(cabinet),
			       archive_blob_old,
			       FWUPD_INSTALL_FLAG_NONE,
			       error))
		return FALSE;
	if (!fu_cabinet_sign(cabinet, cert, privkey, FU_CABINET_SIGN_FLAG_NONE, error))
		return FALSE;
	archive_blob_new = fu_firmware_write(FU_FIRMWARE(cabinet), error);
	if (archive_blob_new == NULL)
		return FALSE;
	return fu_bytes_set_contents(values[0], archive_blob_new, error);
}

static gboolean
fu_util_firmware_dump(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(GBytes) blob_empty = g_bytes_new(NULL, 0);
	g_autoptr(GBytes) blob_fw = NULL;

	/* progress */
	fu_progress_set_id(priv->progress, G_STRLOC);
	fu_progress_add_flag(priv->progress, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_add_step(priv->progress, FWUPD_STATUS_LOADING, 5, "start-engine");
	fu_progress_add_step(priv->progress, FWUPD_STATUS_DEVICE_READ, 95, NULL);

	/* invalid args */
	if (g_strv_length(values) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}

	/* file already exists */
	if ((priv->flags & FWUPD_INSTALL_FLAG_FORCE) == 0 &&
	    g_file_test(values[0], G_FILE_TEST_EXISTS)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Filename already exists");
		return FALSE;
	}

	/* write a zero length file to ensure the destination is writable to
	 * avoid failing at the end of a potentially lengthy operation */
	if (!fu_bytes_set_contents(values[0], blob_empty, error))
		return FALSE;

	/* load engine */
	if (!fu_util_start_engine(priv,
				  FU_ENGINE_LOAD_FLAG_COLDPLUG,
				  fu_progress_get_child(priv->progress),
				  error))
		return FALSE;
	fu_progress_step_done(priv->progress);

	/* get device */
	priv->filter_device_include |= FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE;
	if (g_strv_length(values) >= 2) {
		device = fu_util_get_device(priv, values[1], error);
		if (device == NULL)
			return FALSE;
	} else {
		device = fu_util_prompt_for_device(priv, NULL, error);
		if (device == NULL)
			return FALSE;
	}
	priv->current_operation = FU_UTIL_OPERATION_READ;
	g_signal_connect(FU_ENGINE(priv->engine),
			 "device-changed",
			 G_CALLBACK(fu_util_update_device_changed_cb),
			 priv);

	/* dump firmware */
	blob_fw = fu_engine_firmware_dump(priv->engine,
					  device,
					  fu_progress_get_child(priv->progress),
					  priv->flags,
					  error);
	if (blob_fw == NULL)
		return FALSE;
	fu_progress_step_done(priv->progress);
	return fu_bytes_set_contents(values[0], blob_fw, error);
}

static gboolean
fu_util_firmware_read(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuFirmware) fw = NULL;
	g_autoptr(GBytes) blob_empty = g_bytes_new(NULL, 0);
	g_autoptr(GBytes) blob_fw = NULL;

	/* progress */
	fu_progress_set_id(priv->progress, G_STRLOC);
	fu_progress_add_flag(priv->progress, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_add_step(priv->progress, FWUPD_STATUS_LOADING, 5, "start-engine");
	fu_progress_add_step(priv->progress, FWUPD_STATUS_DEVICE_READ, 95, NULL);

	/* invalid args */
	if (g_strv_length(values) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}

	/* file already exists */
	if ((priv->flags & FWUPD_INSTALL_FLAG_FORCE) == 0 &&
	    g_file_test(values[0], G_FILE_TEST_EXISTS)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Filename already exists");
		return FALSE;
	}

	/* write a zero length file to ensure the destination is writable to
	 * avoid failing at the end of a potentially lengthy operation */
	if (!fu_bytes_set_contents(values[0], blob_empty, error))
		return FALSE;

	/* load engine */
	if (!fu_util_start_engine(priv,
				  FU_ENGINE_LOAD_FLAG_COLDPLUG,
				  fu_progress_get_child(priv->progress),
				  error))
		return FALSE;
	fu_progress_step_done(priv->progress);

	/* get device */
	priv->filter_device_include |= FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE;
	if (g_strv_length(values) >= 2) {
		device = fu_util_get_device(priv, values[1], error);
		if (device == NULL)
			return FALSE;
	} else {
		device = fu_util_prompt_for_device(priv, NULL, error);
		if (device == NULL)
			return FALSE;
	}
	priv->current_operation = FU_UTIL_OPERATION_READ;
	g_signal_connect(FU_ENGINE(priv->engine),
			 "device-changed",
			 G_CALLBACK(fu_util_update_device_changed_cb),
			 priv);

	/* read firmware into the container format */
	fw = fu_engine_firmware_read(priv->engine,
				     device,
				     fu_progress_get_child(priv->progress),
				     priv->flags,
				     error);
	if (fw == NULL)
		return FALSE;
	blob_fw = fu_firmware_write(fw, error);
	if (blob_fw == NULL)
		return FALSE;
	fu_progress_step_done(priv->progress);
	return fu_bytes_set_contents(values[0], blob_fw, error);
}

static gint
fu_util_release_sort_cb(gconstpointer a, gconstpointer b)
{
	FuRelease *release1 = *((FuRelease **)a);
	FuRelease *release2 = *((FuRelease **)b);
	return fu_release_compare(release1, release2);
}

static gchar *
fu_util_download_if_required(FuUtilPrivate *priv, const gchar *perhapsfn, GError **error)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(GFile) file = NULL;

	/* a local file */
	if (g_file_test(perhapsfn, G_FILE_TEST_EXISTS))
		return g_strdup(perhapsfn);
	if (!fu_util_is_url(perhapsfn))
		return g_strdup(perhapsfn);

	/* download the firmware to a cachedir */
	filename = fu_util_get_user_cache_path(perhapsfn);
	if (!fu_path_mkdir_parent(filename, error))
		return NULL;
	file = g_file_new_for_path(filename);
	if (!fwupd_client_download_file(priv->client,
					perhapsfn,
					file,
					FWUPD_CLIENT_DOWNLOAD_FLAG_NONE,
					priv->cancellable,
					error))
		return NULL;
	return g_steal_pointer(&filename);
}

static gboolean
fu_util_install(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(GBytes) blob_cab = NULL;
	g_autoptr(GPtrArray) components = NULL;
	g_autoptr(GPtrArray) devices_possible = NULL;
	g_autoptr(GPtrArray) errors = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(XbSilo) silo = NULL;

	/* progress */
	fu_progress_set_id(priv->progress, G_STRLOC);
	fu_progress_add_flag(priv->progress, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_add_step(priv->progress, FWUPD_STATUS_LOADING, 50, "start-engine");
	fu_progress_add_step(priv->progress, FWUPD_STATUS_DEVICE_WRITE, 50, NULL);

	/* load engine */
	if (!fu_util_start_engine(priv,
				  FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_REMOTES,
				  fu_progress_get_child(priv->progress),
				  error))
		return FALSE;
	fu_progress_step_done(priv->progress);

	/* handle both forms */
	if (g_strv_length(values) == 1) {
		devices_possible = fu_engine_get_devices(priv->engine, error);
		if (devices_possible == NULL)
			return FALSE;
		fwupd_device_array_ensure_parents(devices_possible);
	} else if (g_strv_length(values) == 2) {
		FuDevice *device = fu_util_get_device(priv, values[1], error);
		if (device == NULL)
			return FALSE;
		if (!priv->no_safety_check) {
			if (!fu_util_prompt_warning_fde(priv->console, FWUPD_DEVICE(device), error))
				return FALSE;
		}
		devices_possible =
		    fu_engine_get_devices_by_composite_id(priv->engine,
							  fu_device_get_composite_id(device),
							  error);
		if (devices_possible == NULL)
			return FALSE;

		g_ptr_array_add(devices_possible, device);
	} else {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}

	/* download if required */
	filename = fu_util_download_if_required(priv, values[0], error);
	if (filename == NULL)
		return FALSE;

	/* parse silo */
	blob_cab = fu_bytes_get_contents(filename, error);
	if (blob_cab == NULL) {
		fu_util_maybe_prefix_sandbox_error(filename, error);
		return FALSE;
	}
	silo = fu_engine_get_silo_from_blob(priv->engine, blob_cab, error);
	if (silo == NULL)
		return FALSE;
	components = xb_silo_query(silo, "components/component", 0, error);
	if (components == NULL)
		return FALSE;

	/* for each component in the silo */
	errors = g_ptr_array_new_with_free_func((GDestroyNotify)g_error_free);
	releases = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index(components, i);

		/* do any devices pass the requirements */
		for (guint j = 0; j < devices_possible->len; j++) {
			FuDevice *device = g_ptr_array_index(devices_possible, j);
			g_autoptr(FuRelease) release = fu_release_new();
			g_autoptr(GError) error_local = NULL;

			/* is this component valid for the device */
			fu_release_set_device(release, device);
			fu_release_set_request(release, priv->request);
			if (!fu_release_load(release, component, NULL, priv->flags, &error_local)) {
				g_debug("loading release failed on %s:%s failed: %s",
					fu_device_get_id(device),
					xb_node_query_text(component, "id", NULL),
					error_local->message);
				g_ptr_array_add(errors, g_steal_pointer(&error_local));
				continue;
			}
			if (!fu_engine_check_requirements(priv->engine,
							  release,
							  priv->flags,
							  &error_local)) {
				g_debug("requirement on %s:%s failed: %s",
					fu_device_get_id(device),
					xb_node_query_text(component, "id", NULL),
					error_local->message);
				g_ptr_array_add(errors, g_steal_pointer(&error_local));
				continue;
			}

			/* if component should have an update message from CAB */
			fu_device_incorporate_from_component(device, component);

			/* success */
			g_ptr_array_add(releases, g_steal_pointer(&release));
		}
	}

	/* order the install tasks by the device priority */
	g_ptr_array_sort(releases, fu_util_release_sort_cb);

	/* nothing suitable */
	if (releases->len == 0) {
		GError *error_tmp = fu_engine_error_array_get_best(errors);
		g_propagate_error(error, error_tmp);
		return FALSE;
	}

	priv->current_operation = FU_UTIL_OPERATION_INSTALL;
	g_signal_connect(FU_ENGINE(priv->engine),
			 "device-changed",
			 G_CALLBACK(fu_util_update_device_changed_cb),
			 priv);

	/* install all the tasks */
	if (!fu_engine_install_releases(priv->engine,
					priv->request,
					releases,
					blob_cab,
					fu_progress_get_child(priv->progress),
					priv->flags,
					error))
		return FALSE;
	fu_progress_step_done(priv->progress);

	fu_util_display_current_message(priv);

	/* we don't want to ask anything */
	if (priv->no_reboot_check) {
		g_debug("skipping reboot check");
		return TRUE;
	}

	/* success */
	return fu_util_prompt_complete(priv->console, priv->completion_flags, TRUE, error);
}

static gboolean
fu_util_install_release(FuUtilPrivate *priv, FwupdRelease *rel, GError **error)
{
	FwupdRemote *remote;
	GPtrArray *locations;
	const gchar *remote_id;
	const gchar *uri_tmp;
	g_auto(GStrv) argv = NULL;

	/* get the default release only until other parts of fwupd can cope */
	locations = fwupd_release_get_locations(rel);
	if (locations->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "release missing URI");
		return FALSE;
	}
	uri_tmp = g_ptr_array_index(locations, 0);
	remote_id = fwupd_release_get_remote_id(rel);
	if (remote_id == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "failed to find remote for %s",
			    uri_tmp);
		return FALSE;
	}

	remote = fu_engine_get_remote_by_id(priv->engine, remote_id, error);
	if (remote == NULL)
		return FALSE;

	argv = g_new0(gchar *, 2);
	/* local remotes may have the firmware already */
	if (fwupd_remote_get_kind(remote) == FWUPD_REMOTE_KIND_LOCAL && !fu_util_is_url(uri_tmp)) {
		const gchar *fn_cache = fwupd_remote_get_filename_cache(remote);
		g_autofree gchar *path = g_path_get_dirname(fn_cache);
		argv[0] = g_build_filename(path, uri_tmp, NULL);
	} else if (fwupd_remote_get_kind(remote) == FWUPD_REMOTE_KIND_DIRECTORY) {
		argv[0] = g_strdup(uri_tmp + 7);
		/* web remote, fu_util_install will download file */
	} else {
		argv[0] = fwupd_remote_build_firmware_uri(remote, uri_tmp, error);
	}

	/* reset progress before reusing it. */
	fu_progress_reset(priv->progress);

	return fu_util_install(priv, argv, error);
}

static gboolean
fu_util_update(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;
	gboolean no_updates_header = FALSE;
	gboolean latest_header = FALSE;

	if (priv->flags & FWUPD_INSTALL_FLAG_ALLOW_OLDER) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "--allow-older is not supported for this command");
		return FALSE;
	}

	if (priv->flags & FWUPD_INSTALL_FLAG_ALLOW_REINSTALL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "--allow-reinstall is not supported for this command");
		return FALSE;
	}

	if (!fu_util_start_engine(priv,
				  FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_REMOTES,
				  priv->progress,
				  error))
		return FALSE;

	/* DEVICE-ID and GUID are acceptable args to update */
	for (guint idx = 0; idx < g_strv_length(values); idx++) {
		if (!fwupd_guid_is_valid(values[idx]) && !fwupd_device_id_is_valid(values[idx])) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "'%s' is not a valid GUID nor DEVICE-ID",
				    values[idx]);
			return FALSE;
		}
	}

	priv->current_operation = FU_UTIL_OPERATION_UPDATE;
	g_signal_connect(FU_ENGINE(priv->engine),
			 "device-changed",
			 G_CALLBACK(fu_util_update_device_changed_cb),
			 priv);

	devices = fu_engine_get_devices(priv->engine, error);
	if (devices == NULL)
		return FALSE;
	fwupd_device_array_ensure_parents(devices);
	g_ptr_array_sort(devices, fu_util_sort_devices_by_flags_cb);
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devices, i);
		FwupdRelease *rel;
		const gchar *device_id = fu_device_get_id(dev);
		g_autoptr(GPtrArray) rels = NULL;
		g_autoptr(GError) error_local = NULL;
		gboolean dev_skip_byid = TRUE;

		/* only process particular DEVICE-ID or GUID if specified */
		for (guint idx = 0; idx < g_strv_length(values); idx++) {
			const gchar *tmpid = values[idx];
			if (fwupd_device_has_guid(dev, tmpid) || g_strcmp0(device_id, tmpid) == 0) {
				dev_skip_byid = FALSE;
				break;
			}
		}
		if (g_strv_length(values) > 0 && dev_skip_byid)
			continue;
		if (!fu_util_is_interesting_device(dev))
			continue;
		/* only show stuff that has metadata available */
		if (!fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE))
			continue;
		if (!fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_SUPPORTED)) {
			if (!no_updates_header) {
				fu_console_print_literal(
				    priv->console,
				    /* TRANSLATORS: message letting the user know no
				     * device upgrade available due to missing on LVFS */
				    _("Devices with no available firmware updates: "));
				no_updates_header = TRUE;
			}
			fu_console_print(priv->console, " • %s", fwupd_device_get_name(dev));
			continue;
		}
		if (!fwupd_device_match_flags(dev,
					      priv->filter_device_include,
					      priv->filter_device_exclude))
			continue;

		rels = fu_engine_get_upgrades(priv->engine, priv->request, device_id, &error_local);
		if (rels == NULL) {
			if (!latest_header) {
				fu_console_print_literal(
				    priv->console,
				    /* TRANSLATORS: message letting the user know no device upgrade
				     * available */
				    _("Devices with the latest available firmware version:"));
				latest_header = TRUE;
			}
			fu_console_print(priv->console, " • %s", fwupd_device_get_name(dev));
			/* discard the actual reason from user, but leave for debugging */
			g_debug("%s", error_local->message);
			continue;
		}

		rel = g_ptr_array_index(rels, 0);
		if (!priv->no_safety_check) {
			g_autofree gchar *title =
			    g_strdup_printf("%s %s",
					    fu_engine_get_host_vendor(priv->engine),
					    fu_engine_get_host_product(priv->engine));
			if (!fu_util_prompt_warning(priv->console, dev, rel, title, error))
				return FALSE;
			if (!fu_util_prompt_warning_fde(priv->console, dev, error))
				return FALSE;
		}

		if (!fu_util_install_release(priv, rel, &error_local)) {
			fu_console_print_literal(priv->console, error_local->message);
			continue;
		}
		fu_util_display_current_message(priv);
	}

	/* we don't want to ask anything */
	if (priv->no_reboot_check) {
		g_debug("skipping reboot check");
		return TRUE;
	}

	return fu_util_prompt_complete(priv->console, priv->completion_flags, TRUE, error);
}

static gboolean
fu_util_reinstall(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FwupdRelease) rel = NULL;
	g_autoptr(GPtrArray) rels = NULL;
	g_autoptr(FuDevice) dev = NULL;

	if (g_strv_length(values) != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}

	if (!fu_util_start_engine(priv,
				  FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_REMOTES,
				  priv->progress,
				  error))
		return FALSE;

	dev = fu_util_get_device(priv, values[0], error);
	if (dev == NULL)
		return FALSE;

	/* try to lookup/match release from client */
	rels = fu_engine_get_releases_for_device(priv->engine, priv->request, dev, error);
	if (rels == NULL)
		return FALSE;

	for (guint j = 0; j < rels->len; j++) {
		FwupdRelease *rel_tmp = g_ptr_array_index(rels, j);
		if (!fwupd_release_match_flags(rel_tmp,
					       priv->filter_release_include,
					       priv->filter_release_exclude))
			continue;
		if (fu_version_compare(fwupd_release_get_version(rel_tmp),
				       fu_device_get_version(dev),
				       fu_device_get_version_format(dev)) == 0) {
			rel = g_object_ref(rel_tmp);
			break;
		}
	}
	if (rel == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Unable to locate release for %s version %s",
			    fu_device_get_name(dev),
			    fu_device_get_version(dev));
		return FALSE;
	}

	/* update the console if composite devices are also updated */
	priv->current_operation = FU_UTIL_OPERATION_INSTALL;
	g_signal_connect(FU_ENGINE(priv->engine),
			 "device-changed",
			 G_CALLBACK(fu_util_update_device_changed_cb),
			 priv);
	priv->flags |= FWUPD_INSTALL_FLAG_ALLOW_REINSTALL;
	if (!fu_util_install_release(priv, rel, error))
		return FALSE;
	fu_util_display_current_message(priv);

	/* we don't want to ask anything */
	if (priv->no_reboot_check) {
		g_debug("skipping reboot check");
		return TRUE;
	}

	return fu_util_prompt_complete(priv->console, priv->completion_flags, TRUE, error);
}

static gboolean
fu_util_detach(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* progress */
	fu_progress_set_id(priv->progress, G_STRLOC);
	fu_progress_add_step(priv->progress, FWUPD_STATUS_LOADING, 95, "start-engine");
	fu_progress_add_step(priv->progress, FWUPD_STATUS_DEVICE_BUSY, 5, NULL);

	/* load engine */
	if (!fu_util_start_engine(priv,
				  FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_REMOTES,
				  fu_progress_get_child(priv->progress),
				  error))
		return FALSE;
	fu_progress_step_done(priv->progress);

	/* get device */
	priv->filter_device_exclude |= FWUPD_DEVICE_FLAG_IS_BOOTLOADER;
	if (g_strv_length(values) >= 1) {
		device = fu_util_get_device(priv, values[0], error);
		if (device == NULL)
			return FALSE;
	} else {
		device = fu_util_prompt_for_device(priv, NULL, error);
		if (device == NULL)
			return FALSE;
	}

	/* run vfunc */
	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;
	if (!fu_device_detach_full(device, fu_progress_get_child(priv->progress), error))
		return FALSE;
	fu_progress_step_done(priv->progress);
	return TRUE;
}

static gboolean
fu_util_unbind_driver(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* load engine */
	if (!fu_util_start_engine(priv,
				  FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_REMOTES,
				  priv->progress,
				  error))
		return FALSE;

	/* get device */
	if (g_strv_length(values) == 1) {
		device = fu_util_get_device(priv, values[0], error);
	} else {
		device = fu_util_prompt_for_device(priv, NULL, error);
	}
	if (device == NULL)
		return FALSE;

	/* run vfunc */
	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_unbind_driver(device, error);
}

static gboolean
fu_util_bind_driver(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* load engine */
	if (!fu_util_start_engine(priv,
				  FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_REMOTES,
				  priv->progress,
				  error))
		return FALSE;

	/* get device */
	if (g_strv_length(values) == 3) {
		device = fu_util_get_device(priv, values[2], error);
		if (device == NULL)
			return FALSE;
	} else if (g_strv_length(values) == 2) {
		device = fu_util_prompt_for_device(priv, NULL, error);
		if (device == NULL)
			return FALSE;
	} else {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}

	/* run vfunc */
	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_bind_driver(device, values[0], values[1], error);
}

static gboolean
fu_util_attach(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* progress */
	fu_progress_set_id(priv->progress, G_STRLOC);
	fu_progress_add_step(priv->progress, FWUPD_STATUS_LOADING, 95, "start-engine");
	fu_progress_add_step(priv->progress, FWUPD_STATUS_DEVICE_BUSY, 5, NULL);

	/* load engine */
	if (!fu_util_start_engine(priv,
				  FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_REMOTES,
				  fu_progress_get_child(priv->progress),
				  error))
		return FALSE;
	fu_progress_step_done(priv->progress);

	/* get device */
	if ((priv->flags & FWUPD_INSTALL_FLAG_FORCE) == 0)
		priv->filter_device_include |= FWUPD_DEVICE_FLAG_IS_BOOTLOADER;
	if (g_strv_length(values) >= 1) {
		device = fu_util_get_device(priv, values[0], error);
		if (device == NULL)
			return FALSE;
	} else {
		device = fu_util_prompt_for_device(priv, NULL, error);
		if (device == NULL)
			return FALSE;
	}

	/* run vfunc */
	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;
	if (!fu_device_attach_full(device, fu_progress_get_child(priv->progress), error))
		return FALSE;
	fu_progress_step_done(priv->progress);

	/* success */
	return TRUE;
}

static void
fu_util_report_metadata_to_string(GHashTable *metadata, guint idt, GString *str)
{
	g_autoptr(GList) keys =
	    g_list_sort(g_hash_table_get_keys(metadata), (GCompareFunc)g_strcmp0);
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		const gchar *value = g_hash_table_lookup(metadata, key);
		fu_string_append(str, idt, key, value);
	}
}

static gboolean
fu_util_get_report_metadata(FuUtilPrivate *priv, gchar **values, GError **error)
{
	GPtrArray *plugins;
	g_autoptr(GHashTable) metadata = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GString) str = g_string_new(NULL);

	/* progress */
	fu_progress_set_id(priv->progress, G_STRLOC);
	fu_progress_add_step(priv->progress, FWUPD_STATUS_LOADING, 95, "start-engine");
	fu_progress_add_step(priv->progress, FWUPD_STATUS_DEVICE_BUSY, 5, NULL);

	/* load engine */
	if (!fu_util_start_engine(priv,
				  FU_ENGINE_LOAD_FLAG_COLDPLUG,
				  fu_progress_get_child(priv->progress),
				  error))
		return FALSE;
	fu_progress_step_done(priv->progress);

	/* daemon metadata */
	metadata = fu_engine_get_report_metadata(priv->engine, error);
	if (metadata == NULL)
		return FALSE;
	fu_util_report_metadata_to_string(metadata, 0, str);

	/* device metadata */
	devices = fu_engine_get_devices(priv->engine, error);
	if (devices == NULL)
		return FALSE;
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		g_autoptr(FuDeviceLocker) locker = NULL;
		g_autoptr(GHashTable) metadata_post = NULL;
		g_autoptr(GHashTable) metadata_pre = NULL;

		locker = fu_device_locker_new(device, error);
		if (locker == NULL)
			return FALSE;
		metadata_pre = fu_device_report_metadata_pre(device);
		metadata_post = fu_device_report_metadata_post(device);
		if (metadata_pre != NULL || metadata_post != NULL) {
			fu_string_append(str,
					 0,
					 FWUPD_RESULT_KEY_DEVICE_ID,
					 fu_device_get_id(device));
		}
		if (metadata_pre != NULL) {
			fu_string_append(str, 1, "pre", NULL);
			fu_util_report_metadata_to_string(metadata_pre, 3, str);
		}
		if (metadata_post != NULL) {
			fu_string_append(str, 1, "post", NULL);
			fu_util_report_metadata_to_string(metadata_post, 3, str);
		}
	}

	/* plugin metadata */
	plugins = fu_engine_get_plugins(priv->engine);
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		if (fu_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED))
			continue;
		if (fu_plugin_get_report_metadata(plugin) == NULL)
			continue;
		fu_util_report_metadata_to_string(fu_plugin_get_report_metadata(plugin), 3, str);
	}
	fu_progress_step_done(priv->progress);

	/* display */
	fu_console_print_literal(priv->console, str->str);

	/* success */
	return TRUE;
}

static gboolean
fu_util_check_activation_needed(FuUtilPrivate *priv, GError **error)
{
	gboolean has_pending = FALSE;
	g_autoptr(FuHistory) history = fu_history_new();
	g_autoptr(GPtrArray) devices = fu_history_get_devices(history, error);
	if (devices == NULL)
		return FALSE;

	/* only start up the plugins needed */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index(devices, i);
		if (fu_device_has_flag(dev, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION)) {
			fu_engine_add_plugin_filter(priv->engine, fu_device_get_plugin(dev));
			has_pending = TRUE;
		}
	}

	if (!has_pending) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "No devices to activate");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_util_activate(FuUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean has_pending = FALSE;
	g_autoptr(GPtrArray) devices = NULL;

	/* check the history database before starting the daemon */
	if (!fu_util_check_activation_needed(priv, error))
		return FALSE;

	/* progress */
	fu_progress_set_id(priv->progress, G_STRLOC);
	fu_progress_add_step(priv->progress, FWUPD_STATUS_LOADING, 95, "start-engine");
	fu_progress_add_step(priv->progress, FWUPD_STATUS_DEVICE_BUSY, 5, NULL);

	/* load engine */
	if (!fu_util_start_engine(priv,
				  FU_ENGINE_LOAD_FLAG_READONLY | FU_ENGINE_LOAD_FLAG_COLDPLUG |
				      FU_ENGINE_LOAD_FLAG_REMOTES |
				      FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS,
				  fu_progress_get_child(priv->progress),
				  error))
		return FALSE;
	fu_progress_step_done(priv->progress);

	/* parse arguments */
	if (g_strv_length(values) == 0) {
		devices = fu_engine_get_devices(priv->engine, error);
		if (devices == NULL)
			return FALSE;
	} else if (g_strv_length(values) == 1) {
		FuDevice *device;
		device = fu_util_get_device(priv, values[0], error);
		if (device == NULL)
			return FALSE;
		devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
		g_ptr_array_add(devices, device);
	} else {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}

	/* activate anything with _NEEDS_ACTIVATION */
	/* order by device priority */
	g_ptr_array_sort(devices, fu_util_device_order_sort_cb);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		if (!fwupd_device_match_flags(FWUPD_DEVICE(device),
					      priv->filter_device_include,
					      priv->filter_device_exclude))
			continue;
		if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION))
			continue;
		has_pending = TRUE;
		fu_console_print(
		    priv->console,
		    "%s %s…",
		    /* TRANSLATORS: shown when shutting down to switch to the new version */
		    _("Activating firmware update"),
		    fu_device_get_name(device));
		if (!fu_engine_activate(priv->engine,
					fu_device_get_id(device),
					fu_progress_get_child(priv->progress),
					error))
			return FALSE;
	}
	fu_progress_step_done(priv->progress);

	if (!has_pending) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "No devices to activate");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_util_export_hwids(FuUtilPrivate *priv, gchar **values, GError **error)
{
	FuContext *ctx = fu_engine_get_context(priv->engine);
	FuHwids *hwids = fu_context_get_hwids(ctx);
	g_autoptr(GKeyFile) kf = g_key_file_new();
	g_autoptr(GPtrArray) hwid_keys = NULL;

	/* check args */
	if (g_strv_length(values) != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments, expected HWIDS-FILE");
		return FALSE;
	}

	/* setup default hwids */
	if (!fu_context_load_hwinfo(ctx, priv->progress, FU_CONTEXT_HWID_FLAG_LOAD_ALL, error))
		return FALSE;

	/* save all keys */
	hwid_keys = fu_hwids_get_keys(hwids);
	for (guint i = 0; i < hwid_keys->len; i++) {
		const gchar *hwid_key = g_ptr_array_index(hwid_keys, i);
		const gchar *value = fu_hwids_get_value(hwids, hwid_key);
		if (value == NULL)
			continue;
		g_key_file_set_string(kf, "HwIds", hwid_key, value);
	}

	/* success */
	return g_key_file_save_to_file(kf, values[0], error);
}

static gboolean
fu_util_hwids(FuUtilPrivate *priv, gchar **values, GError **error)
{
	FuContext *ctx = fu_engine_get_context(priv->engine);
	FuHwids *hwids = fu_context_get_hwids(ctx);
	g_autoptr(GPtrArray) hwid_keys = fu_hwids_get_keys(hwids);

	/* a keyfile with overrides */
	if (g_strv_length(values) == 1) {
		g_autoptr(GKeyFile) kf = g_key_file_new();
		if (!g_key_file_load_from_file(kf, values[0], G_KEY_FILE_NONE, error))
			return FALSE;
		for (guint i = 0; i < hwid_keys->len; i++) {
			const gchar *hwid_key = g_ptr_array_index(hwid_keys, i);
			g_autofree gchar *tmp = NULL;
			tmp = g_key_file_get_string(kf, "HwIds", hwid_key, NULL);
			fu_hwids_add_value(hwids, hwid_key, tmp);
		}
	}
	if (!fu_context_load_hwinfo(ctx, priv->progress, FU_CONTEXT_HWID_FLAG_LOAD_ALL, error))
		return FALSE;

	/* show debug output */
	fu_console_print_literal(priv->console, "Computer Information");
	fu_console_print_literal(priv->console, "--------------------");
	for (guint i = 0; i < hwid_keys->len; i++) {
		const gchar *hwid_key = g_ptr_array_index(hwid_keys, i);
		const gchar *value = fu_hwids_get_value(hwids, hwid_key);
		if (value == NULL)
			continue;
		if (g_strcmp0(hwid_key, FU_HWIDS_KEY_BIOS_MAJOR_RELEASE) == 0 ||
		    g_strcmp0(hwid_key, FU_HWIDS_KEY_BIOS_MINOR_RELEASE) == 0) {
			guint64 val = g_ascii_strtoull(value, NULL, 16);
			fu_console_print(priv->console, "%s: %" G_GUINT64_FORMAT, hwid_key, val);
		} else {
			fu_console_print(priv->console, "%s: %s", hwid_key, value);
		}
	}

	/* show GUIDs */
	fu_console_print_literal(priv->console, "Hardware IDs");
	fu_console_print_literal(priv->console, "------------");
	for (guint i = 0; i < 15; i++) {
		const gchar *keys = NULL;
		g_autofree gchar *guid = NULL;
		g_autofree gchar *key = NULL;
		g_autofree gchar *keys_str = NULL;
		g_auto(GStrv) keysv = NULL;
		g_autoptr(GError) error_local = NULL;

		/* get the GUID */
		key = g_strdup_printf("HardwareID-%u", i);
		keys = fu_hwids_get_replace_keys(hwids, key);
		guid = fu_hwids_get_guid(hwids, key, &error_local);
		if (guid == NULL) {
			fu_console_print_literal(priv->console, error_local->message);
			continue;
		}

		/* show what makes up the GUID */
		keysv = g_strsplit(keys, "&", -1);
		keys_str = g_strjoinv(" + ", keysv);
		fu_console_print(priv->console, "{%s}   <- %s", guid, keys_str);
	}

	return TRUE;
}

static gboolean
fu_util_self_sign(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autofree gchar *sig = NULL;

	/* check args */
	if (g_strv_length(values) != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments: value expected");
		return FALSE;
	}

	/* start engine */
	if (!fu_util_start_engine(priv, FU_ENGINE_LOAD_FLAG_NONE, priv->progress, error))
		return FALSE;
	sig = fu_engine_self_sign(priv->engine,
				  values[0],
				  JCAT_SIGN_FLAG_ADD_TIMESTAMP | JCAT_SIGN_FLAG_ADD_CERT,
				  error);
	if (sig == NULL)
		return FALSE;
	fu_console_print_literal(priv->console, sig);
	return TRUE;
}

static void
fu_util_device_added_cb(FwupdClient *client, FwupdDevice *device, gpointer user_data)
{
	FuUtilPrivate *priv = (FuUtilPrivate *)user_data;
	g_autofree gchar *tmp = fu_util_device_to_string(priv->client, device, 0);
	/* TRANSLATORS: this is when a device is hotplugged */
	fu_console_print(priv->console, "%s\n%s", _("Device added:"), tmp);
}

static void
fu_util_device_removed_cb(FwupdClient *client, FwupdDevice *device, gpointer user_data)
{
	FuUtilPrivate *priv = (FuUtilPrivate *)user_data;
	g_autofree gchar *tmp = fu_util_device_to_string(priv->client, device, 0);
	/* TRANSLATORS: this is when a device is hotplugged */
	fu_console_print(priv->console, "%s\n%s", _("Device removed:"), tmp);
}

static void
fu_util_device_changed_cb(FwupdClient *client, FwupdDevice *device, gpointer user_data)
{
	FuUtilPrivate *priv = (FuUtilPrivate *)user_data;
	g_autofree gchar *tmp = fu_util_device_to_string(priv->client, device, 0);
	/* TRANSLATORS: this is when a device has been updated */
	fu_console_print(priv->console, "%s\n%s", _("Device changed:"), tmp);
}

static void
fu_util_changed_cb(FwupdClient *client, gpointer user_data)
{
	FuUtilPrivate *priv = (FuUtilPrivate *)user_data;
	/* TRANSLATORS: this is when the daemon state changes */
	fu_console_print_literal(priv->console, _("Changed"));
}

static gboolean
fu_util_monitor(FuUtilPrivate *priv, gchar **values, GError **error)
{
	/* get all the devices */
	if (!fwupd_client_connect(priv->client, priv->cancellable, error))
		return FALSE;

	/* watch for any hotplugged device */
	g_signal_connect(FWUPD_CLIENT(priv->client),
			 "changed",
			 G_CALLBACK(fu_util_changed_cb),
			 priv);
	g_signal_connect(FWUPD_CLIENT(priv->client),
			 "device-added",
			 G_CALLBACK(fu_util_device_added_cb),
			 priv);
	g_signal_connect(FWUPD_CLIENT(priv->client),
			 "device-removed",
			 G_CALLBACK(fu_util_device_removed_cb),
			 priv);
	g_signal_connect(FWUPD_CLIENT(priv->client),
			 "device-changed",
			 G_CALLBACK(fu_util_device_changed_cb),
			 priv);
	g_signal_connect(G_CANCELLABLE(priv->cancellable),
			 "cancelled",
			 G_CALLBACK(fu_util_cancelled_cb),
			 priv);
	g_main_loop_run(priv->loop);
	return TRUE;
}

static gboolean
fu_util_get_firmware_types(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) firmware_types = NULL;

	/* load engine */
	if (!fu_engine_load(priv->engine,
			    FU_ENGINE_LOAD_FLAG_READONLY | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS,
			    priv->progress,
			    error))
		return FALSE;

	firmware_types = fu_context_get_firmware_gtype_ids(fu_engine_get_context(priv->engine));
	for (guint i = 0; i < firmware_types->len; i++) {
		const gchar *id = g_ptr_array_index(firmware_types, i);
		fu_console_print_literal(priv->console, id);
	}
	if (firmware_types->len == 0) {
		/* TRANSLATORS: nothing found */
		fu_console_print_literal(priv->console, _("No firmware IDs found"));
		return TRUE;
	}

	return TRUE;
}

static gboolean
fu_util_get_firmware_gtypes(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GArray) firmware_types = NULL;

	/* load engine */
	if (!fu_engine_load(priv->engine,
			    FU_ENGINE_LOAD_FLAG_READONLY | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS,
			    priv->progress,
			    error))
		return FALSE;

	firmware_types = fu_context_get_firmware_gtypes(fu_engine_get_context(priv->engine));
	for (guint i = 0; i < firmware_types->len; i++) {
		GType gtype = g_array_index(firmware_types, GType, i);
		fu_console_print_literal(priv->console, g_type_name(gtype));
	}
	if (firmware_types->len == 0) {
		/* TRANSLATORS: nothing found */
		fu_console_print_literal(priv->console, _("No firmware found"));
		return TRUE;
	}

	return TRUE;
}

static gchar *
fu_util_prompt_for_firmware_type(FuUtilPrivate *priv, GPtrArray *firmware_types, GError **error)
{
	guint idx;

	/* no detected types */
	if (firmware_types->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "No detected firmware types");
		return NULL;
	}

	/* there is no point asking */
	if (firmware_types->len == 1) {
		const gchar *id = g_ptr_array_index(firmware_types, 0);
		return g_strdup(id);
	}

	/* TRANSLATORS: this is to abort the interactive prompt */
	fu_console_print(priv->console, "0.\t%s", _("Cancel"));
	for (guint i = 0; i < firmware_types->len; i++) {
		const gchar *id = g_ptr_array_index(firmware_types, i);
		fu_console_print(priv->console, "%u.\t%s", i + 1, id);
	}
	/* TRANSLATORS: get interactive prompt */
	idx = fu_console_input_uint(priv->console, firmware_types->len, "%s", _("Choose firmware"));
	if (idx == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "Request canceled");
		return NULL;
	}

	return g_strdup(g_ptr_array_index(firmware_types, idx - 1));
}

static gboolean
fu_util_firmware_parse(FuUtilPrivate *priv, gchar **values, GError **error)
{
	FuContext *ctx = fu_engine_get_context(priv->engine);
	GType gtype;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autofree gchar *firmware_type = NULL;
	g_autofree gchar *str = NULL;

	/* check args */
	if (g_strv_length(values) == 0 || g_strv_length(values) > 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments: filename required");
		return FALSE;
	}

	/* load file */
	blob = fu_bytes_get_contents(values[0], error);
	if (blob == NULL)
		return FALSE;

	/* load engine */
	if (!fu_engine_load(priv->engine,
			    FU_ENGINE_LOAD_FLAG_READONLY | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS,
			    priv->progress,
			    error))
		return FALSE;

	/* find the GType to use */
	if (g_strv_length(values) == 1) {
		g_autoptr(GPtrArray) firmware_types = fu_context_get_firmware_gtype_ids(ctx);
		firmware_type = fu_util_prompt_for_firmware_type(priv, firmware_types, error);
		if (firmware_type == NULL)
			return FALSE;
	} else if (g_strcmp0(values[1], "auto") == 0) {
		g_autoptr(GPtrArray) gtype_ids = fu_context_get_firmware_gtype_ids(ctx);
		g_autoptr(GPtrArray) firmware_auto_types = g_ptr_array_new_with_free_func(g_free);
		for (guint i = 0; i < gtype_ids->len; i++) {
			const gchar *gtype_id = g_ptr_array_index(gtype_ids, i);
			GType gtype_tmp;
			g_autofree gchar *firmware_str = NULL;
			g_autoptr(FuFirmware) firmware_tmp = NULL;
			g_autoptr(GError) error_local = NULL;

			if (g_strcmp0(gtype_id, "raw") == 0)
				continue;
			g_debug("parsing as %s", gtype_id);
			gtype_tmp = fu_context_get_firmware_gtype_by_id(ctx, gtype_id);
			if (gtype_tmp == G_TYPE_INVALID) {
				g_set_error(error,
					    G_IO_ERROR,
					    G_IO_ERROR_NOT_FOUND,
					    "GType %s not supported",
					    gtype_id);
				return FALSE;
			}
			firmware_tmp = g_object_new(gtype_tmp, NULL);
			if (fu_firmware_has_flag(firmware_tmp, FU_FIRMWARE_FLAG_NO_AUTO_DETECTION))
				continue;
			if (!fu_firmware_parse(firmware_tmp,
					       blob,
					       FWUPD_INSTALL_FLAG_NO_SEARCH,
					       &error_local)) {
				g_debug("failed to parse as %s: %s",
					gtype_id,
					error_local->message);
				continue;
			}
			firmware_str = fu_firmware_to_string(firmware_tmp);
			g_debug("parsed as %s: %s", gtype_id, firmware_str);
			g_ptr_array_add(firmware_auto_types, g_strdup(gtype_id));
		}
		firmware_type = fu_util_prompt_for_firmware_type(priv, firmware_auto_types, error);
		if (firmware_type == NULL)
			return FALSE;
	} else {
		firmware_type = g_strdup(values[1]);
	}
	gtype = fu_context_get_firmware_gtype_by_id(ctx, firmware_type);
	if (gtype == G_TYPE_INVALID) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_FOUND,
			    "GType %s not supported",
			    firmware_type);
		return FALSE;
	}

	/* does firmware specify an internal size */
	firmware = g_object_new(gtype, NULL);
	if (fu_firmware_has_flag(firmware, FU_FIRMWARE_FLAG_HAS_STORED_SIZE)) {
		g_autoptr(FuFirmware) firmware_linear = fu_linear_firmware_new(gtype);
		g_autoptr(GPtrArray) imgs = NULL;
		if (!fu_firmware_parse(firmware_linear, blob, priv->flags, error))
			return FALSE;
		imgs = fu_firmware_get_images(firmware_linear);
		if (imgs->len == 1) {
			g_set_object(&firmware, g_ptr_array_index(imgs, 0));
		} else {
			g_set_object(&firmware, firmware_linear);
		}
	} else {
		if (!fu_firmware_parse(firmware, blob, priv->flags, error))
			return FALSE;
	}

	str = fu_firmware_to_string(firmware);
	fu_console_print_literal(priv->console, str);
	return TRUE;
}

static gboolean
fu_util_firmware_export(FuUtilPrivate *priv, gchar **values, GError **error)
{
	FuContext *ctx = fu_engine_get_context(priv->engine);
	FuFirmwareExportFlags flags = FU_FIRMWARE_EXPORT_FLAG_NONE;
	GType gtype;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autofree gchar *firmware_type = NULL;
	g_autofree gchar *str = NULL;

	/* check args */
	if (g_strv_length(values) == 0 || g_strv_length(values) > 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments: filename required");
		return FALSE;
	}

	if (g_strv_length(values) == 2)
		firmware_type = g_strdup(values[1]);

	/* load file */
	blob = fu_bytes_get_contents(values[0], error);
	if (blob == NULL)
		return FALSE;

	/* load engine */
	if (!fu_engine_load(priv->engine,
			    FU_ENGINE_LOAD_FLAG_READONLY | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS,
			    priv->progress,
			    error))
		return FALSE;

	/* find the GType to use */
	if (firmware_type == NULL) {
		g_autoptr(GPtrArray) firmware_types = fu_context_get_firmware_gtype_ids(ctx);
		firmware_type = fu_util_prompt_for_firmware_type(priv, firmware_types, error);
	}
	if (firmware_type == NULL)
		return FALSE;
	gtype = fu_context_get_firmware_gtype_by_id(ctx, firmware_type);
	if (gtype == G_TYPE_INVALID) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_FOUND,
			    "GType %s not supported",
			    firmware_type);
		return FALSE;
	}
	firmware = g_object_new(gtype, NULL);
	if (!fu_firmware_parse(firmware, blob, priv->flags, error))
		return FALSE;
	if (priv->show_all)
		flags |= FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG;
	str = fu_firmware_export_to_xml(firmware, flags, error);
	if (str == NULL)
		return FALSE;
	fu_console_print_literal(priv->console, str);
	return TRUE;
}

static gboolean
fu_util_firmware_extract(FuUtilPrivate *priv, gchar **values, GError **error)
{
	FuContext *ctx = fu_engine_get_context(priv->engine);
	GType gtype;
	g_autofree gchar *firmware_type = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GPtrArray) images = NULL;

	/* check args */
	if (g_strv_length(values) == 0 || g_strv_length(values) > 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments: filename required");
		return FALSE;
	}
	if (g_strv_length(values) == 2)
		firmware_type = g_strdup(values[1]);

	/* load file */
	blob = fu_bytes_get_contents(values[0], error);
	if (blob == NULL)
		return FALSE;

	/* load engine */
	if (!fu_engine_load(priv->engine,
			    FU_ENGINE_LOAD_FLAG_READONLY | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS,
			    priv->progress,
			    error))
		return FALSE;

	/* find the GType to use */
	if (firmware_type == NULL) {
		g_autoptr(GPtrArray) firmware_types = fu_context_get_firmware_gtype_ids(ctx);
		firmware_type = fu_util_prompt_for_firmware_type(priv, firmware_types, error);
	}
	if (firmware_type == NULL)
		return FALSE;
	gtype = fu_context_get_firmware_gtype_by_id(ctx, firmware_type);
	if (gtype == G_TYPE_INVALID) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_FOUND,
			    "GType %s not supported",
			    firmware_type);
		return FALSE;
	}
	firmware = g_object_new(gtype, NULL);
	if (!fu_firmware_parse(firmware, blob, priv->flags, error))
		return FALSE;
	str = fu_firmware_to_string(firmware);
	fu_console_print_literal(priv->console, str);
	images = fu_firmware_get_images(firmware);
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autofree gchar *fn = NULL;
		g_autoptr(GBytes) blob_img = NULL;

		/* get raw image without generated header, footer or crc */
		blob_img = fu_firmware_get_bytes(img, error);
		if (blob_img == NULL)
			return FALSE;
		if (g_bytes_get_size(blob_img) == 0)
			continue;

		/* use suitable filename */
		if (fu_firmware_get_filename(img) != NULL) {
			fn = g_strdup(fu_firmware_get_filename(img));
		} else if (fu_firmware_get_id(img) != NULL) {
			fn = g_strdup_printf("id-%s.fw", fu_firmware_get_id(img));
		} else if (fu_firmware_get_idx(img) != 0x0) {
			fn = g_strdup_printf("idx-0x%x.fw", (guint)fu_firmware_get_idx(img));
		} else {
			fn = g_strdup_printf("img-0x%x.fw", i);
		}
		/* TRANSLATORS: decompressing images from a container firmware */
		fu_console_print(priv->console, "%s : %s", _("Writing file:"), fn);
		if (!fu_bytes_set_contents(fn, blob_img, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_util_firmware_build(FuUtilPrivate *priv, gchar **values, GError **error)
{
	GType gtype = FU_TYPE_FIRMWARE;
	const gchar *tmp;
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuFirmware) firmware_dst = NULL;
	g_autoptr(GBytes) blob_dst = NULL;
	g_autoptr(GBytes) blob_src = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbNode) n = NULL;
	g_autoptr(XbSilo) silo = NULL;

	/* check args */
	if (g_strv_length(values) != 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments: filename required");
		return FALSE;
	}

	/* load file */
	blob_src = fu_bytes_get_contents(values[0], error);
	if (blob_src == NULL)
		return FALSE;

	/* load engine */
	if (!fu_engine_load(priv->engine,
			    FU_ENGINE_LOAD_FLAG_READONLY | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS,
			    priv->progress,
			    error))
		return FALSE;

	/* parse XML */
	if (!xb_builder_source_load_bytes(source, blob_src, XB_BUILDER_SOURCE_FLAG_NONE, error)) {
		g_prefix_error(error, "could not parse XML: ");
		return FALSE;
	}
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, error);
	if (silo == NULL)
		return FALSE;

	/* create FuFirmware of specific GType */
	n = xb_silo_query_first(silo, "firmware", error);
	if (n == NULL)
		return FALSE;
	tmp = xb_node_get_attr(n, "gtype");
	if (tmp != NULL) {
		gtype = g_type_from_name(tmp);
		if (gtype == G_TYPE_INVALID) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_FOUND,
				    "GType %s not registered",
				    tmp);
			return FALSE;
		}
	}
	tmp = xb_node_get_attr(n, "id");
	if (tmp != NULL) {
		gtype =
		    fu_context_get_firmware_gtype_by_id(fu_engine_get_context(priv->engine), tmp);
		if (gtype == G_TYPE_INVALID) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_FOUND,
				    "GType %s not supported",
				    tmp);
			return FALSE;
		}
	}
	firmware = g_object_new(gtype, NULL);
	if (!fu_firmware_build(firmware, n, error))
		return FALSE;

	/* write new file */
	blob_dst = fu_firmware_write(firmware, error);
	if (blob_dst == NULL)
		return FALSE;
	if (!fu_bytes_set_contents(values[1], blob_dst, error))
		return FALSE;

	/* show what we wrote */
	firmware_dst = g_object_new(gtype, NULL);
	if (!fu_firmware_parse(firmware_dst, blob_dst, priv->flags, error))
		return FALSE;
	str = fu_firmware_to_string(firmware_dst);
	fu_console_print_literal(priv->console, str);

	/* success */
	return TRUE;
}

static gboolean
fu_util_firmware_convert(FuUtilPrivate *priv, gchar **values, GError **error)
{
	FuContext *ctx = fu_engine_get_context(priv->engine);
	GType gtype_dst;
	GType gtype_src;
	g_autofree gchar *firmware_type_dst = NULL;
	g_autofree gchar *firmware_type_src = NULL;
	g_autofree gchar *str_dst = NULL;
	g_autofree gchar *str_src = NULL;
	g_autoptr(FuFirmware) firmware_dst = NULL;
	g_autoptr(FuFirmware) firmware_src = NULL;
	g_autoptr(GBytes) blob_dst = NULL;
	g_autoptr(GBytes) blob_src = NULL;
	g_autoptr(GPtrArray) images = NULL;

	/* check args */
	if (g_strv_length(values) < 2 || g_strv_length(values) > 4) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments: filename required");
		return FALSE;
	}

	if (g_strv_length(values) > 2)
		firmware_type_src = g_strdup(values[2]);
	if (g_strv_length(values) > 3)
		firmware_type_dst = g_strdup(values[3]);

	/* load file */
	blob_src = fu_bytes_get_contents(values[0], error);
	if (blob_src == NULL)
		return FALSE;

	/* load engine */
	if (!fu_engine_load(priv->engine,
			    FU_ENGINE_LOAD_FLAG_READONLY | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS,
			    priv->progress,
			    error))
		return FALSE;

	/* find the GType to use */
	if (firmware_type_src == NULL) {
		g_autoptr(GPtrArray) firmware_types = fu_context_get_firmware_gtype_ids(ctx);
		firmware_type_src = fu_util_prompt_for_firmware_type(priv, firmware_types, error);
	}
	if (firmware_type_src == NULL)
		return FALSE;
	if (firmware_type_dst == NULL) {
		g_autoptr(GPtrArray) firmware_types = fu_context_get_firmware_gtype_ids(ctx);
		firmware_type_dst = fu_util_prompt_for_firmware_type(priv, firmware_types, error);
	}
	if (firmware_type_dst == NULL)
		return FALSE;
	gtype_src = fu_context_get_firmware_gtype_by_id(ctx, firmware_type_src);
	if (gtype_src == G_TYPE_INVALID) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_FOUND,
			    "GType %s not supported",
			    firmware_type_src);
		return FALSE;
	}
	firmware_src = g_object_new(gtype_src, NULL);
	if (!fu_firmware_parse(firmware_src, blob_src, priv->flags, error))
		return FALSE;
	gtype_dst = fu_context_get_firmware_gtype_by_id(ctx, firmware_type_dst);
	if (gtype_dst == G_TYPE_INVALID) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_FOUND,
			    "GType %s not supported",
			    firmware_type_dst);
		return FALSE;
	}
	str_src = fu_firmware_to_string(firmware_src);
	fu_console_print_literal(priv->console, str_src);

	/* copy images */
	firmware_dst = g_object_new(gtype_dst, NULL);
	images = fu_firmware_get_images(firmware_src);
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		fu_firmware_add_image(firmware_dst, img);
	}

	/* copy data as fallback, preferring a binary blob to the export */
	if (images->len == 0) {
		g_autoptr(GBytes) fw = NULL;
		g_autoptr(FuFirmware) img = NULL;
		fw = fu_firmware_get_bytes(firmware_src, NULL);
		if (fw == NULL) {
			fw = fu_firmware_write(firmware_src, error);
			if (fw == NULL)
				return FALSE;
		}
		img = fu_firmware_new_from_bytes(fw);
		fu_firmware_add_image(firmware_dst, img);
	}

	/* write new file */
	blob_dst = fu_firmware_write(firmware_dst, error);
	if (blob_dst == NULL)
		return FALSE;
	if (!fu_bytes_set_contents(values[1], blob_dst, error))
		return FALSE;
	str_dst = fu_firmware_to_string(firmware_dst);
	fu_console_print_literal(priv->console, str_dst);

	/* success */
	return TRUE;
}

static GBytes *
fu_util_hex_string_to_bytes(const gchar *val, GError **error)
{
	gsize valsz;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* sanity check */
	if (val == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "nothing to parse");
		return NULL;
	}

	/* parse each hex byte */
	valsz = strlen(val);
	for (guint i = 0; i < valsz; i += 2) {
		guint8 tmp = 0;
		if (!fu_firmware_strparse_uint8_safe(val, valsz, i, &tmp, error))
			return NULL;
		fu_byte_array_append_uint8(buf, tmp);
	}
	return g_bytes_new(buf->data, buf->len);
}

static gboolean
fu_util_firmware_patch(FuUtilPrivate *priv, gchar **values, GError **error)
{
	FuContext *ctx = fu_engine_get_context(priv->engine);
	GType gtype;
	g_autofree gchar *firmware_type = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GBytes) blob_dst = NULL;
	g_autoptr(GBytes) blob_src = NULL;
	g_autoptr(GBytes) patch = NULL;
	guint64 offset = 0;

	/* check args */
	if (g_strv_length(values) != 3 && g_strv_length(values) != 4) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_ARGS,
			    "Invalid arguments, expected %s",
			    "FILENAME OFFSET DATA [FIRMWARE-TYPE]");
		return FALSE;
	}

	/* hardcoded */
	if (g_strv_length(values) == 4)
		firmware_type = g_strdup(values[3]);

	/* load file */
	blob_src = fu_bytes_get_contents(values[0], error);
	if (blob_src == NULL)
		return FALSE;

	/* parse offset */
	if (!fu_strtoull(values[1], &offset, 0x0, G_MAXUINT32, error)) {
		g_prefix_error(error, "failed to parse offset: ");
		return FALSE;
	}

	/* parse blob */
	patch = fu_util_hex_string_to_bytes(values[2], error);
	if (patch == NULL)
		return FALSE;
	if (g_bytes_get_size(patch) == 0) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_ARGS, "no data provided");
		return FALSE;
	}

	/* load engine */
	if (!fu_engine_load(priv->engine,
			    FU_ENGINE_LOAD_FLAG_READONLY | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS,
			    priv->progress,
			    error))
		return FALSE;

	/* find the GType to use */
	if (firmware_type == NULL) {
		g_autoptr(GPtrArray) firmware_types = fu_context_get_firmware_gtype_ids(ctx);
		firmware_type = fu_util_prompt_for_firmware_type(priv, firmware_types, error);
	}
	if (firmware_type == NULL)
		return FALSE;
	gtype = fu_context_get_firmware_gtype_by_id(ctx, firmware_type);
	if (gtype == G_TYPE_INVALID) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_FOUND,
			    "GType %s not supported",
			    firmware_type);
		return FALSE;
	}
	firmware = g_object_new(gtype, NULL);
	if (!fu_firmware_parse(firmware, blob_src, priv->flags, error))
		return FALSE;

	/* add patch */
	fu_firmware_add_patch(firmware, offset, patch);

	/* write new file */
	blob_dst = fu_firmware_write(firmware, error);
	if (blob_dst == NULL)
		return FALSE;
	if (!fu_bytes_set_contents(values[0], blob_dst, error))
		return FALSE;
	str = fu_firmware_to_string(firmware);
	fu_console_print_literal(priv->console, str);

	/* success */
	return TRUE;
}

static gboolean
fu_util_verify_update(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autofree gchar *str = NULL;
	g_autoptr(FuDevice) dev = NULL;

	/* progress */
	fu_progress_set_id(priv->progress, G_STRLOC);
	fu_progress_add_step(priv->progress, FWUPD_STATUS_LOADING, 50, "start-engine");
	fu_progress_add_step(priv->progress, FWUPD_STATUS_DEVICE_VERIFY, 50, "verify-update");

	/* load engine */
	if (!fu_util_start_engine(priv,
				  FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_REMOTES,
				  fu_progress_get_child(priv->progress),
				  error))
		return FALSE;
	fu_progress_step_done(priv->progress);

	/* get device */
	priv->filter_device_include |= FWUPD_DEVICE_FLAG_UPDATABLE;
	if (g_strv_length(values) == 1) {
		dev = fu_util_get_device(priv, values[0], error);
		if (dev == NULL)
			return FALSE;
	} else {
		dev = fu_util_prompt_for_device(priv, NULL, error);
		if (dev == NULL)
			return FALSE;
	}

	/* add checksums */
	if (!fu_engine_verify_update(priv->engine,
				     fu_device_get_id(dev),
				     fu_progress_get_child(priv->progress),
				     error))
		return FALSE;
	fu_progress_step_done(priv->progress);

	/* show checksums */
	str = fu_device_to_string(dev);
	fu_console_print_literal(priv->console, str);
	return TRUE;
}

static gboolean
fu_util_get_history(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GNode) root = g_node_new(NULL);

	/* load engine */
	if (!fu_util_start_engine(priv,
				  FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_REMOTES,
				  priv->progress,
				  error))
		return FALSE;

	/* get all devices from the history database */
	devices = fu_engine_get_history(priv->engine, error);
	if (devices == NULL)
		return FALSE;

	/* show each device */
	for (guint i = 0; i < devices->len; i++) {
		g_autoptr(GPtrArray) rels = NULL;
		FwupdDevice *dev = g_ptr_array_index(devices, i);
		FwupdRelease *rel;
		const gchar *remote;
		GNode *child;

		if (!fwupd_device_match_flags(dev,
					      priv->filter_device_include,
					      priv->filter_device_exclude))
			continue;
		child = g_node_append_data(root, dev);

		rel = fwupd_device_get_release_default(dev);
		if (rel == NULL)
			continue;
		remote = fwupd_release_get_remote_id(rel);

		/* doesn't actually map to remote */
		if (remote == NULL) {
			g_node_append_data(child, rel);
			continue;
		}

		/* try to lookup releases from client */
		rels = fu_engine_get_releases(priv->engine,
					      priv->request,
					      fwupd_device_get_id(dev),
					      error);
		if (rels == NULL)
			return FALSE;

		/* map to a release in client */
		for (guint j = 0; j < rels->len; j++) {
			FwupdRelease *rel2 = g_ptr_array_index(rels, j);
			if (!fwupd_release_match_flags(rel2,
						       priv->filter_release_include,
						       priv->filter_release_exclude))
				continue;
			if (g_strcmp0(remote, fwupd_release_get_remote_id(rel2)) != 0)
				continue;
			if (g_strcmp0(fwupd_release_get_version(rel),
				      fwupd_release_get_version(rel2)) != 0)
				continue;
			g_node_append_data(child, g_object_ref(rel2));
			rel = NULL;
			break;
		}

		/* didn't match anything */
		if (rels->len == 0 || rel != NULL) {
			g_node_append_data(child, rel);
			continue;
		}
	}
	fu_util_print_tree(priv->console, priv->client, root);

	return TRUE;
}

static gboolean
fu_util_refresh_remote(FuUtilPrivate *priv, FwupdRemote *remote, GError **error)
{
	const gchar *metadata_uri = NULL;
	g_autoptr(GBytes) bytes_raw = NULL;
	g_autoptr(GBytes) bytes_sig = NULL;

	/* signature */
	metadata_uri = fwupd_remote_get_metadata_uri_sig(remote);
	if (metadata_uri == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOTHING_TO_DO,
			    "no metadata signature URI available for %s",
			    fwupd_remote_get_id(remote));
		return FALSE;
	}
	bytes_sig = fwupd_client_download_bytes(priv->client,
						metadata_uri,
						FWUPD_CLIENT_DOWNLOAD_FLAG_NONE,
						priv->cancellable,
						error);
	if (bytes_sig == NULL)
		return FALSE;
	if (!fwupd_remote_load_signature_bytes(remote, bytes_sig, error))
		return FALSE;

	/* payload */
	metadata_uri = fwupd_remote_get_metadata_uri(remote);
	if (metadata_uri == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOTHING_TO_DO,
			    "no metadata URI available for %s",
			    fwupd_remote_get_id(remote));
		return FALSE;
	}
	bytes_raw = fwupd_client_download_bytes(priv->client,
						metadata_uri,
						FWUPD_CLIENT_DOWNLOAD_FLAG_NONE,
						priv->cancellable,
						error);
	if (bytes_raw == NULL)
		return FALSE;

	/* send to daemon */
	g_info("updating %s", fwupd_remote_get_id(remote));
	return fu_engine_update_metadata_bytes(priv->engine,
					       fwupd_remote_get_id(remote),
					       bytes_raw,
					       bytes_sig,
					       error);
}

static gboolean
fu_util_refresh(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) remotes = NULL;

	/* load engine */
	if (!fu_util_start_engine(priv,
				  FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_REMOTES,
				  priv->progress,
				  error))
		return FALSE;

	/* download new metadata */
	remotes = fu_engine_get_remotes(priv->engine, error);
	if (remotes == NULL)
		return FALSE;
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index(remotes, i);
		if (!fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_ENABLED))
			continue;
		if (fwupd_remote_get_kind(remote) != FWUPD_REMOTE_KIND_DOWNLOAD)
			continue;
		if (!fu_util_refresh_remote(priv, remote, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_util_get_remotes(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GNode) root = g_node_new(NULL);
	g_autoptr(GPtrArray) remotes = NULL;

	/* load engine */
	if (!fu_util_start_engine(priv, FU_ENGINE_LOAD_FLAG_REMOTES, priv->progress, error))
		return FALSE;

	/* list remotes */
	remotes = fu_engine_get_remotes(priv->engine, error);
	if (remotes == NULL)
		return FALSE;
	if (remotes->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "no remotes available");
		return FALSE;
	}
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote_tmp = g_ptr_array_index(remotes, i);
		g_node_append_data(root, remote_tmp);
	}
	fu_util_print_tree(priv->console, priv->client, root);

	return TRUE;
}

static gboolean
fu_util_security(FuUtilPrivate *priv, gchar **values, GError **error)
{
	FuSecurityAttrToStringFlags flags = FU_SECURITY_ATTR_TO_STRING_FLAG_NONE;
	g_autoptr(FuSecurityAttrs) attrs = NULL;
	g_autoptr(FuSecurityAttrs) events = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) items = NULL;
	g_autoptr(GPtrArray) events_array = NULL;
	g_autofree gchar *str = NULL;

#ifndef HAVE_HSI
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    /* TRANSLATORS: error message for unsupported feature */
		    _("Host Security ID (HSI) is not supported"));
	return FALSE;
#endif /* HAVE_HSI */

	if (!fu_util_start_engine(priv,
				  FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_REMOTES,
				  priv->progress,
				  error))
		return FALSE;

	/* show or hide different elements */
	if (priv->show_all) {
		flags |= FU_SECURITY_ATTR_TO_STRING_FLAG_SHOW_OBSOLETES;
		flags |= FU_SECURITY_ATTR_TO_STRING_FLAG_SHOW_URLS;
	}

	attrs = fu_engine_get_host_security_attrs(priv->engine);
	items = fu_security_attrs_get_all(attrs);

	/* print the "why" */
	if (priv->as_json) {
		str = fu_security_attrs_to_json_string(attrs, error);
		if (str == NULL)
			return FALSE;
		fu_console_print_literal(priv->console, str);
		return TRUE;
	}

	fu_console_print(priv->console,
			 "%s \033[1m%s\033[0m",
			 /* TRANSLATORS: this is a string like 'HSI:2-U' */
			 _("Host Security ID:"),
			 fu_engine_get_host_security_id(priv->engine));

	str = fu_util_security_attrs_to_string(items, flags);
	fu_console_print_literal(priv->console, str);

	/* print the "when" */
	events = fu_engine_get_host_security_events(priv->engine, 10, error);
	if (events == NULL)
		return FALSE;
	events_array = fu_security_attrs_get_all(attrs);
	if (events_array->len > 0) {
		g_autofree gchar *estr = fu_util_security_events_to_string(events_array, flags);
		if (estr != NULL)
			fu_console_print_literal(priv->console, estr);
	}

	/* print the "also" */
	devices = fu_engine_get_devices(priv->engine, error);
	if (devices == NULL)
		return FALSE;
	if (devices->len > 0) {
		g_autofree gchar *estr = fu_util_security_issues_to_string(devices);
		if (estr != NULL)
			fu_console_print_literal(priv->console, estr);
	}

	/* success */
	return TRUE;
}

static FuVolume *
fu_util_prompt_for_volume(FuUtilPrivate *priv, GError **error)
{
	FuContext *ctx = fu_engine_get_context(priv->engine);
	FuVolume *volume;
	guint idx;
	g_autoptr(GPtrArray) volumes = NULL;

	/* exactly one */
	volumes = fu_context_get_esp_volumes(ctx, error);
	if (volumes == NULL)
		return NULL;
	if (volumes->len == 1) {
		volume = g_ptr_array_index(volumes, 0);
		fu_console_print(priv->console,
				 "%s: %s",
				 /* TRANSLATORS: Volume has been chosen by the user */
				 _("Selected volume"),
				 fu_volume_get_id(volume));
		return g_object_ref(volume);
	}

	/* TRANSLATORS: this is to abort the interactive prompt */
	fu_console_print(priv->console, "0.\t%s", _("Cancel"));
	for (guint i = 0; i < volumes->len; i++) {
		volume = g_ptr_array_index(volumes, i);
		fu_console_print(priv->console, "%u.\t%s", i + 1, fu_volume_get_id(volume));
	}
	/* TRANSLATORS: get interactive prompt */
	idx = fu_console_input_uint(priv->console, volumes->len, "%s", _("Choose volume"));
	if (idx == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "Request canceled");
		return NULL;
	}
	volume = g_ptr_array_index(volumes, idx - 1);
	return g_object_ref(volume);
}

static gboolean
fu_util_esp_mount(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FuVolume) volume = NULL;
	volume = fu_util_prompt_for_volume(priv, error);
	if (volume == NULL)
		return FALSE;
	return fu_volume_mount(volume, error);
}

static gboolean
fu_util_esp_unmount(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FuVolume) volume = NULL;
	volume = fu_util_prompt_for_volume(priv, error);
	if (volume == NULL)
		return FALSE;
	return fu_volume_unmount(volume, error);
}

static gboolean
fu_util_esp_list(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autofree gchar *mount_point = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuVolume) volume = NULL;
	g_autoptr(GPtrArray) files = NULL;

	volume = fu_util_prompt_for_volume(priv, error);
	if (volume == NULL)
		return FALSE;
	locker = fu_volume_locker(volume, error);
	if (locker == NULL)
		return FALSE;
	mount_point = fu_volume_get_mount_point(volume);
	files = fu_path_get_files(mount_point, error);
	if (files == NULL)
		return FALSE;
	for (guint i = 0; i < files->len; i++) {
		const gchar *fn = g_ptr_array_index(files, i);
		fu_console_print_literal(priv->console, fn);
	}
	return TRUE;
}

static gboolean
_g_str_equal0(gconstpointer str1, gconstpointer str2)
{
	return g_strcmp0(str1, str2) == 0;
}

static gboolean
fu_util_switch_branch(FuUtilPrivate *priv, gchar **values, GError **error)
{
	const gchar *branch;
	g_autoptr(FwupdRelease) rel = NULL;
	g_autoptr(GPtrArray) rels = NULL;
	g_autoptr(GPtrArray) branches = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(FuDevice) dev = NULL;

	/* load engine */
	if (!fu_util_start_engine(priv,
				  FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_REMOTES,
				  priv->progress,
				  error))
		return FALSE;

	/* find the device and check it has multiple branches */
	priv->filter_device_include |= FWUPD_DEVICE_FLAG_HAS_MULTIPLE_BRANCHES;
	priv->filter_device_include |= FWUPD_DEVICE_FLAG_UPDATABLE;
	if (g_strv_length(values) == 1)
		dev = fu_util_get_device(priv, values[1], error);
	else
		dev = fu_util_prompt_for_device(priv, NULL, error);
	if (dev == NULL)
		return FALSE;
	if (!fu_device_has_flag(dev, FWUPD_DEVICE_FLAG_HAS_MULTIPLE_BRANCHES)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Multiple branches not available");
		return FALSE;
	}

	/* get all releases, including the alternate branch versions */
	rels = fu_engine_get_releases(priv->engine, priv->request, fu_device_get_id(dev), error);
	if (rels == NULL)
		return FALSE;

	/* get all the unique branches */
	for (guint i = 0; i < rels->len; i++) {
		FwupdRelease *rel_tmp = g_ptr_array_index(rels, i);
		const gchar *branch_tmp = fwupd_release_get_branch(rel_tmp);
		if (!fwupd_release_match_flags(rel_tmp,
					       priv->filter_release_include,
					       priv->filter_release_exclude))
			continue;
		if (g_ptr_array_find_with_equal_func(branches, branch_tmp, _g_str_equal0, NULL))
			continue;
		g_ptr_array_add(branches, g_strdup(branch_tmp));
	}

	/* branch name is optional */
	if (g_strv_length(values) > 1) {
		branch = values[1];
	} else if (branches->len == 1) {
		branch = g_ptr_array_index(branches, 0);
	} else {
		guint idx;

		/* TRANSLATORS: this is to abort the interactive prompt */
		fu_console_print(priv->console, "0.\t%s", _("Cancel"));
		for (guint i = 0; i < branches->len; i++) {
			const gchar *branch_tmp = g_ptr_array_index(branches, i);
			fu_console_print(priv->console,
					 "%u.\t%s",
					 i + 1,
					 fu_util_branch_for_display(branch_tmp));
		}
		/* TRANSLATORS: get interactive prompt, where branch is the
		 * supplier of the firmware, e.g. "non-free" or "free" */
		idx = fu_console_input_uint(priv->console, branches->len, "%s", _("Choose branch"));
		if (idx == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOTHING_TO_DO,
					    "Request canceled");
			return FALSE;
		}
		branch = g_ptr_array_index(branches, idx - 1);
	}

	/* sanity check */
	if (g_strcmp0(branch, fu_device_get_branch(dev)) == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Device %s is already on branch %s",
			    fu_device_get_name(dev),
			    fu_util_branch_for_display(branch));
		return FALSE;
	}

	/* the releases are ordered by version */
	for (guint j = 0; j < rels->len; j++) {
		FwupdRelease *rel_tmp = g_ptr_array_index(rels, j);
		if (g_strcmp0(fwupd_release_get_branch(rel_tmp), branch) == 0) {
			rel = g_object_ref(rel_tmp);
			break;
		}
	}
	if (rel == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "No releases for branch %s",
			    fu_util_branch_for_display(branch));
		return FALSE;
	}

	/* we're switching branch */
	if (!fu_util_switch_branch_warning(priv->console, FWUPD_DEVICE(dev), rel, FALSE, error))
		return FALSE;

	/* update the console if composite devices are also updated */
	priv->current_operation = FU_UTIL_OPERATION_INSTALL;
	g_signal_connect(FU_ENGINE(priv->engine),
			 "device-changed",
			 G_CALLBACK(fu_util_update_device_changed_cb),
			 priv);
	priv->flags |= FWUPD_INSTALL_FLAG_ALLOW_REINSTALL;
	priv->flags |= FWUPD_INSTALL_FLAG_ALLOW_BRANCH_SWITCH;
	if (!fu_util_install_release(priv, rel, error))
		return FALSE;
	fu_util_display_current_message(priv);

	/* we don't want to ask anything */
	if (priv->no_reboot_check) {
		g_debug("skipping reboot check");
		return TRUE;
	}

	return fu_util_prompt_complete(priv->console, priv->completion_flags, TRUE, error);
}

static gboolean
fu_util_set_bios_setting(FuUtilPrivate *priv, gchar **input, GError **error)
{
	g_autoptr(GHashTable) settings = fu_util_bios_settings_parse_argv(input, error);

	if (settings == NULL)
		return FALSE;

	if (!fu_util_start_engine(priv, FU_ENGINE_LOAD_FLAG_COLDPLUG, priv->progress, error))
		return FALSE;

	if (!fu_engine_modify_bios_settings(priv->engine, settings, FALSE, error)) {
		if (!g_error_matches(*error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO))
			g_prefix_error(error, "failed to set BIOS setting: ");
		return FALSE;
	}

	if (!priv->as_json) {
		gpointer key, value;
		GHashTableIter iter;

		g_hash_table_iter_init(&iter, settings);
		while (g_hash_table_iter_next(&iter, &key, &value)) {
			g_autofree gchar *msg =
			    /* TRANSLATORS: Configured a BIOS setting to a value */
			    g_strdup_printf(_("Set BIOS setting '%s' using '%s'."),
					    (const gchar *)key,
					    (const gchar *)value);
			fu_console_print_literal(priv->console, msg);
		}
	}
	priv->completion_flags |= FWUPD_DEVICE_FLAG_NEEDS_REBOOT;

	if (priv->no_reboot_check) {
		g_debug("skipping reboot check");
		return TRUE;
	}

	return fu_util_prompt_complete(priv->console, priv->completion_flags, TRUE, error);
}

static gboolean
fu_util_security_fix(FuUtilPrivate *priv, gchar **values, GError **error)
{
#ifndef HAVE_HSI
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    /* TRANSLATORS: error message for unsupported feature */
		    _("Host Security ID (HSI) is not supported"));
	return FALSE;
#endif /* HAVE_HSI */

	if (g_strv_length(values) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    /* TRANSLATOR: This is the error message for
				     * incorrect parameter */
				    _("Invalid arguments, expected an AppStream ID"));
		return FALSE;
	}

	if (!fu_util_start_engine(priv,
				  FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_REMOTES |
				      FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS,
				  priv->progress,
				  error))
		return FALSE;
	if (!fu_engine_fix_host_security_attr(priv->engine, values[0], error))
		return FALSE;
	/* TRANSLATORS: we've fixed a security problem on the machine */
	fu_console_print_literal(priv->console, _("Fixed successfully"));
	return TRUE;
}

static gboolean
fu_util_security_undo(FuUtilPrivate *priv, gchar **values, GError **error)
{
#ifndef HAVE_HSI
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    /* TRANSLATORS: error message for unsupported feature */
		    _("Host Security ID (HSI) is not supported"));
	return FALSE;
#endif /* HAVE_HSI */

	if (g_strv_length(values) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    /* TRANSLATOR: This is the error message for
				     * incorrect parameter */
				    _("Invalid arguments, expected an AppStream ID"));
		return FALSE;
	}

	if (!fu_util_start_engine(priv,
				  FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_REMOTES |
				      FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS,
				  priv->progress,
				  error))
		return FALSE;
	if (!fu_engine_undo_host_security_attr(priv->engine, values[0], error))
		return FALSE;
	/* TRANSLATORS: we've fixed a security problem on the machine */
	fu_console_print_literal(priv->console, _("Fix reverted successfully"));
	return TRUE;
}

static gboolean
fu_util_get_bios_setting(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FuBiosSettings) attrs = NULL;
	g_autoptr(GPtrArray) items = NULL;
	FuContext *ctx = fu_engine_get_context(priv->engine);
	gboolean found = FALSE;

	/* load engine */
	if (!fu_util_start_engine(priv, FU_ENGINE_LOAD_FLAG_COLDPLUG, priv->progress, error))
		return FALSE;

	attrs = fu_context_get_bios_settings(ctx);
	items = fu_bios_settings_get_all(attrs);
	if (priv->as_json)
		return fu_util_get_bios_setting_as_json(priv->console, values, items, error);

	for (guint i = 0; i < items->len; i++) {
		FwupdBiosSetting *attr = g_ptr_array_index(items, i);
		if (fu_util_bios_setting_matches_args(attr, values)) {
			g_autofree gchar *tmp = fu_util_bios_setting_to_string(attr, 0);
			fu_console_print_literal(priv->console, tmp);
			found = TRUE;
		}
	}
	if (items->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    /* TRANSLATORS: error message */
				    _("This system doesn't support firmware settings"));
		return FALSE;
	}
	if (!found) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_ARGS,
			    "%s: '%s'",
			    /* TRANSLATORS: error message */
			    _("Unable to find attribute"),
			    values[0]);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_util_reboot_cleanup(FuUtilPrivate *priv, gchar **values, GError **error)
{
	FuPlugin *plugin;
	g_autoptr(FuDevice) device = NULL;

	if (!fu_util_start_engine(priv, FU_ENGINE_LOAD_FLAG_COLDPLUG, priv->progress, error))
		return FALSE;

	/* both arguments are optional */
	if (g_strv_length(values) >= 1) {
		device = fu_engine_get_device(priv->engine, values[1], error);
		if (device == NULL)
			return FALSE;
	} else {
		device = fu_util_prompt_for_device(priv, NULL, error);
		if (device == NULL)
			return FALSE;
	}
	plugin = fu_engine_get_plugin_by_name(priv->engine, fu_device_get_plugin(device), error);
	if (plugin == NULL)
		return FALSE;
	return fu_plugin_runner_reboot_cleanup(plugin, device, error);
}

static gboolean
fu_util_efivar_list(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) names = NULL;

	/* sanity check */
	if (g_strv_length(values) < 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    /* TRANSLATORS: error message */
				    _("Invalid arguments, expected GUID"));
		return FALSE;
	}
	names = fu_efivar_get_names(values[0], error);
	if (names == NULL)
		return FALSE;
	for (guint i = 0; i < names->len; i++) {
		const gchar *name = g_ptr_array_index(names, i);
		fu_console_print(priv->console, "name: %s", name);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_util_build_cabinet(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GBytes) cab_blob = NULL;
	g_autoptr(FuCabinet) cab_file = fu_cabinet_new();

	/* sanity check */
	if (g_strv_length(values) < 3) {
		g_set_error_literal(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOTHING_TO_DO,
		    /* TRANSLATORS: error message */
		    _("Invalid arguments, expected at least ARCHIVE FIRMWARE METAINFO"));
		return FALSE;
	}

	/* file already exists */
	if ((priv->flags & FWUPD_INSTALL_FLAG_FORCE) == 0 &&
	    g_file_test(values[0], G_FILE_TEST_EXISTS)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Filename already exists");
		return FALSE;
	}

	/* add each file */
	for (guint i = 1; values[i] != NULL; i++) {
		g_autoptr(GBytes) blob = NULL;
		g_autofree gchar *basename = g_path_get_basename(values[i]);
		blob = fu_bytes_get_contents(values[i], error);
		if (blob == NULL)
			return FALSE;
		if (g_bytes_get_size(blob) == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "%s has zero size",
				    values[i]);
			return FALSE;
		}
		fu_cabinet_add_file(cab_file, basename, blob);
	}

	/* sanity check JCat and XML MetaInfo files */
	if (!fu_firmware_parse(FU_FIRMWARE(cab_file), NULL, FWUPD_INSTALL_FLAG_NONE, error))
		return FALSE;

	/* export */
	cab_blob = fu_firmware_write(FU_FIRMWARE(cab_file), error);
	if (cab_blob == NULL)
		return FALSE;
	return fu_bytes_set_contents(values[0], cab_blob, error);
}

static gboolean
fu_util_version(FuUtilPrivate *priv, GError **error)
{
	g_autoptr(GHashTable) metadata = NULL;
	g_autofree gchar *str = NULL;

	/* load engine */
	if (!fu_util_start_engine(priv,
				  FU_ENGINE_LOAD_FLAG_READONLY |
				      FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS,
				  priv->progress,
				  error))
		return FALSE;

	/* get metadata */
	metadata = fu_engine_get_report_metadata(priv->engine, error);
	if (metadata == NULL)
		return FALSE;

	/* dump to the screen in the most appropriate format */
	if (priv->as_json)
		return fu_util_project_versions_as_json(priv->console, metadata, error);
	str = fu_util_project_versions_to_string(metadata);
	fu_console_print_literal(priv->console, str);
	return TRUE;
}

static gboolean
fu_util_clear_history(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FuHistory) history = fu_history_new();
	return fu_history_remove_all(history, error);
}

static gboolean
fu_util_setup_interactive(FuUtilPrivate *priv, GError **error)
{
	if (priv->as_json) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "using --json");
		return FALSE;
	}
	return fu_console_setup(priv->console, error);
}

static void
fu_util_print_error(FuUtilPrivate *priv, const GError *error)
{
	if (priv->as_json) {
		fu_util_print_error_as_json(priv->console, error);
		return;
	}
	fu_console_print_full(priv->console, FU_CONSOLE_PRINT_FLAG_STDERR, "%s\n", error->message);
}

int
main(int argc, char *argv[])
{
	gboolean allow_branch_switch = FALSE;
	gboolean allow_older = FALSE;
	gboolean allow_reinstall = FALSE;
	gboolean force = FALSE;
	gboolean no_search = FALSE;
	gboolean ret;
	gboolean version = FALSE;
	gboolean ignore_checksum = FALSE;
	gboolean ignore_vid_pid = FALSE;
	g_auto(GStrv) plugin_glob = NULL;
	g_autoptr(FuUtilPrivate) priv = g_new0(FuUtilPrivate, 1);
	g_autoptr(GError) error_console = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) cmd_array = fu_util_cmd_array_new();
	g_autofree gchar *cmd_descriptions = NULL;
	g_autofree gchar *filter_device = NULL;
	g_autofree gchar *filter_release = NULL;
	const GOptionEntry options[] = {
	    {"version",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &version,
	     /* TRANSLATORS: command line option */
	     N_("Show client and daemon versions"),
	     NULL},
	    {"allow-reinstall",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &allow_reinstall,
	     /* TRANSLATORS: command line option */
	     N_("Allow reinstalling existing firmware versions"),
	     NULL},
	    {"allow-older",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &allow_older,
	     /* TRANSLATORS: command line option */
	     N_("Allow downgrading firmware versions"),
	     NULL},
	    {"allow-branch-switch",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &allow_branch_switch,
	     /* TRANSLATORS: command line option */
	     N_("Allow switching firmware branch"),
	     NULL},
	    {"force",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &force,
	     /* TRANSLATORS: command line option */
	     N_("Force the action by relaxing some runtime checks"),
	     NULL},
	    {"ignore-checksum",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &ignore_checksum,
	     /* TRANSLATORS: command line option */
	     N_("Ignore firmware checksum failures"),
	     NULL},
	    {"ignore-vid-pid",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &ignore_vid_pid,
	     /* TRANSLATORS: command line option */
	     N_("Ignore firmware hardware mismatch failures"),
	     NULL},
	    {"no-reboot-check",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &priv->no_reboot_check,
	     /* TRANSLATORS: command line option */
	     N_("Do not check or prompt for reboot after update"),
	     NULL},
	    {"no-search",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &no_search,
	     /* TRANSLATORS: command line option */
	     N_("Do not search the firmware when parsing"),
	     NULL},
	    {"no-safety-check",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &priv->no_safety_check,
	     /* TRANSLATORS: command line option */
	     N_("Do not perform device safety checks"),
	     NULL},
	    {"no-device-prompt",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &priv->no_device_prompt,
	     /* TRANSLATORS: command line option */
	     N_("Do not prompt for devices"),
	     NULL},
	    {"show-all",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &priv->show_all,
	     /* TRANSLATORS: command line option */
	     N_("Show all results"),
	     NULL},
	    {"show-all-devices",
	     '\0',
	     G_OPTION_FLAG_HIDDEN,
	     G_OPTION_ARG_NONE,
	     &priv->show_all,
	     /* TRANSLATORS: command line option */
	     N_("Show devices that are not updatable"),
	     NULL},
	    {"plugins",
	     '\0',
	     0,
	     G_OPTION_ARG_STRING_ARRAY,
	     &plugin_glob,
	     /* TRANSLATORS: command line option */
	     N_("Manually enable specific plugins"),
	     NULL},
	    {"plugin-whitelist",
	     '\0',
	     G_OPTION_FLAG_HIDDEN,
	     G_OPTION_ARG_STRING_ARRAY,
	     &plugin_glob,
	     /* TRANSLATORS: command line option */
	     N_("Manually enable specific plugins"),
	     NULL},
	    {"prepare",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &priv->prepare_blob,
	     /* TRANSLATORS: command line option */
	     N_("Run the plugin composite prepare routine when using install-blob"),
	     NULL},
	    {"cleanup",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &priv->cleanup_blob,
	     /* TRANSLATORS: command line option */
	     N_("Run the plugin composite cleanup routine when using install-blob"),
	     NULL},
	    {"disable-ssl-strict",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &priv->disable_ssl_strict,
	     /* TRANSLATORS: command line option */
	     N_("Ignore SSL strict checks when downloading files"),
	     NULL},
	    {"filter",
	     '\0',
	     0,
	     G_OPTION_ARG_STRING,
	     &filter_device,
	     /* TRANSLATORS: command line option */
	     N_("Filter with a set of device flags using a ~ prefix to "
		"exclude, e.g. 'internal,~needs-reboot'"),
	     NULL},
	    {"filter-release",
	     '\0',
	     0,
	     G_OPTION_ARG_STRING,
	     &filter_release,
	     /* TRANSLATORS: command line option */
	     N_("Filter with a set of release flags using a ~ prefix to "
		"exclude, e.g. 'trusted-release,~trusted-metadata'"),
	     NULL},
	    {"json",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &priv->as_json,
	     /* TRANSLATORS: command line option */
	     N_("Output in JSON format"),
	     NULL},
	    {NULL}};

#ifdef _WIN32
	/* workaround Windows setting the codepage to 1252 */
	(void)g_setenv("LANG", "C.UTF-8", FALSE);
#endif

	setlocale(LC_ALL, "");

	bindtextdomain(GETTEXT_PACKAGE, FWUPD_LOCALEDIR);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
	textdomain(GETTEXT_PACKAGE);

	/* create helper object */
	priv->lock_fd = -1;
	priv->main_ctx = g_main_context_new();
	priv->loop = g_main_loop_new(priv->main_ctx, FALSE);
	priv->console = fu_console_new();
	priv->post_requests = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	fu_console_set_main_context(priv->console, priv->main_ctx);
	priv->request = fu_engine_request_new();

	/* used for monitoring and downloading */
	priv->client = fwupd_client_new();
	fwupd_client_set_main_context(priv->client, priv->main_ctx);
	fwupd_client_set_daemon_version(priv->client, PACKAGE_VERSION);
	fwupd_client_set_user_agent_for_package(priv->client, "fwupdtool", PACKAGE_VERSION);
	g_signal_connect(FWUPD_CLIENT(priv->client),
			 "notify::percentage",
			 G_CALLBACK(fu_util_client_notify_cb),
			 priv);
	g_signal_connect(FWUPD_CLIENT(priv->client),
			 "notify::status",
			 G_CALLBACK(fu_util_client_notify_cb),
			 priv);

	/* when not using the engine */
	priv->progress = fu_progress_new(G_STRLOC);
	g_signal_connect(priv->progress,
			 "percentage-changed",
			 G_CALLBACK(fu_util_progress_percentage_changed_cb),
			 priv);
	g_signal_connect(priv->progress,
			 "status-changed",
			 G_CALLBACK(fu_util_progress_status_changed_cb),
			 priv);

	/* add commands */
	fu_util_cmd_array_add(cmd_array,
			      "smbios-dump",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("FILE"),
			      /* TRANSLATORS: command description */
			      _("Dump SMBIOS data from a file"),
			      fu_util_smbios_dump);
	fu_util_cmd_array_add(cmd_array,
			      "get-plugins",
			      NULL,
			      /* TRANSLATORS: command description */
			      _("Get all enabled plugins registered with the system"),
			      fu_util_get_plugins);
	fu_util_cmd_array_add(cmd_array,
			      "get-details",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("FILE"),
			      /* TRANSLATORS: command description */
			      _("Gets details about a firmware file"),
			      fu_util_get_details);
	fu_util_cmd_array_add(cmd_array,
			      "get-history",
			      NULL,
			      /* TRANSLATORS: command description */
			      _("Show history of firmware updates"),
			      fu_util_get_history);
	fu_util_cmd_array_add(cmd_array,
			      "get-updates,get-upgrades",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("[DEVICE-ID|GUID]"),
			      /* TRANSLATORS: command description */
			      _("Gets the list of updates for connected hardware"),
			      fu_util_get_updates);
	fu_util_cmd_array_add(cmd_array,
			      "get-devices,get-topology",
			      NULL,
			      /* TRANSLATORS: command description */
			      _("Get all devices that support firmware updates"),
			      fu_util_get_devices);
	fu_util_cmd_array_add(cmd_array,
			      "get-device-flags",
			      NULL,
			      /* TRANSLATORS: command description */
			      _("Get all device flags supported by fwupd"),
			      fu_util_get_device_flags);
	fu_util_cmd_array_add(cmd_array,
			      "watch",
			      NULL,
			      /* TRANSLATORS: command description */
			      _("Watch for hardware changes"),
			      fu_util_watch);
	fu_util_cmd_array_add(cmd_array,
			      "install-blob",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("FILENAME DEVICE-ID"),
			      /* TRANSLATORS: command description */
			      _("Install a raw firmware blob on a device"),
			      fu_util_install_blob);
	fu_util_cmd_array_add(cmd_array,
			      "install",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("FILE [DEVICE-ID|GUID]"),
			      /* TRANSLATORS: command description */
			      _("Install a specific firmware on a device, all possible devices"
				" will also be installed once the CAB matches"),
			      fu_util_install);
	fu_util_cmd_array_add(cmd_array,
			      "reinstall",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("DEVICE-ID|GUID"),
			      /* TRANSLATORS: command description */
			      _("Reinstall firmware on a device"),
			      fu_util_reinstall);
	fu_util_cmd_array_add(cmd_array,
			      "attach",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("DEVICE-ID|GUID"),
			      /* TRANSLATORS: command description */
			      _("Attach to firmware mode"),
			      fu_util_attach);
	fu_util_cmd_array_add(cmd_array,
			      "get-report-metadata",
			      NULL,
			      /* TRANSLATORS: command description */
			      _("Get device report metadata"),
			      fu_util_get_report_metadata);
	fu_util_cmd_array_add(cmd_array,
			      "detach",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("DEVICE-ID|GUID"),
			      /* TRANSLATORS: command description */
			      _("Detach to bootloader mode"),
			      fu_util_detach);
	fu_util_cmd_array_add(cmd_array,
			      "unbind-driver",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("[DEVICE-ID|GUID]"),
			      /* TRANSLATORS: command description */
			      _("Unbind current driver"),
			      fu_util_unbind_driver);
	fu_util_cmd_array_add(cmd_array,
			      "bind-driver",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("SUBSYSTEM DRIVER [DEVICE-ID|GUID]"),
			      /* TRANSLATORS: command description */
			      _("Bind new kernel driver"),
			      fu_util_bind_driver);
	fu_util_cmd_array_add(cmd_array,
			      "activate",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("[DEVICE-ID|GUID]"),
			      /* TRANSLATORS: command description */
			      _("Activate pending devices"),
			      fu_util_activate);
	fu_util_cmd_array_add(cmd_array,
			      "hwids",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("[SMBIOS-FILE|HWIDS-FILE]"),
			      /* TRANSLATORS: command description */
			      _("Return all the hardware IDs for the machine"),
			      fu_util_hwids);
	fu_util_cmd_array_add(cmd_array,
			      "export-hwids",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("HWIDS-FILE"),
			      /* TRANSLATORS: command description */
			      _("Save a file that allows generation of hardware IDs"),
			      fu_util_export_hwids);
	fu_util_cmd_array_add(cmd_array,
			      "monitor",
			      NULL,
			      /* TRANSLATORS: command description */
			      _("Monitor the daemon for events"),
			      fu_util_monitor);
	fu_util_cmd_array_add(cmd_array,
			      "update,upgrade",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("[DEVICE-ID|GUID]"),
			      /* TRANSLATORS: command description */
			      _("Updates all specified devices to latest firmware version, or all "
				"devices if unspecified"),
			      fu_util_update);
	fu_util_cmd_array_add(cmd_array,
			      "self-sign",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("TEXT"),
			      /* TRANSLATORS: command description */
			      C_("command-description", "Sign data using the client certificate"),
			      fu_util_self_sign);
	fu_util_cmd_array_add(cmd_array,
			      "verify-update",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("[DEVICE-ID|GUID]"),
			      /* TRANSLATORS: command description */
			      _("Update the stored metadata with current contents"),
			      fu_util_verify_update);
	fu_util_cmd_array_add(cmd_array,
			      "firmware-sign",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("FILENAME CERTIFICATE PRIVATE-KEY"),
			      /* TRANSLATORS: command description */
			      _("Sign a firmware with a new key"),
			      fu_util_firmware_sign);
	fu_util_cmd_array_add(cmd_array,
			      "firmware-dump",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("FILENAME [DEVICE-ID|GUID]"),
			      /* TRANSLATORS: command description */
			      _("Read a firmware blob from a device"),
			      fu_util_firmware_dump);
	fu_util_cmd_array_add(cmd_array,
			      "firmware-read",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("FILENAME [DEVICE-ID|GUID]"),
			      /* TRANSLATORS: command description */
			      _("Read a firmware from a device"),
			      fu_util_firmware_read);
	fu_util_cmd_array_add(cmd_array,
			      "firmware-patch",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("FILENAME OFFSET DATA [FIRMWARE-TYPE]"),
			      /* TRANSLATORS: command description */
			      _("Patch a firmware blob at a known offset"),
			      fu_util_firmware_patch);
	fu_util_cmd_array_add(
	    cmd_array,
	    "firmware-convert",
	    /* TRANSLATORS: command argument: uppercase, spaces->dashes */
	    _("FILENAME-SRC FILENAME-DST [FIRMWARE-TYPE-SRC] [FIRMWARE-TYPE-DST]"),
	    /* TRANSLATORS: command description */
	    _("Convert a firmware file"),
	    fu_util_firmware_convert);
	fu_util_cmd_array_add(cmd_array,
			      "firmware-build",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("BUILDER-XML FILENAME-DST"),
			      /* TRANSLATORS: command description */
			      _("Build a firmware file"),
			      fu_util_firmware_build);
	fu_util_cmd_array_add(cmd_array,
			      "firmware-parse",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("FILENAME [FIRMWARE-TYPE]"),
			      /* TRANSLATORS: command description */
			      _("Parse and show details about a firmware file"),
			      fu_util_firmware_parse);
	fu_util_cmd_array_add(cmd_array,
			      "firmware-export",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("FILENAME [FIRMWARE-TYPE]"),
			      /* TRANSLATORS: command description */
			      _("Export a firmware file structure to XML"),
			      fu_util_firmware_export);
	fu_util_cmd_array_add(cmd_array,
			      "firmware-extract",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("FILENAME [FIRMWARE-TYPE]"),
			      /* TRANSLATORS: command description */
			      _("Extract a firmware blob to images"),
			      fu_util_firmware_extract);
	fu_util_cmd_array_add(cmd_array,
			      "get-firmware-types",
			      NULL,
			      /* TRANSLATORS: command description */
			      _("List the available firmware types"),
			      fu_util_get_firmware_types);
	fu_util_cmd_array_add(cmd_array,
			      "get-firmware-gtypes",
			      NULL,
			      /* TRANSLATORS: command description */
			      _("List the available firmware GTypes"),
			      fu_util_get_firmware_gtypes);
	fu_util_cmd_array_add(cmd_array,
			      "get-remotes",
			      NULL,
			      /* TRANSLATORS: command description */
			      _("Gets the configured remotes"),
			      fu_util_get_remotes);
	fu_util_cmd_array_add(cmd_array,
			      "refresh",
			      NULL,
			      /* TRANSLATORS: command description */
			      _("Refresh metadata from remote server"),
			      fu_util_refresh);
	fu_util_cmd_array_add(cmd_array,
			      "security",
			      NULL,
			      /* TRANSLATORS: command description */
			      _("Gets the host security attributes"),
			      fu_util_security);
	fu_util_cmd_array_add(cmd_array,
			      "esp-mount",
			      NULL,
			      /* TRANSLATORS: command description */
			      _("Mounts the ESP"),
			      fu_util_esp_mount);
	fu_util_cmd_array_add(cmd_array,
			      "esp-unmount",
			      NULL,
			      /* TRANSLATORS: command description */
			      _("Unmounts the ESP"),
			      fu_util_esp_unmount);
	fu_util_cmd_array_add(cmd_array,
			      "esp-list",
			      NULL,
			      /* TRANSLATORS: command description */
			      _("Lists files on the ESP"),
			      fu_util_esp_list);
	fu_util_cmd_array_add(cmd_array,
			      "switch-branch",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("[DEVICE-ID|GUID] [BRANCH]"),
			      /* TRANSLATORS: command description */
			      _("Switch the firmware branch on the device"),
			      fu_util_switch_branch);
	fu_util_cmd_array_add(cmd_array,
			      "clear-history",
			      NULL,
			      /* TRANSLATORS: command description */
			      _("Erase all firmware update history"),
			      fu_util_clear_history);
	fu_util_cmd_array_add(
	    cmd_array,
	    "get-bios-settings,get-bios-setting",
	    /* TRANSLATORS: command argument: uppercase, spaces->dashes */
	    _("[SETTING1] [ SETTING2]..."),
	    /* TRANSLATORS: command description */
	    _("Retrieve BIOS settings.  If no arguments are passed all settings are returned"),
	    fu_util_get_bios_setting);
	fu_util_cmd_array_add(cmd_array,
			      "set-bios-setting",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("SETTING VALUE"),
			      /* TRANSLATORS: command description */
			      _("Set a BIOS setting"),
			      fu_util_set_bios_setting);
	fu_util_cmd_array_add(cmd_array,
			      "build-cabinet",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("ARCHIVE FIRMWARE METAINFO [FIRMWARE] [METAINFO] [JCATFILE]"),
			      /* TRANSLATORS: command description */
			      _("Build a cabinet archive from a firmware blob and XML metadata"),
			      fu_util_build_cabinet);
	fu_util_cmd_array_add(cmd_array,
			      "efivar-list",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      C_("command-argument", "GUID"),
			      /* TRANSLATORS: command description */
			      _("List EFI variables with a specific GUID"),
			      fu_util_efivar_list);
	fu_util_cmd_array_add(cmd_array,
			      "security-fix",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("[APPSTREAM_ID]"),
			      /* TRANSLATORS: command description */
			      _("Fix a specific host security attribute"),
			      fu_util_security_fix);
	fu_util_cmd_array_add(cmd_array,
			      "security-undo",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("[APPSTREAM_ID]"),
			      /* TRANSLATORS: command description */
			      _("Undo the host security attribute fix"),
			      fu_util_security_undo);
	fu_util_cmd_array_add(cmd_array,
			      "reboot-cleanup",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("[DEVICE]"),
			      /* TRANSLATORS: command description */
			      _("Run the post-reboot cleanup action"),
			      fu_util_reboot_cleanup);

	/* do stuff on ctrl+c */
	priv->cancellable = g_cancellable_new();
	fu_util_setup_signal_handlers(priv);
	g_signal_connect(G_CANCELLABLE(priv->cancellable),
			 "cancelled",
			 G_CALLBACK(fu_util_cancelled_cb),
			 priv);

	/* sort by command name */
	fu_util_cmd_array_sort(cmd_array);

	/* non-TTY consoles cannot answer questions */
	if (!fu_util_setup_interactive(priv, &error_console)) {
		g_info("failed to initialize interactive console: %s", error_console->message);
		priv->no_reboot_check = TRUE;
		priv->no_safety_check = TRUE;
		priv->no_device_prompt = TRUE;
	} else {
		priv->interactive = TRUE;
		/* set our implemented feature set */
		fu_engine_request_set_feature_flags(
		    priv->request,
		    FWUPD_FEATURE_FLAG_DETACH_ACTION | FWUPD_FEATURE_FLAG_SWITCH_BRANCH |
			FWUPD_FEATURE_FLAG_FDE_WARNING | FWUPD_FEATURE_FLAG_UPDATE_ACTION |
			FWUPD_FEATURE_FLAG_COMMUNITY_TEXT | FWUPD_FEATURE_FLAG_SHOW_PROBLEMS |
			FWUPD_FEATURE_FLAG_REQUESTS | FWUPD_FEATURE_FLAG_REQUESTS_NON_GENERIC);
	}
	fu_console_set_interactive(priv->console, priv->interactive);

	/* get a list of the commands */
	priv->context = g_option_context_new(NULL);
	cmd_descriptions = fu_util_cmd_array_to_string(cmd_array);
	g_option_context_set_summary(priv->context, cmd_descriptions);
	g_option_context_set_description(
	    priv->context,
	    /* TRANSLATORS: CLI description */
	    _("This tool allows an administrator to use the fwupd plugins "
	      "without being installed on the host system."));

	/* TRANSLATORS: program name */
	g_set_application_name(_("Firmware Utility"));
	g_option_context_add_main_entries(priv->context, options, NULL);
	g_option_context_add_group(priv->context, fu_debug_get_option_group());
	ret = g_option_context_parse(priv->context, &argc, &argv, &error);
	if (!ret) {
		fu_console_print(priv->console,
				 "%s: %s",
				 /* TRANSLATORS: the user didn't read the man page */
				 _("Failed to parse arguments"),
				 error->message);
		return EXIT_FAILURE;
	}
	fu_progress_set_profile(priv->progress, g_getenv("FWUPD_VERBOSE") != NULL);

	/* allow disabling SSL strict mode for broken corporate proxies */
	if (priv->disable_ssl_strict) {
		fu_console_print_full(priv->console,
				      FU_CONSOLE_PRINT_FLAG_WARNING,
				      "%s\n",
				      /* TRANSLATORS: try to help */
				      _("Ignoring SSL strict checks, "
					"to do this automatically in the future "
					"export DISABLE_SSL_STRICT in your environment"));
		(void)g_setenv("DISABLE_SSL_STRICT", "1", TRUE);
	}

	/* parse filter flags */
	if (filter_device != NULL) {
		if (!fu_util_parse_filter_device_flags(filter_device,
						       &priv->filter_device_include,
						       &priv->filter_device_exclude,
						       &error)) {
			/* TRANSLATORS: the user didn't read the man page */
			g_prefix_error(&error, "%s: ", _("Failed to parse flags for --filter"));
			fu_util_print_error(priv, error);
			return EXIT_FAILURE;
		}
	}
	if (filter_release != NULL) {
		if (!fu_util_parse_filter_release_flags(filter_release,
							&priv->filter_release_include,
							&priv->filter_release_exclude,
							&error)) {
			/* TRANSLATORS: the user didn't read the man page */
			g_prefix_error(&error,
				       "%s: ",
				       _("Failed to parse flags for --filter-release"));
			fu_util_print_error(priv, error);
			return EXIT_FAILURE;
		}
	}

	/* set flags */
	if (allow_reinstall)
		priv->flags |= FWUPD_INSTALL_FLAG_ALLOW_REINSTALL;
	if (allow_older)
		priv->flags |= FWUPD_INSTALL_FLAG_ALLOW_OLDER;
	if (allow_branch_switch)
		priv->flags |= FWUPD_INSTALL_FLAG_ALLOW_BRANCH_SWITCH;
	if (force)
		priv->flags |= FWUPD_INSTALL_FLAG_FORCE;
	if (no_search)
		priv->flags |= FWUPD_INSTALL_FLAG_NO_SEARCH;
	if (ignore_checksum)
		priv->flags |= FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM;
	if (ignore_vid_pid)
		priv->flags |= FWUPD_INSTALL_FLAG_IGNORE_VID_PID;

	/* load engine */
	priv->engine = fu_engine_new();
	g_signal_connect(FU_ENGINE(priv->engine),
			 "device-request",
			 G_CALLBACK(fu_util_update_device_request_cb),
			 priv);
	g_signal_connect(FU_ENGINE(priv->engine),
			 "device-added",
			 G_CALLBACK(fu_main_engine_device_added_cb),
			 priv);
	g_signal_connect(FU_ENGINE(priv->engine),
			 "device-removed",
			 G_CALLBACK(fu_main_engine_device_removed_cb),
			 priv);
	g_signal_connect(FU_ENGINE(priv->engine),
			 "status-changed",
			 G_CALLBACK(fu_main_engine_status_changed_cb),
			 priv);

	/* just show versions and exit */
	if (version) {
		if (!fu_util_version(priv, &error)) {
			fu_util_print_error(priv, error);
			return EXIT_FAILURE;
		}
		return EXIT_SUCCESS;
	}

	/* any plugin allowlist specified */
	for (guint i = 0; plugin_glob != NULL && plugin_glob[i] != NULL; i++)
		fu_engine_add_plugin_filter(priv->engine, plugin_glob[i]);

	/* run the specified command */
	ret = fu_util_cmd_array_run(cmd_array, priv, argv[1], (gchar **)&argv[2], &error);
	if (!ret) {
#ifdef SUPPORTED_BUILD
		/* sanity check */
		if (error == NULL) {
			g_critical("exec failed but no error set!");
			return EXIT_FAILURE;
		}
#endif
		fu_util_print_error(priv, error);
		if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_ARGS)) {
			fu_console_print_literal(priv->console,
						 /* TRANSLATORS: explain how to get help */
						 _("Use fwupdtool --help for help"));
		} else if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO)) {
			g_info("%s\n", error->message);
			return EXIT_NOTHING_TO_DO;
		} else if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_info("%s\n", error->message);
			return EXIT_NOT_FOUND;
		}
#ifdef HAVE_GETUID
		/* if not root, then notify users on the error path */
		if (priv->interactive && (getuid() != 0 || geteuid() != 0)) {
			fu_console_print_full(priv->console,
					      FU_CONSOLE_PRINT_FLAG_STDERR |
						  FU_CONSOLE_PRINT_FLAG_WARNING,
					      "%s\n",
					      /* TRANSLATORS: we're poking around as a power user */
					      _("This program may only work correctly as root"));
		}
#endif
		return EXIT_FAILURE;
	}

	/* a good place to do the traceback */
	if (fu_progress_get_profile(priv->progress)) {
		g_autofree gchar *str = fu_progress_traceback(priv->progress);
		if (str != NULL)
			fu_console_print_literal(priv->console, str);
	}

	/* success */
	return EXIT_SUCCESS;
}
