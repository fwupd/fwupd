/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

/**
 * SECTION:fwupdplugin
 * @short_description: Helper objects for plugins interacting with fwupd daemon
 */

#define __FWUPDPLUGIN_H_INSIDE__

#include <libfwupdplugin/fu-archive.h>
#include <libfwupdplugin/fu-chunk.h>
#include <libfwupdplugin/fu-common.h>
#include <libfwupdplugin/fu-common-cab.h>
#include <libfwupdplugin/fu-common-guid.h>
#include <libfwupdplugin/fu-common-version.h>
#include <libfwupdplugin/fu-device.h>
#include <libfwupdplugin/fu-device-locker.h>
#include <libfwupdplugin/fu-device-metadata.h>
#include <libfwupdplugin/fu-dfu-firmware.h>
#include <libfwupdplugin/fu-firmware.h>
#include <libfwupdplugin/fu-firmware-common.h>
#include <libfwupdplugin/fu-firmware-image.h>
#include <libfwupdplugin/fu-hwids.h>
#include <libfwupdplugin/fu-ihex-firmware.h>
#include <libfwupdplugin/fu-io-channel.h>
#include <libfwupdplugin/fu-plugin.h>
#include <libfwupdplugin/fu-plugin-vfuncs.h>
#include <libfwupdplugin/fu-quirks.h>
#include <libfwupdplugin/fu-smbios.h>
#include <libfwupdplugin/fu-srec-firmware.h>
#include <libfwupdplugin/fu-udev-device.h>
#include <libfwupdplugin/fu-usb-device.h>

#ifndef FWUPD_DISABLE_DEPRECATED
#include <libfwupdplugin/fu-deprecated.h>
#endif

#undef __FWUPDPLUGIN_H_INSIDE__
