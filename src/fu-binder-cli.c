/*
 * Copyright 2025 Colin Kinloch <colin.kinloch@collabora.com>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuMain"

#include "config.h"

#include <fwupdplugin.h>

#include <android/binder_ibinder.h>
#include <android/binder_manager.h>
#include <android/binder_parcel.h>
#include <android/binder_process.h>
#include <android/binder_status.h>

#include <glib/gi18n.h>
#include <locale.h>
#include <stdlib.h>
#include <unistd.h>

#include "fwupd-client-private.h"
#include "fwupd-common-private.h"

#include "fu-binder-cli-bridge.h"
#include "fu-binder-cli.h"
#include "fu-cli-common.h"

struct _FuBinderCli {
	FuCli parent_instance;
	AIBinder *fwupd_binder;
};

G_DEFINE_TYPE(FuBinderCli, fu_binder_cli, FU_TYPE_CLI)

static GPtrArray *
fu_binder_cli_sync_impl_get_remotes(FwupdClient *client,
				    gpointer user_data,
				    GCancellable *cancellable,
				    GError **error)
{
	FuBinderCli *self = FU_BINDER_CLI(user_data);
	return fu_binder_cli_bridge_get_remotes(self->fwupd_binder, error);
}

static GPtrArray *
fu_binder_cli_sync_impl_get_upgrades(FwupdClient *client,
				     const gchar *device_id,
				     gpointer user_data,
				     GCancellable *cancellable,
				     GError **error)
{
	FuBinderCli *self = FU_BINDER_CLI(user_data);
	return fu_binder_cli_bridge_get_upgrades(self->fwupd_binder, device_id, error);
}

static GPtrArray *
fu_binder_cli_sync_impl_get_devices(FwupdClient *client,
				    gpointer user_data,
				    GCancellable *cancellable,
				    GError **error)
{
	FuBinderCli *self = FU_BINDER_CLI(user_data);
	return fu_binder_cli_bridge_get_devices(self->fwupd_binder, error);
}

static FwupdDevice *
fu_binder_cli_sync_impl_get_device_by_id(FwupdClient *client,
					 const gchar *device_id,
					 gpointer user_data,
					 GCancellable *cancellable,
					 GError **error)
{
	g_autoptr(GPtrArray) devices =
	    fu_binder_cli_sync_impl_get_devices(client, user_data, cancellable, error);
	if (devices == NULL)
		return NULL;
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devices, i);
		if (g_strcmp0(fwupd_device_get_id(dev), device_id) == 0)
			return g_object_ref(dev);
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_FOUND,
		    "device %s not found",
		    device_id);
	return NULL;
}

static GPtrArray *
fu_binder_cli_sync_impl_get_devices_by_guid(FwupdClient *client,
					    const gchar *guid,
					    gpointer user_data,
					    GCancellable *cancellable,
					    GError **error)
{
	g_autoptr(GPtrArray) devices =
	    fu_binder_cli_sync_impl_get_devices(client, user_data, cancellable, error);
	g_autoptr(GPtrArray) results = g_ptr_array_new_with_free_func(g_object_unref);
	if (devices == NULL)
		return NULL;
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devices, i);
		if (fwupd_device_has_guid(dev, guid))
			g_ptr_array_add(results, g_object_ref(dev));
	}
	return g_steal_pointer(&results);
}

static gboolean
fu_binder_cli_sync_impl_connect(FwupdClient *client,
				gpointer user_data,
				GCancellable *cancellable,
				GError **error)
{
	FuBinderCli *self = FU_BINDER_CLI(user_data);
	return fu_binder_cli_bridge_connect_client(self->fwupd_binder, client, error);
}

static gboolean
fu_binder_cli_sync_impl_set_feature_flags(FwupdClient *client,
					  FwupdFeatureFlags feature_flags,
					  gpointer user_data,
					  GCancellable *cancellable,
					  GError **error)
{
	return TRUE;
}

static gboolean
fu_binder_cli_sync_impl_install(FwupdClient *client,
				const gchar *device_id,
				const gchar *filename,
				FwupdInstallFlags install_flags,
				gpointer user_data,
				GCancellable *cancellable,
				GError **error)
{
	FuBinderCli *self = FU_BINDER_CLI(user_data);
	gint fd;
	g_autoptr(GUnixInputStream) stream = NULL;

	stream = fwupd_unix_input_stream_from_fn(filename, error);
	if (stream == NULL)
		return FALSE;
	fd = g_unix_input_stream_get_fd(stream);
	return fu_binder_cli_bridge_install(self->fwupd_binder,
					    device_id,
					    fd,
					    install_flags,
					    error);
}

static GPtrArray *
fu_binder_cli_sync_impl_get_history(FwupdClient *client,
				    gpointer user_data,
				    GCancellable *cancellable,
				    GError **error)
{
	return g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
}

static GPtrArray *
fu_binder_cli_sync_impl_get_plugins(FwupdClient *client,
				    gpointer user_data,
				    GCancellable *cancellable,
				    GError **error)
{
	return g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
}

static FwupdRemote *
fu_binder_cli_sync_impl_get_remote_by_id(FwupdClient *client,
					 const gchar *remote_id,
					 gpointer user_data,
					 GCancellable *cancellable,
					 GError **error)
{
	g_autoptr(GPtrArray) remotes =
	    fu_binder_cli_sync_impl_get_remotes(client, user_data, cancellable, error);
	if (remotes == NULL)
		return NULL;
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index(remotes, i);
		if (g_strcmp0(fwupd_remote_get_id(remote), remote_id) == 0)
			return g_object_ref(remote);
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_FOUND,
		    "remote %s not found",
		    remote_id);
	return NULL;
}

static void
fu_binder_cli_init(FuBinderCli *self)
{
	static FwupdClientSyncImpl impl = {
	    .get_remotes = fu_binder_cli_sync_impl_get_remotes,
	    .get_remote_by_id = fu_binder_cli_sync_impl_get_remote_by_id,
	    .get_upgrades = fu_binder_cli_sync_impl_get_upgrades,
	    .get_releases = fu_binder_cli_sync_impl_get_upgrades,
	    .get_devices = fu_binder_cli_sync_impl_get_devices,
	    .get_devices_by_guid = fu_binder_cli_sync_impl_get_devices_by_guid,
	    .get_device_by_id = fu_binder_cli_sync_impl_get_device_by_id,
	    .get_history = fu_binder_cli_sync_impl_get_history,
	    .get_plugins = fu_binder_cli_sync_impl_get_plugins,
	    .connect = fu_binder_cli_sync_impl_connect,
	    .install = fu_binder_cli_sync_impl_install,
	    .set_feature_flags = fu_binder_cli_sync_impl_set_feature_flags,
	};
	fwupd_client_set_daemon_version(fu_cli_get_client(FU_CLI(self)), PACKAGE_VERSION);
	fwupd_client_set_sync_impl(fu_cli_get_client(FU_CLI(self)), &impl, self, NULL);
	fwupd_client_set_daemon_version(fu_cli_get_client(FU_CLI(self)), PACKAGE_VERSION);
	fwupd_client_set_user_agent_for_package(fu_cli_get_client(FU_CLI(self)),
						"fwupdbinder",
						PACKAGE_VERSION);
	fu_cli_add_arg_flag(FU_CLI(self), FU_CLI_ARG_FLAG_USES_DAEMON);
}

static void
fu_binder_cli_finalize(GObject *obj)
{
	FuBinderCli *self = FU_BINDER_CLI(obj);
	if (self->fwupd_binder != NULL)
		AIBinder_decStrong(self->fwupd_binder);
	G_OBJECT_CLASS(fu_binder_cli_parent_class)->finalize(obj);
}

static void
fu_binder_cli_class_init(FuBinderCliClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_binder_cli_finalize;
}

int
main(int argc, char *argv[])
{
	g_autoptr(FuBinderCli) self = g_object_new(FU_TYPE_BINDER_CLI, NULL);
	g_autoptr(GOptionContext) option_context = g_option_context_new(NULL);
	g_autofree gchar *cmd_descriptions = NULL;
	g_autoptr(GError) error = NULL;

	setlocale(LC_ALL, "");
	bindtextdomain(GETTEXT_PACKAGE, FWUPD_LOCALEDIR);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
	textdomain(GETTEXT_PACKAGE);
	g_set_prgname(fu_cli_get_prgname(argv[0]));

	/* ensure D-Bus errors are registered */
	(void)fwupd_error_quark();

	/* add commands */
	fu_cli_cmd_array_add_common(FU_CLI(self));

	/* get a list of the commands */
	cmd_descriptions = fu_cli_cmd_array_to_string(FU_CLI(self));
	g_option_context_set_summary(option_context, cmd_descriptions);
	g_option_context_set_description(
	    option_context,
	    /* TRANSLATORS: CLI description */
	    _("This tool allows an administrator to query and control the "
	      "fwupd daemon, allowing them to perform actions such as "
	      "installing or downgrading firmware."));

	/* TRANSLATORS: program name */
	g_set_application_name(_("Firmware Utility"));
	g_option_context_add_group(option_context, fu_cli_get_option_group(FU_CLI(self)));
	if (!g_option_context_parse(option_context, &argc, &argv, &error)) {
		fu_console_print(fu_cli_get_console(FU_CLI(self)),
				 "%s: %s",
				 /* TRANSLATORS: the user didn't read the man page */
				 _("Failed to parse arguments"),
				 error->message);
		return EXIT_FAILURE;
	}

	/* fail if daemon doesn't exist */
	self->fwupd_binder = fu_binder_cli_bridge_get_service_handle(&error);
	if (self->fwupd_binder == NULL) {
		fu_cli_print_error(FU_CLI(self), error);
		return EXIT_FAILURE;
	}

	/* hook up the listener for progress updates */
	if (!fu_binder_cli_bridge_setup_listener(self->fwupd_binder,
						 fu_cli_get_client(FU_CLI(self)),
						 &error)) {
		fu_cli_print_error(FU_CLI(self), error);
		return EXIT_FAILURE;
	}

	/* process command line arguments */
	return fu_cli_main(FU_CLI(self), argc, argv);
}
