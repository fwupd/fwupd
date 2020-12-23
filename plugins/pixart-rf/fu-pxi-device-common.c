/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 Jeremy Soller <jeremy@system76.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"


#include "fu-pxi-device-common.h"



gboolean
fu_pxi_device_set_feature (FuDevice *self,
                           const guint8 *data,
                           guint datasz,
                           GError **error)
{
        fu_common_dump_raw (G_LOG_DOMAIN, "SetFeature", data, datasz);
        return fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
                                     HIDIOCSFEATURE(datasz), (guint8 *) data,
                                     NULL, error);
}

gboolean
fu_pxi_device_get_hid_raw_info(FuDevice *self,
                               struct hidraw_devinfo *info,
                               GError **error)
{
        if (!fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
                                   HIDIOCGRAWINFO, (guint8 *) info,
                                   NULL, error)) {
                return FALSE;
        }

        g_debug("bustype: %d",(gint32)info->bustype);
        g_debug("vendor: 0x %04hx", info->vendor);
        g_debug("product: 0x%04hx", info->product);

        return TRUE;

}

gboolean
fu_pxi_device_get_feature (FuDevice *self,
                           guint8 *data,
                           guint datasz,
                           GError **error)
{
        if (!fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
                                   HIDIOCGFEATURE(datasz), data,
                                   NULL, error)) {
                return FALSE;
        }
        fu_common_dump_raw (G_LOG_DOMAIN, "GetFeature", data, datasz);
        return TRUE;
}

void
fu_pxi_device_calculate_checksum(gushort* checksum, gsize sz, const guint8* data)
{
        guint32 idx;

        for (idx = 0; idx < sz; idx++) {
                *checksum += (gushort)data[idx];
        }
}




