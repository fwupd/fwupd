/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 * Copyright 2019 Synaptics Inc
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

GByteArray *
fu_synaptics_prometheus_reply_new(gsize cmdlen);
gboolean
fu_synaptics_prometheus_error_from_status(guint16 status, GError **error);
