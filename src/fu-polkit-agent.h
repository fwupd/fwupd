/*
 * Copyright 2011 Lennart Poettering <lennart@poettering.net>
 * Copyright 2012 Matthias Klumpp <matthias@tenstral.net>
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib-object.h>

#define FU_TYPE_POLKIT_AGENT (fu_polkit_agent_get_type())
G_DECLARE_FINAL_TYPE(FuPolkitAgent, fu_polkit_agent, FU, POLKIT_AGENT, GObject)

FuPolkitAgent *
fu_polkit_agent_new(void);
gboolean
fu_polkit_agent_open(FuPolkitAgent *self, GError **error);
