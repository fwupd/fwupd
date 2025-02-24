/*
 * Copyright 2024-2025 B&R Industrial Automation GmbH
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>
#include <glib/gtypes.h>

#include "fu-bnr-dp-struct.h"

gchar *
fu_bnr_dp_version_to_string(guint64 version);

gboolean
fu_bnr_dp_version_from_header(const FuStructBnrDpPayloadHeader *header,
			      guint64 *version,
			      GError **error);

guint32
fu_bnr_dp_effective_product_num(const FuStructBnrDpFactoryData *factory_data);

guint16
fu_bnr_dp_effective_compat_id(const FuStructBnrDpFactoryData *factory_data);
