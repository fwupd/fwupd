/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-qsi-dock-child-device.h"
#include "fu-qsi-dock-mcu-device.h"

struct _FuQsiDockChildDevice {
	FuDevice parent_instance;
	guint8 chip_idx;
};

G_DEFINE_TYPE(FuQsiDockChildDevice, fu_qsi_dock_child_device, FU_TYPE_DEVICE)

void
fu_qsi_dock_child_device_set_chip_idx(FuQsiDockChildDevice *self, guint8 chip_idx)
{
	self->chip_idx = chip_idx;
}

static void
fu_qsi_dock_child_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuQsiDockChildDevice *self = FU_QSI_DOCK_CHILD_DEVICE(device);
	fu_string_append_kx(str, idt, "ChipIdx", self->chip_idx);
}

/* use the parents parser */
static FuFirmware *
fu_qsi_dock_mcu_device_prepare_firmware(FuDevice *device,
					GBytes *fw,
					FwupdInstallFlags flags,
					GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	if (parent == NULL) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "no parent");
		return NULL;
	}
	return fu_device_prepare_firmware(parent, fw, flags, error);
}

/* only update this specific child component */
static gboolean
fu_qsi_dock_mcu_device_write_firmware(FuDevice *device,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuQsiDockChildDevice *self = FU_QSI_DOCK_CHILD_DEVICE(device);
	FuDevice *parent = fu_device_get_parent(device);
	if (parent == NULL) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "no parent");
		return FALSE;
	}
	return fu_qsi_dock_mcu_device_write_firmware_with_idx(FU_QSI_DOCK_MCU_DEVICE(parent),
							      firmware,
							      self->chip_idx,
							      progress,
							      flags,
							      error);
}

static void
fu_qsi_dock_child_device_init(FuQsiDockChildDevice *self)
{
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_USE_PARENT_FOR_OPEN);
}

static void
fu_qsi_dock_child_device_class_init(FuQsiDockChildDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_qsi_dock_child_device_to_string;
	klass_device->prepare_firmware = fu_qsi_dock_mcu_device_prepare_firmware;
	klass_device->write_firmware = fu_qsi_dock_mcu_device_write_firmware;
}

FuDevice *
fu_qsi_dock_child_new(FuContext *ctx)
{
	return g_object_new(FU_TYPE_QSI_DOCK_CHILD_DEVICE, "context", ctx, NULL);
}
