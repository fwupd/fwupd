/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef __DFU_ERROR_H
#define __DFU_ERROR_H

#include <glib.h>

#define DFU_ERROR			dfu_error_quark()

/**
 * DfuError:
 * @DFU_ERROR_INTERNAL:				Internal error
 * @DFU_ERROR_VERIFY_FAILED:			Failed to verify write
 * @DFU_ERROR_INVALID_FILE:			Invalid file format
 * @DFU_ERROR_INVALID_DEVICE:			Invalid device type
 * @DFU_ERROR_NOT_FOUND:			Resource not found
 * @DFU_ERROR_NOT_SUPPORTED:			Action was not supported
 * @DFU_ERROR_PERMISSION_DENIED:		Failed due to access permissions
 *
 * The error code.
 **/
typedef enum {
	DFU_ERROR_INTERNAL,			/* Since: 0.5.4 */
	DFU_ERROR_VERIFY_FAILED,		/* Since: 0.5.4 */
	DFU_ERROR_INVALID_FILE,			/* Since: 0.5.4 */
	DFU_ERROR_INVALID_DEVICE,		/* Since: 0.5.4 */
	DFU_ERROR_NOT_FOUND,			/* Since: 0.5.4 */
	DFU_ERROR_NOT_SUPPORTED,		/* Since: 0.5.4 */
	DFU_ERROR_PERMISSION_DENIED,		/* Since: 0.5.4 */
	/*< private >*/
	DFU_ERROR_LAST
} DfuError;

GQuark		 dfu_error_quark			(void);

#endif /* __DFU_ERROR_H */
