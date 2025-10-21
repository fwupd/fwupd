/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: Use FuFirmwareClass->convert_version rather than fu_firmware_set_version
 */

static void
fu_firmware_convert_version_test(void)
{
	fu_firmware_set_version_raw(firmware, 0x123);
	fu_firmware_set_version(firmware, "1.2.3");
}
