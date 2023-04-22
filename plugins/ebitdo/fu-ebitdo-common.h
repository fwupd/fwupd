/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

/* little endian */
typedef struct __attribute__((packed)) {
	guint8 pkt_len;
	guint8 type;	/* FuEbitdoPktType */
	guint8 subtype; /* FuEbitdoPktCmd */
	guint16 cmd_len;
	guint8 cmd;	     /* FuEbitdoPktCmd */
	guint16 payload_len; /* optional */
} FuEbitdoPkt;

#define FU_EBITDO_USB_TIMEOUT		5000 /* ms */
#define FU_EBITDO_USB_BOOTLOADER_EP_IN	0x82
#define FU_EBITDO_USB_BOOTLOADER_EP_OUT 0x01
#define FU_EBITDO_USB_RUNTIME_EP_IN	0x81
#define FU_EBITDO_USB_RUNTIME_EP_OUT	0x02
#define FU_EBITDO_USB_EP_SIZE		64 /* bytes */

void
fu_ebitdo_dump_pkt(FuEbitdoPkt *hdr);
