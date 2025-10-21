/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: Use FuDeviceClass->convert_version rather than fu_device_set_version
 */

static void
fu_device_convert_version_test(void)
{
	fu_device_set_version_raw(device, 0x123);
	fu_device_set_version(device, "1.2.3");
}
