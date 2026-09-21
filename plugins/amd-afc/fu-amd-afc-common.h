/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#define AFC_REVISION_MAJOR	 1
#define AFC_REVISION_MAJOR_SHIFT 12

#define AFC_CONFIG_STRING_TOKEN (1 << 8)
#define AFC_CONFIG_PATH_MASK	(AFC_CONFIG_STRING_TOKEN - 1)

#define AFC_EFIVAR_GUID "f5c0066d-dd67-4186-bcca-55d7e73ecd56"
#define AFC_EFIVAR_NAME "AmdFwConfig"
#define AFC_EFIVAR_ATTRS                                                                           \
	(FU_EFI_VARIABLE_ATTR_NON_VOLATILE | FU_EFI_VARIABLE_ATTR_BOOTSERVICE_ACCESS |             \
	 FU_EFI_VARIABLE_ATTR_RUNTIME_ACCESS)

#define HII_STRING_LANGUAGE_OFFSET 46

#define IFR_BITFIELD_WIDTH_MASK 0x3f
#define IFR_NUMERIC_VALUE_COUNT 3
#define IFR_OP_LENGTH_MASK	0x7f
#define IFR_OP_SCOPE		0x80
#define IFR_OPTION_DEFAULT	(1 << 4)
#define IFR_TYPE_U64		3
#define IFR_TYPE_WIDTH_MASK	0x03
