/*
 * Copyright (C) 2021 Michael Cheng <michael.cheng@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "fu-elanfp-file-control.h"

#define SIZE_IDENTIFY_PACKET 16

#define S2F_TAG_FIRMWAREVERSION 0x00
#define S2F_TAG_CFU_OFFER_A	0x72
#define S2F_TAG_CFU_OFFER_B	0x73
#define S2F_TAG_CFU_PAYLOAD_A	0x74
#define S2F_TAG_CFU_PAYLOAD_B	0x75
#define S2F_TAG_END_OF_INDEX	0xFF

gboolean
binaryVerify(const guint8 *pbinary, gsize binary_size, S2FFILE *ps2ffile, GError **error)
{
	S2FINDEX *ps2findex = NULL;
	guint8 *pindex = NULL;
	gboolean result = TRUE;

	if (pbinary == NULL)
		return FALSE;

	ps2ffile->S2FHeader.Tag = *(guint32 *)pbinary;
	ps2ffile->S2FHeader.FormatVersion = *(guint32 *)(pbinary + 4);
	ps2ffile->S2FHeader.ICID = *(guint32 *)(pbinary + 8);
	ps2ffile->S2FHeader.Reserve = *(guint32 *)(pbinary + 12);

	if (ps2ffile->S2FHeader.Tag != 0x46325354)
		return FALSE;

	ps2ffile->Tag[0][0] = 0x41;
	ps2ffile->Tag[0][1] = 0x00;
	ps2ffile->Tag[1][0] = 0x42;
	ps2ffile->Tag[1][1] = 0x00;

	pindex = (guint8 *)pbinary + SIZE_IDENTIFY_PACKET;

	ps2findex = (S2FINDEX *)pindex;

	while (pindex < (pbinary + binary_size)) {
		switch (ps2findex->Type) {
		case S2F_TAG_CFU_OFFER_A:

			ps2ffile->pOffer[0] = (guint8 *)pbinary + ps2findex->StartAddress;
			ps2ffile->OfferLength[0] = ps2findex->Length;
			break;
		case S2F_TAG_CFU_OFFER_B:

			ps2ffile->pOffer[1] = (guint8 *)pbinary + ps2findex->StartAddress;
			ps2ffile->OfferLength[1] = ps2findex->Length;
			break;
		case S2F_TAG_CFU_PAYLOAD_A:

			ps2ffile->pPayload[0] = (guint8 *)pbinary + ps2findex->StartAddress;
			ps2ffile->PayloadLength[0] = ps2findex->Length;
			break;
		case S2F_TAG_CFU_PAYLOAD_B:

			ps2ffile->pPayload[1] = (guint8 *)pbinary + ps2findex->StartAddress;
			ps2ffile->PayloadLength[1] = ps2findex->Length;
			break;
		case S2F_TAG_END_OF_INDEX:
			goto Exit;

			break;
		default:;
		}

		ps2findex++;

		pindex += sizeof(S2FINDEX);
	}

Exit:

	return result;
}
