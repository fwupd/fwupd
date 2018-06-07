/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __LU_DEVICE_H
#define __LU_DEVICE_H

#include <glib-object.h>
#include <gudev/gudev.h>

#include "fu-plugin.h"

#include "lu-hidpp-msg.h"

G_BEGIN_DECLS

#define LU_TYPE_DEVICE (lu_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (LuDevice, lu_device, LU, DEVICE, FuDevice)

struct _LuDeviceClass
{
	FuDeviceClass	parent_class;
	gboolean	 (*open)		(LuDevice		*device,
						 GError			**error);
	gboolean	 (*close)		(LuDevice		*device,
						 GError			**error);
	gboolean	 (*probe)		(LuDevice		*device,
						 GError			**error);
	gboolean	 (*poll)		(LuDevice		*device,
						 GError			**error);
	gboolean	 (*attach)		(LuDevice		*device,
						 GError			**error);
	gboolean	 (*detach)		(LuDevice		*device,
						 GError			**error);
	gboolean	 (*write_firmware)	(LuDevice		*device,
						 GBytes			*fw,
						 GError			**error);
};

#define	LU_DEVICE_VID				0x046d

#define	LU_DEVICE_PID_RUNTIME			0xc52b
#define	LU_DEVICE_PID_BOOTLOADER_NORDIC		0xaaaa
#define	LU_DEVICE_PID_BOOTLOADER_NORDIC_PICO	0xaaae
#define	LU_DEVICE_PID_BOOTLOADER_TEXAS		0xaaac
#define	LU_DEVICE_PID_BOOTLOADER_TEXAS_PICO	0xaaad

#define LU_DEVICE_EP1				0x81
#define LU_DEVICE_EP3				0x83
/* Signed firmware are very long to verify on the device */
#define LU_DEVICE_TIMEOUT_MS			20000

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
	LU_DEVICE_FLAG_ACTIVE			= 1 << 0,
	LU_DEVICE_FLAG_IS_OPEN			= 1 << 1,
	LU_DEVICE_FLAG_REQUIRES_SIGNED_FIRMWARE	= 1 << 3,
	LU_DEVICE_FLAG_REQUIRES_RESET		= 1 << 4,
	LU_DEVICE_FLAG_REQUIRES_ATTACH		= 1 << 5,
	LU_DEVICE_FLAG_REQUIRES_DETACH		= 1 << 6,
	LU_DEVICE_FLAG_ATTACH_WILL_REPLUG	= 1 << 7,
	LU_DEVICE_FLAG_DETACH_WILL_REPLUG	= 1 << 8,
	/*< private >*/
	LU_DEVICE_FLAG_LAST
} LuDeviceFlags;

LuDeviceKind	 lu_device_kind_from_string	(const gchar	*kind);
const gchar	*lu_device_kind_to_string	(LuDeviceKind	 kind);

LuDeviceKind	 lu_device_get_kind		(LuDevice		*device);
guint8		 lu_device_get_hidpp_id		(LuDevice		*device);
void		 lu_device_set_hidpp_id		(LuDevice		*device,
						 guint8			 hidpp_id);
guint8		 lu_device_get_battery_level	(LuDevice		*device);
void		 lu_device_set_battery_level	(LuDevice		*device,
						 guint8			 percentage);
gdouble		 lu_device_get_hidpp_version	(LuDevice		*device);
void		 lu_device_set_hidpp_version	(LuDevice		*device,
						 gdouble		 hidpp_version);
gboolean	 lu_device_has_flag		(LuDevice		*device,
						 LuDeviceFlags		 flag);
void		 lu_device_add_flag		(LuDevice		*device,
						 LuDeviceFlags		 flag);
void		 lu_device_remove_flag		(LuDevice		*device,
						 LuDeviceFlags		 flag);
LuDeviceFlags	 lu_device_get_flags		(LuDevice		*device);
const gchar	*lu_device_get_version_hw	(LuDevice		*device);
void		 lu_device_set_version_hw	(LuDevice		*device,
						 const gchar		*version_hw);
GUdevDevice	*lu_device_get_udev_device	(LuDevice		*device);
GUsbDevice	*lu_device_get_usb_device	(LuDevice		*device);

LuDevice	*lu_device_fake_new		(LuDeviceKind		 kind);
gboolean	 lu_device_open			(LuDevice		*device,
						 GError			**error);
gboolean	 lu_device_close		(LuDevice		*device,
						 GError			**error);
gboolean	 lu_device_probe		(LuDevice		*device,
						 GError			**error);
gboolean	 lu_device_poll			(LuDevice		*device,
						 GError			**error);
gboolean	 lu_device_hidpp_send		(LuDevice		*device,
						 LuHidppMsg		*msg,
						 guint			 timeout,
						 GError			**error);
gboolean	 lu_device_hidpp_receive	(LuDevice		*device,
						 LuHidppMsg		*msg,
						 guint			 timeout,
						 GError			**error);
gboolean	 lu_device_hidpp_transfer	(LuDevice		*device,
						 LuHidppMsg		*msg,
						 GError			**error);
gboolean	 lu_device_hidpp_feature_search	(LuDevice		*device,
						 guint16		 feature,
						 GError			**error);
guint8		 lu_device_hidpp_feature_get_idx (LuDevice		*device,
						 guint16		 feature);
guint16		 lu_device_hidpp_feature_find_by_idx (LuDevice		*device,
						 guint8			 idx);

G_END_DECLS

#endif /* __LU_DEVICE_H */
