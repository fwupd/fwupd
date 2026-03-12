/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-usi-dock-child-device.h"
#include "fu-usi-dock-mcu-device.h"

struct _FuUsiDockChildDevice {
	FuDevice parent_instance;
	FuUsiDockFirmwareIdx chip_idx;
};

G_DEFINE_TYPE(FuUsiDockChildDevice, fu_usi_dock_child_device, FU_TYPE_DEVICE)

FuUsiDockFirmwareIdx
fu_usi_dock_child_device_get_chip_idx(FuUsiDockChildDevice *self)
{
	return self->chip_idx;
}

void
fu_usi_dock_child_device_set_chip_idx(FuUsiDockChildDevice *self, FuUsiDockFirmwareIdx chip_idx)
{
	self->chip_idx = chip_idx;
}

static void
fu_usi_dock_child_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuUsiDockChildDevice *self = FU_USI_DOCK_CHILD_DEVICE(device);
	fwupd_codec_string_append(str,
				  idt,
				  "ChipIdx",
				  fu_usi_dock_firmware_idx_to_string(self->chip_idx));
}

/* use the parents parser */
static FuFirmware *
fu_usi_dock_child_device_prepare_firmware(FuDevice *device,
					  GInputStream *stream,
					  FuProgress *progress,
					  FuFirmwareParseFlags flags,
					  GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device, error);
	if (proxy == NULL)
		return NULL;
	return fu_device_prepare_firmware(proxy, stream, progress, flags, error);
}

/* only update this specific child component */
static gboolean
fu_usi_dock_child_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuUsiDockChildDevice *self = FU_USI_DOCK_CHILD_DEVICE(device);
	FuDevice *proxy = fu_device_get_proxy(device, error);
	if (proxy == NULL)
		return FALSE;
	return fu_usi_dock_mcu_device_write_firmware_with_idx(FU_USI_DOCK_MCU_DEVICE(proxy),
							      firmware,
							      self->chip_idx,
							      progress,
							      flags,
							      error);
}

static void
fu_usi_dock_child_device_init(FuUsiDockChildDevice *self)
{
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FOR_OPEN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REFCOUNTED_PROXY);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_PARENT_NAME_PREFIX);
	fu_device_set_proxy_gtype(FU_DEVICE(self), FU_TYPE_USI_DOCK_MCU_DEVICE);
}

static void
fu_usi_dock_child_device_class_init(FuUsiDockChildDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_usi_dock_child_device_to_string;
	device_class->prepare_firmware = fu_usi_dock_child_device_prepare_firmware;
	device_class->write_firmware = fu_usi_dock_child_device_write_firmware;
}

FuDevice *
fu_usi_dock_child_device_new(FuDevice *proxy)
{
	return g_object_new(FU_TYPE_USI_DOCK_CHILD_DEVICE, "proxy", proxy, NULL);
}
