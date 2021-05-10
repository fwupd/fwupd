/*
 * Copyright (C) 2021 Peter Marheine <pmarheine@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"
#include "fu-common.h"
#include "fu-flashrom-opener.h"

#ifdef HAVE_LIBFLASHROM
#include <libflashrom.h>
#endif
#include <libfwupd/fwupd.h>

struct _FuFlashromOpener {
	GObject parent_instance;
	gchar *programmer_name;
	gchar *programmer_args;
	enum fu_flashrom_opener_layout_kind layout_kind;
	union fu_flashrom_opener_layout_data layout_data;
};

G_DEFINE_TYPE (FuFlashromOpener, fu_flashrom_opener, G_TYPE_OBJECT)

static void
fu_flashrom_opener_init (FuFlashromOpener *self)
{
	self->programmer_name = NULL;
	self->programmer_args = NULL;
	self->layout_kind = FLASHROM_OPENER_LAYOUT_KIND_UNSET;
	self->layout_data = (union fu_flashrom_opener_layout_data){0};
}

FuFlashromOpener *
fu_flashrom_opener_new (void)
{
	return g_object_new (FU_TYPE_FLASHROM_OPENER, NULL);
}

static void
fu_flashrom_opener_finalize (GObject *object)
{
	FuFlashromOpener *self = FU_FLASHROM_OPENER (object);

	g_free (self->programmer_name);
	g_free (self->programmer_args);
	if (self->layout_kind == FLASHROM_OPENER_LAYOUT_KIND_STATIC)
		g_ptr_array_free (self->layout_data.static_regions, TRUE);
	G_OBJECT_CLASS (fu_flashrom_opener_parent_class)->finalize (object);
}

static void
fu_flashrom_opener_class_init (FuFlashromOpenerClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = fu_flashrom_opener_finalize;
}

/**
 * fu_flashrom_opener_get_programmer
 *
 * Returns: (transfer none) (nullable): the current programmer name or NULL
 * if none has been set.
 *
 * Since: 1.6.1
 */
const gchar *
fu_flashrom_opener_get_programmer (FuFlashromOpener *self)
{
	return self->programmer_name;
}

/**
 * fu_flashrom_opener_set_programmer:
 * @name: (transfer none): the name of the flashrom programmer to use
 *
 * Set the programmer to use to access device flash.
 *
 * Since: 1.6.1
 */
void
fu_flashrom_opener_set_programmer (FuFlashromOpener *self,
				   const gchar *name)
{
	if (g_strcmp0 (self->programmer_name, name) == 0)
		return;

	g_free (self->programmer_name);
	self->programmer_name = g_strdup (name);
}

/**
 * fu_flashrom_opener_get_programmer_args:
 * @self:
 *
 * Returns: (transfer none) (nullable): the arguments most recently passed to
 * set_programmer_args or NULL if none have been set.
 *
 * Since: 1.6.1
 */
const gchar *
fu_flashrom_opener_get_programmer_args (FuFlashromOpener *self)
{
	return self->programmer_args;
}

/**
 * fu_flashrom_opener_set_programmer_args:
 * @self:
 * @args: (transfer none): string of programmer options, of the form
 * key=value,key2=value2
 *
 * Since: 1.6.1
 */
void
fu_flashrom_opener_set_programmer_args (FuFlashromOpener *self,
					const gchar *args)
{
	if (g_strcmp0 (self->programmer_args, args) == 0)
		return;

	g_free (self->programmer_args);
	self->programmer_args = g_strdup (args);
}

/**
 * fu_flashrom_opener_get_layout:
 * @self:
 * @layout_data: (out): additional data corresponding to the layout
 *
 * Returns: the kind of layout in use
 *
 * Since: 1.6.1
 */
enum fu_flashrom_opener_layout_kind
fu_flashrom_opener_get_layout (FuFlashromOpener *self,
			       const union fu_flashrom_opener_layout_data **layout_data)
{
	*layout_data = &self->layout_data;
	return self->layout_kind;
}

/**
 * fu_flashrom_opener_set_layout_from_ifd:
 * @self:
 *
 * Set the flash layout to be detected automatically from an Intel Flash
 * Descriptor stored in the flash, loaded on device open.
 *
 * Since: 1.6.1
 */
void
fu_flashrom_opener_set_layout_from_ifd (FuFlashromOpener *self)
{
	self->layout_kind = FLASHROM_OPENER_LAYOUT_KIND_IFD;
}

static void
free_layout_region (gpointer ptr)
{
	struct fu_flashrom_opener_layout_region *region = ptr;
	g_free ((char *) region->name);
	g_free (region);
}

/**
 * fu_flashrom_opener_set_layout
 * @self:
 * @layout: (transfer none): array of region definitions
 * @num_regions: number of entries in @layout
 *
 * Set the layout of this opener to consist of exactly the provided regions.
 *
 * Since: 1.6.1
 */
void
fu_flashrom_opener_set_layout (FuFlashromOpener *self,
			       const struct fu_flashrom_opener_layout_region *layout,
			       gsize num_regions)
{
	GPtrArray *regions = g_ptr_array_new_with_free_func (free_layout_region);

	if (self->layout_kind == FLASHROM_OPENER_LAYOUT_KIND_STATIC)
		g_ptr_array_free (self->layout_data.static_regions, TRUE);

	for (gsize i = 0; i < num_regions; i++) {
		struct fu_flashrom_opener_layout_region *region =
			g_memdup(&layout[i], sizeof (*layout));
		region->name = g_strdup (region->name);
		g_ptr_array_add (regions, region);
	}

	self->layout_kind = FLASHROM_OPENER_LAYOUT_KIND_STATIC;
	self->layout_data.static_regions = regions;
}
