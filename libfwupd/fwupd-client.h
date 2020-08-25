/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <gio/gio.h>

#include "fwupd-enums.h"
#include "fwupd-device.h"
#include "fwupd-remote.h"

G_BEGIN_DECLS

#define FWUPD_TYPE_CLIENT (fwupd_client_get_type ())
G_DECLARE_DERIVABLE_TYPE (FwupdClient, fwupd_client, FWUPD, CLIENT, GObject)

struct _FwupdClientClass
{
	GObjectClass		 parent_class;
	void			(*changed)		(FwupdClient	*client);
	void			(*status_changed)	(FwupdClient	*client,
							 FwupdStatus	 status);
	void			(*device_added)		(FwupdClient	*client,
							 FwupdDevice	*result);
	void			(*device_removed)	(FwupdClient	*client,
							 FwupdDevice	*result);
	void			(*device_changed)	(FwupdClient	*client,
							 FwupdDevice	*result);
	/*< private >*/
	void (*_fwupd_reserved1)	(void);
	void (*_fwupd_reserved2)	(void);
	void (*_fwupd_reserved3)	(void);
	void (*_fwupd_reserved4)	(void);
	void (*_fwupd_reserved5)	(void);
	void (*_fwupd_reserved6)	(void);
	void (*_fwupd_reserved7)	(void);
};

/**
 * FwupdClientDownloadFlags:
 * @FWUPD_CLIENT_DOWNLOAD_FLAG_NONE:		No flags set
 *
 * The options to use for downloading.
 **/
typedef enum {
	FWUPD_CLIENT_DOWNLOAD_FLAG_NONE			= 0,		/* Since: 1.4.5 */
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
	FWUPD_CLIENT_UPLOAD_FLAG_NONE			= 0,		/* Since: 1.4.5 */
	FWUPD_CLIENT_UPLOAD_FLAG_ALWAYS_MULTIPART	= 1 << 0,	/* Since: 1.4.5 */
	/*< private >*/
	FWUPD_CLIENT_UPLOAD_FLAG_LAST
} FwupdClientUploadFlags;

FwupdClient	*fwupd_client_new			(void);
gboolean	 fwupd_client_connect			(FwupdClient	*self,
							 GCancellable	*cancellable,
							 GError		**error);
GPtrArray	*fwupd_client_get_devices		(FwupdClient	*self,
							 GCancellable	*cancellable,
							 GError		**error);
GPtrArray	*fwupd_client_get_history		(FwupdClient	*self,
							 GCancellable	*cancellable,
							 GError		**error);
GPtrArray	*fwupd_client_get_releases		(FwupdClient	*self,
							 const gchar	*device_id,
							 GCancellable	*cancellable,
							 GError		**error);
GPtrArray	*fwupd_client_get_downgrades		(FwupdClient	*self,
							 const gchar	*device_id,
							 GCancellable	*cancellable,
							 GError		**error);
GPtrArray	*fwupd_client_get_upgrades		(FwupdClient	*self,
							 const gchar	*device_id,
							 GCancellable	*cancellable,
							 GError		**error);
GPtrArray	*fwupd_client_get_details		(FwupdClient	*self,
							 const gchar	*filename,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_verify			(FwupdClient	*self,
							 const gchar	*device_id,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_verify_update		(FwupdClient	*self,
							 const gchar	*device_id,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_unlock			(FwupdClient	*self,
							 const gchar	*device_id,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_modify_config		(FwupdClient	*self,
							 const gchar	*key,
							 const gchar	*value,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_activate			(FwupdClient	*self,
							 GCancellable	*cancellable,
							 const gchar	*device_id,
							 GError		**error);
gboolean	 fwupd_client_clear_results		(FwupdClient	*self,
							 const gchar	*device_id,
							 GCancellable	*cancellable,
							 GError		**error);
FwupdDevice	*fwupd_client_get_results		(FwupdClient	*self,
							 const gchar	*device_id,
							 GCancellable	*cancellable,
							 GError		**error);
GPtrArray	*fwupd_client_get_host_security_attrs	(FwupdClient	*self,
							 GCancellable	*cancellable,
							 GError		**error);
FwupdDevice	*fwupd_client_get_device_by_id		(FwupdClient	*self,
							 const gchar	*device_id,
							 GCancellable	*cancellable,
							 GError		**error);
GPtrArray	*fwupd_client_get_devices_by_guid	(FwupdClient	*self,
							 const gchar	*guid,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_install			(FwupdClient	*self,
							 const gchar	*device_id,
							 const gchar	*filename,
							 FwupdInstallFlags install_flags,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_install_bytes		(FwupdClient	*self,
							 const gchar	*device_id,
							 GBytes		*bytes,
							 FwupdInstallFlags install_flags,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_install_release		(FwupdClient	*self,
							 FwupdDevice	*device,
							 FwupdRelease	*release,
							 FwupdInstallFlags install_flags,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_update_metadata		(FwupdClient	*self,
							 const gchar	*remote_id,
							 const gchar	*metadata_fn,
							 const gchar	*signature_fn,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_update_metadata_bytes	(FwupdClient	*self,
							 const gchar	*remote_id,
							 GBytes		*metadata,
							 GBytes		*signature,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_refresh_remote		(FwupdClient	*self,
							 FwupdRemote	*remote,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_modify_remote		(FwupdClient	*self,
							 const gchar	*remote_id,
							 const gchar	*key,
							 const gchar	*value,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_modify_device		(FwupdClient	*self,
							 const gchar	*device_id,
							 const gchar	*key,
							 const gchar	*value,
							 GCancellable	*cancellable,
							 GError		**error);
GHashTable	*fwupd_client_get_report_metadata	(FwupdClient	*self,
							 GCancellable	*cancellable,
							 GError		**error);
FwupdStatus	 fwupd_client_get_status		(FwupdClient	*self);
gboolean	 fwupd_client_get_tainted		(FwupdClient	*self);
gboolean	 fwupd_client_get_daemon_interactive	(FwupdClient	*self);
guint		 fwupd_client_get_percentage		(FwupdClient	*self);
const gchar	*fwupd_client_get_daemon_version	(FwupdClient	*self);
const gchar	*fwupd_client_get_host_product		(FwupdClient	*self);
const gchar	*fwupd_client_get_host_machine_id	(FwupdClient	*self);
const gchar	*fwupd_client_get_host_security_id	(FwupdClient	*self);

GPtrArray	*fwupd_client_get_remotes		(FwupdClient	*self,
							 GCancellable	*cancellable,
							 GError		**error);
FwupdRemote	*fwupd_client_get_remote_by_id		(FwupdClient	*self,
							 const gchar	*remote_id,
							 GCancellable	*cancellable,
							 GError		**error);

gchar		**fwupd_client_get_approved_firmware	(FwupdClient	*self,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_set_approved_firmware	(FwupdClient	*self,
							 gchar		**checksums,
							 GCancellable	*cancellable,
							 GError		**error);
gchar		**fwupd_client_get_blocked_firmware	(FwupdClient	*self,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_set_blocked_firmware	(FwupdClient	*self,
							 gchar		**checksums,
							 GCancellable	*cancellable,
							 GError		**error);
gchar		*fwupd_client_self_sign			(FwupdClient	*self,
							 const gchar	*value,
							 FwupdSelfSignFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_set_feature_flags		(FwupdClient	*self,
							 FwupdFeatureFlags feature_flags,
							 GCancellable	*cancellable,
							 GError		**error);
void		 fwupd_client_set_user_agent		(FwupdClient	*self,
							 const gchar	*user_agent);
void		 fwupd_client_set_user_agent_for_package(FwupdClient	*self,
							 const gchar	*package_name,
							 const gchar	*package_version);
GBytes		*fwupd_client_download_bytes		(FwupdClient	*self,
							 const gchar	*url,
							 FwupdClientDownloadFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
GBytes		*fwupd_client_upload_bytes		(FwupdClient	*self,
							 const gchar	*url,
							 const gchar	*payload,
							 const gchar	*signature,
							 FwupdClientUploadFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_ensure_networking		(FwupdClient	*self,
							 GError		**error);

G_END_DECLS
