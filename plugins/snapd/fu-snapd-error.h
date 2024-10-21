/*
 * Copyright 2024 Maciej Borzecki <maciej.borzecki@canonical.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

#define FU_SNAPD_ERROR fu_snapd_error_quark()

/**
 * FuSnapdError:
 *
 * The error code.
 **/
typedef enum {
	FU_SNAPD_ERROR_INTERNAL,
	FU_SNAPD_ERROR_UNSUPPORTED,
	/*< private >*/
	FU_SNAPD_ERROR_LAST
} FuSnapdError;

GQuark
fu_snapd_error_quark(void);
