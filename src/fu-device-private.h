/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fu-device.h>
#include <xmlb.h>

G_BEGIN_DECLS

GPtrArray	*fu_device_get_parent_guids		(FuDevice	*self);
gboolean	 fu_device_has_parent_guid		(FuDevice	*self,
							 const gchar	*guid);
void		 fu_device_set_parent			(FuDevice	*self,
							 FuDevice	*parent);
guint		 fu_device_get_order			(FuDevice	*self);
void		 fu_device_set_order			(FuDevice	*self,
							 guint		 order);
guint		 fu_device_get_priority			(FuDevice	*self);
void		 fu_device_set_priority			(FuDevice	*self,
							 guint		 priority);
void		 fu_device_set_alternate		(FuDevice	*self,
							 FuDevice	*alternate);
gboolean	 fu_device_ensure_id			(FuDevice	*self,
							 GError		**error);
void		 fu_device_incorporate_from_component	(FuDevice	*device,
							 XbNode		*component);
void		 fu_device_convert_instance_ids		(FuDevice	*self);

G_END_DECLS
