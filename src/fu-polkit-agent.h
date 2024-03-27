/*
 * Copyright 2011 Lennart Poettering <lennart@poettering.net>
 * Copyright 2012 Matthias Klumpp <matthias@tenstral.net>
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

gboolean
fu_polkit_agent_open(GError **error);
void
fu_polkit_agent_close(void);
