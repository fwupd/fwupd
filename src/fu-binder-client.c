/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "glib.h"
#include "glibconfig.h"

#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <android/persistable_bundle.h>
#include <stdlib.h>
#define G_LOG_DOMAIN "FuMain"

#include "config.h"
#include "fu-binder-aidl.h"
#include "fu-debug.h"
#include "fu-util-common.h"

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <glib/gi18n.h>

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

static bool
parcelable_array_allocator(gpointer user_data, gint32 length)
{
	GPtrArray **ptr_array = (GPtrArray **)user_data;
	g_warning("allocate %p to %d ", user_data, length);
	*ptr_array = g_ptr_array_sized_new(length);
	return TRUE;
}

static binder_status_t
read_parcelable_element(const AParcel *parcel, gpointer user_data, gsize index)
{
	GPtrArray **ptr_array = (GPtrArray **)user_data;
	g_warning("setting element %ld to %p ", index, parcel);
	g_ptr_array_insert(*ptr_array, index, parcel);
	return STATUS_OK;
}

// TODO: APersistableBundle_stringAllocator that allocates g_variant_dict (context) entries?
char *
bundle_string_allocator(gint32 size_bytes, void *context)
{
	return g_malloc0(size_bytes);
}

static gboolean
fu_util_get_devices(FuUtilPrivate *priv, gchar **values, GError **error)
{
	AParcel *in = NULL;
	AParcel *out = NULL;
	AStatus *status = NULL;
	binder_status_t nstatus = STATUS_OK;
	GPtrArray *device_parcels = NULL;

	g_warning("get devices");

	AIBinder_prepareTransaction(priv->fwupd_binder, &in);
	nstatus =
	    AIBinder_transact(priv->fwupd_binder, FWUPD_BINDER_CALL_GET_DEVICES, &in, &out, 0);

	if (nstatus != STATUS_OK) {
		status = AStatus_fromStatus(nstatus);
		g_warning("get_device transaction returned %s", AStatus_getDescription(status));
	}

	nstatus = AParcel_readStatusHeader(out, &status);

	if (nstatus != STATUS_OK) {
		status = AStatus_fromStatus(nstatus);
		g_warning("couldn't read status header %s", AStatus_getDescription(status));
	}

	nstatus = AStatus_getStatus(status);
	if (nstatus != STATUS_OK) {
		g_warning("status not okay %s", AStatus_getDescription(status));
	}

	// TODO: gp_parcel_read_variant with type "aa{sv}" to decode array of PersistantBundles
	nstatus = AParcel_readParcelableArray(out,
					      (void *)&device_parcels,
					      parcelable_array_allocator,
					      read_parcelable_element);

	if (nstatus != STATUS_OK) {
		status = AStatus_fromStatus(nstatus);
		g_warning("couldn't decode parcellable array %s", AStatus_getDescription(status));
	}

	g_warning("device list size is %d", device_parcels->len);

	for (guint i = 0; i < device_parcels->len; i++) {
		const AParcel *parcel = g_ptr_array_index(device_parcels, i);
		gint32 is_some = 0;
		AParcel_readInt32(parcel, &is_some);
		g_warning("is null is %d", is_some);
		if (is_some) {
			APersistableBundle *bundle = APersistableBundle_new();
			char *str_keys[256] = {0};
			gint32 str_keys_len;

			nstatus = APersistableBundle_readFromParcel(parcel, &bundle);
			if (nstatus != STATUS_OK) {
				status = AStatus_fromStatus(nstatus);
				g_warning("couldn't decode persistable bundle %s",
					  AStatus_getDescription(status));
			}

			// Why return the length that I provided?
			str_keys_len = APersistableBundle_getStringKeys(bundle,
									(gchar **)&str_keys,
									G_N_ELEMENTS(str_keys),
									bundle_string_allocator,
									NULL);
			for (char **iter_key = str_keys; *iter_key; iter_key++) {
				char *val = NULL;
				g_warning(" - bundle str key is %s", *iter_key);
				APersistableBundle_getString(bundle,
							     *iter_key,
							     &val,
							     bundle_string_allocator,
							     NULL);
				g_warning(" - - value: %s", val);
			}
		}
	}

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
	const gchar *id;
	g_autofree gchar *filename = NULL;
	g_autoptr(FwupdDevice) dev = NULL;

	priv->current_operation = FU_UTIL_OPERATION_INSTALL;

	/* install with flags chosen by the user */
	filename = fu_util_download_if_required(priv, values[0], error);
	if (filename == NULL)
		return FALSE;

	g_warning("local install %s", filename);
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

static AIBinder_Class *
get_listener_class(void)
{
	static AIBinder_Class *listener_class = NULL;
	if (!listener_class) {
		listener_class = AIBinder_Class_define(BINDER_DEFAULT_IFACE,
						       fwupd_service_on_create,
						       fwupd_service_on_destroy,
						       fwupd_service_on_transact);
	}

	return listener_class;
}

static int
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
	binder_status_t nstatus = STATUS_OK;
	gboolean ret;

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

	g_option_context_add_group(context, fu_debug_get_option_group());
	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_printerr("Failed to parse arguments: %s\n", error->message);
		return EXIT_FAILURE;
	}

	/* connect to the daemon */
	ABinderProcess_setupPolling(&priv->binder_fd);
	g_idle_add(poll_binder_process, priv);

	priv->fwupd_binder = AServiceManager_waitForService(BINDER_SERVICE_NAME);
	const AIBinder_Class *fwupd_binder_class = get_listener_class();
	AIBinder_associateClass(priv->fwupd_binder, fwupd_binder_class);

	/* run the specified command */
	ret = fu_util_cmd_array_run(cmd_array, priv, argv[1], (gchar **)&argv[2], &error);

	return EXIT_SUCCESS;
}
