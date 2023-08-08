/*
 * Copyright (C) 2023 GN Audio A/S
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_JABRA_GNP_IMAGE (fu_jabra_gnp_image_get_type())
G_DECLARE_FINAL_TYPE(FuJabraGnpImage, fu_jabra_gnp_image, FU, JABRA_GNP_IMAGE, FuFirmware)

FuJabraGnpImage *
fu_jabra_gnp_image_new(void);

gboolean
fu_jabra_gnp_image_parse(FuJabraGnpImage *self,
			 XbNode *n,
			 FuFirmware *firmware_archive,
			 GError **error);
guint32
fu_jabra_gnp_image_get_crc32(FuJabraGnpImage *self);
