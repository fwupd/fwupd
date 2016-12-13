/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef __UNIFYING_DONGLE_H
#define __UNIFYING_DONGLE_H

#include <glib-object.h>
#include <gusb.h>

G_BEGIN_DECLS

#define UNIFYING_TYPE_DONGLE (unifying_dongle_get_type ())
G_DECLARE_DERIVABLE_TYPE (UnifyingDongle, unifying_dongle, UNIFYING, DONGLE, GObject)

struct _UnifyingDongleClass
{
	GObjectClass		parent_class;
};

typedef enum {
	UNIFYING_DONGLE_KIND_UNKNOWN,
	UNIFYING_DONGLE_KIND_RUNTIME,
	UNIFYING_DONGLE_KIND_BOOTLOADER_NORDIC,
	UNIFYING_DONGLE_KIND_BOOTLOADER_TEXAS,
	UNIFYING_DONGLE_KIND_LAST
} UnifyingDongleKind;

UnifyingDongle	*unifying_dongle_new			(GUsbDevice 	*usb_device);
UnifyingDongle	*unifying_dongle_emulated_new		(UnifyingDongleKind kind);

UnifyingDongleKind unifying_dongle_kind_from_string	(const gchar	*kind);
const gchar	*unifying_dongle_kind_to_string		(UnifyingDongleKind kind);

UnifyingDongleKind unifying_dongle_get_kind		(UnifyingDongle	*dongle);
const gchar	*unifying_dongle_get_version_fw		(UnifyingDongle	*dongle);
const gchar	*unifying_dongle_get_version_bl		(UnifyingDongle	*dongle);
const gchar	*unifying_dongle_get_guid		(UnifyingDongle	*dongle);
GUsbDevice	*unifying_dongle_get_usb_device		(UnifyingDongle	*dongle);

gboolean	 unifying_dongle_open			(UnifyingDongle	*dongle,
							 GError		**error);
gboolean	 unifying_dongle_detach			(UnifyingDongle	*dongle,
							 GError		**error);
gboolean	 unifying_dongle_attach			(UnifyingDongle	*dongle,
							 GError		**error);
gboolean	 unifying_dongle_close			(UnifyingDongle	*dongle,
							 GError		**error);
gboolean	 unifying_dongle_write_firmware		(UnifyingDongle	*dongle,
							 GBytes		*fw,
							 GFileProgressCallback progress_cb,
							 gpointer	 progress_data,
							 GError		**error);

G_END_DECLS

#endif /* __UNIFYING_DONGLE_H */
