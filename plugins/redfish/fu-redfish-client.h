 /*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

#define REDFISH_TYPE_CLIENT (fu_redfish_client_get_type ())

G_DECLARE_FINAL_TYPE (FuRedfishClient, fu_redfish_client, FU, REDFISH_CLIENT, GObject)

FuRedfishClient	*fu_redfish_client_new		(void);
void		 fu_redfish_client_set_hostname	(FuRedfishClient	*self,
						 const gchar		*hostname);
void		 fu_redfish_client_set_username	(FuRedfishClient	*self,
						 const gchar		*username);
void		 fu_redfish_client_set_password	(FuRedfishClient	*self,
						 const gchar		*password);
void		 fu_redfish_client_set_port	(FuRedfishClient	*self,
						 guint			 port);
void		 fu_redfish_client_set_https	(FuRedfishClient	*self,
						 gboolean		 use_https);
void		 fu_redfish_client_set_cacheck	(FuRedfishClient	*self,
						 gboolean		 cacheck);
gboolean	 fu_redfish_client_update       (FuRedfishClient	*self,
						 FuDevice		*device,
						 GBytes			*blob_fw,
						 GError			**error);
gboolean	 fu_redfish_client_setup	(FuRedfishClient	*self,
						 GBytes			*smbios_table,
						 GError			**error);
gboolean	 fu_redfish_client_coldplug	(FuRedfishClient	*self,
						 GError			**error);
GPtrArray	*fu_redfish_client_get_devices	(FuRedfishClient	*self);
