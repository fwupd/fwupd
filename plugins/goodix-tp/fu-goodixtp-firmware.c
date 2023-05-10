/*
 * Copyright (C) 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-goodixtp-firmware.h"

typedef struct {
	FuFirmwareClass parent_instance;
	guint32 version;
} FuGoodixtpFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuGoodixtpFirmware, fu_goodixtp_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_goodixtp_firmware_get_instance_private(o))

guint32
fu_goodixtp_firmware_get_version(FuGoodixtpFirmware *self)
{
	FuGoodixtpFirmwarePrivate *priv = GET_PRIVATE(self);
	return priv->version;
}

void
fu_goodixtp_firmware_set_version(FuGoodixtpFirmware *self, guint32 version)
{
	FuGoodixtpFirmwarePrivate *priv = GET_PRIVATE(self);
	priv->version = version;
}

static void
fu_goodixtp_firmware_init(FuGoodixtpFirmware *self)
{
}

static void
fu_goodixtp_firmware_class_init(FuGoodixtpFirmwareClass *klass)
{
}
