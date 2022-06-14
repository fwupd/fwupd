/*
 * Copyright (C) 2011 Lennart Poettering <lennart@poettering.net>
 * Copyright (C) 2012 Matthias Klumpp <matthias@tenstral.net>
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

gboolean
fu_polkit_agent_open(GError **error);
void
fu_polkit_agent_close(void);
