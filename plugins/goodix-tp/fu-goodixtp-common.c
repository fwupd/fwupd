/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <linux/hidraw.h>
#include <linux/input.h>

#include "fu-goodixtp-common.h"

gboolean
get_report(FuDevice *device, guint8 *buf, GError **error)
{
	guint8 rcv_buf[PACKAGE_LEN + 1] = {0};
	gboolean ret;

	rcv_buf[0] = REPORT_ID;
	g_clear_error(error);
	ret = fu_udev_device_ioctl((FuUdevDevice *)device,
				   HIDIOCGFEATURE(PACKAGE_LEN),
				   rcv_buf,
				   NULL,
				   GOODIX_DEVICE_IOCTL_TIMEOUT,
				   error);
	if (ret && rcv_buf[0] == REPORT_ID) {
		memcpy(buf, rcv_buf, PACKAGE_LEN);
		return TRUE;
	}

	g_debug("Failed get report, rcv_buf[0]:%02x", rcv_buf[0]);
	return FALSE;
}

gboolean
set_report(FuDevice *device, guint8 *buf, guint32 len, GError **error)
{
	gboolean ret;

	g_clear_error(error);
	ret = fu_udev_device_ioctl((FuUdevDevice *)device,
				   HIDIOCSFEATURE(len),
				   buf,
				   NULL,
				   GOODIX_DEVICE_IOCTL_TIMEOUT,
				   error);
	if (ret)
		return TRUE;

	g_debug("Failed set report");
	return ret;
}
