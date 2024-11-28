/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DELL_KESTREL_WTPD (fu_dell_kestrel_wtpd_get_type())
G_DECLARE_FINAL_TYPE(FuDellKestrelWtpd, fu_dell_kestrel_wtpd, FU, DELL_KESTREL_WTPD, FuDevice)

FuDellKestrelWtpd *
fu_dell_kestrel_wtpd_new(FuDevice *proxy);
