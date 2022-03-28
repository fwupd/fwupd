/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 Aleksander Morgado <aleksander@aleksander.es>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_MM_DEVICE_H
#define __FU_MM_DEVICE_H

#include <fwupdplugin.h>

#include <libmm-glib.h>

#define FU_TYPE_MM_DEVICE (fu_mm_device_get_type())
G_DECLARE_FINAL_TYPE(FuMmDevice, fu_mm_device, FU, MM_DEVICE, FuDevice)

FuMmDevice *
fu_mm_device_new(FuContext *ctx, MMManager *manager, MMObject *omodem);
FuUsbDevice *
fu_mm_device_get_usb_device(FuMmDevice *self);
void
fu_mm_device_set_usb_device(FuMmDevice *self, FuUsbDevice *usb_device);
const gchar *
fu_mm_device_get_inhibition_uid(FuMmDevice *device);
const gchar *
fu_mm_device_get_detach_fastboot_at(FuMmDevice *device);
gint
fu_mm_device_get_port_at_ifnum(FuMmDevice *device);
gint
fu_mm_device_get_port_qmi_ifnum(FuMmDevice *device);
gint
fu_mm_device_get_port_mbim_ifnum(FuMmDevice *device);
MMModemFirmwareUpdateMethod
fu_mm_device_get_update_methods(FuMmDevice *device);

FuMmDevice *
fu_mm_shadow_device_new(FuMmDevice *device);
FuMmDevice *
fu_mm_device_udev_new(FuContext *ctx, MMManager *manager, FuMmDevice *shadow_device);
void
fu_mm_device_udev_add_port(FuMmDevice *device,
			   const gchar *subsystem,
			   const gchar *path,
			   gint ifnum);

#endif /* __FU_MM_DEVICE_H */
