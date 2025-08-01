/*
 * Copyright 2025 Jason Huang <jason.huang@egistec.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

#define OTA_CHALLENGE_SIZE     32
#define HMAC_SHA256_SIZE       32
#define OTA_CHALLENGE_HMAC_KEY "EgistecUsbVcTest"

typedef struct __attribute__((__packed__)) {
	guint8 cla;
	guint8 ins;
	guint8 p1;
	guint8 p2;
	guint8 lc1;
	guint8 lc2;
	guint8 lc3;
} EgisCmdReq;

typedef struct __attribute__((__packed__)) {
	guint8 platform[4];
	guint8 dot1;
	guint8 major_version;
	guint8 dot2;
	guint8 minor_version;
	guint8 dot3;
	guint8 revision[2];
} EgisfpVersionInfo;

typedef struct __attribute__((__packed__)) { /* nocheck:blocked */
	guint8 Sync[4];
	guint8 ID[4];
	guint16 ChkSum;
	guint8 Len[4];
} EgisfpPkgHeader;
