/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

#define	FU_UNIFYING_DEVICE_VID				0x046d

#define	FU_UNIFYING_DEVICE_PID_RUNTIME			0xc52b
#define	FU_UNIFYING_DEVICE_PID_BOOTLOADER_NORDIC	0xaaaa
#define	FU_UNIFYING_DEVICE_PID_BOOTLOADER_NORDIC_PICO	0xaaae
#define	FU_UNIFYING_DEVICE_PID_BOOTLOADER_TEXAS		0xaaac
#define	FU_UNIFYING_DEVICE_PID_BOOTLOADER_TEXAS_PICO	0xaaad

/* Signed firmware are very long to verify on the device */
#define FU_UNIFYING_DEVICE_TIMEOUT_MS			30000

guint8		 fu_logitech_hidpp_buffer_read_uint8	(const gchar	*str);
guint16		 fu_logitech_hidpp_buffer_read_uint16	(const gchar	*str);

gchar		*fu_logitech_hidpp_format_version	(const gchar	*name,
							 guint8		 major,
							 guint8		 minor,
							 guint16	 build);
