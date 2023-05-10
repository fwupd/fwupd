/*
 * Copyright (C) 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#ifdef HAVE_HIDRAW_H
#include <linux/hidraw.h>
#include <linux/input.h>
#endif

#include "fu-goodixtp-common.h"

gboolean
get_report(FuDevice *device, guint8 *buf, GError **error)
{
#ifdef HAVE_HIDRAW_H
	guint8 rcv_buf[PACKAGE_LEN + 1] = {0};

	rcv_buf[0] = REPORT_ID;
	if (!fu_udev_device_ioctl((FuUdevDevice *)device,
				  HIDIOCGFEATURE(PACKAGE_LEN),
				  rcv_buf,
				  NULL,
				  GOODIX_DEVICE_IOCTL_TIMEOUT,
				  error)) {
		g_debug("get report failed");
		return FALSE;
	}
	if (rcv_buf[0] != REPORT_ID) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "rcv_buf[0]:%02x != 0x0E",
			    rcv_buf[0]);
		return FALSE;
	}

	memcpy(buf, rcv_buf, PACKAGE_LEN);
	return TRUE;
#else
	g_set_error_literal(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "<linux/hidraw.h> not available");
	return FALSE;
#endif
}

gboolean
set_report(FuDevice *device, guint8 *buf, guint32 len, GError **error)
{
#ifdef HAVE_HIDRAW_H
	if (!fu_udev_device_ioctl((FuUdevDevice *)device,
				  HIDIOCSFEATURE(len),
				  buf,
				  NULL,
				  GOODIX_DEVICE_IOCTL_TIMEOUT,
				  error)) {
		g_debug("Failed set report");
		return FALSE;
	}
	return TRUE;
#else
	g_set_error_literal(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "<linux/hidraw.h> not available");
	return FALSE;
#endif
}
