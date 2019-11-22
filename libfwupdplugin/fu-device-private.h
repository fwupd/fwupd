/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fu-device.h>
#include <xmlb.h>

#define fu_device_set_plugin(d,v)		fwupd_device_set_plugin(FWUPD_DEVICE(d),v)

/**
 * FuDeviceInstanceFlags:
 * @FU_DEVICE_INSTANCE_FLAG_NONE:		No flags set
 * @FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS:	Only use instance ID for quirk matching
 *
 * The flags to use when interacting with a device instance
 **/
typedef enum {
	FU_DEVICE_INSTANCE_FLAG_NONE		= 0,
	FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS	= 1 << 0,
	/*< private >*/
	FU_DEVICE_INSTANCE_FLAG_LAST
} FuDeviceInstanceFlags;

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
GType		 fu_device_get_specialized_gtype	(FuDevice	*self);
gboolean	 fu_device_ensure_id			(FuDevice	*self,
							 GError		**error);
void		 fu_device_incorporate_from_component	(FuDevice	*device,
							 XbNode		*component);
void		 fu_device_convert_instance_ids		(FuDevice	*self);
void		 fu_device_add_instance_id_full		(FuDevice	*self,
							 const gchar	*instance_id,
							 FuDeviceInstanceFlags flags);
gchar		*fu_device_get_guids_as_str		(FuDevice	*self);
GPtrArray	*fu_device_get_possible_plugins		(FuDevice	*self);
