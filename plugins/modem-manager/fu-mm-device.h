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

/*
 * FU_MM_DEVICE_FLAG_UNINHIBIT_MM_AFTER_FASTBOOT_REBOOT
 *
 * after entering the fastboot state, the modem cannot execute the attach method
 * in the MM plugin plugin plugin. shadow_device needs to be used to uninhibit the modem
 * when fu_mm_plugin_udev_uevent_cb detects it.
 */
#define FU_MM_DEVICE_FLAG_UNINHIBIT_MM_AFTER_FASTBOOT_REBOOT (1 << 1)

#define FU_TYPE_MM_DEVICE (fu_mm_device_get_type())
G_DECLARE_FINAL_TYPE(FuMmDevice, fu_mm_device, FU, MM_DEVICE, FuDevice)

FuMmDevice *
fu_mm_device_new(FuContext *ctx, MMManager *manager, MMObject *omodem);
void
fu_mm_device_set_usb_device(FuMmDevice *self, FuUsbDevice *usb_device);
const gchar *
fu_mm_device_get_inhibition_uid(FuMmDevice *device);
MMModemFirmwareUpdateMethod
fu_mm_device_get_update_methods(FuMmDevice *device);

FuMmDevice *
fu_mm_shadow_device_new(FuMmDevice *device);
FuMmDevice *
fu_mm_device_udev_new(FuContext *ctx, MMManager *manager, FuMmDevice *shadow_device);
void
fu_mm_device_udev_add_port(FuMmDevice *self, const gchar *subsystem, const gchar *path, gint ifnum);

gboolean
fu_mm_device_write_suspend_ms(FuDevice *device, const gchar *buf, GError **error);

gboolean
fu_mm_device_get_device_info(FuDevice *device, GError **error);

#endif /* __FU_MM_DEVICE_H */
