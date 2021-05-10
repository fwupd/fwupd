/*
 * Copyright (C) 2021 Peter Marheine <pmarheine@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#pragma once

#include <glib-object.h>

#define FU_TYPE_FLASHROM_OPENER \
	(fu_flashrom_opener_get_type ())
G_DECLARE_FINAL_TYPE (FuFlashromOpener,
		      fu_flashrom_opener, FU,
		      FLASHROM_OPENER, GObject)

struct fu_flashrom_opener_layout_region {
	const gchar *name;
	gsize offset;
	gsize size;
};

enum fu_flashrom_opener_layout_kind {
	FLASHROM_OPENER_LAYOUT_KIND_UNSET,
	FLASHROM_OPENER_LAYOUT_KIND_IFD,
	FLASHROM_OPENER_LAYOUT_KIND_STATIC,
};

union fu_flashrom_opener_layout_data {
	GPtrArray *static_regions;
};

FuFlashromOpener 	*fu_flashrom_opener_new (void);

const gchar 		*fu_flashrom_opener_get_programmer (FuFlashromOpener *self);
void 			 fu_flashrom_opener_set_programmer (FuFlashromOpener *self,
					    		    const gchar *name);

const gchar 		*fu_flashrom_opener_get_programmer_args (FuFlashromOpener *self);
void 			 fu_flashrom_opener_set_programmer_args (FuFlashromOpener *self,
								 const gchar *args);

enum fu_flashrom_opener_layout_kind fu_flashrom_opener_get_layout (FuFlashromOpener *self,
								   const union fu_flashrom_opener_layout_data **layout_data);
void 			 fu_flashrom_opener_set_layout_from_ifd (FuFlashromOpener *self);
void			 fu_flashrom_opener_set_layout (FuFlashromOpener *self,
							const struct fu_flashrom_opener_layout_region *regions,
							gsize num_regions);
