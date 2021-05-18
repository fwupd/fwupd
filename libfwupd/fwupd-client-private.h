/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fwupd-client.h"

#ifdef HAVE_GIO_UNIX
#include <gio/gunixinputstream.h>
#endif

void		 fwupd_client_download_bytes2_async	(FwupdClient	*self,
							 GPtrArray	*urls,
							 FwupdClientDownloadFlags flags,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 callback_data);

#ifdef HAVE_GIO_UNIX
void		 fwupd_client_get_details_stream_async	(FwupdClient	*self,
							 GUnixInputStream *istr,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 callback_data);
void		 fwupd_client_install_stream_async	(FwupdClient	*self,
							 const gchar	*device_id,
							 GUnixInputStream *istr,
							 const gchar	*filename_hint,
							 FwupdInstallFlags install_flags,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 callback_data);
void		 fwupd_client_update_metadata_stream_async(FwupdClient	*self,
							 const gchar	*remote_id,
							 GUnixInputStream *istr,
							 GUnixInputStream *istr_sig,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 callback_data);
#endif
