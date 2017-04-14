/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
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

#ifndef __LU_DEVICE_H
#define __LU_DEVICE_H

#include <glib-object.h>
#include <gudev/gudev.h>

#include "fu-plugin.h"

G_BEGIN_DECLS

#define LU_TYPE_DEVICE (lu_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (LuDevice, lu_device, LU, DEVICE, GObject)

struct _LuDeviceClass
{
	GObjectClass	parent_class;
	gboolean	 (*open)		(LuDevice		*device,
						 GError			**error);
	gboolean	 (*close)		(LuDevice		*device,
						 GError			**error);
	gboolean	 (*probe)		(LuDevice		*device,
						 GError			**error);
	gboolean	 (*attach)		(LuDevice		*device,
						 GError			**error);
	gboolean	 (*detach)		(LuDevice		*device,
						 GError			**error);
	gboolean	 (*write_firmware)	(LuDevice		*device,
						 GBytes			*fw,
						 GFileProgressCallback	 progress_cb,
						 gpointer		 progress_data,
						 GError			**error);
};

#define	LU_DEVICE_VID				0x046d

#define	LU_DEVICE_PID_RUNTIME			0xc52b
#define	LU_DEVICE_PID_BOOTLOADER_NORDIC		0xaaaa
#define	LU_DEVICE_PID_BOOTLOADER_TEXAS		0xaaac

#define LU_DEVICE_EP1				0x81
#define LU_DEVICE_EP3				0x83
#define LU_DEVICE_TIMEOUT_MS			2500

typedef enum {
	LU_DEVICE_KIND_UNKNOWN,
	LU_DEVICE_KIND_RUNTIME,
	LU_DEVICE_KIND_BOOTLOADER_NORDIC,
	LU_DEVICE_KIND_BOOTLOADER_TEXAS,
	LU_DEVICE_KIND_PERIPHERAL,
	/*< private >*/
	LU_DEVICE_KIND_LAST
} LuDeviceKind;

typedef enum {
	LU_DEVICE_FLAG_NONE,
	LU_DEVICE_FLAG_CAN_FLASH	= 1 << 0,
	LU_DEVICE_FLAG_SIGNED_FIRMWARE	= 1 << 1,
	/*< private >*/
	LU_DEVICE_FLAG_LAST
} LuDeviceFlags;

typedef struct {
	guint8	 report_id;
	guint8	 device_idx;
	guint8	 sub_id;
	guint8	 data[128];
	gsize	 len;
} LuDeviceHidppMsg;

G_DEFINE_AUTOPTR_CLEANUP_FUNC(LuDeviceHidppMsg, g_free);

LuDeviceHidppMsg *lu_device_hidpp_new		(void);

LuDeviceKind	 lu_device_kind_from_string	(const gchar	*kind);
const gchar	*lu_device_kind_to_string	(LuDeviceKind	 kind);

LuDeviceKind	 lu_device_get_kind		(LuDevice		*device);
const gchar	*lu_device_get_platform_id	(LuDevice		*device);
void		 lu_device_set_platform_id	(LuDevice		*device,
						 const gchar		*platform_id);
gboolean	 lu_device_has_flag		(LuDevice		*device,
						 LuDeviceFlags		 flag);
void		 lu_device_add_flag		(LuDevice		*device,
						 LuDeviceFlags		 flag);
LuDeviceFlags	 lu_device_get_flags		(LuDevice		*device);
const gchar	*lu_device_get_product		(LuDevice		*device);
void		 lu_device_set_product		(LuDevice		*device,
						 const gchar		*product);
const gchar	*lu_device_get_vendor		(LuDevice		*device);
void		 lu_device_set_vendor		(LuDevice		*device,
						 const gchar		*vendor);
const gchar	*lu_device_get_version_bl	(LuDevice		*device);
void		 lu_device_set_version_bl	(LuDevice		*device,
						 const gchar		*version_bl);
const gchar	*lu_device_get_version_fw	(LuDevice		*device);
void		 lu_device_set_version_fw	(LuDevice		*device,
						 const gchar		*version_fw);
GPtrArray	*lu_device_get_guids		(LuDevice		*device);
void		 lu_device_add_guid		(LuDevice		*device,
						 const gchar		*guid);
GUdevDevice	*lu_device_get_udev_device	(LuDevice		*device);
GUsbDevice	*lu_device_get_usb_device	(LuDevice		*device);

LuDevice	*lu_device_fake_new		(LuDeviceKind		 kind);
gboolean	 lu_device_open			(LuDevice		*device,
						 GError			**error);
gboolean	 lu_device_close		(LuDevice		*device,
						 GError			**error);
gboolean	 lu_device_detach		(LuDevice		*device,
						 GError			**error);
gboolean	 lu_device_attach		(LuDevice		*device,
						 GError			**error);
gboolean	 lu_device_write_firmware	(LuDevice		*device,
						 GBytes			*fw,
						 GFileProgressCallback	 progress_cb,
						 gpointer		 progress_data,
						 GError			**error);
gboolean	 lu_device_hidpp_send		(LuDevice		*device,
						 LuDeviceHidppMsg	*msg,
						 GError			**error);
gboolean	 lu_device_hidpp_receive	(LuDevice		*device,
						 LuDeviceHidppMsg	*msg,
						 GError			**error);
gboolean	 lu_device_hidpp_transfer	(LuDevice		*device,
						 LuDeviceHidppMsg	*msg,
						 GError			**error);

G_END_DECLS

#endif /* __LU_DEVICE_H */
