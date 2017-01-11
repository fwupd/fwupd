/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
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

#define MAX_DP_AUX_NODES	3

struct _SynapticsMSTDeviceClass
{
	GObjectClass		parent_class;
	/*< private >*/
	void (*_as_reserved1)	(void);
	void (*_as_reserved2)	(void);
	void (*_as_reserved3)	(void);
	void (*_as_reserved4)	(void);
	void (*_as_reserved5)	(void);
	void (*_as_reserved6)	(void);
	void (*_as_reserved7)	(void);
	void (*_as_reserved8)	(void);
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
	SYNAPTICSMST_DEVICE_BOARDID_UNKNOW = 0,
	SYNAPTICSMST_DEVICE_BOARDID_SYNA_EVB = 0x0082, // should be removed before release
	SYNAPTICSMST_DEVICE_BOARDID_X6 = 0x0110,
	SYNAPTICSMST_DEVICE_BOARDID_X7,
	SYNAPTICSMST_DEVICE_BOARDID_TRINITY_WIRE,
	SYNAPTICSMST_DEVICE_BOARDID_TRINITY_WIRELESS
} SynapticsMSTDeviceBoardID;

SynapticsMSTDevice	*synapticsmst_device_new	(SynapticsMSTDeviceKind kind,
							 const gchar *aux_node);

const gchar	*synapticsmst_device_get_aux_node	(guint8 index);

/* helpers */
SynapticsMSTDeviceKind synapticsmst_device_kind_from_string	(const gchar	*kind);
const gchar	*synapticsmst_device_kind_to_string		(SynapticsMSTDeviceKind kind);
const gchar	*synapticsmst_device_boardID_to_string		(SynapticsMSTDeviceBoardID boardID);
const gchar 	*synapticsmst_device_get_guid 			(SynapticsMSTDevice *device);
gboolean	 synapticsmst_device_enable_remote_control	(SynapticsMSTDevice *device,
								 GError **error);
gboolean	 synapticsmst_device_disable_remote_control	(SynapticsMSTDevice *device,
								 GError **error);

/* getters */
SynapticsMSTDeviceKind synapticsmst_device_get_kind	(SynapticsMSTDevice	*device);
SynapticsMSTDeviceBoardID synapticsmst_device_get_boardID (SynapticsMSTDevice	*device);
const gchar	*synapticsmst_device_get_devfs_node	(SynapticsMSTDevice	*device);
const gchar	*synapticsmst_device_get_version	(SynapticsMSTDevice	*device);
const gchar	*synapticsmst_device_get_chipID		(SynapticsMSTDevice	*device);
guint8		 synapticsmst_device_get_aux_node_to_int(SynapticsMSTDevice	*device);

/* object methods */
gboolean	synapticsmst_device_enumerate_device	(SynapticsMSTDevice	*devices,
							 GError			**error);
gboolean	synapticsmst_device_write_firmware	(SynapticsMSTDevice	*device,
							 GBytes			*fw,
							 GFileProgressCallback	progress_cb,
							 gpointer 		user_data,
							 GError			**error);

G_END_DECLS

#endif /* __SYNAPTICSMST_DEVICE_H */
