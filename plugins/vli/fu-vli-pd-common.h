/*
 * Copyright (C) 2017-2019 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#include "fu-vli-common.h"

typedef struct __attribute__ ((packed)) {
	guint32		fwver;	/* BE */
	guint16		vid;	/* LE */
	guint16		pid;	/* LE */
} FuVliPdHdr;

FuVliDeviceKind	 fu_vli_pd_common_guess_device_kind		(guint32		 fwver);
