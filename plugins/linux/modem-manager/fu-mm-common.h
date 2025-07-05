/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include <libmm-glib.h>

const gchar *
fu_mm_device_port_type_to_string(MMModemPortType port_type);
MMModemPortType
fu_mm_device_port_type_from_string(const gchar *port_type);
