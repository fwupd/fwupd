/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <gio/gio.h>

#define DFU_TYPE_ELEMENT (dfu_element_get_type ())
G_DECLARE_DERIVABLE_TYPE (DfuElement, dfu_element, DFU, ELEMENT, GObject)

struct _DfuElementClass
{
	GObjectClass		 parent_class;
};

DfuElement	*dfu_element_new		(void);

GBytes		*dfu_element_get_contents	(DfuElement	*element);
guint32		 dfu_element_get_address	(DfuElement	*element);
void		 dfu_element_set_contents	(DfuElement	*element,
						 GBytes		*contents);
void		 dfu_element_set_address	(DfuElement	*element,
						 guint32	 address);
gchar		*dfu_element_to_string		(DfuElement	*element);
