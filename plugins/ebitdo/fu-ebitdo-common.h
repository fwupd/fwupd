/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

/* little endian */
typedef struct __attribute__((packed)) {
	guint8		pkt_len;
	guint8		type;			/* FuEbitdoPktType */
	guint8		subtype;		/* FuEbitdoPktCmd */
	guint16		cmd_len;
	guint8		cmd;			/* FuEbitdoPktCmd */
	guint16		payload_len;		/* optional */
} FuEbitdoPkt;

#define FU_EBITDO_USB_TIMEOUT			5000	/* ms */
#define FU_EBITDO_USB_BOOTLOADER_EP_IN		0x82
#define FU_EBITDO_USB_BOOTLOADER_EP_OUT		0x01
#define FU_EBITDO_USB_RUNTIME_EP_IN		0x81
#define FU_EBITDO_USB_RUNTIME_EP_OUT		0x02
#define FU_EBITDO_USB_EP_SIZE			64	/* bytes */

typedef enum {
	FU_EBITDO_PKT_TYPE_USER_CMD		= 0x00,
	FU_EBITDO_PKT_TYPE_USER_DATA		= 0x01,
	FU_EBITDO_PKT_TYPE_MID_CMD		= 0x02,
	FU_EBITDO_PKT_TYPE_LAST
} FuEbitdoPktType;

/* commands */
typedef enum {
	FU_EBITDO_PKT_CMD_FW_UPDATE_DATA	= 0x00, /* update firmware data */
	FU_EBITDO_PKT_CMD_FW_UPDATE_HEADER	= 0x01, /* update firmware header */
	FU_EBITDO_PKT_CMD_FW_UPDATE_OK		= 0x02, /* mark update as successful */
	FU_EBITDO_PKT_CMD_FW_UPDATE_ERROR	= 0x03, /* update firmware error */
	FU_EBITDO_PKT_CMD_FW_GET_VERSION	= 0x04, /* get cur firmware vision */
	FU_EBITDO_PKT_CMD_FW_SET_VERSION	= 0x05, /* set firmware version */
	FU_EBITDO_PKT_CMD_FW_SET_ENCODE_ID	= 0x06, /* set app firmware encode ID */
	FU_EBITDO_PKT_CMD_ACK			= 0x14, /* acknowledge */
	FU_EBITDO_PKT_CMD_NAK			= 0x15, /* negative acknowledge */
	FU_EBITDO_PKT_CMD_UPDATE_FIRMWARE_DATA	= 0x16, /* update firmware data */
	FU_EBITDO_PKT_CMD_TRANSFER_ABORT	= 0x18, /* aborts transfer */
	FU_EBITDO_PKT_CMD_VERIFICATION_ID	= 0x19, /* verification id (only BT?) */
	FU_EBITDO_PKT_CMD_GET_VERIFICATION_ID	= 0x1a, /* verification id (only BT) */
	FU_EBITDO_PKT_CMD_VERIFY_ERROR		= 0x1b, /* verification error */
	FU_EBITDO_PKT_CMD_VERIFY_OK		= 0x1c, /* verification successful */
	FU_EBITDO_PKT_CMD_TRANSFER_TIMEOUT	= 0x1d, /* send or receive data timeout */
	FU_EBITDO_PKT_CMD_GET_VERSION		= 0x21, /* get fw ver, joystick mode */
	FU_EBITDO_PKT_CMD_GET_VERSION_RESPONSE	= 0x22, /* get fw version response */
	FU_EBITDO_PKT_CMD_FW_LAST
} FuEbitdoPktCmd;

const gchar	*fu_ebitdo_pkt_cmd_to_string	(FuEbitdoPktCmd		 cmd);
const gchar	*fu_ebitdo_pkt_type_to_string	(FuEbitdoPktType	 type);
void		 fu_ebitdo_dump_pkt		(FuEbitdoPkt		*hdr);
