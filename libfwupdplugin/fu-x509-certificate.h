/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_X509_CERTIFICATE (fu_x509_certificate_get_type())
G_DECLARE_FINAL_TYPE(FuX509Certificate, fu_x509_certificate, FU, X509_CERTIFICATE, FuFirmware)

FuX509Certificate *
fu_x509_certificate_new(void);
const gchar *
fu_x509_certificate_get_issuer(FuX509Certificate *self) G_GNUC_NON_NULL(1);
const gchar *
fu_x509_certificate_get_subject(FuX509Certificate *self) G_GNUC_NON_NULL(1);
