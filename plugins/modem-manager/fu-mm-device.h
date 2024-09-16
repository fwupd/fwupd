/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2019 Aleksander Morgado <aleksander@aleksander.es>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include <libmm-glib.h>

#define FU_MM_DEVICE_FLAG_DETACH_AT_FASTBOOT_HAS_NO_RESPONSE "detach-at-fastboot-has-no-response"
#define FU_MM_DEVICE_FLAG_UNINHIBIT_MM_AFTER_FASTBOOT_REBOOT                                       \
	"uninhibit-modemmanager-after-fastboot-reboot"
#define FU_MM_DEVICE_FLAG_USE_BRANCH "use-branch"

#define FU_TYPE_MM_DEVICE (fu_mm_device_get_type())
G_DECLARE_FINAL_TYPE(FuMmDevice, fu_mm_device, FU, MM_DEVICE, FuDevice)

FuMmDevice *
fu_mm_device_new(FuContext *ctx, MMManager *manager, MMObject *omodem);
void
fu_mm_device_set_udev_device(FuMmDevice *self, FuUdevDevice *udev_device);
const gchar *
fu_mm_device_get_inhibition_uid(FuMmDevice *device);
MMModemFirmwareUpdateMethod
fu_mm_device_get_update_methods(FuMmDevice *device);

FuMmDevice *
fu_mm_device_shadow_new(FuMmDevice *device);
FuMmDevice *
fu_mm_device_udev_new(FuContext *ctx, MMManager *manager, FuMmDevice *shadow_device);
void
fu_mm_device_udev_add_port(FuMmDevice *self, const gchar *subsystem, const gchar *path);
