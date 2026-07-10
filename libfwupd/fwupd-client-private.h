/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-client.h"

#ifdef HAVE_GIO_UNIX
#include <gio/gunixinputstream.h>
#endif

#if 0
	GPtrArray *(*xxxxxxxxxxxxxxxxxx)(FwupdClient *self,
				const gchar *token,
				  gpointer user_data,
				  GCancellable *cancellable,
				  GError **error);
GPtrArray *
fwupd_client_sync_impl_xxxxxxxxxxxxxxxxxx(FwupdClient *self,
				const gchar *token,
				  gpointer user_data,
				  GCancellable *cancellable,
				  GError **error);

GPtrArray *
fwupd_client_sync_impl_xxxxxxxxxxxxxxxxxx(FwupdClient *self,
				const gchar *token,
				  gpointer user_data,
				  GCancellable *cancellable,
				  GError **error)
{
	xxx
}

GPtrArray *
fwupd_client_sync_impl_xxxxxxxxxxxxxxxxxx(FwupdClient *self,
				const gchar *token,
				  gpointer user_data,
				  GCancellable *cancellable,
				  GError **error);

	    .xxxxxxxxxxxxxxxxxx = fwupd_client_sync_impl_xxxxxxxxxxxxxxxxxx,


	gpointer impl_userdata = NULL;
	const FwupdClientSyncImpl *impl = fwupd_client_get_sync_impl(self, &impl_userdata);

	if (impl->xxxxxxxxxxxxxxxxxx == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no FwupdClientSyncImpl->xxxxxxxxxxxxxxxxxx");
		return NULL;
	}
	return impl->xxxxxxxxxxxxxxxxxx(self, token, impl_userdata, cancellable, error);


static GPtrArray *
fu_tool_cli_sync_impl_xxxxxxxxxxxxxxxxxx(FwupdClient *client,
			 const gchar *token,
			 gpointer user_data,
			 GCancellable *cancellable,
			 GError **error)
{
	FuToolCli *self = (FuToolCli *)user_data;
	return fu_engine_xxxxxxxxxxxxxxxxxx(self->engine, token, error);
}

	    .xxxxxxxxxxxxxxxxxx = fu_tool_cli_sync_impl_xxxxxxxxxxxxxxxxxx,
#endif

/* allow overrides for synchronous clients that are not using DBus */
struct FwupdClientSyncImpl {
	gboolean (*activate)(FwupdClient *self,
			     const gchar *device_id,
			     gpointer user_data,
			     GCancellable *cancellable,
			     GError **error);
	gboolean (*connect)(FwupdClient *self,
			    gpointer user_data,
			    GCancellable *cancellable,
			    GError **error);
	GPtrArray *(*get_bios_settings)(FwupdClient *self,
					gpointer user_data,
					GCancellable *cancellable,
					GError **error);
	GPtrArray *(*get_devices)(FwupdClient *self,
				  gpointer user_data,
				  GCancellable *cancellable,
				  GError **error);
	GPtrArray *(*get_devices_by_guid)(FwupdClient *self,
					  const gchar *guid,
					  gpointer user_data,
					  GCancellable *cancellable,
					  GError **error);
	FwupdDevice *(*get_device_by_id)(FwupdClient *self,
					 const gchar *device_id,
					 gpointer user_data,
					 GCancellable *cancellable,
					 GError **error);
	GPtrArray *(*get_history)(FwupdClient *self,
				  gpointer user_data,
				  GCancellable *cancellable,
				  GError **error);
	GPtrArray *(*get_releases)(FwupdClient *self,
				   const gchar *device_id,
				   gpointer user_data,
				   GCancellable *cancellable,
				   GError **error);
	FwupdDevice *(*get_results)(FwupdClient *self,
				    const gchar *device_id,
				    gpointer user_data,
				    GCancellable *cancellable,
				    GError **error);
	GPtrArray *(*get_upgrades)(FwupdClient *self,
				   const gchar *device_id,
				   gpointer user_data,
				   GCancellable *cancellable,
				   GError **error);
	GPtrArray *(*get_plugins)(FwupdClient *self,
				  gpointer user_data,
				  GCancellable *cancellable,
				  GError **error);
	GPtrArray *(*get_remotes)(FwupdClient *self,
				  gpointer user_data,
				  GCancellable *cancellable,
				  GError **error);
	GHashTable *(*get_report_metadata)(FwupdClient *self,
					   gpointer user_data,
					   GCancellable *cancellable,
					   GError **error);
	FwupdRemote *(*get_remote_by_id)(FwupdClient *self,
					 const gchar *remote_id,
					 gpointer user_data,
					 GCancellable *cancellable,
					 GError **error);
	gboolean (*modify_bios_setting)(FwupdClient *self,
					GHashTable *settings,
					gpointer user_data,
					GCancellable *cancellable,
					GError **error);
	gboolean (*modify_config)(FwupdClient *self,
				  const gchar *section,
				  const gchar *key,
				  const gchar *value,
				  gpointer user_data,
				  GCancellable *cancellable,
				  GError **error);
	gboolean (*modify_remote)(FwupdClient *self,
				  const gchar *remote_id,
				  const gchar *key,
				  const gchar *value,
				  gpointer user_data,
				  GCancellable *cancellable,
				  GError **error);
	GPtrArray *(*search)(FwupdClient *self,
			     const gchar *token,
			     gpointer user_data,
			     GCancellable *cancellable,
			     GError **error);
};

const FwupdClientSyncImpl *
fwupd_client_get_sync_impl(FwupdClient *self, gpointer *impl_userdata) G_GNUC_NON_NULL(1);

void
fwupd_client_download_bytes2_async(FwupdClient *self,
				   GPtrArray *urls,
				   FwupdClientDownloadFlags flags,
				   GCancellable *cancellable,
				   GAsyncReadyCallback callback,
				   gpointer callback_data) G_GNUC_NON_NULL(1, 2);

#ifdef HAVE_GIO_UNIX
void
fwupd_client_get_details_stream_async(FwupdClient *self,
				      GUnixInputStream *istr,
				      GCancellable *cancellable,
				      GAsyncReadyCallback callback,
				      gpointer callback_data) G_GNUC_NON_NULL(1, 2);
void
fwupd_client_install_stream_async(FwupdClient *self,
				  const gchar *device_id,
				  GUnixInputStream *istr,
				  const gchar *filename_hint,
				  FwupdInstallFlags install_flags,
				  GCancellable *cancellable,
				  GAsyncReadyCallback callback,
				  gpointer callback_data) G_GNUC_NON_NULL(1, 2, 3);
void
fwupd_client_update_metadata_stream_async(FwupdClient *self,
					  const gchar *remote_id,
					  GUnixInputStream *istr,
					  GUnixInputStream *istr_sig,
					  GCancellable *cancellable,
					  GAsyncReadyCallback callback,
					  gpointer callback_data) G_GNUC_NON_NULL(1, 2, 3, 4);
#endif
