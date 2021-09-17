/*
 * Copyright (C) 2021 Michael Cheng <michael.cheng@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FW_SET_ID_OFFER_A   "offer_A"
#define FW_SET_ID_OFFER_B   "offer_B"
#define FW_SET_ID_PAYLOAD_A "payload_A"
#define FW_SET_ID_PAYLOAD_B "payload_B"

typedef struct _S2F_HEADER {
	guint32 Tag;
	guint32 FormatVersion;
	guint32 ICID;
	guint32 Reserve;
} S2F_HEADER;

typedef struct _S2F_INDEX {
	guint32 Type;
	guint32 Reserve;
	guint32 StartAddress;
	guint32 Length;
} S2F_INDEX;

typedef struct _S2F_FILE {
	S2F_HEADER S2FHeader;
	guint8 Tag[2][2];
	const guint8 *pOffer[2];
	const guint8 *pPayload[2];
	gsize OfferLength[2];
	gsize PayloadLength[2];
} S2F_FILE;

typedef struct _PAYLOAD_HEADER {
	guint32 Address;
	guint8 Length;
} PAYLOAD_HEADER;

gboolean
fU_elanfp_file_ctrl_binary_verify(FuFirmware *firmware, GError **error);
