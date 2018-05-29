/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __DFU_FORMAT_XTEA_H
#define __DFU_FORMAT_XTEA_H

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

#endif /* __DFU_FORMAT_XTEA_H */
