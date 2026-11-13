/*
 * Copyright 2023 Adam.Chen <Adam.Chen@genesyslogic.com.tw>
 * Copyright 2021 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-genesys-struct.h"

typedef struct {
	guint8 N[0x206];
	guint8 E[0x00c];
} FuGenesysPublicKey;

typedef struct {
	guint8 reg;
	guint8 expected_val;
} FuGenesysWaitFlashRegisterHelper;

typedef struct {
	FuGenesysIspModel model;
	gint32 revision;
} FuGenesysChip;

#define GENESYS_USBHUB_CODE_SIZE_OFFSET 0xFB
#define GENESYS_USBHUB_VERSION_OFFSET	0x10E
