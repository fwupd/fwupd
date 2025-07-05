/*
 * Copyright 2024 B&R Industrial Automation GmbH
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-bnr-dp-struct.h"

gchar *
fu_bnr_dp_version_to_string(guint64 version);

gboolean
fu_bnr_dp_version_from_header(const FuStructBnrDpPayloadHeader *st_header,
			      guint64 *version,
			      GError **error) G_GNUC_NON_NULL(1, 2);

guint32
fu_bnr_dp_effective_product_num(const FuStructBnrDpFactoryData *st_factory_data) G_GNUC_NON_NULL(1);

guint16
fu_bnr_dp_effective_compat_id(const FuStructBnrDpFactoryData *st_factory_data) G_GNUC_NON_NULL(1);
