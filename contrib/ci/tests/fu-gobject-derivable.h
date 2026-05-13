/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: wrong parent_class
 * nocheck:expect: wrong parent GType
 */

G_DECLARE_DERIVABLE_TYPE(FuDevice, fu_device, FU, DEVICE, FwupdDevice)

typedef enum {
	FU_DEVICE_FLAG_NONE = 0,
} FuDeviceFlags;

/* usually in the .c file... */
G_DEFINE_TYPE_WITH_PRIVATE(FuDevice, fu_device, FWUPD_TYPE_FIRMWARE);

struct _FuDeviceClass {
	FwupdFirmwareClass parent_class;
	void (*func)(FuDevice *self);
}
