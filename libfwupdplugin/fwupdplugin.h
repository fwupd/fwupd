/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#define __FWUPDPLUGIN_H_INSIDE__

#include <libfwupdplugin/fu-archive.h>
#include <libfwupdplugin/fu-backend.h>
#include <libfwupdplugin/fu-bluez-device.h>
#include <libfwupdplugin/fu-chunk.h>
#include <libfwupdplugin/fu-common-cab.h>
#include <libfwupdplugin/fu-common-guid.h>
#include <libfwupdplugin/fu-common-version.h>
#include <libfwupdplugin/fu-common.h>
#include <libfwupdplugin/fu-context.h>
#include <libfwupdplugin/fu-device-locker.h>
#include <libfwupdplugin/fu-device-metadata.h>
#include <libfwupdplugin/fu-device.h>
#include <libfwupdplugin/fu-dfu-firmware.h>
#include <libfwupdplugin/fu-dfuse-firmware.h>
#include <libfwupdplugin/fu-efi-signature-list.h>
#include <libfwupdplugin/fu-efi-signature.h>
#include <libfwupdplugin/fu-efivar.h>
#include <libfwupdplugin/fu-firmware-common.h>
#include <libfwupdplugin/fu-firmware.h>
#include <libfwupdplugin/fu-fmap-firmware.h>
#include <libfwupdplugin/fu-hid-device.h>
#include <libfwupdplugin/fu-i2c-device.h>
#include <libfwupdplugin/fu-ifd-bios.h>
#include <libfwupdplugin/fu-ifd-firmware.h>
#include <libfwupdplugin/fu-ihex-firmware.h>
#include <libfwupdplugin/fu-io-channel.h>
#include <libfwupdplugin/fu-plugin-vfuncs.h>
#include <libfwupdplugin/fu-plugin.h>
#include <libfwupdplugin/fu-progress.h>
#include <libfwupdplugin/fu-security-attrs.h>
#include <libfwupdplugin/fu-srec-firmware.h>
#include <libfwupdplugin/fu-udev-device.h>
#include <libfwupdplugin/fu-usb-device.h>
#include <libfwupdplugin/fu-volume.h>

#ifndef FWUPD_DISABLE_DEPRECATED
#include <libfwupdplugin/fu-deprecated.h>
#endif

#undef __FWUPDPLUGIN_H_INSIDE__
