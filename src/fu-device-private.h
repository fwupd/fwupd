/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
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

#ifndef __FU_DEVICE_PRIVATE_H
#define __FU_DEVICE_PRIVATE_H

#include <fu-device.h>

G_BEGIN_DECLS

#define fu_device_get_update_filename(d)	fwupd_release_get_filename(fwupd_result_get_release(FWUPD_RESULT(d)))
#define fu_device_set_unique_id(d,v)		fwupd_result_set_unique_id(FWUPD_RESULT(d),v)
#define fu_device_set_update_description(d,v)	fwupd_release_set_description(fwupd_result_get_release(FWUPD_RESULT(d)),v)
#define fu_device_set_update_filename(d,v)	fwupd_release_set_filename(fwupd_result_get_release(FWUPD_RESULT(d)),v)
#define fu_device_set_update_homepage(d,v)	fwupd_release_set_homepage(fwupd_result_get_release(FWUPD_RESULT(d)),v)
#define fu_device_set_update_id(d,v)		fwupd_release_set_appstream_id(fwupd_result_get_release(FWUPD_RESULT(d)),v)
#define fu_device_set_update_license(d,v)	fwupd_release_set_license(fwupd_result_get_release(FWUPD_RESULT(d)),v)
#define fu_device_set_update_name(d,v)		fwupd_release_set_name(fwupd_result_get_release(FWUPD_RESULT(d)),v)
#define fu_device_set_update_remote_id(d,v)	fwupd_release_set_remote_id(fwupd_result_get_release(FWUPD_RESULT(d)),v)
#define fu_device_set_update_summary(d,v)	fwupd_release_set_summary(fwupd_result_get_release(FWUPD_RESULT(d)),v)
#define fu_device_set_update_uri(d,v)		fwupd_release_set_uri(fwupd_result_get_release(FWUPD_RESULT(d)),v)
#define fu_device_set_update_vendor(d,v)	fwupd_release_set_vendor(fwupd_result_get_release(FWUPD_RESULT(d)),v)
#define fu_device_set_update_version(d,v)	fwupd_release_set_version(fwupd_result_get_release(FWUPD_RESULT(d)),v)

gchar		*fu_device_to_string			(FuDevice	*device);

G_END_DECLS

#endif /* __FU_DEVICE_PRIVATE_H */

