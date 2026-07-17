/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 * Copyright 2025 Colin Kinloch <colin.kinloch@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuMain"

#include "config.h"

#include <android/binder_ibinder.h>
#include <android/binder_manager.h>
#include <android/binder_parcel.h>
#include <android/binder_process.h>
#include <android/binder_status.h>
#include <gio/gunixinputstream.h>
#include <glib/gi18n.h>
#include <stdlib.h>

#include "fwupd-common-private.h"
#include "fwupd-error.h"

#include "fu-binder-client-bridge.h"
#include "fu-debug.h"
#include "fu-util-common.h"

/* Forward declare the C++ remotes function (ensure it matches your .h file) */
GVariant *
fu_binder_client_get_remotes_aidl(AIBinder *binder, GError **error);

/* Custom return codes */
#define EXIT_NOTHING_TO_DO 2
#define EXIT_NOT_FOUND	   3

/* Private addition to binder_flag_t for vendor-to-vendor stability */
#define FLAG_PRIVATE_VENDOR 0x10000000

typedef enum {
	FU_UTIL_OPERATION_UNKNOWN,
	FU_UTIL_OPERATION_UPDATE,
	FU_UTIL_OPERATION_DOWNGRADE,
	FU_UTIL_OPERATION_INSTALL,
	FU_UTIL_OPERATION_LAST
} FuUtilOperation;

struct FuUtil {
	GCancellable *cancellable;
	GMainContext *main_ctx;
	GMainLoop *loop;
	GOptionContext *context;
	AIBinder *fwupd_binder;
	gint binder_fd;
	FwupdInstallFlags flags;
	FwupdClientDownloadFlags download_flags;
	FwupdClient *client;
	FuConsole *console;
	gboolean no_remote_check;
	gboolean no_metadata_check;
	gboolean no_reboot_check;
	gboolean no_unreported_check;
	gboolean no_safety_check;
	gboolean no_device_prompt;
	gboolean no_emulation_check;
	gboolean no_security_fix;
	gboolean assume_yes;
	gboolean sign;
	gboolean show_all;
	gboolean disable_ssl_strict;
	gboolean as_json;
	FuUtilOperation current_operation;
	FwupdDevice *current_device;
	GPtrArray *post_requests;
	FwupdDeviceFlags completion_flags;
	FwupdDeviceFlags filter_device_include;
	FwupdDeviceFlags filter_device_exclude;
	FwupdReleaseFlags filter_release_include;
	FwupdReleaseFlags filter_release_exclude;
};

typedef struct FuUtil FuUtil;

static void
fu_util_private_free(FuUtil *self)
{
	if (self->fwupd_binder != NULL)
		AIBinder_decStrong(self->fwupd_binder);
	if (self->client != NULL)
		g_object_unref(self->client);
	if (self->current_device != NULL)
		g_object_unref(self->current_device);
	if (self->post_requests != NULL)
		g_ptr_array_unref(self->post_requests);
	if (self->loop != NULL)
		g_main_loop_unref(self->loop);
	if (self->main_ctx != NULL)
		g_main_context_unref(self->main_ctx);
	if (self->console != NULL)
		g_object_unref(self->console);
	g_free(self);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuUtil, fu_util_private_free)

static void
fu_util_print_error(FuUtil *self, const GError *error)
{
	if (self->as_json) {
		fu_util_print_error_as_json(self->console, error);
		return;
	}
	fu_console_print_full(self->console, FU_CONSOLE_PRINT_FLAG_STDERR, "%s\n", error->message);
}

/* --- Common Core AIDL Calls --- */

static GPtrArray *
fu_util_get_remotes_call(FuUtil *self, GError **error)
{
	GVariant *val = NULL;
	GVariant *tuple = NULL;
	g_autoptr(GPtrArray) array = NULL;

	val = fu_binder_client_get_remotes_aidl(self->fwupd_binder, error);
	if (val == NULL)
		return NULL;

	tuple = g_variant_ref_sink(g_variant_new_tuple(&val, 1));
	array = fwupd_codec_array_from_variant(tuple, FWUPD_TYPE_REMOTE, error);
	g_variant_unref(tuple);

	if (array == NULL)
		return NULL;

	return g_steal_pointer(&array);
}

static GPtrArray *
fu_util_get_upgrades_call(FuUtil *self, const gchar *device_id, GError **error)
{
	GVariant *val = NULL;
	GVariant *tuple = NULL;
	g_autoptr(GPtrArray) array = NULL;

	val = fu_binder_client_get_upgrades_aidl(self->fwupd_binder, device_id, error);
	if (val == NULL)
		return NULL;

	tuple = g_variant_ref_sink(g_variant_new_tuple(&val, 1));
	array = fwupd_codec_array_from_variant(tuple, FWUPD_TYPE_RELEASE, error);
	g_variant_unref(tuple);

	if (array == NULL)
		return NULL;

	fwupd_device_array_ensure_parents(array);
	return g_steal_pointer(&array);
}

static GPtrArray *
fu_util_get_devices_call(FuUtil *self, GError **error)
{
	GVariant *val = NULL;
	GVariant *tuple = NULL;
	g_autoptr(GPtrArray) devs = NULL;

	val = fu_binder_client_get_devices_aidl(self->fwupd_binder, error);
	if (val == NULL)
		return NULL;

	tuple = g_variant_ref_sink(g_variant_new_tuple(&val, 1));
	devs = fwupd_codec_array_from_variant(tuple, FWUPD_TYPE_DEVICE, error);
	g_variant_unref(tuple);

	if (devs == NULL)
		return NULL;

	fwupd_device_array_ensure_parents(devs);

	return g_steal_pointer(&devs);
}

static gboolean
fu_util_get_hwids_call(FuUtil *self, GStrv *keys, GStrv *values, GError **error)
{
	g_autoptr(GVariant) val = fu_binder_client_get_hwids_aidl(self->fwupd_binder, error);
	g_autoptr(GVariant) val_hwids = NULL;
	g_autoptr(GVariantIter) iter = NULL;
	const gchar *hwid_key;
	GVariant *hwid_value_v;
	guint size;

	val = fu_binder_client_get_hwids_aidl(self->fwupd_binder, error);
	if (val == NULL)
		return FALSE;

	if (g_variant_is_of_type(val, G_VARIANT_TYPE_TUPLE)) {
		val_hwids = g_variant_get_child_value(val, 0);
	} else {
		val_hwids = g_variant_ref(val);
	}

	size = g_variant_n_children(val_hwids);
	*keys = g_new0(gchar *, size + 1);
	*values = g_new0(gchar *, size + 1);
	g_variant_get(val_hwids, "a{sv}", &iter);

	for (guint i = 0; g_variant_iter_next(iter, "{&sv}", &hwid_key, &hwid_value_v); i++) {
		(*keys)[i] = g_strdup(hwid_key);
		(*values)[i] = g_variant_dup_string(hwid_value_v, NULL);
		g_variant_unref(hwid_value_v);
	}

	return TRUE;
}

static void
fu_util_build_device_tree(FuUtil *self, FuUtilNode *root, GPtrArray *devs, FwupdDevice *dev)
{
	for (guint i = 0; i < devs->len; i++) {
		FwupdDevice *dev_tmp = g_ptr_array_index(devs, i);
		if (!fwupd_device_match_flags(dev_tmp,
					      self->filter_device_include,
					      self->filter_device_exclude))
			continue;
		if (!self->show_all && !fu_util_is_interesting_device(devs, dev_tmp))
			continue;
		if (fwupd_device_get_parent(dev_tmp) == dev) {
			FuUtilNode *child = g_node_append_data(root, g_object_ref(dev_tmp));
			fu_util_build_device_tree(self, child, devs, dev_tmp);
		}
	}
}

static gboolean
fu_util_get_devices_as_json(FuUtil *self, GPtrArray *devs, GError **error)
{
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();

	for (guint i = 0; i < devs->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devs, i);
		g_autoptr(GPtrArray) rels = NULL;
		g_autoptr(GError) error_local = NULL;
		g_autoptr(FwupdJsonObject) json_obj_tmp = fwupd_json_object_new();

		/* filter */
		if (!fwupd_device_match_flags(dev,
					      self->filter_device_include,
					      self->filter_device_exclude))
			continue;

		/* add all releases that could be applied */
		rels = fu_util_get_upgrades_call(self, fwupd_device_get_id(dev), &error_local);
		if (rels == NULL) {
			g_debug("not adding releases to device: %s",
				error_local ? error_local->message : "unknown error");
		} else {
			for (guint j = 0; j < rels->len; j++) {
				FwupdRelease *rel = g_ptr_array_index(rels, j);
				if (!fwupd_release_match_flags(rel,
							       self->filter_release_include,
							       self->filter_release_exclude))
					continue;
				fwupd_device_add_release(dev, rel);
			}
		}

		/* add to builder */
		fwupd_codec_to_json(FWUPD_CODEC(dev), json_obj_tmp, FWUPD_CODEC_FLAG_TRUSTED);
		fwupd_json_array_add_object(json_arr, json_obj_tmp);
	}
	fwupd_json_object_add_array(json_obj, "Devices", json_arr);
	fu_util_print_json_object(self->console, json_obj);
	return TRUE;
}

static gboolean
fu_util_get_devices(FuUtil *self, gchar **values, GError **error)
{
	g_autoptr(FuUtilNode) root = g_node_new(NULL);
	g_autoptr(GPtrArray) devs = fu_util_get_devices_call(self, error);

	if (devs == NULL)
		return FALSE;

	/* JSON Path */
	if (self->as_json)
		return fu_util_get_devices_as_json(self, devs, error);

	if (devs->len > 0)
		fu_util_build_device_tree(self, root, devs, NULL);

	if (g_node_n_children(root) == 0) {
		fu_console_print_literal(self->console,
					 /* TRANSLATORS: nothing attached that can be upgraded */
					 _("No hardware detected with firmware update capability"));
		return TRUE;
	}

	fu_util_print_node(self->console, self->client, root);

	return TRUE;
}

/* --- Upgrades Logic --- */

static gboolean
fu_util_get_upgrades_as_json(FuUtil *self, GPtrArray *devices, GError **error)
{
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();

	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devices, i);
		g_autoptr(GPtrArray) rels = NULL;
		g_autoptr(GError) error_local = NULL;

		/* not going to have results, so save a D-Bus round-trip */
		if (!fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE) &&
		    !fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN))
			continue;
		if (!fwupd_device_match_flags(dev,
					      self->filter_device_include,
					      self->filter_device_exclude))
			continue;

		/* get the releases for this device and filter for validity */
		rels = fu_util_get_upgrades_call(self, fwupd_device_get_id(dev), &error_local);
		if (rels == NULL) {
			if (error_local)
				g_debug("%s", error_local->message);
			continue;
		}

		/* add all releases */
		for (guint j = 0; j < rels->len; j++) {
			FwupdRelease *rel = g_ptr_array_index(rels, j);
			g_autoptr(FwupdJsonObject) json_obj_tmp = fwupd_json_object_new();
			if (!fwupd_release_match_flags(rel,
						       self->filter_release_include,
						       self->filter_release_exclude))
				continue;
			fwupd_codec_to_json(FWUPD_CODEC(rel),
					    json_obj_tmp,
					    FWUPD_CODEC_FLAG_TRUSTED);
			fwupd_json_array_add_object(json_arr, json_obj_tmp);
		}
	}
	fwupd_json_object_add_array(json_obj, "Releases", json_arr);
	fu_util_print_json_object(self->console, json_obj);
	return TRUE;
}

static gboolean
fu_util_get_upgrades(FuUtil *self, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;
	gboolean supported = FALSE;
	g_autoptr(FuUtilNode) root = g_node_new(NULL);
	g_autoptr(GPtrArray) devices_no_support = g_ptr_array_new();
	g_autoptr(GPtrArray) devices_no_upgrades = g_ptr_array_new();

	/* handle both forms */
	if (g_strv_length(values) == 0) {
		devices = fu_util_get_devices_call(self, error);
		if (devices == NULL)
			return FALSE;
	} else if (g_strv_length(values) == 1) {
		FwupdDevice *device = NULL; /* fu_util_get_device_by_id(self, values[0], error); */
		if (device == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_FOUND,
					    "Device ID lookup not yet migrated");
			return FALSE;
		}
		devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
		g_ptr_array_add(devices, device);
	} else {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}

	/* JSON Path */
	if (self->as_json)
		return fu_util_get_upgrades_as_json(self, devices, error);

	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devices, i);
		g_autoptr(GPtrArray) rels = NULL;
		g_autoptr(GError) error_local = NULL;
		FuUtilNode *child;

		if (!fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE) &&
		    !fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN))
			continue;
		if (!fwupd_device_match_flags(dev,
					      self->filter_device_include,
					      self->filter_device_exclude))
			continue;
		if (!fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_SUPPORTED)) {
			g_ptr_array_add(devices_no_support, dev);
			continue;
		}
		supported = TRUE;

		rels = fu_util_get_upgrades_call(self, fwupd_device_get_id(dev), &error_local);

		if (rels == NULL) {
			g_ptr_array_add(devices_no_upgrades, dev);
			if (error_local)
				g_debug("%s", error_local->message);
			continue;
		}
		child = g_node_append_data(root, g_object_ref(dev));

		for (guint j = 0; j < rels->len; j++) {
			FwupdRelease *rel = g_ptr_array_index(rels, j);
			if (!fwupd_release_match_flags(rel,
						       self->filter_release_include,
						       self->filter_release_exclude))
				continue;
			g_node_append_data(child, g_object_ref(rel));
		}
	}

	if (devices_no_support->len > 0) {
		fu_console_print_literal(self->console,
					 _("Devices with no available firmware updates: "));
		for (guint i = 0; i < devices_no_support->len; i++) {
			FwupdDevice *dev = g_ptr_array_index(devices_no_support, i);
			fu_console_print(self->console, " • %s", fwupd_device_get_name(dev));
		}
	}
	if (devices_no_upgrades->len > 0) {
		fu_console_print_literal(self->console,
					 _("Devices with the latest available firmware version:"));
		for (guint i = 0; i < devices_no_upgrades->len; i++) {
			FwupdDevice *dev = g_ptr_array_index(devices_no_upgrades, i);
			fu_console_print(self->console, " • %s", fwupd_device_get_name(dev));
		}
	}

	if (!supported) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    _("No updatable devices"));
		return FALSE;
	}
	if (g_node_n_nodes(root, G_TRAVERSE_ALL) <= 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    _("No updates available"));
		return FALSE;
	}

	fu_util_print_node(self->console, self->client, root);

	return TRUE;
}

/* --- Hwids Logic --- */

static void
fu_util_hwids_as_json(FuUtil *self, GStrv hwids_keys, GStrv hwids_values)
{
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	for (guint i = 0; hwids_keys[i] != NULL; i++)
		fwupd_json_object_add_string(json_obj, hwids_keys[i], hwids_values[i]);
	fu_util_print_json_object(self->console, json_obj);
}

static gboolean
fu_util_hwids(FuUtil *self, gchar **values, GError **error)
{
	g_auto(GStrv) hwids_keys = NULL;
	g_auto(GStrv) hwids_values = NULL;

	if (!fu_util_get_hwids_call(self, &hwids_keys, &hwids_values, error))
		return FALSE;

	/* JSON Path */
	if (self->as_json) {
		fu_util_hwids_as_json(self, hwids_keys, hwids_values);
		return TRUE;
	}

	fu_console_print_literal(self->console, "Computer Information");
	fu_console_print_literal(self->console, "--------------------");
	for (guint i = 0; hwids_keys[i] != NULL; i++) {
		if (fwupd_guid_is_valid(hwids_values[i]))
			continue;
		fu_console_print(self->console, "%s: %s", hwids_keys[i], hwids_values[i]);
	}

	fu_console_print_literal(self->console, "Hardware IDs");
	fu_console_print_literal(self->console, "------------");
	for (guint i = 0; hwids_keys[i] != NULL; i++) {
		g_autofree gchar *hwids_keys_real = NULL;
		g_auto(GStrv) hwids_keys_strv = NULL;
		if (!fwupd_guid_is_valid(hwids_values[i]))
			continue;
		hwids_keys_strv = g_strsplit(hwids_keys[i], "&", -1);
		hwids_keys_real = g_strjoinv(" + ", hwids_keys_strv);
		fu_console_print(self->console, "{%s}   <- %s", hwids_values[i], hwids_keys_real);
	}

	return TRUE;
}

/* --- Remotes Logic --- */

static gboolean
fu_util_get_remotes(FuUtil *self, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) remotes = NULL;
	g_autoptr(FuUtilNode) root = g_node_new(NULL);

	remotes = fu_util_get_remotes_call(self, error);
	if (remotes == NULL)
		return FALSE;

	/* JSON Path */
	if (self->as_json) {
		g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
		g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();

		for (guint i = 0; i < remotes->len; i++) {
			FwupdRemote *remote = g_ptr_array_index(remotes, i);
			g_autoptr(FwupdJsonObject) json_obj_tmp = fwupd_json_object_new();
			fwupd_codec_to_json(FWUPD_CODEC(remote),
					    json_obj_tmp,
					    FWUPD_CODEC_FLAG_NONE);
			fwupd_json_array_add_object(json_arr, json_obj_tmp);
		}
		fwupd_json_object_add_array(json_obj, "Remotes", json_arr);
		fu_util_print_json_object(self->console, json_obj);
		return TRUE;
	}

	if (remotes->len == 0) {
		fu_console_print_literal(self->console, _("No remotes found"));
		return TRUE;
	}

	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index(remotes, i);
		g_node_append_data(root, g_object_ref(remote));
	}

	fu_util_print_node(self->console, self->client, root);

	return TRUE;
}

/* --- local-install Logic --- */

static gboolean
fu_util_local_install(FuUtil *self, gchar **values, GError **error)
{
	const gchar *id;
	g_autofree gchar *filename = NULL;
	g_autoptr(GUnixInputStream) istr = NULL;
	g_autoptr(GVariant) options = NULL;
	gint fd;
	FwupdInstallFlags install_flags = self->flags;

	id = FWUPD_DEVICE_ID_ANY;
	self->current_operation = FU_UTIL_OPERATION_INSTALL;

	filename = g_strdup(values[0]);
	if (!g_file_test(filename, G_FILE_TEST_EXISTS)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "Local file not found: %s",
			    filename);
		return FALSE;
	}

	istr = fwupd_unix_input_stream_from_fn(filename, error);
	if (istr == NULL)
		return FALSE;

	fd = g_unix_input_stream_get_fd(istr);

	{
		g_auto(GVariantBuilder) builder;
		g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
		g_variant_builder_add(&builder,
				      "{sv}",
				      "reason",
				      g_variant_new_string("user-action"));
		if (filename != NULL) {
			g_variant_builder_add(&builder,
					      "{sv}",
					      "filename",
					      g_variant_new_string(filename));
		}
		g_variant_builder_add(&builder,
				      "{sv}",
				      "install-flags",
				      g_variant_new_uint64(install_flags));
		options = g_variant_builder_end(&builder);
	}

	return fu_binder_client_install_aidl(self->fwupd_binder, id, fd, options, error);
}

/* --- Stubs for other commands (Pending AIDL Migration) --- */

static gboolean
fu_util_refresh(FuUtil *self, gchar **values, GError **error)
{
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not migrated to AIDL yet.");
	return FALSE;
}

static void
fu_binder_client_log_handler(const gchar *log_domain,
			     GLogLevelFlags log_level,
			     const gchar *message,
			     gpointer user_data)
{
	g_printerr("%s: %s\n", log_domain, message);
}

/* --- Main Entry --- */

int
main(int argc, char *argv[])
{
	g_autoptr(GOptionContext) context = g_option_context_new(NULL);
	g_autoptr(FuUtil) self = g_new0(FuUtil, 1);
	g_autoptr(GPtrArray) cmd_array = fu_util_cmd_array_new();
	g_autoptr(GError) error = NULL;
	gboolean force = FALSE;
	gboolean allow_branch_switch = FALSE;
	gboolean allow_older = FALSE;
	gboolean allow_reinstall = FALSE;
	gboolean no_history = FALSE;
	gboolean ret;
	gboolean verbose = FALSE;
	gboolean version = FALSE;
	g_autofree gchar *cmd_descriptions = NULL;

	const GOptionEntry options[] = {{"verbose",
					 'v',
					 0,
					 G_OPTION_ARG_NONE,
					 &verbose,
					 /* TRANSLATORS: command line option */
					 N_("Show extra debugging information"),
					 NULL},
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
					{"json",
					 '\0',
					 0,
					 G_OPTION_ARG_NONE,
					 &self->as_json,
					 /* TRANSLATORS: command line option */
					 N_("Output in JSON format"),
					 NULL},
					{"no-history",
					 '\0',
					 0,
					 G_OPTION_ARG_NONE,
					 &no_history,
					 /* TRANSLATORS: command line option */
					 N_("Do not write to the history database"),
					 NULL},
					{"force",
					 '\0',
					 0,
					 G_OPTION_ARG_NONE,
					 &force,
					 /* TRANSLATORS: command line option */
					 N_("Force the action by relaxing some runtime checks"),
					 NULL},
					{NULL}};

	/* create helper object */
	self->main_ctx = g_main_context_new();
	self->loop = g_main_loop_new(self->main_ctx, FALSE);
	self->console = fu_console_new();
	self->post_requests = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	fu_console_set_main_context(self->console, self->main_ctx);

	/* add commands */
	fu_util_cmd_array_add(cmd_array,
			      "get-devices,get-topology",
			      NULL,
			      /* TRANSLATORS: command description */
			      _("Get all devices that support firmware updates"),
			      fu_util_get_devices);
	fu_util_cmd_array_add(cmd_array,
			      "local-install",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("FILE [DEVICE-ID|GUID]"),
			      /* TRANSLATORS: command description */
			      _("Install a firmware file in cabinet format on this hardware"),
			      fu_util_local_install);
	fu_util_cmd_array_add(cmd_array,
			      "get-updates,get-upgrades",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("[DEVICE-ID|GUID]"),
			      /* TRANSLATORS: command description */
			      _("Gets the list of updates for connected hardware"),
			      fu_util_get_upgrades);
	fu_util_cmd_array_add(cmd_array,
			      "get-remotes",
			      NULL,
			      /* TRANSLATORS: command description */
			      _("Gets the configured remotes"),
			      fu_util_get_remotes);
	fu_util_cmd_array_add(cmd_array,
			      "refresh",
			      /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			      _("[FILE FILE_SIG REMOTE-ID]"),
			      /* TRANSLATORS: command description */
			      _("Refresh metadata from remote server"),
			      fu_util_refresh);
	fu_util_cmd_array_add(cmd_array,
			      "hwids",
			      NULL,
			      /* TRANSLATORS: command description */
			      _("Return all the hardware IDs for the machine"),
			      fu_util_hwids);

	/* get a list of the commands */
	self->context = g_option_context_new(NULL);
	cmd_descriptions = fu_util_cmd_array_to_string(cmd_array);
	g_option_context_set_summary(self->context, cmd_descriptions);
	g_option_context_set_description(
	    self->context,
	    /* TRANSLATORS: CLI description */
	    _("This tool allows an administrator to query and control the "
	      "fwupd daemon, allowing them to perform actions such as "
	      "installing or downgrading firmware."));

	/* TRANSLATORS: program name */
	g_set_application_name(_("Firmware Utility"));
	g_option_context_add_main_entries(self->context, options, NULL);
	ret = g_option_context_parse(self->context, &argc, &argv, &error);
	if (!ret) {
		fu_console_print(self->console,
				 "%s: %s",
				 /* TRANSLATORS: the user didn't read the man page */
				 _("Failed to parse arguments"),
				 error->message);
		return EXIT_FAILURE;
	}

	/* show version */
	if (version) {
		g_print("%s\n", PACKAGE_VERSION);
		return EXIT_SUCCESS;
	}

	/* set verbose? */
	if (verbose) {
		g_log_set_default_handler(fu_binder_client_log_handler, NULL);
		(void)g_setenv("G_MESSAGES_DEBUG", "all", FALSE);
		(void)g_setenv("FWUPD_VERBOSE", "1", FALSE);
	}

	/* set flags */
	if (allow_reinstall)
		self->flags |= FWUPD_INSTALL_FLAG_ALLOW_REINSTALL;
	if (allow_older)
		self->flags |= FWUPD_INSTALL_FLAG_ALLOW_OLDER;
	if (allow_branch_switch)
		self->flags |= FWUPD_INSTALL_FLAG_ALLOW_BRANCH_SWITCH;
	if (force) {
		self->flags |= FWUPD_INSTALL_FLAG_FORCE;
		self->flags |= FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS;
	}
	if (no_history)
		self->flags |= FWUPD_INSTALL_FLAG_NO_HISTORY;

	/* connect to the daemon */
	self->client = fwupd_client_new();

	self->fwupd_binder = fu_binder_client_get_service_handle_aidl(&error);

	/* fail if daemon doesn't exist */
	if (!self->fwupd_binder) {
		/* TRANSLATORS: could not contact the fwupd service over binder */
		g_set_error_literal(&error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    _("Failed to connect to daemon"));
		fu_util_print_error(self, error);
		return EXIT_FAILURE;
	}

	/* hook up the listener for progress updates */
	if (!fu_binder_client_setup_listener_aidl(self->fwupd_binder)) {
		g_prefix_error(&error, _("Failed to attach listener to daemon: "));
		fu_util_print_error(self, error);
		return EXIT_FAILURE;
	}

	/* run the specified command */
	ret = fu_util_cmd_array_run(cmd_array, self, argv[1], (gchar **)&argv[2], &error);

	if (!ret) {
#ifdef SUPPORTED_BUILD
		/* sanity check */
		if (error == NULL) {
			g_critical("exec failed but no error set!");
			return EXIT_FAILURE;
		}
#endif
		fu_util_print_error(self, error);
		if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_ARGS)) {
			g_autofree gchar *cmd = g_strdup_printf("%s --help", g_get_prgname());
			g_autoptr(GString) str = g_string_new("\n");
			/* TRANSLATORS: explain how to get help,
			 * where $1 is something like 'fwupdmgr --help' */
			g_string_append_printf(str, _("Use %s for help"), cmd);
			fu_console_print_literal(self->console, str->str);
		} else if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO))
			return EXIT_NOTHING_TO_DO;
		else if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND))
			return EXIT_NOT_FOUND;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
