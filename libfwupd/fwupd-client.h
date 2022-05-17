/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>
#include <glib-object.h>

#include "fwupd-device.h"
#include "fwupd-enums.h"
#include "fwupd-plugin.h"
#include "fwupd-remote.h"
#include "fwupd-request.h"

G_BEGIN_DECLS

#define FWUPD_TYPE_CLIENT (fwupd_client_get_type())
G_DECLARE_DERIVABLE_TYPE(FwupdClient, fwupd_client, FWUPD, CLIENT, GObject)

struct _FwupdClientClass {
	GObjectClass parent_class;
	void (*changed)(FwupdClient *client);
	void (*status_changed)(FwupdClient *client, FwupdStatus status);
	void (*device_added)(FwupdClient *client, FwupdDevice *result);
	void (*device_removed)(FwupdClient *client, FwupdDevice *result);
	void (*device_changed)(FwupdClient *client, FwupdDevice *result);
	void (*device_request)(FwupdClient *client, FwupdRequest *request);
	/*< private >*/
	void (*_fwupd_reserved1)(void);
	void (*_fwupd_reserved2)(void);
	void (*_fwupd_reserved3)(void);
	void (*_fwupd_reserved4)(void);
	void (*_fwupd_reserved5)(void);
	void (*_fwupd_reserved6)(void);
};

/**
 * FwupdClientDownloadFlags:
 * @FWUPD_CLIENT_DOWNLOAD_FLAG_NONE:		No flags set
 * @FWUPD_CLIENT_DOWNLOAD_FLAG_ONLY_IPFS:	Only use IPFS when downloading URIs
 *
 * The options to use for downloading.
 **/
typedef enum {
	FWUPD_CLIENT_DOWNLOAD_FLAG_NONE = 0,	       /* Since: 1.4.5 */
	FWUPD_CLIENT_DOWNLOAD_FLAG_ONLY_IPFS = 1 << 0, /* Since: 1.5.6 */
	/*< private >*/
	FWUPD_CLIENT_DOWNLOAD_FLAG_LAST
} FwupdClientDownloadFlags;

/**
 * FwupdClientUploadFlags:
 * @FWUPD_CLIENT_UPLOAD_FLAG_NONE:		No flags set
 * @FWUPD_CLIENT_UPLOAD_FLAG_ALWAYS_MULTIPART:	Always use multipart/form-data
 *
 * The options to use for uploading.
 **/
typedef enum {
	FWUPD_CLIENT_UPLOAD_FLAG_NONE = 0,		    /* Since: 1.4.5 */
	FWUPD_CLIENT_UPLOAD_FLAG_ALWAYS_MULTIPART = 1 << 0, /* Since: 1.4.5 */
	/*< private >*/
	FWUPD_CLIENT_UPLOAD_FLAG_LAST
} FwupdClientUploadFlags;

FwupdClient *
fwupd_client_new(void);
GMainContext *
fwupd_client_get_main_context(FwupdClient *self);
void
fwupd_client_set_main_context(FwupdClient *self, GMainContext *main_ctx);
void
fwupd_client_connect_async(FwupdClient *self,
			   GCancellable *cancellable,
			   GAsyncReadyCallback callback,
			   gpointer callback_data);
gboolean
fwupd_client_connect_finish(FwupdClient *self,
			    GAsyncResult *res,
			    GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fwupd_client_disconnect(FwupdClient *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_get_devices_async(FwupdClient *self,
			       GCancellable *cancellable,
			       GAsyncReadyCallback callback,
			       gpointer callback_data);
GPtrArray *
fwupd_client_get_devices_finish(FwupdClient *self,
				GAsyncResult *res,
				GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_get_plugins_async(FwupdClient *self,
			       GCancellable *cancellable,
			       GAsyncReadyCallback callback,
			       gpointer callback_data);
GPtrArray *
fwupd_client_get_plugins_finish(FwupdClient *self,
				GAsyncResult *res,
				GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_get_history_async(FwupdClient *self,
			       GCancellable *cancellable,
			       GAsyncReadyCallback callback,
			       gpointer callback_data);
GPtrArray *
fwupd_client_get_history_finish(FwupdClient *self,
				GAsyncResult *res,
				GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_get_releases_async(FwupdClient *self,
				const gchar *device_id,
				GCancellable *cancellable,
				GAsyncReadyCallback callback,
				gpointer callback_data);
GPtrArray *
fwupd_client_get_releases_finish(FwupdClient *self,
				 GAsyncResult *res,
				 GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_get_downgrades_async(FwupdClient *self,
				  const gchar *device_id,
				  GCancellable *cancellable,
				  GAsyncReadyCallback callback,
				  gpointer callback_data);
GPtrArray *
fwupd_client_get_downgrades_finish(FwupdClient *self,
				   GAsyncResult *res,
				   GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_get_upgrades_async(FwupdClient *self,
				const gchar *device_id,
				GCancellable *cancellable,
				GAsyncReadyCallback callback,
				gpointer callback_data);
GPtrArray *
fwupd_client_get_upgrades_finish(FwupdClient *self,
				 GAsyncResult *res,
				 GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_get_details_bytes_async(FwupdClient *self,
				     GBytes *bytes,
				     GCancellable *cancellable,
				     GAsyncReadyCallback callback,
				     gpointer callback_data);
GPtrArray *
fwupd_client_get_details_bytes_finish(FwupdClient *self,
				      GAsyncResult *res,
				      GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_verify_async(FwupdClient *self,
			  const gchar *device_id,
			  GCancellable *cancellable,
			  GAsyncReadyCallback callback,
			  gpointer callback_data);
gboolean
fwupd_client_verify_finish(FwupdClient *self,
			   GAsyncResult *res,
			   GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_verify_update_async(FwupdClient *self,
				 const gchar *device_id,
				 GCancellable *cancellable,
				 GAsyncReadyCallback callback,
				 gpointer callback_data);
gboolean
fwupd_client_verify_update_finish(FwupdClient *self,
				  GAsyncResult *res,
				  GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_unlock_async(FwupdClient *self,
			  const gchar *device_id,
			  GCancellable *cancellable,
			  GAsyncReadyCallback callback,
			  gpointer callback_data);
gboolean
fwupd_client_unlock_finish(FwupdClient *self,
			   GAsyncResult *res,
			   GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_modify_config_async(FwupdClient *self,
				 const gchar *key,
				 const gchar *value,
				 GCancellable *cancellable,
				 GAsyncReadyCallback callback,
				 gpointer callback_data);
gboolean
fwupd_client_modify_config_finish(FwupdClient *self,
				  GAsyncResult *res,
				  GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_activate_async(FwupdClient *self,
			    const gchar *device_id,
			    GCancellable *cancellable,
			    GAsyncReadyCallback callback,
			    gpointer callback_data);
gboolean
fwupd_client_activate_finish(FwupdClient *self,
			     GAsyncResult *res,
			     GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_clear_results_async(FwupdClient *self,
				 const gchar *device_id,
				 GCancellable *cancellable,
				 GAsyncReadyCallback callback,
				 gpointer callback_data);
gboolean
fwupd_client_clear_results_finish(FwupdClient *self,
				  GAsyncResult *res,
				  GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_get_results_async(FwupdClient *self,
			       const gchar *device_id,
			       GCancellable *cancellable,
			       GAsyncReadyCallback callback,
			       gpointer callback_data);
FwupdDevice *
fwupd_client_get_results_finish(FwupdClient *self,
				GAsyncResult *res,
				GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_get_host_security_attrs_async(FwupdClient *self,
					   GCancellable *cancellable,
					   GAsyncReadyCallback callback,
					   gpointer callback_data);
GPtrArray *
fwupd_client_get_host_security_attrs_finish(FwupdClient *self,
					    GAsyncResult *res,
					    GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_get_host_security_events_async(FwupdClient *self,
					    guint limit,
					    GCancellable *cancellable,
					    GAsyncReadyCallback callback,
					    gpointer callback_data);
GPtrArray *
fwupd_client_get_host_security_events_finish(FwupdClient *self,
					     GAsyncResult *res,
					     GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_get_device_by_id_async(FwupdClient *self,
				    const gchar *device_id,
				    GCancellable *cancellable,
				    GAsyncReadyCallback callback,
				    gpointer callback_data);
FwupdDevice *
fwupd_client_get_device_by_id_finish(FwupdClient *self,
				     GAsyncResult *res,
				     GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_get_devices_by_guid_async(FwupdClient *self,
				       const gchar *guid,
				       GCancellable *cancellable,
				       GAsyncReadyCallback callback,
				       gpointer callback_data);
GPtrArray *
fwupd_client_get_devices_by_guid_finish(FwupdClient *self,
					GAsyncResult *res,
					GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_install_async(FwupdClient *self,
			   const gchar *device_id,
			   const gchar *filename,
			   FwupdInstallFlags install_flags,
			   GCancellable *cancellable,
			   GAsyncReadyCallback callback,
			   gpointer callback_data);
gboolean
fwupd_client_install_finish(FwupdClient *self,
			    GAsyncResult *res,
			    GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_install_bytes_async(FwupdClient *self,
				 const gchar *device_id,
				 GBytes *bytes,
				 FwupdInstallFlags install_flags,
				 GCancellable *cancellable,
				 GAsyncReadyCallback callback,
				 gpointer callback_data);
gboolean
fwupd_client_install_bytes_finish(FwupdClient *self,
				  GAsyncResult *res,
				  GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_install_release_async(FwupdClient *self,
				   FwupdDevice *device,
				   FwupdRelease *release,
				   FwupdInstallFlags install_flags,
				   GCancellable *cancellable,
				   GAsyncReadyCallback callback,
				   gpointer callback_data)
    G_DEPRECATED_FOR(fwupd_client_install_release2_async);
void
fwupd_client_install_release2_async(FwupdClient *self,
				    FwupdDevice *device,
				    FwupdRelease *release,
				    FwupdInstallFlags install_flags,
				    FwupdClientDownloadFlags download_flags,
				    GCancellable *cancellable,
				    GAsyncReadyCallback callback,
				    gpointer callback_data);
gboolean
fwupd_client_install_release_finish(FwupdClient *self,
				    GAsyncResult *res,
				    GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_update_metadata_bytes_async(FwupdClient *self,
					 const gchar *remote_id,
					 GBytes *metadata,
					 GBytes *signature,
					 GCancellable *cancellable,
					 GAsyncReadyCallback callback,
					 gpointer callback_data);
gboolean
fwupd_client_update_metadata_bytes_finish(FwupdClient *self,
					  GAsyncResult *res,
					  GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_refresh_remote_async(FwupdClient *self,
				  FwupdRemote *remote,
				  GCancellable *cancellable,
				  GAsyncReadyCallback callback,
				  gpointer callback_data);
gboolean
fwupd_client_refresh_remote_finish(FwupdClient *self,
				   GAsyncResult *res,
				   GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_modify_remote_async(FwupdClient *self,
				 const gchar *remote_id,
				 const gchar *key,
				 const gchar *value,
				 GCancellable *cancellable,
				 GAsyncReadyCallback callback,
				 gpointer callback_data);
gboolean
fwupd_client_modify_remote_finish(FwupdClient *self,
				  GAsyncResult *res,
				  GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_modify_device_async(FwupdClient *self,
				 const gchar *device_id,
				 const gchar *key,
				 const gchar *value,
				 GCancellable *cancellable,
				 GAsyncReadyCallback callback,
				 gpointer callback_data);
gboolean
fwupd_client_modify_device_finish(FwupdClient *self,
				  GAsyncResult *res,
				  GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_get_report_metadata_async(FwupdClient *self,
				       GCancellable *cancellable,
				       GAsyncReadyCallback callback,
				       gpointer callback_data);
GHashTable *
fwupd_client_get_report_metadata_finish(FwupdClient *self,
					GAsyncResult *res,
					GError **error) G_GNUC_WARN_UNUSED_RESULT;

FwupdStatus
fwupd_client_get_status(FwupdClient *self);
gboolean
fwupd_client_get_tainted(FwupdClient *self);
gboolean
fwupd_client_get_only_trusted(FwupdClient *self);
gboolean
fwupd_client_get_daemon_interactive(FwupdClient *self);
guint
fwupd_client_get_percentage(FwupdClient *self);
const gchar *
fwupd_client_get_daemon_version(FwupdClient *self);
const gchar *
fwupd_client_get_host_bkc(FwupdClient *self);
const gchar *
fwupd_client_get_host_product(FwupdClient *self);
const gchar *
fwupd_client_get_host_machine_id(FwupdClient *self);
const gchar *
fwupd_client_get_host_security_id(FwupdClient *self);
guint32
fwupd_client_get_battery_level(FwupdClient *self);
guint32
fwupd_client_get_battery_threshold(FwupdClient *self);

void
fwupd_client_get_remotes_async(FwupdClient *self,
			       GCancellable *cancellable,
			       GAsyncReadyCallback callback,
			       gpointer callback_data);
GPtrArray *
fwupd_client_get_remotes_finish(FwupdClient *self,
				GAsyncResult *res,
				GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_get_remote_by_id_async(FwupdClient *self,
				    const gchar *remote_id,
				    GCancellable *cancellable,
				    GAsyncReadyCallback callback,
				    gpointer callback_data);
FwupdRemote *
fwupd_client_get_remote_by_id_finish(FwupdClient *self,
				     GAsyncResult *res,
				     GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_get_approved_firmware_async(FwupdClient *self,
					 GCancellable *cancellable,
					 GAsyncReadyCallback callback,
					 gpointer callback_data);
GPtrArray *
fwupd_client_get_approved_firmware_finish(FwupdClient *self,
					  GAsyncResult *res,
					  GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_set_approved_firmware_async(FwupdClient *self,
					 GPtrArray *checksums,
					 GCancellable *cancellable,
					 GAsyncReadyCallback callback,
					 gpointer callback_data);
gboolean
fwupd_client_set_approved_firmware_finish(FwupdClient *self,
					  GAsyncResult *res,
					  GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_get_blocked_firmware_async(FwupdClient *self,
					GCancellable *cancellable,
					GAsyncReadyCallback callback,
					gpointer callback_data);
GPtrArray *
fwupd_client_get_blocked_firmware_finish(FwupdClient *self,
					 GAsyncResult *res,
					 GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_set_blocked_firmware_async(FwupdClient *self,
					GPtrArray *checksums,
					GCancellable *cancellable,
					GAsyncReadyCallback callback,
					gpointer callback_data);
gboolean
fwupd_client_set_blocked_firmware_finish(FwupdClient *self,
					 GAsyncResult *res,
					 GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_self_sign_async(FwupdClient *self,
			     const gchar *value,
			     FwupdSelfSignFlags flags,
			     GCancellable *cancellable,
			     GAsyncReadyCallback callback,
			     gpointer callback_data);
gchar *
fwupd_client_self_sign_finish(FwupdClient *self,
			      GAsyncResult *res,
			      GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_set_feature_flags_async(FwupdClient *self,
				     FwupdFeatureFlags feature_flags,
				     GCancellable *cancellable,
				     GAsyncReadyCallback callback,
				     gpointer callback_data);
gboolean
fwupd_client_set_feature_flags_finish(FwupdClient *self,
				      GAsyncResult *res,
				      GError **error) G_GNUC_WARN_UNUSED_RESULT;
const gchar *
fwupd_client_get_user_agent(FwupdClient *self);
void
fwupd_client_set_user_agent(FwupdClient *self, const gchar *user_agent);
void
fwupd_client_set_user_agent_for_package(FwupdClient *self,
					const gchar *package_name,
					const gchar *package_version);
void
fwupd_client_download_bytes_async(FwupdClient *self,
				  const gchar *url,
				  FwupdClientDownloadFlags flags,
				  GCancellable *cancellable,
				  GAsyncReadyCallback callback,
				  gpointer callback_data);
GBytes *
fwupd_client_download_bytes_finish(FwupdClient *self,
				   GAsyncResult *res,
				   GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_upload_bytes_async(FwupdClient *self,
				const gchar *url,
				const gchar *payload,
				const gchar *signature,
				FwupdClientUploadFlags flags,
				GCancellable *cancellable,
				GAsyncReadyCallback callback,
				gpointer callback_data);
GBytes *
fwupd_client_upload_bytes_finish(FwupdClient *self,
				 GAsyncResult *res,
				 GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fwupd_client_ensure_networking(FwupdClient *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_client_add_hint(FwupdClient *self, const gchar *key, const gchar *value);

G_END_DECLS
