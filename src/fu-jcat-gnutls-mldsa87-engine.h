/*
 * Copyright (C) 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-jcat-context.h"
#include "fu-jcat-engine.h"

#define FU_TYPE_JCAT_GNUTLS_MLDSA87_ENGINE (fu_jcat_gnutls_mldsa87_engine_get_type())
G_DECLARE_FINAL_TYPE(FuJcatGnutlsMldsa87Engine,
		     fu_jcat_gnutls_mldsa87_engine,
		     FU,
		     JCAT_GNUTLS_MLDSA87_ENGINE,
		     FuJcatEngine)

FuJcatEngine *
fu_jcat_gnutls_mldsa87_engine_new(FuJcatContext *context) G_GNUC_NON_NULL(1);
