/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
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

#ifndef __SYNAPTICSMST_DEVICE_H
#define __SYNAPTICSMST_DEVICE_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define SYNAPTICSMST_TYPE_DEVICE (synapticsmst_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (SynapticsMSTDevice, synapticsmst_device, SYNAPTICSMST, DEVICE, GObject)

#define SYSFS_DRM_DP_AUX "/sys/class/drm_dp_aux_dev"

struct _SynapticsMSTDeviceClass
{
	GObjectClass		parent_class;
};

/**
 * SynapticsMSTDeviceKind:
 * @SYNAPTICSMST_DEVICE_KIND_UNKNOWN:		Type invalid or not known
 * @SYNAPTICSMST_DEVICE_KIND_DIRECT:		Directly addressable
 * @SYNAPTICSMST_DEVICE_KIND_REMOTE:		Requires remote register work
 *
 * The device type.
 **/
typedef enum {
	SYNAPTICSMST_DEVICE_KIND_UNKNOWN,
	SYNAPTICSMST_DEVICE_KIND_DIRECT,
	SYNAPTICSMST_DEVICE_KIND_REMOTE,
	/*< private >*/
	SYNAPTICSMST_DEVICE_KIND_LAST
} SynapticsMSTDeviceKind;

typedef enum {
	SYNAPTICSMST_DEVICE_BOARDID_EVB = 0x00,
	SYNAPTICSMST_DEVICE_BOARDID_DELL_X6 = 0x110,
	SYNAPTICSMST_DEVICE_BOARDID_DELL_X7,
	SYNAPTICSMST_DEVICE_BOARDID_DELL_WD15_TB16_WIRE,
	SYNAPTICSMST_DEVICE_BOARDID_DELL_WLD15_WIRELESS,
	SYNAPTICSMST_DEVICE_BOARDID_DELL_X7_RUGGED = 0X115,
	SYNAPTICSMST_DEVICE_BOARDID_UNKNOWN = 0xFF,
} SynapticsMSTDeviceBoardID;

#define CUSTOMERID_DELL 	0x1

SynapticsMSTDevice	*synapticsmst_device_new	(SynapticsMSTDeviceKind kind,
							 const gchar *aux_node,
							 guint8 layer,
							 guint16 rad);

/* helpers */
SynapticsMSTDeviceKind synapticsmst_device_kind_from_string	(const gchar	*kind);
const gchar	*synapticsmst_device_kind_to_string		(SynapticsMSTDeviceKind kind);
const gchar	*synapticsmst_device_board_id_to_string		(SynapticsMSTDeviceBoardID board_id);
const gchar 	*synapticsmst_device_get_guid 			(SynapticsMSTDevice *device);
gboolean	 synapticsmst_device_scan_cascade_device 	(SynapticsMSTDevice *device,
								 GError **error,
								 guint8 tx_port);
gboolean	 synapticsmst_device_open 			(SynapticsMSTDevice *device,
								 GError	**error);

/* getters */
SynapticsMSTDeviceKind synapticsmst_device_get_kind		(SynapticsMSTDevice *device);
SynapticsMSTDeviceBoardID synapticsmst_device_get_board_id 	(SynapticsMSTDevice *device);
const gchar	*synapticsmst_device_get_version		(SynapticsMSTDevice *device);
const gchar 	*synapticsmst_device_get_chip_id 		(SynapticsMSTDevice *device);
const gchar 	*synapticsmst_device_get_aux_node		(SynapticsMSTDevice *device);
guint16 	 synapticsmst_device_get_rad 			(SynapticsMSTDevice *device);
guint8 		 synapticsmst_device_get_layer 			(SynapticsMSTDevice *device);
gboolean
synapticsmst_device_get_cascade					(SynapticsMSTDevice *device);

/* object methods */
gboolean	 synapticsmst_device_enumerate_device 		(SynapticsMSTDevice *devices,
								const gchar *dock_type,
								const gchar *sytem_type,
								 GError **error);
gboolean	 synapticsmst_device_write_firmware		(SynapticsMSTDevice *device,
								 GBytes	*fw,
								 GFileProgressCallback	progress_cb,
								 gpointer user_data,
								 GError	**error);

G_END_DECLS

#endif /* __SYNAPTICSMST_DEVICE_H */
