/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_ROM_H
#define __FU_ROM_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define FU_TYPE_ROM (fu_rom_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuRom, fu_rom, FU, ROM, GObject)

struct _FuRomClass
{
	GObjectClass		 parent_class;
};

typedef enum {
	FU_ROM_KIND_UNKNOWN,
	FU_ROM_KIND_ATI,
	FU_ROM_KIND_NVIDIA,
	FU_ROM_KIND_INTEL,
	FU_ROM_KIND_PCI,
	FU_ROM_KIND_LAST
} FuRomKind;

typedef enum {
	FU_ROM_LOAD_FLAG_NONE,
	FU_ROM_LOAD_FLAG_BLANK_PPID = 1,
	FU_ROM_LOAD_FLAG_LAST
} FuRomLoadFlags;

FuRom		*fu_rom_new				(void);

gboolean	 fu_rom_load_file			(FuRom		*rom,
							 GFile		*file,
							 FuRomLoadFlags	 flags,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fu_rom_load_data			(FuRom		*rom,
							 guint8		*buffer,
							 gsize		 buffer_sz,
							 FuRomLoadFlags	 flags,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fu_rom_extract_all			(FuRom		*rom,
							 const gchar	*path,
							 GError		**error);
FuRomKind	 fu_rom_get_kind			(FuRom		*rom);
const gchar	*fu_rom_get_version			(FuRom		*rom);
GPtrArray	*fu_rom_get_checksums			(FuRom		*rom);
const gchar	*fu_rom_get_guid			(FuRom		*rom);
guint16		 fu_rom_get_vendor			(FuRom		*rom);
guint16		 fu_rom_get_model			(FuRom		*rom);
const gchar	*fu_rom_kind_to_string			(FuRomKind	 kind);

G_END_DECLS

#endif /* __FU_ROM_H */

