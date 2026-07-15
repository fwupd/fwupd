/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-client-sync.h"

G_BEGIN_DECLS

gboolean
fwupd_client_sync_impl_activate(FwupdClient *self,
				const gchar *device_id,
				gpointer user_data,
				GCancellable *cancellable,
				GError **error);
gboolean
fwupd_client_sync_impl_connect(FwupdClient *self,
			       gpointer user_data,
			       GCancellable *cancellable,
			       GError **error);
gboolean
fwupd_client_sync_impl_emulation_load(FwupdClient *self,
				      const gchar *filename,
				      gpointer user_data,
				      GCancellable *cancellable,
				      GError **error);
gboolean
fwupd_client_sync_impl_emulation_save(FwupdClient *self,
				      const gchar *filename,
				      gpointer user_data,
				      GCancellable *cancellable,
				      GError **error);
GPtrArray *
fwupd_client_sync_impl_get_bios_settings(FwupdClient *self,
					 gpointer user_data,
					 GCancellable *cancellable,
					 GError **error);
GPtrArray *
fwupd_client_sync_impl_get_details(FwupdClient *self,
				   const gchar *filename,
				   gpointer user_data,
				   GCancellable *cancellable,
				   GError **error);
GPtrArray *
fwupd_client_sync_impl_get_devices(FwupdClient *self,
				   gpointer user_data,
				   GCancellable *cancellable,
				   GError **error);
FwupdDevice *
fwupd_client_sync_impl_get_device_by_id(FwupdClient *self,
					const gchar *device_id,
					gpointer user_data,
					GCancellable *cancellable,
					GError **error);
GPtrArray *
fwupd_client_sync_impl_get_devices_by_guid(FwupdClient *self,
					   const gchar *guid,
					   gpointer user_data,
					   GCancellable *cancellable,
					   GError **error);
GPtrArray *
fwupd_client_sync_impl_get_plugins(FwupdClient *self,
				   gpointer user_data,
				   GCancellable *cancellable,
				   GError **error);
GPtrArray *
fwupd_client_sync_impl_get_history(FwupdClient *self,
				   gpointer user_data,
				   GCancellable *cancellable,
				   GError **error);
GPtrArray *
fwupd_client_sync_impl_get_host_security_attrs(FwupdClient *self,
					       gpointer user_data,
					       GCancellable *cancellable,
					       GError **error);
GPtrArray *
fwupd_client_sync_impl_get_host_security_events(FwupdClient *self,
						guint limit,
						gpointer user_data,
						GCancellable *cancellable,
						GError **error);
GPtrArray *
fwupd_client_sync_impl_get_releases(FwupdClient *self,
				    const gchar *device_id,
				    gpointer user_data,
				    GCancellable *cancellable,
				    GError **error);
FwupdDevice *
fwupd_client_sync_impl_get_results(FwupdClient *self,
				   const gchar *device_id,
				   gpointer user_data,
				   GCancellable *cancellable,
				   GError **error);
GPtrArray *
fwupd_client_sync_impl_get_upgrades(FwupdClient *self,
				    const gchar *device_id,
				    gpointer user_data,
				    GCancellable *cancellable,
				    GError **error);
GPtrArray *
fwupd_client_sync_impl_get_remotes(FwupdClient *self,
				   gpointer user_data,
				   GCancellable *cancellable,
				   GError **error);
FwupdRemote *
fwupd_client_sync_impl_get_remote_by_id(FwupdClient *self,
					const gchar *remote_id,
					gpointer user_data,
					GCancellable *cancellable,
					GError **error);
GHashTable *
fwupd_client_sync_impl_get_report_metadata(FwupdClient *self,
					   gpointer user_data,
					   GCancellable *cancellable,
					   GError **error);
gboolean
fwupd_client_sync_impl_modify_bios_setting(FwupdClient *self,
					   GHashTable *settings,
					   gpointer user_data,
					   GCancellable *cancellable,
					   GError **error);
gboolean
fwupd_client_sync_impl_modify_config(FwupdClient *self,
				     const gchar *section,
				     const gchar *key,
				     const gchar *value,
				     gpointer user_data,
				     GCancellable *cancellable,
				     GError **error);
gboolean
fwupd_client_sync_impl_modify_remote(FwupdClient *self,
				     const gchar *remote_id,
				     const gchar *key,
				     const gchar *value,
				     gpointer user_data,
				     GCancellable *cancellable,
				     GError **error);
gboolean
fwupd_client_sync_impl_reset_config(FwupdClient *self,
				    const gchar *section,
				    gpointer user_data,
				    GCancellable *cancellable,
				    GError **error);
GPtrArray *
fwupd_client_sync_impl_search(FwupdClient *self,
			      const gchar *token,
			      gpointer user_data,
			      GCancellable *cancellable,
			      GError **error);
gboolean
fwupd_client_sync_impl_set_feature_flags(FwupdClient *self,
					 FwupdFeatureFlags feature_flags,
					 gpointer user_data,
					 GCancellable *cancellable,
					 GError **error);

G_END_DECLS
