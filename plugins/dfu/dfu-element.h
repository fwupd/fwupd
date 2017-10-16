/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef __DFU_ELEMENT_H
#define __DFU_ELEMENT_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define DFU_TYPE_ELEMENT (dfu_element_get_type ())
G_DECLARE_DERIVABLE_TYPE (DfuElement, dfu_element, DFU, ELEMENT, GObject)

struct _DfuElementClass
{
	GObjectClass		 parent_class;
	/*< private >*/
	/* Padding for future expansion */
	void (*_dfu_element_reserved1) (void);
	void (*_dfu_element_reserved2) (void);
	void (*_dfu_element_reserved3) (void);
	void (*_dfu_element_reserved4) (void);
	void (*_dfu_element_reserved5) (void);
	void (*_dfu_element_reserved6) (void);
	void (*_dfu_element_reserved7) (void);
	void (*_dfu_element_reserved8) (void);
	void (*_dfu_element_reserved9) (void);
};

DfuElement	*dfu_element_new		(void);

GBytes		*dfu_element_get_contents	(DfuElement	*element);
guint32		 dfu_element_get_address	(DfuElement	*element);

void		 dfu_element_set_contents	(DfuElement	*element,
						 GBytes		*contents);
void		 dfu_element_set_address	(DfuElement	*element,
						 guint32	 address);
void		 dfu_element_set_target_size	(DfuElement	*element,
						 guint32	 target_size);
void		 dfu_element_set_padding_value	(DfuElement	*element,
						 guint8		 padding_value);

gchar		*dfu_element_to_string		(DfuElement	*element);

G_END_DECLS

#endif /* __DFU_ELEMENT_H */
