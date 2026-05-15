/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fwupd-jcat-blob.h"

#define FU_TYPE_JCAT_RESULT (fu_jcat_result_get_type())
G_DECLARE_FINAL_TYPE(FuJcatResult, fu_jcat_result, FU, JCAT_RESULT, GObject)

gint64
fu_jcat_result_get_timestamp(FuJcatResult *self) G_GNUC_NON_NULL(1);
const gchar *
fu_jcat_result_get_authority(FuJcatResult *self) G_GNUC_NON_NULL(1);
FwupdJcatBlobKind
fu_jcat_result_get_kind(FuJcatResult *self) G_GNUC_NON_NULL(1);
FwupdJcatBlobMethod
fu_jcat_result_get_method(FuJcatResult *self) G_GNUC_NON_NULL(1);
