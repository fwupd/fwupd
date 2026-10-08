/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-bluetooth-proxy.h"

G_BEGIN_DECLS

#define FU_TYPE_TEST_BLUETOOTH_PROXY (fu_test_bluetooth_proxy_get_type())
G_DECLARE_FINAL_TYPE(FuTestBluetoothProxy,
		     fu_test_bluetooth_proxy,
		     FU,
		     TEST_BLUETOOTH_PROXY,
		     FuBluetoothProxy)

FuTestBluetoothProxy *
fu_test_bluetooth_proxy_new(void);

G_END_DECLS
