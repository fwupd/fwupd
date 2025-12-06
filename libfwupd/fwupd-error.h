/*
 * Copyright 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

#include "fwupd-error-struct.h"

G_BEGIN_DECLS

#define FWUPD_ERROR fwupd_error_quark()

GQuark
fwupd_error_quark(void);
void
fwupd_error_convert(GError **perror);
const gchar *
fwupd_strerror(gint errnum);

G_END_DECLS
