/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware-image.h"

#define FU_TYPE_BCM57XX_DICT_IMAGE (fu_bcm57xx_dict_image_get_type ())
G_DECLARE_FINAL_TYPE (FuBcm57xxDictImage, fu_bcm57xx_dict_image, FU, BCM57XX_DICT_IMAGE, FuFirmwareImage)

FuFirmwareImage	*fu_bcm57xx_dict_image_new		(void);
void		 fu_bcm57xx_dict_image_set_kind		(FuBcm57xxDictImage	*self,
							 guint8			 kind);
guint8		 fu_bcm57xx_dict_image_get_kind		(FuBcm57xxDictImage	*self);
void		 fu_bcm57xx_dict_image_set_target	(FuBcm57xxDictImage	*self,
							 guint8			 target);
guint8		 fu_bcm57xx_dict_image_get_target	(FuBcm57xxDictImage	*self);
