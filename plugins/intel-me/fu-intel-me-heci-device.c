/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-intel-me-heci-device.h"

G_DEFINE_TYPE(FuIntelMeHeciDevice, fu_intel_me_heci_device, FU_TYPE_MKHI_DEVICE)

static void
fu_intel_me_heci_device_version_notify_cb(FuDevice *device, GParamSpec *pspec, gpointer user_data)
{
	if (fu_device_has_private_flag(device, FU_INTEL_ME_HECI_DEVICE_FLAG_LEAKED_KM))
		fu_device_inhibit(device, "leaked-km", "Provisioned with a leaked private key");
}

static void
fu_intel_me_heci_device_init(FuIntelMeHeciDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_icon(FU_DEVICE(self), "computer");
	fu_device_register_private_flag(FU_DEVICE(self), FU_INTEL_ME_HECI_DEVICE_FLAG_LEAKED_KM);
	g_signal_connect(FWUPD_DEVICE(self),
			 "notify::private-flags",
			 G_CALLBACK(fu_intel_me_heci_device_version_notify_cb),
			 NULL);
}

static void
fu_intel_me_heci_device_class_init(FuIntelMeHeciDeviceClass *klass)
{
}
