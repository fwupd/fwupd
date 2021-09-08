/*
 * Copyright (C) 2021 Michael Cheng <michael.cheng@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

//#include <glib.h>
#include <fwupdplugin.h>

typedef struct _S2FIDENTITY {
	guint32 Tag;
	guint32 FormatVersion;
	guint32 ICID;
	guint32 Reserve;
} S2FIDENTITY;

typedef struct _S2FINDEX {
	guint32 Type;
	guint32 Reserve;
	guint32 StartAddress;
	guint32 Length;
} S2FINDEX;

typedef struct _S2FFILE {
	S2FIDENTITY S2FHeader;
	guint8 Tag[2][2];
	guint8 *pOffer[2];
	guint8 *pPayload[2];
	guint32 OfferLength[2];
	guint32 PayloadLength[2];
} S2FFILE;

typedef struct _PAYLOAD_HEADER {
	guint32 Address;
	guint8 Length;
} PAYLOAD_HEADER;

gboolean
binary_verify(const guint8 *pbinary, gsize binary_size, S2FFILE *ps2ffile, GError **error);
