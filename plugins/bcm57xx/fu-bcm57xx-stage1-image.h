/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_BCM57XX_STAGE1_IMAGE (fu_bcm57xx_stage1_image_get_type())
G_DECLARE_FINAL_TYPE(FuBcm57xxStage1Image,
		     fu_bcm57xx_stage1_image,
		     FU,
		     BCM57XX_STAGE1_IMAGE,
		     FuFirmware)

FuFirmware *
fu_bcm57xx_stage1_image_new(void);
