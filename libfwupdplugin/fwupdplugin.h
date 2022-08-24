/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#define __FWUPDPLUGIN_H_INSIDE__

#include <fwupd.h>
#include <libfwupdplugin/fu-archive-firmware.h>
#include <libfwupdplugin/fu-archive.h>
#include <libfwupdplugin/fu-backend.h>
#include <libfwupdplugin/fu-bios-settings.h>
#include <libfwupdplugin/fu-bluez-device.h>
#include <libfwupdplugin/fu-byte-array.h>
#include <libfwupdplugin/fu-bytes.h>
#include <libfwupdplugin/fu-cfi-device.h>
#include <libfwupdplugin/fu-cfu-common.h>
#include <libfwupdplugin/fu-cfu-offer.h>
#include <libfwupdplugin/fu-cfu-payload.h>
#include <libfwupdplugin/fu-chunk.h>
#include <libfwupdplugin/fu-common-guid.h>
#include <libfwupdplugin/fu-common.h>
#include <libfwupdplugin/fu-context.h>
#include <libfwupdplugin/fu-crc.h>
#include <libfwupdplugin/fu-device-locker.h>
#include <libfwupdplugin/fu-device-metadata.h>
#include <libfwupdplugin/fu-device.h>
#include <libfwupdplugin/fu-dfu-firmware.h>
#include <libfwupdplugin/fu-dfuse-firmware.h>
#include <libfwupdplugin/fu-dump.h>
#include <libfwupdplugin/fu-efi-firmware-file.h>
#include <libfwupdplugin/fu-efi-firmware-filesystem.h>
#include <libfwupdplugin/fu-efi-firmware-section.h>
#include <libfwupdplugin/fu-efi-firmware-volume.h>
#include <libfwupdplugin/fu-efi-signature-list.h>
#include <libfwupdplugin/fu-efi-signature.h>
#include <libfwupdplugin/fu-efivar.h>
#include <libfwupdplugin/fu-fdt-firmware.h>
#include <libfwupdplugin/fu-fdt-image.h>
#include <libfwupdplugin/fu-firmware-common.h>
#include <libfwupdplugin/fu-firmware.h>
#include <libfwupdplugin/fu-fit-firmware.h>
#include <libfwupdplugin/fu-fmap-firmware.h>
#include <libfwupdplugin/fu-hid-device.h>
#include <libfwupdplugin/fu-i2c-device.h>
#include <libfwupdplugin/fu-ifd-bios.h>
#include <libfwupdplugin/fu-ifd-firmware.h>
#include <libfwupdplugin/fu-ifd-image.h>
#include <libfwupdplugin/fu-ifwi-cpd-firmware.h>
#include <libfwupdplugin/fu-ifwi-fpt-firmware.h>
#include <libfwupdplugin/fu-ihex-firmware.h>
#include <libfwupdplugin/fu-io-channel.h>
#include <libfwupdplugin/fu-kernel.h>
#include <libfwupdplugin/fu-linear-firmware.h>
#include <libfwupdplugin/fu-mei-device.h>
#include <libfwupdplugin/fu-mem.h>
#include <libfwupdplugin/fu-oprom-firmware.h>
#include <libfwupdplugin/fu-path.h>
#include <libfwupdplugin/fu-plugin-vfuncs.h>
#include <libfwupdplugin/fu-plugin.h>
#include <libfwupdplugin/fu-progress.h>
#include <libfwupdplugin/fu-security-attr.h>
#include <libfwupdplugin/fu-security-attrs.h>
#include <libfwupdplugin/fu-srec-firmware.h>
#include <libfwupdplugin/fu-string.h>
#include <libfwupdplugin/fu-sum.h>
#include <libfwupdplugin/fu-udev-device.h>
#include <libfwupdplugin/fu-usb-device.h>
#include <libfwupdplugin/fu-uswid-firmware.h>
#include <libfwupdplugin/fu-version-common.h>
#include <libfwupdplugin/fu-volume.h>

#ifndef FWUPD_DISABLE_DEPRECATED
#include <libfwupdplugin/fu-deprecated.h>
#endif

#undef __FWUPDPLUGIN_H_INSIDE__
