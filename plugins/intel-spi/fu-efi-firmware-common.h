/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

gboolean	fu_efi_firmware_parse_sections		(FuFirmware	*firmware,
							 GBytes		*fw,
							 FwupdInstallFlags flags,
							 GError		**error);
GBytes		*fu_efi_firmware_decompress_lzma	(GBytes		*fw,
							 GError		**error);
