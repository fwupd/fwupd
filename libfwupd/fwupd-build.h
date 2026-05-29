/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

/* see https://bugzilla.gnome.org/show_bug.cgi?id=113075 */
#ifndef G_GNUC_NON_NULL
#if !defined(SUPPORTED_BUILD) && !defined(_WIN32) && (__GNUC__ > 3) ||                             \
    (__GNUC__ == 3 && __GNUC_MINOR__ >= 3)
#define G_GNUC_NON_NULL(params...) __attribute__((nonnull(params)))
#else
#define G_GNUC_NON_NULL(params...)
#endif
#endif

#ifndef G_GNUC_FLAG_ENUM
#if __has_attribute(flag_enum)
#define G_GNUC_FLAG_ENUM __attribute__((flag_enum))
#else
#define G_GNUC_FLAG_ENUM
#endif
#endif

#if !GLIB_CHECK_VERSION(2, 70, 0)
#include "fwupd-build-glib-2-70.h"
#endif
#if !GLIB_CHECK_VERSION(2, 72, 0)
#include "fwupd-build-glib-2-72.h"
#endif
#if !GLIB_CHECK_VERSION(2, 76, 0)
#include "fwupd-build-glib-2-76.h"
#endif
#if !GLIB_CHECK_VERSION(2, 80, 0)
#include "fwupd-build-glib-2-80.h"
#endif
