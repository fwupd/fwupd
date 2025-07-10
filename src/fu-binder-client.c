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
#include <android/persistable_bundle.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <stdlib.h>

#include "fwupd-common-private.h"
#include "fwupd-error.h"

#include "fu-binder-aidl.h"
#include "fu-debug.h"
#include "fu-util-common.h"
#include "gparcelable.h"

/* custom return codes */
#define EXIT_NOTHING_TO_DO 2
#define EXIT_NOT_FOUND	   3

G_DEFINE_AUTOPTR_CLEANUP_FUNC(AStatus, AStatus_delete)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(AParcel, AParcel_delete)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(APersistableBundle, APersistableBundle_delete)

typedef enum {
	FU_UTIL_OPERATION_UNKNOWN,
	FU_UTIL_OPERATION_UPDATE,
	FU_UTIL_OPERATION_DOWNGRADE,
	FU_UTIL_OPERATION_INSTALL,
	FU_UTIL_OPERATION_LAST
} FuUtilOperation;

struct FuUtilPrivate {
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

typedef struct self {
	GMainLoop *loop;
	gulong death_id;
} FuUtil;

static void
fu_util_private_free(FuUtilPrivate *priv)
{
	if (priv->client != NULL) {
		/* when destroying GDBusProxy in a custom GMainContext, the context must be
		 * iterated enough after finalization of the proxies that any pending D-Bus traffic
		 * can be freed */
		fwupd_client_disconnect(priv->client, NULL);
		while (g_main_context_iteration(priv->main_ctx, FALSE)) {
			/* nothing needs to be done here */
		};
		g_object_unref(priv->client);
	}
	if (priv->current_device != NULL)
		g_object_unref(priv->current_device);
	// g_ptr_array_unref(priv->post_requests);
	g_main_loop_unref(priv->loop);
	g_main_context_unref(priv->main_ctx);
	// g_object_unref(priv->cancellable);
	g_object_unref(priv->console);
	// g_option_context_free(priv->context);
	g_free(priv);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuUtilPrivate, fu_util_private_free)
#pragma clang diagnostic pop

static void
fu_self_free(FuUtil *self)
{
	g_free(self);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuUtil, fu_self_free)

/* Attempts to set error from the parcel status
 * sets error and returns FALSE if the header is invalid or contains an error
 */
static gboolean
fu_util_binder_parcel_read_header(AParcel *parcel, GError **error)
{
	binder_status_t nstatus = STATUS_OK;
	g_autoptr(AStatus) status = NULL;
	binder_exception_t ex_code = EX_NONE;
	const gchar *message;

	if (parcel == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Attempting to read header of NULL parcel");
		return FALSE;
	}

	// TODO: Is it worth reporting AParcel_getDataPosition(parcel); to the caller?
	nstatus = AParcel_readStatusHeader(parcel, &status);

	if (nstatus != STATUS_OK) {
		status = AStatus_fromStatus(nstatus);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to read transaction header %s",
			    AStatus_getDescription(status));
		return FALSE;
	}

	if (!AStatus_isOk(status)) {
		ex_code = AStatus_getExceptionCode(status);
		message = AStatus_getMessage(status);

		if (ex_code == EX_SERVICE_SPECIFIC) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    AStatus_getServiceSpecificError(status),
					    message);
		} else {
#ifndef SUPPORTED_BUILD
			g_critical(
			    "AStatus: Binder error could not be not converted to FwupdError %s",
			    AStatus_getDescription(status));
#endif
			g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, message);
		}
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_util_transact(FuUtilPrivate *priv,
		 transaction_code_t code,
		 GVariant *parameters,
		 binder_flags_t flags,
		 AParcel **out,
		 GError **error)
{
	AParcel *in = NULL;
	g_autoptr(AParcel) pending_in = NULL;
	g_autoptr(AStatus) status = NULL;
	binder_status_t nstatus = STATUS_OK;

	AIBinder_prepareTransaction(priv->fwupd_binder, &pending_in);

	if (parameters)
		if (gp_parcel_write_variant(pending_in, parameters, error) != STATUS_OK)
			return FALSE;

	in = g_steal_pointer(&pending_in);
	nstatus = AIBinder_transact(priv->fwupd_binder, code, &in, out, flags);

	if (nstatus != STATUS_OK) {
		status = AStatus_fromStatus(nstatus);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Binder transaction %d returned %s",
			    code,
			    AStatus_getDescription(status));
		return FALSE;
	}

	if (*out == NULL) {
		// A transaction with STATUS_OK should always have an output parcel
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Binder transaction succeeded but didn't return a value");
		return FALSE;
	}

	if (!fu_util_binder_parcel_read_header(*out, error)) {
		return FALSE;
	}

	return TRUE;
}

static GPtrArray *
fu_util_get_upgrades_call(FuUtilPrivate *priv, const gchar *device_id, GError **error)
{
	g_autoptr(AParcel) out = NULL;
	g_autoptr(AStatus) status = NULL;
	g_autoptr(GVariant) val = NULL;
	g_autoptr(GVariant) parameters = NULL;
	g_autoptr(GPtrArray) array = NULL;
	GVariantBuilder builder;
	const GVariantType *vtype = G_VARIANT_TYPE("(aa{sv})");

	parameters = g_variant_new("(s)", device_id);

	if (!fu_util_transact(priv, FWUPD_BINDER_CALL_GET_UPGRADES, parameters, 0, &out, error)) {
		return NULL;
	}

	g_variant_builder_init(&builder, vtype);
	if (!gp_parcel_to_variant(&builder, out, vtype, error)) {
		return NULL;
	}
	val = g_variant_builder_end(&builder);

	array = fwupd_codec_array_from_variant(val, FWUPD_TYPE_RELEASE, error);
	if (array == NULL) {
		return NULL;
	}
	fwupd_device_array_ensure_parents(array);

	return g_steal_pointer(&array);
}

static GPtrArray *
fu_util_get_devices_call(FuUtilPrivate *priv, GError **error)
{
	g_autoptr(AParcel) out = NULL;
	g_autoptr(AStatus) status = NULL;
	g_autoptr(GVariant) val = NULL;
	g_autoptr(GPtrArray) array = NULL;
	GVariantBuilder builder;
	const GVariantType *vtype = G_VARIANT_TYPE("(aa{sv})");

	if (!fu_util_transact(priv, FWUPD_BINDER_CALL_GET_DEVICES, NULL, 0, &out, error)) {
		return NULL;
	}

	g_variant_builder_init(&builder, vtype);
	if (!gp_parcel_to_variant(&builder, out, vtype, error)) {
		return NULL;
	}
	val = g_variant_builder_end(&builder);

	array = fwupd_codec_array_from_variant(val, FWUPD_TYPE_DEVICE, error);
	if (array == NULL) {
		return NULL;
	}
	fwupd_device_array_ensure_parents(array);

	return g_steal_pointer(&array);
}

static GPtrArray *
fu_util_get_remotes_call(FuUtilPrivate *priv, GError **error)
{
	g_autoptr(AParcel) out = NULL;
	g_autoptr(GVariant) val = NULL;
	g_autoptr(GPtrArray) array = NULL;
	GVariantBuilder builder;
	const GVariantType *vtype = G_VARIANT_TYPE("(aa{sv})");

	if (!fu_util_transact(priv, FWUPD_BINDER_CALL_GET_REMOTES, NULL, 0, &out, error)) {
		return NULL;
	}

	g_variant_builder_init(&builder, vtype);
	if (!gp_parcel_to_variant(&builder, out, vtype, error)) {
		return NULL;
	}
	val = g_variant_builder_end(&builder);

	array = fwupd_codec_array_from_variant(val, FWUPD_TYPE_REMOTE, error);
	if (array == NULL) {
		return NULL;
	}

	return g_steal_pointer(&array);
}

// Taken from FuUtil
static void
fu_util_build_device_tree_node(FuUtilPrivate *priv, FuUtilNode *root, FwupdDevice *dev)
{
	FuUtilNode *root_child = g_node_append_data(root, g_object_ref(dev));
	if (fwupd_device_get_release_default(dev) != NULL)
		g_node_append_data(root_child, g_object_ref(fwupd_device_get_release_default(dev)));
}

// Taken from FuUtil
static gboolean
fu_util_build_device_tree_cb(FuUtilNode *n, gpointer user_data)
{
	FuUtilPrivate *priv = (FuUtilPrivate *)user_data;
	FwupdDevice *dev = n->data;

	/* root node */
	if (dev == NULL)
		return FALSE;

	/* release */
	if (FWUPD_IS_RELEASE(n->data))
		return FALSE;

	/* an interesting child, so include the parent */
	for (FuUtilNode *c = n->children; c != NULL; c = c->next) {
		if (c->data != NULL)
			return FALSE;
	}

	/* not interesting, clear the node data */
	if (!fwupd_device_match_flags(dev,
				      priv->filter_device_include,
				      priv->filter_device_exclude))
		g_clear_object(&n->data);
	else if (!priv->show_all && !fu_util_is_interesting_device(dev))
		g_clear_object(&n->data);

	/* continue */
	return FALSE;
}

// Taken from FuUtil
static void
fu_util_build_device_tree(FuUtilPrivate *priv, FuUtilNode *root, GPtrArray *devs)
{
	/* add the top-level parents */
	for (guint i = 0; i < devs->len; i++) {
		FwupdDevice *dev_tmp = g_ptr_array_index(devs, i);
		if (fwupd_device_get_parent(dev_tmp) != NULL)
			continue;
		fu_util_build_device_tree_node(priv, root, dev_tmp);
	}

	/* children */
	for (guint i = 0; i < devs->len; i++) {
		FwupdDevice *dev_tmp = g_ptr_array_index(devs, i);
		FuUtilNode *root_parent;

		if (fwupd_device_get_parent(dev_tmp) == NULL)
			continue;
		root_parent = g_node_find(root,
					  G_PRE_ORDER,
					  G_TRAVERSE_ALL,
					  fwupd_device_get_parent(dev_tmp));
		if (root_parent == NULL)
			continue;
		fu_util_build_device_tree_node(priv, root_parent, dev_tmp);
	}

	/* prune children that are not updatable */
	g_node_traverse(root, G_POST_ORDER, G_TRAVERSE_ALL, -1, fu_util_build_device_tree_cb, priv);
}

static gboolean
fu_util_get_devices(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FuUtilNode) root = g_node_new(NULL);
	g_autoptr(GPtrArray) devs = fu_util_get_devices_call(priv, error);

	if (devs == NULL)
		return FALSE;

	/* print */
	if (devs->len > 0)
		fu_util_build_device_tree(priv, root, devs);
	if (g_node_n_children(root) == 0) {
		fu_console_print_literal(priv->console,
					 /* TRANSLATORS: nothing attached that can be upgraded */
					 _("No hardware detected with firmware update capability"));
		return TRUE;
	}
	fu_util_print_node(priv->console, priv->client, root);

	return TRUE;
}

static gchar *
fu_util_download_if_required(FuUtilPrivate *priv, const gchar *perhapsfn, GError **error)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* a local file */
	if (g_file_test(perhapsfn, G_FILE_TEST_EXISTS))
		return g_strdup(perhapsfn);
	if (!fu_util_is_url(perhapsfn))
		return g_strdup(perhapsfn);

	/* download the firmware to a cachedir */
	filename = fu_util_get_user_cache_path(perhapsfn);

	if (g_file_test(filename, G_FILE_TEST_EXISTS))
		return g_steal_pointer(&filename);

	if (!fu_path_mkdir_parent(filename, error))
		return NULL;

	blob = fwupd_client_download_bytes(priv->client,
					   perhapsfn,
					   priv->download_flags,
					   priv->cancellable,
					   error);

	if (blob == NULL)
		return NULL;

	/* save file to cache */
	if (!fu_bytes_set_contents(filename, blob, error))
		return NULL;

	return g_steal_pointer(&filename);
}

static gboolean
fu_util_local_install(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(AParcel) out = NULL;
	g_autoptr(AStatus) status = NULL;
	g_autoptr(GVariant) val = NULL;
	const gchar *id;
	g_autofree gchar *filename = NULL;
	g_autoptr(FwupdDevice) dev = NULL;
	g_autoptr(GUnixInputStream) istr = NULL;
	FwupdInstallFlags install_flags = priv->flags;

	/* for now we ignore the requested device */
	id = FWUPD_DEVICE_ID_ANY;

	priv->current_operation = FU_UTIL_OPERATION_INSTALL;

	/* install with flags chosen by the user */
	filename = fu_util_download_if_required(priv, values[0], error);
	if (filename == NULL)
		return FALSE;

	istr = fwupd_unix_input_stream_from_fn(filename, error);
	if (istr == NULL)
		return FALSE;

	{
		g_auto(GVariantBuilder) builder;
		g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
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

		val = g_variant_new("(sha{sv})", id, g_unix_input_stream_get_fd(istr), &builder);
	}
	g_message("encoding install params %s", g_variant_print(val, TRUE));

	return fu_util_transact(priv, FWUPD_BINDER_CALL_INSTALL, val, 0, &out, error);
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

static gboolean
fu_util_get_remotes(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(FuUtilNode) root = g_node_new(NULL);
	g_autoptr(GPtrArray) remotes = NULL;

	remotes = fu_util_get_remotes_call(priv, error);
	if (remotes == NULL)
		return FALSE;

	if (remotes->len == 0) {
		/* TRANSLATORS: no repositories to download from */
		fu_console_print_literal(priv->console, _("No remotes available"));
		return TRUE;
	}

	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote_tmp = g_ptr_array_index(remotes, i);
		g_node_append_data(root, g_object_ref(remote_tmp));
	}
	fu_util_print_node(priv->console, priv->client, root);

	return TRUE;
}

static gboolean
fu_util_refresh(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(AParcel) out = NULL;
	g_autoptr(AStatus) status = NULL;
	g_autoptr(GVariant) val = NULL;
	g_autoptr(GUnixInputStream) istr = NULL;
	g_autoptr(GUnixInputStream) istr_sig = NULL;

	const gchar *remote_id = values[2];
	const gchar *metadata_fn = values[0];
	const gchar *signature_fn = values[1];

	istr = fwupd_unix_input_stream_from_fn(metadata_fn, error);
	if (istr == NULL)
		return FALSE;
	istr_sig = fwupd_unix_input_stream_from_fn(signature_fn, error);
	if (istr_sig == NULL)
		return FALSE;

	val = g_variant_new("(&shh)",
			    remote_id,
			    g_unix_input_stream_get_fd(istr),
			    g_unix_input_stream_get_fd(istr_sig));

	return fu_util_transact(priv, FWUPD_BINDER_CALL_UPDATE_METADATA, val, 0, &out, error);
}

static gboolean
fu_util_get_upgrades(FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;
	gboolean supported = FALSE;
	g_autoptr(FuUtilNode) root = g_node_new(NULL);
	g_autoptr(GPtrArray) devices_no_support = g_ptr_array_new();
	g_autoptr(GPtrArray) devices_no_upgrades = g_ptr_array_new();

	/* are the remotes very old */
	// if (!fu_util_perhaps_refresh_remotes(priv, error))
	//	return FALSE;

	/* handle both forms */
	if (g_strv_length(values) == 0) {
		devices = fu_util_get_devices_call(priv, error);
		// fwupd_client_get_devices(priv->client, priv->cancellable, error);
		if (devices == NULL)
			return FALSE;
	} else if (g_strv_length(values) == 1) {
		// TODO: get by id
		FwupdDevice *device = NULL; // fu_util_get_device_by_id(priv, values[0], error);
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

	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devices, i);
		g_autoptr(GPtrArray) rels = NULL;
		g_autoptr(GError) error_local = NULL;
		FuUtilNode *child;

		/* not going to have results, so save a D-Bus round-trip */
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
		supported = TRUE;

		/* get the releases for this device and filter for validity */
		rels = fu_util_get_upgrades_call(priv, fwupd_device_get_id(dev), &error_local);
		/*fwupd_client_get_upgrades(priv->client,
						 fwupd_device_get_id(dev),
						 priv->cancellable,
						 &error_local);*/
		if (rels == NULL) {
			g_ptr_array_add(devices_no_upgrades, dev);
			/* discard the actual reason from user, but leave for debugging */
			g_debug("%s", error_local->message);
			continue;
		}
		child = g_node_append_data(root, g_object_ref(dev));

		/* add all releases */
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

	/* nag? */
	// if (!fu_util_perhaps_show_unreported(priv, error))
	//	return FALSE;

	/* no devices supported by LVFS or all are filtered */
	if (!supported) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    /* TRANSLATORS: this is an error string */
				    _("No updatable devices"));
		return FALSE;
	}
	/* no updates available */
	if (g_node_n_nodes(root, G_TRAVERSE_ALL) <= 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    /* TRANSLATORS: this is an error string */
				    _("No updates available"));
		return FALSE;
	}

	fu_util_print_node(priv->console, priv->client, root);

	return TRUE;
}

static void *
fwupd_service_on_create(void *arg)
{
	return arg;
}
static void
fwupd_service_on_destroy(void *arg)
{
	// TODO: Clean up ???
}

static binder_status_t
fwupd_service_on_transact(AIBinder *binder,
			  transaction_code_t code,
			  const AParcel *in,
			  AParcel *out)
{
	// TODO: Do we need to transact??
	return STATUS_OK;
}

static gboolean
poll_binder_process(void *user_data)
{
	// Daemon *daemon = user_data;
	FuUtilPrivate *priv = user_data;
	binder_status_t nstatus = STATUS_OK;
	if (priv->binder_fd < 0)
		return G_SOURCE_CONTINUE;

	nstatus = ABinderProcess_handlePolledCommands();

	if (nstatus != STATUS_OK) {
		AStatus *status = AStatus_fromStatus(nstatus);
		g_warning("failed to handle polled commands %s", AStatus_getDescription(status));
	}

	return G_SOURCE_CONTINUE;
}

int
main(int argc, char *argv[])
{
	g_autoptr(FuUtil) self = g_new0(FuUtil, 1);
	g_autoptr(GOptionContext) context = g_option_context_new(NULL);
	g_autoptr(FuUtilPrivate) priv = g_new0(FuUtilPrivate, 1);
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
	const AIBinder_Class *fwupd_binder_class = AIBinder_Class_define(BINDER_DEFAULT_IFACE,
									 fwupd_service_on_create,
									 fwupd_service_on_destroy,
									 fwupd_service_on_transact);

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
					 &priv->as_json,
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
	priv->main_ctx = g_main_context_new();
	priv->loop = g_main_loop_new(priv->main_ctx, FALSE);
	priv->console = fu_console_new();
	priv->post_requests = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	fu_console_set_main_context(priv->console, priv->main_ctx);

	// TODO: Add binder alternative to fwupd-client.c
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

	/* get a list of the commands */
	priv->context = g_option_context_new(NULL);
	cmd_descriptions = fu_util_cmd_array_to_string(cmd_array);
	g_option_context_set_summary(priv->context, cmd_descriptions);
	g_option_context_set_description(
	    priv->context,
	    /* TRANSLATORS: CLI description */
	    _("This tool allows an administrator to query and control the "
	      "fwupd daemon, allowing them to perform actions such as "
	      "installing or downgrading firmware."));

	/* TRANSLATORS: program name */
	g_set_application_name(_("Firmware Utility"));
	g_option_context_add_main_entries(priv->context, options, NULL);
	ret = g_option_context_parse(priv->context, &argc, &argv, &error);
	if (!ret) {
		fu_console_print(priv->console,
				 "%s: %s",
				 /* TRANSLATORS: the user didn't read the man page */
				 _("Failed to parse arguments"),
				 error->message);
		return EXIT_FAILURE;
	}

	/* set verbose? */
	if (verbose) {
		(void)g_setenv("G_MESSAGES_DEBUG", "all", FALSE);
		(void)g_setenv("FWUPD_VERBOSE", "1", FALSE);
	} else {
		//g_log_set_handler(G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, fu_util_ignore_cb, NULL);
	}

	/* set flags */
	if (allow_reinstall)
		priv->flags |= FWUPD_INSTALL_FLAG_ALLOW_REINSTALL;
	if (allow_older)
		priv->flags |= FWUPD_INSTALL_FLAG_ALLOW_OLDER;
	if (allow_branch_switch)
		priv->flags |= FWUPD_INSTALL_FLAG_ALLOW_BRANCH_SWITCH;
	if (force) {
		priv->flags |= FWUPD_INSTALL_FLAG_FORCE;
		priv->flags |= FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS;
	}
	if (no_history)
		priv->flags |= FWUPD_INSTALL_FLAG_NO_HISTORY;

	/* connect to the daemon */
	// TODO: Maybe create FuClientBinder
	// priv->client = fwupd_client_new();
	// fwupd_client_set_main_context(priv->client, priv->main_ctx);
	ABinderProcess_setupPolling(&priv->binder_fd);
	g_idle_add(poll_binder_process, priv);

	priv->fwupd_binder = AServiceManager_checkService(BINDER_SERVICE_NAME);

	/* fail if daemon doesn't exist */
	if (!priv->fwupd_binder) {
		/* TRANSLATORS: could not contact the fwupd service over binder */
		g_set_error_literal(&error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    _("Failed to connect to daemon"));
		fu_util_print_error(priv, error);
		return EXIT_FAILURE;
	}

	AIBinder_associateClass(priv->fwupd_binder, fwupd_binder_class);

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
			g_autofree gchar *cmd = g_strdup_printf("%s --help", g_get_prgname());
			g_autoptr(GString) str = g_string_new("\n");
			/* TRANSLATORS: explain how to get help,
			 * where $1 is something like 'fwupdmgr --help' */
			g_string_append_printf(str, _("Use %s for help"), cmd);
			fu_console_print_literal(priv->console, str->str);
		} else if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO))
			return EXIT_NOTHING_TO_DO;
		else if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND))
			return EXIT_NOT_FOUND;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
