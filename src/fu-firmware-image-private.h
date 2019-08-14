/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware-image.h"

G_BEGIN_DECLS

void		 fu_firmware_image_add_string	(FuFirmwareImage	*self,
						 guint			 idt,
						 GString		*str);

G_END_DECLS
