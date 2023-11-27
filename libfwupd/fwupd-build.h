/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

#if !defined(__FWUPD_H_INSIDE__) && !defined(FWUPD_COMPILATION)
#error "Only <fwupd.h> can be included directly."
#endif

/* see https://bugzilla.gnome.org/show_bug.cgi?id=113075 */
#ifndef G_GNUC_NON_NULL
#if !defined(_WIN32) && (__GNUC__ > 3) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 3)
#define G_GNUC_NON_NULL(params...) __attribute__((nonnull(params)))
#else
#define G_GNUC_NON_NULL(params...)
#endif
#endif
