/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __FU_KEYRING_H
#define __FU_KEYRING_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define FU_TYPE_KEYRING (fu_keyring_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuKeyring, fu_keyring, FU, KEYRING, GObject)

struct _FuKeyringClass
{
	GObjectClass		 parent_class;
};

FuKeyring	*fu_keyring_new				(void);

gboolean	 fu_keyring_add_public_keys		(FuKeyring	*keyring,
							 const gchar	*dirname,
							 GError		**error);
gboolean	 fu_keyring_add_public_key		(FuKeyring	*keyring,
							 const gchar	*filename,
							 GError		**error);
gboolean	 fu_keyring_verify_file			(FuKeyring	*keyring,
							 const gchar	*filename,
							 const gchar	*signature,
							 GError		**error);
gboolean	 fu_keyring_verify_data			(FuKeyring	*keyring,
							 GBytes		*payload,
							 GBytes		*payload_signature,
							 GError		**error);

G_END_DECLS

#endif /* __FU_KEYRING_H */

