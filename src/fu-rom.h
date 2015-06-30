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

#ifndef __FU_ROM_H
#define __FU_ROM_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define FU_TYPE_ROM		(fu_rom_get_type ())
#define FU_ROM(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), FU_TYPE_ROM, FuRom))
#define FU_ROM_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), FU_TYPE_ROM, FuRomClass))
#define FU_IS_ROM(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), FU_TYPE_ROM))
#define FU_IS_ROM_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), FU_TYPE_ROM))
#define FU_ROM_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), FU_TYPE_ROM, FuRomClass))
#define FU_ROM_ERROR		fu_rom_error_quark()

typedef struct _FuRomPrivate	FuRomPrivate;
typedef struct _FuRom		FuRom;
typedef struct _FuRomClass	FuRomClass;

struct _FuRom
{
	 GObject		 parent;
	 FuRomPrivate		*priv;
};

struct _FuRomClass
{
	GObjectClass		 parent_class;
};

typedef enum {
	FU_ROM_KIND_UNKNOWN,
	FU_ROM_KIND_ATI,
	FU_ROM_KIND_NVIDIA,
	FU_ROM_KIND_LAST
} FuRomKind;

GType		 fu_rom_get_type			(void);
FuRom		*fu_rom_new				(void);

gboolean	 fu_rom_load_file			(FuRom		*rom,
							 GFile		*file,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fu_rom_generate_checksum		(FuRom		*rom,
							 GCancellable	*cancellable,
							 GError		**error);
FuRomKind	 fu_rom_get_kind			(FuRom		*rom);
const gchar	*fu_rom_get_version			(FuRom		*rom);
const gchar	*fu_rom_get_checksum			(FuRom		*rom);
const gchar	*fu_rom_kind_to_string			(FuRomKind	 kind);

G_END_DECLS

#endif /* __FU_ROM_H */

