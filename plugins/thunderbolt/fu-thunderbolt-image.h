/* -*- mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Intel Corporation.
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __FU_THUNDERBOLT_IMAGE_H__
#define __FU_THUNDERBOLT_IMAGE_H__

#include <glib.h>

typedef enum {
	VALIDATION_PASSED,
	VALIDATION_FAILED,
	UNKNOWN_DEVICE,
} FuPluginValidation;

FuPluginValidation	fu_plugin_thunderbolt_validate_image	(GBytes  *controller_fw,
								 GBytes  *blob_fw,
								 GError **error);

#endif /* __FU_THUNDERBOLT_IMAGE_H__ */
