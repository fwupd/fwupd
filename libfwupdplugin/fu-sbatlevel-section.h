/*
 * Copyright (C) 2023 Canonical Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_SBATLEVEL_SECTION (fu_sbatlevel_section_get_type())
G_DECLARE_DERIVABLE_TYPE(FuSbatlevelSection,
			 fu_sbatlevel_section,
			 FU,
			 SBATLEVEL_SECTION,
			 FuFirmware)

struct _FuSbatlevelSectionClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_sbatlevel_section_new(void);
