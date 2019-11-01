/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

#define FU_TYPE_ROM (fu_rom_get_type ())
G_DECLARE_FINAL_TYPE (FuRom, fu_rom, FU, ROM, GObject)

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

gboolean	 fu_rom_load_file			(FuRom		*self,
							 GFile		*file,
							 FuRomLoadFlags	 flags,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fu_rom_load_data			(FuRom		*self,
							 guint8		*buffer,
							 gsize		 buffer_sz,
							 FuRomLoadFlags	 flags,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fu_rom_extract_all			(FuRom		*self,
							 const gchar	*path,
							 GError		**error);
FuRomKind	 fu_rom_get_kind			(FuRom		*self);
const gchar	*fu_rom_get_version			(FuRom		*self);
GBytes		*fu_rom_get_data			(FuRom		*self);
guint16		 fu_rom_get_vendor			(FuRom		*self);
guint16		 fu_rom_get_model			(FuRom		*self);
const gchar	*fu_rom_kind_to_string			(FuRomKind	 kind);
