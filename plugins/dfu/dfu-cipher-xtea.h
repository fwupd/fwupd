/*
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

gboolean		 dfu_cipher_encrypt_xtea	(const gchar	*key,
							 guint8		*data,
							 guint32	 length,
							 GError		**error);
gboolean		 dfu_cipher_decrypt_xtea	(const gchar	*key,
							 guint8		*data,
							 guint32	 length,
							 GError		**error);

G_END_DECLS
