/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fwupd-client.h"

G_BEGIN_DECLS

gboolean
fwupd_client_connect(FwupdClient *self,
		     GCancellable *cancellable,
		     GError **error) G_GNUC_WARN_UNUSED_RESULT;
GPtrArray *
fwupd_client_get_devices(FwupdClient *self,
			 GCancellable *cancellable,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT;
GPtrArray *
fwupd_client_get_plugins(FwupdClient *self,
			 GCancellable *cancellable,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT;
GPtrArray *
fwupd_client_get_history(FwupdClient *self,
			 GCancellable *cancellable,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT;
GPtrArray *
fwupd_client_get_releases(FwupdClient *self,
			  const gchar *device_id,
			  GCancellable *cancellable,
			  GError **error) G_GNUC_WARN_UNUSED_RESULT;
GPtrArray *
fwupd_client_get_downgrades(FwupdClient *self,
			    const gchar *device_id,
			    GCancellable *cancellable,
			    GError **error) G_GNUC_WARN_UNUSED_RESULT;
GPtrArray *
fwupd_client_get_upgrades(FwupdClient *self,
			  const gchar *device_id,
			  GCancellable *cancellable,
			  GError **error) G_GNUC_WARN_UNUSED_RESULT;
GPtrArray *
fwupd_client_get_details(FwupdClient *self,
			 const gchar *filename,
			 GCancellable *cancellable,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT;
GPtrArray *
fwupd_client_get_details_bytes(FwupdClient *self,
			       GBytes *bytes,
			       GCancellable *cancellable,
			       GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fwupd_client_verify(FwupdClient *self,
		    const gchar *device_id,
		    GCancellable *cancellable,
		    GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fwupd_client_verify_update(FwupdClient *self,
			   const gchar *device_id,
			   GCancellable *cancellable,
			   GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fwupd_client_unlock(FwupdClient *self,
		    const gchar *device_id,
		    GCancellable *cancellable,
		    GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fwupd_client_modify_config(FwupdClient *self,
			   const gchar *key,
			   const gchar *value,
			   GCancellable *cancellable,
			   GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fwupd_client_activate(FwupdClient *self,
		      GCancellable *cancellable,
		      const gchar *device_id,
		      GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fwupd_client_clear_results(FwupdClient *self,
			   const gchar *device_id,
			   GCancellable *cancellable,
			   GError **error) G_GNUC_WARN_UNUSED_RESULT;
FwupdDevice *
fwupd_client_get_results(FwupdClient *self,
			 const gchar *device_id,
			 GCancellable *cancellable,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fwupd_client_modify_bios_setting(FwupdClient *self,
				 GHashTable *settings,
				 GCancellable *cancellable,
				 GError **error);
GPtrArray *
fwupd_client_get_bios_settings(FwupdClient *self,
			       GCancellable *cancellable,
			       GError **error) G_GNUC_WARN_UNUSED_RESULT;
GPtrArray *
fwupd_client_get_host_security_attrs(FwupdClient *self,
				     GCancellable *cancellable,
				     GError **error) G_GNUC_WARN_UNUSED_RESULT;
GPtrArray *
fwupd_client_get_host_security_events(FwupdClient *self,
				      guint limit,
				      GCancellable *cancellable,
				      GError **error) G_GNUC_WARN_UNUSED_RESULT;
FwupdDevice *
fwupd_client_get_device_by_id(FwupdClient *self,
			      const gchar *device_id,
			      GCancellable *cancellable,
			      GError **error) G_GNUC_WARN_UNUSED_RESULT;
GPtrArray *
fwupd_client_get_devices_by_guid(FwupdClient *self,
				 const gchar *guid,
				 GCancellable *cancellable,
				 GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fwupd_client_install(FwupdClient *self,
		     const gchar *device_id,
		     const gchar *filename,
		     FwupdInstallFlags install_flags,
		     GCancellable *cancellable,
		     GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fwupd_client_install_bytes(FwupdClient *self,
			   const gchar *device_id,
			   GBytes *bytes,
			   FwupdInstallFlags install_flags,
			   GCancellable *cancellable,
			   GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fwupd_client_install_release(FwupdClient *self,
			     FwupdDevice *device,
			     FwupdRelease *release,
			     FwupdInstallFlags install_flags,
			     GCancellable *cancellable,
			     GError **error) G_DEPRECATED_FOR(fwupd_client_install_release2);
gboolean
fwupd_client_install_release2(FwupdClient *self,
			      FwupdDevice *device,
			      FwupdRelease *release,
			      FwupdInstallFlags install_flags,
			      FwupdClientDownloadFlags download_flags,
			      GCancellable *cancellable,
			      GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fwupd_client_update_metadata(FwupdClient *self,
			     const gchar *remote_id,
			     const gchar *metadata_fn,
			     const gchar *signature_fn,
			     GCancellable *cancellable,
			     GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fwupd_client_update_metadata_bytes(FwupdClient *self,
				   const gchar *remote_id,
				   GBytes *metadata,
				   GBytes *signature,
				   GCancellable *cancellable,
				   GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fwupd_client_refresh_remote(FwupdClient *self,
			    FwupdRemote *remote,
			    GCancellable *cancellable,
			    GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fwupd_client_modify_remote(FwupdClient *self,
			   const gchar *remote_id,
			   const gchar *key,
			   const gchar *value,
			   GCancellable *cancellable,
			   GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fwupd_client_modify_device(FwupdClient *self,
			   const gchar *device_id,
			   const gchar *key,
			   const gchar *value,
			   GCancellable *cancellable,
			   GError **error) G_GNUC_WARN_UNUSED_RESULT;
GHashTable *
fwupd_client_get_report_metadata(FwupdClient *self,
				 GCancellable *cancellable,
				 GError **error) G_GNUC_WARN_UNUSED_RESULT;
GPtrArray *
fwupd_client_get_remotes(FwupdClient *self,
			 GCancellable *cancellable,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT;
FwupdRemote *
fwupd_client_get_remote_by_id(FwupdClient *self,
			      const gchar *remote_id,
			      GCancellable *cancellable,
			      GError **error) G_GNUC_WARN_UNUSED_RESULT;
gchar **
fwupd_client_get_approved_firmware(FwupdClient *self,
				   GCancellable *cancellable,
				   GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fwupd_client_set_approved_firmware(FwupdClient *self,
				   gchar **checksums,
				   GCancellable *cancellable,
				   GError **error) G_GNUC_WARN_UNUSED_RESULT;
gchar **
fwupd_client_get_blocked_firmware(FwupdClient *self,
				  GCancellable *cancellable,
				  GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fwupd_client_set_blocked_firmware(FwupdClient *self,
				  gchar **checksums,
				  GCancellable *cancellable,
				  GError **error) G_GNUC_WARN_UNUSED_RESULT;
gchar *
fwupd_client_self_sign(FwupdClient *self,
		       const gchar *value,
		       FwupdSelfSignFlags flags,
		       GCancellable *cancellable,
		       GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fwupd_client_set_feature_flags(FwupdClient *self,
			       FwupdFeatureFlags feature_flags,
			       GCancellable *cancellable,
			       GError **error) G_GNUC_WARN_UNUSED_RESULT;
GBytes *
fwupd_client_download_bytes(FwupdClient *self,
			    const gchar *url,
			    FwupdClientDownloadFlags flags,
			    GCancellable *cancellable,
			    GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fwupd_client_download_file(FwupdClient *self,
			   const gchar *url,
			   GFile *file,
			   FwupdClientDownloadFlags flags,
			   GCancellable *cancellable,
			   GError **error) G_GNUC_WARN_UNUSED_RESULT;
GBytes *
fwupd_client_upload_bytes(FwupdClient *self,
			  const gchar *url,
			  const gchar *payload,
			  const gchar *signature,
			  FwupdClientUploadFlags flags,
			  GCancellable *cancellable,
			  GError **error) G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS
