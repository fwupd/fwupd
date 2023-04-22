/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#define FU_WAC_PACKET_LEN 512

#define FU_WAC_REPORT_ID_COMMAND 0x01
#define FU_WAC_REPORT_ID_STATUS	 0x02
#define FU_WAC_REPORT_ID_CONTROL 0x03

#define FU_WAC_REPORT_ID_GET_FIRMWARE_VERSION_MAIN	0x07
#define FU_WAC_REPORT_ID_GET_FIRMWARE_VERSION_TOUCH	0x07
#define FU_WAC_REPORT_ID_GET_FIRMWARE_VERSION_BLUETOOTH 0x16

void
fu_wac_buffer_dump(const gchar *title, guint8 cmd, const guint8 *buf, gsize sz);
