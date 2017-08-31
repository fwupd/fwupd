 /* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useredfishl,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __REDFISH_CLIENT_H
#define __REDFISH_CLIENT_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define REDFISH_TYPE_CLIENT (redfish_client_get_type ())

G_DECLARE_FINAL_TYPE (RedfishClient, redfish_client, REDFISH, CLIENT, GObject)

RedfishClient	*redfish_client_new		(void);
void		 redfish_client_set_hostname	(RedfishClient	*self,
						 const gchar	*hostname);
void		 redfish_client_set_username	(RedfishClient	*self,
						 const gchar	*username);
void		 redfish_client_set_password	(RedfishClient	*self,
						 const gchar	*password);
void		 redfish_client_set_port	(RedfishClient	*self,
						 guint		 port);
gboolean	 redfish_client_setup		(RedfishClient	*self,
						 GBytes		*smbios_table,
						 GError		**error);
gboolean	 redfish_client_coldplug	(RedfishClient	*self,
						 GError		**error);
GPtrArray	*redfish_client_get_devices	(RedfishClient	*self);

G_END_DECLS

#endif /* __REDFISH_CLIENT_H */

/* vim: set noexpandtab: */
