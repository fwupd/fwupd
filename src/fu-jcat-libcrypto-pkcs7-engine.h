/*
 * Copyright (C) 2017-2020 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2025 Colin Kinloch <colin.kinloch@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-jcat-context.h"
#include "fu-jcat-engine.h"

#define FU_TYPE_JCAT_LIBCRYPTO_PKCS7_ENGINE (fu_jcat_libcrypto_pkcs7_engine_get_type())
G_DECLARE_FINAL_TYPE(FuJcatLibcryptoPkcs7Engine,
		     fu_jcat_libcrypto_pkcs7_engine,
		     FU,
		     JCAT_LIBCRYPTO_PKCS7_ENGINE,
		     FuJcatEngine)

FuJcatEngine *
fu_jcat_libcrypto_pkcs7_engine_new(FuJcatContext *context) G_GNUC_NON_NULL(1);
