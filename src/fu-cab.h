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

#ifndef __FU_CAB_H
#define __FU_CAB_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define FU_TYPE_CAB		(fu_cab_get_type ())
#define FU_CAB(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), FU_TYPE_CAB, FuCab))
#define FU_CAB_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), FU_TYPE_CAB, FuCabClass))
#define FU_IS_CAB(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), FU_TYPE_CAB))
#define FU_IS_CAB_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), FU_TYPE_CAB))
#define FU_CAB_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), FU_TYPE_CAB, FuCabClass))
#define FU_CAB_ERROR		fu_cab_error_quark()

typedef struct _FuCabPrivate	FuCabPrivate;
typedef struct _FuCab		FuCab;
typedef struct _FuCabClass	FuCabClass;

struct _FuCab
{
	 GObject		 parent;
	 FuCabPrivate		*priv;
};

struct _FuCabClass
{
	GObjectClass		 parent_class;
};

GType		 fu_cab_get_type			(void);
FuCab		*fu_cab_new				(void);

gboolean	 fu_cab_load_fd				(FuCab		*cab,
							 gint		 fd,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fu_cab_load_file			(FuCab		*cab,
							 GFile		*file,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fu_cab_extract_firmware		(FuCab		*cab,
							 GError		**error);
gboolean	 fu_cab_delete_temp_files		(FuCab		*cab,
							 GError		**error);
GInputStream	*fu_cab_get_stream			(FuCab		*cab);
const gchar	*fu_cab_get_guid			(FuCab		*cab);
const gchar	*fu_cab_get_version			(FuCab		*cab);
const gchar	*fu_cab_get_vendor			(FuCab		*cab);
const gchar	*fu_cab_get_summary			(FuCab		*cab);
const gchar	*fu_cab_get_name			(FuCab		*cab);
const gchar	*fu_cab_get_description			(FuCab		*cab);
const gchar	*fu_cab_get_license			(FuCab		*cab);
const gchar	*fu_cab_get_url_homepage		(FuCab		*cab);
const gchar	*fu_cab_get_filename_firmware		(FuCab		*cab);
guint64		 fu_cab_get_size			(FuCab		*cab);

G_END_DECLS

#endif /* __FU_CAB_H */

