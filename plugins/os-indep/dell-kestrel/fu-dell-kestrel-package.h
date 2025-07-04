/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DELL_KESTREL_PACKAGE (fu_dell_kestrel_package_get_type())
G_DECLARE_FINAL_TYPE(FuDellKestrelPackage,
		     fu_dell_kestrel_package,
		     FU,
		     DELL_KESTREL_PACKAGE,
		     FuDevice)

FuDellKestrelPackage *
fu_dell_kestrel_package_new(FuDevice *proxy);
