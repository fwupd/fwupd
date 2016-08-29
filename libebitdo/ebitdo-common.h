/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __EBITDO_COMMON_H
#define __EBITDO_COMMON_H

#include <glib.h>

G_BEGIN_DECLS

/* little endian */
typedef struct __attribute__((packed)) {
	guint32		version;
	guint32		destination_addr;
	guint32		destination_len;
	guint32		reserved[4];
} EbitdoFirmwareHeader;

/* little endian */
typedef struct __attribute__((packed)) {
	guint8		pkt_len;
	guint8		type;			/* EbitdoPktType */
	guint8		subtype;		/* EbitdoPktCmd */
	guint16		cmd_len;
	guint8		cmd;			/* EbitdoPktCmd */
	guint16		payload_len;		/* optional */
} EbitdoPkt;

#define EBITDO_USB_TIMEOUT			5000	/* ms */
#define EBITDO_USB_BOOTLOADER_EP_IN		0x82
#define EBITDO_USB_BOOTLOADER_EP_OUT		0x01
#define EBITDO_USB_RUNTIME_EP_IN		0x81
#define EBITDO_USB_RUNTIME_EP_OUT		0x02
#define EBITDO_USB_EP_SIZE			64	/* bytes */

typedef enum {
	EBITDO_PKT_TYPE_USER_CMD		= 0x00,
	EBITDO_PKT_TYPE_USER_DATA		= 0x01,
	EBITDO_PKT_TYPE_MID_CMD			= 0x02,
	EBITDO_PKT_TYPE_LAST
} EbitdoPktType;

/* commands */
typedef enum {
	EBITDO_PKT_CMD_FW_UPDATE_DATA		= 0x00, /* update firmware data */
	EBITDO_PKT_CMD_FW_UPDATE_HEADER		= 0x01, /* update firmware header */
	EBITDO_PKT_CMD_FW_UPDATE_OK		= 0x02, /* mark update as successful */
	EBITDO_PKT_CMD_FW_UPDATE_ERROR		= 0x03, /* update firmware error */
	EBITDO_PKT_CMD_FW_GET_VERSION		= 0x04, /* get cur firmware vision */
	EBITDO_PKT_CMD_FW_SET_VERSION		= 0x05, /* set firmware version */
	EBITDO_PKT_CMD_FW_SET_ENCODE_ID		= 0x06, /* set app firmware encode ID */
	EBITDO_PKT_CMD_ACK			= 0x14, /* acknowledge */
	EBITDO_PKT_CMD_NAK			= 0x15, /* negative acknowledge */
	EBITDO_PKT_CMD_UPDATE_FIRMWARE_DATA	= 0x16, /* update firmware data */
	EBITDO_PKT_CMD_TRANSFER_ABORT		= 0x18, /* aborts transfer */
	EBITDO_PKT_CMD_VERIFICATION_ID		= 0x19, /* verification id (only BT?) */
	EBITDO_PKT_CMD_GET_VERIFICATION_ID	= 0x1a, /* verification id (only BT) */
	EBITDO_PKT_CMD_VERIFY_ERROR		= 0x1b, /* verification error */
	EBITDO_PKT_CMD_VERIFY_OK		= 0x1c, /* verification successful */
	EBITDO_PKT_CMD_TRANSFER_TIMEOUT		= 0x1d, /* send or recieve data timeout */
	EBITDO_PKT_CMD_GET_VERSION		= 0x21, /* get fw ver, joystick mode */
	EBITDO_PKT_CMD_GET_VERSION_RESPONSE	= 0x22, /* get fw version response */
	EBITDO_PKT_CMD_FW_LAST
} EbitdoPktCmd;

const gchar	*ebitdo_pkt_cmd_to_string	(EbitdoPktCmd		 cmd);
const gchar	*ebitdo_pkt_type_to_string	(EbitdoPktType		 type);
void		 ebitdo_dump_firmware_header	(EbitdoFirmwareHeader	*hdr);
void		 ebitdo_dump_pkt		(EbitdoPkt		*hdr);
void		 ebitdo_dump_raw		(const gchar		*title,
						 const guint8		*data,
						 gsize			 len);

#endif /* __EBITDO_COMMON_H */
