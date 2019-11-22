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

FwupdClient	*fwupd_client_new			(void);
gboolean	 fwupd_client_connect			(FwupdClient	*client,
							 GCancellable	*cancellable,
							 GError		**error);
GPtrArray	*fwupd_client_get_devices		(FwupdClient	*client,
							 GCancellable	*cancellable,
							 GError		**error);
GPtrArray	*fwupd_client_get_history		(FwupdClient	*client,
							 GCancellable	*cancellable,
							 GError		**error);
GPtrArray	*fwupd_client_get_releases		(FwupdClient	*client,
							 const gchar	*device_id,
							 GCancellable	*cancellable,
							 GError		**error);
GPtrArray	*fwupd_client_get_downgrades		(FwupdClient	*client,
							 const gchar	*device_id,
							 GCancellable	*cancellable,
							 GError		**error);
GPtrArray	*fwupd_client_get_upgrades		(FwupdClient	*client,
							 const gchar	*device_id,
							 GCancellable	*cancellable,
							 GError		**error);
GPtrArray	*fwupd_client_get_details		(FwupdClient	*client,
							 const gchar	*filename,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_verify			(FwupdClient	*client,
							 const gchar	*device_id,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_verify_update		(FwupdClient	*client,
							 const gchar	*device_id,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_unlock			(FwupdClient	*client,
							 const gchar	*device_id,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_modify_config		(FwupdClient	*client,
							 const gchar	*key,
							 const gchar	*value,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_activate			(FwupdClient	*client,
							 GCancellable	*cancellable,
							 const gchar	*device_id,
							 GError		**error);
gboolean	 fwupd_client_clear_results		(FwupdClient	*client,
							 const gchar	*device_id,
							 GCancellable	*cancellable,
							 GError		**error);
FwupdDevice	*fwupd_client_get_results		(FwupdClient	*client,
							 const gchar	*device_id,
							 GCancellable	*cancellable,
							 GError		**error);
FwupdDevice	*fwupd_client_get_device_by_id		(FwupdClient	*client,
							 const gchar	*device_id,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_install			(FwupdClient	*client,
							 const gchar	*device_id,
							 const gchar	*filename,
							 FwupdInstallFlags install_flags,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_update_metadata		(FwupdClient	*client,
							 const gchar	*remote_id,
							 const gchar	*metadata_fn,
							 const gchar	*signature_fn,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_modify_remote		(FwupdClient	*client,
							 const gchar	*remote_id,
							 const gchar	*key,
							 const gchar	*value,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_modify_device		(FwupdClient	*client,
							 const gchar	*device_id,
							 const gchar	*key,
							 const gchar	*value,
							 GCancellable	*cancellable,
							 GError		**error);
FwupdStatus	 fwupd_client_get_status		(FwupdClient	*client);
gboolean	 fwupd_client_get_tainted		(FwupdClient	*client);
gboolean	 fwupd_client_get_daemon_interactive	(FwupdClient	*client);
guint		 fwupd_client_get_percentage		(FwupdClient	*client);
const gchar	*fwupd_client_get_daemon_version	(FwupdClient	*client);
const gchar	*fwupd_client_get_host_product		(FwupdClient	*client);
const gchar	*fwupd_client_get_host_machine_id	(FwupdClient	*client);

GPtrArray	*fwupd_client_get_remotes		(FwupdClient	*client,
							 GCancellable	*cancellable,
							 GError		**error);
FwupdRemote	*fwupd_client_get_remote_by_id		(FwupdClient	*client,
							 const gchar	*remote_id,
							 GCancellable	*cancellable,
							 GError		**error);

gchar		**fwupd_client_get_approved_firmware	(FwupdClient	*client,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_set_approved_firmware	(FwupdClient	*client,
							 gchar		**checksums,
							 GCancellable	*cancellable,
							 GError		**error);
gchar		*fwupd_client_self_sign			(FwupdClient	*client,
							 const gchar	*value,
							 FwupdSelfSignFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);

G_END_DECLS
